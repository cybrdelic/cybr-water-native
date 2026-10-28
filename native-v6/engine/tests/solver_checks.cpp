#include "../src/fluid.hpp"
#include <sstream>
using namespace water;
static Scene scene(){Scene s;s.lower={0,0,0};s.upper={1.6f,1.2f,1.2f};s.solidSdf=[](Vec3 p){return std::min({p.x-.08f,1.52f-p.x,p.y-.08f,p.z-.08f,1.12f-p.z});};s.initialLiquidSdf=[](Vec3 p){return p.y-.62f;};return s;}
int main(){try{
    Config c;c.dx=.1f;c.threads=2;c.whitewater=false;c.reseedInterval=0;c.pressureTolerance=2e-6f;c.pressureIterations=300;
    Solver s(scene(),c);s.initialize();
    double weightError=0,firstMomentError=0,secondMomentError=0;
    for(int sample=0;sample<100;sample++)for(int axis=0;axis<3;axis++){
        Vec3 p=Vec3(.4f,.4f,.4f)+randomVector(sample*13+7)*.15f;double w=0;Vec3 mean;double second[3]={};
        s.stencil(p,axis,true,[&](size_t,float a,Vec3 d){w+=a;mean+=d*a;for(int k=0;k<3;k++)second[k]+=a*d[k]*d[k];});
        weightError=std::max(weightError,std::abs(w-1));firstMomentError=std::max(firstMomentError,double(length(mean)));for(int k=0;k<3;k++)secondMomentError=std::max(secondMomentError,std::abs(second[k]-.25*c.dx*c.dx));
    }
    if(weightError>2e-6||firstMomentError>2e-7||secondMomentError>2e-8)throw std::runtime_error("Quadratic APIC moment identity failed");
    s.addForces(.01f);s.buildPressureSystem(.01f);
    std::vector<float> x(s.cells),y(s.cells),ax(s.cells),ay(s.cells);
    for(int i:s.active){x[i]=random01(i+17)-.5f;y[i]=random01(i+137)-.5f;}
    s.multiply(x,ax);s.multiply(y,ay);double xy=s.inner(x,ay),yx=s.inner(y,ax),energy=s.inner(x,ax);double symmetry=std::abs(xy-yx)/std::max(1.0,std::abs(xy));
    if(symmetry>1e-5||!(energy>0))throw std::runtime_error("Pressure matrix symmetry/energy test failed");
    s.pressureSolve(.01f);double divRatio=s.stats.divergenceAfterL2/std::max(1e-12,s.stats.divergenceBeforeL2);
    if(divRatio>1e-4||s.stats.pressureRelativeResidual>1e-4)throw std::runtime_error("Pressure projection failed divergence reduction");
    for(auto &p:s.particles)p.v={.13f+.2f*p.p.x,-.03f+.1f*p.p.z,.07f-.12f*p.p.y};
    double beforeVolume=s.particleVolume();Vec3 beforeMomentum;for(auto &p:s.particles)beforeMomentum+=p.v*p.volume;
    s.config.maxParticlesPerCell=3;s.reseed();double volumeError=std::abs(s.particleVolume()-beforeVolume);Vec3 afterMomentum;for(auto &p:s.particles)afterMomentum+=p.v*p.volume;
    if(volumeError>1e-7||length(afterMomentum-beforeMomentum)>1e-6)throw std::runtime_error("Conservative marker merge failed");
    s.config.minParticlesPerCell=6;s.config.maxParticlesPerCell=24;s.reseed();double splitError=std::abs(s.particleVolume()-beforeVolume);if(splitError>1e-7)throw std::runtime_error("Conservative marker split failed");
    // Exact executable-state restart includes pressure warm start and emission debt.
    s.config.minParticlesPerCell=3;s.step(.004f);auto checkpoint=std::filesystem::temp_directory_path()/"cybr_flip_solver_check.bin";s.saveCheckpoint(checkpoint);
    Solver resumed(scene(),s.config);resumed.loadCheckpoint(checkpoint);s.step(.004f);resumed.step(.004f);double restart=0;
    if(s.particles.size()!=resumed.particles.size())throw std::runtime_error("Restart marker count mismatch");
    for(size_t i=0;i<s.particles.size();i++)restart=std::max(restart,double(length(s.particles[i].p-resumed.particles[i].p)+length(s.particles[i].v-resumed.particles[i].v)));
    if(restart>2e-6)throw std::runtime_error("Checkpoint continuation mismatch");std::filesystem::remove(checkpoint);
    // APIC mode is advanced through the same pressure and contact core.
    Config apic=c;apic.apic=true;Solver a(scene(),apic);a.initialize();a.step(.004f);if(!std::isfinite(a.stats.maxSpeed)||a.stats.divergenceAfterL2>1e-3)throw std::runtime_error("APIC integrated projection failed");
    // APIC's affine transfer is tested on an actual nonconstant velocity field.
    const std::array<float,9> affine{{.3f,.1f,-.2f,-.1f,.15f,.24f,.17f,-.31f,.08f}};
    auto affineVelocity=[&](Vec3 p){return Vec3{.2f+affine[0]*p.x+affine[1]*p.y+affine[2]*p.z,-.1f+affine[3]*p.x+affine[4]*p.y+affine[5]*p.z,.05f+affine[6]*p.x+affine[7]*p.y+affine[8]*p.z};};
    for(auto &p:a.particles){p.v=affineVelocity(p.p);p.C=affine;}a.transferToGrid();double affineError=0;
    for(int axis=0;axis<3;axis++){auto &f=a.grid[axis];for(int z=3;z<f.value.nz-3;z++)for(int y=3;y<5;y++)for(int x=3;x<f.value.nx-3;x++){
        Vec3 p=a.scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*a.config.dx;size_t n=f.value.index(x,y,z);
        if(f.weight.a[n]>1e-5f&&f.open.a[n]>.999f)affineError=std::max(affineError,double(std::abs(f.value.a[n]-affineVelocity(p)[axis])));
    }}if(affineError>3e-6)throw std::runtime_error("APIC affine field transfer failed");
    // Open boundary stencils are truncated. Both a constant field and a full
    // affine field must reproduce their value and gradient there.
    double boundaryValueError=0,boundaryGradientError=0,constantGradientError=0;
    for(int pass=0;pass<2;pass++){
        for(int axis=0;axis<3;axis++){auto &f=a.grid[axis];for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
            Vec3 p=a.scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*a.config.dx;
            f.value(x,y,z)=pass?affineVelocity(p)[axis]:Vec3(1.45f,-.3f,.12f)[axis];
        }}
        for(int corner=0;corner<8;corner++)for(int inset=0;inset<4;inset++){
            Vec3 p;for(int axis=0;axis<3;axis++)p[axis]=(corner&(1<<axis))?a.scene.upper[axis]-(.003f+.009f*inset):a.scene.lower[axis]+.003f+.009f*inset;
            Vec3 v;std::array<float,9> C;a.gatherAffine(p,v,C);
            boundaryValueError=std::max(boundaryValueError,double(length(v-(pass?affineVelocity(p):Vec3(1.45f,-.3f,.12f)))));
            for(int k=0;k<9;k++)if(pass)boundaryGradientError=std::max(boundaryGradientError,double(std::abs(C[k]-affine[k])));else constantGradientError=std::max(constantGradientError,double(std::abs(C[k])));
        }
    }
    if(boundaryValueError>2e-6||boundaryGradientError>8e-6||constantGradientError>1e-7)throw std::runtime_error("APIC truncated-boundary affine reproduction failed");
    a.particles.clear();Particle parent;parent.p={.54f,.44f,.44f};parent.v=affineVelocity(parent.p);parent.C=affine;parent.volume=.001f;parent.id=a.nextParticleId++;a.particles.push_back(parent);
    a.config.minParticlesPerCell=8;a.config.maxParticlesPerCell=24;a.reseed();double affineSplitError=0;Vec3 splitMomentum;
    for(const auto &p:a.particles){affineSplitError=std::max(affineSplitError,double(length(p.v-affineVelocity(p.p))));splitMomentum+=p.v*p.volume;}
    double affineSplitMomentumError=length(splitMomentum-parent.v*parent.volume);
    if(a.particles.size()!=8||affineSplitError>2e-6||affineSplitMomentumError>1e-9||std::abs(a.particleVolume()-parent.volume)>1e-10)throw std::runtime_error("APIC marker split lost affine field or conserved quantities");
    // A prescribed inlet intersects a curved solid. Only its normal plane is
    // constrained, leaving interior/tangential pressure degrees of freedom.
    Scene inlet=scene();inlet.solidSdf=[](Vec3 p){return std::min({p.y-.08f,p.z-.08f,1.12f-p.z,length(p-Vec3(.04f,.39f,.87f))-.19f});};
    Inlet source;source.lower={0,.20f,.22f};source.upper={.22f,.72f,.98f};source.velocity={1.2f,0,0};inlet.inlets.push_back(source);
    Solver b(inlet,c);b.initialize();b.step(.005f);double inletDivergence=b.stats.divergenceAfterL2;
    if(inletDivergence>1e-3)throw std::runtime_error("Curved inlet pressure boundary regression failed");
    // Sustained flux through a half-masked source: rejection must advance its
    // own sample counter, not reuse the last accepted particle's ID forever.
    Scene masked=scene();masked.solidSdf=[](Vec3 p){return std::min(p.y-.08f,p.z-.6f);};
    Inlet half;half.lower={0,.2f,.2f};half.upper={.2f,.8f,1.f};half.velocity={1.2f,0,0};masked.inlets.push_back(half);
    Solver m(masked,c);m.initialize();uint64_t previous=0;bool everyIntervalEmitted=true;
    for(int i=0;i<128;i++){m.emit(.005f);everyIntervalEmitted&=m.stats.emittedParticles>previous;previous=m.stats.emittedParticles;}
    double nominal=std::pow(double(c.dx)/c.particlesPerAxis,3);
    double fluxError=std::abs(m.stats.emittedVolume-m.stats.prescribedInletVolume);
    if(!everyIntervalEmitted||m.stats.inletRejectedAttempts<100||m.stats.inletAttempts!=m.stats.emittedParticles+m.stats.inletRejectedAttempts||fluxError>nominal*1.001)throw std::runtime_error("Masked source emission froze or failed its open-flux volume budget");
    m.saveCheckpoint(checkpoint);Solver mr(masked,c);mr.loadCheckpoint(checkpoint);m.emit(.013f);mr.emit(.013f);std::filesystem::remove(checkpoint);
    double emitterRestart=0;if(m.particles.size()!=mr.particles.size()||m.stats.inletAttempts!=mr.stats.inletAttempts)throw std::runtime_error("Emitter checkpoint counter mismatch");
    for(size_t i=0;i<m.particles.size();i++)emitterRestart=std::max(emitterRestart,double(length(m.particles[i].p-mr.particles[i].p)));
    if(emitterRestart!=0)throw std::runtime_error("Emitter checkpoint continuation changed sample positions");
    // Contact refinement is checked against the original curved field, not
    // merely against the discretized grid used as its broad phase.
    double clearance=1;
    for(int i=0;i<100;i++){Vec3 p=Vec3(.04f,.39f,.87f)+normalized(randomVector(i*7+3))*.175f,v{-1,-1,-1};b.projectSolid(p,v,.002f);clearance=std::min(clearance,double(inlet.solidSdf(p)));}
    if(clearance<.00199)throw std::runtime_error("Continuous curved contact clearance failed");
    // Selected finite-domain walls close only their normal MAC flux. Marker
    // contact removes outward normal motion and preserves tangential motion.
    Scene closed=scene();closed.solidSdf=[](Vec3 p){return p.y-.08f;};closed.closedBoundaries={true,false,false,false,true,true};
    Solver wall(closed,c);wall.initialize();wall.step(.005f);double closedFlux=0;
    for(int axis:{0,2}){auto &f=wall.grid[axis];for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
        int coordinate=axis==0?x:z,extent=axis==0?wall.nx:wall.nz;
        if((coordinate==0&&closed.closedBoundaries[2*axis])||(coordinate==extent&&closed.closedBoundaries[2*axis+1]))closedFlux=std::max(closedFlux,double(std::abs(f.open(x,y,z)*f.value(x,y,z))));
    }}
    Vec3 wallPoint{-.02f,.4f,-.02f},wallVelocity{-1,.7f,-2};wall.projectSolid(wallPoint,wallVelocity,.002f);
    if(closedFlux!=0||wall.outside(wallPoint)||wallVelocity.x!=0||wallVelocity.z!=0||wallVelocity.y!=.7f)throw std::runtime_error("Slip domain boundary leaked flux or changed tangential contact velocity");
    Scene changed=closed;changed.closedBoundaries[0]=false;Solver changedBoundary(changed,c);if(changedBoundary.stateSignature==wall.stateSignature)throw std::runtime_error("Domain boundary flags are absent from checkpoint fingerprint");
    bool mismatchRejected=false;Config wrong=s.config;wrong.flipRatio=.85f;Solver incompatible(scene(),wrong);s.saveCheckpoint(checkpoint);try{incompatible.loadCheckpoint(checkpoint);}catch(const std::runtime_error&){mismatchRejected=true;}std::filesystem::remove(checkpoint);
    if(!mismatchRejected)throw std::runtime_error("Incompatible checkpoint was accepted");
    std::cout<<std::setprecision(12)<<"{\"passed\":true,\"quadratic_weight_error\":"<<weightError<<",\"quadratic_first_moment_error_m\":"<<firstMomentError<<",\"quadratic_second_moment_error_m2\":"<<secondMomentError<<",\"pressure_symmetry_relative_error\":"<<symmetry<<",\"pressure_quadratic_energy\":"<<energy<<",\"projection_divergence_ratio\":"<<divRatio<<",\"merge_volume_error_m3\":"<<volumeError<<",\"merge_momentum_error_m4_s_inv\":"<<length(afterMomentum-beforeMomentum)<<",\"split_volume_error_m3\":"<<splitError<<",\"restart_max_state_error\":"<<restart<<",\"incompatible_checkpoint_rejected\":true,\"apic_affine_velocity_error_m_s\":"<<affineError<<",\"inlet_divergence_l2_s_inv\":"<<inletDivergence<<",\"continuous_solid_clearance_m\":"<<clearance<<",\"masked_inlet_flux_m3_s\":"<<m.inletFlux[0]<<",\"masked_inlet_attempts\":"<<m.stats.inletAttempts<<",\"masked_inlet_rejected_attempts\":"<<m.stats.inletRejectedAttempts<<",\"masked_inlet_volume_error_m3\":"<<fluxError<<",\"masked_inlet_marker_volume_m3\":"<<nominal<<",\"masked_inlet_emits_every_interval\":true,\"masked_inlet_restart_position_error_m\":"<<emitterRestart<<",";
    std::cout<<std::setprecision(12)<<"\"apic_boundary_value_error_m_s\":"<<boundaryValueError<<",\"apic_boundary_gradient_error_s_inv\":"<<boundaryGradientError<<",\"apic_constant_boundary_gradient_error_s_inv\":"<<constantGradientError<<",\"apic_split_affine_error_m_s\":"<<affineSplitError<<",\"apic_split_momentum_error_m4_s_inv\":"<<affineSplitMomentumError<<",\"closed_domain_normal_flux_m_s\":"<<closedFlux<<",\"closed_domain_tangential_velocity_preserved\":true,\"closed_domain_fingerprint_distinct\":true}\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
