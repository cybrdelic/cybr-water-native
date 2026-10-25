#pragma once

// Additional conditions for the SAME water::Solver, surface reconstruction,
// secondary-particle evolution, and dielectric tracer. This file defines scene
// fields only. No scene-specific liquid solver or animated render geometry.
// Metres, seconds, +y up; the ordinary Config gravity remains (0,-9.81,0).
#include "fluid.hpp"
#include "cinematic_choreography.hpp"
#include "scene.hpp" // Reuse the frozen fractured-rock primitive, not its scene.
#include <memory>

namespace water {
namespace gallery {

constexpr float pi = 3.14159265358979323846f;
inline float saturated(float x) { return std::clamp(x, 0.f, 1.f); }
inline float smooth(float a, float b, float x) {
    float t = saturated((x-a)/(b-a));
    return t*t*(3.f-2.f*t);
}

// The zero set is the exported heightfield; the value is a local normal-distance
// approximation. This is not a globally exact Euclidean signed distance.
struct Geometry {
    std::function<float(float,float)> height;
    std::vector<hero::Rock> rocks;
    uint32_t groundMaterial = 2; // CYBRMSH1: 1 rock, 2 sand, 3 moss.
    float normalStep=.0025f, distanceStep=.008f;

    Vec3 normal(float x, float z, float e=0.f) const {
        if(e<=0)e=normalStep;
        float hx=(height(x+e,z)-height(x-e,z))/(2.f*e);
        float hz=(height(x,z+e)-height(x,z-e))/(2.f*e);
        return normalized(Vec3(-hx,1.f,-hz));
    }
    float bedSdf(Vec3 p) const {
        float e=distanceStep;
        float hx=(height(p.x+e,p.z)-height(p.x-e,p.z))/(2.f*e);
        float hz=(height(p.x,p.z+e)-height(p.x,p.z-e))/(2.f*e);
        return (p.y-height(p.x,p.z))/std::sqrt(1.f+hx*hx+hz*hz);
    }
    float solidSdf(Vec3 p) const {
        float result=bedSdf(p);
        for (const auto &rock:rocks) {
            Vec3 q=p-rock.center;
            float outside=std::max({std::abs(q.x),std::abs(q.y),std::abs(q.z)})-rock.bound;
            if (outside>std::max(.05f,result)) continue;
            result=std::min(result,rock.sdf(p));
        }
        return result;
    }
};

struct Preset {
    Scene scene;
    std::shared_ptr<const Geometry> geometry;
    Vec3 camera, target;
    float verticalFov=23.f, referenceWaterLevel=0;
    float geometryScale=1.f;
    std::string materialNotes, forceNotes, timingNotes;
};

inline float beachHeight(float x, float z) {
    // A resolved gently concave sand ramp; shoaling is caused by bathymetry.
    // Cross-shore level rises 0.61 m over 2.6 m, into genuinely dry upper beach.
    float shorelineOffset=.065f*std::sin(2.1f*z)+.028f*std::sin(5.3f*z+.6f);
    float q=x-shorelineOffset;
    float ramp=.55f*smooth(-1.24f,1.28f,q)+.060f*smooth(.66f,1.45f,q);
    float low=.006f*(hero::noise(Vec3(3.8f*x,9.3f,3.8f*z))-.5f);
    // Millimetre sand ripple geometry belongs to the solid bed, never water.
    float ripple=.0016f*std::sin(67.f*x+2.3f*std::sin(3.f*z));
    return -.024f+ramp+low+ripple;
}
inline float beachCrest(float x, float z) {
    float centre=-.92f+.055f*std::sin(2.8f*z+.3f);
    float amplitude=.137f*(.95f+.05f*std::cos(3.2f*z));
    float q=(x-centre)/.39f;
    float ch=std::cosh(std::clamp(q,-18.f,18.f));
    return amplitude/(ch*ch);
}
inline float beachSurface(float x, float z) { return .405f+beachCrest(x,z); }
inline float beachU(float x, float z) {
    float depth=std::max(.035f,.405f-beachHeight(x,z));
    float crest=beachCrest(x,z);
    // A solitary-wave-inspired *initial condition*, not an analytic claim that
    // this finite, laterally varying crest is an exact nonlinear wave solution.
    float speed=std::sqrt(9.81f*(depth+.137f));
    return speed*crest/(depth+crest);
}

inline Preset beachPreset() {
    auto geometry=std::make_shared<Geometry>();
    geometry->height=beachHeight;
    geometry->groundMaterial=2;
    auto rock=[&](float x,float z,Vec3 size,uint32_t seed) {
        geometry->rocks.push_back(hero::makeRock(
            Vec3(x,beachHeight(x,z)+.26f*size.y,z),size,seed,
            std::max({size.x,size.y,size.z})>.12f,false));
    };
    // Peripheral outcrops and a sparse upper-shore pebble line leave the crest
    // unobstructed. They are actual collision objects, not reflection cards.
    rock(-1.12f,-.87f,{.18f,.15f,.19f},17011);
    rock(.53f,.91f,{.25f,.20f,.22f},17039);
    rock(1.10f,-.86f,{.20f,.13f,.24f},17077);
    for (uint32_t i=0;i<15;i++) {
        uint32_t seed=17401+i*97;
        float x=.83f+.69f*hero::random(seed);
        float z=-1.10f+2.20f*hero::random(seed+1);
        float r=.016f+.041f*std::pow(hero::random(seed+2),1.8f);
        rock(x,z,{r,r*.48f,r*.85f},seed);
    }
    Preset preset;
    preset.geometry=geometry;
    auto &scene=preset.scene;
    scene.name="pocket_beach_shoaling_crest";
    scene.lower={-1.5f,-.15f,-1.f}; scene.upper={1.5f,1.45f,1.f};
    // Shared-engine frictionless numerical boundaries, ordered x-/x+, y-/y+,
    // z-/z+. These prevent the finite water window draining sideways/upstream;
    // top and the genuinely dry upper shore retain ordinary open conditions.
    scene.closedBoundaries={true,false,false,false,true,true};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){
        return std::max(p.y-beachSurface(p.x,p.z),-.145f-p.y);
    };
    scene.initialVelocity=[](Vec3 p){
        constexpr float e=.006f;
        float u=beachU(p.x,p.z);
        float ux=(beachU(p.x+e,p.z)-beachU(p.x-e,p.z))/(2.f*e);
        float hx=(beachHeight(p.x+e,p.z)-beachHeight(p.x-e,p.z))/(2.f*e);
        float d=std::max(0.f,p.y-beachHeight(p.x,p.z));
        // w=u*h_x-(y-h)*u_x gives bottom tangency and local x/y continuity.
        return Vec3(u,u*hx-d*ux,0);
    };
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    // One finite incoming crest. No perpetual source silently increases volume.
    preset.camera={1.08f,1.20f,2.44f};
    preset.target={.03f,.34f,0}; preset.verticalFov=25.f;
    preset.referenceWaterLevel=.405f;
    preset.materialNotes="Pale mineral sand; darker genuinely submerged bed; sparse fractured shore stones. Broad coastal sky with a darker landward reflection hemisphere. No sand-coloured water absorption.";
    preset.forceNotes="Ordinary gravity only. A 137 mm initial crest and shallow-water-inspired velocity evolve through the common pressure solver over the rising bed. A breaking crest is an outcome to verify, not pre-authored geometry.";
    preset.timingNotes="Inspect t=0.25 to 0.85 s. This is a finite 3 m coastal patch with frictionless closed upstream and side boundaries; top and the dry upper-shore edge remain open. The reflected finite-domain condition is explicit, not an infinite spectral-ocean model. Longer takes require appropriate extended-domain or boundary-wave treatment.";
    return preset;
}

inline float bendingBedHeight(float x,float z) {
    float r=std::sqrt((x/.94f)*(x/.94f)+(z/.74f)*(z/.74f));
    float theta=std::atan2(z/.74f,x/.94f);
    float shoreline=.071f*std::sin(3.f*theta+.47f)+.037f*std::cos(5.f*theta-.8f)
                    +.025f*(hero::noise(Vec3(x*8.3f,7.2f,z*8.3f))-.5f);
    float erosion=.013f*(hero::fbm(Vec3(x*4.2f,4.7f,z*4.2f))-.5f)
                  +.003f*(hero::noise(Vec3(x*31.f,2.4f,z*31.f))-.5f);
    return .026f+.42f*smooth(.56f,1.57f,r+shoreline)+erosion;
}

// A tilted elliptical force guide, in a vertical plane. Its geometry is used
// only to initialize markers and evaluate an external acceleration. Rendering
// uses the actual solved particles; it never exports this ideal guide as water.
struct LoopGuide {
    Vec3 center{0,.755f,0}, horizontal{1,0,0};
    Vec3 vertical{0,.9850305f,.1723803f}, normal{0,-.1723803f,.9850305f};
    float rx=.590f, ry=.505f, angularSpeed=3.30f, radius=.071f;
    Vec3 local(Vec3 p) const {
        Vec3 q=p-center;
        return {dot(q,horizontal),dot(q,vertical),dot(q,normal)};
    }
    float angle(Vec3 p) const { Vec3 q=local(p);return std::atan2(q.y/ry,q.x/rx); }
    Vec3 point(float theta) const {
        return center+horizontal*(rx*std::cos(theta))+vertical*(ry*std::sin(theta));
    }
    Vec3 velocity(float theta) const {
        return (horizontal*(-rx*std::sin(theta))+vertical*(ry*std::cos(theta)))*angularSpeed;
    }
    float distance(Vec3 p) const { return length(p-point(angle(p))); }
    Vec3 acceleration(Vec3 p,double) const {
        Vec3 q=local(p);
        float theta=std::atan2(q.y/ry,q.x/rx);
        float radial=std::sqrt((q.x/rx)*(q.x/rx)+(q.y/ry)*(q.y/ry));
        float influence=1.f-smooth(.105f,.235f,distance(p));
        // A left-lower release region lets the descending stream lose support
        // and fall into the basin. No mesh deletion or particle teleportation.
        float release=(theta+2.12f)/.26f;
        influence*=1.f-.97f*std::exp(-release*release);
        influence*=smooth(.125f,.245f,p.y);
        Vec3 outward=normalized(horizontal*(q.x/(rx*rx))+vertical*(q.y/(ry*ry)));
        float omega2=angularSpeed*angularSpeed;
        Vec3 force=horizontal*(-omega2*q.x)+vertical*(-omega2*q.y);
        force-=outward*(45.f*(radial-1.f)*.5475f);
        force-=normal*(60.f*q.z);
        force+=normalized(velocity(theta))*.18f;
        // The callback adds to gravity, so +9.81 explicitly supplies levitation.
        force=(force+Vec3(0,9.81f,0))*influence;
        float magnitude=length(force);
        return magnitude>32.f?force*(32.f/magnitude):force;
    }
};

inline Preset waterbendingPreset() {
    auto geometry=std::make_shared<Geometry>();
    geometry->height=bendingBedHeight; geometry->groundMaterial=1;
    auto rock=[&](float x,float z,Vec3 size,uint32_t seed) {
        geometry->rocks.push_back(hero::makeRock(
            Vec3(x,bendingBedHeight(x,z)+.30f*size.y,z),size,seed,true,false));
    };
    rock(.97f,.59f,{.22f,.16f,.21f},23003);
    rock(-1.01f,-.49f,{.23f,.20f,.18f},23027);
    rock(-.79f,.80f,{.19f,.10f,.21f},23059);
    LoopGuide guide;
    Preset preset;
    preset.geometry=geometry;
    auto &scene=preset.scene;
    scene.name="externally_forced_waterbending_loop";
    scene.lower={-1.5f,-.12f,-1.f}; scene.upper={1.5f,1.48f,1.f};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[guide](Vec3 p){
        float basin=std::max(p.y-.205f,-.115f-p.y);
        float theta=guide.angle(p);
        float arc=std::max({guide.distance(p)-guide.radius,
                           (-1.61f-theta)*.54f,(theta+.15f)*.54f});
        return std::min(basin,arc);
    };
    scene.initialVelocity=[guide](Vec3 p){
        float theta=guide.angle(p);
        if (theta>=-1.70f && theta<=-.08f && guide.distance(p)<.13f)
            return guide.velocity(theta);
        return Vec3(0,0,0);
    };
    scene.bodyAcceleration=[guide](Vec3 p,double t){return guide.acceleration(p,t);};
    Inlet inlet;
    inlet.lower={-.13f,.184f,-.151f}; inlet.upper={.03f,.316f,-.008f};
    inlet.velocity={guide.rx*guide.angularSpeed,0,0};
    inlet.axis=0; inlet.direction=1; inlet.rateScale=1;
    scene.inlets.push_back(inlet);
    preset.camera={1.18f,1.90f,3.20f}; preset.target={0,.70f,0};
    preset.verticalFov=22.f; preset.referenceWaterLevel=.205f;
    preset.materialNotes="Dark warm bare rock basin; restrained back/side light and dark reflection regions. Leave a readable air gap behind the rising clear stream. Background and solids stay physically present in reflections.";
    preset.forceNotes="Declared fantasy forcing: elliptical harmonic acceleration, bounded radial and plane confinement, local gravity cancellation, and 0.18 m/s^2 tangential drive, capped at 32 m/s^2 total additional acceleration. The left-lower field tapers so returning liquid falls into the basin. The common solver still computes pressure, advection, collisions and secondary particles.";
    preset.timingNotes="Inspect t=0.35 to 1.8 s. Only the first ascending arc is initialized; the remainder must form by transport. Inlet volume is reported by the common ledger. This force field is not an unforced natural-water claim or a surface-tension/soap-film model.";
    return preset;
}

inline float dropBowlHeight(float x,float z) {
    float radius=std::sqrt(x*x+z*z);
    float theta=std::atan2(z,x);
    float rimRadius=radius+.0011f*std::sin(3.f*theta+.6f)+.0007f*std::cos(5.f*theta);
    float basin=.0008f*std::pow(std::min(radius/.069f,1.f),4.f);
    float rim=.037f*smooth(.057f,.082f,rimRadius);
    float mineral=.00011f*(hero::noise(Vec3(x*820.f,3.7f,z*820.f))-.5f);
    return basin+rim+mineral;
}

inline Preset dropImpactPreset() {
    auto geometry=std::make_shared<Geometry>();
    geometry->height=dropBowlHeight;geometry->groundMaterial=1;
    geometry->normalStep=.000125f;geometry->distanceStep=.0004f;
    Preset preset; preset.geometry=geometry;preset.geometryScale=.05f;
    auto &scene=preset.scene;
    scene.name="macro_drop_early_inertial_impact";
    scene.lower={-.080f,-.010f,-.080f};scene.upper={.080f,.136f,.080f};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    // 28 mm diameter water drop over an 18 mm pool. Its initial surface is
    // prescribed only at t=0; all later impact/crown geometry is solved liquid.
    scene.initialLiquidSdf=[](Vec3 p){
        float pool=std::max(p.y-.018f,-.009f-p.y);
        float drop=length(p-Vec3(.0018f,.096f,-.0010f))-.014f;
        return std::min(pool,drop);
    };
    scene.initialVelocity=[](Vec3){return Vec3(0,0,0);};
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    preset.camera={.139f,.125f,.220f};preset.target={0,.027f,0};
    preset.verticalFov=24.f;preset.referenceWaterLevel=.018f;
    preset.materialNotes="Close macro view into a small eroded bare-stone bowl. Clear water and broad side/back reflections; keep the crown silhouette distinct from the submerged floor. Millimetre geometry uses the same metre-based optics.";
    preset.forceNotes="Ordinary gravity only. The 14 mm-radius drop falls from rest into an 18 mm-deep pool. This is an early inertia/gravity impact test: no resolved capillary pressure, microscopic air cushion, or prediction of a true late capillary crown.";
    preset.timingNotes="Use solver dx=0.0015 to 0.002 m. Inspect approximately t=0.11 to 0.18 s, with a selected state near 0.15 s. Sample the matching solid field at <=0.001 m; use about 0.00003 m liquid/opaque-solid contact overlap. Reject numerical breakup masquerading as capillary droplets.";
    return preset;
}

inline float whirlpoolBedHeight(float x,float z) {
    float theta=std::atan2(z,x),radius=std::sqrt(x*x+z*z);
    float warped=radius+.016f*std::sin(3.f*theta+.6f)+.009f*std::cos(5.f*theta-1.f);
    return .006f+.32f*smooth(.305f,.568f,warped)
           +.004f*(hero::fbm(Vec3(x*8.3f,8.1f,z*8.3f))-.5f)
           +.0013f*(hero::noise(Vec3(x*47.f,3.1f,z*47.f))-.5f);
}

inline Preset whirlpoolPreset() {
    auto geometry=std::make_shared<Geometry>();
    geometry->height=whirlpoolBedHeight;geometry->groundMaterial=1;
    auto rock=[&](float x,float z,Vec3 size,uint32_t seed){
        geometry->rocks.push_back(hero::makeRock(
            Vec3(x,whirlpoolBedHeight(x,z)+.24f*size.y,z),size,seed,
            std::max({size.x,size.y,size.z})>.10f,false));
    };
    rock(-.48f,-.23f,{.16f,.11f,.14f},27011);
    rock(.34f,.44f,{.15f,.09f,.14f},27031);
    rock(.49f,-.20f,{.13f,.12f,.11f},27061);
    rock(.24f,-.12f,{.042f,.021f,.031f},27091);
    rock(-.17f,.24f,{.024f,.013f,.035f},27119);
    Preset preset;preset.geometry=geometry;preset.geometryScale=.55f;
    auto &scene=preset.scene;
    scene.name="released_rotation_rock_basin";
    scene.lower={-.60f,-.030f,-.60f};scene.upper={.60f,.495f,.60f};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){return std::max(p.y-.168f,-.029f-p.y);};
    scene.initialVelocity=[](Vec3 p){constexpr float omega=4.f;return Vec3(-omega*p.z,0,omega*p.x);};
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    // The actual raised rock bed confines the pool; there is no drain or
    // invisible continuously applied vortex force, and no special solver.
    preset.camera={.73f,1.06f,1.18f};preset.target={0,.125f,0};
    preset.verticalFov=29.f;preset.referenceWaterLevel=.168f;
    preset.materialNotes="Natural asymmetric bare-rock basin with a visible submerged pebble bed and a changing central surface depression. Oblique overhead camera; darker environment reflection behind the brighter outer rim.";
    preset.forceNotes="Initial solid-body rotation v=(-4*z,0,4*x) m/s, then ordinary gravity and the common incompressible solver only. Pressure and the actual asymmetric basin determine the developing whirlpool. There is no bottom drain, forced persistent circulation, or separately solved air core.";
    preset.timingNotes="Use solver dx about 0.015 m and inspect t=0.4 to 1.0 s. A selected state near 0.7 s should show the developing depression and waves if the simulated response supports it. The released rotation can oscillate and decay; do not call it a steady draining vortex.";
    return preset;
}

