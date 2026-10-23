#include "../src/gallery_scenes.hpp"
#include <iostream>
#include <stdexcept>
using namespace water;
static void require(bool v,const char *message){if(!v)throw std::runtime_error(message);}
int main(){try{
    using G=gallery::RetainedTrickGuide;
    G g;float closure=0,maxTarget=0,maxExtra=0;
    for(int k=0;k<=100;k++){
        float t=k*.1f;g.prepare(t);
        for(int b=0;b<2;b++)closure=std::max(closure,length(G::curve(b,0,t)-G::curve(b,1,t)));
        for(int j=0;j<64;j++)for(int b=0;b<2;b++){
            auto p=G::curve(b,float(j)/64,t)+Vec3(.013f,-.017f,.019f);
            auto q=g.sample(p,t);require(finite(q.target)&&finite(q.acceleration),"guide not finite");
            maxTarget=std::max(maxTarget,length(q.target));maxExtra=std::max(maxExtra,length(q.acceleration));
            if(t>=8.55f)require(q.rate==0&&length(q.acceleration)==0,"release is not zero");
        }
    }
    require(closure<2e-6f,"streamline is not closed");require(maxTarget<=3.40001f,"target speed cap failed");
    float transitionJump=0;
    for(float t:{.35f,1.65f,1.7f,2.65f,2.7f,3.65f,4.45f,5.5f,5.85f,6.95f,7.85f,8.55f})
     for(int b=0;b<2;b++)for(int j=0;j<64;j++){
        float u=float(j)/64,dt=.001f;
        auto left=(G::curve(b,u,t)-G::curve(b,u,t-dt))/dt;
        auto right=(G::curve(b,u,t+dt)-G::curve(b,u,t))/dt;
        transitionJump=std::max(transitionJump,length(left-right));
     }
    require(transitionJump<.02f,"curve velocity discontinuity");
    Scene s;s.lower={0,0,0};s.upper={1,1,1};s.solidSdf=[](Vec3){return 1.f;};s.initialLiquidSdf=[](Vec3){return 1.f;};
    s.velocityGuide=[](Vec3,double){VelocityGuide q;q.target={2,-.3f,.5f};q.rate=10;q.accelerationLimit=100;return q;};
    Config c;c.dx=.2f;c.gravity={0,0,0};c.threads=1;c.whitewater=false;
    Solver solver(s,c);
    for(auto &f:solver.grid){std::fill(f.valid.begin(),f.valid.end(),1);std::fill(f.fixed.begin(),f.fixed.end(),0);f.open.clear(1);f.value.clear(0);}
    for(int i=0;i<500;i++)solver.addForces(.01f);
    float feedbackError=0;Vec3 target{2,-.3f,.5f};
    for(int a=0;a<3;a++)for(float v:solver.grid[a].value.a)feedbackError=std::max(feedbackError,std::abs(v-target[a]));
    require(feedbackError<2e-5f,"desired speed keeps accelerating instead of converging");
    auto preset=gallery::waterbendingTrickPreset();require(preset.scene.inlets.empty(),"hidden liquid inlet");
    require(!preset.scene.bodyAcceleration&&bool(preset.scene.velocityGuide),"new feedback path not selected");
    auto legacy=gallery::makePreset("legacy_trick");require(!legacy.scene.velocityGuide&&bool(legacy.scene.bodyAcceleration),"legacy preset changed");
    std::cout<<"{\"passed\":true,\"closed_guide_error_m\":"<<closure<<",\"target_speed_max_m_s\":"<<maxTarget<<",\"feedforward_accel_max_m_s2\":"<<maxExtra<<",\"transition_velocity_jump_m_s\":"<<transitionJump<<",\"feedback_equilibrium_error_m_s\":"<<feedbackError<<",\"inlet_count\":0,\"guidance_off_after_s\":8.55,\"legacy_preserved\":true}\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
