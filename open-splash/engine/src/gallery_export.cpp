#include <sstream>
// Optional source-only companion for gallery_scenes.hpp. The same ground zero
// set and fractured-rock fields drive both Solver collisions and this geometry.
// Build separately; this file does not modify the hero executable/build rules.
#include "gallery_scenes.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
using water::Vec3;
struct Vertex {Vec3 p,n;};
struct Writer {
    std::ofstream file;
    uint64_t triangles=0,bedTriangles=0,rockTriangles=0;
    explicit Writer(const std::string &path):file(path,std::ios::binary) {
        if (!file) throw std::runtime_error("Cannot create "+path);
        uint32_t version=1,flags=0;uint64_t count=0;
        file.write("CYBRMSH1",8);file.write(reinterpret_cast<char*>(&version),4);
        file.write(reinterpret_cast<char*>(&flags),4);file.write(reinterpret_cast<char*>(&count),8);
    }
    void triangle(Vertex a,Vertex b,Vertex c,uint32_t material,bool rock=false) {
        Vec3 normal=water::cross(b.p-a.p,c.p-a.p);
        if (water::dot(normal,normal)<1e-30f) return;
        if (water::dot(normal,a.n+b.n+c.n)<0) std::swap(b,c);
        for (const Vertex &v:{a,b,c}) {
            float data[6]={v.p.x,v.p.y,v.p.z,v.n.x,v.n.y,v.n.z};
            file.write(reinterpret_cast<const char*>(data),sizeof(data));
        }
        file.write(reinterpret_cast<const char*>(&material),4);
        triangles++;if(rock)rockTriangles++;else bedTriangles++;
    }
    void finish() {
        file.seekp(16);file.write(reinterpret_cast<const char*>(&triangles),8);
        file.flush();if(!file)throw std::runtime_error("Geometry write failed");
    }
};

std::vector<float> coordinates(float a,float b,float distantA,float distantB,float step) {
    std::vector<float> left,result;
    float p=a,d=step;
    while(p>distantA){d=std::min(.60f,d*1.15f);p=std::max(distantA,p-d);left.push_back(p);}
    std::reverse(left.begin(),left.end());result=std::move(left);
    int n=int(std::ceil((b-a)/step));
    for(int i=0;i<=n;i++)result.push_back(a+(b-a)*float(i)/n);
    p=b;d=step;
    while(p<distantB){d=std::min(.60f,d*1.15f);p=std::min(distantB,p+d);result.push_back(p);}
    return result;
}

void ground(Writer &writer,const water::gallery::Geometry &geometry,float quality,float scale) {
    auto xs=coordinates(-2.0f*scale,2.0f*scale,-16.f*scale,16.f*scale,.0125f*quality*scale);
    auto zs=coordinates(-1.40f*scale,1.40f*scale,-16.f*scale,16.f*scale,.0125f*quality*scale);
    std::vector<Vertex> row0(xs.size()),row1(xs.size());
    auto row=[&](std::vector<Vertex> &r,float z) {
        for(size_t x=0;x<xs.size();x++)r[x]={{xs[x],geometry.height(xs[x],z),z},geometry.normal(xs[x],z)};
    };
    row(row0,zs[0]);
    for(size_t z=0;z+1<zs.size();z++) {
        row(row1,zs[z+1]);
        for(size_t x=0;x+1<xs.size();x++) {
            auto a=row0[x],b=row0[x+1],c=row1[x+1],d=row1[x];
            writer.triangle(a,c,b,geometry.groundMaterial);
            writer.triangle(a,d,c,geometry.groundMaterial);
        }
        row0.swap(row1);
    }
}

Vec3 normal(const water::hero::Rock &rock,Vec3 p,float e) {
    return water::normalized(Vec3(
        rock.sdf(p+Vec3(e,0,0))-rock.sdf(p-Vec3(e,0,0)),
        rock.sdf(p+Vec3(0,e,0))-rock.sdf(p-Vec3(0,e,0)),
        rock.sdf(p+Vec3(0,0,e))-rock.sdf(p-Vec3(0,0,e))));
}