// A released dam column, not a moving obstacle or an authored breaking wave.
inline float rockImpactBedHeight(float x,float z) {
    float channel=z-.023f*std::sin(3.1f*x);
    float sides=.74f*smooth(.55f,.91f,std::abs(channel));
    float farBank=.58f*smooth(1.07f,1.54f,x);
    float grain=.003f*(hero::noise(Vec3(x*29.f,4.2f,z*29.f))-.5f);
    return .019f+sides+farBank+grain;
}
inline Preset rockImpactPreset() {
    auto geometry=std::make_shared<Geometry>();
    geometry->height=rockImpactBedHeight;geometry->groundMaterial=1;
    geometry->rocks.push_back(hero::makeRock({.11f,.267f,.015f},{.30f,.39f,.285f},31013,true,false));
    geometry->rocks.push_back(hero::makeRock({.71f,.12f,-.48f},{.20f,.16f,.17f},31039,true,false));
    geometry->rocks.push_back(hero::makeRock({-.13f,.16f,.64f},{.22f,.22f,.16f},31069,true,false));
    Preset preset;preset.geometry=geometry;
    auto &scene=preset.scene;scene.name="dam_release_fractured_boulder_impact";
    scene.lower={-1.5f,-.06f,-.9f};scene.upper={1.5f,1.40f,.9f};
    scene.closedBoundaries={true,false,false,false,true,true};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){
        float pool=std::max(p.y-.10f,-.055f-p.y);
        float reservoir=std::max({p.y-.75f,-.055f-p.y,-1.28f-p.x,p.x+.50f,std::abs(p.z)-.49f});
        return std::min(pool,reservoir);
    };
    scene.initialVelocity=[](Vec3 p){return p.x<-.50f&&p.y>.10f?Vec3(.55f,0,0):Vec3(0,0,0);};
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    preset.camera={1.0f,1.55f,2.95f};preset.target={-.08f,.32f,0};
    preset.verticalFov=31.f;preset.referenceWaterLevel=.10f;
    preset.materialNotes="Fractured fixed central boulder in a bare rock channel. Frame the incident water column, impact face and both side channels together; no animated or fractured rigid-body solver is claimed.";
    preset.forceNotes="Finite dam-release initial condition: 0.75 m water level upstream with 0.55 m/s initial forward velocity above the 0.10 m receiving pool. Gravity, pressure and the shared static-rock collision field drive the impact. No inlet, time-dependent force or authored splash.";
    preset.timingNotes="Use dx=0.022 m. Capture 48 actual states at60 Hz, frames27 through74 inclusive (0.45 to1.233333333 s). Inspect impact, split flow and downstream wave. Raised physical banks and closed upstream/lateral slip faces confine this finite channel; it is not a deforming-rock destruction simulation.";
    return preset;
}

