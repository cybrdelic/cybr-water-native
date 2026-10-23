#define main cybr_original_renderer_main
#include "../src/trace.cpp"
#undef main
using namespace cybr;
static void require(bool condition,const char*text){if(!condition)throw std::runtime_error(text);}
int main(){try{
 double minReflection=1,minTransmission=1,maxFurnaceError=0;uint64_t tested=0;
 for(int direction=0;direction<2;direction++)for(int i=1;i<89;i++)for(int k=-85;k<=85;k++){
  float a=i*PI/180,b=k*PI/180;V3 d=unit(V3(std::sin(a),0,-std::cos(a))),ng(0,0,1),ns(std::sin(b),0,std::cos(b));float ni=direction?1.333f:1.f,nt=direction?1.f:1.333f;
  V3 n=admissibleDielectricNormal(d,ns,ng,ni,nt),wr=reflect(d,n),wt;bool transmit=refract(d,n,ni,nt,wt);
  require(dot(wr,ng)>=-1e-6f,"Reflection crossed the geometric interface");minReflection=std::min(minReflection,double(dot(wr,ng)));
  if(transmit){require(dot(wt,ng)<1e-6f,"Transmission remained on the incident geometric side");minTransmission=std::min(minTransmission,double(-dot(wt,ng)));}
  float f=transmit?fresnel(-dot(d,n),ni,nt):1;require(std::isfinite(f)&&f>=0&&f<=1,"Fresnel energy out of bounds");tested++;
 }
 RNG directionRng(27091);
 for(int i=0;i<50000;i++){
  float c=.0002f+.9996f*directionRng.uniform(),a=2*PI*directionRng.uniform(),z=std::sqrt(1-c*c);
  V3 d(z*std::cos(a),z*std::sin(a),-c),ng(0,0,1),ns=uniformSphere(directionRng.uniform(),directionRng.uniform());
  float ni=i%2?1.333f:1.f,nt=i%2?1.f:1.333f;V3 n=admissibleDielectricNormal(d,ns,ng,ni,nt),wr=reflect(d,n),wt;
  require(dot(wr,ng)>=-2e-6f,"3D reflection crossed the geometric interface");
  if(refract(d,n,ni,nt,wt))require(dot(wt,ng)<2e-6f,"3D transmission remained on incident side");
  tested++;
 }
 BVH b;b.spheres.push_back({V3(0),V3(0),1,WATER,1});b.build();Options o;o.uniformEnvironment=true;o.uniformRadiance=V3(1);o.sunPower=0;o.aperture=0;o.maxDepth=64;o.branchCutoff=1e-7f;o.absorption=V3(0);o.coherentNormals=true;Lighting sky(o,b);
 for(int k=0;k<10;k++){float x=k*.095f;PathStats stats;V3 v=traceClean(Ray({x,0,3},{0,0,-1}),b,sky,o,stats).beauty;maxFurnaceError=std::max(maxFurnaceError,double(length(v-V3(1))));}
 require(maxFurnaceError<.0001,"Dielectric white furnace energy changed");
 Options aa;aa.spp=4;aa.adaptiveAA=16;double mx=0,my=0;bool used[16]={};for(int i=0;i<16;i++){auto q=cameraSample(i,aa);int k=int(q.second*4)*4+int(q.first*4);require(!used[k],"Repeated antialias sample");used[k]=true;mx+=q.first;my+=q.second;}
 require(std::abs(mx/16-.5)<1e-8&&std::abs(my/16-.5)<1e-8,"Pixel sample centroid drift");
 BVH foam;foam.addFoam(V3(0),.001f);foam.buildFoam();double area=0;const float step=.0005f;
 for(float y=-.06f;y<.06f;y+=step)for(float x=-.06f;x<.06f;x+=step){float c=foam.foamCoverage(V3(x,y,0),V3(0,0,1));require(c>=0&&c<1,"Foam coverage invalid");area+=-std::log1p(-c)*step*step;}
 double expected=PI*1e-6,relative=std::abs(area/expected-1);require(relative<.025,"Filtered foam failed projected-area normalization");
 BVH narrow,expanded;narrow.addFoam(V3(.05f,.02f,.03f),.001f);narrow.buildFoam();
 expanded.addFoam(V3(.05f,.02f,.03f),.001f);expanded.addFoam(V3(.6f,.5f,.7f),.001f);expanded.addFoam(V3(-.4f,.2f,-.5f),.001f);expanded.buildFoam();
 double domainError=0;for(int i=0;i<50;i++)for(int j=0;j<50;j++){
  V3 p(.03f+i*.0008f,.02f,.01f+j*.0008f);double a=narrow.foamCoverage(p,V3(0,1,0)),b=expanded.foamCoverage(p,V3(0,1,0));domainError=std::max(domainError,std::abs(a-b));
 }require(domainError<1e-6,"Changing foam bounds moved the filtering lattice");
 std::cout<<std::setprecision(12)<<"{\"passed\":true,\"normal_configurations\":"<<tested<<",\"min_reflection_geometric_cosine\":"<<minReflection<<",\"min_transmission_geometric_cosine\":"<<minTransmission<<",\"max_furnace_error\":"<<maxFurnaceError<<",\"adaptive_sample_locations_unique\":true,\"foam_projected_area_relative_error\":"<<relative<<",\"foam_bound_change_max_opacity_error\":"<<domainError<<"}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
