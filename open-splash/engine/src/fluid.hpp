#pragma once
// CYBR WATER VFX: one staggered-grid FLIP/APIC liquid core.
// Units: metres, seconds, kilograms. Negative SDF values mean inside.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

namespace water {
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3()=default;
    Vec3(float a,float b,float c):x(a),y(b),z(c){}
    float &operator[](int a){return a==0?x:(a==1?y:z);}
    float operator[](int a)const{return a==0?x:(a==1?y:z);}
    Vec3 operator+(Vec3 q)const{return {x+q.x,y+q.y,z+q.z};}
    Vec3 operator-(Vec3 q)const{return {x-q.x,y-q.y,z-q.z};}
    Vec3 operator-()const{return {-x,-y,-z};}
    Vec3 operator*(float s)const{return {x*s,y*s,z*s};}
    Vec3 operator/(float s)const{return {x/s,y/s,z/s};}
    Vec3 &operator+=(Vec3 q){x+=q.x;y+=q.y;z+=q.z;return *this;}
    Vec3 &operator-=(Vec3 q){x-=q.x;y-=q.y;z-=q.z;return *this;}
    Vec3 &operator*=(float s){x*=s;y*=s;z*=s;return *this;}
};
inline Vec3 operator*(float s,Vec3 p){return p*s;}
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(Vec3 p){return std::sqrt(dot(p,p));}
inline Vec3 normalized(Vec3 p){return p/std::max(1e-20f,length(p));}
inline float clamp(float x,float a,float b){return std::max(a,std::min(b,x));}
inline bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
inline uint64_t mix64(uint64_t x){x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);}
inline float random01(uint64_t x){return float((mix64(x)>>40)&0xffffffu)*(1.0f/16777216.0f);}
inline Vec3 randomVector(uint64_t x){return {random01(x)-.5f,random01(x+1)-.5f,random01(x+2)-.5f};}

struct Inlet {
    Vec3 lower,upper,velocity;
    int axis=0, direction=1;
    float rateScale=1;
};
// Optional art-direction input. It acts on MAC faces BEFORE pressure projection.
// It never changes marker positions, creates water, or bypasses incompressibility.
struct VelocityGuide {
    Vec3 target{}, acceleration{};
    float rate=0, accelerationLimit=50;
};
struct Scene {
    Vec3 lower{-1.8f,-.35f,-1.2f},upper{1.8f,2.05f,1.2f};
    std::function<float(Vec3)> solidSdf,initialLiquidSdf;
    std::function<Vec3(Vec3)> initialVelocity;
    std::function<Vec3(Vec3,double)> bodyAcceleration;
    std::function<void(double)> prepareForces;
    std::function<VelocityGuide(Vec3,double)> velocityGuide;
    float extraAccelerationBound=0;
    std::vector<Inlet> inlets;
    // x-low, x-high, y-low, y-high, z-low, z-high. Closed faces are
    // stationary frictionless walls; omitted/default faces permit outflow.
    std::array<bool,6> closedBoundaries{};
    std::string name="liquid";
};

struct Config {
    float dx=.05f, cfl=.9f, flipRatio=.97f;
    float density=1000, pressureTolerance=2e-5f, pressureAbsoluteTolerance=2e-7f;
    Vec3 gravity{0,-9.81f,0};
    int threads=4,pressureIterations=160,extrapolationLayers=4;
    int particlesPerAxis=2,reseedInterval=8,minParticlesPerCell=3,maxParticlesPerCell=24;
    bool apic=false,whitewater=true,refineSolidContact=true;
    float whitewaterRate=24000; // secondary parcels / square metre / second at full activation
    float whitewaterLength=.05f; // physical normalization length, independent of numerical grid resolution
    size_t maxParticles=4000000,maxWhitewater=500000;
    uint64_t seed=37;
    uint64_t buildSignature=0;
};
struct Particle {
    Vec3 p,v;
    std::array<float,9> C{};
    float volume=0;
    uint64_t id=0;
};
enum WhiteType:uint32_t {Spray=0,Foam=1,Bubble=2};
struct WhiteParticle {
    Vec3 p,v;
    float radius=.001f,age=0,lifetime=3;
    uint32_t type=Spray;
    uint64_t id=0;
};
struct Statistics {
    uint64_t steps=0,initialParticles=0,emittedParticles=0,outflowParticles=0;
    uint64_t splitParticles=0,mergedParticles=0,whiteEmitted=0,whiteExpired=0,whiteRejected=0;
    uint64_t collisionProjections=0;
    uint64_t inletAttempts=0,inletRejectedAttempts=0;
    double initialVolume=0,emittedVolume=0,outflowVolume=0;
    double requestedInletVolume=0,prescribedInletVolume=0;
    double time=0,dt=0,pressureRelativeResidual=0,pressureAbsoluteResidual=0;
    double divergenceBeforeL2=0,divergenceAfterL2=0,divergenceAfterMax=0;
    double volume=0,massLedgerError=0,kineticEnergy=0,maxSpeed=0;
    double transferSeconds=0,pressureSeconds=0,advectionSeconds=0,secondarySeconds=0,stepSeconds=0;
    int pressureIterations=0,fluidCells=0;
};

struct Array3 {
    int nx=0,ny=0,nz=0;
    std::vector<float> a;
    Array3()=default;
    Array3(int x,int y,int z,float val=0):nx(x),ny(y),nz(z),a(size_t(x)*y*z,val){}
    size_t index(int x,int y,int z)const{return (size_t(z)*ny+y)*nx+x;}
    float &operator()(int x,int y,int z){return a[index(x,y,z)];}
    float operator()(int x,int y,int z)const{return a[index(x,y,z)];}
    float get(int x,int y,int z)const{return (*this)(std::clamp(x,0,nx-1),std::clamp(y,0,ny-1),std::clamp(z,0,nz-1));}
    void clear(float v=0){std::fill(a.begin(),a.end(),v);}
    float sample(Vec3 q)const {
        q.x=clamp(q.x,0,float(nx-1));q.y=clamp(q.y,0,float(ny-1));q.z=clamp(q.z,0,float(nz-1));
        int x=int(std::floor(q.x)),y=int(std::floor(q.y)),z=int(std::floor(q.z));
        float tx=q.x-x,ty=q.y-y,tz=q.z-z,answer=0;
        for(int k=0;k<2;k++)for(int j=0;j<2;j++)for(int i=0;i<2;i++)
            answer+=(i?tx:1-tx)*(j?ty:1-ty)*(k?tz:1-tz)*get(x+i,y+j,z+k);
        return answer;
    }
};
struct FaceGrid {
    Array3 value,old,weight,open;
    std::vector<uint8_t> valid,fixed;
    Vec3 offset;
    FaceGrid()=default;
    FaceGrid(int nx,int ny,int nz,Vec3 offset_):value(nx,ny,nz),old(nx,ny,nz),weight(nx,ny,nz),open(nx,ny,nz,1),valid(size_t(nx)*ny*nz),fixed(valid.size()),offset(offset_){}
};

