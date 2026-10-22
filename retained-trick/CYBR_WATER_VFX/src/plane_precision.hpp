// Compensated plane distance for platforms where long double is only Float64.
// Error-free TwoSum and explicit std::fma product residuals retain cancellation
// information. This is a finite-range double-double fallback, not arbitrary precision.
#pragma once
#include <array>
#include <cmath>
#include <limits>

namespace cybr_precision {
struct DD {
    double hi, lo;
    DD(double h=0, double l=0):hi(h),lo(l){}
};
inline DD twoSum(double a,double b) {
    const double s=a+b, v=s-a;
    return {s,(a-(s-v))+(b-v)};
}
inline DD add(DD a,DD b) {
    const DD s=twoSum(a.hi,b.hi), t=twoSum(a.lo,b.lo);
    const DD u=twoSum(s.hi,s.lo+t.hi);
    return twoSum(u.hi,u.lo+t.lo);
}
inline DD sub(DD a,DD b) { return add(a,{-b.hi,-b.lo}); }
inline DD mul(DD a,DD b) {
    const double p=a.hi*b.hi;
    const double error=std::fma(a.hi,b.hi,-p);
    DD result=add({p,error},twoSum(a.hi*b.lo,a.lo*b.hi));
    return add(result,{a.lo*b.lo});
}
inline double planeDistance(const std::array<double,3>& a,const std::array<double,3>& b,
                            const std::array<double,3>& c,const std::array<double,3>& origin,
                            const std::array<double,3>& direction) {
    std::array<DD,3> e,f,p,n;
    for(int i=0;i<3;++i) { e[i]=sub(b[i],a[i]);f[i]=sub(c[i],a[i]);p[i]=sub(a[i],origin[i]); }
    for(int i=0;i<3;++i) { const int j=(i+1)%3,k=(i+2)%3;n[i]=sub(mul(e[j],f[k]),mul(e[k],f[j])); }
    DD numerator,denominator;
    for(int i=0;i<3;++i) { numerator=add(numerator,mul(n[i],p[i]));denominator=add(denominator,mul(n[i],direction[i])); }
    if(denominator.hi==0 && denominator.lo==0)return std::numeric_limits<double>::quiet_NaN();
    const double q=numerator.hi/denominator.hi;
    const DD residual=sub(numerator,mul(denominator,q));
    return q+(residual.hi+residual.lo)/(denominator.hi+denominator.lo);
}
} // namespace cybr_precision