void rock(Writer &writer,const water::gallery::Geometry &geometry,
          const water::hero::Rock &shape,float quality) {
    float radius=std::max({shape.radii.x,shape.radii.y,shape.radii.z});
    float step=std::max(.003f,std::min(.010f,radius/9.f))*quality;
    float half=std::min(shape.bound,1.65f*radius+.025f);
    int n=int(std::ceil(2.f*half/step));step=2.f*half/n;
    int ns=n+1;
    Vec3 lo=shape.center-Vec3(half,half,half);
    std::vector<float> lower(size_t(ns)*ns),upper(lower.size());
    auto fill=[&](std::vector<float> &plane,int z) {
        for(int y=0;y<ns;y++)for(int x=0;x<ns;x++)
            plane[size_t(y)*ns+x]=shape.sdf(lo+Vec3(x*step,y*step,z*step));
    };
    fill(lower,0);
    constexpr int corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},
                               {0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    constexpr int tetra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},
                              {0,3,7,6},{0,7,4,6},{0,4,5,6}};
    for(int z=0;z<n;z++) {
        fill(upper,z+1);
        for(int y=0;y<n;y++)for(int x=0;x<n;x++) {
            Vec3 p[8];float f[8];uint64_t ids[8];bool positive=false,negative=false;
            for(int i=0;i<8;i++) {
                int xx=x+corner[i][0],yy=y+corner[i][1],zz=z+corner[i][2];
                p[i]=lo+Vec3(xx*step,yy*step,zz*step);
                f[i]=(corner[i][2]?upper:lower)[size_t(yy)*ns+xx];
                ids[i]=(uint64_t(zz)*ns+yy)*ns+xx;
                positive|=f[i]>=0;negative|=f[i]<0;
            }
            if(!(positive&&negative))continue;
            auto edge=[&](int a,int b) {
                // Canonical endpoint order prevents ulp cracks on shared edges.
                if(ids[a]>ids[b])std::swap(a,b);
                float ta=0,tb=1,fa=f[a],fb=f[b];
                float t=std::clamp(fa/(fa-fb),0.f,1.f);
                Vec3 point=p[a]+(p[b]-p[a])*t;
                for(int j=0;j<5;j++) {
                    float fp=shape.sdf(point);
                    if(std::abs(fp)<1e-7f)break;
                    if((fp<0)==(fa<0)){ta=t;fa=fp;}else{tb=t;fb=fp;}
                    float fraction=std::clamp(fa/(fa-fb),.05f,.95f);
                    t=ta+(tb-ta)*fraction;point=p[a]+(p[b]-p[a])*t;
                }
                return Vertex{point,normal(shape,point,std::max(.0003f,step*.13f))};
            };
            auto triangle=[&](Vertex a,Vertex b,Vertex c) {
                Vec3 centre=(a.p+b.p+c.p)/3.f;
                if(centre.y<geometry.height(centre.x,centre.z)-1.5f*step)return;
                // Some hidden overlap faces remain under the opaque ground.
                // First-hit rendering shows the union, matching solidSdf.
                writer.triangle(a,b,c,1,true);
            };
            for(const auto &t:tetra) {
                int in[4],out[4],ni=0,no=0;
                for(int i:t)if(f[i]<0)in[ni++]=i;else out[no++]=i;
                if(ni==0||ni==4)continue;
                if(ni==1)triangle(edge(in[0],out[0]),edge(in[0],out[1]),edge(in[0],out[2]));
                else if(ni==3)triangle(edge(out[0],in[0]),edge(out[0],in[2]),edge(out[0],in[1]));
                else {
                    Vertex a=edge(in[0],out[0]),b=edge(in[0],out[1]);
                    Vertex c=edge(in[1],out[0]),d=edge(in[1],out[1]);
                    triangle(a,b,c);triangle(b,d,c);
                }
            }
        }
        lower.swap(upper);
    }
}