class Solver {
public:
    Scene scene;
    Config config;
    int nx=0,ny=0,nz=0;
    size_t cells=0;
    double time=0,lastDt=0;
    uint64_t nextParticleId=1,nextWhiteId=1;
    uint64_t stateSignature=0;
    std::vector<Particle> particles;
    std::vector<WhiteParticle> white;
    std::vector<double> inletDebt;
    std::vector<double> inletFlux;
    Statistics stats;
    Array3 solid,phi;
    std::array<FaceGrid,3> grid;
    std::vector<uint8_t> type,surface;
    std::vector<int> active;
    std::vector<float> diag,cx,cy,cz,pressure,rhs,precon,workR,workZ,workS,workQ,workT;
    std::vector<std::vector<float>> scatterVelocity,scatterWeight,scatterPhi;
    std::array<size_t,3> faceOffset{};
    size_t totalFaces=0;

    explicit Solver(Scene s,Config c):scene(std::move(s)),config(c) {
        if(!(config.dx>0)||config.particlesPerAxis<1||config.particlesPerAxis>3)throw std::runtime_error("Invalid grid/marker configuration");
        if(!scene.solidSdf||!scene.initialLiquidSdf)throw std::runtime_error("Scene SDF callbacks are required");
#ifdef _OPENMP
        omp_set_dynamic(0);omp_set_num_threads(config.threads);
#endif
        Vec3 d=(scene.upper-scene.lower)/config.dx;
        nx=int(std::ceil(d.x-1e-4f));ny=int(std::ceil(d.y-1e-4f));nz=int(std::ceil(d.z-1e-4f));
        if(nx<4||ny<4||nz<4)throw std::runtime_error("Grid is too small");
        scene.upper=scene.lower+Vec3(float(nx),float(ny),float(nz))*config.dx;
        cells=size_t(nx)*ny*nz;
        solid=Array3(nx+1,ny+1,nz+1);phi=Array3(nx,ny,nz,4*config.dx);
        grid[0]=FaceGrid(nx+1,ny,nz,{0,.5f,.5f});
        grid[1]=FaceGrid(nx,ny+1,nz,{.5f,0,.5f});
        grid[2]=FaceGrid(nx,ny,nz+1,{.5f,.5f,0});
        for(int a=0;a<3;a++){faceOffset[a]=totalFaces;totalFaces+=grid[a].value.a.size();}
        type.resize(cells);surface.resize(cells);
        for(auto *v:{&diag,&cx,&cy,&cz,&pressure,&rhs,&precon,&workR,&workZ,&workS,&workQ,&workT})v->resize(cells);
        int nt=std::max(1,config.threads);
        scatterVelocity.resize(nt,std::vector<float>(totalFaces));
        scatterWeight.resize(nt,std::vector<float>(totalFaces));
        scatterPhi.resize(nt,std::vector<float>(cells));
        inletDebt.resize(scene.inlets.size());
        inletFlux.resize(scene.inlets.size());
        cacheSolids();
        stateSignature=configurationSignature();
    }
    Vec3 cellPosition(int x,int y,int z)const{return scene.lower+Vec3(x+.5f,y+.5f,z+.5f)*config.dx;}
    size_t cellIndex(int x,int y,int z)const{return (size_t(z)*ny+y)*nx+x;}
    bool inGrid(int x,int y,int z)const{return x>=0&&y>=0&&z>=0&&x<nx&&y<ny&&z<nz;}
    int cellType(int x,int y,int z)const{return inGrid(x,y,z)?type[cellIndex(x,y,z)]:0;}
    float liquidPhi(Vec3 p)const{return phi.sample((p-scene.lower)/config.dx-Vec3(.5f,.5f,.5f));}
    float solidPhi(Vec3 p)const{return solid.sample((p-scene.lower)/config.dx);}
    Vec3 solidNormal(Vec3 p)const {
        float e=.35f*config.dx;
        return normalized({solidPhi(p+Vec3(e,0,0))-solidPhi(p-Vec3(e,0,0)),solidPhi(p+Vec3(0,e,0))-solidPhi(p-Vec3(0,e,0)),solidPhi(p+Vec3(0,0,e))-solidPhi(p-Vec3(0,0,e))});
    }
    Vec3 liquidNormal(Vec3 p)const {
        float e=.5f*config.dx;
        return normalized({liquidPhi(p+Vec3(e,0,0))-liquidPhi(p-Vec3(e,0,0)),liquidPhi(p+Vec3(0,e,0))-liquidPhi(p-Vec3(0,e,0)),liquidPhi(p+Vec3(0,0,e))-liquidPhi(p-Vec3(0,0,e))});
    }
    Vec3 velocity(Vec3 p,bool previous=false)const {
        Vec3 q=(p-scene.lower)/config.dx,out;
        for(int a=0;a<3;a++)out[a]=(previous?grid[a].old:grid[a].value).sample(q-grid[a].offset);
        return out;
    }
    static double seconds(std::chrono::steady_clock::time_point a){return std::chrono::duration<double>(std::chrono::steady_clock::now()-a).count();}
    float inletPlane(const Inlet &in)const {
        float p=in.direction>0?in.lower[in.axis]:in.upper[in.axis];
        return scene.lower[in.axis]+std::round((p-scene.lower[in.axis])/config.dx)*config.dx;
    }
    bool isInletFace(Vec3 p,int axis,const Inlet &in)const {
        if(axis!=in.axis||std::abs(p[axis]-inletPlane(in))>.1f*config.dx)return false;
        int b=(axis+1)%3,c=(axis+2)%3;
        return p[b]>=in.lower[b]&&p[b]<=in.upper[b]&&p[c]>=in.lower[c]&&p[c]<=in.upper[c];
    }
    uint64_t configurationSignature()const {
        uint64_t signature=1469598103934665603ULL;
        auto bytes=[&](const void *p,size_t size){const auto *b=static_cast<const uint8_t *>(p);for(size_t i=0;i<size;i++){signature^=b[i];signature*=1099511628211ULL;}};
        auto add=[&](const auto &v){bytes(&v,sizeof(v));};
        add(config.dx);add(config.cfl);add(config.flipRatio);add(config.density);add(config.pressureTolerance);add(config.pressureAbsoluteTolerance);add(config.gravity);
        add(config.threads);add(config.pressureIterations);add(config.extrapolationLayers);add(config.particlesPerAxis);add(config.reseedInterval);add(config.minParticlesPerCell);add(config.maxParticlesPerCell);
        add(config.apic);add(config.whitewater);add(config.refineSolidContact);add(config.whitewaterRate);add(config.whitewaterLength);add(config.maxParticles);add(config.maxWhitewater);add(config.seed);add(config.buildSignature);
        add(scene.lower);add(scene.upper);add(scene.closedBoundaries);bytes(solid.a.data(),solid.a.size()*sizeof(float));
        for(const auto &in:scene.inlets){add(in.lower);add(in.upper);add(in.velocity);add(in.axis);add(in.direction);add(in.rateScale);}
        return signature;
    }