inline float jetBasinHeight(float x,float z) {
    float shelf=.82f*((1.f-smooth(-1.06f,-.90f,x))+smooth(.90f,1.06f,x));
    float bank=.42f*smooth(.52f,.86f,std::abs(z+.017f*std::sin(4.f*x)));
    float grain=.0025f*(hero::noise(Vec3(x*31.f,2.5f,z*31.f))-.5f);
    return .018f+shelf+bank+grain;
}
inline Preset collidingJetsPreset() {
    auto geometry=std::make_shared<Geometry>();geometry->height=jetBasinHeight;geometry->groundMaterial=1;
    geometry->rocks.push_back(hero::makeRock({-.81f,.28f,-.66f},{.20f,.20f,.16f},35011,true,false));
    geometry->rocks.push_back(hero::makeRock({.76f,.29f,.65f},{.20f,.19f,.16f},35039,true,false));
    Preset preset;preset.geometry=geometry;auto &scene=preset.scene;
    scene.name="opposed_liquid_jets_over_rock_basin";
    scene.lower={-1.35f,-.06f,-.88f};scene.upper={1.35f,1.43f,.88f};
    scene.closedBoundaries={false,false,false,false,true,true};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){
        float pool=std::max(p.y-.165f,-.055f-p.y);
        float left=std::max({p.y-1.f,.86f-p.y,-1.13f-p.x,p.x+.97f,std::abs(p.z)-.08f});
        float right=std::max({p.y-1.f,.86f-p.y,.97f-p.x,p.x-1.13f,std::abs(p.z)-.08f});
        return std::min({pool,left,right});
    };
    scene.initialVelocity=[](Vec3 p){return p.y>.70f?Vec3(p.x<0?3.2f:-3.2f,.35f,0):Vec3(0,0,0);};
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    Inlet left;left.lower={-1.12f,.86f,-.08f};left.upper={-.94f,1.f,.08f};left.velocity={3.2f,.35f,0};left.axis=0;left.direction=1;
    Inlet right;right.lower={.94f,.86f,-.08f};right.upper={1.12f,1.f,.08f};right.velocity={-3.2f,.35f,0};right.axis=0;right.direction=-1;
    scene.inlets={left,right};
    preset.camera={.76f,1.61f,3.45f};preset.target={0,.56f,0};
    preset.verticalFov=28.f;preset.referenceWaterLevel=.165f;
    preset.materialNotes="Opposed elevated rock spillways frame two clear pressurized slot jets. The central collision and out-of-plane sheet sit above a receiving pool. Use natural daylight with a darker distant reflection region behind the clear sheet.";
    preset.forceNotes="Two ordinary shared-core rectangular inlets: opposing3.2 m/s horizontal flow plus0.35 m/s initial upward velocity. Each source has a0.14 by0.16 m section above a real rock shelf. Gravity and pressure determine their intersection and sheet breakup; no separate jet solver, circular-nozzle model or invisible levitation force.";
    preset.timingNotes="Use dx=0.022 m. Capture48 actual states at24 Hz, frames9 through56 inclusive (0.375 to2.333333333 s). Initial liquid occupies only the receiving pool and short source blocks; the collision develops by transport. Each inlet continues throughout the take, with open-grid flux and any outflow in the common volume ledger. Set max primary markers at least1.5 million.";
    return preset;
}

