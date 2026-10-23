#include "../src/cinematic_choreography.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace water;
using G=water::choreography::DuetGuide;
static G::Hit reference(const G &g,Vec3 p,int branch){
    G::Hit h;
    for(int i=0;i<G::segments;i++){
        const auto &a=g.knots[branch][i],&b=g.knots[branch][i+1];
        Vec3 e=b.p-a.p;float f=clamp(dot(p-a.p,e)/std::max(dot(e,e),1e-9f),0.f,1.f);
        Vec3 foot=a.p+e*f;float d2=dot(p-foot,p-foot);
        if(d2<h.d2){
            h.d2=d2;h.k.p=foot;h.k.v=G::lerp(a.v,b.v,f);h.k.a=G::lerp(a.a,b.a,f);
            h.k.tangent=normalized(G::lerp(a.tangent,b.tangent,f));
            h.k.curvature=G::lerp(a.curvature,b.curvature,f);
            h.k.thin=normalized(G::lerp(a.thin,b.thin,f));h.k.circulation=a.circulation*(1-f)+b.circulation*f;
        }
    }return h;
}
static bool equal(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
int main(){try{
    G g;uint64_t count=0;double error=0;std::vector<Vec3> queries;
    for(int i=0;i<5000;i++)queries.push_back({4.f*random01(3*i)-2.f,2.5f*random01(3*i+1),2.7f*random01(3*i+2)-1.35f});
    for(float t: {0.f,.2f,.9f,1.65f,2.1f,2.6f,3.2f,3.65f,4.32f,4.38f,4.58f,5.10f,5.30f,5.55f,5.75f,6.48f,6.68f,7.32f,7.8f,8.35f,8.66f,8.95f,10.f}){
        g.prepare(t);
        for(int b=0;b<2;b++)for(Vec3 p:queries){
            auto r=reference(g,p,b),s=g.nearest(p,b);count++;
            if(r.d2!=s.d2||!equal(r.k.p,s.k.p)||!equal(r.k.v,s.k.v)||!equal(r.k.a,s.k.a)||!equal(r.k.tangent,s.k.tangent)||!equal(r.k.curvature,s.k.curvature)||!equal(r.k.thin,s.k.thin)||r.k.circulation!=s.k.circulation)throw std::runtime_error("accelerated query differs from full-segment oracle");
        }
        for(int b=0;b<2;b++)for(const auto &k:g.knots[b]){
            auto r=reference(g,k.p,b),s=g.nearest(k.p,b);if(r.d2!=s.d2||!equal(r.k.p,s.k.p))throw std::runtime_error("knot/closure tie regression");count++;
        }
    }
    std::cout<<"PASS "<<count<<" exact nearest-segment comparisons, all interpolated fields identical\n";
    g.prepare(3.1);volatile float sink=0;
    auto start=std::chrono::steady_clock::now();for(int k=0;k<4;k++)for(Vec3 p:queries)for(int b=0;b<2;b++)sink+=reference(g,p,b).d2;
    auto middle=std::chrono::steady_clock::now();for(int k=0;k<4;k++)for(Vec3 p:queries)for(int b=0;b<2;b++)sink+=g.nearest(p,b).d2;
    auto end=std::chrono::steady_clock::now();
    std::cout<<"oracle_seconds="<<std::chrono::duration<double>(middle-start).count()<<" accelerated_seconds="<<std::chrono::duration<double>(end-middle).count()<<" checksum="<<sink<<'\n';
    return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
