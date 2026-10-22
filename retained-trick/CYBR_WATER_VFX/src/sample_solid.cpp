// Sample the frozen shared collision definition for liquid-surface constraints.
// File: 36-byte <8sIIIffff> header, then x-fast little-endian float32 samples.
// The samples themselves contain no contact padding or altered collision shape.
#include "scene.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "openmp_compat.hpp"

namespace {
using water::Vec3;

Vec3 parseVector(const std::string &text){
    std::istringstream s(text);Vec3 p;char comma1=0,comma2=0;
    if(!(s>>p.x>>comma1>>p.y>>comma2>>p.z)||comma1!=','||comma2!=',')
        throw std::runtime_error("Expected x,y,z vector: "+text);
    return p;
}
template<class T> void write(std::ostream &stream,const T &value){
    stream.write(reinterpret_cast<const char*>(&value),sizeof(value));
}
}

int main(int argc,char **argv){
    try{
        Vec3 lower(-1.8f,-.05f,-1.2f),upper(1.8f,1.65f,1.2f);
        float dx=.0125f;int threads=4;
        std::string output="cache_solid/hero_solid.sdf";
        for(int i=1;i<argc;i++){
            std::string key=argv[i];
            auto next=[&](){if(i+1>=argc)throw std::runtime_error("Missing argument for "+key);return std::string(argv[++i]);};
            if(key=="--out")output=next();
            else if(key=="--dx")dx=std::stof(next());
            else if(key=="--threads")threads=std::stoi(next());
            else if(key=="--lower")lower=parseVector(next());
            else if(key=="--upper")upper=parseVector(next());
            else throw std::runtime_error("Usage: sample_solid [--out hero_solid.sdf] [--dx .0125] [--threads 4] [--lower x,y,z] [--upper x,y,z]");
        }
        if(!(dx>=.002f&&dx<=.10f)||threads<1||threads>64)
            throw std::runtime_error("Invalid spacing or thread count");
        if(!(upper.x>lower.x&&upper.y>lower.y&&upper.z>lower.z))
            throw std::runtime_error("Invalid sampling bounds");
        auto nodes=[&](float extent){return uint32_t(std::ceil(double(extent)/dx-1e-5))+1u;};
        uint32_t nx=nodes(upper.x-lower.x),ny=nodes(upper.y-lower.y),nz=nodes(upper.z-lower.z);
        uint64_t count=uint64_t(nx)*ny*nz;
        if(count>150000000ull)throw std::runtime_error("Requested solid field exceeds sample memory budget");
        Vec3 actualUpper=lower+Vec3(float(nx-1),float(ny-1),float(nz-1))*dx;
        std::vector<float> field(count);
        const auto scene=water::makeHeroScene();
        // Build the function-local immutable rock list before entering OpenMP.
        (void)scene.solidSdf(lower);
        omp_set_num_threads(threads);
        auto started=std::chrono::steady_clock::now();
        float minimum=std::numeric_limits<float>::infinity(),maximum=-minimum;
        uint64_t negative=0,nonfinite=0;
        #pragma omp parallel for schedule(dynamic,1) reduction(min:minimum) reduction(max:maximum) reduction(+:negative,nonfinite)
        for(int z=0;z<int(nz);z++){
            for(uint32_t y=0;y<ny;y++)for(uint32_t x=0;x<nx;x++){
                Vec3 p=lower+Vec3(float(x),float(y),float(z))*dx;
                float value=scene.solidSdf(p);
                field[(uint64_t(z)*ny+y)*nx+x]=value;
                minimum=std::min(minimum,value);maximum=std::max(maximum,value);
                negative+=value<0;nonfinite+=!std::isfinite(value);
            }
        }
        if(nonfinite)throw std::runtime_error("Shared solid field returned nonfinite samples");
        std::filesystem::path path(output);
        if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
        std::string temporary=output+".tmp";
        std::ofstream stream(temporary,std::ios::binary);
        if(!stream)throw std::runtime_error("Cannot create solid field");
        stream.write("CYBRSDF1",8);write(stream,nx);write(stream,ny);write(stream,nz);
        write(stream,dx);write(stream,lower.x);write(stream,lower.y);write(stream,lower.z);
        stream.write(reinterpret_cast<const char*>(field.data()),std::streamsize(field.size()*sizeof(float)));
        stream.close();if(!stream)throw std::runtime_error("Failed writing sampled solid field");
        std::filesystem::rename(temporary,output);
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::ostringstream report;
        report<<std::setprecision(10)<<"{\n"
              <<"  \"format\": \"CYBRSDF1\",\n"
              <<"  \"header_bytes\": 36,\n"
              <<"  \"nodes\": ["<<nx<<','<<ny<<','<<nz<<"],\n"
              <<"  \"sample_count\": "<<count<<",\n"
              <<"  \"voxel_m\": "<<dx<<",\n"
              <<"  \"origin_m\": ["<<lower.x<<','<<lower.y<<','<<lower.z<<"],\n"
              <<"  \"upper_sample_m\": ["<<actualUpper.x<<','<<actualUpper.y<<','<<actualUpper.z<<"],\n"
              <<"  \"value_range_m\": ["<<minimum<<','<<maximum<<"],\n"
              <<"  \"negative_samples\": "<<negative<<",\n"
              <<"  \"nonfinite_samples\": "<<nonfinite<<",\n"
              <<"  \"threads\": "<<threads<<",\n"
              <<"  \"seconds\": "<<seconds<<",\n"
              <<"  \"padding_applied_to_samples_m\": 0,\n"
              <<"  \"planned_surface_contact_padding_m\": 0.0003,\n"
              <<"  \"contact_note\": \"The mesher uses max(rawLiquidPhi-offset, -solidSdf-contactPadding). A positive 0.3 mm padding permits a shallow overlap into opaque rock to avoid coincident optical interfaces. It is not applied to this sampled file.\",\n"
              <<"  \"distance_note\": \"Samples use the frozen shared scene. The terrain uses a local normal-distance approximation; trilinear interpolation is also a finite-resolution approximation of the analytic fields.\"\n"
              <<"}\n";
        std::ofstream metadata(output+".json");metadata<<report.str();metadata.close();
        if(!metadata)throw std::runtime_error("Failed writing sample report");
        std::cout<<report.str();
        return 0;
    }catch(const std::exception &error){std::cerr<<"sample_solid: "<<error.what()<<'\n';return 1;}
}