    void cacheSolids() {
        #pragma omp parallel for schedule(static)
        for(int z=0;z<=nz;z++)for(int y=0;y<=ny;y++)for(int x=0;x<=nx;x++)
            solid(x,y,z)=scene.solidSdf(scene.lower+Vec3(float(x),float(y),float(z))*config.dx);
        for(int a=0;a<3;a++){
            auto &f=grid[a];int b=(a+1)%3,c=(a+2)%3;
            #pragma omp parallel for schedule(static)
            for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
                Vec3 p=scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*config.dx;
                int open=0;
                for(int j=0;j<4;j++)for(int i=0;i<4;i++){
                    Vec3 q=p;q[b]+=(float(i)*.25f-.375f)*config.dx;q[c]+=(float(j)*.25f-.375f)*config.dx;
                    open+=solidPhi(q)>=0;
                }
                int L[3]={x,y,z},R[3]={x,y,z};L[a]--;
                bool lSolid=inGrid(L[0],L[1],L[2])&&solidPhi(cellPosition(L[0],L[1],L[2]))<0;
                bool rSolid=inGrid(R[0],R[1],R[2])&&solidPhi(cellPosition(R[0],R[1],R[2]))<0;
                size_t q=f.value.index(x,y,z);
                f.open.a[q]=(lSolid||rSolid)?0:float(open)/16;
                int normal=a==0?x:(a==1?y:z),extent=a==0?nx:(a==1?ny:nz);
                if((normal==0&&scene.closedBoundaries[2*a])||(normal==extent&&scene.closedBoundaries[2*a+1])){
                    f.open.a[q]=0;f.fixed[q]=1;f.value.a[q]=0;
                }
                for(const auto &in:scene.inlets){
                    if(isInletFace(p,a,in)&&f.open.a[q]>0){
                        f.fixed[q]=1;f.value.a[q]=in.velocity[a];break;
                    }
                }
            }
        }
        for(size_t e=0;e<scene.inlets.size();e++){
            const auto &in=scene.inlets[e];int a=in.axis;auto &f=grid[a];double flux=0;
            #pragma omp parallel for schedule(static) reduction(+:flux)
            for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
                Vec3 p=scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*config.dx;
                size_t n=f.value.index(x,y,z);
                if(f.fixed[n]&&isInletFace(p,a,in))flux+=double(f.open.a[n])*std::max(0.0f,in.direction*in.velocity[a])*config.dx*config.dx*in.rateScale;
            }
            inletFlux[e]=flux;
        }
    }

    void initialize() {
        particles.clear();white.clear();stats=Statistics{};time=lastDt=0;nextParticleId=nextWhiteId=1;
        int n=config.particlesPerAxis;float h=config.dx,spacing=h/n,volume=spacing*spacing*spacing;
        for(int z=0;z<nz;z++)for(int y=0;y<ny;y++)for(int x=0;x<nx;x++)for(int k=0;k<n;k++)for(int j=0;j<n;j++)for(int i=0;i<n;i++){
            Vec3 p=scene.lower+Vec3(x+(i+.5f)/n,y+(j+.5f)/n,z+(k+.5f)/n)*h;
            p+=randomVector(config.seed+uint64_t(((z*ny+y)*nx+x)*27+(k*n+j)*n+i)*5)*(.12f*spacing);
            if(scene.initialLiquidSdf(p)>=0||solidPhi(p)<.04f*h)continue;
            if(config.refineSolidContact&&solidPhi(p)<.85f*h&&scene.solidSdf(p)<.04f*h)continue;
            Particle q;q.p=p;q.v=scene.initialVelocity?scene.initialVelocity(p):Vec3{};q.volume=volume;q.id=nextParticleId++;
            particles.push_back(q);
        }
        if(particles.empty())throw std::runtime_error("Scene seeded no liquid particles");
        if(particles.size()>config.maxParticles)throw std::runtime_error("Initial particle count exceeds configured capacity");
        stats.initialParticles=particles.size();stats.initialVolume=particleVolume();
        std::fill(inletDebt.begin(),inletDebt.end(),0);
        transferToGrid();updateStatistics();
    }
    double particleVolume()const{double v=0;for(const auto &p:particles)v+=p.volume;return v;}

    template<class F> void stencil(Vec3 p,int axis,bool quadratic,F callback)const {
        Vec3 q=(p-scene.lower)/config.dx-grid[axis].offset;
        auto &g=grid[axis].value;
        if(quadratic){
            int b[3];float w[3][3];
            for(int d=0;d<3;d++){
                b[d]=int(std::floor(q[d]-.5f));float f=q[d]-b[d];
                w[d][0]=.5f*(1.5f-f)*(1.5f-f);w[d][1]=.75f-(f-1)*(f-1);w[d][2]=.5f*(f-.5f)*(f-.5f);
            }
            for(int k=0;k<3;k++)for(int j=0;j<3;j++)for(int i=0;i<3;i++){
                int x=b[0]+i,y=b[1]+j,z=b[2]+k;
                if(x<0||y<0||z<0||x>=g.nx||y>=g.ny||z>=g.nz)continue;
                Vec3 delta=(Vec3(float(x),float(y),float(z))-q)*config.dx;
                callback(g.index(x,y,z),w[0][i]*w[1][j]*w[2][k],delta);
            }
        }else{
            int b[3]={int(std::floor(q.x)),int(std::floor(q.y)),int(std::floor(q.z))};
            Vec3 f=q-Vec3(float(b[0]),float(b[1]),float(b[2]));
            for(int k=0;k<2;k++)for(int j=0;j<2;j++)for(int i=0;i<2;i++){
                int x=b[0]+i,y=b[1]+j,z=b[2]+k;
                if(x<0||y<0||z<0||x>=g.nx||y>=g.ny||z>=g.nz)continue;
                callback(g.index(x,y,z),(i?f.x:1-f.x)*(j?f.y:1-f.y)*(k?f.z:1-f.z),(Vec3(float(x),float(y),float(z))-q)*config.dx);
            }
        }
    }

    void transferToGrid() {
        float h=config.dx,nominal=std::pow(h/config.particlesPerAxis,3),radius=.84f*h;
        #pragma omp parallel
        {
            int tid=0;
#ifdef _OPENMP
            tid=omp_get_thread_num();
#endif
            auto &sv=scatterVelocity[tid],&sw=scatterWeight[tid],&sp=scatterPhi[tid];
            std::fill(sv.begin(),sv.end(),0);std::fill(sw.begin(),sw.end(),0);std::fill(sp.begin(),sp.end(),4*h);
            #pragma omp for schedule(static)
            for(size_t k=0;k<particles.size();k++){
                const auto &p=particles[k];float mass=p.volume/nominal;
                for(int a=0;a<3;a++)stencil(p.p,a,config.apic,[&](size_t n,float w,Vec3 d){
                    float vel=p.v[a];if(config.apic)vel+=p.C[a*3]*d.x+p.C[a*3+1]*d.y+p.C[a*3+2]*d.z;
                    sv[faceOffset[a]+n]+=mass*w*vel;sw[faceOffset[a]+n]+=mass*w;
                });
                Vec3 q=(p.p-scene.lower)/h-Vec3(.5f,.5f,.5f);
                int bx=int(std::round(q.x)),by=int(std::round(q.y)),bz=int(std::round(q.z));
                for(int z=std::max(0,bz-1);z<=std::min(nz-1,bz+1);z++)for(int y=std::max(0,by-1);y<=std::min(ny-1,by+1);y++)for(int x=std::max(0,bx-1);x<=std::min(nx-1,bx+1);x++){
                    float d=length(cellPosition(x,y,z)-p.p)-radius;size_t n=cellIndex(x,y,z);sp[n]=std::min(sp[n],d);
                }
            }
        }
        for(int a=0;a<3;a++){
            auto &f=grid[a];
            #pragma omp parallel for schedule(static)
            for(size_t n=0;n<f.value.a.size();n++){
                float v=0,w=0;
                for(int t=0;t<config.threads;t++){v+=scatterVelocity[t][faceOffset[a]+n];w+=scatterWeight[t][faceOffset[a]+n];}
                f.value.a[n]=w>1e-9f?v/w:0;f.weight.a[n]=w;f.valid[n]=w>1e-9f;
                if(f.open.a[n]==0){f.value.a[n]=0;f.valid[n]=1;}
            }
        }
        active.clear();
        #pragma omp parallel for schedule(static)
        for(int z=0;z<nz;z++)for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
            size_t n=cellIndex(x,y,z);float p=4*h;
            for(int t=0;t<config.threads;t++)p=std::min(p,scatterPhi[t][n]);
            bool s=solidPhi(cellPosition(x,y,z))<0;
            type[n]=s?2:(p<0?1:0);phi.a[n]=s?4*h:p;
        }
        for(size_t n=0;n<cells;n++)if(type[n]==1)active.push_back(int(n));
        // Build a narrow signed-distance interior for secondary classification.
        // Air-interface fractions retain the reconstructed zero crossing.
        #pragma omp parallel for schedule(static)
        for(int z=0;z<nz;z++)for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
            size_t n=cellIndex(x,y,z);surface[n]=0;
            if(type[n]!=1)continue;
            bool boundary=cellType(x-1,y,z)==0||cellType(x+1,y,z)==0||cellType(x,y-1,z)==0||cellType(x,y+1,z)==0||cellType(x,y,z-1)==0||cellType(x,y,z+1)==0;
            surface[n]=boundary;
            if(!boundary)phi.a[n]=-4*h;
        }
        for(int iter=0;iter<3;iter++){
            workT=phi.a;
            #pragma omp parallel for schedule(static)
            for(int z=0;z<nz;z++)for(int y=0;y<ny;y++)for(int x=0;x<nx;x++){
                size_t n=cellIndex(x,y,z);if(type[n]!=1||surface[n])continue;
                float d=-workT[n];
                for(int a=0;a<3;a++)for(int sign:{-1,1}){
                    int q[3]={x,y,z};q[a]+=sign;
                    if(inGrid(q[0],q[1],q[2])&&cellType(q[0],q[1],q[2])==1)d=std::min(d,-workT[cellIndex(q[0],q[1],q[2])]+h);
                }
                phi.a[n]=-d;
            }
        }
        prescribeInlets();
        for(auto &f:grid){extrapolate(f,config.extrapolationLayers);f.old.a=f.value.a;}
        stats.fluidCells=int(active.size());
    }

    void prescribeInlets() {
        for(int a=0;a<3;a++){
            auto &f=grid[a];
            #pragma omp parallel for schedule(static)
            for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
                size_t q=f.value.index(x,y,z);if(!f.fixed[q])continue;
                Vec3 p=scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*config.dx;
                for(const auto &in:scene.inlets)if(isInletFace(p,a,in)){f.value.a[q]=in.velocity[a];f.valid[q]=1;break;}
            }
        }
    }
    void extrapolate(FaceGrid &f,int layers) {
        std::vector<float> temp=f.value.a;
        std::vector<uint8_t> valid=f.valid;
        auto &g=f.value;
        for(int it=0;it<layers;it++){
            temp=g.a;valid=f.valid;
            #pragma omp parallel for schedule(static)
            for(int z=0;z<g.nz;z++)for(int y=0;y<g.ny;y++)for(int x=0;x<g.nx;x++){
                size_t n=g.index(x,y,z);if(f.valid[n])continue;
                float sum=0;int count=0;
                for(int a=0;a<3;a++)for(int sign:{-1,1}){
                    int q[3]={x,y,z};q[a]+=sign;
                    if(q[0]<0||q[1]<0||q[2]<0||q[0]>=g.nx||q[1]>=g.ny||q[2]>=g.nz)continue;
                    size_t m=g.index(q[0],q[1],q[2]);if(f.valid[m]){sum+=g.a[m];count++;}
                }
                if(count){temp[n]=sum/count;valid[n]=1;}
            }
            g.a.swap(temp);f.valid.swap(valid);
        }
    }

    void addForces(float dt) {
        for(int a=0;a<3;a++){
            auto &f=grid[a];
            #pragma omp parallel for schedule(static)
            for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
                size_t n=f.value.index(x,y,z);if(!f.valid[n]||f.fixed[n]||f.open.a[n]<=0)continue;
                Vec3 p=scene.lower+(Vec3(float(x),float(y),float(z))+f.offset)*config.dx;
                float accel=config.gravity[a];if(scene.bodyAcceleration)accel+=scene.bodyAcceleration(p,time)[a];
                float change=dt*accel;
                if(scene.velocityGuide) {
                    const VelocityGuide q=scene.velocityGuide(p,time);
                    // Exponential feedback: a requested SPEED is not repeatedly
                    // added as acceleration. Stable under changes of substep size.
                    const float alpha=-std::expm1(-std::max(0.f,q.rate)*dt);
                    float drive=(q.target[a]-f.value.a[n])*alpha+dt*q.acceleration[a];
                    drive=clamp(drive,-q.accelerationLimit*dt,q.accelerationLimit*dt);
                    change+=drive;
                }
                f.value.a[n]+=change;
            }
        }
    }
    float interfaceTheta(int x,int y,int z,int qx,int qy,int qz)const {
        if(!inGrid(qx,qy,qz))return 1;
        float a=phi(x,y,z),b=phi(qx,qy,qz);
        return clamp(std::abs(a)/(std::abs(a)+std::max(0.0f,b)+1e-20f),.05f,1.0f);
    }
    float divergence(int x,int y,int z)const {
        return grid[0].open(x+1,y,z)*grid[0].value(x+1,y,z)-grid[0].open(x,y,z)*grid[0].value(x,y,z)
             + grid[1].open(x,y+1,z)*grid[1].value(x,y+1,z)-grid[1].open(x,y,z)*grid[1].value(x,y,z)
             + grid[2].open(x,y,z+1)*grid[2].value(x,y,z+1)-grid[2].open(x,y,z)*grid[2].value(x,y,z);
    }

    void buildPressureSystem(float dt) {
        std::fill(diag.begin(),diag.end(),0);std::fill(cx.begin(),cx.end(),0);std::fill(cy.begin(),cy.end(),0);std::fill(cz.begin(),cz.end(),0);std::fill(rhs.begin(),rhs.end(),0);
        float scale=lastDt>0?float(dt/lastDt):1;
        #pragma omp parallel for schedule(static)
        for(size_t k=0;k<cells;k++)pressure[k]=type[k]==1?pressure[k]*scale:0;
        double divergence2=0;
        #pragma omp parallel for schedule(static) reduction(+:divergence2)
        for(size_t ai=0;ai<active.size();ai++){
            int n=active[ai],x=n%nx,y=(n/nx)%ny,z=n/(nx*ny);float d=0;
            rhs[n]=-divergence(x,y,z);divergence2+=double(rhs[n])*rhs[n];
            for(int a=0;a<3;a++)for(int sign:{-1,1}){
                int q[3]={x,y,z},face[3]={x,y,z};q[a]+=sign;if(sign>0)face[a]++;
                auto &f=grid[a];size_t fn=f.value.index(face[0],face[1],face[2]);
                float w=f.open.a[fn];if(f.fixed[fn]||w==0)continue;
                int t=cellType(q[0],q[1],q[2]);
                if(t==1){
                    d+=w;
                    if(sign>0)(a==0?cx:(a==1?cy:cz))[n]=-w;
                }else if(t==0)d+=w/interfaceTheta(x,y,z,q[0],q[1],q[2]);
            }
            if(d<1e-9f){diag[n]=1;rhs[n]=0;pressure[n]=0;}else diag[n]=d;
        }
        stats.divergenceBeforeL2=std::sqrt(divergence2/std::max<size_t>(1,active.size()))/config.dx;
        // Incomplete Cholesky factor of the seven-point SPD operator.
        std::fill(precon.begin(),precon.end(),0);
        for(int n:active){
            double e=diag[n];
            if(n>=1){double v=cx[n-1]*precon[n-1];e-=v*v;}
            if(n>=nx){double v=cy[n-nx]*precon[n-nx];e-=v*v;}
            if(n>=nx*ny){double v=cz[n-nx*ny]*precon[n-nx*ny];e-=v*v;}
            if(e<.25*diag[n])e=diag[n];
            precon[n]=float(1/std::sqrt(e));
        }
    }
    void multiply(const std::vector<float> &x,std::vector<float> &out)const {
        #pragma omp parallel for schedule(static)
        for(size_t ai=0;ai<active.size();ai++){
            int n=active[ai];float v=diag[n]*x[n];
            if(n>=1)v+=cx[n-1]*x[n-1];if(size_t(n)+1<cells)v+=cx[n]*x[n+1];
            if(n>=nx)v+=cy[n-nx]*x[n-nx];if(size_t(n)+nx<cells)v+=cy[n]*x[n+nx];
            if(n>=nx*ny)v+=cz[n-nx*ny]*x[n-nx*ny];if(size_t(n)+nx*ny<cells)v+=cz[n]*x[n+nx*ny];
            out[n]=v;
        }
    }
    void applyPreconditioner(const std::vector<float> &r,std::vector<float> &z) {
        std::fill(workT.begin(),workT.end(),0);std::fill(z.begin(),z.end(),0);
        for(int n:active){
            float v=r[n];
            if(n>=1)v-=cx[n-1]*precon[n-1]*workT[n-1];
            if(n>=nx)v-=cy[n-nx]*precon[n-nx]*workT[n-nx];
            if(n>=nx*ny)v-=cz[n-nx*ny]*precon[n-nx*ny]*workT[n-nx*ny];
            workT[n]=v*precon[n];
        }
        for(auto it=active.rbegin();it!=active.rend();++it){
            int n=*it;float v=workT[n];
            if(size_t(n)+1<cells)v-=cx[n]*precon[n]*z[n+1];
            if(size_t(n)+nx<cells)v-=cy[n]*precon[n]*z[n+nx];
            if(size_t(n)+nx*ny<cells)v-=cz[n]*precon[n]*z[n+nx*ny];
            z[n]=v*precon[n];
        }
    }
    double inner(const std::vector<float> &a,const std::vector<float> &b)const {
        double r=0;
        #pragma omp parallel for schedule(static) reduction(+:r)
        for(size_t i=0;i<active.size();i++){int n=active[i];r+=double(a[n])*b[n];}
        return r;
    }
    void pressureSolve(float dt) {
        buildPressureSystem(dt);multiply(pressure,workQ);
        #pragma omp parallel for schedule(static)
        for(size_t ai=0;ai<active.size();ai++){int n=active[ai];workR[n]=rhs[n]-workQ[n];}
        double b2=inner(rhs,rhs),r2=inner(workR,workR);
        double tolerance=std::max(double(config.pressureAbsoluteTolerance)*config.pressureAbsoluteTolerance*active.size(),double(config.pressureTolerance)*config.pressureTolerance*b2);
        applyPreconditioner(workR,workZ);workS=workZ;double rz=inner(workR,workZ);int iterations=0;
        while(r2>tolerance&&iterations<config.pressureIterations){
            multiply(workS,workQ);double denom=inner(workS,workQ);
            if(!(denom>0)||!std::isfinite(denom))throw std::runtime_error("Pressure operator lost positive definiteness");
            float alpha=float(rz/denom);
            #pragma omp parallel for schedule(static)
            for(size_t ai=0;ai<active.size();ai++){int n=active[ai];pressure[n]+=alpha*workS[n];workR[n]-=alpha*workQ[n];}
            r2=inner(workR,workR);iterations++;
            if(r2<=tolerance)break;
            applyPreconditioner(workR,workZ);double nrz=inner(workR,workZ);float beta=float(nrz/std::max(1e-40,rz));rz=nrz;
            #pragma omp parallel for schedule(static)
            for(size_t ai=0;ai<active.size();ai++){int n=active[ai];workS[n]=workZ[n]+beta*workS[n];}
        }
        // Recompute the actual residual; recurrence residuals alone can hide roundoff.
        multiply(pressure,workQ);
        double actual=0,maxResidual=0;
        #pragma omp parallel for schedule(static) reduction(+:actual) reduction(max:maxResidual)
        for(size_t ai=0;ai<active.size();ai++){int n=active[ai];double r=rhs[n]-workQ[n];actual+=r*r;maxResidual=std::max(maxResidual,std::abs(r));}
        stats.pressureIterations=iterations;stats.pressureRelativeResidual=std::sqrt(actual/std::max(1e-30,b2));stats.pressureAbsoluteResidual=maxResidual;
        if(!std::isfinite(actual)||stats.pressureRelativeResidual>.035)throw std::runtime_error("Pressure solve failed residual gate");
        projectVelocity();
        double post2=0,postMax=0;
        #pragma omp parallel for schedule(static) reduction(+:post2) reduction(max:postMax)
        for(size_t ai=0;ai<active.size();ai++){
            int n=active[ai],x=n%nx,y=(n/nx)%ny,z=n/(nx*ny);double d=divergence(x,y,z)/config.dx;
            post2+=d*d;postMax=std::max(postMax,std::abs(d));
        }
        stats.divergenceAfterL2=std::sqrt(post2/std::max<size_t>(1,active.size()));stats.divergenceAfterMax=postMax;
        for(auto &f:grid)extrapolate(f,config.extrapolationLayers);
    }
    void projectVelocity() {
        for(int a=0;a<3;a++){
            auto &f=grid[a];
            #pragma omp parallel for schedule(static)
            for(int z=0;z<f.value.nz;z++)for(int y=0;y<f.value.ny;y++)for(int x=0;x<f.value.nx;x++){
                size_t n=f.value.index(x,y,z);if(f.fixed[n]){f.valid[n]=1;continue;}
                int L[3]={x,y,z},R[3]={x,y,z};L[a]--;
                int lt=cellType(L[0],L[1],L[2]),rt=cellType(R[0],R[1],R[2]);
                if(f.open.a[n]==0||lt==2||rt==2){f.value.a[n]=0;f.valid[n]=1;continue;}
                if(lt!=1&&rt!=1){f.valid[n]=0;continue;}
                float lp=lt==1?pressure[cellIndex(L[0],L[1],L[2])]:0,rp=rt==1?pressure[cellIndex(R[0],R[1],R[2])]:0;
                float theta=1;
                if(lt==1&&rt==0)theta=interfaceTheta(L[0],L[1],L[2],R[0],R[1],R[2]);
                if(rt==1&&lt==0)theta=interfaceTheta(R[0],R[1],R[2],L[0],L[1],L[2]);
                f.value.a[n]-=(rp-lp)/theta;f.valid[n]=1;
            }
        }
    }

    bool outside(Vec3 p)const{return p.x<scene.lower.x||p.y<scene.lower.y||p.z<scene.lower.z||p.x>=scene.upper.x||p.y>=scene.upper.y||p.z>=scene.upper.z;}
    int projectDomain(Vec3 &p,Vec3 &v,float gap)const {
        int count=0;
        for(int a=0;a<3;a++){
            if(scene.closedBoundaries[2*a]&&p[a]<scene.lower[a]+gap){p[a]=scene.lower[a]+gap;if(v[a]<0)v[a]=0;count++;}
            if(scene.closedBoundaries[2*a+1]&&p[a]>scene.upper[a]-gap){p[a]=scene.upper[a]-gap;if(v[a]>0)v[a]=0;count++;}
        }
        return count;
    }
    int projectSolid(Vec3 &p,Vec3 &v,float gap)const {
        int count=projectDomain(p,v,gap);
        for(int i=0;i<5;i++){
            float d=solidPhi(p);if(d>=gap)break;
            Vec3 n=solidNormal(p);if(dot(n,n)<.5f)break;
            p+=n*(gap-d+1e-6f*config.dx);
            float vn=dot(v,n);if(vn<0)v-=n*vn;
            count++;
        }
        // Contact is refined against the same continuous field exported as rock
        // geometry. The grid cache remains the inexpensive broad phase.
        if(config.refineSolidContact&&solidPhi(p)<.85f*config.dx){
            float e=.06f*config.dx;
            for(int i=0;i<5;i++){
                float d=scene.solidSdf(p);if(d>=gap)break;
                Vec3 g{scene.solidSdf(p+Vec3(e,0,0))-scene.solidSdf(p-Vec3(e,0,0)),scene.solidSdf(p+Vec3(0,e,0))-scene.solidSdf(p-Vec3(0,e,0)),scene.solidSdf(p+Vec3(0,0,e))-scene.solidSdf(p-Vec3(0,0,e))};g*=1/(2*e);
                float magnitude2=dot(g,g);if(magnitude2<1e-6f)break;
                Vec3 n=g/std::sqrt(magnitude2);p+=n*clamp((gap-d)/std::sqrt(magnitude2)+1e-6f*config.dx,0,.65f*config.dx);
                float vn=dot(v,n);if(vn<0)v-=n*vn;count++;
            }
        }
        return count+projectDomain(p,v,gap);
    }
    void gatherAffine(Vec3 p,Vec3 &v,std::array<float,9> &C)const {
        // Weighted affine fit. Interior quadratic stencils have zero first
        // moment and variance dx^2/4. At open domain boundaries the truncated
        // tensor-product stencil needs its actual mean and variance; applying
        // the interior formula there spuriously differentiates constant flow.
        v={};C.fill(0);
        for(int a=0;a<3;a++){
            double W=0,U=0,D[3]={},DD[3]={},UD[3]={};
            stencil(p,a,true,[&](size_t n,float weight,Vec3 d){
                double w=weight,u=grid[a].value.a[n];W+=w;U+=w*u;
                for(int b=0;b<3;b++){D[b]+=w*d[b];DD[b]+=w*d[b]*d[b];UD[b]+=w*u*d[b];}
            });
            if(W<1e-12)continue;
            double mean=U/W,intercept=mean;
            for(int b=0;b<3;b++){
                double offset=D[b]/W,variance=DD[b]/W-offset*offset;
                double slope=variance>1e-10*config.dx*config.dx?(UD[b]/W-mean*offset)/variance:0;
                C[a*3+b]=float(slope);intercept-=slope*offset;
            }
            v[a]=float(intercept);
        }
    }
    void moveParticles(float dt) {
        uint64_t projected=0;int bad=0;
        #pragma omp parallel for schedule(static) reduction(+:projected,bad)
        for(size_t k=0;k<particles.size();k++){
            auto &p=particles[k];Vec3 pic,delta;
            if(config.apic){
                gatherAffine(p.p,pic,p.C);
                p.v=pic;
            }else{
                pic=velocity(p.p);delta=pic-velocity(p.p,true);
                p.v=(p.v+delta)*config.flipRatio+pic*(1-config.flipRatio);
            }
            Vec3 mid=p.p+pic*(.5f*dt);
            p.p+=velocity(mid)*dt;
            projected+=projectSolid(p.p,p.v,.04f*config.dx);
            if(!finite(p.p)||!finite(p.v))bad++;
        }
        if(bad)throw std::runtime_error("Non-finite particle state");
        stats.collisionProjections+=projected;
        size_t dst=0;
        for(size_t k=0;k<particles.size();k++){
            if(outside(particles[k].p)){stats.outflowParticles++;stats.outflowVolume+=particles[k].volume;continue;}
            if(dst!=k)particles[dst]=particles[k];dst++;
        }
        particles.resize(dst);
    }

    void emit(float dt) {
        double nominal=std::pow(double(config.dx)/config.particlesPerAxis,3);
        for(size_t e=0;e<scene.inlets.size();e++){
            const auto &in=scene.inlets[e];int a=in.axis,b=(a+1)%3,c=(a+2)%3;
            double area=(in.upper[b]-in.lower[b])*(in.upper[c]-in.lower[c]);
            double rectangularFlux=area*std::max(0.0f,in.direction*in.velocity[a])*in.rateScale;
            double volume=inletFlux[e]*dt;
            stats.requestedInletVolume+=rectangularFlux*dt;stats.prescribedInletVolume+=volume;
            inletDebt[e]+=volume/nominal;
            uint64_t count=uint64_t(std::floor(inletDebt[e]));inletDebt[e]-=count;
            if(particles.size()+count>config.maxParticles)throw std::runtime_error("Particle capacity exceeded; no silent liquid culling is allowed");
            uint64_t accepted=0,attempts=0;
            uint64_t budget=1024+uint64_t(std::ceil(8*count*rectangularFlux/std::max(1e-30,inletFlux[e])));
            while(accepted<count){
                if(++attempts>budget)throw std::runtime_error("Masked inlet could not realize its prescribed open-grid flux");
                uint64_t key=config.seed+(++stats.inletAttempts)*13;Particle p;
                p.p=in.lower;p.p[b]+=(in.upper[b]-in.lower[b])*random01(key);p.p[c]+=(in.upper[c]-in.lower[c])*random01(key+1);
                float depth=std::max(.15f*config.dx,std::abs(in.velocity[a])*dt);
                p.p[a]=inletPlane(in)+in.direction*(.06f*config.dx+depth*random01(key+2));
                if(solidPhi(p.p)<.04f*config.dx||outside(p.p)||(config.refineSolidContact&&scene.solidSdf(p.p)<.04f*config.dx)){stats.inletRejectedAttempts++;continue;}
                p.v=in.velocity;p.volume=float(nominal);p.id=nextParticleId++;
                particles.push_back(p);stats.emittedParticles++;stats.emittedVolume+=p.volume;accepted++;
            }
        }
    }

    void reseed() {
        if(particles.empty())return;
        std::vector<int> counts(cells),starts(cells+1),order(particles.size());
        for(const auto &p:particles){Vec3 q=(p.p-scene.lower)/config.dx;counts[cellIndex(std::clamp(int(q.x),0,nx-1),std::clamp(int(q.y),0,ny-1),std::clamp(int(q.z),0,nz-1))]++;}
        for(size_t i=0;i<cells;i++)starts[i+1]=starts[i]+counts[i];
        auto cursor=starts;
        for(size_t i=0;i<particles.size();i++){Vec3 q=(particles[i].p-scene.lower)/config.dx;size_t c=cellIndex(std::clamp(int(q.x),0,nx-1),std::clamp(int(q.y),0,ny-1),std::clamp(int(q.z),0,nz-1));order[cursor[c]++]=int(i);}
        std::vector<Particle> result;result.reserve(std::min(config.maxParticles,particles.size()+particles.size()/8));
        for(size_t c=0;c<cells;c++){
            int count=counts[c];if(count==0)continue;
            size_t begin=result.size();int keep=std::min(count,config.maxParticlesPerCell);
            for(int j=0;j<keep;j++)result.push_back(particles[order[starts[c]+j]]);
            for(int j=keep;j<count;j++){
                auto &a=result[begin+size_t(j-keep)%keep];const auto &b=particles[order[starts[c]+j]];
                float v=a.volume+b.volume,w=b.volume/v;Vec3 candidate=a.p*(1-w)+b.p*w;
                if(solidPhi(candidate)<.04f*config.dx||(config.refineSolidContact&&solidPhi(candidate)<.85f*config.dx&&scene.solidSdf(candidate)<.04f*config.dx)){result.push_back(b);continue;}
                a.p=candidate;a.v=a.v*(1-w)+b.v*w;
                for(int k=0;k<9;k++)a.C[k]=a.C[k]*(1-w)+b.C[k]*w;
                a.volume=v;stats.mergedParticles++;
            }
            if(keep<config.minParticlesPerCell&&result.size()<config.maxParticles){
                int need=config.minParticlesPerCell-keep;
                for(int j=0;j<need&&result.size()<config.maxParticles;j++){
                    size_t pick=begin+size_t(j)%keep;Particle b=result[pick];
                    Vec3 offset=randomVector(b.id+stats.steps*1231+j*7)*(.24f*config.dx);
                    if(solidPhi(b.p+offset)<.04f*config.dx||solidPhi(b.p-offset)<.04f*config.dx||outside(b.p+offset)||outside(b.p-offset))continue;
                    if(config.refineSolidContact&&(scene.solidSdf(b.p+offset)<.04f*config.dx||scene.solidSdf(b.p-offset)<.04f*config.dx))continue;
                    result[pick].p-=offset;result[pick].volume*=.5f;b.p+=offset;b.volume=result[pick].volume;b.id=nextParticleId++;
                    if(config.apic){
                        Vec3 dv;
                        for(int a=0;a<3;a++)dv[a]=b.C[3*a]*offset.x+b.C[3*a+1]*offset.y+b.C[3*a+2]*offset.z;
                        result[pick].v-=dv;b.v+=dv;
                    }
                    result.push_back(b);stats.splitParticles++;
                }
            }
        }
        particles.swap(result);
    }

    void updateWhitewater(float dt) {
        if(!config.whitewater){white.clear();return;}
        float h=config.dx;
        #pragma omp parallel for schedule(static)
        for(size_t i=0;i<white.size();i++){
            auto &w=white[i];float p=liquidPhi(w.p);Vec3 carrier=velocity(w.p);
            if(p>.35f*h)w.type=Spray;else if(p<-.7f*h)w.type=Bubble;else w.type=Foam;
            if(w.type==Spray){w.v+=config.gravity*dt;w.v*=std::exp(-.18f*dt);w.p+=w.v*dt;}
            else if(w.type==Bubble){
                float rise=clamp(std::sqrt(2*9.81f*w.radius/.47f),.08f,.45f);
                Vec3 target=carrier+Vec3(0,rise,0);w.v+=(target-w.v)*(1-std::exp(-8*dt));w.p+=w.v*dt;
            }else{
                w.v+=(carrier-w.v)*(1-std::exp(-18*dt));w.p+=w.v*dt;
                float d=liquidPhi(w.p);if(std::abs(d)<1.5f*h)w.p-=liquidNormal(w.p)*clamp(d+.05f*h,-.3f*h,.3f*h);
            }
            projectSolid(w.p,w.v,std::min(w.radius,.03f*h));w.age+=dt;
        }
        size_t dst=0;
        for(size_t i=0;i<white.size();i++){
            if(white[i].age>=white[i].lifetime||outside(white[i].p)||!finite(white[i].p)||!finite(white[i].v)){stats.whiteExpired++;continue;}
            if(dst!=i)white[dst]=white[i];dst++;
        }
        white.resize(dst);
        for(int cell:active){
            if(!surface[cell])continue;
            int x=cell%nx,y=(cell/nx)%ny,z=cell/(nx*ny);Vec3 p=cellPosition(x,y,z);
            if(solidPhi(p)<.3f*h)continue;
            Vec3 v=velocity(p),normal=liquidNormal(p);float speed=length(v);
            float energy=clamp((speed-.6f)/3.6f,0,1);if(energy<=0)continue;
            Vec3 gx=(velocity(p+Vec3(h,0,0))-velocity(p-Vec3(h,0,0)))/(2*h);
            Vec3 gy=(velocity(p+Vec3(0,h,0))-velocity(p-Vec3(0,h,0)))/(2*h);
            Vec3 gz=(velocity(p+Vec3(0,0,h))-velocity(p-Vec3(0,0,h)))/(2*h);
            Vec3 curl{gy.z-gz.y,gz.x-gx.z,gx.y-gy.x};
            float strain=std::sqrt(dot(gx,gx)+dot(gy,gy)+dot(gz,gz));
            float activation=clamp((.65f*strain+.35f*length(curl))*config.whitewaterLength/(speed+.2f)-.025f,0,1);
            activation*=clamp(.3f+std::max(0.0f,dot(v,normal))/(speed+.1f),.3f,1);
            double expected=config.whitewaterRate*h*h*dt*energy*activation;
            uint64_t key=config.seed+uint64_t(cell)*9137+stats.steps*113513;
            unsigned count=unsigned(expected)+(random01(key)<expected-std::floor(expected)?1:0);
            for(unsigned j=0;j<count;j++){
                if(white.size()>=config.maxWhitewater){stats.whiteRejected+=count-j;break;}
                WhiteParticle w;uint64_t k=key+j*17+nextWhiteId*3;
                w.p=p+randomVector(k)*(.8f*h);
                float d=liquidPhi(w.p);w.p-=normal*clamp(d+.08f*h,-h,h);
                if(solidPhi(w.p)<.02f*h||outside(w.p))continue;
                w.v=velocity(w.p)+randomVector(k+3)*(std::min(1.2f,.18f*speed)*activation);
                // Broad subgrid size distribution. These are visual parcels; no bulk mass is removed.
                w.radius=.00025f*std::pow(14.0f,random01(k+6));
                w.lifetime=1.4f+4.8f*random01(k+7);w.type=Foam;w.id=nextWhiteId++;
                white.push_back(w);stats.whiteEmitted++;
            }
        }
    }

    double stableDt(double remaining)const {
        float vmax=0;
        #pragma omp parallel for schedule(static) reduction(max:vmax)
        for(size_t i=0;i<particles.size();i++)vmax=std::max(vmax,length(particles[i].v));
        for(const auto &in:scene.inlets)vmax=std::max(vmax,length(in.velocity));
        double gravity=length(config.gravity),h=config.dx;
        double dt=config.cfl*h/(vmax+std::sqrt(std::max(0.0,gravity*h))+1e-6);
        if(scene.extraAccelerationBound>0)
            dt=std::min(dt,.55*std::sqrt(h/scene.extraAccelerationBound));
        return std::min(remaining,std::min(.025,dt));
    }
    void step(float dt) {
        if(!(dt>0))throw std::runtime_error("Nonpositive time step");
        auto start=std::chrono::steady_clock::now(),phase=start;
        emit(dt);transferToGrid();stats.transferSeconds=seconds(phase);phase=std::chrono::steady_clock::now();
        if(scene.prepareForces)scene.prepareForces(time);
        addForces(dt);pressureSolve(dt);stats.pressureSeconds=seconds(phase);phase=std::chrono::steady_clock::now();
        moveParticles(dt);if(config.reseedInterval>0&&(stats.steps+1)%config.reseedInterval==0)reseed();
        stats.advectionSeconds=seconds(phase);phase=std::chrono::steady_clock::now();
        updateWhitewater(dt);stats.secondarySeconds=seconds(phase);
        time+=dt;lastDt=dt;stats.steps++;stats.dt=dt;stats.time=time;updateStatistics();stats.stepSeconds=seconds(start);
        if(stats.maxSpeed>60)throw std::runtime_error("Velocity safety gate exceeded 60 m/s; state needs diagnosis");
        if(std::abs(stats.massLedgerError)>std::max(1e-7,stats.initialVolume*3e-6))throw std::runtime_error("Liquid volume ledger failed");
    }
    void advanceTo(double target) {
        while(time<target-1e-8){double remaining=target-time;double d=stableDt(remaining);int n=std::max(1,int(std::ceil(remaining/d)));step(float(remaining/n));}
        time=target;stats.time=time;
    }
    void updateStatistics() {
        double volume=0,energy=0;float vmax=0;
        #pragma omp parallel for schedule(static) reduction(+:volume,energy) reduction(max:vmax)
        for(size_t i=0;i<particles.size();i++){
            const auto &p=particles[i];volume+=p.volume;energy+=.5*config.density*p.volume*dot(p.v,p.v);vmax=std::max(vmax,length(p.v));
        }
        stats.volume=volume;stats.kineticEnergy=energy;stats.maxSpeed=vmax;stats.massLedgerError=volume-(stats.initialVolume+stats.emittedVolume-stats.outflowVolume);
    }

    template<class T> static void writeRaw(std::ostream &f,const T &value){f.write(reinterpret_cast<const char *>(&value),sizeof(T));}
    template<class T> static void readRaw(std::istream &f,T &value){f.read(reinterpret_cast<char *>(&value),sizeof(T));}
    void writeFrame(const std::filesystem::path &folder,uint32_t frame)const {
        std::filesystem::create_directories(folder);char label[64];std::snprintf(label,sizeof(label),"frame_%06u",frame);auto base=folder/label;
        auto temporary=base.string()+".bin.tmp";std::ofstream f(temporary,std::ios::binary);
        const char magic[8]={'C','Y','B','R','W','T','R','1'};f.write(magic,8);
        uint32_t version=1,count=uint32_t(particles.size());float t=float(time),d=config.dx/config.particlesPerAxis,h=2*d;
        writeRaw(f,version);writeRaw(f,count);writeRaw(f,frame);writeRaw(f,t);writeRaw(f,d);writeRaw(f,h);
        std::ofstream ids(base.string()+".ids.tmp",std::ios::binary),volumes(base.string()+".volumes.tmp",std::ios::binary);
        for(const auto &p:particles){
            float foam=0;Vec3 v=velocity(p.p);float ph=liquidPhi(p.p);if(ph>-config.dx)foam=clamp((length(v)-1)/5,0,1);
            float record[8]={p.p.x,p.p.y,p.p.z,p.v.x,p.v.y,p.v.z,1,foam};f.write(reinterpret_cast<char *>(record),sizeof(record));
            if(p.id>std::numeric_limits<uint32_t>::max())throw std::runtime_error("Legacy compatible particle ID width exceeded");
            uint32_t id=uint32_t(p.id);writeRaw(ids,id);writeRaw(volumes,p.volume);
        }
        f.close();ids.close();volumes.close();if(!f||!ids||!volumes)throw std::runtime_error("Failed to write primary frame");
        std::filesystem::rename(temporary,base.string()+".bin");std::filesystem::rename(base.string()+".ids.tmp",base.string()+".ids");std::filesystem::rename(base.string()+".volumes.tmp",base.string()+".volumes");
        std::ofstream w(base.string()+".white.tmp",std::ios::binary);w.write("CYBRWW01",8);uint32_t wc=uint32_t(white.size());float zero=0;
        writeRaw(w,version);writeRaw(w,wc);writeRaw(w,frame);writeRaw(w,t);writeRaw(w,config.dx);writeRaw(w,zero);
        for(const auto &p:white){float record[9]={p.p.x,p.p.y,p.p.z,p.v.x,p.v.y,p.v.z,p.radius,p.age,p.lifetime};w.write(reinterpret_cast<char *>(record),sizeof(record));writeRaw(w,p.type);}
        w.close();if(!w)throw std::runtime_error("Failed to write secondary frame");std::filesystem::rename(base.string()+".white.tmp",base.string()+".white");
    }
    void saveCheckpoint(const std::filesystem::path &path)const {
        std::filesystem::create_directories(path.parent_path());std::ofstream f(path.string()+".tmp",std::ios::binary);f.write("CYBRFLP4",8);
        uint32_t version=4;writeRaw(f,version);writeRaw(f,stateSignature);writeRaw(f,config.dx);writeRaw(f,nx);writeRaw(f,ny);writeRaw(f,nz);writeRaw(f,scene.lower);writeRaw(f,time);writeRaw(f,lastDt);writeRaw(f,nextParticleId);writeRaw(f,nextWhiteId);writeRaw(f,stats);
        auto vectorWrite=[&](const auto &v){uint64_t n=v.size();writeRaw(f,n);if(n)f.write(reinterpret_cast<const char *>(v.data()),n*sizeof(v[0]));};
        vectorWrite(particles);vectorWrite(white);vectorWrite(pressure);vectorWrite(inletDebt);
        f.close();if(!f)throw std::runtime_error("Checkpoint write failed");std::filesystem::rename(path.string()+".tmp",path);
    }
    void loadCheckpoint(const std::filesystem::path &path) {
        std::ifstream f(path,std::ios::binary);char magic[8];f.read(magic,8);uint32_t version;uint64_t signature;float dx;int x,y,z;Vec3 lower;
        readRaw(f,version);readRaw(f,signature);readRaw(f,dx);readRaw(f,x);readRaw(f,y);readRaw(f,z);readRaw(f,lower);
        if(std::memcmp(magic,"CYBRFLP4",8)||version!=4||signature!=stateSignature||dx!=config.dx||x!=nx||y!=ny||z!=nz||length(lower-scene.lower)>1e-6f)throw std::runtime_error("Checkpoint build/configuration/scene mismatch");
        readRaw(f,time);readRaw(f,lastDt);readRaw(f,nextParticleId);readRaw(f,nextWhiteId);readRaw(f,stats);
        auto vectorRead=[&](auto &v,size_t cap){uint64_t n;readRaw(f,n);if(n>cap)throw std::runtime_error("Checkpoint vector length exceeded limit");v.resize(n);if(n)f.read(reinterpret_cast<char *>(v.data()),n*sizeof(v[0]));};
        vectorRead(particles,config.maxParticles);vectorRead(white,config.maxWhitewater);vectorRead(pressure,cells);vectorRead(inletDebt,scene.inlets.size());
        if(!f||pressure.size()!=cells||inletDebt.size()!=scene.inlets.size())throw std::runtime_error("Truncated checkpoint");
        transferToGrid();updateStatistics();
    }
};
} // namespace water
