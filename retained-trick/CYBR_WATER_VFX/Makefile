CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -fopenmp

.PHONY: all clean checks
all: build/water_sim build/gallery_sim build/surface build/trace build/solid_export build/sample_solid build/gallery_export

build:
	mkdir -p build

build/water_sim: src/simulate.cpp src/fluid.hpp src/scene.hpp | build
	$(CXX) $(CXXFLAGS) src/simulate.cpp -o $@

build/gallery_sim: src/gallery_simulate.cpp src/fluid.hpp src/scene.hpp src/gallery_scenes.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_simulate.cpp -o $@

build/surface: src/surface.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/trace: src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/solid_export: src/solid_export.cpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/solid_export.cpp -o $@

build/sample_solid: src/sample_solid.cpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/sample_solid.cpp -o $@

build/gallery_export: src/gallery_export.cpp src/gallery_scenes.hpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_export.cpp -o $@

build/solver_checks: tests/solver_checks.cpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) tests/solver_checks.cpp -o $@

checks: build/solver_checks build/trace
	build/solver_checks
	build/trace --self-test

clean:
	rm -f build/water_sim build/gallery_sim build/surface build/trace build/solid_export build/sample_solid build/gallery_export build/solver_checks