inline float cascadeBedHeight(float x,float z) {
    float steps=.32f*(1.f-smooth(-.84f,-.74f,x))+.28f*(1.f-smooth(-.14f,-.04f,x))+.30f*(1.f-smooth(.51f,.61f,x));
    float center=.028f*std::sin(2.8f*x);
    float bank=.49f*smooth(.47f,.88f,std::abs(z-center));
    float downstream=.31f*smooth(1.19f,1.56f,x);
    float rough=.009f*(hero::fbm(Vec3(x*7.3f,6.1f,z*7.3f))-.5f);
    return .035f+steps+bank+downstream+rough;
}
inline Preset cascadePreset() {
    auto geometry=std::make_shared<Geometry>();geometry->height=cascadeBedHeight;geometry->groundMaterial=1;
    auto rock=[&](float x,float z,Vec3 radius,uint32_t seed){geometry->rocks.push_back(hero::makeRock({x,cascadeBedHeight(x,z)+.24f*radius.y,z},radius,seed,true,false));};
    rock(-.50f,-.52f,{.22f,.20f,.17f},39011);
    rock(.14f,.51f,{.21f,.16f,.18f},39031);
    rock(.82f,-.50f,{.22f,.16f,.20f},39061);
    Preset preset;preset.geometry=geometry;auto &scene=preset.scene;
    scene.name="three_step_rock_cascade_into_pool";
    scene.lower={-1.45f,-.07f,-.9f};scene.upper={1.6f,1.50f,.9f};
    scene.closedBoundaries={false,false,false,false,true,true};
    scene.solidSdf=[geometry](Vec3 p){return geometry->solidSdf(p);};
    scene.initialLiquidSdf=[](Vec3 p){
        float upper=std::max({p.y-1.065f,.925f-p.y,-1.39f-p.x,p.x+.87f,std::abs(p.z)-.38f});
        float pool=std::max({p.y-.205f,-.065f-p.y,.64f-p.x});
        return std::min(upper,pool);
    };
    scene.initialVelocity=[](Vec3 p){return p.x<-.84f?Vec3(1.6f,0,0):Vec3(0,0,0);};
    scene.bodyAcceleration=[](Vec3,double){return Vec3(0,0,0);};
    Inlet inlet;inlet.lower={-1.40f,.945f,-.35f};inlet.upper={-1.21f,1.065f,.35f};inlet.velocity={1.6f,0,0};inlet.axis=0;inlet.direction=1;scene.inlets={inlet};
    preset.camera={1.68f,1.65f,3.15f};preset.target={-.03f,.52f,0};
    preset.verticalFov=31.f;preset.referenceWaterLevel=.205f;
    preset.materialNotes="Three eroded rock shelves form separate falls of0.32,0.28 and0.30 m into a lower pool, with peripheral fractured stones. Use an oblique downstream view that reveals each drop and the actual wet bed.";
    preset.forceNotes="Ordinary gravity with one1.6 m/s upper inlet. Only the upstream short reservoir and lower pool are initialized; intermediate shelves begin dry. All later sheets, impacts and connecting flow are produced by the shared pressure/advection/collision core.";
    preset.timingNotes="Use dx=0.022 m. Capture48 actual states at24 Hz, frames15 through62 inclusive (0.625 to2.583333333 s). The leading flow reaches successive shelves during the take. Real banks contain the lower pool; continuing inlet/outflow is recorded. Set max primary markers at least1.5 million. This is three resolved falls, not an authored waterfall surface.";
    return preset;
}


