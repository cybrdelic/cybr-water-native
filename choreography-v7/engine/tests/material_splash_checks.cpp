#include "../src/fluid.hpp"
using namespace water;
static void require(bool ok,const char*m){if(!ok)throw std::runtime_error(m);}
int main(){try{
    Config c;c.dx=.025f;c.threads=2;c.apic=true;c.whitewater=false;c.reseedInterval=0;c.gravity={0,0,0};c.surfaceTension=.072f;c.pressureTolerance=1e-6f;c.pressureIterations=300;
    Scene sc;sc.lower={-.6f,-.6f,-.6f};sc.upper={.6f,.6f,.6f};sc.solidSdf=[](Vec3){return 100.f;};sc.initialLiquidSdf=[](Vec3 p){return length(p)-.25f;};
    Solver s(sc,c);s.initialize();s.active.clear();
    for(int z=0;z<s.nz;z++)for(int y=0;y<s.ny;y++)for(int x=0;x<s.nx;x++){
        size_t n=s.cellIndex(x,y,z);s.phi.a[n]=length(s.cellPosition(x,y,z))-.25f;s.type[n]=s.phi.a[n]<0?1:0;if(s.type[n]==1)s.active.push_back(n);
    }
    for(int z=0;z<s.nz;z++)for(int y=0;y<s.ny;y++)for(int x=0;x<s.nx;x++){
        size_t n=s.cellIndex(x,y,z);s.surface[n]=s.type[n]==1&&(s.cellType(x-1,y,z)==0||s.cellType(x+1,y,z)==0||s.cellType(x,y-1,z)==0||s.cellType(x,y+1,z)==0||s.cellType(x,y,z-1)==0||s.cellType(x,y,z+1)==0);
    }
    const float dt=.004f;s.updateCapillary(dt);double mean=0;int count=0;
    for(int n:s.active)if(s.surface[n]){mean+=s.capillary[n]*(c.density*c.dx/dt);count++;}mean/=count;
    double expected=2*c.surfaceTension/.25f;double relative=std::abs(mean-expected)/expected;
    require(relative<.12,"Young-Laplace pressure magnitude/sign failed");
    for(auto&g:s.grid){g.value.clear();std::fill(g.valid.begin(),g.valid.end(),1);}
    s.pressureSolve(dt);double maxSpeed=0;for(auto&g:s.grid)for(float v:g.value.a)maxSpeed=std::max(maxSpeed,double(std::abs(v)));
    require(maxSpeed<.0006,"Static curved-interface parasitic velocity too large");
    require(s.stats.divergenceAfterL2<.0002,"Capillary RHS/gradient inconsistency");
    // Complete state transfer, including APIC's affine energy-carrying state.
    for(auto&p:s.particles){for(int j=0;j<9;j++)p.C[j]=float(j+1)*.17f;p.v={.1f,-.3f,.2f};}
    s.particles[0].p.x=s.scene.upper.x-.08f;
    for(size_t i=0;i<s.pressure.size();i++)s.pressure[i]=float(i%17)*.01f;
    auto before=s.particles;auto pressure=s.pressure;Vec3 lower=s.scene.lower;int nx=s.nx,ny=s.ny,nz=s.nz;
    bool grew=s.growOpenDomain(.4f,16);require(grew,"Expansion did not trigger");
    require(before.size()==s.particles.size()&&std::memcmp(before.data(),s.particles.data(),before.size()*sizeof(Particle))==0,"Expansion altered primary/APIC state");
    int ox=std::llround((lower.x-s.scene.lower.x)/c.dx),oy=std::llround((lower.y-s.scene.lower.y)/c.dx),oz=std::llround((lower.z-s.scene.lower.z)/c.dx);
    double pe=0;for(int z=0;z<nz;z++)for(int y=0;y<ny;y++)for(int x=0;x<nx;x++)pe=std::max(pe,double(std::abs(pressure[(z*ny+y)*nx+x]-s.pressure[s.cellIndex(x+ox,y+oy,z+oz)])));
    require(pe==0,"Expansion lost warm-start pressure");
    std::cout<<std::setprecision(12)<<"{\"passed\":true,\"capillary_pressure_Pa\":"<<mean<<",\"analytic_pressure_Pa\":"<<expected<<",\"relative_pressure_error\":"<<relative<<",\"static_sphere_peak_velocity_m_s\":"<<maxSpeed<<",\"capillary_divergence_L2_s_inv\":"<<s.stats.divergenceAfterL2<<",\"domain_growth_preserves_particle_and_affine_bytes\":true,\"domain_growth_pressure_max_error\":"<<pe<<"}\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
