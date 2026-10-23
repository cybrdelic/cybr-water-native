#define main cybr_renderer_main
#include "../src/trace.cpp"
#undef main
using namespace cybr;
static void require(bool v,const char*s){if(!v)throw std::runtime_error(s);}
static void sheet(BVH&b,float y,bool upper){
 V3 a(-.2f,y,-.2f),c(.2f,y,.2f),d(-.2f,y,.2f),e(.2f,y,-.2f),n(0,upper?-1.f:1.f,0);
 if(upper){b.addTriangle(a,e,c,n,n,n,WATER,1);b.addTriangle(a,c,d,n,n,n,WATER,1);}else{b.addTriangle(a,c,e,n,n,n,WATER,1);b.addTriangle(a,d,c,n,n,n,WATER,1);}
}
int main(){try{
 BVH b;sheet(b,0,false);sheet(b,.002f,true);b.addFoam({0,.0003f,0},.001f);b.build();b.orientedFoam=true;b.bindFoamToWater();b.buildFoam();
 require(b.boundFoam==1&&b.unboundFoam==0,"Foam nearest-mesh binding failed");
 float front=b.foamCoverage({0,0,0},{0,1,0}),back=b.foamCoverage({0,.002f,0},{0,-1,0});require(front>0&&back==0,"Foam leaked onto opposite sheet");
 BVH negative;negative.addFoam({0,0,0},.001f);negative.buildFoam();float leak=negative.foamCoverage({0,.002f,0},{0,-1,0});require(leak>front*.8f,"Negative control failed to reproduce old leak");
 BVH both;both.orientedFoam=true;both.addOrientedFoam({0,0,0},.001f,{0,1,0});both.addOrientedFoam({0,.002f,0},.001f,{0,-1,0});both.buildFoam();require(both.foamCoverage({0,0,0},{0,1,0})>0&&both.foamCoverage({0,.002f,0},{0,-1,0})>0,"Opposing lobes cancelled");
 double integral=0;float step=.0005f;for(float x=-.06f;x<.06f;x+=step)for(float z=-.06f;z<.06f;z+=step)integral+=-std::log1p(-b.foamCoverage({x,0,z},{0,1,0}))*step*step;
 double err=std::abs(integral/(PI*1e-6)-1);require(err<.025,"Projected area normalization failed");
 BVH wide;wide.orientedFoam=true;wide.addOrientedFoam({0,0,0},.001f,{0,1,0});wide.addOrientedFoam({3,1,2},.001f,{0,1,0});wide.buildFoam();
 double drift=0;for(int i=0;i<41;i++)for(int j=0;j<41;j++){V3 p((i-20)*.001f,0,(j-20)*.001f);drift=std::max(drift,double(std::abs(b.foamCoverage(p,{0,1,0})-wide.foamCoverage(p,{0,1,0}))));}require(drift<1e-6,"World lattice drift");
 std::cout<<std::setprecision(12)<<"{\"passed\":true,\"same_side_coverage\":"<<front<<",\"opposite_side_coverage\":"<<back<<",\"negative_control_leak\":"<<leak<<",\"projected_area_relative_error\":"<<err<<",\"world_lattice_opacity_error\":"<<drift<<"}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
