// CYBR WATER VFX — native C++17 surface transport renderer.
//
// This is a Monte Carlo renderer of the exported simulation surface. There is
// no generated bitmap, screen-space reflection, painted water color, or raster
// background substitution. Sun caustics optionally use a finite-radius photon
// estimate, explicitly separated from camera-path estimates to avoid counting
// the same light paths twice. Input positions and dimensions are in metres.
//
// Build: c++ -O3 -march=native -fopenmp -std=c++17 src/trace.cpp -o build/trace

#include <algorithm>
#include <array>
#include <atomic>
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
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "openmp_compat.hpp"
#include "plane_precision.hpp"

namespace cybr {
constexpr float PI = 3.14159265358979323846f;
constexpr float INF = 1.0e30f;
// Strictly positive segments; origin displacement follows Float64 position
// error instead of a fixed world-space distance that can cross a thin sheet.
constexpr double RAY_EPS = 0.0;
inline float clamp(float x, float lo=0.f, float hi=1.f) {
    return std::max(lo, std::min(hi, x));
}
inline float sq(float x) { return x*x; }
inline float mix(float a, float b, float t) { return a+(b-a)*t; }
inline float smooth(float a, float b, float x) {
    float t=clamp((x-a)/(b-a)); return t*t*(3-2*t);
}
struct V3 {
    float x=0, y=0, z=0;
    V3()=default;
    V3(float a,float b,float c):x(a),y(b),z(c){}
    explicit V3(float a):x(a),y(a),z(a){}
    float operator[](int a) const { return a==0?x:(a==1?y:z); }
    float &operator[](int a) { return a==0?x:(a==1?y:z); }
    V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
    V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
    V3 operator-()const{return {-x,-y,-z};}
    V3 operator*(float b)const{return {x*b,y*b,z*b};}
    V3 operator/(float b)const{return *this*(1.f/b);}
    V3 operator*(V3 b)const{return {x*b.x,y*b.y,z*b.z};}
    V3 operator/(V3 b)const{return {x/b.x,y/b.y,z/b.z};}
    V3 &operator+=(V3 b){x+=b.x;y+=b.y;z+=b.z;return *this;}
    V3 &operator*=(V3 b){x*=b.x;y*=b.y;z*=b.z;return *this;}
    V3 &operator*=(float b){x*=b;y*=b;z*=b;return *this;}
};
inline V3 operator*(float a,V3 b){return b*a;}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length2(V3 a){return dot(a,a);}
inline float length(V3 a){return std::sqrt(length2(a));}
inline V3 unit(V3 a){return a/std::sqrt(std::max(1e-30f,length2(a)));}
inline V3 vmin(V3 a,V3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
inline V3 vmax(V3 a,V3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
inline V3 mix(V3 a,V3 b,float t){return a+(b-a)*t;}
inline V3 expneg(V3 a){return {std::exp(-a.x),std::exp(-a.y),std::exp(-a.z)};}
inline float maxcomp(V3 a){return std::max(a.x,std::max(a.y,a.z));}
inline float lum(V3 a){return .2126f*a.x+.7152f*a.y+.0722f*a.z;}
inline bool finite(V3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
struct D3 {
    double x=0,y=0,z=0;
    D3()=default;D3(double a,double b,double c):x(a),y(b),z(c){}
    D3(V3 p):x(p.x),y(p.y),z(p.z){}
    double operator[](int i)const{return i==0?x:(i==1?y:z);}
    D3 operator+(D3 b)const{return {x+b.x,y+b.y,z+b.z};}
    D3 operator-(D3 b)const{return {x-b.x,y-b.y,z-b.z};}
    D3 operator*(double a)const{return {x*a,y*a,z*a};}
    V3 floats()const{return {float(x),float(y),float(z)};}
};
inline double dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline D3 absolute(D3 p){return {std::abs(p.x),std::abs(p.y),std::abs(p.z)};}
inline double arithmeticGamma(int operations){
    double e=operations*(std::numeric_limits<double>::epsilon()*.5);return e/(1-e);
}
// Preserve every exact nonzero Float32 face, including valid microfacets on
// otherwise macroscopic closed water. All area arithmetic is Float64.
inline bool triangleNormal(V3 a,V3 b,V3 c,V3 &normal){
    D3 u=D3(b)-D3(a),v=D3(c)-D3(a);
    D3 g{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
    double areaSquared=dot(g,g);
    if(!std::isfinite(areaSquared))throw std::runtime_error("Nonfinite triangle area");
    if(areaSquared==0)return false;
    normal=(g*(1.0/std::sqrt(areaSquared))).floats();return true;
}
inline V3 reflect(V3 incident,V3 n){return incident-2.f*dot(incident,n)*n;}
inline uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;x^=x>>16;return x;}
struct RNG {
    uint64_t state=1, inc=3;
    explicit RNG(uint64_t seed=1){state=0;inc=(seed<<1)|1;next();state+=seed^0x9e3779b97f4a7c15ULL;next();}
    uint32_t next(){uint64_t o=state;state=o*6364136223846793005ULL+inc;
        uint32_t x=uint32_t(((o>>18)^o)>>27),r=uint32_t(o>>59);return (x>>r)|(x<<((-r)&31));}
    float uniform(){return (next()>>8)*(1.f/16777216.f);}
};
struct Frame {
    V3 x,y,z;
    explicit Frame(V3 n):z(n){x=unit(std::abs(n.z)<.999f?cross(V3(0,0,1),n):cross(V3(0,1,0),n));y=cross(z,x);}
    V3 world(V3 p)const{return x*p.x+y*p.y+z*p.z;}
    V3 local(V3 p)const{return {dot(p,x),dot(p,y),dot(p,z)};}
};
inline V3 uniformSphere(float a,float b){float z=1-2*a,r=std::sqrt(std::max(0.f,1-z*z)),p=2*PI*b;return {r*std::cos(p),r*std::sin(p),z};}
inline V3 cosineHemisphere(float a,float b){float r=std::sqrt(a),p=2*PI*b;return {r*std::cos(p),r*std::sin(p),std::sqrt(1-a)};}
inline V3 uniformCone(float a,float b,float cosMax,V3 axis){float z=mix(cosMax,1.f,a),r=std::sqrt(std::max(0.f,1-z*z)),p=2*PI*b;return Frame(axis).world({r*std::cos(p),r*std::sin(p),z});}
inline std::pair<float,float> concentricDisk(float u,float v){
    float a=2*u-1,b=2*v-1;if(a==0&&b==0)return {0,0};
    float r,p;if(std::abs(a)>std::abs(b)){r=a;p=(PI/4)*(b/a);}else{r=b;p=PI/2-(PI/4)*(a/b);}
    return {r*std::cos(p),r*std::sin(p)};
}

// Deterministic object-space material noise only; never displaces simulated water.
inline float lattice(int x,int y,int z){return (hash(uint32_t(x)*73856093u^uint32_t(y)*19349663u^uint32_t(z)*83492791u)&0xffffff)*(1.f/16777215.f);}
float noise(V3 p){
    int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    float a=p.x-x,b=p.y-y,c=p.z-z;a=a*a*a*(a*(a*6-15)+10);b=b*b*b*(b*(b*6-15)+10);c=c*c*c*(c*(c*6-15)+10);
    return mix(mix(mix(lattice(x,y,z),lattice(x+1,y,z),a),mix(lattice(x,y+1,z),lattice(x+1,y+1,z),a),b),
               mix(mix(lattice(x,y,z+1),lattice(x+1,y,z+1),a),mix(lattice(x,y+1,z+1),lattice(x+1,y+1,z+1),a),b),c);
}
float fbm(V3 p,int oct=4){float f=0,a=.5f;for(int i=0;i<oct;i++){f+=a*noise(p);p=p*2.03f+V3(9.31f,17.71f,6.19f);a*=.5f;}return f;}

struct Ray {
    V3 o,d,inv;
    D3 exactOrigin;
    float time=0;
    int kx=0,ky=1,kz=2;double sx=0,sy=0,sz=1;
    Ray(D3 o_,V3 d_,float time_=0):o(o_.floats()),d(d_),exactOrigin(o_),time(time_){
        inv={std::abs(d.x)>1e-30f?1.f/d.x:std::copysign(INF,d.x),
             std::abs(d.y)>1e-30f?1.f/d.y:std::copysign(INF,d.y),
             std::abs(d.z)>1e-30f?1.f/d.z:std::copysign(INF,d.z)};
        kz=std::abs(d.y)>std::abs(d.x)?1:0;if(std::abs(d.z)>std::abs(d[kz]))kz=2;
        kx=(kz+1)%3;ky=(kx+1)%3;if(d[kz]<0)std::swap(kx,ky);
        sx=double(d[kx])/d[kz];sy=double(d[ky])/d[kz];sz=1.0/d[kz];
    }
};
struct Bounds {
    V3 lo{INF},hi{-INF};
    void grow(V3 p){lo=vmin(lo,p);hi=vmax(hi,p);}
    void grow(const Bounds &b){lo=vmin(lo,b.lo);hi=vmax(hi,b.hi);}
    V3 center()const{return (lo+hi)*.5f;}
    float area()const{V3 d=vmax(hi-lo,V3(0));return 2*(d.x*d.y+d.y*d.z+d.z*d.x);}
    bool intersect(const Ray &r,float maxt,float &near)const{
        V3 a=(lo-r.o)*r.inv,b=(hi-r.o)*r.inv;
        V3 l=vmin(a,b),h=vmax(a,b);
        near=std::max(0.f,std::max(l.x,std::max(l.y,l.z)));
        float far=std::min(maxt,std::min(h.x,std::min(h.y,h.z)));
        return near<=far;
    }
};
enum Mat:uint32_t { WATER=0, ROCK=1, SAND=2, MOSS=3, FOAM=4, BUBBLE=5, LIGHT=6, DARK=7 };
struct Triangle {
    V3 a,b,c;
    V3 na,nb,nc;
    V3 da,db,dc;
    uint32_t mat=ROCK,object=0;
    bool motion=false;
    Bounds bounds()const{
        Bounds box;box.grow(a);box.grow(b);box.grow(c);
        if(motion){box.grow(a+da);box.grow(b+db);box.grow(c+dc);}
        box.lo=box.lo-V3(1e-6f);box.hi=box.hi+V3(1e-6f);return box;
    }
};
struct Sphere {V3 c,dc;float r;uint32_t mat,object;};
struct Hit {
    double t=INF;V3 p,ng,ns;D3 exactP,pError;uint32_t mat=ROCK,object=0,primitive=0;
    bool front=true;
};
struct Node {Bounds bounds;uint32_t first=0,count=0,right=0;};
struct BVH {
    std::vector<Triangle> triangles;
    std::vector<Sphere> spheres;
    std::vector<Node> nodes;
    std::vector<uint32_t> indices;
    std::vector<Bounds> primitiveBounds;
    Bounds waterBounds;
    uint32_t maxDepth=0;
    static constexpr uint32_t SPHERE_BIT=0x80000000u;
    bool addTriangle(V3 a,V3 b,V3 c,V3 na,V3 nb,V3 nc,uint32_t mat,uint32_t object){
        V3 ng;if(!triangleNormal(a,b,c,ng))return false;
        Triangle t;t.a=a;t.b=b;t.c=c;t.na=na;t.nb=nb;t.nc=nc;t.mat=mat;t.object=object;
        triangles.push_back(t);return true;
    }
    uint32_t buildNode(uint32_t begin,uint32_t end,uint32_t depth){
        uint32_t at=uint32_t(nodes.size());nodes.push_back({});
        Bounds bounds,centers;
        for(uint32_t i=begin;i<end;i++){bounds.grow(primitiveBounds[indices[i]]);centers.grow(primitiveBounds[indices[i]].center());}
        nodes[at].bounds=bounds;maxDepth=std::max(maxDepth,depth);
        uint32_t n=end-begin;
        if(n<=4||depth>=60){nodes[at].first=begin;nodes[at].count=n;return at;}
        constexpr int BINS=16;
        struct Bin {Bounds bounds;uint32_t count=0;};
        float best=float(n)*bounds.area();int bestAxis=-1,bestSplit=-1;
        for(int axis=0;axis<3;axis++){
            float span=centers.hi[axis]-centers.lo[axis];if(span<1e-8f)continue;
            std::array<Bin,BINS> bins{};
            for(uint32_t i=begin;i<end;i++){
                const Bounds &b=primitiveBounds[indices[i]];
                int k=std::min(BINS-1,int((b.center()[axis]-centers.lo[axis])*BINS/span));
                bins[k].count++;bins[k].bounds.grow(b);
            }
            std::array<Bounds,BINS> left,right;std::array<uint32_t,BINS> nl{},nr{};
            Bounds l,r;uint32_t lc=0,rc=0;
            for(int j=0;j<BINS;j++){l.grow(bins[j].bounds);lc+=bins[j].count;left[j]=l;nl[j]=lc;
                int k=BINS-1-j;r.grow(bins[k].bounds);rc+=bins[k].count;right[k]=r;nr[k]=rc;}
            for(int k=0;k<BINS-1;k++)if(nl[k]&&nr[k+1]){
                float cost=left[k].area()*nl[k]+right[k+1].area()*nr[k+1]+bounds.area()*.35f;
                if(cost<best){best=cost;bestAxis=axis;bestSplit=k;}
            }
        }
        uint32_t mid=begin;
        if(bestAxis>=0){
            float span=centers.hi[bestAxis]-centers.lo[bestAxis];
            auto p=std::partition(indices.begin()+begin,indices.begin()+end,[&](uint32_t id){
                int bin=std::min(BINS-1,int((primitiveBounds[id].center()[bestAxis]-centers.lo[bestAxis])*BINS/span));return bin<=bestSplit;});
            mid=uint32_t(p-indices.begin());
        }
        if(mid==begin||mid==end){
            if(n<=12){nodes[at].first=begin;nodes[at].count=n;return at;}
            V3 d=centers.hi-centers.lo;int axis=d.y>d.x?1:0;if(d.z>d[axis])axis=2;
            mid=begin+n/2;
            std::nth_element(indices.begin()+begin,indices.begin()+mid,indices.begin()+end,[&](uint32_t a,uint32_t b){return primitiveBounds[a].center()[axis]<primitiveBounds[b].center()[axis];});
        }
        uint32_t l=buildNode(begin,mid,depth+1),r=buildNode(mid,end,depth+1);
        nodes[at].first=l;nodes[at].right=r;return at;
    }
    void build(){
        primitiveBounds.clear();indices.clear();nodes.clear();maxDepth=0;
        primitiveBounds.reserve(triangles.size()+spheres.size());
        for(const auto &t:triangles){primitiveBounds.push_back(t.bounds());if(t.mat==WATER)waterBounds.grow(t.bounds());}
        for(const auto &s:spheres){Bounds b;b.grow(s.c-V3(s.r));b.grow(s.c+V3(s.r));b.grow(s.c+s.dc-V3(s.r));b.grow(s.c+s.dc+V3(s.r));primitiveBounds.push_back(b);}
        indices.resize(primitiveBounds.size());std::iota(indices.begin(),indices.end(),0u);
        nodes.reserve(indices.size()*2);if(!indices.empty())buildNode(0,uint32_t(indices.size()),0);
        // Convert the sorted index into a compact primitive tag after building.
        for(uint32_t &i:indices)if(i>=triangles.size())i=SPHERE_BIT|uint32_t(i-triangles.size());
        primitiveBounds.clear();primitiveBounds.shrink_to_fit();
    }
    bool triangleHit(uint32_t id,const Ray &r,Hit &h)const{
        const Triangle &t=triangles[id];V3 a=t.a,b=t.b,c=t.c;
        if(t.motion){a+=t.da*r.time;b+=t.db*r.time;c+=t.dc*r.time;}
        // Watertight dominant-axis shear/edge test. Adjacent triangles perform
        // the same edge arithmetic; near-zero edge products are recomputed in
        // double precision. No arbitrary determinant threshold opens tiny
        // exported water faces or shared triangle edges.
        D3 A=D3(a)-r.exactOrigin,B=D3(b)-r.exactOrigin,C=D3(c)-r.exactOrigin;
        double ax=A[r.kx]-r.sx*A[r.kz],ay=A[r.ky]-r.sy*A[r.kz];
        double bx=B[r.kx]-r.sx*B[r.kz],by=B[r.ky]-r.sy*B[r.kz];
        double cx=C[r.kx]-r.sx*C[r.kz],cy=C[r.ky]-r.sy*C[r.kz];
        double U=cx*by-cy*bx,V=ax*cy-ay*cx,W=bx*ay-by*ax;
        if((U<0||V<0||W<0)&&(U>0||V>0||W>0))return false;
        double det=U+V+W;if(det==0)return false;
        double az=r.sz*A[r.kz],bz=r.sz*B[r.kz],cz=r.sz*C[r.kz],T=U*az+V*bz+W*cz;
        // A cancellation bound selects extra precision; it never rejects a
        // positive segment or imposes a minimum spatial distance. Test both
        // signs before the ordinary sheared-T early rejection.
        double maxX=std::max({std::abs(ax),std::abs(bx),std::abs(cx)});
        double maxY=std::max({std::abs(ay),std::abs(by),std::abs(cy)});
        double maxZ=std::max({std::abs(az),std::abs(bz),std::abs(cz)});
        double maxE=std::max({std::abs(U),std::abs(V),std::abs(W)});
        double deltaZ=arithmeticGamma(3)*maxZ;
        double deltaX=arithmeticGamma(8)*(maxX+2*maxZ),deltaY=arithmeticGamma(8)*(maxY+2*maxZ);
        double deltaE=2*(arithmeticGamma(3)*maxX*maxY+deltaY*maxX+deltaX*maxY+deltaX*deltaY);
        double deltaT=3*(arithmeticGamma(5)*maxE*maxZ+deltaE*maxZ+deltaZ*maxE+deltaE*deltaZ);
        double inv=1.0/det,du=V*inv,dv=W*inv,d=T*inv;
        if(std::abs(T)<=deltaT){
            if constexpr(std::numeric_limits<long double>::digits>std::numeric_limits<double>::digits){
            long double ex=static_cast<long double>(b.x)-a.x,ey=static_cast<long double>(b.y)-a.y,ez=static_cast<long double>(b.z)-a.z;
            long double fx=static_cast<long double>(c.x)-a.x,fy=static_cast<long double>(c.y)-a.y,fz=static_cast<long double>(c.z)-a.z;
            long double nx=ey*fz-ez*fy,ny=ez*fx-ex*fz,nz=ex*fy-ey*fx;
            long double px=static_cast<long double>(a.x)-r.exactOrigin.x,py=static_cast<long double>(a.y)-r.exactOrigin.y,pz=static_cast<long double>(a.z)-r.exactOrigin.z;
            long double numerator=nx*px+ny*py+nz*pz,denominator=nx*r.d.x+ny*r.d.y+nz*r.d.z;
            if(denominator==0)return false;
            long double preciseT=numerator/denominator;
            if(preciseT<=0||preciseT>=h.t)return false;
            d=static_cast<double>(preciseT);
            }else{
                const double preciseT=cybr_precision::planeDistance(
                    {a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},
                    {r.exactOrigin.x,r.exactOrigin.y,r.exactOrigin.z},{r.d.x,r.d.y,r.d.z});
                if(!std::isfinite(preciseT)||preciseT<=0||preciseT>=h.t)return false;
                d=preciseT;
            }
        }else{
            if(det>0){if(T<=RAY_EPS*det||T>=h.t*det)return false;}
            else if(T>=RAY_EPS*det||T<=h.t*det)return false;
        }
        float u=float(du),v=float(dv);
        V3 ng;if(!triangleNormal(a,b,c,ng))return false;
        h.t=d;h.exactP=D3(a)*(1-du-dv)+D3(b)*du+D3(c)*dv;h.p=h.exactP.floats();h.ng=ng;
        // Conservative componentwise bound for Float64 barycentric position
        // reconstruction, including subtraction in the first weight.
        h.pError=(absolute(D3(a))+absolute(D3(b))+absolute(D3(c)))*arithmeticGamma(12);
        h.ns=unit(t.na*(1-u-v)+t.nb*u+t.nc*v);
        if(length2(h.ns)<.5f)h.ns=h.ng;
        if(dot(h.ns,h.ng)<0)h.ns=-h.ns;
        h.mat=t.mat;h.object=t.object;h.primitive=id;h.front=dot(r.d,h.ng)<0;return true;
    }
    bool sphereHit(uint32_t id,const Ray &r,Hit &h)const{
        const Sphere &s=spheres[id];V3 c=s.c+s.dc*r.time;D3 oc=r.exactOrigin-D3(c),direction(r.d);
        double directionLength2=dot(direction,direction),closestT=-dot(oc,direction)/directionLength2;
        // Closest approach avoids subtracting two large squared distances
        // when a distant ray intersects a small secondary droplet.
        D3 closest=oc+direction*closestT;
        double halfChord2=(double(s.r)*s.r-dot(closest,closest))/directionLength2;
        if(halfChord2<=0)return false;
        double halfChord=std::sqrt(halfChord2),t=closestT-halfChord;if(t<=RAY_EPS)t=closestT+halfChord;
        if(t<=RAY_EPS||t>=h.t)return false;
        D3 radial=oc+direction*t;double radialLength=std::sqrt(dot(radial,radial));
        if(radialLength==0)return false;
        D3 surfaceRadial=radial*(double(s.r)/radialLength);
        h.t=t;h.exactP=D3(c)+surfaceRadial;h.p=h.exactP.floats();
        h.pError=(absolute(D3(c))+absolute(surfaceRadial))*arithmeticGamma(12);
        h.ng=h.ns=(radial*(1.0/radialLength)).floats();h.mat=s.mat;h.object=s.object;h.primitive=SPHERE_BIT|id;h.front=dot(r.d,h.ng)<0;return true;
    }
    bool intersect(const Ray &r,Hit &h,bool any=false)const{
        if(nodes.empty())return false;
        struct Pending {uint32_t node;float near;};
        Pending stack[128];uint32_t top=0,at=0;bool found=false;float rootNear;
        if(!nodes[0].bounds.intersect(r,float(h.t),rootNear))return false;
        for(;;){
            const Node &n=nodes[at];
            if(n.count){
                for(uint32_t i=0;i<n.count;i++){
                    uint32_t id=indices[n.first+i];bool ok=(id&SPHERE_BIT)?sphereHit(id&~SPHERE_BIT,r,h):triangleHit(id,r,h);
                    if(ok){found=true;if(any)return true;}
                }
            }else{
                float ln,rn;bool l=nodes[n.first].bounds.intersect(r,float(h.t),ln),rr=nodes[n.right].bounds.intersect(r,float(h.t),rn);
                if(l&&rr){if(ln<rn){stack[top++]={n.right,rn};at=n.first;}else{stack[top++]={n.first,ln};at=n.right;}continue;}
                if(l){at=n.first;continue;}if(rr){at=n.right;continue;}
            }
            // The lower bound does not change when a closer hit is found.
            // Reuse it instead of testing the same child box a second time.
            while(top&&stack[top-1].near>h.t)--top;
            if(!top)break;at=stack[--top].node;
        }
        return found;
    }
    bool occluded(D3 o,V3 d,double maxT,float time)const{Hit h;h.t=maxT;return intersect(Ray(o,d,time),h,true);}
};

struct ObjIndex {int v=-1,n=-1;};
struct ObjFace {std::array<ObjIndex,3> indices;uint32_t mat,object;};
struct ObjData {std::vector<V3> p,n;std::vector<ObjFace> faces;};
uint32_t materialName(const std::string &s,uint32_t fallback){
    if(s.find("water")!=std::string::npos)return WATER;
    if(s.find("sand")!=std::string::npos)return SAND;
    if(s.find("moss")!=std::string::npos)return MOSS;
    if(s.find("foam")!=std::string::npos)return FOAM;
    if(s.find("bubble")!=std::string::npos)return BUBBLE;
    if(s.find("dark")!=std::string::npos)return DARK;
    if(s.find("rock")!=std::string::npos)return ROCK;return fallback;
}
ObjData readObj(const std::string &path,uint32_t defaultMat){
    std::ifstream in(path);if(!in)throw std::runtime_error("Cannot open OBJ "+path);
    ObjData data;std::string line;uint32_t mat=defaultMat,object=0;
    while(std::getline(in,line)){
        if(line.empty()||line[0]=='#')continue;
        std::istringstream ss(line);std::string tag;ss>>tag;
        if(tag=="v"||tag=="vn"){
            V3 p;ss>>p.x>>p.y>>p.z;if(!ss||!finite(p))throw std::runtime_error("Nonfinite/corrupt OBJ vertex");
            if(tag=="v")data.p.push_back(p);else data.n.push_back(unit(p));
        }else if(tag=="usemtl"){
            std::string name;ss>>name;mat=materialName(name,defaultMat);
        }else if(tag=="o"||tag=="g"){
            ++object;
        }else if(tag=="f"){
            std::vector<ObjIndex> face;std::string part;
            while(ss>>part){
                ObjIndex id;size_t a=part.find('/'),b=a==std::string::npos?a:part.find('/',a+1);
                int v=std::stoi(part.substr(0,a));id.v=v>0?v-1:int(data.p.size())+v;
                if(b!=std::string::npos&&b+1<part.size()){int n=std::stoi(part.substr(b+1));id.n=n>0?n-1:int(data.n.size())+n;}
                if(id.v<0||id.v>=int(data.p.size())||id.n>=int(data.n.size()))throw std::runtime_error("OBJ face index outside data");
                face.push_back(id);
            }
            for(size_t j=1;j+1<face.size();j++)data.faces.push_back({{face[0],face[j],face[j+1]},mat,object});
        }
    }
    if(data.p.empty()||data.faces.empty())throw std::runtime_error("Empty OBJ "+path);return data;
}
void appendObj(BVH &bvh,const std::string &path,uint32_t defaultMat,const std::string &endPath,uint32_t objectBase){
    ObjData d=readObj(path,defaultMat),end;
    bool motion=!endPath.empty();if(motion){end=readObj(endPath,defaultMat);if(end.p.size()!=d.p.size()||end.faces.size()!=d.faces.size())throw std::runtime_error("Motion mesh topology differs");}
    for(size_t i=0;i<d.faces.size();i++){
        const auto &f=d.faces[i];V3 a=d.p[f.indices[0].v],b=d.p[f.indices[1].v],c=d.p[f.indices[2].v];
        V3 ng;if(!triangleNormal(a,b,c,ng))continue;
        V3 na=f.indices[0].n<0?ng:d.n[f.indices[0].n],nb=f.indices[1].n<0?ng:d.n[f.indices[1].n],nc=f.indices[2].n<0?ng:d.n[f.indices[2].n];
        if(!bvh.addTriangle(a,b,c,na,nb,nc,f.mat,objectBase+f.object))continue;
        if(motion){
            const auto &ef=end.faces[i];for(int k=0;k<3;k++)if(ef.indices[k].v!=f.indices[k].v)throw std::runtime_error("Motion mesh index order differs");
            auto &t=bvh.triangles.back();V3 aa=end.p[f.indices[0].v],bb=end.p[f.indices[1].v],cc=end.p[f.indices[2].v];
            t.da=aa-a;t.db=bb-b;t.dc=cc-c;t.motion=true;
        }
    }
}
struct MeshHeader {char magic[8];uint32_t version,flags;uint64_t triangleCount;};
struct MeshVertex {float px,py,pz,nx,ny,nz;};
struct MeshRecord {MeshVertex vertices[3];uint32_t material;};
static_assert(sizeof(MeshHeader)==24,"CYBRMSH1 header ABI");
static_assert(sizeof(MeshRecord)==76,"CYBRMSH1 triangle ABI");
void appendBinaryMesh(BVH &bvh,const std::string &path,const std::string &endPath,uint32_t objectBase){
    std::ifstream f(path,std::ios::binary),end;
    if(!f)throw std::runtime_error("Cannot open binary mesh "+path);
    MeshHeader h{};f.read(reinterpret_cast<char*>(&h),sizeof(h));
    if(!f||std::memcmp(h.magic,"CYBRMSH1",8)||h.version!=1||h.flags!=0||h.triangleCount>30000000)
        throw std::runtime_error("Invalid CYBRMSH1 header "+path);
    bool motion=!endPath.empty();
    if(motion){end.open(endPath,std::ios::binary);MeshHeader eh{};end.read(reinterpret_cast<char*>(&eh),sizeof(eh));
        if(!end||std::memcmp(eh.magic,"CYBRMSH1",8)||eh.version!=1||eh.flags!=0||eh.triangleCount!=h.triangleCount)
            throw std::runtime_error("Motion binary mesh header/count differs");}
    bvh.triangles.reserve(bvh.triangles.size()+size_t(h.triangleCount));
    for(uint64_t i=0;i<h.triangleCount;i++){
        MeshRecord r{},er{};f.read(reinterpret_cast<char*>(&r),sizeof(r));if(motion)end.read(reinterpret_cast<char*>(&er),sizeof(er));
        if(!f||(motion&&!end))throw std::runtime_error("Truncated binary mesh");
        V3 p[3],n[3],ep[3];
        for(int k=0;k<3;k++){
            const auto &v=r.vertices[k];p[k]={v.px,v.py,v.pz};n[k]=unit({v.nx,v.ny,v.nz});
            if(!finite(p[k])||!finite(n[k]))throw std::runtime_error("Nonfinite binary mesh record");
            if(motion){const auto &e=er.vertices[k];ep[k]={e.px,e.py,e.pz};if(!finite(ep[k]))throw std::runtime_error("Nonfinite motion mesh");}
        }
        if(r.material>7||(motion&&r.material!=er.material))throw std::runtime_error("Invalid binary mesh material");
        if(!bvh.addTriangle(p[0],p[1],p[2],n[0],n[1],n[2],r.material,objectBase+r.material))continue;
        if(motion){auto &t=bvh.triangles.back();t.da=ep[0]-p[0];t.db=ep[1]-p[1];t.dc=ep[2]-p[2];t.motion=true;}
    }
    char trailing;if(f.read(&trailing,1))throw std::runtime_error("Unexpected trailing binary mesh bytes");
}
void appendMesh(BVH &bvh,const std::string &path,uint32_t defaultMat,const std::string &endPath=""){
    std::ifstream f(path,std::ios::binary);char magic[8]{};f.read(magic,8);f.close();
    // A changing water triangle count must never change an unrelated rock's
    // material seed or photon receiving group during an animation.
    uint32_t objectBase=defaultMat==WATER?0x10000000u:0x20000000u;
    if(std::memcmp(magic,"CYBRMSH1",8)==0)appendBinaryMesh(bvh,path,endPath,objectBase);else appendObj(bvh,path,defaultMat,endPath,objectBase);
}
void appendSpheres(BVH &bvh,const std::string &path,float shutterSeconds){
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot open spheres CSV "+path);
    std::string line;uint32_t object=0x40000000u;
    while(std::getline(f,line)){
        if(line.empty()||line[0]=='#'||line.find("x,y")!=std::string::npos)continue;
        std::replace(line.begin(),line.end(),',',' ');std::istringstream ss(line);
        V3 p,v;float r;int type;ss>>p.x>>p.y>>p.z>>r>>v.x>>v.y>>v.z>>type;
        if(!ss||!finite(p)||!finite(v)||r<=0||r>.2f||type<0||type>2)throw std::runtime_error("Invalid sphere CSV record");
        bvh.spheres.push_back({p,v*shutterSeconds,r,type==0?WATER:(type==1?FOAM:BUBBLE),object++});
    }
}

struct Options {
    std::string water,waterEnd,solids,solidsEnd,spheres,out="output/beauty";
    std::string environment="neutral";
    int width=640,height=360,spp=16,maxDepth=14,threads=4,seed=17;
    V3 eye{2.4f,1.25f,2.7f},target{-.2f,.5f,0},up{0,1,0};
    float fov=38,focus=-1,aperture=.003f,exposure=0,shutterSeconds=0;
    float shutterOpen=0,shutterClose=1,wetLevel=.45f,wetReach=.22f;
    V3 absorption{.10f,.03f,.015f};
    V3 sunDir=unit(V3(-.5f,.72f,-.48f));
    float skyPower=1.2f,sunPower=42000,sunAngle=.28f;
    bool deterministicPreview=false;
    bool cameraWater=false,selfTest=false,aovs=false,uniformEnvironment=false,auditInterfaces=false,geometricWaterNormals=false,aovSpecularPaths=false;
    V3 uniformRadiance{1};
    uint64_t photons=0;float causticRadius=.018f;
    bool addArea=false;V3 areaCenter{-1.2f,3.f,-1.f},areaU{1.3f,0,0},areaV{0,0,.7f},areaRadiance{3.5f,3.7f,4.f};
};
V3 parseV3(std::string s){std::replace(s.begin(),s.end(),',',' ');std::istringstream ss(s);V3 p;ss>>p.x>>p.y>>p.z;if(!ss||!finite(p))throw std::runtime_error("Expected x,y,z vector, got "+s);return p;}
Options parseArgs(int argc,char **argv){
    Options o;
    for(int i=1;i<argc;i++){
        std::string a=argv[i];auto value=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+a);return std::string(argv[++i]);};
        if(a=="--water")o.water=value();else if(a=="--water-end")o.waterEnd=value();
        else if(a=="--solids")o.solids=value();else if(a=="--solids-end")o.solidsEnd=value();
        else if(a=="--spheres"||a=="--particles")o.spheres=value();
        else if(a=="--out")o.out=value();else if(a=="--width")o.width=std::stoi(value());
        else if(a=="--height")o.height=std::stoi(value());else if(a=="--spp")o.spp=std::stoi(value());
        else if(a=="--bounces")o.maxDepth=std::stoi(value());else if(a=="--threads")o.threads=std::stoi(value());
        else if(a=="--seed")o.seed=std::stoi(value());else if(a=="--camera"||a=="--eye")o.eye=parseV3(value());
        else if(a=="--target")o.target=parseV3(value());else if(a=="--up")o.up=parseV3(value());
        else if(a=="--fov")o.fov=std::stof(value());else if(a=="--focus")o.focus=std::stof(value());
        else if(a=="--aperture")o.aperture=std::stof(value());else if(a=="--exposure")o.exposure=std::stof(value());
        else if(a=="--shutter-seconds")o.shutterSeconds=std::stof(value());
        else if(a=="--shutter-open")o.shutterOpen=std::stof(value());else if(a=="--shutter-close")o.shutterClose=std::stof(value());
        else if(a=="--wet-level")o.wetLevel=std::stof(value());else if(a=="--wet-reach")o.wetReach=std::stof(value());
        else if(a=="--absorption")o.absorption=parseV3(value());else if(a=="--sun")o.sunDir=unit(parseV3(value()));
        else if(a=="--sun-power")o.sunPower=std::stof(value());else if(a=="--sun-angle")o.sunAngle=std::stof(value());
        else if(a=="--sky-power")o.skyPower=std::stof(value());else if(a=="--camera-water")o.cameraWater=true;
        else if(a=="--aovs")o.aovs=true;else if(a=="--uniform-environment"){o.uniformEnvironment=true;o.uniformRadiance=parseV3(value());}
        else if(a=="--aov-specular-paths"){o.aovs=true;o.aovSpecularPaths=true;}
        else if(a=="--deterministic-preview")o.deterministicPreview=true;
        else if(a=="--environment")o.environment=value();
        else if(a=="--audit-interfaces")o.auditInterfaces=true;else if(a=="--geometric-water-normals")o.geometricWaterNormals=true;
        else if(a=="--caustic-photons")o.photons=std::stoull(value());else if(a=="--caustic-radius")o.causticRadius=std::stof(value());
        else if(a=="--area")o.addArea=true;else if(a=="--area-center")o.areaCenter=parseV3(value());
        else if(a=="--area-u")o.areaU=parseV3(value());else if(a=="--area-v")o.areaV=parseV3(value());
        else if(a=="--area-radiance")o.areaRadiance=parseV3(value());else if(a=="--self-test")o.selfTest=true;
        else if(a=="--help"){
            std::cout<<"CYBR WATER VFX path tracer\n--water mesh.obj --solids scene.obj [--spheres particles.csv] --out stem\n"
                <<"--width 640 --height 360 --spp 16 --bounces 14 --threads 4\n"
                <<"--camera x,y,z --target x,y,z --fov 38 --aperture .003 --focus metres\n"
                <<"--water-end mesh.obj --shutter-seconds .020833 --shutter-open 0 --shutter-close 1\n"
                <<"--absorption .18,.055,.025 --sun x,y,z --sun-power 32000 --sky-power 1\n"
                <<"--environment neutral|forest --aovs --aov-specular-paths\n"
                <<"--caustic-photons 1000000 --caustic-radius .018 --self-test\n";std::exit(0);
        }else throw std::runtime_error("Unknown argument "+a);
    }
    if(o.width<8||o.height<8||o.width>16384||o.height>16384||o.spp<1||o.spp>65536||o.maxDepth<2||o.maxDepth>128||o.threads<1||o.aperture<0||o.fov<3||o.fov>150||o.skyPower<0||o.sunPower<0||o.causticRadius<=0||o.shutterOpen<0||o.shutterClose>1||o.shutterOpen>o.shutterClose)
        throw std::runtime_error("Render option outside valid range");
    if(o.focus<0)o.focus=length(o.target-o.eye);
    if(o.environment!="neutral"&&o.environment!="forest"&&o.environment!="studio")throw std::runtime_error("Environment must be neutral or forest");
    if(o.deterministicPreview){
        int side=int(std::sqrt(float(o.spp)));
        if(side*side!=o.spp)throw std::runtime_error("Deterministic AA requires square spp: 1,4,9,16,...");
        if(o.aperture!=0)throw std::runtime_error("Deterministic preview currently requires --aperture 0");
        if(!o.waterEnd.empty()||!o.solidsEnd.empty())throw std::runtime_error("Deterministic preview currently requires static per-frame geometry");
    }
    return o;
}

// Exact unpolarized dielectric Fresnel. etaI/etaT are absolute indices.
float fresnel(float cosI,float etaI,float etaT){
    cosI=clamp(std::abs(cosI));float ratio=etaI/etaT;
    float sinT2=ratio*ratio*std::max(0.f,1-cosI*cosI);if(sinT2>=1)return 1;
    float cosT=std::sqrt(1-sinT2);
    float rs=(etaI*cosI-etaT*cosT)/(etaI*cosI+etaT*cosT);
    float rp=(etaT*cosI-etaI*cosT)/(etaT*cosI+etaI*cosT);
    return .5f*(rs*rs+rp*rp);
}
bool refract(V3 incident,V3 n,float etaI,float etaT,V3 &out){
    float c=clamp(-dot(incident,n)),eta=etaI/etaT,k=1-eta*eta*(1-c*c);
    if(k<0)return false;out=unit(incident*eta+n*(eta*c-std::sqrt(k)));return true;
}
struct Medium {
    int water=0,air=0;
    bool wet()const{return water>0&&air==0;}
    float index()const{return wet()?1.333f:1.f;}
    Medium after(const Hit &h)const{
        Medium m=*this;
        if(h.mat==WATER)m.water=std::max(0,m.water+(h.front?1:-1));
        else if(h.mat==BUBBLE)m.air=std::max(0,m.air+(h.front?1:-1));
        return m;
    }
};
struct DielectricSample {V3 direction;Medium medium;float factor=1,etaScale=1;bool transmitted=false;};
DielectricSample sampleDielectric(const Ray &ray,const Hit &h,Medium current,RNG &rng,bool radiance){
    Medium next=current.after(h);float etaI=current.index(),etaT=next.index();
    V3 ng=h.front?h.ng:-h.ng,ns=h.front?h.ns:-h.ns;
    if(dot(-ray.d,ns)<=1e-4f)ns=ng;
    V3 wt;bool canRefract=refract(ray.d,ns,etaI,etaT,wt);
    // Shading normals may not send a transmitted ray onto the incident
    // geometric side. Fall back to the actual interface normal in that case.
    if(canRefract&&dot(wt,ng)>=-1e-6f){ns=ng;canRefract=refract(ray.d,ns,etaI,etaT,wt);}
    float F=canRefract?fresnel(-dot(ray.d,ns),etaI,etaT):1.f;
    DielectricSample s;s.medium=current;
    if(rng.uniform()<F){
        s.direction=unit(reflect(ray.d,ns));
        if(dot(s.direction,ng)<=1e-6f)s.direction=unit(reflect(ray.d,ng));
    }else{
        s.direction=wt;s.medium=next;s.transmitted=true;
        if(radiance)s.factor=sq(etaI/etaT);
        s.etaScale=sq(etaT/etaI);
    }
    return s;
}
D3 offset(const Hit &h,V3 d){
    D3 n(h.ng);double displacement=dot(absolute(n),h.pError);
    if(dot(d,h.ng)<0)n=n*(-1);
    D3 q=h.exactP+n*displacement;
    auto outward=[](double x,double direction){return direction==0?x:std::nextafter(x,direction>0?std::numeric_limits<double>::infinity():-std::numeric_limits<double>::infinity());};
    // A strictly outward rounding step also handles an exactly represented
    // plane through zero without introducing a fixed world-space length.
    q.x=outward(q.x,n.x);q.y=outward(q.y,n.y);q.z=outward(q.z,n.z);return q;
}

struct Sky {
    static constexpr int W=256,H=128;
    std::vector<V3> texels;
    std::vector<double> cdf;
    double integral=0;
    V3 sunDir,sunRadiance;
    float cosSun=1,sunOmega=1;
    V3 forestRadiance(V3 d,V3 openSky)const{
        float phi=std::atan2(d.z,d.x),elevation=std::asin(clamp(d.y,-1,1));
        if(elevation<0)return mix(V3(.021f,.027f,.012f),V3(.006f,.012f,.004f),clamp(-d.y));
        float canopy=0,trunk=0;
        for(int tree=0;tree<23;tree++){
            float az=2*PI*(tree+.25f)/23+.055f*(lattice(tree,9,3)-.5f);
            float delta=std::atan2(std::sin(phi-az),std::cos(phi-az));
            float width=.090f+.095f*lattice(tree,27,8),height=.27f+.27f*lattice(tree,4,21);
            float crown=.22f+.29f*lattice(tree,31,18);
            for(int lobe=0;lobe<3;lobe++){
                float horizontal=(lobe-1)*width*.47f;
                float vertical=crown+(.06f*lattice(tree,lobe,84)-.03f);
                float metric=sq((delta-horizontal)/width)+sq((elevation-vertical)/height);
                float edge=1-smooth(.76f,1.04f,metric);
                canopy=std::max(canopy,edge);
            }
            float trunkWidth=.005f+.005f*lattice(tree,71,2);
            float trunkAxis=delta-.03f*std::sin(elevation*4+tree);
            trunk=std::max(trunk,(1-smooth(trunkWidth,trunkWidth*1.4f,std::abs(trunkAxis)))*(1-smooth(crown-.03f,crown+.08f,elevation)));
        }
        float leafGaps=smooth(.22f,.38f,noise(d*97.f+V3(7.1f,13.3f,29.7f)));
        canopy*=mix(.76f,1.f,leafGaps);
        // The sun has an explicit open window, so its sampled disk is not
        // spuriously added behind an opaque procedural canopy silhouette.
        float sunWindow=1-smooth(.13f,.34f,length(d-sunDir));
        canopy*=1-sunWindow;trunk*=1-sunWindow;
        float undergrowth=(1-smooth(.01f,.17f,elevation))*.98f;
        float obstruction=std::max(undergrowth,std::max(canopy,trunk));
        float leaves=fbm(d*37.f+V3(1.7f,11.f,23.f),3);
        V3 dark=mix(V3(.008f,.017f,.005f),V3(.030f,.048f,.013f),leaves);
        dark=mix(dark,V3(.012f,.009f,.005f),trunk*.65f);
        return mix(openSky,dark,clamp(obstruction));
    }
    explicit Sky(const Options &o):sunDir(o.sunDir),sunRadiance(V3(1.f,.88f,.69f)*o.sunPower){
        cosSun=std::cos(o.sunAngle*PI/180);sunOmega=2*PI*(1-cosSun);
        texels.resize(W*H);cdf.resize(W*H);
        for(int y=0;y<H;y++){
            float theta=PI*(y+.5f)/H,cy=std::cos(theta),sy=std::sin(theta);
            double omega=(2*PI/W)*(std::cos(PI*y/H)-std::cos(PI*(y+1)/H));
            for(int x=0;x<W;x++){
                float phi=2*PI*(x+.5f)/W;V3 d{sy*std::cos(phi),cy,sy*std::sin(phi)};
                V3 c;
                if(d.y>0){
                    float zen=std::sqrt(d.y),nearSun=std::pow(clamp(dot(d,sunDir)),7.f);
                    V3 clear=mix(V3(.91f,.86f,.76f),V3(.34f,.47f,.64f),zen);
                    // Angular cloud structure is a procedural infinite emitter,
                    // sampled with its actual solid-angle luminance PDF.
                    float clouds=fbm(d*4.6f+V3(19.7f,3.1f,27.2f),3);
                    float cover=smooth(.40f,.69f,clouds)*smooth(.01f,.28f,d.y);
                    c=mix(clear,V3(1.26f,1.22f,1.13f),cover*.8f);
                    c+=V3(1.1f,.84f,.55f)*nearSun;
                    // A low dark canopy on one bank gives water a readable
                    // reflection silhouette without painting its surface.
                    float bank=smooth(.25f,.7f,-d.z)*smooth(.28f,.04f,d.y);
                    c=mix(c,V3(.027f,.045f,.029f),bank*.93f);
                }else{
                    float earth=clamp(-d.y);c=mix(V3(.085f,.09f,.08f),V3(.023f,.031f,.021f),earth);
                }
                if(o.environment=="forest")c=forestRadiance(d,c);
                if(o.environment=="studio") {
                    // Procedural infinite studio illumination; no imported image.
                    c=V3(.009f,.016f,.025f)+V3(.055f,.065f,.080f)*std::max(0.f,d.y);
                    V3 key=unit(V3(-.60f,.56f,.57f)),rim=unit(V3(.65f,.48f,-.48f));
                    c+=V3(3.7f,3.6f,3.4f)*smooth(.89f,.955f,dot(d,key));
                    c+=V3(2.1f,2.8f,3.7f)*smooth(.94f,.98f,dot(d,rim));
                    c+=V3(.30f,.35f,.45f)*std::pow(std::max(0.f,d.y),3.f);
                }
                c*=o.skyPower;if(o.uniformEnvironment)c=o.uniformRadiance;texels[y*W+x]=c;
                integral+=std::max(1e-12,double(lum(c)))*omega;cdf[y*W+x]=integral;
            }
        }
        for(double &v:cdf)v/=integral;
    }
    int cell(V3 d)const{
        float phi=std::atan2(d.z,d.x);if(phi<0)phi+=2*PI;
        int x=std::min(W-1,int(phi*(W/(2*PI))));int y=std::min(H-1,int(std::acos(clamp(d.y,-1,1))*(H/PI)));
        return y*W+x;
    }
    V3 base(V3 d)const{
        // Bilinear lookup of the precomputed procedural radiance, never an
        // imported picture. Sampling still uses an exact piecewise-constant PDF.
        float phi=std::atan2(d.z,d.x);if(phi<0)phi+=2*PI;
        float fx=phi*(W/(2*PI))-.5f,fy=std::acos(clamp(d.y,-1,1))*(H/PI)-.5f;
        int x=int(std::floor(fx)),y=int(std::floor(fy));float a=fx-x,b=fy-y;
        auto at=[&](int xx,int yy){xx=(xx%W+W)%W;yy=std::max(0,std::min(H-1,yy));return texels[yy*W+xx];};
        return mix(mix(at(x,y),at(x+1,y),a),mix(at(x,y+1),at(x+1,y+1),a),b);
    }
    V3 sun(V3 d)const{return dot(d,sunDir)>=cosSun?sunRadiance:V3(0);}
    V3 evaluate(V3 d,bool includeSun=true)const{return base(d)+(includeSun?sun(d):V3(0));}
    float pdf(V3 d)const{
        int id=cell(d),y=id/W;double mass=cdf[id]-(id?cdf[id-1]:0);
        double omega=(2*PI/W)*(std::cos(PI*y/H)-std::cos(PI*(y+1)/H));return float(mass/omega);
    }
    V3 sample(RNG &rng)const{
        double u=rng.uniform();int id=int(std::lower_bound(cdf.begin(),cdf.end(),u)-cdf.begin());id=std::min(id,W*H-1);
        int x=id%W,y=id/W;float phi=(x+rng.uniform())*(2*PI/W);
        float cy=mix(std::cos(PI*y/H),std::cos(PI*(y+1)/H),rng.uniform()),sy=std::sqrt(std::max(0.f,1-cy*cy));
        return {sy*std::cos(phi),cy,sy*std::sin(phi)};
    }
};
struct AreaLight {
    V3 center,u,v,radiance,n;float area=0;uint32_t object=0;
    AreaLight(const Options &o,uint32_t id):center(o.areaCenter),u(o.areaU),v(o.areaV),radiance(o.areaRadiance),n(unit(cross(u,v))),area(4*length(cross(u,v))),object(id){}
};
struct LightSample {V3 d,Le;float pdf=0,distance=INF;};
struct Lighting {
    Sky sky;std::vector<AreaLight> areas;float pSky=.45f,pSun=.55f,pArea=0;
    Lighting(const Options &o,BVH &bvh):sky(o){
        if(o.addArea){
            uint32_t object=0x70000000u;areas.emplace_back(o,object);const auto &a=areas.back();
            V3 p=a.center-a.u-a.v,q=a.center+a.u-a.v,r=a.center+a.u+a.v,s=a.center-a.u+a.v;
            bvh.addTriangle(p,q,r,a.n,a.n,a.n,LIGHT,object);bvh.addTriangle(p,r,s,a.n,a.n,a.n,LIGHT,object);
        }
        float energySky=float(sky.integral),energySun=lum(sky.sunRadiance)*sky.sunOmega;
        pSun=energySun>0?clamp(energySun/(energySun+energySky),.15f,.7f):0;
        pArea=areas.empty()?0:.15f;pSky=(1-pSun)*(1-pArea);pSun*=1-pArea;
    }
    float infinitePdf(V3 d)const{return pSky*sky.pdf(d)+(dot(d,sky.sunDir)>=sky.cosSun?pSun/sky.sunOmega:0);}
    float areaPdf(V3 p,const Hit &h)const{
        for(const auto &a:areas)if(h.object==a.object){V3 delta=h.p-p;float d2=length2(delta),c=dot(a.n,-unit(delta));return c>0?pArea/areas.size()*d2/(c*a.area):0;}return 0;
    }
    V3 emission(const Hit &h,V3 wo)const{
        for(const auto &a:areas)if(h.object==a.object)return dot(a.n,wo)>0?a.radiance:V3(0);return V3(0);
    }
    LightSample sample(V3 p,RNG &rng)const{
        LightSample s;float choice=rng.uniform();
        if(choice<pSky){s.d=sky.sample(rng);s.Le=sky.evaluate(s.d);s.pdf=infinitePdf(s.d);}
        else if(choice<pSky+pSun){s.d=uniformCone(rng.uniform(),rng.uniform(),sky.cosSun,sky.sunDir);s.Le=sky.evaluate(s.d);s.pdf=infinitePdf(s.d);}
        else{
            int index=std::min(int(areas.size())-1,int(rng.uniform()*areas.size()));const auto &a=areas[index];
            V3 q=a.center+a.u*(2*rng.uniform()-1)+a.v*(2*rng.uniform()-1),delta=q-p;
            s.distance=length(delta);s.d=delta/s.distance;float cosine=dot(a.n,-s.d);
            if(cosine>0){s.Le=a.radiance;s.pdf=pArea/areas.size()*sq(s.distance)/(cosine*a.area);}
        }
        return s;
    }
};
inline float powerHeuristic(float a,float b){float aa=a*a,bb=b*b;return aa/(aa+bb+1e-30f);}

struct Surface {
    V3 n,albedo;float roughness=.5f,f0=.04f,specProbability=.25f;
};
float rockHeight(V3 p){
    float fine=noise(p*195.f),grain=noise(p*73.f),layers=noise(V3(p.x*21.f,p.y*56.f,p.z*23.f));
    return fine*.00024f+grain*.00065f+layers*.00048f+noise(p*22.f)*.0011f;
}
Surface surfaceAt(const Hit &h,Medium medium,const Options &o){
    Surface s;s.n=h.front?h.ns:-h.ns;V3 ng=h.front?h.ng:-h.ng,p=h.p;
    if(dot(s.n,ng)<.3f)s.n=ng;
    if(h.mat==FOAM){s.albedo={.83f,.85f,.83f};s.roughness=.75f;s.f0=.025f;s.specProbability=.12f;return s;}
    if(h.mat==DARK){s.albedo={.019f,.028f,.015f};s.roughness=.8f;s.f0=.025f;s.specProbability=.1f;return s;}
    float broad=fbm(p*3.2f+V3(float(hash(h.object)&255)*.1f),4);
    float mineral=noise(p*47.f),fleck=noise(p*260.f),bedding=fbm(V3(p.x*3.1f,p.y*27.f+p.x*2.1f,p.z*4.6f),3);
    float palette=(hash(h.object*719u)&0xffffff)*(1.f/16777215.f);
    // Feldspar-rich river stone with a minority of cooler mineral inclusions.
    // Values are linear diffuse reflectances, not display-space paint colors.
    V3 warm=mix(V3(.245f,.178f,.111f),V3(.30f,.252f,.187f),palette);
    V3 cool=mix(V3(.147f,.150f,.140f),V3(.22f,.221f,.208f),palette);
    V3 base=mix(cool,warm,smooth(.15f,.61f,broad));
    base*=.63f+.70f*broad+.22f*mineral;
    float vein=std::pow(clamp(1-std::abs(bedding-.49f)*47),3.f);
    float quartz=smooth(.76f,.92f,noise(p*153.f));
    base=mix(base,V3(.43f,.418f,.38f),clamp(vein*.47f+quartz*.21f));
    float darkInclusion=smooth(.78f,.91f,noise(p*81.f+V3(23.1f,7.3f,41.5f)));
    base=mix(base,V3(.06f,.054f,.039f),darkInclusion*.55f);
    base*=.84f+.30f*fleck;
    if(h.mat==SAND){base=mix(V3(.20f,.145f,.084f),V3(.35f,.284f,.178f),broad)*(.72f+.48f*fleck);}
    if(h.mat==MOSS){base=mix(V3(.028f,.065f,.013f),V3(.072f,.12f,.025f),broad)*(.74f+.43f*mineral);}
    float irregular=.04f*(fbm(p*8.f,3)-.45f);
    float wet=medium.wet()?1.f:1-smooth(o.wetLevel+irregular,o.wetLevel+o.wetReach+irregular,p.y);
    if(h.mat==MOSS)wet*=.45f;
    s.albedo=base*mix(1.f,.58f,wet);
    s.roughness=mix(.63f,.10f+mineral*.12f,wet);
    if(h.mat==SAND)s.roughness=mix(.88f,.38f,wet);
    if(h.mat==MOSS)s.roughness=.80f;
    // The wet layer has the water/air interface reflectance. Under water,
    // the solid/water interface is much weaker than solid/air.
    s.f0=medium.wet()?sq((1.52f-1.333f)/(1.52f+1.333f)):mix(.042f,.02037f,wet);
    s.specProbability=clamp(.12f+2.7f*s.f0+.08f*wet,.1f,.45f);
    if(h.mat==ROCK||h.mat==SAND){
        const float e=.0008f;
        V3 gradient{(rockHeight(p+V3(e,0,0))-rockHeight(p-V3(e,0,0)))/(2*e),
                    (rockHeight(p+V3(0,e,0))-rockHeight(p-V3(0,e,0)))/(2*e),
                    (rockHeight(p+V3(0,0,e))-rockHeight(p-V3(0,0,e)))/(2*e)};
        // True material micro-normal, attenuated under a wet clearcoat; this
        // affects only solid reflection, never the water normal or silhouette.
        gradient=gradient-s.n*dot(gradient,s.n);s.n=unit(s.n-gradient*mix(.75f,.30f,wet));
        if(dot(s.n,ng)<.15f)s.n=ng;
    }
    return s;
}
inline float ggxD(float nh,float alpha){float a2=alpha*alpha,d=nh*nh*(a2-1)+1;return a2/(PI*d*d);}
inline float ggxG1(float nv,float alpha){if(nv<=0)return 0;return 2*nv/(nv+std::sqrt(alpha*alpha+(1-alpha*alpha)*nv*nv));}
inline float schlick(float c,float f0){float m=1-clamp(c),m2=m*m;return f0+(1-f0)*m2*m2*m;}
struct BsdfEval {V3 f;float pdf=0;};
BsdfEval evaluateBsdf(const Surface &s,V3 wo,V3 wi){
    BsdfEval e;float nv=dot(s.n,wo),nl=dot(s.n,wi);if(nv<=0||nl<=0)return e;
    V3 h=unit(wo+wi);float nh=clamp(dot(s.n,h)),vh=clamp(dot(wo,h));if(nh<=0||vh<=0)return e;
    float alpha=std::max(.006f,s.roughness*s.roughness),D=ggxD(nh,alpha),G=ggxG1(nv,alpha)*ggxG1(nl,alpha),F=schlick(vh,s.f0);
    float fo=schlick(nv,s.f0),fi=schlick(nl,s.f0);
    e.f=s.albedo*((1-fo)*(1-fi)/PI)+V3(F*D*G/(4*nv*nl));
    float specPdf=D*ggxG1(nv,alpha)/(4*nv);
    e.pdf=s.specProbability*specPdf+(1-s.specProbability)*nl/PI;return e;
}
V3 sampleVisibleGGX(V3 wo,float alpha,RNG &rng){
    // Visible-normal sampling: stretch view, sample its projected disk,
    // unstretch the resulting micro-normal. This avoids grazing fireflies
    // produced by sampling the full, mostly invisible GGX distribution.
    V3 v=unit(V3(alpha*wo.x,alpha*wo.y,wo.z));
    float len2=v.x*v.x+v.y*v.y;V3 t1=len2>0?V3(-v.y,v.x,0)/std::sqrt(len2):V3(1,0,0),t2=cross(v,t1);
    float r=std::sqrt(rng.uniform()),phi=2*PI*rng.uniform(),a=r*std::cos(phi),b=r*std::sin(phi),s=.5f*(1+v.z);
    b=(1-s)*std::sqrt(std::max(0.f,1-a*a))+s*b;
    V3 nh=t1*a+t2*b+v*std::sqrt(std::max(0.f,1-a*a-b*b));
    return unit(V3(alpha*nh.x,alpha*nh.y,std::max(0.f,nh.z)));
}
struct BsdfSample {V3 direction,weight;float pdf=0;};
BsdfSample sampleBsdf(const Surface &s,V3 wo,RNG &rng){
    BsdfSample r;Frame frame(s.n);
    if(rng.uniform()<s.specProbability){
        V3 view=frame.local(wo);if(view.z<=0)return r;
        V3 h=frame.world(sampleVisibleGGX(view,std::max(.006f,s.roughness*s.roughness),rng));r.direction=unit(reflect(-wo,h));
    }else r.direction=frame.world(cosineHemisphere(rng.uniform(),rng.uniform()));
    BsdfEval e=evaluateBsdf(s,wo,r.direction);r.pdf=e.pdf;if(e.pdf>0)r.weight=e.f*(std::max(0.f,dot(s.n,r.direction))/e.pdf);return r;
}

struct Photon {V3 p,power,normal,incoming;uint32_t object=0;uint8_t bin=0;};
struct PhotonRange {uint32_t begin=0,end=0;};
struct PhotonMap {
    std::vector<Photon> photons;
    std::unordered_map<uint64_t,PhotonRange> cells;
    float radius=.018f;
    int bins=1;
    uint64_t emitted=0;
    uint64_t key(int x,int y,int z,int bin)const{
        // Domain coordinates require far fewer than 19 bits per axis. The
        // 57-bit signed-wrapped spatial key leaves room for temporal bins.
        constexpr uint64_t mask=(1u<<19)-1;
        return (uint64_t(uint32_t(x))&mask)|((uint64_t(uint32_t(y))&mask)<<19)|((uint64_t(uint32_t(z))&mask)<<38)|(uint64_t(bin)<<57);
    }
    uint64_t key(V3 p,int bin)const{return key(int(std::floor(p.x/radius)),int(std::floor(p.y/radius)),int(std::floor(p.z/radius)),bin);}
    int timeBin(float t,const Options &o)const{
        if(bins==1||o.shutterClose==o.shutterOpen)return 0;
        return std::min(bins-1,std::max(0,int((t-o.shutterOpen)/(o.shutterClose-o.shutterOpen)*bins)));
    }
    void build(const BVH &bvh,const Lighting &lights,const Options &o){
        radius=o.causticRadius;emitted=o.photons;if(!emitted||maxcomp(lights.sky.sunRadiance)<=0)return;
        bool moving=!o.waterEnd.empty()||!o.solidsEnd.empty()||(!o.spheres.empty()&&o.shutterSeconds>0);
        bins=moving?4:1;
        Bounds bounds=bvh.waterBounds;
        if(bounds.area()==0||!finite(bounds.lo)||bounds.lo.x>bounds.hi.x)throw std::runtime_error("Caustic photons require a water mesh");
        Frame plane(lights.sky.sunDir);float loU=INF,hiU=-INF,loV=INF,hiV=-INF,planeZ=-INF;
        for(int c=0;c<8;c++){
            V3 p{c&1?bounds.hi.x:bounds.lo.x,c&2?bounds.hi.y:bounds.lo.y,c&4?bounds.hi.z:bounds.lo.z};
            V3 q=plane.local(p);loU=std::min(loU,q.x);hiU=std::max(hiU,q.x);loV=std::min(loV,q.y);hiV=std::max(hiV,q.y);planeZ=std::max(planeZ,q.z);
        }
        // Start above every solid/emitter, not just above the water bounds;
        // an upstream rock must be able to shadow a water-bound photon.
        if(!bvh.nodes.empty())for(int c=0;c<8;c++){
            const Bounds &world=bvh.nodes[0].bounds;
            V3 p{c&1?world.hi.x:world.lo.x,c&2?world.hi.y:world.lo.y,c&4?world.hi.z:world.lo.z};
            planeZ=std::max(planeZ,dot(p,lights.sky.sunDir));
        }
        float padding=.08f;loU-=padding;hiU+=padding;loV-=padding;hiV+=padding;planeZ+=.5f;
        float area=(hiU-loU)*(hiV-loV);
        std::vector<std::vector<Photon>> local(size_t(o.threads));
        std::atomic<uint64_t> completed{0};
        #pragma omp parallel num_threads(o.threads)
        {
            int tid=omp_get_thread_num();local[size_t(tid)].reserve(size_t(emitted/o.threads/3));
            #pragma omp for schedule(static)
            for(uint64_t i=0;i<emitted;i++){
                RNG rng((uint64_t(o.seed)<<32)^i^0xca05c71c692b4e51ULL);int bin=int(i%uint64_t(bins));
                float t=mix(o.shutterOpen,o.shutterClose,(bin+rng.uniform())/bins);
                V3 incoming=-uniformCone(rng.uniform(),rng.uniform(),lights.sky.cosSun,lights.sky.sunDir);
                V3 origin=plane.world({mix(loU,hiU,rng.uniform()),mix(loV,hiV,rng.uniform()),planeZ});
                // Each temporal bin is an estimate of the full radiance for
                // that interval, so its photon power uses that bin's count.
                uint64_t binCount=(emitted+uint64_t(bins)-1-uint64_t(bin))/uint64_t(bins);
                V3 power=lights.sky.sunRadiance*(lights.sky.sunOmega*area*std::max(0.f,dot(-incoming,lights.sky.sunDir))/float(binCount));
                Ray ray(origin,incoming,t);Medium medium;bool dielectricEvent=false;
                for(int depth=0;depth<o.maxDepth;depth++){
                    Hit hit;if(!bvh.intersect(ray,hit))break;
                    if(medium.wet())power*=expneg(o.absorption*hit.t);
                    if(hit.mat==WATER||hit.mat==BUBBLE){
                        if(o.geometricWaterNormals)hit.ns=hit.ng;
                        DielectricSample s=sampleDielectric(ray,hit,medium,rng,false);
                        if(medium.index()!=medium.after(hit).index())dielectricEvent=true;
                        medium=s.medium;ray=Ray(offset(hit,s.direction),s.direction,t);
                    }else{
                        if(dielectricEvent&&hit.mat!=LIGHT&&lum(power)>0){
                            V3 n=hit.front?hit.ng:-hit.ng;
                            local[size_t(tid)].push_back({hit.p,power,n,-ray.d,hit.object,uint8_t(bin)});
                        }
                        break;
                    }
                }
                completed.fetch_add(1,std::memory_order_relaxed);
            }
        }
        size_t total=0;for(const auto &p:local)total+=p.size();photons.reserve(total);
        for(auto &p:local){photons.insert(photons.end(),p.begin(),p.end());p.clear();p.shrink_to_fit();}
        std::sort(photons.begin(),photons.end(),[&](const Photon &a,const Photon &b){return key(a.p,a.bin)<key(b.p,b.bin);});
        cells.reserve(photons.size()/5+1);
        for(uint32_t begin=0;begin<photons.size();){uint64_t k=key(photons[begin].p,photons[begin].bin);uint32_t end=begin+1;
            while(end<photons.size()&&key(photons[end].p,photons[end].bin)==k)++end;
            cells.emplace(k,PhotonRange{begin,end});begin=end;}
        std::cerr<<"Caustics: "<<emitted<<" emitted / "<<photons.size()<<" stored, "<<bins<<" time bins, radius "<<radius<<" m\n";
    }
    V3 gather(const Hit &h,const Surface &s,V3 wo,float time,const Options &o)const{
        if(photons.empty())return V3(0);
        int x=int(std::floor(h.p.x/radius)),y=int(std::floor(h.p.y/radius)),z=int(std::floor(h.p.z/radius)),bin=timeBin(time,o);
        V3 n=h.front?h.ng:-h.ng,result;float r2=radius*radius;
        for(int dz=-1;dz<=1;dz++)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
            auto it=cells.find(key(x+dx,y+dy,z+dz,bin));if(it==cells.end())continue;
            for(uint32_t i=it->second.begin;i<it->second.end;i++){
                const Photon &p=photons[i];V3 delta=p.p-h.p;float d2=length2(delta);
                if(d2>=r2||dot(p.normal,n)<.87f||std::abs(dot(delta,n))>radius*.12f||p.object!=h.object)continue;
                // A normalized Epanechnikov disk kernel integrates to one.
                // It estimates incident flux per area; the BRDF is applied
                // afterwards, without an erroneous extra cosine factor.
                float kernel=2*(1-d2/r2)/(PI*r2);
                result+=p.power*evaluateBsdf(s,wo,p.incoming).f*kernel;
            }
        }
        return result;
    }
};

struct PathStats {
    uint64_t cameraPaths=0,segments=0,dielectricEvents=0,reflections=0,transmissions=0,internalReflections=0,invalid=0,maxDepthTerminations=0,mediumUnderflows=0,unclosedEscapes=0;
    void add(const PathStats &s){cameraPaths+=s.cameraPaths;segments+=s.segments;dielectricEvents+=s.dielectricEvents;reflections+=s.reflections;transmissions+=s.transmissions;internalReflections+=s.internalReflections;invalid+=s.invalid;maxDepthTerminations+=s.maxDepthTerminations;mediumUnderflows+=s.mediumUnderflows;unclosedEscapes+=s.unclosedEscapes;}
};
struct SampleResult {V3 beauty,albedo,normal;};
SampleResult tracePath(Ray ray,const BVH &bvh,const Lighting &lights,const PhotonMap &photons,const Options &o,RNG &rng,PathStats &stats){
    SampleResult result;V3 beta(1);Medium medium;if(o.cameraWater)medium.water=1;
    bool aovCaptured=false;V3 auxiliaryTransmission(1);
    float etaScale=1,previousPdf=0;bool previousDelta=true,causticChain=false,hasDiffuseVertex=false;
    V3 previousPoint=ray.o;stats.cameraPaths++;
    for(int depth=0;depth<o.maxDepth;depth++){
        stats.segments++;Hit hit;bool found=bvh.intersect(ray,hit);
        if(!found){
            if(medium.water>0||medium.air>0)stats.unclosedEscapes++;
            // An escaped ray is never assigned a fabricated trapped-water
            // color. A closed medium must end with an exit interface.
            bool includeSun=!(o.photons&&hasDiffuseVertex&&causticChain);
            V3 Le=lights.sky.evaluate(ray.d,includeSun);
            float weight=previousDelta?1.f:powerHeuristic(previousPdf,lights.infinitePdf(ray.d));
            result.beauty+=beta*Le*weight;return result;
        }
        if(medium.wet()){
            V3 transmission=expneg(o.absorption*float(hit.t));beta*=transmission;
            if(!aovCaptured)auxiliaryTransmission*=transmission;
        }
        if(!finite(beta)||maxcomp(beta)>1e15f){stats.invalid++;return result;}
        if(hit.mat==LIGHT){
            V3 Le=lights.emission(hit,-ray.d);float weight=previousDelta?1.f:powerHeuristic(previousPdf,lights.areaPdf(previousPoint,hit));
            result.beauty+=beta*Le*weight;return result;
        }
        if(hit.mat==WATER||hit.mat==BUBBLE){
            if(o.geometricWaterNormals)hit.ns=hit.ng;
            if(depth==0&&!o.aovSpecularPaths){result.albedo=V3(1);result.normal=hit.front?hit.ns:-hit.ns;aovCaptured=true;}
            if((hit.mat==WATER&&!hit.front&&medium.water==0)||(hit.mat==BUBBLE&&!hit.front&&medium.air==0))stats.mediumUnderflows++;
            stats.dielectricEvents++;
            Medium after=medium.after(hit);float etaI=medium.index(),etaT=after.index();
            DielectricSample s=sampleDielectric(ray,hit,medium,rng,true);
            if(s.transmitted)stats.transmissions++;else{stats.reflections++;if(etaI>etaT&&fresnel(-dot(ray.d,hit.front?hit.ns:-hit.ns),etaI,etaT)>=.999999f)stats.internalReflections++;}
            beta*=s.factor;etaScale*=s.etaScale;medium=s.medium;
            if(hasDiffuseVertex&&etaI!=etaT)causticChain=true;
            previousDelta=true;ray=Ray(offset(hit,s.direction),s.direction,ray.time);
        }else{
            Surface surface=surfaceAt(hit,medium,o);V3 ng=hit.front?hit.ng:-hit.ng,wo=-ray.d;
            if(dot(wo,surface.n)<=0)surface.n=ng;
            if(!aovCaptured&&(depth==0||o.aovSpecularPaths)){
                result.albedo=surface.albedo*auxiliaryTransmission;result.normal=surface.n;aovCaptured=true;
            }
            // The photon map owns exactly direct-sun paths that contain
            // specular water/bubble interactions before this diffuse vertex.
            // Corresponding escaped camera paths suppress only that sun term.
            result.beauty+=beta*photons.gather(hit,surface,wo,ray.time,o);
            LightSample light=lights.sample(hit.p,rng);
            if(light.pdf>0&&dot(ng,light.d)>0){
                BsdfEval bsdf=evaluateBsdf(surface,wo,light.d);
                if(bsdf.pdf>0&&maxcomp(bsdf.f)>0){
                    D3 origin=offset(hit,light.d);double distance=light.distance<INF?light.distance-4*RAY_EPS:INF;
                    if(!bvh.occluded(origin,light.d,distance,ray.time)){
                        float w=powerHeuristic(light.pdf,bsdf.pdf);
                        result.beauty+=beta*bsdf.f*light.Le*(std::max(0.f,dot(surface.n,light.d))*w/light.pdf);
                    }
                }
            }
            BsdfSample bs=sampleBsdf(surface,wo,rng);
            if(bs.pdf<=0||dot(bs.direction,ng)<=0)return result;
            beta*=bs.weight;previousPdf=bs.pdf;previousPoint=hit.p;previousDelta=false;
            hasDiffuseVertex=true;causticChain=false;ray=Ray(offset(hit,bs.direction),bs.direction,ray.time);
        }
        // When recording auxiliaries through perfect interfaces, keep following
        // this existing path until its first non-delta hit. Turning off roulette
        // for just that prefix avoids biasing the Fresnel feature mixture and
        // does not introduce any additional path tree.
        if(depth>=5&&!(o.aovSpecularPaths&&!aovCaptured)){
            float survive=std::min(.98f,std::max(.05f,maxcomp(beta)*etaScale));
            if(rng.uniform()>survive)return result;beta*=1.f/survive;
        }
    }
    stats.maxDepthTerminations++;return result;
}

// Deterministic native review integrator. This is deliberately an APPROXIMATE
// direct-light/Whitted preview, not an unbiased Monte Carlo GI estimator.
// Exact interface Fresnel, refraction/TIR and Beer attenuation use the same
// geometry, medium bookkeeping and error-bounded ray origins as tracePath.
// Environment diffuse lighting uses fixed hemispherical quadrature; indirect
// diffuse transport/caustics are omitted. No stochastic grain or video filtering.
V3 previewShadow(D3 origin,V3 direction,float distance,float time,Medium medium,
                 const BVH &bvh,const Options &o) {
    V3 visibility(1);float traveled=0;Ray r(origin,direction,time);
    for(int i=0;i<12;i++) {
        Hit h;h.t=distance-traveled;
        if(!bvh.intersect(r,h))return visibility;
        if(h.mat!=WATER&&h.mat!=BUBBLE&&h.mat!=LIGHT)return V3(0);
        if(h.mat==LIGHT)return visibility;
        if(medium.wet())visibility*=expneg(o.absorption*float(h.t));
        Medium next=medium.after(h);V3 n=h.front?h.ns:-h.ns;
        visibility*=1.f-fresnel(std::abs(dot(direction,n)),medium.index(),next.index());
        medium=next;traveled+=float(h.t);if(traveled>=distance)return visibility;
        r=Ray(offset(h,direction),direction,time);
    }
    return visibility;
}
V3 traceCleanBranch(Ray ray,Medium medium,const BVH &bvh,const Lighting &lights,
                    const Options &o,PathStats &stats,int depth,float contribution) {
    stats.segments++;
    if(depth>=o.maxDepth||contribution<.0025f){stats.maxDepthTerminations++;return V3(0);}
    Hit hit;
    if(!bvh.intersect(ray,hit)) {
        if(medium.water>0||medium.air>0)stats.unclosedEscapes++;
        // Fixed diffuse studio emitters avoid a subpixel delta-sun firefly.
        return lights.sky.base(ray.d);
    }
    V3 attenuation=medium.wet()?expneg(o.absorption*float(hit.t)):V3(1);
    if(hit.mat==LIGHT)return attenuation*lights.emission(hit,-ray.d);
    if(hit.mat==WATER||hit.mat==BUBBLE) {
        stats.dielectricEvents++;
        Medium next=medium.after(hit);float ni=medium.index(),nt=next.index();
        V3 ng=hit.front?hit.ng:-hit.ng,ns=hit.front?hit.ns:-hit.ns;
        if(dot(-ray.d,ns)<=1e-4f)ns=ng;
        V3 wt;bool transmit=refract(ray.d,ns,ni,nt,wt);
        if(transmit&&dot(wt,ng)>=-1e-6f){ns=ng;transmit=refract(ray.d,ns,ni,nt,wt);}
        float F=transmit?fresnel(-dot(ray.d,ns),ni,nt):1.f;
        V3 wr=unit(reflect(ray.d,ns));if(dot(wr,ng)<=1e-6f)wr=unit(reflect(ray.d,ng));
        V3 c;
        if(F>0){stats.reflections++;c+=traceCleanBranch(Ray(offset(hit,wr),wr,ray.time),medium,bvh,lights,o,stats,depth+1,contribution*F)*F;}
        if(transmit&&F<1){stats.transmissions++;float f=(1-F)*sq(ni/nt);c+=traceCleanBranch(Ray(offset(hit,wt),wt,ray.time),next,bvh,lights,o,stats,depth+1,contribution*f)*f;}
        return c*attenuation;
    }
    Surface surface=surfaceAt(hit,medium,o);V3 n=surface.n,wo=-ray.d;
    if(dot(n,wo)<=0)n=hit.front?hit.ng:-hit.ng;
    surface.n=n;Frame basis(n);V3 ambient;
    // Six fixed cosine-weighted directions: smooth, deterministic approximation.
    for(int i=0;i<6;i++) {
        float z=std::sqrt((i+.5f)/6.f),r=std::sqrt(1-z*z),a=i*2.39996323f;
        V3 d=basis.world(V3(r*std::cos(a),r*std::sin(a),z));
        ambient+=lights.sky.base(d)/6.f;
    }
    V3 result=surface.albedo*ambient*(1-surface.f0);
    V3 reflected=unit(reflect(ray.d,n));
    result+=lights.sky.base(reflected)*schlick(std::abs(dot(wo,n)),surface.f0)*(1-.65f*surface.roughness);
    V3 sun=lights.sky.sunDir;float cosine=std::max(0.f,dot(n,sun));
    if(cosine>0&&maxcomp(lights.sky.sunRadiance)>0) {
        V3 visible=previewShadow(offset(hit,sun),sun,INF,ray.time,medium,bvh,o);
        result+=evaluateBsdf(surface,wo,sun).f*lights.sky.sunRadiance*(lights.sky.sunOmega*cosine)*visible;
    }
    for(const auto &light:lights.areas)for(int k=0;k<4;k++) {
        V3 point=light.center+light.u*((k&1)?.55f:-.55f)+light.v*((k&2)?.55f:-.55f);
        V3 delta=point-hit.p;float dist=length(delta);V3 d=delta/dist;
        float c=std::max(0.f,dot(n,d))*std::max(0.f,dot(light.n,-d));
        if(c<=0)continue;
        V3 visible=previewShadow(offset(hit,d),d,dist*.99999f,ray.time,medium,bvh,o);
        result+=evaluateBsdf(surface,wo,d).f*light.radiance*(.25f*light.area*c/(dist*dist))*visible;
    }
    return result*attenuation;
}
SampleResult traceClean(Ray ray,const BVH &bvh,const Lighting &lights,const Options &o,PathStats &stats) {
    stats.cameraPaths++;Medium m;if(o.cameraWater)m.water=1;SampleResult r;
    r.beauty=traceCleanBranch(ray,m,bvh,lights,o,stats,0,1.f);return r;
}

struct Camera {
    V3 eye,forward,right,up;float tanFov,aspect,focus,aperture;
    explicit Camera(const Options &o):eye(o.eye),forward(unit(o.target-o.eye)),right(unit(cross(forward,o.up))),up(cross(right,forward)),tanFov(std::tan(o.fov*PI/360)),aspect(float(o.width)/o.height),focus(o.focus),aperture(o.aperture){}
    Ray generate(float px,float py,float lu,float lv,float time,const Options &o)const{
        V3 direction=unit(forward+right*((2*px/o.width-1)*aspect*tanFov)+up*((1-2*py/o.height)*tanFov));
        auto disk=concentricDisk(lu,lv);V3 offsetLens=(right*disk.first+up*disk.second)*aperture;
        V3 focusPoint=eye+direction*(focus/std::max(.001f,dot(direction,forward)));
        return Ray(eye+offsetLens,unit(focusPoint-eye-offsetLens),time);
    }
};
float srgb(float x){x=clamp(x);return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;}
V3 toneMap(V3 c,float ev){
    c*=std::exp2(ev);
    // Luminance-preserving shoulder, followed by an sRGB display transfer.
    // Highlights retain their hue instead of independent-channel clipping.
    float l=lum(c),white=4.f,mapped=l*(1+l/(white*white))/(1+l);
    if(l>1e-8f)c*=mapped/l;
    float maxValue=maxcomp(c);if(maxValue>1)c=c/maxValue;
    return {srgb(c.x),srgb(c.y),srgb(c.z)};
}
void writePfm(const std::string &path,const std::vector<V3> &pixels,int w,int h){
    std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot write "+path);
    f<<"PF\n"<<w<<" "<<h<<"\n-1.0\n";
    for(int y=h-1;y>=0;y--)f.write(reinterpret_cast<const char*>(pixels.data()+size_t(y)*w),std::streamsize(w*sizeof(V3)));
    if(!f)throw std::runtime_error("PFM write failed "+path);
}
void writePpm(const std::string &path,const std::vector<V3> &pixels,int w,int h,float exposure){
    std::ofstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot write "+path);
    f<<"P6\n"<<w<<" "<<h<<"\n255\n";std::vector<uint8_t> bytes(size_t(w)*h*3);
    for(size_t i=0;i<pixels.size();i++){V3 p=toneMap(pixels[i],exposure);for(int c=0;c<3;c++)bytes[i*3+c]=uint8_t(std::lround(clamp(p[c])*255));}
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));if(!f)throw std::runtime_error("PPM write failed");
}
std::string jsonString(const std::string &s){
    std::ostringstream out;out<<'"';
    for(unsigned char c:s){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else out<<char(c);}out<<'"';return out.str();
}
void writeJson(const std::string &path,const Options &o,const BVH &bvh,const PhotonMap &photons,const PathStats &stats,double buildSeconds,double renderSeconds,const std::vector<V3> &pixels){
    std::ofstream f(path);if(!f)throw std::runtime_error("Cannot write metadata");
    double sum=0;float peak=0;uint64_t finitePixels=0;
    for(V3 p:pixels){if(finite(p))finitePixels++;sum+=lum(p);peak=std::max(peak,maxcomp(p));}
    f<<std::setprecision(10)<<"{\n  \"renderer\": "<<jsonString(o.deterministicPreview?"CYBR WATER VFX deterministic direct-light Whitted preview":"CYBR WATER VFX native Monte Carlo surface transport")<<",\n"
     <<"  \"width\": "<<o.width<<", \"height\": "<<o.height<<", \"samples_per_pixel\": "<<o.spp<<",\n"
     <<"  \"max_path_depth\": "<<o.maxDepth<<", \"threads\": "<<o.threads<<", \"seed\": "<<o.seed<<",\n"
     <<"  \"inputs\": {\"water\": "<<jsonString(o.water)<<", \"water_end\": "<<jsonString(o.waterEnd)<<", \"solids\": "<<jsonString(o.solids)<<", \"solids_end\": "<<jsonString(o.solidsEnd)<<", \"spheres\": "<<jsonString(o.spheres)<<"},\n"
     <<"  \"camera\": ["<<o.eye.x<<","<<o.eye.y<<","<<o.eye.z<<"], \"target\": ["<<o.target.x<<","<<o.target.y<<","<<o.target.z<<"], \"vertical_fov_degrees\": "<<o.fov<<",\n"
     <<"  \"environment\": "<<jsonString(o.environment)<<", \"sky_power\": "<<o.skyPower<<", \"sun_radiance\": "<<o.sunPower<<", \"sun_angular_radius_degrees\": "<<o.sunAngle<<", \"sun_direction\": ["<<o.sunDir.x<<","<<o.sunDir.y<<","<<o.sunDir.z<<"],\n"
     <<"  \"triangles\": "<<bvh.triangles.size()<<", \"spheres\": "<<bvh.spheres.size()<<", \"bvh_nodes\": "<<bvh.nodes.size()<<", \"bvh_depth\": "<<bvh.maxDepth<<",\n"
     <<"  \"build_seconds\": "<<buildSeconds<<", \"render_seconds\": "<<renderSeconds<<",\n"
     <<"  \"camera_paths\": "<<stats.cameraPaths<<", \"segments\": "<<stats.segments<<",\n"
     <<"  \"dielectric_events\": "<<stats.dielectricEvents<<", \"reflections\": "<<stats.reflections<<", \"transmissions\": "<<stats.transmissions<<", \"total_internal_reflections\": "<<stats.internalReflections<<",\n"
     <<"  \"nonfinite_paths\": "<<stats.invalid<<", \"medium_underflows\": "<<stats.mediumUnderflows<<", \"unclosed_medium_escapes\": "<<stats.unclosedEscapes<<", \"max_depth_terminations\": "<<stats.maxDepthTerminations<<",\n"
     <<"  \"ray_position_precision\": \"float64\", \"mesh_storage_precision\": \"float32\", \"ray_minimum_distance_metres\": "<<RAY_EPS<<",\n"
     <<"  \"ray_origin_offset\": \"Float64 componentwise gamma(12) position-error envelope projected onto geometric normal, followed by outward nextafter rounding\",\n"
     <<"  \"triangle_intersection\": \"Watertight Float64 sheared edge test; either-sign uncertain near-origin plane distance uses extended long double or compensated double-double before sign rejection; no spatial cutoff\",\n"
     <<"  \"uncertain_triangle_plane_strategy\": \""<<(std::numeric_limits<long double>::digits>std::numeric_limits<double>::digits?"extended-long-double":"compensated-double-double")<<"\",\n"
     <<"  \"platform_long_double_precision_bits\": "<<std::numeric_limits<long double>::digits<<",\n"
     <<"  \"sphere_intersection\": \"Float64 closest approach with reprojection to the analytic surface\",\n"
     <<"  \"material_identity\": \"stable role/group ranges independent of dynamic primitive count\",\n"
     <<"  \"finite_pixels\": "<<finitePixels<<", \"mean_linear_luminance\": "<<sum/pixels.size()<<", \"peak_linear_channel\": "<<peak<<",\n"
     <<"  \"water_ior\": 1.333, \"absorption_per_metre\": ["<<o.absorption.x<<","<<o.absorption.y<<","<<o.absorption.z<<"],\n"
     <<"  \"exposure_ev\": "<<o.exposure<<", \"lens_radius_metres\": "<<o.aperture<<", \"focus_metres\": "<<o.focus<<",\n"
     <<"  \"shutter_seconds\": "<<o.shutterSeconds<<", \"motion_mesh\": "<<(!o.waterEnd.empty()?"true":"false")<<",\n"
     <<"  \"sun_caustics\": {\"method\": \"normal-and-object-filtered Epanechnikov photon density estimate\", \"emitted\": "<<photons.emitted<<", \"stored\": "<<photons.photons.size()<<", \"radius_metres\": "<<photons.radius<<", \"temporal_bins\": "<<photons.bins<<", \"double_count_exclusion\": true},\n"
     <<"  \"aovs\": "<<(o.aovs?"true":"false")<<", \"aov_specular_paths\": "<<(o.aovSpecularPaths?"true":"false")<<", \"aov_definition\": "
     <<jsonString(o.aovSpecularPaths?"Fresnel-sampled first non-delta surface reflectance and signed world normal through perfect specular paths; Beer attenuation included in reflectance; identical camera samples":"linear first-surface base reflectance and signed world-space normal, averaged over identical camera samples")<<",\n"
     <<"  \"limitations\": "<<(o.deterministicPreview?"[\"deterministic preview, not unbiased global illumination\",\"fixed hemispherical diffuse quadrature\",\"no diffuse interreflection or caustics\",\"12-bounce/low-contribution branch cutoff in delivered sequence\",\"straight shadow transmission approximation\"]":"[\"RGB rather than spectral transport\",\"finite bounce cutoff with Russian roulette\",\"finite-radius sun caustic estimate when enabled\"]")<<"\n}\n";
}

