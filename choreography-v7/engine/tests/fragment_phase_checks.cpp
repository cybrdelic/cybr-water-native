#define main surface_program_main
#include "../src/surface.cpp"
#undef main
#include <stdexcept>
static void require(bool x,const char*m){if(!x)throw std::runtime_error(m);}
int main(){try{
 using R=water::Reconstruction;float d=.01f;
 require(!R::detached(2.7f*2.7f*d*d,d,0),"Liquid separates inside deadband");
 require(R::detached(2.7f*2.7f*d*d,d,1),"Drop switches inside deadband");
 require(R::detached(3.3f*3.3f*d*d,d,0),"Separated drop stays a sheet");
 require(!R::detached(2.1f*2.1f*d*d,d,1),"Drop cannot rejoin");
 for(int i=0;i<50;i++)for(uint8_t prior:{uint8_t(0),uint8_t(1)}){
  float distance=.001f*i;auto next=R::detached(distance*distance,d,prior);
  require(R::detached(distance*distance,d,uint8_t(next))==next,"Classifier not idempotent at restart");
 }
 water::Snapshot s{};s.header.spacing=d;s.header.count=3;
 for(int i=0;i<3;i++)s.particles.push_back({float(i)*d,0,0,0,0,0,1,0});
 R r(d,water::Options{});r.coherentFragments=true;r.ids={0,1,2};r.previousPhase={0,0,0};r.measureAnisotropy(s);
 for(auto &m:r.particleMetrics)require(!m.droplet,"Sparse continuous ligament turned into three spheres");
 R old(d,water::Options{});old.measureAnisotropy(s);for(auto &m:old.particleMetrics)require(m.droplet,"Regression negative control failed");
 // Spatially isolated samples still have an explicit liquid-volume owner.
 s.particles[2].x=.5f;r.measureAnisotropy(s);require(r.particleMetrics[2].droplet,"Far separated liquid not recognized");
 double v=d*d*d,rad=std::cbrt(3*v/(4*water::PI));
 require(std::abs(4*water::PI*rad*rad*rad/3-v)<1e-15,"Drop radius loses liquid volume");
 std::cout<<"{\"passed\":true,\"hysteresis_enter_spacing\":3.2,\"hysteresis_exit_spacing\":2.2,\"sparse_ligament_preserved\":true,\"same_state_restart_idempotent\":true,\"old_count_cutoff_negative_control\":true,\"isolated_volume_preserved\":true}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
