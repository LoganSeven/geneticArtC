cmake_minimum_required(VERSION 3.10)

# ------------------ Project setup ---------------------------------
project(GeneticArt C)     # Name of the project; language is C
set(CMAKE_C_STANDARD 11)  # We'll build with C11
set(CMAKE_C_STANDARD_REQUIRED ON)  # Fail if the compiler doesn't support C11

# If the user didn't specify, default to a Release build (optional)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ------------------ Compiler warnings (cross-platform) -------------
if(MSVC)
    # MSVC: Enable warning level 4
    add_compile_options(/W4)
else()
    # GCC or Clang: Enable common warnings
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# ------------------ Dependencies: SDL2 + Threads --------------------
find_package(SDL2 REQUIRED)     # This should define SDL2::SDL2 (+ optional SDL2::SDL2main on Windows)
find_package(Threads REQUIRED)  # Pthreads

# ------------------ Include directories ---------------------------
include_directories(
    ${SDL2_INCLUDE_DIRS}                  # From find_package(SDL2)
    ${CMAKE_SOURCE_DIR}/includes          # Project headers
    ${CMAKE_SOURCE_DIR}/includes/Nuklear  # Nuklear single-header directory
    ${CMAKE_SOURCE_DIR}/includes/SDL      # Local SDL2 headers (optional)
)

# ------------------ Our executable ----------------------------------
add_executable(genetic_art
    main.c
    genetic_art.c
    genetic_structs.c
)

# ------------------ Linking -----------------------------------------
# 1) Always link SDL2::SDL2 and Threads::Threads
# 2) On non‑Windows, also link 'm' for math functions
# 3) On Windows, we often need SDL2::SDL2main in addition to SDL2::SDL2

target_link_libraries(genetic_art
    PRIVATE
        SDL2::SDL2
        Threads::Threads
)

if(WIN32)
    # Windows typically needs the "SDL2main" library for WinMain entry point
    target_link_libraries(genetic_art PRIVATE SDL2::SDL2main)
else()
    # On Unix-like systems, math library is usually separate
    target_link_libraries(genetic_art PRIVATE m)
endif()

# ------------------ Optional Nuklear definitions --------------------
# Enable only if required by your build of Nuklear
# target_compile_definitions(genetic_art PRIVATE NK_INCLUDE_FIXED_TYPES)
# target_compile_definitions(genetic_art PRIVATE NK_INCLUDE_DEFAULT_ALLOCATOR)
# target_compile_definitions(genetic_art PRIVATE NK_INCLUDE_VERTEX_BUFFER_OUTPUT)
# target_compile_definitions(genetic_art PRIVATE NK_INCLUDE_FONT_BAKING)
# target_compile_definitions(genetic_art PRIVATE NK_INCLUDE_DEFAULT_FONT)

# ------------------ Notes -------------------------------------------
# 1) If you're using older SDL2 CMake modules, you might need:
#    include_directories(${SDL2_INCLUDE_DIRS})
#    target_link_libraries(genetic_art PRIVATE ${SDL2_LIBRARIES})
#
# 2) For macOS, you usually do not need an extra '-lm' because
#    the default C library provides math functions. Linking 'm'
#    is typically still harmless.
#
# 3) If you need extra Apple frameworks, you can add them:
#    if(APPLE)
#        find_library(COCOA_LIBRARY Cocoa)
#        target_link_libraries(genetic_art PRIVATE ${COCOA_LIBRARY})
#    endif()
#
# 4) By default, 'find_package(SDL2 REQUIRED)' relies on a config or
#    module. If you have trouble, see official SDL2 instructions or
#    locate a FindSDL2.cmake that fits your environment.
