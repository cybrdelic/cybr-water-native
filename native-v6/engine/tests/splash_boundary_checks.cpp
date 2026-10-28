#include "../src/gallery_scenes.hpp"
#include <iostream>
#include <stdexcept>
using namespace water;
static void require(bool x,const char* m){if(!x)throw std::runtime_error(m);}
int main(){try{
 auto p=gallery::waterbendingTrickPreset();auto s=p.scene;
 require(!s.closedBoundaries[0]&&!s.closedBoundaries[1]&&!s.closedBoundaries[4]&&!s.closedBoundaries[5],"Hidden lateral wall still enabled");
 require(s.closedBoundaries[2]&&!s.closedBoundaries[3],"Incorrect bottom/top boundary");
 require(s.lower.x<=-3.29f&&s.upper.x>=3.29f&&s.lower.z<=-2.51f&&s.upper.z>=2.51f,"Splash domain not enlarged");
 require(s.inlets.empty(),"New water created by inlet");
 Config c;c.dx=.3f;c.threads=1;c.whitewater=false;c.apic=true;
 Solver solver(s,c);
 // A marker outside an open lateral face must not be clamped/rebounded.
 Vec3 pos(s.upper.x+.04f,.8f,0),vel(2,0,0);solver.projectSolid(pos,vel,.001f);
 require(pos.x>s.upper.x&&vel.x==2.f,"Invisible open-face collision");
 require(solver.outside(pos),"Open outflow cannot be accounted");
 Scene closed=s;closed.closedBoundaries[0]=closed.closedBoundaries[1]=closed.closedBoundaries[4]=closed.closedBoundaries[5]=true;
 Solver old(closed,c);Vec3 oldp(closed.upper.x+.04f,.8f,0),oldv(2,0,.4f);old.projectSolid(oldp,oldv,.001f);
 require(oldp.x<old.scene.upper.x&&oldv.x==0.f&&oldv.z==.4f,"Old closed-wall negative control failed");
 // Same controller remains zero after the intended release.
 gallery::RetainedTrickGuide g;float maxAfter=0;
 for(double t:{8.55,8.75,9.0,9.75,10.0}){
  g.prepare(t);for(int i=0;i<20;i++){
   auto q=g.sample(Vec3(-1.f+.1f*i,.9f,0),t);
   maxAfter=std::max(maxAfter,length(q.acceleration)+q.rate);
  }
 }
 require(maxAfter==0,"Guidance active during free splashdown");
 std::cout<<"{\"passed\":true,\"lateral_faces_open\":true,\"open_marker_not_clamped\":true,\"open_outflow_recognized\":true,\"old_wall_negative_control\":true,\"inlet_count\":0,\"max_post_release_guidance\":"<<maxAfter<<"}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
