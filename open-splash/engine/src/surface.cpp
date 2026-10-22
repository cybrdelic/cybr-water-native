// CYBR WATER VFX surface reconstruction; no image-space geometry synthesis.
// Anisotropic Zhu-Bridson scalar field from the common FLIP liquid markers.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>
namespace water {
constexpr float PI = 3.14159265358979323846f;
constexpr float INF = 1.0e20f;
inline float clamp(float x,float a=0.f,float b=1.f){ return std::max(a,std::min(b,x)); }
inline float mix(float a,float b,float t){ return a+(b-a)*t; }
inline float smooth(float a,float b,float x){ float t=clamp((x-a)/(b-a)); return t*t*(3-2*t); }
inline float sq(float x){return x*x;}

struct V3 {
    float x=0,y=0,z=0;
    V3()=default; V3(float a,float b,float c):x(a),y(b),z(c){}
    explicit V3(float a):x(a),y(a),z(a){}
    V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
    V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
    V3 operator-()const{return {-x,-y,-z};}
    V3 operator*(float b)const{return {x*b,y*b,z*b};}
    V3 operator/(float b)const{return *this*(1.f/b);}
    V3 operator*(V3 b)const{return {x*b.x,y*b.y,z*b.z};}
    V3 &operator+=(V3 b){x+=b.x;y+=b.y;z+=b.z;return *this;}
    V3 &operator*=(float b){x*=b;y*=b;z*=b;return *this;}
    float operator[](int a)const{return a==0?x:(a==1?y:z);}
};
inline V3 operator*(float a,V3 b){return b*a;}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V3 a){return std::sqrt(dot(a,a));}
inline V3 unit(V3 a){return a/std::sqrt(std::max(1e-20f,dot(a,a)));}
inline V3 vmin(V3 a,V3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
inline V3 vmax(V3 a,V3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
inline V3 mix(V3 a,V3 b,float t){return a+(b-a)*t;}
inline V3 reflect(V3 a,V3 n){return a-n*(2*dot(a,n));}
inline V3 expneg(V3 a){return {std::exp(-a.x),std::exp(-a.y),std::exp(-a.z)};}
inline float luminance(V3 a){return .2126f*a.x+.7152f*a.y+.0722f*a.z;}

struct Header {
    char magic[8];
    uint32_t version,count,frame;
    float time,spacing,smoothingRadius;
};
struct Particle {float x,y,z,vx,vy,vz,densityRatio,foam;};
static_assert(sizeof(Header)==32,"Snapshot header ABI");
static_assert(sizeof(Particle)==32,"Snapshot particle ABI");
struct Snapshot {Header header;std::vector<Particle> particles;};
Snapshot readSnapshot(const std::string &filename){
    std::ifstream f(filename,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot open snapshot: "+filename);
    Snapshot s{}; f.read(reinterpret_cast<char*>(&s.header),sizeof(s.header));
    if(!f || std::memcmp(s.header.magic,"CYBRWTR1",8)!=0 || s.header.version!=1)
        throw std::runtime_error("Unsupported/corrupt CYBR WATER snapshot: "+filename);
    if(s.header.count>10000000 || !(s.header.spacing>1e-6f && s.header.spacing<.2f))
        throw std::runtime_error("Invalid snapshot count or particle spacing");
    s.particles.resize(s.header.count);
    f.read(reinterpret_cast<char*>(s.particles.data()),s.particles.size()*sizeof(Particle));
    if(!f)throw std::runtime_error("Truncated snapshot: "+filename);
    return s;
}


struct Options {float surfaceScale=.85f;float supportScale=3.0f;int smooth=2;};
struct SolidGrid {
    uint32_t nx=0,ny=0,nz=0;float step=0;V3 origin;std::vector<float> phi;
    void read(const std::string &path){
        std::ifstream f(path,std::ios::binary);char magic[8];f.read(magic,8);
        f.read(reinterpret_cast<char*>(&nx),4);f.read(reinterpret_cast<char*>(&ny),4);f.read(reinterpret_cast<char*>(&nz),4);
        f.read(reinterpret_cast<char*>(&step),4);f.read(reinterpret_cast<char*>(&origin),12);
        if(!f||std::memcmp(magic,"CYBRSDF1",8)||nx<2||ny<2||nz<2||size_t(nx)*ny*nz>100000000||step<=0)throw std::runtime_error("Invalid solid field");
        phi.resize(size_t(nx)*ny*nz);f.read(reinterpret_cast<char*>(phi.data()),phi.size()*sizeof(float));if(!f)throw std::runtime_error("Truncated solid field");
    }
    float sample(V3 p)const{
        V3 q=(p-origin)/step;q={clamp(q.x,0,float(nx-1)-1e-4f),clamp(q.y,0,float(ny-1)-1e-4f),clamp(q.z,0,float(nz-1)-1e-4f)};
        int x=int(q.x),y=int(q.y),z=int(q.z);float a=q.x-x,b=q.y-y,c=q.z-z;
        size_t i=(size_t(z)*ny+y)*nx+x;auto s=[&](int dx,int dy,int dz){return phi[i+dx+size_t(dy)*nx+size_t(dz)*nx*ny];};
        return mix(mix(mix(s(0,0,0),s(1,0,0),a),mix(s(0,1,0),s(1,1,0),a),b),mix(mix(s(0,0,1),s(1,0,1),a),mix(s(0,1,1),s(1,1,1),a),b),c);
    }
};
struct Reconstruction {
    float particleSpacing;
    Options opt;
    std::vector<float> volumes;
    // Optional persistent liquid/drop classification, keyed by actual marker ID.
    // A sparse ligament does not become separate spheres merely because it has
    // fewer than four neighbours. Different separation/rejoin thresholds avoid
    // the original one-frame switch at the neighbour-count cutoff.
    std::vector<uint32_t> ids;
    std::vector<uint8_t> previousPhase;
    bool coherentFragments=false;
    static bool detached(float nearestSquared,float spacing,uint8_t previous) {
        float threshold=previous==1?2.20f:(previous==0?3.20f:2.70f);
        return nearestSquared>sq(threshold*spacing);
    }

    Reconstruction(float d,Options o):particleSpacing(d),opt(o){}
    struct Anisotropy {
        float xx=1,xy=0,xz=0,yy=1,yz=0,zz=1;
        V3 bound;
        bool droplet=false;
    };
    std::vector<Anisotropy> particleMetrics;
    void measureAnisotropy(const Snapshot &s){
        // Local particle covariance supplies sheet normals and filament axes.
        // Eigenvalue bounds prevent arbitrarily thin or large reconstruction
        // kernels, while geometric-mean normalization preserves kernel volume.
        if(s.particles.empty()){particleMetrics.clear();return;}
        float neighborhood=particleSpacing*4.f,inverseCell=1.f/neighborhood;
        V3 lo(INF),hi(-INF);
        for(const auto &p:s.particles){lo=vmin(lo,{p.x,p.y,p.z});hi=vmax(hi,{p.x,p.y,p.z});}
        lo=lo-V3(neighborhood*.1f);hi=hi+V3(neighborhood*.1f);
        int nx=int((hi.x-lo.x)*inverseCell)+1,ny=int((hi.y-lo.y)*inverseCell)+1,nz=int((hi.z-lo.z)*inverseCell)+1;
        if(size_t(nx)*ny*nz>4000000)throw std::runtime_error("Particle neighborhood domain is unexpectedly large");
        std::vector<std::vector<uint32_t>> bins(size_t(nx)*ny*nz);
        for(uint32_t i=0;i<s.particles.size();i++){
            const auto &p=s.particles[i];V3 q=(V3(p.x,p.y,p.z)-lo)*inverseCell;
            int x=std::clamp(int(q.x),0,nx-1),y=std::clamp(int(q.y),0,ny-1),z=std::clamp(int(q.z),0,nz-1);
            bins[(z*ny+y)*nx+x].push_back(i);
        }
        particleMetrics.resize(s.particles.size());
        float inverseRadius2=1.f/(neighborhood*neighborhood);
        #pragma omp parallel for schedule(dynamic,128)
        for(int i=0;i<int(s.particles.size());i++){
            const auto &p=s.particles[i];V3 pos(p.x,p.y,p.z),q=(pos-lo)*inverseCell;
            int cx=int(q.x),cy=int(q.y),cz=int(q.z),neighbors=0;
            float weight=0,mx=0,my=0,mz=0,xx=0,xy=0,xz=0,yy=0,yz=0,zz=0,nearestSquared=INF;
            for(int z=std::max(0,cz-1);z<=std::min(nz-1,cz+1);z++)
                for(int y=std::max(0,cy-1);y<=std::min(ny-1,cy+1);y++)
                    for(int x=std::max(0,cx-1);x<=std::min(nx-1,cx+1);x++)
                        for(uint32_t j:bins[(z*ny+y)*nx+x]){
                            const auto &other=s.particles[j];V3 d=V3(other.x,other.y,other.z)-pos;
                            float k=1.f-dot(d,d)*inverseRadius2;if(k<=0)continue;
                            float w=k*k*k;weight+=w;mx+=w*d.x;my+=w*d.y;mz+=w*d.z;
                            xx+=w*d.x*d.x;xy+=w*d.x*d.y;xz+=w*d.x*d.z;yy+=w*d.y*d.y;yz+=w*d.y*d.z;zz+=w*d.z*d.z;
                            if(j!=uint32_t(i)){neighbors++;nearestSquared=std::min(nearestSquared,dot(d,d));}
                        }
            Anisotropy metric;metric.bound=V3(particleSpacing*opt.supportScale);metric.droplet=neighbors<=3;
            if(coherentFragments){
                uint8_t old=255;
                if(size_t(i)<ids.size()&&ids[i]<previousPhase.size())old=previousPhase[ids[i]];
                metric.droplet=detached(nearestSquared,particleSpacing,old);
            }
            if(neighbors<5||weight<.001f){particleMetrics[i]=metric;continue;}
            float iw=1.f/weight;mx*=iw;my*=iw;mz*=iw;
            float a[3][3]={{xx*iw-mx*mx,xy*iw-mx*my,xz*iw-mx*mz},
                           {xy*iw-mx*my,yy*iw-my*my,yz*iw-my*mz},
                           {xz*iw-mx*mz,yz*iw-my*mz,zz*iw-mz*mz}};
            float eigenvectors[3][3]={{1,0,0},{0,1,0},{0,0,1}};
            for(int sweep=0;sweep<6;sweep++)for(int pair=0;pair<3;pair++){
                int u=pair==2?1:0,v=pair==0?1:2;
                if(std::abs(a[u][v])<particleSpacing*particleSpacing*1e-8f)continue;
                float tau=(a[v][v]-a[u][u])/(2.f*a[u][v]);
                float t=std::copysign(1.f/(std::abs(tau)+std::sqrt(1.f+tau*tau)),tau);
                float c=1.f/std::sqrt(1.f+t*t),sn=t*c;
                float off=a[u][v];a[u][u]-=t*off;a[v][v]+=t*off;a[u][v]=a[v][u]=0;
                for(int k=0;k<3;k++)if(k!=u&&k!=v){
                    float ku=a[k][u],kv=a[k][v];a[k][u]=a[u][k]=c*ku-sn*kv;a[k][v]=a[v][k]=sn*ku+c*kv;
                }
                for(int k=0;k<3;k++){float ku=eigenvectors[k][u],kv=eigenvectors[k][v];eigenvectors[k][u]=c*ku-sn*kv;eigenvectors[k][v]=sn*ku+c*kv;}
            }
            float largest=std::max(a[0][0],std::max(a[1][1],a[2][2]));
            if(largest<particleSpacing*particleSpacing*1e-5f){particleMetrics[i]=metric;continue;}
            float sigma[3];for(int k=0;k<3;k++)sigma[k]=std::sqrt(std::max(a[k][k],largest/9.f));
            float geometricMean=std::cbrt(sigma[0]*sigma[1]*sigma[2]);
            float axes[3];for(int k=0;k<3;k++)axes[k]=sigma[k]/geometricMean;
            float smallest=std::min(axes[0],std::min(axes[1],axes[2]));
            float exponent=smallest<.62f?std::log(.62f)/std::log(smallest):1.f;
            float ratio=std::sqrt(largest/std::max(largest/100.f,std::min(a[0][0],std::min(a[1][1],a[2][2]))));
            exponent*=smooth(1.15f,1.75f,ratio);
            for(int k=0;k<3;k++)axes[k]=std::pow(axes[k],exponent);
            metric.xx=metric.xy=metric.xz=metric.yy=metric.yz=metric.zz=0;V3 squaredBound;
            for(int k=0;k<3;k++){
                float ix=eigenvectors[0][k],iy=eigenvectors[1][k],iz=eigenvectors[2][k];
                float inv=1.f/(axes[k]*axes[k]),scale=axes[k]*axes[k];
                metric.xx+=ix*ix*inv;metric.xy+=ix*iy*inv;metric.xz+=ix*iz*inv;
                metric.yy+=iy*iy*inv;metric.yz+=iy*iz*inv;metric.zz+=iz*iz*inv;
                squaredBound+=V3(ix*ix,iy*iy,iz*iz)*scale;
            }
            metric.bound=V3(std::sqrt(squaredBound.x),std::sqrt(squaredBound.y),std::sqrt(squaredBound.z))*(particleSpacing*opt.supportScale);
            particleMetrics[i]=metric;
        }
    }
    struct FieldGrid {
        V3 origin;
        int nx=0,ny=0,nz=0;
        float step=0;
        std::vector<float> phi,foam,solidConstraint;
        int index(int x,int y,int z)const{return (z*ny+y)*nx+x;}
        V3 point(int x,int y,int z)const{return origin+V3(float(x),float(y),float(z))*step;}
        float coordinateUlp()const{
            if(phi.empty())return 0;
            V3 far=point(nx-1,ny-1,nz-1);float largest=0;
            for(int a=0;a<3;a++)largest=std::max(largest,std::max(std::abs(origin[a]),std::abs(far[a])));
            return std::nextafter(largest,std::numeric_limits<float>::infinity())-largest;
        }
        float endpointGuard()const{
            if(phi.empty())return 0;
            float guard=std::max(8.f*coordinateUlp()/step,8.f*std::numeric_limits<float>::epsilon());
            if(!(guard<=.001f))throw std::runtime_error("Surface coordinates cannot represent eight float32 steps within 0.001 voxel; use a local coordinate origin or a coarser surface grid");
            return guard;
        }
        float value(int i,float iso=0)const{return solidConstraint.empty()?phi[i]-iso:std::max(phi[i]-iso,solidConstraint[i]);}
        V3 gradient(int x,int y,int z,float iso=0)const{
            int xl=std::max(x-1,0),xr=std::min(x+1,nx-1);
            int yl=std::max(y-1,0),yr=std::min(y+1,ny-1);
            int zl=std::max(z-1,0),zr=std::min(z+1,nz-1);
            return {value(index(xr,y,z),iso)-value(index(xl,y,z),iso),
                    value(index(x,yr,z),iso)-value(index(x,yl,z),iso),
                    value(index(x,y,zr),iso)-value(index(x,y,zl),iso)};
        }
    };
    struct SurfaceVertex {V3 p,n;float foam;};
    struct SurfaceTriangle {SurfaceVertex a,b,c;};
    std::vector<SurfaceTriangle> retainedSurface;

    FieldGrid buildImplicit(const Snapshot &s){
        // Zhu--Bridson reconstruction. Each lattice sample measures its distance
        // to a compact-kernel weighted centroid of actual neighboring particles.
        // This is a world-space scalar field, independent of camera or pixels.
        struct Accumulator {float weight=0,x=0,y=0,z=0,foam=0,xx=0,xy=0,xz=0,yy=0,yz=0,zz=0;};
        constexpr int BRICK=8;
        FieldGrid g;g.step=particleSpacing*opt.surfaceScale;
        float support=particleSpacing*opt.supportScale;
        V3 lo(INF),hi(-INF);
        std::vector<uint32_t> dense;
        dense.reserve(s.particles.size());
        for(uint32_t i=0;i<s.particles.size();i++){
            const auto &p=s.particles[i];
            if(particleMetrics[i].droplet)continue;
            V3 q(p.x,p.y,p.z);if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))continue;
            lo=vmin(lo,q);hi=vmax(hi,q);dense.push_back(i);
        }
        if(dense.empty())return g;
        lo=lo-V3(support*2.2f+g.step*2);hi=hi+V3(support*2.2f+g.step*2);
        g.origin={std::floor(lo.x/g.step)*g.step,std::floor(lo.y/g.step)*g.step,std::floor(lo.z/g.step)*g.step};
        g.nx=int(std::ceil((hi.x-g.origin.x)/g.step))+1;
        g.ny=int(std::ceil((hi.y-g.origin.y)/g.step))+1;
        g.nz=int(std::ceil((hi.z-g.origin.z)/g.step))+1;
        size_t nodes=size_t(g.nx)*g.ny*g.nz;
        if(nodes>110000000)throw std::runtime_error("Surface lattice exceeds memory budget; increase --surface-scale");
        int bx=(g.nx+BRICK-1)/BRICK,by=(g.ny+BRICK-1)/BRICK,bz=(g.nz+BRICK-1)/BRICK;
        std::vector<std::vector<uint32_t>> bins(size_t(bx)*by*bz);
        float inverseStep=1.f/g.step;
        for(uint32_t id:dense){
            const auto &p=s.particles[id];V3 q=(V3(p.x,p.y,p.z)-g.origin)*inverseStep;
            V3 r=particleMetrics[id].bound*inverseStep;
            int x0=std::max(0,int(q.x-r.x)),x1=std::min(g.nx-1,int(q.x+r.x)+1);
            int y0=std::max(0,int(q.y-r.y)),y1=std::min(g.ny-1,int(q.y+r.y)+1);
            int z0=std::max(0,int(q.z-r.z)),z1=std::min(g.nz-1,int(q.z+r.z)+1);
            for(int z=z0/BRICK;z<=z1/BRICK;z++)for(int y=y0/BRICK;y<=y1/BRICK;y++)for(int x=x0/BRICK;x<=x1/BRICK;x++)
                bins[(z*by+y)*bx+x].push_back(id);
        }
        // Accumulation is brick-local: the same particles visit each node in the
        // same order, but a thread needs only 512 temporary records instead of a
        // 44-byte record for every node in the reconstruction domain.
        g.phi.assign(nodes,support);g.foam.resize(nodes);
        const float radius=particleSpacing*.67f;
        float inverseSupport2=1.f/(support*support);
        #pragma omp parallel for schedule(dynamic,1)
        for(int brick=0;brick<bx*by*bz;brick++){
            if(bins[brick].empty())continue;
            int ix=brick%bx,iy=(brick/bx)%by,iz=brick/(bx*by);
            int xlo=ix*BRICK,xhi=std::min(g.nx-1,xlo+BRICK-1);
            int ylo=iy*BRICK,yhi=std::min(g.ny-1,ylo+BRICK-1);
            int zlo=iz*BRICK,zhi=std::min(g.nz-1,zlo+BRICK-1);
            std::array<Accumulator,BRICK*BRICK*BRICK> accum{};
            for(uint32_t id:bins[brick]){
                const auto &p=s.particles[id];V3 pos(p.x,p.y,p.z);V3 q=(pos-g.origin)*inverseStep;
                const Anisotropy &metric=particleMetrics[id];V3 r=metric.bound*inverseStep;
                int x0=std::max(xlo,int(q.x-r.x)),x1=std::min(xhi,int(q.x+r.x)+1);
                int y0=std::max(ylo,int(q.y-r.y)),y1=std::min(yhi,int(q.y+r.y)+1);
                int z0=std::max(zlo,int(q.z-r.z)),z1=std::min(zhi,int(q.z+r.z)+1);
                for(int z=z0;z<=z1;z++){
                    float dz=g.origin.z+z*g.step-p.z;
                    for(int y=y0;y<=y1;y++){
                        float dy=g.origin.y+y*g.step-p.y;
                        float remaining=1.f-(metric.yy*dy*dy+2.f*metric.yz*dy*dz+metric.zz*dz*dz)*inverseSupport2;
                        float linear=2.f*(metric.xy*dy+metric.xz*dz)*inverseSupport2,quadratic=metric.xx*inverseSupport2;
                        int base=((z-zlo)*BRICK+(y-ylo))*BRICK-xlo;
                        for(int x=x0;x<=x1;x++){
                            float dx=g.origin.x+x*g.step-p.x;
                            float k=remaining-linear*dx-quadratic*dx*dx;if(k<=0)continue;
                            float volumeWeight=volumes.empty()?1.f:volumes[id]/(particleSpacing*particleSpacing*particleSpacing);
                            float w=k*k*k*volumeWeight;
                            Accumulator &a=accum[base+x];a.weight+=w;a.x+=w*p.x;a.y+=w*p.y;a.z+=w*p.z;a.foam+=w*clamp(p.foam);
                            a.xx+=w*metric.xx;a.xy+=w*metric.xy;a.xz+=w*metric.xz;a.yy+=w*metric.yy;a.yz+=w*metric.yz;a.zz+=w*metric.zz;
                        }
                    }
                }
            }
            for(int z=zlo;z<=zhi;z++)for(int y=ylo;y<=yhi;y++)for(int x=xlo;x<=xhi;x++){
                int i=g.index(x,y,z);const auto &a=accum[((z-zlo)*BRICK+(y-ylo))*BRICK+(x-xlo)];
                if(a.weight<1e-10f){g.phi[i]=support;g.foam[i]=0;continue;}
                float inverseWeight=1.f/a.weight;
                V3 center={a.x*inverseWeight,a.y*inverseWeight,a.z*inverseWeight};
                V3 d=g.point(x,y,z)-center;
                float metricDistance=(a.xx*d.x*d.x+2.f*a.xy*d.x*d.y+2.f*a.xz*d.x*d.z+a.yy*d.y*d.y+2.f*a.yz*d.y*d.z+a.zz*d.z*d.z)*inverseWeight;
                g.phi[i]=std::sqrt(std::max(0.f,metricDistance))-radius;
                g.foam[i]=a.foam*inverseWeight;
            }
        }
        // Only smooth the reconstructed scalar field over a sub-marker length.
        // This does not add wave noise, invented spray, or new motion. It reduces
        // quadrature roughness from irregular FLIP markers before differentiation.
        for(int pass=0;pass<opt.smooth;pass++){
            std::vector<float> next=g.phi;
            #pragma omp parallel for schedule(static)
            for(int z=1;z<g.nz-1;z++)for(int y=1;y<g.ny-1;y++)for(int x=1;x<g.nx-1;x++){
                int i=g.index(x,y,z);float c=g.phi[i];
                if(std::abs(c)>g.step*2)continue;
                float avg=(g.phi[i-1]+g.phi[i+1]+g.phi[i-g.nx]+g.phi[i+g.nx]+g.phi[i-g.nx*g.ny]+g.phi[i+g.nx*g.ny])/6.f;
                next[i]=mix(c,avg,.28f);
            }
            g.phi.swap(next);
        }
        std::cerr<<"  implicit_grid "<<g.nx<<"x"<<g.ny<<"x"<<g.nz<<" nodes "<<nodes<<" voxel "<<g.step;
        return g;
    }
    std::vector<SurfaceTriangle> triangulate(const FieldGrid &g,float iso=0){
        if(g.phi.empty())return {};
        static constexpr int corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        static constexpr int tets[6][4]={{0,5,1,6},{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6}};
        // All six tetrahedra have positive integer determinant. For the vertex
        // orders below, winding therefore depends only on their local sign mask,
        // never on a near-zero rounded face normal or a smoothed shading normal.
        static constexpr bool reverseCase[16]={false,false,true,true,false,false,true,false,true,true,false,true,true,false,true,false};
        const float endpointGuard=g.endpointGuard();
        // Count each deterministic z slice first, then write straight into one
        // exact-size mesh allocation. This avoids simultaneous per-slice meshes
        // and a second full concatenated copy at native production resolution.
        auto extractSlice=[&](int z,SurfaceTriangle *output,size_t *zeroArea){
            size_t count=0;
            for(int y=0;y<g.ny-1;y++)for(int x=0;x<g.nx-1;x++){
                float values[8];int signMask=0,indices[8];
                for(int k=0;k<8;k++){
                    indices[k]=g.index(x+corner[k][0],y+corner[k][1],z+corner[k][2]);
                    values[k]=g.value(indices[k],iso);if(values[k]<0)signMask|=1<<k;
                }
                if(signMask==0||signMask==255)continue;
                V3 positions[8],gradients[8];
                for(int k=0;k<8;k++){
                    int cx=x+corner[k][0],cy=y+corner[k][1],cz=z+corner[k][2];
                    positions[k]=g.point(cx,cy,cz);gradients[k]=g.gradient(cx,cy,cz,iso);
                }
                auto vertex=[&](int a,int b){
                    // Canonical global edge order makes shared intersections
                    // bit-identical, including across adjacent lattice cells.
                    if(indices[a]>indices[b])std::swap(a,b);
                    // The guard is canonical per global edge. It prevents cuts
                    // from collapsing onto a lattice endpoint in stored float32
                    // positions, without discarding any valid tiny facets.
                    float t=clamp(values[a]/(values[a]-values[b]),endpointGuard,1.f-endpointGuard);
                    return SurfaceVertex{mix(positions[a],positions[b],t),unit(mix(gradients[a],gradients[b],t)),mix(g.foam[indices[a]],g.foam[indices[b]],t)};
                };
                int tetMask=0;
                auto triangle=[&](SurfaceVertex a,SurfaceVertex b,SurfaceVertex c){
                    if(reverseCase[tetMask])std::swap(b,c);
                    double ax=double(b.p.x)-a.p.x,ay=double(b.p.y)-a.p.y,az=double(b.p.z)-a.p.z;
                    double bx=double(c.p.x)-a.p.x,by=double(c.p.y)-a.p.y,bz=double(c.p.z)-a.p.z;
                    double fx=ay*bz-az*by,fy=az*bx-ax*bz,fz=ax*by-ay*bx;
                    double area2=std::sqrt(fx*fx+fy*fy+fz*fz);
                    if(area2==0){if(zeroArea)(*zeroArea)++;return;}
                    V3 gn{float(fx/area2),float(fy/area2),float(fz/area2)};
                    auto hemisphere=[&](V3 n){float d=dot(n,gn);return d>=.05f?n:unit(n+gn*(.05f-d));};
                    a.n=hemisphere(a.n);b.n=hemisphere(b.n);c.n=hemisphere(c.n);
                    if(output)output[count]={a,b,c};
                    count++;
                };
                for(const auto &tet:tets){
                    int inside[4],outside[4],ni=0,no=0;tetMask=0;
                    for(int j=0;j<4;j++){int k=tet[j];if(values[k]<0){inside[ni++]=k;tetMask|=1<<j;}else outside[no++]=k;}
                    if(ni==0||ni==4)continue;
                    if(ni==1)triangle(vertex(inside[0],outside[0]),vertex(inside[0],outside[1]),vertex(inside[0],outside[2]));
                    else if(ni==3)triangle(vertex(outside[0],inside[0]),vertex(outside[0],inside[1]),vertex(outside[0],inside[2]));
                    else{
                        SurfaceVertex a=vertex(inside[0],outside[0]),b=vertex(inside[1],outside[0]);
                        SurfaceVertex c=vertex(inside[1],outside[1]),d=vertex(inside[0],outside[1]);
                        triangle(a,b,c);triangle(a,c,d);
                    }
                }
            }
            return count;
        };
        std::vector<size_t> counts(g.nz-1),offsets(g.nz-1),zeroAreas(g.nz-1);
        #pragma omp parallel for schedule(dynamic,2)
        for(int z=0;z<g.nz-1;z++)counts[z]=extractSlice(z,nullptr,&zeroAreas[z]);
        size_t count=0,zeroArea=0;for(int z=0;z<g.nz-1;z++){offsets[z]=count;count+=counts[z];zeroArea+=zeroAreas[z];}
        if(zeroArea)throw std::runtime_error("Canonical edge guard left "+std::to_string(zeroArea)+" unrepresentable zero-area triangles; reconstruction aborted before writing a holed mesh");
        std::vector<SurfaceTriangle> result(count);
        if(count){
            #pragma omp parallel for schedule(dynamic,2)
            for(int z=0;z<g.nz-1;z++)extractSlice(z,result.data()+offsets[z],nullptr);
        }
        std::cerr<<" triangles "<<count;
        return result;
    }

};
}