// Full ten-second choreography: guidance is a body acceleration on the fluid,
// not render geometry. All transported liquid, pressure, contacts, and breakup
// remain owned by the common MAC/APIC solver.
struct LegacyTrickGuide {
    struct Strength { float attraction, flow, curl, lift, cap; };
    static float phase(float t,float a,float b){return smooth(a,b,t);}
    static float bell(float t,float a,float b,float c,float d){return phase(t,a,b)*(1-phase(t,c,d));}
    static Vec3 curve(int branch,float u,float t){
        float side=branch==0?-1.f:1.f;
        float split=phase(t,1.1f,2.7f);
        float helix=bell(t,2.5f,3.0f,4.45f,5.0f);
        float whip=bell(t,4.4f,4.9f,5.95f,6.3f);
        float ring=phase(t,6.05f,6.6f)*(1-phase(t,7.5f,8.0f));
        float y=.19f+1.15f*u;
        float spread=side*(.15f+.33f*u*split);
        Vec3 gather{spread+side*.08f*std::sin(3.141593f*u),y,
                    side*(.09f*u+.06f*std::sin(3.141593f*u))};
        float theta=2.0f*3.1415926536f*(1.35f*u+.65f*phase(t,2.6f,4.6f))+(branch?3.141593f:0.f);
        Vec3 crossCurve{.33f*std::cos(theta)*u,y,
                        .27f*std::sin(theta)*u};
        float q=phase(t,4.6f,6.1f);
        Vec3 whipCurve{-.08f+1.35f*u*q,.30f+(.95f-.22f*q)*std::sin(1.57f*u),
                       side*.12f*u+.13f*std::sin(5.f*u+q*3.14f)};
        float phi=3.1415926536f*(-.5f+1.85f*u)+.30f*side;
        Vec3 ringCurve{.46f*std::cos(phi),.83f+.50f*std::sin(phi),
                       .14f*std::sin(phi)+.09f*side};
        Vec3 out=gather*(1-helix)+crossCurve*helix;
        if(branch==0)out=out*(1-whip)+whipCurve*whip;
        out=out*(1-ring)+ringCurve*ring;
        return out;
    }
    // Evaluate closest points on line segments, not isolated guide knots.
    // This avoids discontinuous acceleration as a particle crosses between knots.
    // A finite-volume velocity guide: the Eulerian body force steers the *fluid*,
    // not its rendered surface. Each branch transports along its centerline;
    // root capture supplies upward momentum, while the collapsing phase is free.
    Vec3 acceleration(Vec3 p,double seconds) const {
        float t=float(seconds);
        float on=smooth(0.f,.45f,t);
        float off=1.f-smooth(7.7f,9.5f,t);
        if(on*off<1e-5f || p.y<.07f || p.y>1.95f ||
           std::abs(p.x)>1.86f || std::abs(p.z)>1.10f) return {0,0,0};
        float helicity=bell(t,2.5f,2.9f,4.3f,4.9f);
        float whip=bell(t,4.5f,4.9f,5.9f,6.4f);
        float ring=bell(t,6.0f,6.5f,7.45f,8.0f);
        Vec3 bestGoal{},bestTangent{}; float bestU=0, bestDist2=1e20f;
        const int N=48;
        for(int branch=0;branch<2;branch++) {
            float activity=branch==1 ? (1.f-.5f*whip) : 1.f;
            Vec3 a=curve(branch,0.f,t);
            for(int i=0;i<N;i++) {
                float u=float(i)/N,v=float(i+1)/N;
                Vec3 b=curve(branch,v,t),ab=b-a;
                float k=std::clamp(dot(p-a,ab)/std::max(dot(ab,ab),1e-8f),0.f,1.f);
                Vec3 foot=a+ab*k;
                float d2=dot(p-foot,p-foot)/activity;
                if(d2<bestDist2) {
                    bestDist2=d2;bestGoal=foot;bestTangent=normalized(ab);
                    bestU=u+(v-u)*k;
                }
                a=b;
            }
        }
        float dist=length(p-bestGoal);
        // Local capture footprint; an independent pump near each pool root
        // feeds the guide without creating or teleporting liquid.
        float footprint=.10f+.13f*(1.f-bestU);
        float guideInfluence=1.f-smooth(footprint,footprint+.25f,dist);
        Vec3 rootA=curve(0,0.f,t),rootB=curve(1,0.f,t);
        float rootDistance=std::min(length(p-rootA),length(p-rootB));
        float pump=(1.f-smooth(.18f,.60f,rootDistance))*
                   (1.f-smooth(.34f,.70f,p.y));
        float influence=std::max(guideInfluence,pump*.95f)*on*off;
        if(influence<1e-5f)return {0,0,0};
        Vec3 offset=bestGoal-p;
        Vec3 normal=offset-bestTangent*dot(offset,bestTangent);
        float normalLength=length(normal);
        Vec3 n=normalLength>1e-6f?normal/normalLength:Vec3(0,0,0);
        // Driver is a desired acceleration, with a bounded streamline speed.
        // Normal restoring acceleration is proportional to offset (not a flat
        // force pointing at a curve), eliminating the singular capture kick.
        float through=4.2f+1.2f*helicity+2.4f*whip+1.2f*ring;
        Vec3 tangent=bestTangent;
        if(tangent.y<0.f && bestU<.42f && t<6.9f)
            tangent=normalized(tangent+Vec3(0,.9f,0));
        Vec3 flow=tangent*through;
        Vec3 swirl=normalized(cross(tangent,n));
        Vec3 accel=normal*170.f+flow*4.5f+swirl*(1.5f+1.5f*helicity)
                   +Vec3(0,9.81f+8.f*pump,0);
        // Support streams against gravity until final free collapse.
        float cap=105.f;
        float magnitude=length(accel);
        if(magnitude>cap)accel*=cap/magnitude;
        return accel*influence;
    }

};

