#include "../src/scene.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace water;
template<class T>static T read(std::ifstream &f){T x{};f.read(reinterpret_cast<char *>(&x),sizeof(T));if(!f)throw std::runtime_error("Truncated binary state");return x;}
int main(int argc,char **argv){try{
    if(argc<2)throw std::runtime_error("Usage: state_audit frame.bin [solid_sample_stride=64]");
    std::filesystem::path path=argv[1],base=path;base.replace_extension();int stride=argc>2?std::stoi(argv[2]):64;if(stride<1)throw std::runtime_error("Invalid sample stride");
    std::ifstream f(path,std::ios::binary);char magic[8];f.read(magic,8);uint32_t version=read<uint32_t>(f),count=read<uint32_t>(f),frame=read<uint32_t>(f);float time=read<float>(f),spacing=read<float>(f),h=read<float>(f);
    if(std::memcmp(magic,"CYBRWTR1",8)||version!=1||count==0||!(spacing>0)||std::abs(h-2*spacing)>1e-6||std::filesystem::file_size(path)!=32+uint64_t(count)*32)throw std::runtime_error("Primary header/length mismatch");
    std::ifstream idsFile(base.string()+".ids",std::ios::binary),volFile(base.string()+".volumes",std::ios::binary);
    if(std::filesystem::file_size(base.string()+".ids")!=uint64_t(count)*4||std::filesystem::file_size(base.string()+".volumes")!=uint64_t(count)*4)throw std::runtime_error("Primary sidecar length mismatch");
    std::vector<uint32_t> ids(count);double volume=0,energy=0;Vec3 minimum(1e20f,1e20f,1e20f),maximum(-1e20f,-1e20f,-1e20f);double mx=0,my=0,mz=0,maxV=0,minSolid=1e20;uint64_t solidSamples=0;auto s=makeHeroScene();
    for(uint32_t i=0;i<count;i++){
        float data[8];f.read(reinterpret_cast<char *>(data),sizeof(data));if(!f)throw std::runtime_error("Truncated primary record");
        for(float v:data)if(!std::isfinite(v))throw std::runtime_error("Nonfinite primary value");
        Vec3 p(data[0],data[1],data[2]),v(data[3],data[4],data[5]);float pv=read<float>(volFile);ids[i]=read<uint32_t>(idsFile);
        if(!(pv>0)||!std::isfinite(pv)||ids[i]==0||data[6]!=1)throw std::runtime_error("Invalid primary volume/ID/reference ratio");
        for(int a=0;a<3;a++){minimum[a]=std::min(minimum[a],p[a]);maximum[a]=std::max(maximum[a],p[a]);}
        volume+=pv;energy+=.5*1000*pv*dot(v,v);mx+=p.x*pv;my+=p.y*pv;mz+=p.z*pv;maxV=std::max(maxV,double(length(v)));
        if(i%stride==0){minSolid=std::min(minSolid,double(s.solidSdf(p)));solidSamples++;}
    }
    std::sort(ids.begin(),ids.end());for(size_t i=1;i<ids.size();i++)if(ids[i]==ids[i-1])throw std::runtime_error("Duplicate primary IDs");
    if(minSolid<-.0001)throw std::runtime_error("Sampled primary marker penetrates continuous scene solid by more than0.1mm");
    std::ifstream w(base.string()+".white",std::ios::binary);w.read(magic,8);uint32_t wv=read<uint32_t>(w),wn=read<uint32_t>(w),wf=read<uint32_t>(w);float wt=read<float>(w),wh=read<float>(w),reserved=read<float>(w);(void)wh;(void)reserved;
    if(std::memcmp(magic,"CYBRWW01",8)||wv!=1||wf!=frame||wt!=time||std::filesystem::file_size(base.string()+".white")!=32+uint64_t(wn)*40)throw std::runtime_error("Whitewater header/length mismatch");
    uint64_t types[3]={};float radiusMin=1e20f,radiusMax=0;
    for(uint32_t i=0;i<wn;i++){
        float data[9];w.read(reinterpret_cast<char *>(data),sizeof(data));uint32_t type=read<uint32_t>(w);
        for(float v:data)if(!std::isfinite(v))throw std::runtime_error("Nonfinite whitewater value");
        if(type>2||!(data[6]>0)||data[7]<0||data[8]<=data[7])throw std::runtime_error("Invalid whitewater type/radius/age");
        types[type]++;radiusMin=std::min(radiusMin,data[6]);radiusMax=std::max(radiusMax,data[6]);
    }
    std::cout<<std::setprecision(12)<<"{\"passed\":true,\"frame\":"<<frame<<",\"time_s\":"<<time<<",\"primary_count\":"<<count<<",\"marker_spacing_m\":"<<spacing<<",\"primary_volume_m3\":"<<volume<<",\"kinetic_energy_j\":"<<energy<<",\"maximum_primary_speed_m_s\":"<<maxV<<",\"mass_center_m\":["<<mx/volume<<','<<my/volume<<','<<mz/volume<<"],\"bounds_m\":[["<<minimum.x<<','<<minimum.y<<','<<minimum.z<<"],["<<maximum.x<<','<<maximum.y<<','<<maximum.z<<"]],\"unique_primary_ids\":true,\"continuous_solid_sample_count\":"<<solidSamples<<",\"continuous_solid_sample_stride\":"<<stride<<",\"minimum_sampled_solid_clearance_m\":"<<minSolid<<",\"whitewater_count\":"<<wn<<",\"whitewater_type_counts\":{\"spray\":"<<types[0]<<",\"foam\":"<<types[1]<<",\"bubble\":"<<types[2]<<"},\"whitewater_radius_bounds_m\":["<<(wn?radiusMin:0)<<','<<radiusMax<<"]}\n";
    return 0;
}catch(const std::exception &e){std::cerr<<"AUDIT FAILED: "<<e.what()<<'\n';return 1;}}
