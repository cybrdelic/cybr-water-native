// Lossless transport for CYBRMSH1 triangle streams. Vertex identity is 24 raw
// bytes (position + normal), not rounded floats. Original order and all bits are
// recovered on decode. This is storage compression, never geometry reduction.
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using Vertex = std::array<uint32_t,6>;
struct Header {char magic[8];uint32_t version,flags;uint64_t triangles;};
struct Face {uint32_t vertex[3];uint32_t material;};
struct RawFace {Vertex vertices[3];uint32_t material;};
static_assert(sizeof(Header)==24 && sizeof(Vertex)==24 && sizeof(Face)==16 && sizeof(RawFace)==76,"Unexpected ABI");
struct Hash {size_t operator()(const Vertex &v)const noexcept{
 uint64_t h=1469598103934665603ULL;for(uint32_t x:v){h^=x;h*=1099511628211ULL;}return size_t(h^(h>>32));
}};
template<class T>void read(std::istream &f,T &x){f.read(reinterpret_cast<char*>(&x),sizeof(x));if(!f)throw std::runtime_error("Truncated cache");}
template<class T>void write(std::ostream &f,const T &x){f.write(reinterpret_cast<const char*>(&x),sizeof(x));if(!f)throw std::runtime_error("Cache write failure");}
int main(int argc,char**argv){try{
 if(argc!=4)throw std::runtime_error("Usage: mesh_cache_codec pack|unpack INPUT OUTPUT");
 std::ifstream in(argv[2],std::ios::binary);if(!in)throw std::runtime_error("Cannot open input");
 std::ofstream out(argv[3],std::ios::binary);if(!out)throw std::runtime_error("Cannot open output");
 const std::array<char,8> magic={'C','Y','B','R','I','D','X','1'};Header header{};
 if(std::string(argv[1])=="pack"){
  read(in,header);if(std::memcmp(header.magic,"CYBRMSH1",8)||header.version!=1||header.triangles>100000000)throw std::runtime_error("Unsupported native mesh header");
  std::unordered_map<Vertex,uint32_t,Hash> index;index.reserve(size_t(header.triangles));
  std::vector<Vertex> vertices;vertices.reserve(size_t(header.triangles));std::vector<Face> faces;faces.reserve(size_t(header.triangles));
  for(uint64_t i=0;i<header.triangles;i++){
   RawFace raw;read(in,raw);Face face{};face.material=raw.material;
   for(int k=0;k<3;k++){
    auto it=index.find(raw.vertices[k]);if(it==index.end()){
     if(vertices.size()>=std::numeric_limits<uint32_t>::max())throw std::runtime_error("Vertex index overflow");
     uint32_t id=uint32_t(vertices.size());vertices.push_back(raw.vertices[k]);index.emplace(raw.vertices[k],id);face.vertex[k]=id;
    }else face.vertex[k]=it->second;
   }faces.push_back(face);
  }
  char extra;if(in.get(extra))throw std::runtime_error("Unexpected bytes after native mesh");
  write(out,magic);write(out,header);uint64_t count=vertices.size();write(out,count);
  out.write(reinterpret_cast<const char*>(vertices.data()),std::streamsize(vertices.size()*sizeof(Vertex)));
  out.write(reinterpret_cast<const char*>(faces.data()),std::streamsize(faces.size()*sizeof(Face)));
  if(!out)throw std::runtime_error("Indexed write failed");
  std::cout<<"{\"triangles\":"<<header.triangles<<",\"distinct_bitwise_vertices\":"<<count<<",\"original_bytes\":"<<24+76*header.triangles<<",\"indexed_bytes\":"<<40+24*count+16*header.triangles<<"}\n";
 }else if(std::string(argv[1])=="unpack"){
  std::array<char,8> found;read(in,found);if(found!=magic)throw std::runtime_error("Bad indexed cache magic");
  read(in,header);uint64_t count;read(in,count);
  if(std::memcmp(header.magic,"CYBRMSH1",8)||header.version!=1||header.triangles>100000000||count>300000000)throw std::runtime_error("Invalid indexed header");
  std::vector<Vertex> vertices((size_t(count)));in.read(reinterpret_cast<char*>(vertices.data()),std::streamsize(count*sizeof(Vertex)));if(!in)throw std::runtime_error("Truncated vertex table");
  write(out,header);
  for(uint64_t i=0;i<header.triangles;i++){
   Face face;read(in,face);RawFace raw{};raw.material=face.material;
   for(int k=0;k<3;k++){if(face.vertex[k]>=count)throw std::runtime_error("Invalid vertex index");raw.vertices[k]=vertices[face.vertex[k]];}write(out,raw);
  }
  char extra;if(in.get(extra))throw std::runtime_error("Unexpected indexed trailing data");
 }else throw std::runtime_error("Unknown mode");
 out.close();if(!out)throw std::runtime_error("Final output write failed");return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
