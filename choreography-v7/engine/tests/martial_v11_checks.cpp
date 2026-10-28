#include "../src/gallery_scenes.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace water;
using Guide=choreography::MartialGuideV11;
static void check(bool v,const char *message){if(!v)throw std::runtime_error(message);}

int main() {
  try {
    Guide guide;
    float maxHandVelocityJump=0, maxGuideJump=0;
    float maxTarget=0, maxAcceleration=0;
    const float knotTimes[]={
      .45f,1.10f,1.60f,2.12f,2.65f,3.10f,3.55f,4.05f,
      4.55f,5.20f,5.76f,6.33f,6.90f,7.50f,8.05f,8.55f,8.90f
    };
    for(float t:knotTimes)for(int hand=0;hand<2;hand++){
      float eps=1.e-4f;
      auto l=Guide::hand(hand,t-eps);
      auto m=Guide::hand(hand,t);
      auto r=Guide::hand(hand,t+eps);
      check(finite(l.p)&&finite(m.p)&&finite(r.p),"hand position nonfinite");
      check(finite(l.v)&&finite(m.v)&&finite(r.v),"hand velocity nonfinite");
      maxHandVelocityJump=std::max(maxHandVelocityJump,length(r.v-l.v));
      check(length(r.p-l.p)<.005f,"hand teleport at choreography boundary");
    }
    check(maxHandVelocityJump<.09f,"hand velocity discontinuity");
    for(int i=0;i<=900;i++){
      float t=.01f*i;guide.prepare(t);
      for(int j=0;j<9;j++)for(int k=0;k<5;k++){
        Vec3 p{-.92f+j*.23f,.18f+k*.26f,-.38f+.09f*(j%4)};
        auto q=guide.sample(p,t);
        check(finite(q.target)&&finite(q.acceleration)&&std::isfinite(q.rate),"nonfinite guide");
        check(q.rate>=0.f&&std::isfinite(q.accelerationLimit),"invalid guide rate");
        maxTarget=std::max(maxTarget,length(q.target));
        maxAcceleration=std::max(maxAcceleration,length(q.acceleration));
        if(t>=Guide::releaseEnd-.00001f)
          check(q.rate==0.f&&length(q.target)==0.f&&length(q.acceleration)==0.f,
                "guide not fully released");
      }
    }
    check(maxTarget<=5.201f,"guide exceeded target-speed cap");
    check(maxAcceleration<=9.812f,"guide exceeded acceleration cap");
    const float transitions[]={.25f,.82f,1.06f,1.28f,1.50f,2.02f,2.35f,2.55f,
      3.02f,3.43f,3.61f,4.20f,4.54f,4.68f,5.34f,5.75f,5.92f,
      6.76f,7.14f,7.28f,7.64f,8.01f,8.47f,8.90f};
    for(float t:transitions){
      Guide a,b;a.prepare(t-.0001f);b.prepare(t+.0001f);
      for(int i=0;i<8;i++){
        Vec3 p{-.72f+i*.19f,.46f+i*.095f,-.15f};
        auto qa=a.sample(p,t-.0001f),qb=b.sample(p,t+.0001f);
        // Compare actual applied guidance (rate * target), not target alone:
        // target has no effect when rate has faded to zero.
        maxGuideJump=std::max(maxGuideJump,length(qa.target*qa.rate-qb.target*qb.rate));
      }
    }
    check(maxGuideJump<.8f,"velocity-feedback step at beat boundary");
    auto v11=gallery::makePreset("waterbending_martial");
    auto v10=gallery::makePreset("waterbending_martial_v10");
    auto v6=gallery::makePreset("waterbending_trick");
    auto duet=gallery::makePreset("waterbending_duet");
    check(v11.scene.name=="waterbending_martial_v11","new scene not selected");
    check(v10.scene.name=="waterbending_martial_v10","V10 comparison erased");
    check(v6.scene.name=="open_splash_feedback_waterbending_trick","V6 baseline changed");
    check(duet.scene.name=="waterbending_duet_v7","V7 baseline changed");
    check(v11.scene.inlets.empty()&&!v11.scene.bodyAcceleration&&
          bool(v11.scene.velocityGuide),"fake water source or bypass added");
    check(!v11.scene.closedBoundaries[0]&&!v11.scene.closedBoundaries[1]&&
          !v11.scene.closedBoundaries[4]&&!v11.scene.closedBoundaries[5],
          "invisible container walls introduced");
    // The new initial state must contain only the stationary pool, unlike V10's
    // deliberately pre-seeded airborne crescent. No initial velocity in air.
    for(float x:{-.6f,0.f,.6f})for(float y:{.65f,1.1f,1.5f}){
      Vec3 p{x,y,0};
      check(v11.scene.initialLiquidSdf(p)>0,"new scene seeds airborne liquid");
      check(length(v11.scene.initialVelocity(p))==0,"new scene seeds airborne speed");
    }
    std::cout<<"{\"passed\":true,\"max_hand_velocity_jump_m_s\":"<<maxHandVelocityJump
             <<",\"max_feedback_step\":"<<maxGuideJump
             <<",\"max_target_speed_m_s\":"<<maxTarget
             <<",\"max_feedforward_accel_m_s2\":"<<maxAcceleration
             <<",\"release_end_s\":"<<Guide::releaseEnd
             <<",\"v6_v7_v10_preserved\":true,\"no_airborne_seed\":true}\n";
    return 0;
  }catch(const std::exception &e){std::cerr<<e.what()<<"\n";return 1;}
}
