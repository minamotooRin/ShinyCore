.PHONY: build run check test headless
build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
run: build
	./build/shiny examples/lantern
check: build
	./build/shiny --check examples/lantern
test: build
	ctest --test-dir build --output-on-failure
headless:
	cmake -S . -B build-headless -DSHINY_GRAPHICS=OFF -DCMAKE_BUILD_TYPE=Release
	cmake --build build-headless --parallel
	ctest --test-dir build-headless --output-on-failure