inline Preset legacyWaterbendingTrickPreset(){
    Preset preset=waterbendingPreset();
    auto &scene=preset.scene;
    scene.name="full_choreographed_waterbending_trick";
    scene.lower={-1.9f,-.12f,-1.15f};scene.upper={1.9f,2.00f,1.15f};
    // Initial liquid is a genuine shallow pool. The choreography has to draw
    // and lift this water: no preformed helix, ring, shield or whip geometry.
    scene.initialLiquidSdf=[](Vec3 p){
        float r=std::sqrt((p.x/.85f)*(p.x/.85f)+(p.z/.65f)*(p.z/.65f));
        return std::max({p.y-.28f,-.11f-p.y,r-.97f});
    };
    scene.initialVelocity=[](Vec3){return Vec3(0,0,0);};
    LegacyTrickGuide control;
    scene.bodyAcceleration=[control](Vec3 p,double t){return control.acceleration(p,t);};
    // No persistent inlet: the pool is the sole source of liquid. This prevents
    // a false continual water source during the collapse stage.
    scene.inlets.clear();
    preset.camera={1.45f,1.85f,3.55f};preset.target={0,.83f,0};
    preset.verticalFov=29.f;preset.referenceWaterLevel=.28f;
    preset.forceNotes="Continuous closest-segment guiding without persistent inlet. Force-guided twin branches: gather 0-1.3 s; split 1.3-2.6; cross/helix 2.6-4.6; whip 4.6-6.1; ring 6.1-7.8; fade to gravity 7.8-10 s. Body accelerations only; all fluid geometry derives from solved particle states.";
    preset.timingNotes="10 s native fluid integration. Inspect at 1,3,5,7,9.5 s. Persistent inlet may affect collapse; do not use this as evidence of physically exact atomization.";
    return preset;
}


