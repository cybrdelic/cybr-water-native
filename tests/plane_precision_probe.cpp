#include "plane_precision.hpp"
#include <array>
#include <iomanip>
#include <iostream>
int main() {
    std::array<double,3> a,b,c,o,d;
    while(std::cin>>a[0]) {
        std::cin>>a[1]>>a[2];
        for(auto* v:{&b,&c,&o,&d})for(double& x:*v)std::cin>>x;
        if(!std::cin)return 2;
        std::cout<<std::setprecision(17)<<cybr_precision::planeDistance(a,b,c,o,d)<<'\n';
    }
}
