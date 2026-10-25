#pragma once
#include "fluid.hpp"

// Native art direction only. Evaluated on MAC faces before pressure projection;
// never called by the mesher/tracer and never overwrites a particle position.
// Metres, seconds, +Y up. The accepted V6 RetainedTrickGuide remains unchanged.
namespace water { namespace choreography {

struct DuetGuide {
    static constexpr float tau=6.2831853071795864769f;
    static constexpr int segments=80;
    static constexpr float releaseStart=8.35f,releaseEnd=8.95f;
    struct Pose {Vec3 p{},thin{0,0,1};float circulation=.55f;};
    struct Knot {Vec3 p{},v{},a{},tangent{},curvature{},thin{};float circulation=.55f;};
    struct Hit {Knot k;float d2=1e20f;};
    std::array<std::array<Knot,segments+1>,2> knots{};
    std::array<float,2> release{1,1};
    float time=0;
    // Conservative bounds accelerate exactly the same 80-segment guide.
    // They change query cost, not the force, curve resolution or fluid state.
    static constexpr int groupSize=8, groups=(segments+groupSize-1)/groupSize;
    struct Bounds {Vec3 lower{},upper{};};
    std::array<std::array<Bounds,groups>,2> bounds{};

    static float ease(float a,float b,float t) {
        float x=clamp((t-a)/(b-a),0.f,1.f);
        return x*x*x*(x*(x*6.f-15.f)+10.f);
    }
    static float pulse(float a,float b,float c,float d,float t) {
        return ease(a,b,t)*(1.f-ease(c,d,t));
    }
    static Vec3 lerp(Vec3 a,Vec3 b,float t){return a*(1-t)+b*t;}
    static Vec3 limit(Vec3 p,float maximum) {
        float n=length(p);return n>maximum?p*(maximum/n):p;
    }
    static Vec3 rotateY(Vec3 p,float a) {
        float c=std::cos(a),s=std::sin(a);return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};
    }
    static Pose blend(const Pose &a,const Pose &b,float t) {
        return {lerp(a.p,b.p,t),normalized(lerp(a.thin,b.thin,t)),
                a.circulation*(1-t)+b.circulation*t};
    }
    static const char *beat(float t) {
        if(t<.9f)return "draw";
        if(t<2.15f)return "twin_cobras";
        if(t<3.65f)return "braid";
        if(t<4.55f)return "anticipation";
        if(t<5.70f)return "staggered_whip";
        if(t<6.65f)return "orbital_catch";
        if(t<7.80f)return "shield";
        if(t<releaseEnd)return "spiral_return";
        return "free_splash";
    }
    // A stable material parameter u is shared by every pose. Time blends are
    // C2; pose velocity/acceleration come from that same moving curve, not from
    // an independently animated force magnitude. All curves are periodic.
    static Pose pose(int branch,float u,float t) {
        float side=branch? -1.f:1.f;
        float a=tau*u,c=std::cos(a),s=std::sin(a);
        float top=.5f+.5f*s;
        Pose pool{{side*.36f+.235f*c,.17f,.235f*s},{0,1,0},.30f};
        // Tall separated silhouettes; the upper arc fans outward while the
        // lower arc remains close to the surface to draw from the actual pool.
        Pose cobra{{side*(.46f+(.23f+.095f*top)*c),.83f+.65f*s,
                    side*.13f+.07f*std::sin(2*a)}, {0,0,1},.66f};
        // Alternating depth is deliberate: the two curves cross in projection,
        // not at the same 3D point. Pressure still resolves real liquid overlap.
        Pose braid{{.68f*s,1.04f+.54f*std::sin(2*a),
                    side*.25f+.17f*c}, {0,0,1},.88f};
        Pose coil{{side*.36f+.24f*c,1.00f+.46f*s,
                   side*.19f+.12f*std::sin(2*a)}, {0,0,1},.48f};
        // Narrow closed hairpin, rather than the old broad horizontal torus.
        // Branch 1's strike follows 0.20s later; shape, flow and return all share
        // that timing. The tangent speed is bounded independently of feedback.
        Pose strike{{side*(.36f+1.12f*c),1.08f+side*.27f+.13f*s+.15f*(1-c*c),
                     side*.29f+.075f*std::sin(2*a)}, {0,0,1},1.55f};
        float orbitAngle=side*.95f+side*.85f*ease(5.65f,6.60f,t);
        Vec3 orbital=rotateY({.65f*c,.59f*s,.13f*s},orbitAngle);
        Pose orbit{orbital+Vec3(side*.35f,1.07f,side*.20f),rotateY({0,-.215f,.977f},orbitAngle),.95f};
        // A broad tilted protective ring, with a real dwell. The two streams
        // occupy adjacent layers instead of forcing two opposed velocities
        // onto one ideal zero-thickness curve.
        Pose shield{{.68f*c,1.04f+.70f*s,.18f*s+side*.075f},
                     normalized(Vec3(0,-.25f,1)),.62f};
        // Reorient into a low horizontal orbit before release. This reduces
        // the simultaneous vertical slab impact of V6 without faking splash
        // geometry. The final 1.05s is entirely unforced liquid dynamics.
        float returnAngle=side*.35f;
        Vec3 landing=rotateY({.53f*c,.22f*s,.38f*s},returnAngle);
        Pose descend{landing+Vec3(side*.12f,.70f,0),normalized(Vec3(0,.86f,-.50f)),.40f};
        Pose p=blend(pool,cobra,ease(.20f,1.65f,t));
        p=blend(p,braid,ease(2.10f,3.20f,t));
        p=blend(p,coil,ease(3.65f,4.32f,t));
        float lag=branch*.20f;
        p=blend(p,strike,ease(4.38f+lag,5.10f+lag,t));
        p=blend(p,orbit,ease(5.55f+lag,6.48f+lag,t));
        p=blend(p,shield,ease(6.62f,7.32f,t));
        p=blend(p,descend,ease(7.90f,8.66f,t));
        return p;
    }
    void prepare(double seconds) {
        time=float(seconds);
        // Releasing one branch fractionally first gives a travelling return,
        // instead of dropping the complete ring as one rigid-looking slab.
        for(int b=0;b<2;b++) {
            release[b]=1-ease(releaseStart-.10f*b,releaseEnd-.10f*b,time);
            for(int i=0;i<=segments;i++) {
                float u=float(i)/segments,du=1.f/segments,dt=.004f;
                Pose p=pose(b,u,time),before=pose(b,u-du,time),after=pose(b,u+du,time);
                Pose prev=pose(b,u,time-dt),next=pose(b,u,time+dt);
                auto &k=knots[b][i];k.p=p.p;k.v=(next.p-prev.p)/(2*dt);
                k.a=limit((next.p-2*p.p+prev.p)/(dt*dt),12.f);
                k.tangent=normalized(after.p-before.p);
                float ds=.5f*(length(after.p-p.p)+length(p.p-before.p));
                Vec3 curvature=(after.p-2*p.p+before.p)/std::max(ds*ds,1e-7f);
                k.curvature=limit(curvature-k.tangent*dot(curvature,k.tangent),15.f);
                Vec3 thin=p.thin-k.tangent*dot(k.tangent,p.thin);
                if(length(thin)<.05f)thin=cross(k.tangent,Vec3(1,0,0));
                if(length(thin)<.05f)thin=cross(k.tangent,Vec3(0,0,1));
                k.thin=normalized(thin);k.circulation=p.circulation;
            }
            for(int group=0;group<groups;group++) {
                int begin=group*groupSize,end=std::min(segments,begin+groupSize);
                Bounds box{knots[b][begin].p,knots[b][begin].p};
                for(int i=begin+1;i<=end;i++)for(int axis=0;axis<3;axis++) {
                    box.lower[axis]=std::min(box.lower[axis],knots[b][i].p[axis]);
                    box.upper[axis]=std::max(box.upper[axis],knots[b][i].p[axis]);
                }
                bounds[b][group]=box;
            }
        }
    }
    Hit nearest(Vec3 p,int branch) const {
        Hit h;int best=0;float fraction=0;
        // Ascending segment order preserves the original exact tie policy.
        for(int group=0;group<groups;group++) {
            const auto &box=bounds[branch][group];float lowerBound=0;
            for(int axis=0;axis<3;axis++) {
                float delta=std::max({box.lower[axis]-p[axis],0.f,p[axis]-box.upper[axis]});
                lowerBound+=delta*delta;
            }
            // Margin makes pruning conservative with floating-point roundoff.
            if(lowerBound>h.d2+2e-6f)continue;
            int begin=group*groupSize,end=std::min(segments,begin+groupSize);
            for(int i=begin;i<end;i++) {
                const auto &a=knots[branch][i],&b=knots[branch][i+1];
                Vec3 e=b.p-a.p;
                float f=clamp(dot(p-a.p,e)/std::max(dot(e,e),1e-9f),0.f,1.f);
                Vec3 foot=a.p+e*f;float d2=dot(p-foot,p-foot);
                if(d2<h.d2){h.d2=d2;best=i;fraction=f;h.k.p=foot;}
            }
        }
        // Interpolate the winning segment only, not every temporary minimum.
        const auto &a=knots[branch][best],&b=knots[branch][best+1];float f=fraction;
        h.k.v=lerp(a.v,b.v,f);h.k.a=lerp(a.a,b.a,f);
        h.k.tangent=normalized(lerp(a.tangent,b.tangent,f));
        h.k.curvature=lerp(a.curvature,b.curvature,f);
        h.k.thin=normalized(lerp(a.thin,b.thin,f));
        h.k.circulation=a.circulation*(1-f)+b.circulation*f;
        return h;
    }
    VelocityGuide sample(Vec3 p,double) const {
        VelocityGuide g;
        if(time>=releaseEnd||p.y<.035f||p.y>2.5f||std::abs(p.x)>2.0f||std::abs(p.z)>1.35f)return g;
        std::array<Hit,2> h{nearest(p,0),nearest(p,1)};
        float minimum=std::min(h[0].d2,h[1].d2),total=0;
        Vec3 target{},feed{};
        for(int b=0;b<2;b++) {
            const auto &k=h[b].k;float d=std::sqrt(h[b].d2);
            // Smooth competition avoids a discontinuous velocity jump on the
            // nearest-branch bisector. Curves remain depth-separated for braid.
            float competition=std::exp(-std::min(60.f,(h[b].d2-minimum)/.0081f));
            float w=competition*(1-ease(.19f,.48f,d))*release[b]*ease(0,.25f,time);
            if(w<1e-7f)continue;
            Vec3 offset=k.p-p;offset-=k.tangent*dot(offset,k.tangent);
            Vec3 confinement=offset*4.f+k.thin*(dot(offset,k.thin)*11.f);
            Vec3 desired=k.v+k.tangent*k.circulation+confinement;
            target+=desired*w;
            feed+=(Vec3(0,9.81f,0)+k.a+k.curvature*(k.circulation*k.circulation))*w;
            total+=w;
        }
        if(total<=1e-7f)return g;
        g.target=limit(target/total,4.6f);
        g.rate=22.f*std::min(1.f,total);
        g.acceleration=limit(feed/std::max(1.f,total),42.f);
        g.accelerationLimit=65.f;
        return g;
    }
};


// Combat-oriented waterbending performance: explicit whips, slices, wave,
// vortex, spiral lance and slam. Like DuetGuide, this only prescribes moving
// velocity targets before pressure projection; it never renders guide geometry
// and never teleports liquid.
struct CombatGuide {
    static constexpr float tau=6.2831853071795864769f;
    static constexpr float pi=3.14159265358979323846f;
    static constexpr int segments=96;
    static constexpr float releaseStart=8.55f,releaseEnd=9.05f;
    struct Pose {Vec3 p{},thin{0,0,1};float circulation=.55f;};
    struct Knot {Vec3 p{},v{},a{},tangent{},curvature{},thin{};float circulation=.55f;};
    struct Hit {Knot k;float d2=1e20f;};
    std::array<std::array<Knot,segments+1>,2> knots{};
    std::array<float,2> release{1,1};
    float time=0;
    static constexpr int groupSize=8,groups=(segments+groupSize-1)/groupSize;
    struct Bounds {Vec3 lower{},upper{};};
    std::array<std::array<Bounds,groups>,2> bounds{};