int main(int argc,char **argv){
    using namespace water;
    try{
        std::string input,out="water.mesh",obj,spheres,solidPath,phaseIn,phaseOut;bool coherentFragments=false;
        Options options;int threads=4;bool correctVolume=true;float maxOffsetScale=.5f,contactPadding=.0003f;
        for(int i=1;i<argc;i++){
            std::string a=argv[i];auto next=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
            if(a=="--in")input=next();else if(a=="--out")out=next();else if(a=="--obj")obj=next();
            else if(a=="--spheres")spheres=next();else if(a=="--surface-scale")options.surfaceScale=std::stof(next());
            else if(a=="--support")options.supportScale=std::stof(next());else if(a=="--smooth")options.smooth=std::stoi(next());
            else if(a=="--threads")threads=std::stoi(next());
            else if(a=="--coherent-fragments")coherentFragments=true;
            else if(a=="--phase-in")phaseIn=next();else if(a=="--phase-out")phaseOut=next();
            else if(a=="--solid-field")solidPath=next();else if(a=="--no-volume-correction")correctVolume=false;
            else if(a=="--max-iso-offset")maxOffsetScale=std::stof(next());else if(a=="--contact-padding")contactPadding=std::stof(next());
            else throw std::runtime_error("Unknown option "+a);
        }
        if(input.empty())throw std::runtime_error("Usage: surface --in frame.bin --out water.mesh [--spheres water.csv] [--obj water.obj]");
        if(options.surfaceScale<.3f||options.surfaceScale>2||options.supportScale<2||options.supportScale>4.5f||options.smooth<0||options.smooth>8)throw std::runtime_error("Invalid reconstruction scale");
        omp_set_num_threads(threads);auto started=std::chrono::steady_clock::now();
        Snapshot s=readSnapshot(input);Reconstruction r(s.header.spacing,options);
        std::filesystem::path base(input);base.replace_extension();
        std::ifstream v(base.string()+".volumes",std::ios::binary);
        if(v){r.volumes.resize(s.particles.size());v.read(reinterpret_cast<char*>(r.volumes.data()),r.volumes.size()*sizeof(float));if(!v)throw std::runtime_error("Truncated volume companion");}
        r.coherentFragments=coherentFragments;
        if(coherentFragments){
            std::ifstream idfile(base.string()+".ids",std::ios::binary);
            if(!idfile)throw std::runtime_error("Coherent fragments require stable marker IDs");
            r.ids.resize(s.particles.size());idfile.read(reinterpret_cast<char*>(r.ids.data()),r.ids.size()*4);
            if(!idfile)throw std::runtime_error("Truncated marker IDs");
            if(!phaseIn.empty()){
                std::ifstream pf(phaseIn,std::ios::binary);char magic[8];uint32_t count=0;
                pf.read(magic,8);pf.read(reinterpret_cast<char*>(&count),4);
                if(!pf||std::memcmp(magic,"CYBRPH01",8)||count>10000000)throw std::runtime_error("Invalid prior fragment phase");
                for(uint32_t j=0;j<count;j++){
                    uint32_t id;uint8_t phase;pf.read(reinterpret_cast<char*>(&id),4);pf.read(reinterpret_cast<char*>(&phase),1);
                    if(!pf||id>10000000||phase>1)throw std::runtime_error("Bad fragment phase record");
                    if(r.previousPhase.size()<=id)r.previousPhase.resize(size_t(id)+1,255);
                    r.previousPhase[id]=phase;
                }
            }
        }
        r.measureAnisotropy(s);auto grid=r.buildImplicit(s);
        if(!solidPath.empty()){
            SolidGrid solid;solid.read(solidPath);grid.solidConstraint.resize(grid.phi.size());
            #pragma omp parallel for schedule(static)
            for(int z=0;z<grid.nz;z++)for(int y=0;y<grid.ny;y++)for(int x=0;x<grid.nx;x++)
                grid.solidConstraint[grid.index(x,y,z)]=-solid.sample(grid.point(x,y,z))-contactPadding;
        }
        double denseVolume=0;
        for(size_t i=0;i<s.particles.size();i++)if(!r.particleMetrics[i].droplet)denseVolume+=r.volumes.empty()?std::pow(s.header.spacing,3):r.volumes[i];
        auto measure=[](const auto &m){
            double v=0;
            #pragma omp parallel for reduction(+:v) schedule(static)
            for(size_t i=0;i<m.size();i++){
                const auto&t=m[i];const auto&a=t.a.p;const auto&b=t.b.p;const auto&c=t.c.p;
                v+=(double(a.x)*(double(b.y)*c.z-double(b.z)*c.y)+double(a.y)*(double(b.z)*c.x-double(b.x)*c.z)+double(a.z)*(double(b.x)*c.y-double(b.y)*c.x))/6.;
            }
            return v;
        };
        auto mesh=r.triangulate(grid);double uncorrectedVolume=measure(mesh),measured=uncorrectedVolume;float iso=0;int volumeIterations=0;
        if(correctVolume&&denseVolume>0&&std::abs(measured-denseVolume)>denseVolume*.002){
            float bound=maxOffsetScale*s.header.spacing,lo=0,hi=0;double vlo=measured,vhi=measured;
            if(measured<denseVolume){hi=bound;mesh.clear();mesh.shrink_to_fit();mesh=r.triangulate(grid,hi);vhi=measure(mesh);}
            else{lo=-bound;mesh.clear();mesh.shrink_to_fit();mesh=r.triangulate(grid,lo);vlo=measure(mesh);}
            if(!(vlo<=denseVolume&&vhi>=denseVolume))throw std::runtime_error("Dense marker volume is outside the bounded surface-offset bracket: target="+std::to_string(denseVolume)+", range="+std::to_string(vlo)+".."+std::to_string(vhi));
            for(int pass=0;pass<10;pass++){
                float fraction=clamp(float((denseVolume-vlo)/(vhi-vlo)),.1f,.9f);iso=mix(lo,hi,fraction);
                mesh.clear();mesh.shrink_to_fit();mesh=r.triangulate(grid,iso);measured=measure(mesh);volumeIterations++;
                if(std::abs(measured-denseVolume)<=denseVolume*.002)break;
                if(measured<denseVolume){lo=iso;vlo=measured;}else{hi=iso;vhi=measured;}
            }
            if(std::abs(measured-denseVolume)>denseVolume*.003)throw std::runtime_error("Surface volume solve did not converge");
        }
        std::filesystem::path op(out);if(op.has_parent_path())std::filesystem::create_directories(op.parent_path());
        std::string temporary=out+".tmp";std::ofstream f(temporary,std::ios::binary);
        f.write("CYBRMSH1",8);uint32_t version=1,flags=0,material=0;uint64_t n=mesh.size();
        f.write(reinterpret_cast<char*>(&version),4);f.write(reinterpret_cast<char*>(&flags),4);f.write(reinterpret_cast<char*>(&n),8);
        double signedVolume=measured;V3 lo(INF),hi(-INF);
        for(const auto &t:mesh){
            float data[18]={t.a.p.x,t.a.p.y,t.a.p.z,t.a.n.x,t.a.n.y,t.a.n.z,t.b.p.x,t.b.p.y,t.b.p.z,t.b.n.x,t.b.n.y,t.b.n.z,t.c.p.x,t.c.p.y,t.c.p.z,t.c.n.x,t.c.n.y,t.c.n.z};
            f.write(reinterpret_cast<const char*>(data),sizeof(data));f.write(reinterpret_cast<char*>(&material),4);
            for(const auto &p:{t.a.p,t.b.p,t.c.p}){lo=vmin(lo,p);hi=vmax(hi,p);}
        }
        f.close();if(!f)throw std::runtime_error("Mesh write failed");std::filesystem::rename(temporary,out);
        if(!obj.empty()){
            std::ofstream o(obj);o<<std::setprecision(8)<<"# Actual FLIP liquid reconstruction\nusemtl water\n";
            for(const auto&t:mesh)for(const auto&p:{t.a,t.b,t.c})o<<"v "<<p.p.x<<' '<<p.p.y<<' '<<p.p.z<<"\nvn "<<p.n.x<<' '<<p.n.y<<' '<<p.n.z<<'\n';
            for(size_t i=0;i<mesh.size();i++){size_t k=i*3+1;o<<"f "<<k<<"//"<<k<<' '<<k+1<<"//"<<k+1<<' '<<k+2<<"//"<<k+2<<'\n';}
            if(!o)throw std::runtime_error("OBJ write failed");
        }
        uint64_t secondaryCount=0,primaryDrops=0;double primaryVolume=0;
        for(size_t i=0;i<s.particles.size();i++)primaryVolume+=r.volumes.empty()?std::pow(s.header.spacing,3):r.volumes[i];
        if(!spheres.empty()){
            std::ofstream w(spheres);w<<std::setprecision(8)<<"x,y,z,r,vx,vy,vz,type\n";
            for(size_t i=0;i<s.particles.size();i++)if(r.particleMetrics[i].droplet){
                const auto &p=s.particles[i];float volume=r.volumes.empty()?std::pow(s.header.spacing,3):r.volumes[i];float radius=std::cbrt(3.f*volume/(4.f*PI));
                w<<p.x<<','<<p.y<<','<<p.z<<','<<radius<<','<<p.vx<<','<<p.vy<<','<<p.vz<<",0\n";primaryDrops++;
            }
            std::ifstream ww(base.string()+".white",std::ios::binary);
            if(ww){
                Header h{};ww.read(reinterpret_cast<char*>(&h),sizeof(h));if(!ww||std::memcmp(h.magic,"CYBRWW01",8)||h.version!=1||h.count>10000000)throw std::runtime_error("Invalid whitewater file");
                for(uint32_t i=0;i<h.count;i++){
                    float q[9];uint32_t type;ww.read(reinterpret_cast<char*>(q),sizeof(q));ww.read(reinterpret_cast<char*>(&type),4);if(!ww||type>2)throw std::runtime_error("Invalid whitewater record");
                    if(q[7]>=q[8]||q[6]<=0)continue;
                    w<<q[0]<<','<<q[1]<<','<<q[2]<<','<<q[6]<<','<<q[3]<<','<<q[4]<<','<<q[5]<<','<<type<<'\n';secondaryCount++;
                }
            }
            if(!w)throw std::runtime_error("Spheres write failed");
        }
        uint64_t phaseChanges=0;
        if(coherentFragments&&!phaseOut.empty()){
            std::ofstream pf(phaseOut+".tmp",std::ios::binary);pf.write("CYBRPH01",8);
            uint32_t count=uint32_t(r.ids.size());pf.write(reinterpret_cast<char*>(&count),4);
            for(size_t i=0;i<r.ids.size();i++){
                uint32_t id=r.ids[i];uint8_t phase=r.particleMetrics[i].droplet?1:0;
                if(id<r.previousPhase.size()&&r.previousPhase[id]<2&&r.previousPhase[id]!=phase)phaseChanges++;
                pf.write(reinterpret_cast<char*>(&id),4);pf.write(reinterpret_cast<char*>(&phase),1);
            }
            pf.close();if(!pf)throw std::runtime_error("Failed fragment phase write");
            std::filesystem::rename(phaseOut+".tmp",phaseOut);
        }
        double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        std::ofstream report(out+".json");
        double guard=grid.endpointGuard(),ulp=grid.coordinateUlp();
        double endpointBound=std::sqrt(3.)*(guard*(double(grid.step)+2*ulp)+2*ulp);
        report<<std::setprecision(10)<<"{\n  \"mesh_volume_arithmetic\": \"Float64 cross/dot with parallel double reduction on stored float32 vertices.\",\n  \"edge_endpoint_guard_fraction\": "<<guard<<",\n  \"edge_endpoint_guard_displacement_bound_m\": "<<endpointBound<<",\n  \"edge_endpoint_guard_scope\": \"Canonical edge interpolation uses eight float32 coordinate steps. Bound applies at fixed sampled field and isovalue; volume correction may change the selected isovalue. Valid tiny faces are retained; any remaining exact zero-area face aborts reconstruction.\",\n";
        report<<"  \"coherent_fragment_hysteresis\": "<<(coherentFragments?"true":"false")<<",\n  \"fragment_phase_changes\": "<<phaseChanges<<",\n";
        report<<std::setprecision(10)<<"  \"frame\": "<<s.header.frame<<",\n  \"time_s\": "<<s.header.time<<",\n  \"primary_markers\": "<<s.particles.size()<<",\n  \"nominal_marker_spacing_m\": "<<s.header.spacing<<",\n  \"surface_voxel_m\": "<<grid.step<<",\n  \"support_markers\": "<<options.supportScale<<",\n  \"scalar_smoothing_passes\": "<<options.smooth<<",\n  \"triangles\": "<<mesh.size()<<",\n  \"mesh_signed_volume_m3\": "<<signedVolume<<",\n  \"uncorrected_mesh_volume_m3\": "<<uncorrectedVolume<<",\n  \"dense_marker_target_volume_m3\": "<<denseVolume<<",\n  \"volume_correction_iso_m\": "<<iso<<",\n  \"volume_correction_iterations\": "<<volumeIterations<<",\n  \"solid_constraint\": "<<(solidPath.empty()?"false":"true")<<",\n  \"solid_contact_padding_m\": "<<contactPadding<<",\n  \"primary_volume_m3\": "<<primaryVolume<<",\n  \"isolated_primary_spheres\": "<<primaryDrops<<",\n  \"secondary_visual_parcels\": "<<secondaryCount<<",\n  \"secondary_mass_note\": \"Whitewater parcels sample common liquid flow; primary marker volume is retained, not reassigned to secondary particles.\",\n  \"bounds\": [["<<lo.x<<','<<lo.y<<','<<lo.z<<"],["<<hi.x<<','<<hi.y<<','<<hi.z<<"]],\n  \"seconds\": "<<seconds<<"\n}\n";
        std::cerr<<" primary_drops "<<primaryDrops<<" secondary "<<secondaryCount<<" seconds "<<seconds<<'\n';
        return 0;
    }catch(const std::exception&e){std::cerr<<"surface error: "<<e.what()<<'\n';return 1;}
}
