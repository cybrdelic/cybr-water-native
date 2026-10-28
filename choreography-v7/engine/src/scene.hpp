#pragma once

// CYBR WATER VFX: one natural scene definition shared by liquid collisions and
// the static geometry exporter. Units are metres; y points up; the inflow is +x.
// No reference photographs, scanned geometry, or generated images are used.
#include "fluid.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace water {
namespace hero {

constexpr float pi=3.14159265358979323846f;
inline float clamp(float a,float lo=0.f,float hi=1.f){return std::max(lo,std::min(a,hi));}
inline float mix(float a,float b,float t){return a+(b-a)*t;}
inline float smooth(float a,float b,float x){float t=clamp((x-a)/(b-a));return t*t*(3.f-2.f*t);}
inline uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float random(uint32_t key){return float(hash(key)&0xffffffu)/16777215.f;}
inline float lattice(int x,int y,int z){return random(uint32_t(x)*73856093u^uint32_t(y)*19349663u^uint32_t(z)*83492791u);}
inline float noise(Vec3 p){
    int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    float a=p.x-x,b=p.y-y,c=p.z-z;
    a=a*a*a*(a*(a*6.f-15.f)+10.f);
    b=b*b*b*(b*(b*6.f-15.f)+10.f);
    c=c*c*c*(c*(c*6.f-15.f)+10.f);
    return mix(mix(mix(lattice(x,y,z),lattice(x+1,y,z),a),mix(lattice(x,y+1,z),lattice(x+1,y+1,z),a),b),
               mix(mix(lattice(x,y,z+1),lattice(x+1,y,z+1),a),mix(lattice(x,y+1,z+1),lattice(x+1,y+1,z+1),a),b),c);
}
inline float fbm(Vec3 p){return .58f*noise(p)+.28f*noise(p*2.07f+Vec3(5.3f,8.2f,-2.4f))+.14f*noise(p*4.21f+Vec3(-7.1f,1.5f,9.2f));}

// Two broad changes in slope make an eroded rock chute. Irregular shoulders
// retain the water without exposing a rectangular simulation tank in the shot.
inline float bedHeight(float x,float z){
    float center=.055f*std::sin(1.9f*x)-.035f;
    float az=std::abs(z-center);
    float upstream=.66f*(1.f-smooth(-1.13f,-.58f,x));
    float shelf=.035f*(1.f-smooth(-.45f,.35f,x));
    float bank=.38f*std::pow(smooth(.50f,1.42f,az),1.65f);
    float low=.045f*(fbm(Vec3(x*1.55f,4.3f,z*1.55f))-.5f);
    float fractured=.010f*(noise(Vec3(x*13.f,2.7f,z*13.f))-.5f);
    float grain=.0025f*(noise(Vec3(x*49.f,7.3f,z*49.f))-.5f);
    return .073f+upstream+shelf+bank+low+fractured+grain;
}
inline Vec3 bedNormal(float x,float z){
    constexpr float e=.0025f;
    float dx=(bedHeight(x+e,z)-bedHeight(x-e,z))/(2.f*e);
    float dz=(bedHeight(x,z+e)-bedHeight(x,z-e))/(2.f*e);
    return normalized(Vec3(-dx,1.f,-dz));
}
inline float bedSdf(Vec3 p){
    // First-order normal distance to the exact exported heightfield. This is
    // not asserted to be a globally exact nearest-point distance far from it.
    constexpr float e=.009f;
    float h=bedHeight(p.x,p.z);
    float dx=(bedHeight(p.x+e,p.z)-bedHeight(p.x-e,p.z))/(2.f*e);
    float dz=(bedHeight(p.x,p.z+e)-bedHeight(p.x,p.z-e))/(2.f*e);
    return (p.y-h)/std::sqrt(1.f+dx*dx+dz*dz);
}

struct Plane {Vec3 n;float offset;};
struct Rock {
    Vec3 center,radii;
    float yaw=0,pitch=0,rounding=0,detail=0,bound=0;
    float cosineYaw=1,sineYaw=0,cosinePitch=1,sinePitch=0;
    uint32_t seed=0;
    bool hero=false,moss=false;
    std::vector<Plane> planes;

