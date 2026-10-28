#include "../src/fluid.hpp"
using namespace water;
static void require(bool v,const char*s){if(!v)throw std::runtime_error(s);}
static double stateError(const Solver&a,const Solver&b){
 require(a.particles.size()==b.particles.size(),"Particle count mismatch");double e=0;
 for(size_t i=0;i<a.particles.size();i++){auto&x=a.particles[i];auto&y=b.particles[i];require(x.id==y.id&&x.volume==y.volume,"Identity/volume changed");e=std::max(e,double(length(x.p-y.p)));e=std::max(e,double(length(x.v-y.v)));for(int j=0;j<9;j++)e=std::max(e,double(std::abs(x.C[j]-y.C[j])));}return e;
}
int main(){try{
 Config c;c.dx=.08f;c.threads=2;c.apic=true;c.whitewater=false;c.reseedInterval=0;c.buildSignature=54321;
 Scene sc;sc.lower={-.8f,-.16f,-.64f};sc.upper={.8f,1.12f,.64f};sc.solidSdf=[](Vec3 p){return p.y;};sc.initialLiquidSdf=[](Vec3 p){return std::max(p.y-.35f,length(Vec3(p.x,0,p.z))-.5f);};sc.initialVelocity=[](Vec3 p){return Vec3(.15f-p.z,0,p.x);};sc.closedBoundaries[2]=true;
 Solver s(sc,c);s.initialize();s.step(.01f);Solver before=s;require(s.growOpenDomain(.5f,4),"Expected growth");
 Solver reference(s.scene,c);reference.particles=before.particles;reference.white=before.white;reference.stats=before.stats;reference.time=before.time;reference.lastDt=before.lastDt;reference.nextParticleId=before.nextParticleId;reference.nextWhiteId=before.nextWhiteId;reference.inletDebt=before.inletDebt;
 int ox=std::llround((before.scene.lower.x-s.scene.lower.x)/c.dx),oy=std::llround((before.scene.lower.y-s.scene.lower.y)/c.dx),oz=std::llround((before.scene.lower.z-s.scene.lower.z)/c.dx);
 for(int z=0;z<before.nz;z++)for(int y=0;y<before.ny;y++)for(int x=0;x<before.nx;x++)reference.pressure[reference.cellIndex(x+ox,y+oy,z+oz)]=before.pressure[before.cellIndex(x,y,z)];
 require(stateError(s,reference)==0,"Primary/affine preservation failed");s.step(.01f);reference.step(.01f);double replay=stateError(s,reference);require(replay==0,"Releasing scratch changed dynamics");
 auto path=std::filesystem::temp_directory_path()/"cybr_growth_replay.bin";s.saveCheckpoint(path);Solver restored(s.scene,c);restored.loadCheckpoint(path);s.step(.01f);restored.step(.01f);double restart=stateError(s,restored);require(restart==0,"Restart differs");std::filesystem::remove(path);
 std::cout<<std::setprecision(12)<<"{\"passed\":true,\"growth_next_step_max_error\":"<<replay<<",\"checkpoint_next_step_max_error\":"<<restart<<",\"primary_markers\":"<<s.particles.size()<<"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
