#include "../src/gallery_scenes.hpp"
#include <iomanip>
using namespace water;
int main(){try{
    std::cout<<std::setprecision(12)<<"{\"passed\":true,\"scope\":\"Short coarse numerical smoke tests, not visual qualification\",\"scenes\":[";
    bool first=true;
    for(const std::string &name:{"beach","waterbending","drop","whirlpool","rockimpact","jets","cascade","waterbending_trick"}){
        auto preset=gallery::makePreset(name);Config c;c.dx=.09f*preset.geometryScale;c.threads=1;c.apic=true;c.whitewater=true;c.surfaceTension=.072f;c.reseedInterval=0;
        Solver solver(preset.scene,c);solver.initialize();solver.advanceTo(.12);
        if(!std::isfinite(solver.stats.pressureRelativeResidual)||!std::isfinite(solver.stats.volume)||std::abs(solver.stats.massLedgerError)>1e-7||solver.particles.empty())throw std::runtime_error("Scene failed: "+name);
        for(const auto &p:solver.particles)if(!finite(p.p)||!finite(p.v))throw std::runtime_error("Nonfinite state: "+name);
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"name\":\""<<name<<"\",\"dx_m\":"<<c.dx<<",\"time_s\":"<<solver.time<<",\"particles\":"<<solver.particles.size()<<",\"volume_ledger_error_m3\":"<<solver.stats.massLedgerError<<",\"pressure_relative_residual\":"<<solver.stats.pressureRelativeResidual<<",\"finite\":true}";
    }
    std::cout<<"]}\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
