# Genetic Art (SDL2 + Nuklear + C)

This demonstrator visualizes the behavior and optimization potential of a genetic algorithm implemented in pure C11. Designed to be cross-platform, lightweight, and minimal, it serves as a foundation for exploring evolutionary strategies. The algorithm incrementally try tp reconstructs a target image using only primitive shapes, showing  progress through SDL2.

It acts as a standalone, research visual component with the idea to integrate GA in the framework,
where genetic algorithms are used as modular agents capable of adaptive optimization in multi-agent cognitive architectures.

## Features

- C23 (should be C11-compliant with few patches) genetic algorithm
- Pixel-based software rasterizer (triangles & circles)
- Parallelism via POSIX threads
- avx2 intrinsics
- Interactive display using SDL2 and Nuklear
- self contained Nuklear library as one file header
- Cross-platform support (Linux, Windows (untested yet))

## Requirements

- SDL2 (dev headers + runtime)
- CMake (version >= 3.10)
- A C compiler:
  - Linux: gcc or clang
  - Windows: MSVC via Visual Studio, or mingw-w64 via MSYS2

## Installation

### Linux

# Install dependencies
sudo apt update
sudo apt install libsdl2-dev cmake build-essential

# (Optional) Build SDL2 from source if needed
git clone https://github.com/libsdl-org/SDL.git -b SDL2
cd SDL
mkdir build && cd build
cmake .. 
make -j$(nproc)
sudo make install
sudo ldconfig

### Windows (Visual Studio)

1. Install CMake
2. Install SDL2 development libraries for Visual Studio
2b. Nuklear 4.12.7 is self contained as a header file lib
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

```plaintext
./
    └── assets/
        └── fonts/
            └── amiga4ever.ttf
    └── bmp_test_set/
        ├── test1.bmp
        ├── test2.bmp
        └── test3.bmp
    ├── CMakeLists.txt
    ├── display_folder_tree.sh
    ├── doxygen_geneticart_config
    ├── doxygen_nuklear_config
    └── includes/
        ├── async_io/
        │   └── async_file_ops.h
        ├── config.h
        ├── fonts_as_header/
        │   └── embedded_font.h
        ├── genetic_algorithm/
        │   ├── genetic_art.h
        │   └── genetic_structs.h
        ├── Nuklear/
        │   └── nuklear.h
        ├── opengl_rendering/
        ├── software_rendering/
        │   ├── ga_renderer.h
        │   ├── main_runtime.h
        │   └── nuklear_sdl_renderer.h
        ├── tools/
        │   └── system_tools.h
        └── validators/
            └── bmp_validator.h
    ├── README.md
    └── src/
        ├── async_file_ops.c
        ├── bmp_validator.c
        ├── embedded_font.c
        ├── ga_renderer.c
        ├── genetic_art.c
        ├── genetic_structs.c
        ├── main.c
        ├── main_runtime.c
        ├── nuklear.c
        ├── nuklear_sdl_renderer.c
        └── system_tools.c
    ├── TODO.md


```

## Known Limitations

- Convergence may stall after around 100 iterations
- Only 640x480 images are supported internally
- BMP only - no PNG/JPEG support
- Performance is single-threaded on GA side

## License

This part of the project is open-source and available under the MIT (modified) License.
See the [LICENSE](./LICENSE) file for more details.

## Credits

Created by Logan7 — powered by C, SDL2, Nuklear, and evolutionary chaos.

See the [TODO list](./TODO.md) for planned features and ongoing work.

See the [DOCUMENTATION](./Documentation/NeuroGenesysFramework_GeneticArtC.pdf) for the design rationale and conceptual background behind the project.

An overview diagram of the Island Model Genetic Algorithm is available [here](https://github.com/LoganSeven/geneticArtC/issues/8#issuecomment-2817896401).
