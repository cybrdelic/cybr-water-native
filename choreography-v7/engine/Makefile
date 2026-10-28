CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -fopenmp

.PHONY: all clean checks
all: build/water_sim build/gallery_sim build/surface build/trace build/solid_export build/sample_solid build/gallery_export

build:
	mkdir -p build

build/water_sim: src/simulate.cpp src/fluid.hpp src/scene.hpp | build
	$(CXX) $(CXXFLAGS) src/simulate.cpp -o $@

build/gallery_sim: src/gallery_simulate.cpp src/fluid.hpp src/scene.hpp src/gallery_scenes.hpp src/cinematic_choreography.hpp src/martial_choreography_v11.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_simulate.cpp -o $@

build/surface: src/surface.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/trace: src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/solid_export: src/solid_export.cpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/solid_export.cpp -o $@

build/sample_solid: src/sample_solid.cpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/sample_solid.cpp -o $@

build/gallery_export: src/gallery_export.cpp src/gallery_scenes.hpp src/scene.hpp src/fluid.hpp src/cinematic_choreography.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_export.cpp -o $@

build/solver_checks: tests/solver_checks.cpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) tests/solver_checks.cpp -o $@

build/material_splash_checks: tests/material_splash_checks.cpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/fragment_phase_checks: tests/fragment_phase_checks.cpp src/surface.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/retained_guidance_checks: tests/retained_guidance_checks.cpp src/gallery_scenes.hpp src/fluid.hpp src/cinematic_choreography.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/splash_boundary_checks: tests/splash_boundary_checks.cpp src/gallery_scenes.hpp src/fluid.hpp src/cinematic_choreography.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/scene_regression: tests/scene_regression.cpp src/gallery_scenes.hpp src/fluid.hpp src/cinematic_choreography.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

checks: build/scene_regression build/solver_checks build/trace build/material_splash_checks build/fragment_phase_checks build/retained_guidance_checks build/splash_boundary_checks
	build/solver_checks
	build/trace --self-test
	build/material_splash_checks
	build/fragment_phase_checks
	build/retained_guidance_checks
	build/splash_boundary_checks
	build/scene_regression

clean:
	rm -f build/water_sim build/gallery_sim build/surface build/trace build/solid_export build/sample_solid build/gallery_export build/solver_checks

# Additional reconstruction / optical continuity checks for the V6 pass.
build/coherent_finish_checks: tests/coherent_finish_checks.cpp src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
build/topology_normals_checks: tests/topology_normals_checks.cpp src/surface.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
build/normal_continuity_checks: tests/normal_continuity_checks.cpp src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
.PHONY: finish-checks
finish-checks: build/coherent_finish_checks build/topology_normals_checks build/normal_continuity_checks
	build/coherent_finish_checks
	build/topology_normals_checks
	build/normal_continuity_checks

build/trace_native: src/trace.cpp | build
	$(CXX) $(CXXFLAGS) -march=native -ffp-contract=off $< -o $@
build/foam_surface_checks: tests/foam_surface_checks.cpp src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
build/domain_growth_replay_checks: tests/domain_growth_replay_checks.cpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
.PHONY: continuation-checks
continuation-checks: build/foam_surface_checks build/domain_growth_replay_checks
	build/foam_surface_checks
	build/domain_growth_replay_checks

build/mesh_cache_codec: ../mesh_cache_codec.cpp | build
	$(CXX) -O3 -std=c++17 $< -o $@

# V7 choreography regression suite; V6 checks remain intact.
build/choreography_checks: tests/choreography_checks.cpp src/cinematic_choreography.hpp src/gallery_scenes.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@
.PHONY: choreography-checks
choreography-checks: build/choreography_checks build/duet_query_checks build/martial_v11_checks
	build/choreography_checks
	build/duet_query_checks
	build/martial_v11_checks

# Exact-query equivalence is an additional release gate, not a looser tolerance.
build/duet_query_checks: tests/duet_query_checks.cpp src/cinematic_choreography.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@


build/combat_choreography_checks: tests/combat_choreography_checks.cpp src/gallery_scenes.hpp src/fluid.hpp | build src/cinematic_choreography.hpp
	$(CXX) $(CXXFLAGS) $< -o $@
.PHONY: combat-checks
combat-checks: build/combat_choreography_checks
	build/combat_choreography_checks

# V11 transition, boundary and force-release contract regression.
build/martial_v11_checks: tests/martial_v11_checks.cpp src/gallery_scenes.hpp src/martial_choreography_v11.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