// Persistent closed-streamline choreography, used only as external force targets.
// Frame geometry is ALWAYS reconstructed from Solver::particles.
// No target mesh, point teleportation, hidden inlet, or per-frame liquid reset.
struct RetainedTrickGuide {
    static constexpr int count=64;
    struct Knot {Vec3 p,velocity,tangent,curvature;};
    std::array<std::array<Knot,count+1>,2> knots{};
    float currentTime=0, release=1;
    static float ease(float a,float b,float x) {
        float q=std::clamp((x-a)/(b-a),0.f,1.f);
        return q*q*q*(q*(q*6.f-15.f)+10.f); // C2 time transitions
    }
    static Vec3 mix(Vec3 a,Vec3 b,float q){return a*(1-q)+b*q;}
    static Vec3 curve(int b,float u,float t) {
        float side=b?-1.f:1.f;
        float a=2.f*pi*u,c=std::cos(a),sn=std::sin(a);
        // Start under the pool surface. Both guides are loops with no dead end
        // at which the previous controller continuously ejected liquid.
        Vec3 flat{side*.36f+.235f*c,.17f,.235f*sn};
        Vec3 lifted{side*.43f+.24f*c,.75f+.58f*sn,
                    side*.095f+.045f*std::sin(2*a)};
        Vec3 split{side*.55f+.285f*c,.87f+.66f*sn,
                   side*.10f+.065f*std::sin(2*a)};
        float turn=.8f*ease(2.8f,4.5f,t);
        Vec3 helix{.45f*std::cos(a+side*turn),.91f+.66f*sn,
                   side*.185f+.18f*std::sin(2*a+side*turn)};
        Vec3 whip;
        if(b==0) whip={.13f+.99f*c,.89f+.17f*sn+.19f*(1-c*c),
                       .08f+.10f*std::sin(2*a)};
        else whip={-.32f+.32f*c,.79f+.48f*sn,-.19f+.11f*std::sin(2*a)};
        // Two material volumes converge into a broad tilted ring, without
        // deleting either branch. Deliberate telekinetic forcing, not capillarity.
        Vec3 ring{.64f*c,.94f+.66f*sn,.14f*sn+side*.075f};
        Vec3 p=mix(flat,lifted,ease(.35f,1.65f,t));
        p=mix(p,split,ease(1.70f,2.65f,t));
        p=mix(p,helix,ease(2.70f,3.65f,t));
        p=mix(p,whip,ease(4.45f,5.50f,t));
        p=mix(p,ring,ease(5.85f,6.95f,t));
        return p;
    }
    void prepare(double seconds) {
        currentTime=float(seconds);
        release=1-ease(7.85f,8.55f,currentTime);
        for(int b=0;b<2;b++)for(int i=0;i<=count;i++) {
            float u=float(i)/count,du=1.f/count;
            auto &k=knots[b][i];k.p=curve(b,u,currentTime);
            k.velocity=(curve(b,u,currentTime+.002f)-curve(b,u,currentTime-.002f))/.004f;
            Vec3 before=curve(b,u-du,currentTime),after=curve(b,u+du,currentTime);
            k.tangent=normalized(after-before);
            float ds=.5f*(length(after-k.p)+length(k.p-before));
            Vec3 raw=(after+k.p*(-2.f)+before)/std::max(ds*ds,1e-6f);
            k.curvature=raw-k.tangent*dot(raw,k.tangent);
            float mag=length(k.curvature);if(mag>12)k.curvature*=12/mag;
        }
    }
    struct Closest {Vec3 p{},v{},tangent{},curvature{};float d2=1e20f;int branch=0;};
    Closest closest(Vec3 p) const {
        Closest out;
        for(int b=0;b<2;b++)for(int i=0;i<count;i++) {
            auto &a=knots[b][i];auto &c=knots[b][i+1];
            Vec3 edge=c.p-a.p;
            float f=std::clamp(dot(p-a.p,edge)/std::max(dot(edge,edge),1e-8f),0.f,1.f);
            Vec3 foot=a.p+edge*f;float d2=dot(p-foot,p-foot);
            if(d2<out.d2) {
                out.d2=d2;out.p=foot;out.v=mix(a.velocity,c.velocity,f);
                out.tangent=normalized(mix(a.tangent,c.tangent,f));
                out.curvature=mix(a.curvature,c.curvature,f);out.branch=b;
            }
        }
        return out;
    }
    VelocityGuide sample(Vec3 p,double) const {
        VelocityGuide g;
        if(release<=0 || p.y<.04f || p.y>2.05f || std::abs(p.x)>1.6f || std::abs(p.z)>.75f)return g;
        Closest q=closest(p);float d=std::sqrt(q.d2);
        float influence=(1-ease(.20f,.48f,d))*release*ease(0,.30f,currentTime);
        if(influence<=0)return g;
        Vec3 offset=q.p-p;
        offset-=q.tangent*dot(offset,q.tangent);
        // Cross-section confinement is weaker across the ribbon than across
        // its thickness. Pressure remains responsible for volume exclusion.
        Vec3 thin=normalized(Vec3(0,0,1)-q.tangent*q.tangent.z);
        Vec3 normal=offset*3.4f+thin*(dot(offset,thin)*11.5f);
        float speed=.52f+.22f*ease(2.6f,3.5f,currentTime);
        speed+=.30f*ease(4.4f,5.3f,currentTime)*(1-ease(5.8f,6.9f,currentTime));
        g.target=q.v+q.tangent*speed+normal;
        float v=length(g.target);if(v>3.4f)g.target*=3.4f/v;
        g.rate=20.f*influence;
        g.acceleration=(Vec3(0,9.81f,0)+q.curvature*(speed*speed))*influence;
        g.accelerationLimit=50.f;
        return g;
    }
};