void sampledSolid(const water::gallery::Preset &preset,const std::string &path,float dx) {
    if(!(dx>=.00025f&&dx<=.10f))throw std::runtime_error("SDF spacing must be in [.00025,.10] metres");
    auto parent=std::filesystem::path(path).parent_path();
    if(!parent.empty())std::filesystem::create_directories(parent);
    Vec3 lower=preset.scene.lower,extent=preset.scene.upper-lower;
    uint32_t nx=uint32_t(std::ceil(extent.x/dx-1e-4f))+1;
    uint32_t ny=uint32_t(std::ceil(extent.y/dx-1e-4f))+1;
    uint32_t nz=uint32_t(std::ceil(extent.z/dx-1e-4f))+1;
    std::vector<float> values(size_t(nx)*ny*nz);
    #pragma omp parallel for schedule(static) num_threads(2)
    for(int z=0;z<int(nz);z++)for(uint32_t y=0;y<ny;y++)for(uint32_t x=0;x<nx;x++)
        values[(size_t(z)*ny+y)*nx+x]=preset.scene.solidSdf(lower+Vec3(x*dx,y*dx,z*dx));
    for(float value:values)if(!std::isfinite(value))throw std::runtime_error("Non-finite gallery solid field");
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("Cannot create "+path);
    output.write("CYBRSDF1",8);
    for(uint32_t count:{nx,ny,nz})output.write(reinterpret_cast<const char*>(&count),4);
    for(float value:{dx,lower.x,lower.y,lower.z})output.write(reinterpret_cast<const char*>(&value),4);
    output.write(reinterpret_cast<const char*>(values.data()),std::streamsize(values.size()*4));
    if(!output)throw std::runtime_error("Gallery SDF write failed");
    std::cerr<<"Sampled matching solid field: "<<nx<<'x'<<ny<<'x'<<nz<<", dx="<<dx<<" m, "<<path<<'\n';
}
}

int main(int argc,char **argv) {
    try {
        std::string name="beach",path="output/gallery_beach_static.mesh",sdfPath;
        Vec3 sdfLower,sdfUpper;bool hasLower=false,hasUpper=false;
        auto vec=[](std::string q){std::replace(q.begin(),q.end(),',',' ');std::istringstream f(q);Vec3 p;f>>p.x>>p.y>>p.z;if(!f)throw std::runtime_error("Bad SDF bounds");return p;};
        float quality=1.f,sdfDx=.0125f;
        for(int i=1;i<argc;i++) {
            std::string arg=argv[i];
            if(arg=="--scene"&&i+1<argc)name=argv[++i];
            else if(arg=="--out"&&i+1<argc)path=argv[++i];
            else if(arg=="--quality"&&i+1<argc)quality=std::stof(argv[++i]);
            else if(arg=="--sdf-out"&&i+1<argc)sdfPath=argv[++i];
            else if(arg=="--sdf-dx"&&i+1<argc)sdfDx=std::stof(argv[++i]);
            else if(arg=="--sdf-lower"&&i+1<argc){sdfLower=vec(argv[++i]);hasLower=true;}
            else if(arg=="--sdf-upper"&&i+1<argc){sdfUpper=vec(argv[++i]);hasUpper=true;}
            else throw std::runtime_error("Usage: gallery_export --scene beach|waterbending|drop|whirlpool|rockimpact|jets|cascade --out file.mesh [--quality 1] [--sdf-out solid.sdf --sdf-dx .0125]");
        }
        if(!(quality>=.5f&&quality<=4.f))throw std::runtime_error("Quality spacing scale must be in [.5,4]");
        auto preset=water::gallery::makePreset(name);
        auto parent=std::filesystem::path(path).parent_path();
        if(!parent.empty())std::filesystem::create_directories(parent);
        Writer writer(path);
        ground(writer,*preset.geometry,quality,preset.geometryScale);
        for(const auto &shape:preset.geometry->rocks)rock(writer,*preset.geometry,shape,quality);
        writer.finish();
        if(!sdfPath.empty()){if(hasLower)preset.scene.lower=sdfLower;if(hasUpper)preset.scene.upper=sdfUpper;sampledSolid(preset,sdfPath,sdfDx);}
        std::cout<<std::setprecision(9)
                 <<"{\"scene\":\""<<preset.scene.name<<"\",\"triangles\":"<<writer.triangles
                 <<",\"ground_triangles\":"<<writer.bedTriangles<<",\"rock_triangles\":"<<writer.rockTriangles
                 <<",\"format\":\"CYBRMSH1\",\"same_collision_zero_fields\":true"
                 <<",\"static_geometry_only\":true,\"spacing_scale\":"<<quality
                 <<",\"reference_water_level\":"<<preset.referenceWaterLevel
                 <<",\"camera\":["<<preset.camera.x<<','<<preset.camera.y<<','<<preset.camera.z
                 <<"],\"target\":["<<preset.target.x<<','<<preset.target.y<<','<<preset.target.z
                 <<"],\"vertical_fov_degrees\":"<<preset.verticalFov<<"}\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
