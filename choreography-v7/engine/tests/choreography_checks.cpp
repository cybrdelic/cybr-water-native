#include "../src/gallery_scenes.hpp"
#include <iostream>
#include <stdexcept>
using namespace water;
using G=choreography::DuetGuide;
static void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try{
 G g;float closure=0,maxV=0,maxA=0,maxTimeJump=0,minThin=1;
 for(int ti=0;ti<=400;ti++){
  float t=ti*.025f;g.prepare(t);
  for(int b=0;b<2;b++){
   closure=std::max(closure,length(G::pose(b,0,t).p-G::pose(b,1,t).p));
   for(int j=0;j<G::segments;j++){
    const auto &k=g.knots[b][j];require(finite(k.p)&&finite(k.v)&&finite(k.a),"nonfinite curve state");
    minThin=std::min(minThin,length(k.thin));
    require(std::abs(dot(k.tangent,k.thin))<.005f,"cross section frame not orthogonal");
    auto q=g.sample(k.p+Vec3(.012f,-.01f,.015f),t);
    require(finite(q.target)&&finite(q.acceleration)&&std::isfinite(q.rate),"nonfinite force");
    maxV=std::max(maxV,length(q.target));maxA=std::max(maxA,length(q.acceleration));
    if(t>=G::releaseEnd)require(q.rate==0&&length(q.target)==0&&length(q.acceleration)==0,"guide persists after release");
   }
  }
 }
 for(float t:{.2f,1.65f,2.10f,3.20f,3.65f,4.32f,4.38f,4.58f,5.1f,5.3f,5.55f,5.75f,6.48f,6.68f,6.62f,7.32f,7.9f,8.66f})
  for(int b=0;b<2;b++)for(int i=0;i<80;i++){
   float u=i/80.f,h=.001f;Vec3 p=G::pose(b,u,t).p;
   Vec3 vl=(p-G::pose(b,u,t-h).p)/h,vr=(G::pose(b,u,t+h).p-p)/h;
   maxTimeJump=std::max(maxTimeJump,length(vl-vr));
  }
 require(closure<4e-6f,"periodic guide closure");require(minThin>.999f,"degenerate ribbon normal");
 require(maxV<=4.60001f&&maxA<=42.0001f,"bounded force contract");
 require(maxTimeJump<.08f,"keyframe velocity discontinuity");
 auto old=gallery::makePreset("waterbending_trick"),p=gallery::makePreset("waterbending_duet");
 require(old.scene.name=="open_splash_feedback_waterbending_trick","V6 preset overwritten");
 require(p.scene.inlets.empty()&&!p.scene.bodyAcceleration&&p.scene.velocityGuide,"source or bypass introduced");
 require(!p.scene.closedBoundaries[0]&&!p.scene.closedBoundaries[1]&&!p.scene.closedBoundaries[4]&&!p.scene.closedBoundaries[5],"hidden lateral walls");
 // Temporal geometry difference: a final loop is not recycled for every beat.
 float minimumBeatDifference=1e9f;
 float times[]={1.8f,3.3f,4.25f,5.25f,6.50f,7.55f,8.70f};
 for(int j=1;j<7;j++){
  float mean=0;for(int b=0;b<2;b++)for(int i=0;i<80;i++)mean+=length(G::pose(b,i/80.f,times[j]).p-G::pose(b,i/80.f,times[j-1]).p)/160.f;
  minimumBeatDifference=std::min(minimumBeatDifference,mean);
 }
 require(minimumBeatDifference>.10f,"poses not distinct");
 std::cout<<"{\"passed\":true,\"closed_curve_max_error_m\":"<<closure<<",\"max_target_speed_m_s\":"<<maxV<<",\"max_feedforward_acceleration_m_s2\":"<<maxA<<",\"transition_velocity_jump_m_s\":"<<maxTimeJump<<",\"minimum_beat_shape_difference_m\":"<<minimumBeatDifference<<",\"release_end_s\":"<<G::releaseEnd<<",\"v6_preset_preserved\":true,\"inlets\":0,\"closed_lateral_faces\":0}\n";
 return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
