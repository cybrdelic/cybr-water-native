// Native procedural natural-scene geometry. This executable samples exactly the
// terrain and rock fields used by makeHeroScene(); no separate beauty shapes.
#include "scene.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using water::Vec3;
using water::dot;
using water::cross;
using water::length;
using water::normalized;
struct Vertex {Vec3 p,n;};
struct Writer {
    std::ofstream obj;
    uint64_t vertices=0,triangles=0,rockTriangles=0,bedTriangles=0;
    bool binary=false;
    std::string current;
    explicit Writer(const std::string &path):obj(path,std::ios::binary),binary(std::filesystem::path(path).extension()==".mesh"){
        if(!obj)throw std::runtime_error("Cannot write OBJ: "+path);
        if(binary){
            uint32_t version=1,flags=0;uint64_t count=0;
            obj.write("CYBRMSH1",8);obj.write(reinterpret_cast<char*>(&version),4);
            obj.write(reinterpret_cast<char*>(&flags),4);obj.write(reinterpret_cast<char*>(&count),8);
        }else{
            obj<<"# CYBR WATER VFX procedural natural collision scene, metres\n";
            obj<<"# Same terrain/rock zero surfaces as src/scene.hpp\n";
            obj<<"mtllib natural_scene.mtl\n"<<std::setprecision(8);
        }
    }
    void material(const std::string &m){if(!binary&&m!=current){current=m;obj<<"usemtl "<<m<<'\n';}}
    void triangle(Vertex a,Vertex b,Vertex c,const std::string &m,bool rock){
        Vec3 face=cross(b.p-a.p,c.p-a.p);
        if(dot(face,face)<1e-18f)return;
        if(dot(face,a.n+b.n+c.n)<0)std::swap(b,c);
        if(binary){
            for(const Vertex &v:{a,b,c}){
                float values[6]={v.p.x,v.p.y,v.p.z,v.n.x,v.n.y,v.n.z};
                obj.write(reinterpret_cast<const char*>(values),sizeof(values));
            }
            uint32_t material=m=="rock"?1u:(m=="sand"?2u:3u);
            obj.write(reinterpret_cast<const char*>(&material),4);
        }else{
            material(m);
            for(const Vertex &v:{a,b,c})obj<<"v "<<v.p.x<<' '<<v.p.y<<' '<<v.p.z<<'\n';
            for(const Vertex &v:{a,b,c})obj<<"vn "<<v.n.x<<' '<<v.n.y<<' '<<v.n.z<<'\n';
            obj<<"f "<<vertices+1<<"//"<<vertices+1<<' '<<vertices+2<<"//"<<vertices+2<<' '<<vertices+3<<"//"<<vertices+3<<'\n';
        }
        vertices+=3;triangles++;if(rock)rockTriangles++;else bedTriangles++;
    }
    void finish(){
        if(binary){obj.seekp(16);obj.write(reinterpret_cast<const char*>(&triangles),8);}
        obj.flush();
        if(!obj)throw std::runtime_error("Failed while writing geometry output");
    }
};

