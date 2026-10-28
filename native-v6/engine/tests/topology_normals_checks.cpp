#define main surface_original_main
#include "../src/surface.cpp"
#undef main
using namespace water;
int main(){try{
 using V=Reconstruction::SurfaceVertex;using T=Reconstruction::SurfaceTriangle;
 std::vector<T> m;for(int layer=0;layer<2;layer++)for(int j=0;j<12;j++)for(int i=0;i<12;i++){
  float y=layer*.002f;V a{{i*.01f,y,j*.01f},{0,float(layer? -1:1),0},0},b=a,c=a,d=a;b.p.x+=.01f;c.p.x+=.01f;c.p.z+=.01f;d.p.z+=.01f;
  a.n=unit(a.n+V3(.12f*std::sin(7*i+3*j),0,.12f*std::cos(3*i+7*j)));b.n=c.n=d.n=a.n;
  if(layer){m.push_back({a,b,c});m.push_back({a,c,d});}else{m.push_back({a,c,b});m.push_back({a,d,c});}}
 auto original=m;filterTopologyNormals(m,8);float maxShift=0,minSide=1;
 for(size_t i=0;i<m.size();i++){V before[3]={original[i].a,original[i].b,original[i].c},after[3]={m[i].a,m[i].b,m[i].c};
  for(int k=0;k<3;k++){if(std::memcmp(&before[k].p,&after[k].p,sizeof(V3)))throw std::runtime_error("Normal filtering moved a vertex");maxShift=std::max(maxShift,length(after[k].p-before[k].p));minSide=std::min(minSide,dot(before[k].n,after[k].n));}}
 if(minSide<.95)throw std::runtime_error("Normals mixed across opposite sheets");
 std::cout<<"{\"passed\":true,\"triangles\":"<<m.size()<<",\"opposite_sheet_separation_m\":0.002,\"max_vertex_displacement_m\":"<<maxShift<<",\"min_original_normal_dot\":"<<minSide<<",\"positions_bit_identical\":true}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
