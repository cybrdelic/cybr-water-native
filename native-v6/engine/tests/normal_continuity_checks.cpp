#define main original_render_main
#include "../src/trace.cpp"
#undef main
using namespace cybr;
int main(){double maximum=0;float worstView=0,worstTilt=0,worstAz=0;V3 before,after;uint64_t pairs=0;
 for(float etaI:{1.f,1.333f})for(float view:{5.f,25.f,40.f,48.f,65.f,80.f,88.f})for(float az:{0.f,30.f,65.f,90.f,145.f,180.f}){
  V3 d(std::sin(view*PI/180),0,-std::cos(view*PI/180)),ng(0,0,1),last;bool initial=true;
  for(int j=-8000;j<=8000;j++){
   float a=j*.01f*PI/180,phi=az*PI/180;V3 ns(std::sin(a)*std::cos(phi),std::sin(a)*std::sin(phi),std::cos(a));
   V3 n=admissibleDielectricNormal(d,ns,ng,etaI,etaI==1?1.333f:1.f);
   if(!initial){double step=length(n-last);if(step>maximum){maximum=step;worstView=view;worstTilt=j*.01f;worstAz=az;before=last;after=n;}pairs++;}last=n;initial=false;
  }
 }
 std::cout<<"{\"passed\":"<<(maximum<.002?"true":"false")<<",\"pairs\":"<<pairs<<",\"input_angle_step_degrees\":0.01,\"max_corrected_normal_step_norm\":"<<maximum<<",\"worst_view_deg\":"<<worstView<<",\"worst_normal_tilt_deg\":"<<worstTilt<<",\"worst_azimuth_deg\":"<<worstAz<<"}\n";
 return maximum<.002?0:1;
}