void terrain(Writer &out,float step){
    const float xmin=-2.40f,xmax=2.70f,zmin=-1.90f,zmax=1.90f;
    // Preserve close geometry density while continuing the same bed beyond the
    // camera and far beyond the liquid domain. Gradually increasing cell sizes
    // keep the outer ground from becoming a visible rectangular display slab.
    auto coordinates=[&](float a,float b,float farA,float farB){
        int n=int(std::ceil((b-a)/step));
        std::vector<float> left,values;
        float p=a,delta=step;
        while(p>farA){delta=std::min(.75f,delta*1.16f);p=std::max(farA,p-delta);left.push_back(p);}
        std::reverse(left.begin(),left.end());values=std::move(left);
        for(int i=0;i<=n;i++)values.push_back(a+(b-a)*float(i)/n);
        p=b;delta=step;
        while(p<farB){delta=std::min(.75f,delta*1.16f);p=std::min(farB,p+delta);values.push_back(p);}
        return values;
    };
    const auto xs=coordinates(xmin,xmax,-24.f,32.f);
    const auto zs=coordinates(zmin,zmax,-24.f,24.f);
    int nx=int(xs.size())-1,nz=int(zs.size())-1;
    std::vector<Vertex> row0(nx+1),row1(nx+1);
    auto row=[&](std::vector<Vertex> &r,int iz){for(int ix=0;ix<=nx;ix++){
        float x=xs[ix],z=zs[iz];
        r[ix]={{x,water::hero::bedHeight(x,z),z},water::hero::bedNormal(x,z)};
    }};
    row(row0,0);
    for(int iz=0;iz<nz;iz++){
        row(row1,iz+1);
        for(int ix=0;ix<nx;ix++){
            Vertex a=row0[ix],b=row0[ix+1],c=row1[ix+1],d=row1[ix];
            out.triangle(a,c,b,water::hero::bedMaterial((a.p+b.p+c.p)/3.f),false);
            out.triangle(a,d,c,water::hero::bedMaterial((a.p+d.p+c.p)/3.f),false);
        }
        row0.swap(row1);
    }
}

Vec3 rockNormal(const water::hero::Rock &rock,Vec3 p,float e){
    return normalized(Vec3(rock.sdf(p+Vec3(e,0,0))-rock.sdf(p-Vec3(e,0,0)),
                           rock.sdf(p+Vec3(0,e,0))-rock.sdf(p-Vec3(0,e,0)),
                           rock.sdf(p+Vec3(0,0,e))-rock.sdf(p-Vec3(0,0,e))));
}

void exportRock(Writer &out,const water::hero::Rock &rock,float quality){
    float maximum=std::max(rock.radii.x,std::max(rock.radii.y,rock.radii.z));
    // Spend geometric samples on the foreground fractured stone. Small bed
    // pebbles and distant bank rocks share their exact source fields but do not
    // need the same tessellation density as the principal close-up subject.
    float baseStep=rock.seed==101?.0095f:(maximum>.18f?(rock.hero?.020f:.025f):std::max(.004f,maximum/6.f));
    float step=baseStep*quality;
    float half=std::min(rock.bound,maximum*1.65f+.025f);
    Vec3 lo=rock.center-Vec3(half,half,half);
    int n=int(std::ceil(2.f*half/step));step=2.f*half/n;
    int ns=n+1;
    std::vector<float> lower(size_t(ns)*ns),upper(lower.size());
    auto fill=[&](std::vector<float> &plane,int iz){
        for(int iy=0;iy<=n;iy++)for(int ix=0;ix<=n;ix++)
            plane[size_t(iy)*ns+ix]=rock.sdf(lo+Vec3(ix*step,iy*step,iz*step));
    };
    fill(lower,0);
    static const int corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    static const int tetra[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
    for(int iz=0;iz<n;iz++){
        fill(upper,iz+1);
        for(int iy=0;iy<n;iy++)for(int ix=0;ix<n;ix++){
            float f[8];Vec3 p[8];bool positive=false,negative=false;
            for(int k=0;k<8;k++){
                int xx=ix+corners[k][0],yy=iy+corners[k][1];
                f[k]=(corners[k][2]?upper:lower)[size_t(yy)*ns+xx];
                p[k]=lo+Vec3(xx*step,yy*step,(iz+corners[k][2])*step);
                positive|=f[k]>=0;negative|=f[k]<0;
            }
            if(!(positive&&negative))continue;
            auto edge=[&](int a,int b){
                float t=water::hero::clamp(f[a]/(f[a]-f[b]));
                Vec3 point=p[a]+(p[b]-p[a])*t;
                // Refine the intersection of the same scalar field. This helps
                // flat fractured faces keep clean silhouettes at grazing view.
                float ta=0,tb=1;float fa=f[a],fb=f[b];
                for(int j=0;j<5;j++){
                    float fp=rock.sdf(point);
                    if(std::abs(fp)<1e-7f)break;
                    if((fp<0)==(fa<0)){ta=t;fa=fp;}else {tb=t;fb=fp;}
                    float fraction=water::hero::clamp(fa/(fa-fb),.05f,.95f);
                    t=ta+(tb-ta)*fraction;point=p[a]+(p[b]-p[a])*t;
                }
                return Vertex{point,rockNormal(rock,point,std::max(.00035f,step*.13f))};
            };
            auto triangle=[&](Vertex a,Vertex b,Vertex c){
                Vec3 center=(a.p+b.p+c.p)/3.f;
                if(center.y<water::hero::bedHeight(center.x,center.z)-step*1.5f)return;
                out.triangle(a,b,c,water::hero::rockMaterial(rock,center,normalized(a.n+b.n+c.n)),true);
            };
            for(const auto &t:tetra){
                int inside[4],outside[4],ni=0,no=0;
                for(int k:t)if(f[k]<0)inside[ni++]=k;else outside[no++]=k;
                if(ni==0||ni==4)continue;
                if(ni==1){triangle(edge(inside[0],outside[0]),edge(inside[0],outside[1]),edge(inside[0],outside[2]));}
                else if(ni==3){triangle(edge(outside[0],inside[0]),edge(outside[0],inside[2]),edge(outside[0],inside[1]));}
                else{
                    Vertex a=edge(inside[0],outside[0]),b=edge(inside[0],outside[1]),c=edge(inside[1],outside[0]),d=edge(inside[1],outside[1]);
                    triangle(a,b,c);triangle(b,d,c);
                }
            }
        }
        lower.swap(upper);
    }
}
}