    static float ease(float a,float b,float t){
        float x=clamp((t-a)/(b-a),0.f,1.f);return x*x*x*(x*(x*6.f-15.f)+10.f);
    }
    static Vec3 lerp(Vec3 a,Vec3 b,float t){return a*(1-t)+b*t;}
    static Vec3 limit(Vec3 p,float m){float n=length(p);return n>m?p*(m/n):p;}
    static Vec3 rotateY(Vec3 p,float a){float c=std::cos(a),s=std::sin(a);return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};}
    static Pose blend(const Pose&a,const Pose&b,float t){return {lerp(a.p,b.p,t),normalized(lerp(a.thin,b.thin,t)),a.circulation*(1-t)+b.circulation*t};}
    static const char* beat(float t){
        if(t<.75f)return "draw";
        if(t<2.15f)return "dual_whips";
        if(t<3.45f)return "cross_slices";
        if(t<4.95f)return "wave_push";
        if(t<6.45f)return "vortex_gather";
        if(t<7.80f)return "spiral_lance";
        if(t<releaseEnd)return "slam";
        return "free_splash";
    }
    static Pose pose(int branch,float u,float t){
        float side=branch?-1.f:1.f;
        float a=tau*u,c=std::cos(a),s=std::sin(a);
        float upper=.5f+.5f*s;
        Pose pool{{side*.32f+.23f*c,.17f,.23f*s},{0,1,0},.28f};

        float lag=branch*.13f;
        float whipSwing=ease(.78f+lag,1.65f+lag,t);
        float whipReach=.72f+.48f*whipSwing;
        Pose whip{{side*(.28f+whipReach*c),.77f+.53f*s+.14f*(1-c*c),
                   side*.16f+.055f*std::sin(2*a)}, {0,0,1},1.55f};

        float sliceTilt=side*.58f;
        Vec3 sliceBase{1.18f*c,.96f+.10f*s,side*.25f+.05f*std::sin(2*a)};
        Vec3 sliceP=rotateY(sliceBase,sliceTilt);
        if(branch) sliceP.y=.92f+.52f*c+.10f*s;
        Pose slice{sliceP,normalized(branch?Vec3(.28f,.87f,.40f):Vec3(0,.18f,1)),1.95f};

        float wavePush=ease(3.35f,4.70f,t);
        float theta=a+(branch?pi:0.f);
        Pose wave{{.95f*std::cos(theta),.56f+.62f*std::sin(theta),
                   .38f-.86f*wavePush+.08f*std::sin(2*theta)},
                  normalized(Vec3(0,.16f,1)),.92f};

        float va=2.0f*a+side*.72f;
        float vr=.40f+.10f*std::sin(a);
        Pose vortex{{vr*std::cos(va),.87f+.58f*s,vr*std::sin(va)},
                     normalized(Vec3(std::cos(va),0,std::sin(va))),1.38f};

        float lx=-.60f+1.75f*upper;
        float la=3.0f*a+side*pi*.5f;
        Pose lance{{lx,1.03f+.22f*std::cos(la),side*.10f+.22f*std::sin(la)},
                    normalized(Vec3(0,std::cos(la),std::sin(la))),1.72f};

        float slamPhase=ease(7.72f,8.62f,t);
        float sa=a+side*.36f;
        Pose slam{{.62f*c,.98f-.63f*slamPhase+.32f*s,
                   .22f*std::sin(sa)-.34f*slamPhase},
                   normalized(Vec3(0,.72f,-.69f)),.52f};

        Pose p=blend(pool,whip,ease(.18f,.92f,t));
        p=blend(p,slice,ease(2.05f,2.62f,t));
        p=blend(p,wave,ease(3.32f,3.88f,t));
        p=blend(p,vortex,ease(4.86f,5.34f,t));
        p=blend(p,lance,ease(6.35f,6.82f,t));
        p=blend(p,slam,ease(7.72f,8.22f,t));
        return p;
    }
    void prepare(double seconds){
        time=float(seconds);
        for(int b=0;b<2;b++){
            release[b]=1-ease(releaseStart-.08f*b,releaseEnd-.08f*b,time);
            for(int i=0;i<=segments;i++){
                float u=float(i)/segments,du=1.f/segments,dt=.0035f;
                Pose p=pose(b,u,time),before=pose(b,u-du,time),after=pose(b,u+du,time);
                Pose prev=pose(b,u,time-dt),next=pose(b,u,time+dt);
                auto &k=knots[b][i];k.p=p.p;k.v=(next.p-prev.p)/(2*dt);
                k.a=limit((next.p-2*p.p+prev.p)/(dt*dt),18.f);
                k.tangent=normalized(after.p-before.p);
                float ds=.5f*(length(after.p-p.p)+length(p.p-before.p));
                Vec3 curv=(after.p-2*p.p+before.p)/std::max(ds*ds,1e-7f);
                k.curvature=limit(curv-k.tangent*dot(curv,k.tangent),18.f);
                Vec3 thin=p.thin-k.tangent*dot(k.tangent,p.thin);
                if(length(thin)<.05f)thin=cross(k.tangent,Vec3(1,0,0));
                if(length(thin)<.05f)thin=cross(k.tangent,Vec3(0,0,1));
                k.thin=normalized(thin);k.circulation=p.circulation;
            }
            for(int group=0;group<groups;group++){
                int begin=group*groupSize,end=std::min(segments,begin+groupSize);
                Bounds box{knots[b][begin].p,knots[b][begin].p};
                for(int i=begin+1;i<=end;i++)for(int axis=0;axis<3;axis++){
                    box.lower[axis]=std::min(box.lower[axis],knots[b][i].p[axis]);
                    box.upper[axis]=std::max(box.upper[axis],knots[b][i].p[axis]);
                }
                bounds[b][group]=box;
            }
        }
    }
    Hit nearest(Vec3 p,int branch) const{
        Hit h;int best=0;float frac=0;
        for(int group=0;group<groups;group++){
            const auto &box=bounds[branch][group];float lb=0;
            for(int axis=0;axis<3;axis++){float d=std::max({box.lower[axis]-p[axis],0.f,p[axis]-box.upper[axis]});lb+=d*d;}
            if(lb>h.d2+2e-6f)continue;
            int begin=group*groupSize,end=std::min(segments,begin+groupSize);
            for(int i=begin;i<end;i++){
                const auto&a=knots[branch][i];const auto&b=knots[branch][i+1];Vec3 e=b.p-a.p;
                float f=clamp(dot(p-a.p,e)/std::max(dot(e,e),1e-9f),0.f,1.f);Vec3 foot=a.p+e*f;float d2=dot(p-foot,p-foot);
                if(d2<h.d2){h.d2=d2;best=i;frac=f;h.k.p=foot;}
            }
        }
        const auto&a=knots[branch][best];const auto&b=knots[branch][best+1];float f=frac;
        h.k.v=lerp(a.v,b.v,f);h.k.a=lerp(a.a,b.a,f);h.k.tangent=normalized(lerp(a.tangent,b.tangent,f));
        h.k.curvature=lerp(a.curvature,b.curvature,f);h.k.thin=normalized(lerp(a.thin,b.thin,f));
        h.k.circulation=a.circulation*(1-f)+b.circulation*f;return h;
    }
    VelocityGuide sample(Vec3 p,double) const{
        VelocityGuide g;if(time>=releaseEnd||p.y<.035f||p.y>2.65f||std::abs(p.x)>2.2f||std::abs(p.z)>1.65f)return g;
        std::array<Hit,2> h{nearest(p,0),nearest(p,1)};float minimum=std::min(h[0].d2,h[1].d2),total=0;Vec3 target{},feed{};
        std::string stage=beat(time);
        float captureOuter=(stage=="wave_push"||stage=="vortex_gather")?.58f:.46f;
        for(int b=0;b<2;b++){
            const auto&k=h[b].k;float d=std::sqrt(h[b].d2);
            float comp=std::exp(-std::min(60.f,(h[b].d2-minimum)/.009f));
            float w=comp*(1-ease(.18f,captureOuter,d))*release[b]*ease(0,.22f,time);if(w<1e-7f)continue;
            Vec3 offset=k.p-p;offset-=k.tangent*dot(offset,k.tangent);
            float thinGain=(stage=="cross_slices")?15.f:10.f;
            float normalGain=(stage=="wave_push")?2.7f:4.2f;
            Vec3 confinement=offset*normalGain+k.thin*(dot(offset,k.thin)*thinGain);
            Vec3 desired=k.v+k.tangent*k.circulation+confinement;
            target+=desired*w;feed+=(Vec3(0,9.81f,0)+k.a+k.curvature*(k.circulation*k.circulation))*w;total+=w;
        }
        if(total<=1e-7f)return g;
        float cap=(stage=="dual_whips"||stage=="spiral_lance")?5.4f:4.8f;
        g.target=limit(target/total,cap);g.rate=24.f*std::min(1.f,total);g.acceleration=limit(feed/std::max(1.f,total),48.f);g.accelerationLimit=72.f;return g;
    }
};

}} // namespace water::choreography
