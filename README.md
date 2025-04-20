# Genetic Art (SDL2 + C)

This demonstrator visualizes the behavior and optimization potential of a genetic algorithm implemented in pure C11. Designed to be cross-platform, lightweight, and minimal, it serves as a foundation for exploring evolutionary strategies. The algorithm incrementally try tp reconstructs a target image using only primitive shapes, showing  progress through SDL2.

It acts as a standalone, research visual component with the idea to integrate GA in the framework,
where genetic algorithms are used as modular agents capable of adaptive optimization in multi-agent cognitive architectures.

## Features

- C11-compliant genetic algorithm
- Pixel-based software rasterizer (triangles & circles)
- Parallelism via POSIX threads
- Interactive display using SDL2
- Cross-platform support (Linux, Windows)

## Requirements

- SDL2 (dev headers + runtime)
- CMake (version >= 3.10)
- A C compiler:
  - Linux: gcc or clang
  - Windows: MSVC via Visual Studio, or mingw-w64 via MSYS2

## Installation

### Linux

```
sudo apt install libsdl2-dev cmake build-essential

# Or build SDL2 from source
git clone https://github.com/libsdl-org/SDL.git -b SDL2
cd SDL
mkdir build && cd build
../configure
make -j$(nproc)
sudo make install
sudo ldconfig
```

### Windows (Visual Studio)

1. Install CMake
2. Install SDL2 development libraries for Visual Studio
3. Extract and place headers/libraries in a known location (or use vcpkg)
4. Open a Developer Command Prompt and:

```
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Windows (MSYS2 + MinGW)

```
pacman -Syu
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2

mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

## Compilation & Run

### Standard (Linux or Windows with CMake)
```
mkdir build
cd build
cmake ..
cmake --build .
```

### Run the demo
```
./genetic_art path/to/image.bmp
```

The reference image must be a 640x480 BMP file (or will be resized with letterboxing).

## Project Structure

```
main.c            SDL2 display logic and main loop
genetic_art.c     Genetic algorithm and rasterization engine
genetic_art.h     Shared interface between main and GA engine
CMakeLists.txt    CMake build configuration
```

## Known Limitations

- Convergence may stall after around 100 iterations
- Only 640x480 images are supported internally
- BMP only - no PNG/JPEG support
- Performance is single-threaded on GA side

## License

This part of the project is open-source and available under the MIT License.

## Credits

Created by Logan7 - powered by C, SDL2 and evolutionary chaos.

👉 See the [TODO list](./TODO.md) for planned features and ongoing work.