inline Preset waterbendingTrickPreset() {
    Preset preset=waterbendingPreset();auto &s=preset.scene;
    s.name="open_splash_feedback_waterbending_trick";
    // Real bed/rocks are the only lateral collision geometry. The larger
    // computational window is OPEN laterally; escaped mass is accounted.
    // Camera framing does not impose a physical wall.
    s.lower={-3.30f,-.12f,-2.52f};s.upper={3.30f,3.30f,2.52f};
    s.closedBoundaries={false,false,true,false,false,false};
    s.initialLiquidSdf=[](Vec3 p){
        float r=std::sqrt((p.x/.95f)*(p.x/.95f)+(p.z/.72f)*(p.z/.72f));
        return std::max({p.y-.285f,-.11f-p.y,r-.98f});
    };
    s.initialVelocity=[](Vec3){return Vec3{};};s.inlets.clear();
    auto controller=std::make_shared<RetainedTrickGuide>();controller->prepare(0);
    s.bodyAcceleration={};
    s.prepareForces=[controller](double t){controller->prepare(t);};
    s.velocityGuide=[controller](Vec3 p,double t){return controller->sample(p,t);};
    s.extraAccelerationBound=60.f;
    preset.camera={1.9f,1.65f,4.6f};preset.target={0,.78f,0};preset.verticalFov=29.f;
    preset.referenceWaterLevel=.285f;
    preset.forceNotes="Velocity-error feedback before the common pressure projection; periodic guide paths; moving-guide feed-forward; local gravity support; no inlet or position overwrite. All guidance exactly zero after 8.55 s.";
    preset.timingNotes="Gather/split 0-2.65; crossing 2.7-4.45; whip 4.45-5.85; ring 5.85-7.85; release 7.85-8.55; free gravity 8.55-10. Quantify real retained and elevated volume, separately from mass-ledger error.";
    return preset;
}

// Opt-in V7 performance. The accepted V6 preset stays available unchanged.
inline Preset cinematicWaterbendingPreset() {
    Preset preset=waterbendingTrickPreset();auto &scene=preset.scene;
    scene.name="waterbending_duet_v7";
    auto controller=std::make_shared<choreography::DuetGuide>();controller->prepare(0);
    scene.prepareForces=[controller](double t){controller->prepare(t);};
    scene.velocityGuide=[controller](Vec3 p,double t){return controller->sample(p,t);};
    scene.extraAccelerationBound=80.f;
    preset.camera={1.65f,1.58f,4.75f};preset.target={0,.92f,0};preset.verticalFov=31.f;
    preset.forceNotes="V7 duet: C2 pose transitions; moving-curve velocity/acceleration feed-forward; soft nearest-branch competition; bounded pre-projection velocity feedback. No particle repositioning or liquid sources.";
    preset.timingNotes="Draw 0-.9; twin cobras .9-2.15; braid 2.15-3.65; anticipation 3.65-4.55; staggered whip 4.55-5.70; orbital catch 5.70-6.65; shield 6.65-7.80; spiral return 7.80-8.95; unforced splash 8.95-10.";
    return preset;
}

inline Preset combatWaterbendingPreset() {
    Preset preset=waterbendingTrickPreset();auto &scene=preset.scene;
    scene.name="waterbending_combat_v8";
    auto controller=std::make_shared<choreography::CombatGuide>();controller->prepare(0);
    scene.prepareForces=[controller](double t){controller->prepare(t);};
    scene.velocityGuide=[controller](Vec3 p,double t){return controller->sample(p,t);};
    scene.extraAccelerationBound=88.f;
    preset.camera={1.70f,1.48f,4.85f};preset.target={0,.83f,-.08f};preset.verticalFov=32.f;
    preset.forceNotes="V8 combat waterbending: dual whips, crossed slices, broad wave push, rotating vortex gather, spiral lance, descending slam, then unforced splash. Moving-curve velocity feedback only; no particle teleportation, hidden water source, or rendered guide geometry.";
    preset.timingNotes="Draw 0-.75; dual whips .75-2.15; crossed slices 2.15-3.45; wave push 3.45-4.95; vortex 4.95-6.45; spiral lance 6.45-7.80; slam 7.80-9.05; free splash 9.05-10.";
    return preset;
}

inline Preset makePreset(const std::string &name) {
    if(name=="waterbending_combat" || name=="waterbending_combat_v8")return combatWaterbendingPreset();
    if(name=="waterbending_duet" || name=="waterbending_duet_v7")return cinematicWaterbendingPreset();
    if(name=="legacy_trick")return legacyWaterbendingTrickPreset();
    if (name=="waterbending_trick" || name=="full_choreographed_waterbending_trick") return waterbendingTrickPreset();
    if (name=="beach" || name=="pocket_beach_shoaling_crest") return beachPreset();
    if (name=="waterbending" || name=="externally_forced_waterbending_loop") return waterbendingPreset();
    if (name=="drop" || name=="macro_drop_early_inertial_impact") return dropImpactPreset();
    if (name=="whirlpool" || name=="released_rotation_rock_basin") return whirlpoolPreset();
    if (name=="rockimpact" || name=="dam_release_fractured_boulder_impact") return rockImpactPreset();
    if (name=="jets" || name=="opposed_liquid_jets_over_rock_basin") return collidingJetsPreset();
    if (name=="cascade" || name=="three_step_rock_cascade_into_pool") return cascadePreset();
    throw std::runtime_error("Unknown gallery scene; use beach, waterbending, drop, whirlpool, rockimpact, jets or cascade");
}

} // namespace gallery

inline Scene makeBeachScene() { return gallery::beachPreset().scene; }
inline Scene makeWaterbendingScene() { return gallery::waterbendingPreset().scene; }
inline Scene makeDropImpactScene() { return gallery::dropImpactPreset().scene; }
inline Scene makeWhirlpoolScene() { return gallery::whirlpoolPreset().scene; }
inline Scene makeRockImpactScene() { return gallery::rockImpactPreset().scene; }
inline Scene makeCollidingJetsScene() { return gallery::collidingJetsPreset().scene; }
inline Scene makeCascadeScene() { return gallery::cascadePreset().scene; }

} // namespace water
