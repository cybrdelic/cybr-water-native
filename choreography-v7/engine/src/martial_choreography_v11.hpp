#pragma once
#include "fluid.hpp"
#include <array>
#include <cmath>

// V11 is opt-in. No mesh/particle placement and no scene-specific liquid solver.
// Hand kinematics are C1 continuous; attack envelopes crossfade instead of
// exchanging whole velocity fields at a frame boundary. All guide work happens
// on MAC faces before the existing pressure projection.
namespace water { namespace choreography {
struct MartialGuideV11 {
    static constexpr float releaseEnd=8.90f;
    static constexpr int keyCount=19;
    struct Hand { Vec3 p{},v{}; };
    static float ease(float a,float b,float t) {
        float q=clamp((t-a)/(b-a),0.f,1.f);
        return q*q*q*(q*(q*6.f-15.f)+10.f);
    }
    static float envelope(float a,float b,float c,float d,float t) {
        return ease(a,b,t)*(1.f-ease(c,d,t));
    }
    static Vec3 limit(Vec3 v,float m) {
        float q=length(v); return q>m?v*(m/q):v;
    }
    static float bell(float q,float inner,float outer) {
        if(q>=outer)return 0.f;
        return 1.f-ease(inner,outer,q);
    }
    static const char *beat(float t) {
        if(t<1.25f)return "scoop";
        if(t<2.32f)return "whips";
        if(t<3.42f)return "redirect";
        if(t<4.50f)return "slices";
        if(t<5.72f)return "wave";
        if(t<7.05f)return "vortex";
        if(t<7.82f)return "counter";
        if(t<releaseEnd)return "slam";
        return "free_splash";
    }
    // Nonuniform cubic Hermite knots. Shared tangent estimates mean adjacent
    // intervals have identical endpoint velocities: no hand teleport or snap.
    // The two passes are intentionally offset for readable attack silhouettes.
    static Hand hand(int side,float t) {
        static constexpr std::array<float,keyCount> ts={{
            0.f,.45f,1.10f,1.60f,2.12f,2.65f,3.10f,3.55f,4.05f,
            4.55f,5.20f,5.76f,6.33f,6.90f,7.50f,8.05f,8.55f,8.90f,10.f
        }};
        static const std::array<Vec3,keyCount> left={{
            {-.35f,.24f,.10f},{-.54f,.48f,.11f},{-.60f,1.02f,.18f},
            {-.22f,1.31f,.09f},{-1.23f,1.09f,-.16f},{-.54f,.81f,-.18f},
            {-.36f,1.12f,.18f},{-1.09f,.94f,-.08f},{.88f,1.16f,-.28f},
            {-.10f,.71f,.16f},{-.42f,.35f,.22f},{-.36f,.96f,-.12f},
            {.22f,1.26f,-.36f},{-.25f,1.10f,-.43f},{.34f,.97f,-.37f},
            {-.24f,1.15f,-.35f},{-.44f,.46f,-.50f},{-.24f,.26f,-.66f},
            {-.24f,.26f,-.66f}
        }};
        static const std::array<Vec3,keyCount> right={{
            {.35f,.24f,.10f},{.54f,.46f,.11f},{.61f,.98f,.18f},
            {.38f,1.09f,.17f},{1.29f,1.32f,-.15f},{.62f,.83f,-.18f},
            {.45f,1.02f,.20f},{1.03f,.80f,-.10f},{-.89f,1.24f,-.31f},
            {.12f,.79f,.13f},{.43f,.36f,.20f},{.38f,.91f,-.08f},
            {-.27f,1.30f,-.39f},{.23f,1.16f,-.43f},{-.36f,1.06f,-.43f},
            {.28f,1.12f,-.38f},{.46f,.50f,-.53f},{.24f,.26f,-.66f},
            {.24f,.26f,-.66f}
        }};
        const auto &P=side?right:left;
        if(t<=ts[0])return {P[0],{}};
        if(t>=ts[keyCount-1])return {P[keyCount-1],{}};
        int i=0;while(i+2<keyCount&&t>ts[i+1])++i;
        auto tangent=[&](int j)->Vec3 {
            if(j==0||j==keyCount-1)return {};
            return (P[j+1]-P[j-1])/(ts[j+1]-ts[j-1]);
        };
        float dt=ts[i+1]-ts[i],u=clamp((t-ts[i])/dt,0.f,1.f);
        float u2=u*u,u3=u2*u;
        Vec3 a=P[i],b=P[i+1],m0=tangent(i),m1=tangent(i+1);
        Vec3 pos=a*(2*u3-3*u2+1)+m0*(dt*(u3-2*u2+u))+
                 b*(-2*u3+3*u2)+m1*(dt*(u3-u2));
        Vec3 vel=(a*(6*u2-6*u)+m0*(dt*(3*u2-4*u+1))+
                  b*(-6*u2+6*u)+m1*(dt*(3*u2-2*u)))/dt;
        return {pos,vel};
    }
    float time=0;
    std::array<Hand,2> hands{};
    std::array<float,8> weight{};
    void prepare(double seconds) {
        time=float(seconds);
        hands={hand(0,time),hand(1,time)};
        weight={{
            envelope(-.10f,.25f,1.06f,1.50f,time),
            envelope(.82f,1.28f,2.06f,2.55f,time),
            envelope(2.02f,2.35f,3.12f,3.61f,time),
            envelope(3.02f,3.43f,4.20f,4.68f,time),
            envelope(4.20f,4.54f,5.40f,5.92f,time),
            envelope(5.34f,5.75f,6.76f,7.28f,time),
            envelope(6.77f,7.14f,7.67f,8.13f,time),
            envelope(7.64f,8.01f,8.47f,releaseEnd,time)
        }};
    }
    struct Accum {
        Vec3 v{};
        float w=0;
        void add(Vec3 x,float a) {
            if(a>1.e-7f){v+=x*a;w+=a;}
        }
    };
    static float segment(Vec3 p,Vec3 a,Vec3 b,Vec3 &foot,Vec3 &tangent) {
        Vec3 d=b-a;
        float q=clamp(dot(p-a,d)/std::max(dot(d,d),1.e-8f),0.f,1.f);
        foot=a+d*q;
        tangent=normalized(d);
        return length(p-foot);
    }
    // The whip's visible trajectory curves from its basin root to the hand.
    // Only its velocity field is used: never surface/particle geometry.
    void whip(Vec3 p,int h,Accum &out,float intensity) const {
        const Hand &H=hands[h];float s=h?1.f:-1.f;
        Vec3 a{s*.28f,.22f,.09f},b{s*.51f,.84f,.22f};
        Vec3 c=H.p+Vec3(-s*.23f,.18f,-.12f);
        Vec3 last=a;float dmin=1.e12f,uq=0;Vec3 closest{},tangent{};
        for(int i=1;i<=16;i++) {
            float u=float(i)/16.f,r=1-u;
            Vec3 next=a*(r*r*r)+b*(3*r*r*u)+c*(3*r*u*u)+H.p*(u*u*u);
            Vec3 foot,tan;
            float d=segment(p,last,next,foot,tan);
            if(d<dmin){dmin=d;closest=foot;tangent=tan;uq=(i-.5f)/16.f;}
            last=next;
        }
        float tube=bell(dmin,.055f,.185f)*bell(std::abs(p.y-.84f),.72f,1.35f);
        float exposedTip=1.f-.75f*ease(.78f,1.f,uq);
        Vec3 speed=H.v*(.45f+.55f*uq)+tangent*(2.60f*exposedTip);
        speed+=(closest-p)*6.0f;
        out.add(speed,intensity*tube);
    }
    VelocityGuide sample(Vec3 p,double) const {
        VelocityGuide g;
        if(time>=releaseEnd||p.y<.035f||p.y>2.65f||
           std::abs(p.x)>2.3f||std::abs(p.z)>1.75f)return g;
        Accum out;
        const Vec3 c{0,.85f,-.18f};
        const Vec3 q=p-c;
        float radius=std::sqrt(q.x*q.x+q.z*q.z);
        Vec3 radial=radius>1.e-5f?Vec3(q.x/radius,0,q.z/radius):Vec3(1,0,0);
        Vec3 tangent{-radial.z,0,radial.x};
        float w=0;
        if((w=weight[0])>0)for(int h=0;h<2;h++) {
            const Hand &H=hands[h];float s=h?1.f:-1.f;
            Vec3 foot,tan,root{s*.28f,.20f,.07f};
            float d=segment(p,root,H.p,foot,tan);
            float tube=bell(d,.06f,.26f)*bell(std::abs(p.y-.65f),.8f,1.45f);
            out.add(H.v*.70f+tan*1.42f+(foot-p)*7.f+Vec3(0,.58f,0),w*tube);
        }
        if((w=weight[1])>0) {
            whip(p,0,out,w*.95f);whip(p,1,out,w);
        }
        if((w=weight[2])>0) {
            float ann=bell(std::abs(radius-.46f),.09f,.28f)*
                      bell(std::abs(p.y-.94f),.50f,.89f);
            out.add(tangent*3.4f-radial*.45f+Vec3(0,.26f,0),w*ann);
        }
        if((w=weight[3])>0) {
            // A thin plane with finite width, pushed tangentially and
            // compressed through its normal, not a rendered blade.
            Vec3 mid=(hands[0].p+hands[1].p)*.5f;
            Vec3 edge=normalized(hands[1].p-hands[0].p);
            Vec3 n=normalized(Vec3(.07f,.27f,1.f));
            Vec3 rel=p-mid;
            float longitudinal=dot(rel,edge);
            Vec3 perpendicular=rel-edge*longitudinal;
            float breadth=bell(std::abs(longitudinal),.60f,1.35f);
            float thickness=bell(std::abs(dot(perpendicular,n)),.026f,.12f);
            Vec3 sweep=(hands[0].v+hands[1].v)*.48f+
                       Vec3(-2.10f,.10f,-.46f);
            out.add(sweep-n*dot(rel,n)*17.f,w*breadth*thickness);
        }
        if((w=weight[4])>0) {
            // Travelling crest; the fluid decides if/where it breaks.
            float u=ease(4.42f,5.75f,time),z=.46f-1.27f*u;
            float crest=bell(std::abs(p.z-z),.10f,.40f)*
                        bell(std::abs(p.y-(.49f+.22f*std::sin(3.14159f*u))),.20f,.83f)*
                        bell(std::abs(p.x),.95f,1.62f);
            float shoulder=bell(std::abs(p.y-.30f),.13f,.48f)*
                           bell(std::abs(p.x),1.10f,1.75f)*
                           bell(std::abs(p.z-z),.25f,.68f);
            out.add(Vec3(0,1.9f*crest,-3.15f-.45f*u),
                    w*(1.15f*crest+.34f*shoulder));
        }
        if((w=weight[5])>0) {
            float ann=bell(std::abs(radius-.44f),.07f,.28f)*
                      bell(std::abs(p.y-.95f),.52f,1.11f);
            out.add(tangent*3.55f-radial*1.03f+Vec3(0,1.25f,0),
                    w*ann*1.3f);
        }
        if((w=weight[6])>0) {
            float ann=bell(std::abs(radius-.43f),.08f,.29f)*
                      bell(std::abs(p.y-1.0f),.52f,1.04f);
            out.add(tangent*-2.25f-radial*.65f+Vec3(0,.33f,-.32f),
                    w*ann*.90f);
        }
        if((w=weight[7])>0) {
            float x=bell(std::abs(p.x),.88f,1.56f);
            float z=bell(std::abs(p.z+.25f),.54f,1.12f);
            float y=bell(std::abs(p.y-.89f),.45f,1.17f);
            out.add(Vec3(0,-3.60f,-2.0f)+
                    (hands[0].v+hands[1].v)*.24f,w*x*y*z);
        }
        if(out.w<=1.e-7f)return g;
        // Fully normalized feedback prevents overlapping poses injecting
        // the sum of several independent attack speeds.
        g.target=limit(out.v/std::max(out.w,1.f),5.20f);
        float start=ease(.0f,.28f,time);
        float release=1.f-ease(8.48f,releaseEnd,time);
        float influence=std::min(out.w,1.f)*start*release;
        g.rate=19.f*influence;
        g.acceleration=Vec3(0,9.81f*influence,0);
        g.accelerationLimit=72.f;
        return g;
    }
};
}} // namespace water::choreography
