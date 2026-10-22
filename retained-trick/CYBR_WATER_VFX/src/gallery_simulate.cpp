#include <sstream>
// Separate gallery driver: CLI/cache protocol copied from simulate.cpp.
// All fluid evolution remains in the unchanged shared water::Solver.
#include "gallery_scenes.hpp"
#include <thread>

using namespace water;
static std::string quote(const std::string &s){std::string o="\"";for(char c:s){if(c=='\\'||c=='\"')o+='\\';if(c=='\n')o+="\\n";else o+=c;}return o+'\"';}
static uint64_t executableSignature(const std::filesystem::path &path){std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot identify executable for checkpoint compatibility");char buffer[65536];uint64_t h=1469598103934665603ULL;while(f){f.read(buffer,sizeof(buffer));for(std::streamsize i=0;i<f.gcount();i++){h^=uint8_t(buffer[i]);h*=1099511628211ULL;}}return h;}
static void vectorJson(std::ostream &f,Vec3 p){f<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
static void metadata(const Solver &s,const std::filesystem::path &out,double fps,double duration){
    std::ofstream f(out/"manifest.json");auto &c=s.config;
    f<<std::setprecision(12)<<"{\n  \"format\": \"CYBR WATER VFX simulation manifest 1\",\n  \"scene\": "<<quote(s.scene.name)<<",\n  \"solver\": \"staggered MAC free-surface "<<(c.apic?"APIC":"FLIP/PIC")<<" with incomplete-Cholesky PCG pressure\",\n  \"dx_m\": "<<c.dx<<",\n  \"marker_spacing_m\": "<<c.dx/c.particlesPerAxis<<",\n  \"grid\": ["<<s.nx<<','<<s.ny<<','<<s.nz<<"],\n  \"grid_cells\": "<<s.cells<<",\n  \"lower\": ";vectorJson(f,s.scene.lower);f<<",\n  \"upper\": ";vectorJson(f,s.scene.upper);
    f<<",\n  \"fps\": "<<fps<<",\n  \"duration_s\": "<<duration<<",\n  \"threads\": "<<c.threads<<",\n  \"flip_ratio\": "<<c.flipRatio<<",\n  \"pressure_relative_tolerance\": "<<c.pressureTolerance<<",\n  \"pressure_max_iterations\": "<<c.pressureIterations<<",\n  \"cfl\": "<<c.cfl<<",\n  \"seed\": "<<c.seed<<",\n  \"whitewater_rate_m2_s_inv\": "<<c.whitewaterRate<<",\n  \"whitewater_activation_length_m\": "<<c.whitewaterLength<<",\n  \"max_whitewater_parcels\": "<<c.maxWhitewater<<",\n  \"max_primary_markers\": "<<c.maxParticles<<",\n  \"primary_format\": \"CYBRWTR1: 32-byte header; position, velocity, reference density ratio=1, visual surface activity. .ids holds uint32 IDs; .volumes float32 physical marker volumes in m^3. d is marker spacing, h=2d.\",\n  \"secondary_format\": \"CYBRWW01: 32-byte header {magic[8],uint32 version,count,frame,float time,dx,reserved}; records {float x,y,z,vx,vy,vz,radius,age,lifetime,uint32 type}; type0 spray,1 foam,2 bubble\",\n  \"secondary_scope\": \"Flow-derived visual parcels sharing the liquid MAC velocity and reconstructed surface. They do not remove primary marker mass, do not resolve air pressure, and do not predict the physical spray size spectrum. Isolated primary markers retain their volume.\",\n  \"solid_scope\": \"Shared scene SDF sampled at MAC grid nodes; 4x4 face aperture quadrature; cells with solid centers excluded from pressure. Marker contact uses cached trilinear SDF followed by continuous-scene-field refinement near rocks.\",\n  \"reseed_scope\": \"Within-cell marker splits and volume-weighted merges preserve represented volume and linear momentum to floating-point accuracy; no unrecorded bulk-liquid culling.\",\n  \"checkpoint_scope\": \"CYBRFLP4 native little-endian C++ ABI checkpoint, includes marker volumes and affine matrices, secondary states, pressure warm start, emission debt, IDs and ledgers. A stored executable/configuration/cached-SDF fingerprint rejects incompatible resumes.\",\n  \"inlets\": [";
    for(size_t i=0;i<s.scene.inlets.size();i++){if(i)f<<',';auto &in=s.scene.inlets[i];f<<"{\"lower\":";vectorJson(f,in.lower);f<<",\"upper\":";vectorJson(f,in.upper);f<<",\"velocity\":";vectorJson(f,in.velocity);f<<",\"axis\":"<<in.axis<<",\"direction\":"<<in.direction<<",\"rate_scale\":"<<in.rateScale<<",\"open_grid_flux_m3_s\":"<<s.inletFlux[i]<<'}';}
    f<<"]\n}\n";
}
static void csvHeader(std::ostream &f){f<<"step,time,dt,particles,white_spray,white_foam,white_bubble,fluid_cells,volume_m3,initial_volume_m3,emitted_volume_m3,outflow_volume_m3,volume_ledger_error_m3,kinetic_energy_j,max_speed_m_s,pressure_iterations,pressure_relative_residual,pressure_max_flux_residual_m_s,divergence_before_l2_s_inv,divergence_after_l2_s_inv,divergence_after_max_s_inv,initial_particles,emitted_particles,outflow_particles,split_particles,merged_particles,white_emitted,white_expired,white_rejected,collision_projections,transfer_seconds,pressure_seconds,advection_seconds,secondary_seconds,step_seconds,inlet_attempts,inlet_rejected_attempts,requested_rectangular_inlet_volume_m3,prescribed_open_grid_inlet_volume_m3\n";}
static void csvRecord(std::ostream &f,const Solver &s){
    auto &v=s.stats;size_t counts[3]={};for(auto &w:s.white)counts[std::min<uint32_t>(w.type,2)]++;
    f<<std::setprecision(12)<<v.steps<<','<<s.time<<','<<v.dt<<','<<s.particles.size()<<','<<counts[0]<<','<<counts[1]<<','<<counts[2]<<','<<v.fluidCells<<','<<v.volume<<','<<v.initialVolume<<','<<v.emittedVolume<<','<<v.outflowVolume<<','<<v.massLedgerError<<','<<v.kineticEnergy<<','<<v.maxSpeed<<','<<v.pressureIterations<<','<<v.pressureRelativeResidual<<','<<v.pressureAbsoluteResidual<<','<<v.divergenceBeforeL2<<','<<v.divergenceAfterL2<<','<<v.divergenceAfterMax<<','<<v.initialParticles<<','<<v.emittedParticles<<','<<v.outflowParticles<<','<<v.splitParticles<<','<<v.mergedParticles<<','<<v.whiteEmitted<<','<<v.whiteExpired<<','<<v.whiteRejected<<','<<v.collisionProjections<<','<<v.transferSeconds<<','<<v.pressureSeconds<<','<<v.advectionSeconds<<','<<v.secondarySeconds<<','<<v.stepSeconds<<','<<v.inletAttempts<<','<<v.inletRejectedAttempts<<','<<v.requestedInletVolume<<','<<v.prescribedInletVolume<<'\n';f.flush();
}
int main(int argc,char **argv){
    try{
        Config c;double duration=1,fps=24;int saveEvery=1,stopFrame=-1;bool stream=false,skipInitial=false,noCache=false;std::string sceneName="beach";
        float lowerY=std::numeric_limits<float>::quiet_NaN(),upperY=lowerY;
        std::filesystem::path out="water-vfx/cache_gallery",checkpoint,resume;
        for(int i=1;i<argc;i++){
            std::string a=argv[i];auto value=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
            if(a=="--scene")sceneName=value();else if(a=="--dx")c.dx=std::stof(value());else if(a=="--duration")duration=std::stod(value());else if(a=="--fps")fps=std::stod(value());else if(a=="--threads")c.threads=std::stoi(value());
            else if(a=="--out")out=value();else if(a=="--checkpoint")checkpoint=value();else if(a=="--resume")resume=value();else if(a=="--save-every")saveEvery=std::stoi(value());else if(a=="--stop-after-frame")stopFrame=std::stoi(value());
            else if(a=="--flip")c.flipRatio=std::stof(value());else if(a=="--cfl")c.cfl=std::stof(value());else if(a=="--pressure-iterations")c.pressureIterations=std::stoi(value());else if(a=="--pressure-tolerance")c.pressureTolerance=std::stof(value());
            else if(a=="--whitewater-rate")c.whitewaterRate=std::stof(value());else if(a=="--max-whitewater")c.maxWhitewater=std::stoull(value());else if(a=="--max-particles")c.maxParticles=std::stoull(value());else if(a=="--seed")c.seed=std::stoull(value());
            else if(a=="--whitewater-length")c.whitewaterLength=std::stof(value());
            else if(a=="--lower-y")lowerY=std::stof(value());else if(a=="--upper-y")upperY=std::stof(value());
            else if(a=="--particles-per-axis")c.particlesPerAxis=std::stoi(value());else if(a=="--reseed-interval")c.reseedInterval=std::stoi(value());else if(a=="--apic")c.apic=true;else if(a=="--no-whitewater")c.whitewater=false;else if(a=="--stream")stream=true;else if(a=="--skip-initial")skipInitial=true;else if(a=="--no-cache")noCache=true;
            else if(a=="--help"){std::cout<<"gallery_sim --scene beach|waterbending|drop|whirlpool|rockimpact|jets|cascade --dx .025 --duration .7 --fps 24 --threads 2 --out cache [--resume checkpoint.bin] [--stream] [--apic]\nStream protocol: output frame_NNNNNN.ready last; producer waits until consumer removes it before advancing. Primary/white files may be removed by consumer after use. Atomic checkpoint remains current.\n";return 0;}
            else throw std::runtime_error("Unknown argument: "+a);
        }
        if(c.threads<1||c.threads>32||fps<=0||duration<0||saveEvery<1||c.flipRatio<0||c.flipRatio>1)throw std::runtime_error("Invalid command-line configuration");
        c.buildSignature=executableSignature(std::filesystem::exists("/proc/self/exe")?std::filesystem::path("/proc/self/exe"):std::filesystem::path(argv[0]));
        std::filesystem::create_directories(out);if(checkpoint.empty())checkpoint=out/"checkpoint.bin";
        auto preset=gallery::makePreset(sceneName);Scene scene=preset.scene;
        if(std::isfinite(lowerY))scene.lower.y=lowerY;if(std::isfinite(upperY))scene.upper.y=upperY;
        if(scene.upper.y-scene.lower.y<c.dx*4)throw std::runtime_error("Invalid vertical domain crop");
        auto start=std::chrono::steady_clock::now();Solver solver(scene,c);
        if(resume.empty())solver.initialize();else solver.loadCheckpoint(resume);
        metadata(solver,out,fps,duration);
        {
            std::ofstream f(out/"gallery_scene.json");
            f<<std::setprecision(12)<<"{\"scene\":"<<quote(scene.name)<<",\"source_file\":\"src/gallery_scenes.hpp\",\"common_solver\":\"src/fluid.hpp\",\"camera\":";
            vectorJson(f,preset.camera);f<<",\"target\":";vectorJson(f,preset.target);
            f<<",\"vertical_fov_degrees\":"<<preset.verticalFov<<",\"reference_water_level_m\":"<<preset.referenceWaterLevel
             <<",\"material_notes\":"<<quote(preset.materialNotes)<<",\"external_force_notes\":"<<quote(preset.forceNotes)
             <<",\"timing_notes\":"<<quote(preset.timingNotes)<<"}\n";
        }
        {
            std::ofstream f(out/"boundary_conditions.json");
            f<<"{\"order\":[\"x-low\",\"x-high\",\"y-low\",\"y-high\",\"z-low\",\"z-high\"],\"closed_frictionless\":[";
            for(size_t i=0;i<scene.closedBoundaries.size();i++){if(i)f<<',';f<<(scene.closedBoundaries[i]?"true":"false");}
            f<<"],\"implementation\":\"Common Solver zero-normal MAC flux and frictionless marker contact\"}\n";
        }
        bool append=!resume.empty()&&std::filesystem::exists(out/"diagnostics.csv");
        std::ofstream diagnostics(out/"diagnostics.csv",append?std::ios::app:std::ios::out);if(!append)csvHeader(diagnostics);csvRecord(diagnostics,solver);
        std::cout<<"Initialized "<<solver.nx<<'x'<<solver.ny<<'x'<<solver.nz<<" grid, "<<solver.particles.size()<<" markers, "<<solver.stats.volume<<" m3, in "<<Solver::seconds(start)<<" s"<<std::endl;
        std::ofstream retained(out/"retention.csv");
        retained<<"frame,time,particles,volume,initial_volume,outflow_volume,emitted_volume,elevated_05,elevated_08,elevated_11,max_y,max_speed,guide_volume,guide_distance_mean\n";
        auto save=[&](int frame){
            double e05=0,e08=0,e11=0,gv=0,dist=0;float ymax=-1e9;
            gallery::RetainedTrickGuide control;control.prepare(solver.time);
            for(const auto &p:solver.particles){
                if(p.p.y>.5f)e05+=p.volume;if(p.p.y>.8f)e08+=p.volume;if(p.p.y>1.1f)e11+=p.volume;
                ymax=std::max(ymax,p.p.y);
                if(p.p.y>.45f){float d=std::sqrt(control.closest(p.p).d2);dist+=d*p.volume;gv+=p.volume;}
            }
            const auto &st=solver.stats;
            retained<<std::setprecision(12)<<frame<<','<<solver.time<<','<<solver.particles.size()<<','<<st.volume<<','<<st.initialVolume<<','<<st.outflowVolume<<','<<st.emittedVolume<<','<<e05<<','<<e08<<','<<e11<<','<<ymax<<','<<st.maxSpeed<<','<<gv<<','<<(gv?dist/gv:0)<<'\n';retained.flush();
            if(noCache)return;
            solver.writeFrame(out,uint32_t(frame));solver.saveCheckpoint(checkpoint);
            if(stream){
                char name[64];std::snprintf(name,sizeof(name),"frame_%06d.ready",frame);auto ready=out/name;
                std::ofstream f(ready.string()+".tmp");f<<"{\"frame\":"<<frame<<",\"time\":"<<std::setprecision(12)<<solver.time<<",\"particles\":"<<solver.particles.size()<<",\"whitewater\":"<<solver.white.size()<<"}\n";f.close();std::filesystem::rename(ready.string()+".tmp",ready);
                while(std::filesystem::exists(ready)){
                    if(std::filesystem::exists(out/"STOP"))throw std::runtime_error("Producer stopped at consumer request; checkpoint is intact");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        };
        int first=int(std::llround(solver.time*fps)),last=int(std::llround(duration*fps));if(stopFrame>=0)last=std::min(last,stopFrame);
        if(!skipInitial&&first%saveEvery==0)save(first);
        for(int frame=first+1;frame<=last;frame++){
            double target=frame/fps;
            while(solver.time<target-1e-8){
                double rem=target-solver.time,stable=solver.stableDt(rem);int steps=std::max(1,int(std::ceil(rem/stable)));solver.step(float(rem/steps));csvRecord(diagnostics,solver);
            }
            solver.time=target;solver.stats.time=target;
            if(frame%saveEvery==0||frame==last)save(frame);
            auto &v=solver.stats;
            std::cout<<"Frame "<<frame<<" t="<<target<<" N="<<solver.particles.size()<<" WW="<<solver.white.size()<<" pIter="<<v.pressureIterations<<" pRel="<<v.pressureRelativeResidual<<" div="<<v.divergenceAfterL2<<" maxV="<<v.maxSpeed<<" step="<<v.stepSeconds<<"s total="<<Solver::seconds(start)<<"s"<<std::endl;
        }
        if(!noCache)solver.saveCheckpoint(checkpoint);
        std::ofstream complete(out/"complete.json.tmp");complete<<"{\"time\":"<<std::setprecision(12)<<solver.time<<",\"frame\":"<<last<<",\"steps\":"<<solver.stats.steps<<",\"particles\":"<<solver.particles.size()<<",\"whitewater\":"<<solver.white.size()<<",\"wall_seconds\":"<<Solver::seconds(start)<<",\"volume_ledger_error_m3\":"<<solver.stats.massLedgerError<<"}\n";complete.close();std::filesystem::rename(out/"complete.json.tmp",out/"complete.json");
        return 0;
    }catch(const std::exception &e){std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
}