void selfTest(){
    auto require=[](bool condition,const std::string &message){if(!condition)throw std::runtime_error("Self-test: "+message);};
    float F=fresnel(1.f,1.f,1.333f);require(std::abs(F-sq((1.333f-1)/(1.333f+1)))<1e-7f,"normal Fresnel");
    require(fresnel(.5f,1.333f,1.f)==1,"total internal reflection");
    V3 wt;require(refract({0,-1,0},{0,1,0},1,1.333f,wt)&&length(wt-V3(0,-1,0))<1e-6f,"normal refraction");
    BVH bvh;bvh.spheres.push_back({V3(0),V3(0),1,WATER,1});bvh.build();
    double white=0,beer=0;constexpr int N=100000;float absorption=.4f;
    for(int i=0;i<N;i++){
        RNG rng(i+117);Ray ray({0,0,3},{0,0,-1});Medium medium;float beta=1,absorbed=1;
        for(int depth=0;depth<64;depth++){
            Hit hit;if(!bvh.intersect(ray,hit)){white+=beta;beer+=absorbed;break;}
            if(medium.wet())absorbed*=std::exp(-absorption*hit.t);
            auto s=sampleDielectric(ray,hit,medium,rng,true);beta*=s.factor;absorbed*=s.factor;medium=s.medium;
            ray=Ray(offset(hit,s.direction),s.direction);
        }
    }
    // Uniform illumination exists on both sides. After each internal return,
    // either interface may release a path into that same environment; the
    // geometric series therefore advances by F*A, not by F^2*A^2.
    white/=N;beer/=N;float A=std::exp(-2*absorption),expected=F+sq(1-F)*A/(1-F*A);
    require(std::abs(white-1)<.0001,"white dielectric sphere energy");
    require(std::abs(beer-expected)<.003,"Beer segment length with internal reflections");
    Medium m;Hit h;h.mat=WATER;h.front=true;m=m.after(h);require(m.wet(),"water entry");
    h.mat=BUBBLE;m=m.after(h);require(!m.wet()&&m.index()==1,"nested air entry");
    h.front=false;m=m.after(h);require(m.wet(),"nested air exit");
    h.mat=WATER;m=m.after(h);require(!m.wet(),"water exit");
    BVH moving;moving.addTriangle({-1,-1,0},{1,-1,0},{0,1,0},{0,0,1},{0,0,1},{0,0,1},ROCK,1);
    moving.triangles[0].da=moving.triangles[0].db=moving.triangles[0].dc={0,0,1};moving.triangles[0].motion=true;moving.build();
    Hit ha,hb;require(moving.intersect(Ray({0,0,3},{0,0,-1},0),ha)&&moving.intersect(Ray({0,0,3},{0,0,-1},1),hb),"moving triangle hit");
    require(std::abs(ha.t-3)<1e-6f&&std::abs(hb.t-2)<1e-6f,"motion interpolation");
    // Valid faces far below the former absolute area cutoff must survive
    // import and keep unit geometric normals across a closed interface pair.
    constexpr float e=1e-8f;BVH tiny;V3 a{0,0,0},b{e,0,0},c{0,e,0},d{0,0,e};
    tiny.addTriangle(a,c,b,{0,0,-1},{0,0,-1},{0,0,-1},WATER,1);
    tiny.addTriangle(a,b,d,{0,-1,0},{0,-1,0},{0,-1,0},WATER,1);
    tiny.addTriangle(a,d,c,{-1,0,0},{-1,0,0},{-1,0,0},WATER,1);
    V3 diagonal=unit(V3(1));tiny.addTriangle(b,c,d,diagonal,diagonal,diagonal,WATER,1);
    require(tiny.triangles.size()==4,"tiny nonzero-area faces retained");tiny.build();
    Ray tinyRay({.2*e,.2*e,2*e},{0,0,-1});Hit entry,leave;
    require(tiny.intersect(tinyRay,entry)&&entry.front,"tiny closed tetrahedron entry");
    require(std::abs(length(entry.ng)-1)<2e-7f,"tiny entry unit geometric normal");
    tinyRay=Ray(offset(entry,tinyRay.d),tinyRay.d);
    require(tiny.intersect(tinyRay,leave)&&!leave.front,"tiny closed tetrahedron exit");
    require(std::abs(length(leave.ng)-1)<2e-7f,"tiny exit unit geometric normal");
    Hit beyond;require(!tiny.intersect(Ray(offset(leave,tinyRay.d),tinyRay.d),beyond),"tiny tetrahedron closed interface accounting");
    BVH plane;plane.addTriangle({0,0,3},{2,0,3},{0,2,3},{0,0,1},{0,0,1},{0,0,1},WATER,1);plane.build();
    Hit planeHit;require(plane.intersect(Ray({.5,.5,4},{0,0,-1}),planeHit),"offset error-bound plane hit");
    D3 above=offset(planeHit,{0,0,1}),below=offset(planeHit,{0,0,-1});
    require(above.z>3&&below.z<3&&above.z-3<1e-12&&3-below.z<1e-12,"position-error offset sides and scale");
    Hit selfHit;require(!plane.intersect(Ray(above,{0,0,1}),selfHit)&&!plane.intersect(Ray(below,{0,0,-1}),selfHit),"offset plane avoids self-intersection");
    // The exact drop33 plane pair guards both the false self-hit and the
    // genuinely positive reversed ray, without accepting a spatial epsilon.
    BVH nearPlane;
    V3 pa(-0.057374998927116394f,0.0006332624470815063f,-0.008929235860705376f);
    V3 pb(-0.05673757568001747f,0.0006374226650223136f,-0.008925075642764568f);
    V3 pc(-0.05673757568001747f,0.0006374226068146527f,-0.008924998342990875f),pn;
    triangleNormal(pa,pb,pc,pn);nearPlane.addTriangle(pa,pb,pc,pn,pn,pn,WATER,1);
    D3 po{-0.057123413431534827,0.00063490442876355596,-0.0089275663640161311};
    V3 pd(0.68056100606918335f,0.45588186383247375f,0.57359254360198975f);
    Hit away,toward;require(!nearPlane.triangleHit(0,Ray(po,pd),away),"uncertain negative plane rejects false self-hit");
    require(nearPlane.triangleHit(0,Ray(po,-pd),toward)&&toward.t>0&&std::abs(toward.t-9.166719704692891e-18)<1e-24,"uncertain positive plane retains tiny segment");
    double maxBsdfEnergy=0;RNG r(29);
    for(float rough:{.12f,.4f,.8f}){
        Surface s;s.n={0,0,1};s.albedo=V3(.5f);s.roughness=rough;s.f0=.04f;s.specProbability=.3f;double sum=0;
        for(int i=0;i<100000;i++){auto v=sampleBsdf(s,{0,0,1},r);sum+=lum(v.weight);}
        maxBsdfEnergy=std::max(maxBsdfEnergy,sum/100000);require(sum/100000<1.005,"BSDF hemispherical energy");
    }
    std::cout<<std::setprecision(10)<<"{\"passed\":true,\"normal_water_fresnel\":"<<F<<",\"white_sphere_mean\":"<<white
        <<",\"beer_sphere_mean\":"<<beer<<",\"beer_sphere_analytic\":"<<expected<<",\"nested_air_water\":true,\"motion_triangle\":true,\"tiny_closed_tetrahedron\":true,\"adaptive_origin_sides\":true,\"near_origin_plane_pair\":true,\"max_tested_bsdf_energy\":"<<maxBsdfEnergy<<"}\n";
}
void auditInterfaces(const BVH &bvh,const Options &o){
    RNG rng(1729);const Bounds &bounds=bvh.nodes[0].bounds;V3 center=bounds.center();float radius=length(bounds.hi-bounds.lo)*1.5f;
    uint64_t hits=0,negative=0,unclosed=0,firstBack=0,maxHits=0;std::vector<std::string> examples;
    constexpr int rays=30000;
    for(int i=0;i<rays;i++){
        V3 origin=center+uniformSphere(rng.uniform(),rng.uniform())*radius;
        V3 target=bounds.lo+(bounds.hi-bounds.lo)*V3(rng.uniform(),rng.uniform(),rng.uniform());
        V3 dir=unit(target-origin);Ray ray(origin,dir);int winding=0,n=0;
        for(;n<300;n++){
            Hit h;if(!bvh.intersect(ray,h))break;
            if(h.mat==WATER){winding+=h.front?1:-1;hits++;if(n==0&&!h.front)firstBack++;
                if(winding<0){negative++;if(examples.size()<8){std::ostringstream e;e<<std::setprecision(9)<<"{\"ray\":"<<i<<",\"hit\":"<<n<<",\"position\":["<<h.p.x<<","<<h.p.y<<","<<h.p.z<<"],\"primitive\":"<<h.primitive<<",\"front\":"<<(h.front?"true":"false")<<"}";examples.push_back(e.str());}}}
            // Continue without refraction to test the topological winding
            // sequence independently of Fresnel, shading normals and BSDFs.
            ray=Ray(offset(h,dir),dir);
        }
        if(winding!=0)unclosed++;maxHits=std::max(maxHits,uint64_t(n));
    }
    std::ofstream f(o.out+"_interfaces.json");f<<"{\"rays\":"<<rays<<",\"water_hits\":"<<hits<<",\"negative_winding_hits\":"<<negative<<",\"unclosed_rays\":"<<unclosed<<",\"first_hit_backface\":"<<firstBack<<",\"maximum_intersections\":"<<maxHits<<",\"ray_epsilon\":"<<RAY_EPS<<",\"examples\":[";
    for(size_t i=0;i<examples.size();i++){if(i)f<<",";f<<examples[i];}f<<"]}\n";
    std::cerr<<"Interface audit: "<<rays<<" rays, "<<hits<<" hits, "<<negative<<" negative winding hits, "<<unclosed<<" unclosed rays\n";
}
int run(int argc,char **argv){
    Options o=parseArgs(argc,argv);if(o.selfTest){selfTest();return 0;}
    if(o.water.empty()&&o.solids.empty())throw std::runtime_error("Supply --water and/or --solids OBJ");
    auto start=std::chrono::steady_clock::now();omp_set_num_threads(o.threads);
    std::filesystem::path base(o.out);if(!base.parent_path().empty())std::filesystem::create_directories(base.parent_path());
    BVH bvh;if(!o.water.empty())appendMesh(bvh,o.water,WATER,o.waterEnd);if(!o.solids.empty())appendMesh(bvh,o.solids,ROCK,o.solidsEnd);
    if(!o.spheres.empty())appendSpheres(bvh,o.spheres,o.shutterSeconds);
    Lighting lighting(o,bvh);bvh.build();
    std::cerr<<"BVH: "<<bvh.triangles.size()<<" triangles + "<<bvh.spheres.size()<<" spheres / "<<bvh.nodes.size()<<" nodes / depth "<<bvh.maxDepth<<"\n";
    if(o.auditInterfaces){auditInterfaces(bvh,o);return 0;}
    PhotonMap photons;photons.build(bvh,lighting,o);
    auto built=std::chrono::steady_clock::now();double buildSeconds=std::chrono::duration<double>(built-start).count();
    Camera camera(o);std::vector<V3> pixels(size_t(o.width)*o.height),albedo,normal;if(o.aovs){albedo.resize(pixels.size());normal.resize(pixels.size());}
    std::vector<PathStats> threadStats(size_t(o.threads));std::atomic<int> completed{0};int tileSize=16,tx=(o.width+tileSize-1)/tileSize,ty=(o.height+tileSize-1)/tileSize;
    #pragma omp parallel for schedule(dynamic,1) num_threads(o.threads)
    for(int tile=0;tile<tx*ty;tile++){
        int tid=omp_get_thread_num(),tileX=tile%tx,tileY=tile/tx;
        for(int y=tileY*tileSize;y<std::min(o.height,(tileY+1)*tileSize);y++)for(int x=tileX*tileSize;x<std::min(o.width,(tileX+1)*tileSize);x++){
            size_t pixel=size_t(y)*o.width+x;RNG rng((uint64_t(o.seed)<<32)^pixel^0x717acef06a54b863ULL);
            V3 sum,aa,nn;
            // A randomized Latin hypercube covers both pixel dimensions for
            // arbitrary sample counts (including non-square counts such as48).
            int stride=o.spp==1?1:int(rng.next()%uint32_t(o.spp));
            while(std::gcd(stride,o.spp)!=1)stride=(stride+1)%o.spp;
            int shift=o.spp==1?0:int(rng.next()%uint32_t(o.spp));
            for(int s=0;s<o.spp;s++){
                int permuted=int((uint64_t(s)*uint64_t(stride)+uint64_t(shift))%uint64_t(o.spp));
                float dx=(s+rng.uniform())/o.spp,dy=(permuted+rng.uniform())/o.spp;
                if(o.deterministicPreview) {
                    int side=int(std::ceil(std::sqrt(float(o.spp))));
                    dx=(s%side+.5f)/side;dy=(s/side+.5f)/side;
                }
                // Path, lens and shutter draws remain independently randomized.
                float time=mix(o.shutterOpen,o.shutterClose,rng.uniform());
                Ray ray=camera.generate(x+dx,y+dy,rng.uniform(),rng.uniform(),time,o);
                SampleResult value=o.deterministicPreview?traceClean(ray,bvh,lighting,o,threadStats[size_t(tid)]):tracePath(ray,bvh,lighting,photons,o,rng,threadStats[size_t(tid)]);
                if(!finite(value.beauty)){threadStats[size_t(tid)].invalid++;value.beauty=V3(0);}
                sum+=value.beauty;aa+=value.albedo;nn+=value.normal;
            }
            pixels[pixel]=sum/float(o.spp);if(o.aovs){albedo[pixel]=aa/float(o.spp);normal[pixel]=nn/float(o.spp);}
        }
        int n=completed.fetch_add(1)+1;
        if(n==tx*ty||n%std::max(1,tx*ty/20)==0){
            #pragma omp critical(progress)
            {double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-built).count();std::cerr<<"Render "<<int(100.f*n/(tx*ty))<<"%  "<<std::fixed<<std::setprecision(1)<<elapsed<<" s\n";}
        }
    }
    double renderSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-built).count();PathStats stats;for(const auto &s:threadStats)stats.add(s);
    if(stats.invalid)throw std::runtime_error("Nonfinite transport detected; refusing a successful render");
    writePfm(o.out+".pfm",pixels,o.width,o.height);writePpm(o.out+".ppm",pixels,o.width,o.height,o.exposure);
    if(o.aovs){writePfm(o.out+"_albedo.pfm",albedo,o.width,o.height);writePfm(o.out+"_normal.pfm",normal,o.width,o.height);}
    writeJson(o.out+".json",o,bvh,photons,stats,buildSeconds,renderSeconds,pixels);
    {std::ofstream method(o.out+"_method.json");method<<"{\"integrator\":\""<<(o.deterministicPreview?"deterministic_direct_Whitted_preview":"Monte_Carlo_path_tracing")<<"\",\"spp\":"<<o.spp<<",\"approximate_diffuse_GI\":"<<(o.deterministicPreview?"true":"false")<<",\"image_generation\":false}\n";}
    std::cerr<<"Saved "<<o.out<<".pfm/.ppm/.json in "<<renderSeconds<<" render seconds; "<<stats.cameraPaths<<" paths, "<<stats.segments<<" segments\n";
    return 0;
}
} // namespace cybr
int main(int argc,char **argv){try{return cybr::run(argc,argv);}catch(const std::exception &e){std::cerr<<"ERROR: "<<e.what()<<"\n";return 1;}}
