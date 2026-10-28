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
        auto bezier=[](Vec3 p0,Vec3 p1,Vec3 p2,Vec3 p3,float q){float r=1-q;return p0*(r*r*r)+p1*(3*r*r*q)+p2*(3*r*q*q)+p3*(q*q*q);};
        auto closeOpen=[&](Vec3 root,Vec3 tip,Vec3 visible,float vf=.62f){
            if(u<=vf)return visible;
            Vec3 hiddenTip{tip.x,.10f,-1.62f-.08f*side},hiddenRoot{root.x,.10f,-1.62f-.08f*side};
            if(u<.73f)return lerp(tip,hiddenTip,ease(vf,.73f,u));
            if(u<.91f)return lerp(hiddenTip,hiddenRoot,ease(.73f,.91f,u));
            return lerp(hiddenRoot,root,ease(.91f,1.f,u));
        };
        float vf=.62f,q=clamp(u/vf,0.f,1.f);
        Vec3 poolRoot{side*.24f,.18f,side*.11f};
        Vec3 poolTip{side*.48f,.18f,side*.11f};
        Vec3 poolVisible=lerp(poolRoot,poolTip,q);
        Pose pool{closeOpen(poolRoot,poolTip,poolVisible),{0,1,0},.24f};

        float lag=branch*.13f,sw=ease(.70f+lag,1.58f+lag,t);
        Vec3 whipRoot{side*.23f,.27f,side*.20f};
        Vec3 whipTip{side*(.80f+.50f*sw),1.08f+.26f*std::sin(pi*sw),side*(.26f+.06f*sw)};
        Vec3 whipVisible=bezier(whipRoot,{side*.36f,.78f,side*.20f},{side*(.56f+.34f*sw),1.46f,side*.24f},whipTip,q);
        float whipFlow=2.05f*(1-ease(.48f,.62f,u));
        Pose whip{closeOpen(whipRoot,whipTip,whipVisible),{0,0,1},whipFlow};

        Vec3 s0,s1;
        if(!branch){s0={-1.34f,.92f,.18f};s1={1.34f,.92f,.18f};}
        else{s0={-1.08f,.48f,-.20f};s1={1.08f,1.48f,-.20f};}
        Vec3 sliceVisible=lerp(s0,s1,ease(0,1,q));
        float sliceFlow=2.30f*(1-ease(.50f,.62f,u));
        Pose slice{closeOpen(s0,s1,sliceVisible),normalized(branch?Vec3(.22f,.74f,.64f):Vec3(0,.06f,1)),sliceFlow};

        float push=ease(3.32f,4.68f,t);
        float x0=branch?0.f:-1.28f,x1=branch?1.28f:0.f;
        Vec3 w0{x0,.22f,.42f-.98f*push},w1{x1,.22f,.42f-.98f*push};
        float wx=x0+(x1-x0)*q;
        Vec3 waveVisible{wx,.22f+.98f*std::sin(pi*q),.42f-.98f*push+side*.035f};
        Pose wave{closeOpen(w0,w1,waveVisible),normalized(Vec3(0,.10f,1)),1.15f};

        Vec3 vroot{side*.25f,.20f,side*.08f};
        float va=(2.25f*tau*q)+side*.75f,vr=.43f;
        Vec3 vortexVisible{vr*std::cos(va),.24f+1.28f*q,vr*std::sin(va)};
        Vec3 vtip{vr*std::cos(2.25f*tau+side*.75f),1.52f,vr*std::sin(2.25f*tau+side*.75f)};
        if(q<.10f)vortexVisible=lerp(vroot,vortexVisible,ease(0,.10f,q));
        Pose vortex{closeOpen(vroot,vtip,vortexVisible),normalized(Vec3(std::cos(va),0,std::sin(va))),1.55f};

        Vec3 lroot{-.72f,.98f,side*.06f};
        float la=3.f*tau*q+side*pi*.5f;
        Vec3 lanceVisible{-.72f+2.05f*q,1.02f+.18f*std::cos(la),side*.06f+.18f*std::sin(la)};
        Vec3 ltip{1.33f,1.02f+.18f*std::cos(3.f*tau+side*pi*.5f),side*.06f+.18f*std::sin(3.f*tau+side*pi*.5f)};
        float lanceFlow=2.05f*(1-ease(.50f,.62f,u));
        Pose lance{closeOpen(lroot,ltip,lanceVisible),normalized(Vec3(0,std::cos(la),std::sin(la))),lanceFlow};

        float slam=ease(7.72f,8.62f,t);
        Vec3 slamRoot{side*.55f,1.18f,side*.12f},slamTip{side*.16f,.22f,-.42f};
        Vec3 slamVisible=bezier(slamRoot,{side*.75f,1.34f,side*.05f},{side*.48f,.48f,-.30f},slamTip,q);
        slamVisible.y-=.18f*slam;
        Pose slamPose{closeOpen(slamRoot,slamTip,slamVisible),normalized(Vec3(0,.72f,-.69f)),.62f};

        Pose p=blend(pool,whip,ease(.15f,.82f,t));
        p=blend(p,slice,ease(1.98f,2.72f,t));
        p=blend(p,wave,ease(3.28f,3.98f,t));
        p=blend(p,vortex,ease(4.78f,5.42f,t));
        p=blend(p,lance,ease(6.28f,6.92f,t));
        p=blend(p,slamPose,ease(7.68f,8.28f,t));
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
        float captureOuter=.24f;
        if(stage=="wave_push")captureOuter=.45f;
        else if(stage=="vortex_gather")captureOuter=.18f;
        else if(stage=="dual_whips")captureOuter=.13f;
        else if(stage=="cross_slices")captureOuter=.11f;
        else if(stage=="spiral_lance")captureOuter=.13f;        for(int b=0;b<2;b++){
            const auto&k=h[b].k;float d=std::sqrt(h[b].d2);
            float comp=std::exp(-std::min(60.f,(h[b].d2-minimum)/.009f));
            float w=comp*(1-ease(.18f,captureOuter,d))*release[b]*ease(0,.22f,time);if(w<1e-7f)continue;
            Vec3 offset=k.p-p;offset-=k.tangent*dot(offset,k.tangent);
            float thinGain=(stage=="cross_slices")?28.f:(stage=="spiral_lance"?24.f:(stage=="dual_whips"?24.f:14.f));
            float normalGain=(stage=="wave_push")?3.0f:(stage=="vortex_gather"?7.0f:6.2f);
            Vec3 confinement=offset*normalGain+k.thin*(dot(offset,k.thin)*thinGain);
            Vec3 desired=k.v+k.tangent*k.circulation+confinement;
            target+=desired*w;feed+=(Vec3(0,9.81f,0)+k.a+k.curvature*(k.circulation*k.circulation))*w;total+=w;
        }
        if(total<=1e-7f)return g;
        float cap=(stage=="dual_whips"||stage=="spiral_lance")?5.4f:4.8f;
        g.target=limit(target/total,cap);float feedback=(stage=="wave_push")?19.f:15.f;g.rate=feedback*std::min(1.f,total);g.acceleration=limit(feed/std::max(1.f,total),48.f);g.accelerationLimit=72.f;return g;
    }
};