int main(int argc,char **argv){
    try{
        std::string output="output/natural_scene.mesh";float quality=1.f;
        for(int i=1;i<argc;i++){
            std::string key=argv[i];
            if(key=="--out"&&i+1<argc)output=argv[++i];
            else if(key=="--quality"&&i+1<argc)quality=std::stof(argv[++i]);
            else throw std::runtime_error("Usage: solid_export [--out natural_scene.mesh|natural_scene.obj] [--quality 1]");
        }
        if(!(quality>=.5f&&quality<=4.f))throw std::runtime_error("Quality spacing scale must be in [.5,4]");
        std::filesystem::create_directories(std::filesystem::path(output).parent_path());
        Writer writer(output);
        terrain(writer,.0150f*quality);
        int index=0;
        for(const auto &rock:water::hero::rocks()){
            exportRock(writer,rock,quality);
            if(index++<11)std::cerr<<"rock "<<index<<": total triangles "<<writer.triangles<<'\n';
        }
        writer.finish();
        std::filesystem::path mtl=std::filesystem::path(output).parent_path()/"natural_scene.mtl";
        std::ofstream material(mtl);
        material<<"# Fallback material values; CYBR tracer adds procedural mineral detail.\n"
                <<"newmtl rock\nKd 0.17 0.13 0.10\nKs 0.035 0.035 0.035\nNs 35\n"
                <<"newmtl sand\nKd 0.23 0.17 0.11\nKs 0.025 0.025 0.025\nNs 18\n"
                <<"newmtl moss\nKd 0.085 0.115 0.036\nKs 0.02 0.02 0.02\nNs 12\n";
        std::cout<<"{\"output\":\""<<output<<"\",\"triangles\":"<<writer.triangles
                 <<",\"terrain_triangles\":"<<writer.bedTriangles<<",\"rock_triangles\":"<<writer.rockTriangles
                 <<",\"rock_count\":"<<water::hero::rocks().size()<<",\"spacing_scale\":"<<quality
                 <<",\"format\":\""<<(writer.binary?"CYBRMSH1":"OBJ")<<"\""
                 <<",\"same_collision_field\":true,\"scans\":false,\"generated_images\":false}\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