    Vec3 local(Vec3 p)const{
        p-=center;
        Vec3 q(cosineYaw*p.x+sineYaw*p.z,p.y,-sineYaw*p.x+cosineYaw*p.z);
        return Vec3(q.x,cosinePitch*q.y+sinePitch*q.z,-sinePitch*q.y+cosinePitch*q.z);
    }
    float sdf(Vec3 p)const{
        Vec3 q=local(p);
        float field=-std::numeric_limits<float>::infinity();
        for(const Plane &plane:planes){
            float d=dot(q,plane.n)-plane.offset;
            if(std::isfinite(field)){
                float h=std::max(0.f,rounding-std::abs(d-field));
                field=std::max(field,d)+(rounding>0?h*h/(4.f*rounding):0.f);
            }else field=d;
        }
        // Spatial material-scale erosion is fixed to the rock, not the water.
        // It affects the same zero surface in collisions and in the OBJ.
        float erosion=detail*(fbm(q*(hero?23.f:38.f)+Vec3(float(seed%17),1.3f,3.8f))-.5f);
        float r=field+erosion;
        if(hero){
            // An open weathered joint cut only into the upper-facing rind.
            // This produces a real narrow crevice rather than a painted line.
            Vec3 n=normalized(Vec3(.57f,.23f,.79f));
            float joint=std::abs(dot(q,n)-.06f)-.0045f;
            float depth=.11f-q.y;
            float cutter=std::max(joint,depth);
            r=std::max(r,-cutter);
        }
        return r;
    }
};

inline Rock makeRock(Vec3 center,Vec3 radii,uint32_t seed,bool focal=false,bool moss=false){
    Rock rock;
    rock.center=center;rock.radii=radii;rock.seed=seed;rock.hero=focal;rock.moss=moss;
    rock.yaw=(random(seed+11)*2.f-1.f)*pi;
    rock.pitch=(random(seed+23)*2.f-1.f)*.24f;
    rock.cosineYaw=std::cos(rock.yaw);rock.sineYaw=std::sin(rock.yaw);
    rock.cosinePitch=std::cos(rock.pitch);rock.sinePitch=std::sin(rock.pitch);
    float smallest=std::min(radii.x,std::min(radii.y,radii.z));
    rock.rounding=smallest*(focal?.018f:.045f);
    rock.detail=std::min(focal?.019f:.005f,smallest*.10f);
    rock.bound=length(radii)*1.35f+.03f;
    // A clipped, asymmetric convex polyhedron: axis faces, inclined fracture
    // faces, and corner bevel planes. The primitive is never a sphere/ellipsoid.
    std::vector<Vec3> normals={
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
        {1,1,1},{1,1,-1},{-1,1,1},{-1,1,-1},
        {1,-1,1},{1,-1,-1},{-1,-1,1},{-1,-1,-1},
        {.3f,1,.85f},{-.7f,1,.1f},{1,.20f,-.8f},{-.25f,.15f,-1}
    };
    for(size_t i=0;i<normals.size();i++){
        Vec3 n=normals[i];
        if(i>=6)n+=Vec3(random(seed+uint32_t(i)*29)-.5f,random(seed+uint32_t(i)*31)-.5f,random(seed+uint32_t(i)*43)-.5f)*.27f;
        n=normalized(n);
        float support=std::sqrt(n.x*n.x*radii.x*radii.x+n.y*n.y*radii.y*radii.y+n.z*n.z*radii.z*radii.z);
        float asymmetry=.87f+.24f*random(seed+uint32_t(i)*137);
        if(i==2)asymmetry*=.90f;
        rock.planes.push_back({n,support*asymmetry});
    }
    return rock;
}

inline const std::vector<Rock> &rocks(){
    static const std::vector<Rock> values=[](){
        std::vector<Rock> r;
        auto add=[&](Vec3 c,Vec3 s,uint32_t seed,bool focal=false,bool moss=false){r.push_back(makeRock(c,s,seed,focal,moss));};
        // Focal fractured stone; one shoulder intercepts the free falling sheet
        // while the other leaves a clear path into the lower pool.
        add({-.24f,.355f,.035f},{.43f,.40f,.34f},101,true,false);
        add({-.66f,.44f,.54f},{.33f,.30f,.26f},171,true,true);
        add({-.94f,.77f,-.52f},{.40f,.23f,.24f},231,false,true);
        add({.44f,.20f,.50f},{.36f,.22f,.30f},411,false,false);
        add({.70f,.165f,-.57f},{.27f,.15f,.21f},487,false,false);
        add({-1.45f,.87f,.78f},{.44f,.36f,.38f},551,true,true);
        add({-.19f,.37f,-.87f},{.44f,.30f,.33f},601,true,true);
        add({1.30f,.25f,.94f},{.55f,.26f,.37f},701,false,true);
        add({1.62f,.21f,-.84f},{.45f,.20f,.33f},751,false,true);
        add({-1.83f,1.05f,-.92f},{.49f,.43f,.44f},811,false,true);
        add({-2.02f,.98f,.84f},{.51f,.30f,.40f},901,false,true);
        // A naturally irregular size distribution of bed stones. Reject points
        // inside the focal stones so the bottom reads as layered geology rather
        // than a uniform scatter pattern. All positions are deterministic.
        for(uint32_t i=0;i<160;i++){
            uint32_t key=4001+i*97;
            float x=mix(-1.55f,2.15f,random(key));
            float z=mix(-1.36f,1.36f,random(key+1));
            float size=.025f+.080f*std::pow(random(key+2),1.65f);
            Vec3 radii(size*(.85f+.55f*random(key+3)),size*(.40f+.45f*random(key+4)),size*(.70f+.65f*random(key+5)));
            Vec3 center(x,bedHeight(x,z)+radii.y*.27f,z);
            bool clear=true;
            for(const Rock &other:r)if(other.radii.x>.2f && length(center-other.center)<(other.radii.x+other.radii.z)*.40f){clear=false;break;}
            if(clear)add(center,radii,key,false,std::abs(z)>.85f&&random(key+6)>.55f);
        }
        return r;
    }();
    return values;
}

inline float solidSdf(Vec3 p){
    float value=bedSdf(p);
    for(const Rock &rock:rocks()){
        Vec3 delta=p-rock.center;
        float outside=std::max(std::abs(delta.x),std::max(std::abs(delta.y),std::abs(delta.z)))-rock.bound;
        if(outside>std::max(.05f,value))continue;
        value=std::min(value,rock.sdf(p));
    }
    return value;
}

inline std::string bedMaterial(Vec3 p){
    // The submerged bed and near bank are bare mineral surfaces. Broad random
    // moss assignment reads as camouflage and obscures the wetness contrast.
    if(p.x<-.50f)return "rock";
    return "sand";
}
inline std::string rockMaterial(const Rock &rock,Vec3 p,Vec3 n){
    if(rock.moss && p.y>.39f && p.y<.84f && n.y>.06f && n.y<.53f && n.z<-.22f
       && noise(p*12.f+Vec3(2,7,4))>.70f)return "moss";
    return "rock";
}

} // namespace hero

inline Scene makeHeroScene(){
    Scene scene;
    scene.name="fractured_rock_cascade";
    scene.lower={-1.8f,-.35f,-1.2f};scene.upper={1.8f,2.05f,1.2f};
    scene.solidSdf=[](Vec3 p){return hero::solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){
        float pool=std::max(p.y-.31f,-.33f-p.y);
        float upper=std::max(std::max(p.x+.94f,-1.80f-p.x),
                            std::max(std::max(p.y-1.0f,.76f-p.y),std::abs(p.z+.025f)-.385f));
        return std::min(pool,upper);
    };
    scene.initialVelocity=[](Vec3 p){return p.x<-.93f&&p.y>.68f?Vec3(1.45f,0,0):Vec3(.035f,0,0);};
    // Gravity is supplied by the common fluid solver configuration.
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    Inlet inlet;
    inlet.lower={-1.79f,.81f,-.39f};inlet.upper={-1.56f,1.00f,.39f};
    inlet.velocity={1.45f,0,0};inlet.axis=0;inlet.direction=1;inlet.rateScale=1.f;
    scene.inlets.push_back(inlet);
    return scene;
}

} // namespace water