struct MartialGuide {
    static constexpr float pi=3.14159265358979323846f;
    static constexpr float releaseEnd=9.0f;
    float time=0;
    struct Hand {Vec3 p{},v{},palm{};};
    static float ease(float a,float b,float t){float x=clamp((t-a)/(b-a),0.f,1.f);return x*x*x*(x*(x*6.f-15.f)+10.f);}
    static Vec3 lerp(Vec3 a,Vec3 b,float t){return a*(1-t)+b*t;}
    static Vec3 limit(Vec3 v,float m){float n=length(v);return n>m?v*(m/n):v;}
    static Vec3 handPos(int side,float t){
        float s=side?1.f:-1.f;
        if(t<1.25f){float q=ease(0,1.25f,t);float a=-1.15f+1.55f*q;return {s*(.36f+.17f*std::cos(a)),.34f+.82f*q+.18f*std::sin(a),.10f+.20f*std::sin(a)};}
        if(t<2.45f){float q=ease(1.25f,2.45f,t);return side?lerp({.32f,1.04f,.10f},{1.26f,1.18f,-.04f},q):lerp({-.32f,.98f,.10f},{-.08f,.70f,.28f},q);}
        if(t<3.45f){float q=ease(2.45f,3.45f,t);float a=(side?1.f:-1.f)*(1.2f*pi*q);return {s*.48f+.48f*std::cos(a),.90f+.32f*std::sin(a),.15f*std::sin(2*a)};}
        if(t<4.45f){float q=ease(3.45f,4.45f,t);return side?lerp({.62f,.92f,.08f},{-1.10f,1.22f,-.18f},q):lerp({-.62f,.82f,.08f},{1.08f,.72f,-.18f},q);}
        if(t<5.65f){float q=ease(4.45f,5.65f,t);return {s*(.50f-.20f*q),1.00f-.54f*q,.26f-.82f*q};}
        if(t<7.0f){float q=ease(5.65f,7.0f,t);float a=(side?1.f:-1.f)*(2.2f*pi*q);return {.48f*std::cos(a),.88f+.46f*std::sin(a),-.18f+.48f*std::sin(a)};}
        if(t<8.0f){float q=ease(7.0f,8.0f,t);float a=(side?-1.f:1.f)*(1.6f*pi*q);return {.34f*std::cos(a),.96f+.30f*std::sin(a),-.18f+.34f*std::sin(a)};}
        float q=ease(8.0f,8.82f,t);return lerp({s*.35f,.98f,-.12f},{s*.12f,.30f,-.52f},q);
    }
    static Hand hand(int side,float t){float dt=.0035f;Vec3 p=handPos(side,t),a=handPos(side,t-dt),b=handPos(side,t+dt);Vec3 v=(b-a)/(2*dt);Vec3 palm=normalized(Vec3(side?-.25f:.25f,-.12f,-1.f));return {p,v,palm};}
    static float segment(Vec3 p,Vec3 a,Vec3 b,Vec3 &foot,Vec3 &tan){Vec3 e=b-a;float f=clamp(dot(p-a,e)/std::max(dot(e,e),1e-7f),0.f,1.f);foot=a+e*f;tan=normalized(e);return length(p-foot);}
    void prepare(double seconds){time=float(seconds);}
    static const char* beat(float t){if(t<1.25f)return "scoop";if(t<2.45f)return "whip";if(t<3.45f)return "redirect";if(t<4.45f)return "slice";if(t<5.65f)return "wave";if(t<7.f)return "vortex";if(t<8.f)return "counter";if(t<9.f)return "slam";return "splash";}
    VelocityGuide sample(Vec3 p,double) const {
        VelocityGuide g;if(time>=releaseEnd||p.y<.02f||p.y>2.7f||std::abs(p.x)>2.35f||std::abs(p.z)>1.85f)return g;
        auto bell=[](float x,float in,float out){x=std::abs(x);if(x>=out)return 0.f;if(x<=in)return 1.f;float q=(x-in)/(out-in);q=q*q*(3-2*q);return 1-q;};
        auto radial=[](Vec3 q){Vec3 r{q.x,0,q.z};float n=length(r);return n>1e-5f?r/n:Vec3(1,0,0);};
        auto tang=[&](Vec3 q,float sign){Vec3 r=radial(q);return Vec3(-r.z,0,r.x)*sign;};
        std::string st=beat(time);Vec3 sum{},acc{};float total=0,rate=0;
        auto add=[&](Vec3 v,float w){if(w>1e-6f){sum+=v*w;total+=w;}};
        Hand L=hand(0,time),R=hand(1,time);
        if(st=="scoop"||st=="whip"||st=="redirect"){
            for(int h=0;h<2;h++){
                Hand H=h?R:L;Vec3 root{h?.28f:-.28f,.18f,.05f},foot,tan;float d=segment(p,root,H.p,foot,tan);
                float tube=bell(d,0,st=="whip"?.18f:.23f)*bell(p.y-.72f,0,1.15f);Vec3 conf=(foot-p)*5.0f;Vec3 desired=H.v+tan*(st=="whip"?2.15f:1.20f)+conf;
                if(st=="whip"&&h==1)desired+=H.palm*3.35f*ease(1.45f,2.30f,time);
                if(st=="redirect")desired+=tang(p-Vec3(0,.85f,0),-1.f)*1.9f;
                add(desired,tube*(h?1.0f:.78f));
            }
            rate=20.f;acc+=Vec3(0,9.81f,0)*std::min(total,1.f);
        }else if(st=="slice"){
            float q=ease(3.45f,4.45f,time);Vec3 mid=(L.p+R.p)*.5f;Vec3 line=normalized(R.p-L.p);Vec3 n=normalized(Vec3(.08f,.22f,1));Vec3 rel=p-mid;float dline=length(rel-line*dot(rel,line));float sheet=bell(dot(rel,n),0,.12f)*bell(dline,0,.88f);Vec3 sweep=(L.v+R.v)*.5f+Vec3(3.6f-7.2f*q,.22f,-.38f);add(sweep-n*dot(rel,n)*16.f,sheet*1.45f);rate=24.f;acc+=Vec3(0,8.5f,0)*sheet;
        }else if(st=="wave"){
            float q=ease(4.45f,5.65f,time);float low=bell(p.y-.26f,0,.58f)*bell(p.x,0,1.65f)*bell(p.z+.05f,0,1.22f);float z=.30f-1.12f*q;float crest=bell(p.z-z,0,.34f)*bell(p.y-.60f,0,.78f)*bell(p.x,0,1.60f);float w=std::max(.72f*low,1.35f*crest);add(Vec3(0,2.1f*crest*(1-.5f*q),-3.0f-1.2f*q),w);rate=17.f;acc+=Vec3(0,4.5f,0)*w;
        }else if(st=="vortex"||st=="counter"){
            Vec3 c{0,.80f,-.20f},qv=p-c;float r=std::sqrt(qv.x*qv.x+qv.z*qv.z);float ann=bell(r-.45f,0,.25f)*bell(p.y-.95f,0,1.08f);float sign=st=="vortex"?1.f:-1.f;Vec3 v=-radial(qv)*(st=="vortex"?1.45f:1.85f)+tang(qv,sign)*(st=="vortex"?3.75f:3.35f)+Vec3(st=="counter"?.55f:0,st=="vortex"?1.8f:.15f,st=="counter"?-.65f:0);add(v,ann*1.55f);rate=21.f;acc+=Vec3(0,9.81f,0)*ann;
        }else if(st=="slam"){
            float q=ease(8.0f,8.82f,time);float body=bell(p.y-.88f,0,1.18f)*bell(p.x,0,1.62f)*bell(p.z+.12f,0,1.18f);float rel=1-ease(8.72f,9.f,time);add(Vec3(.1f*(1-q),-2.75f-2.1f*q,-1.45f-1.55f*q),body*rel*1.55f);rate=22.f;acc+=Vec3(0,-5.8f*q,0)*body*rel;
        }
        if(total<=1e-6f)return VelocityGuide{};g.target=limit(sum/std::max(total,1.f),5.8f);g.rate=rate*std::min(1.f,total);g.acceleration=limit(acc/std::max(total,1.f),58.f);g.accelerationLimit=88.f;return g;
    }
};
}} // namespace water::choreography
