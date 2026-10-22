CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -fopenmp

.PHONY: all clean
all: build/gallery_sim build/surface build/trace build/gallery_export

build:
	mkdir -p build

build/gallery_sim: src/gallery_simulate.cpp src/fluid.hpp src/scene.hpp src/gallery_scenes.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_simulate.cpp -o $@

build/surface: src/surface.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/trace: src/trace.cpp | build
	$(CXX) $(CXXFLAGS) $< -o $@

build/gallery_export: src/gallery_export.cpp src/gallery_scenes.hpp src/scene.hpp src/fluid.hpp | build
	$(CXX) $(CXXFLAGS) src/gallery_export.cpp -o $@

clean:
	rm -f build/gallery_sim build/surface build/trace build/gallery_export
