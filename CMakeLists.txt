cmake_minimum_required(VERSION 3.10)

# ------------------ Project setup ---------------------------------
project(GeneticArt VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug or Release)" FORCE)
endif()

message(STATUS "⛏️  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "💻 Target platform: ${CMAKE_SYSTEM_NAME}")

# ------------------ Compiler warnings/options -----------------------------
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    if(COMPILER_SUPPORTS_NO_C23_EXTENSIONS)
        add_compile_options(-Wno-c23-extensions)
    endif()
endif()

# ------------------ Dependencies: SDL2 + Threads -------------------
find_package(SDL2 QUIET)
find_package(Threads REQUIRED)

# If SDL2 not found (common on Linux with manual build), fallback manually
if(NOT SDL2_FOUND OR NOT SDL2_INCLUDE_DIRS)
    if(UNIX)
        set(SDL2_INCLUDE_DIRS "/usr/local/include/SDL2")
        set(SDL2_LIBRARIES "-lSDL2")
        message(WARNING "⚠️  SDL2 not found via find_package, using fallback paths:")
        message(STATUS "⚠️    SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")
        message(STATUS "⚠️    SDL2_LIBRARIES    = ${SDL2_LIBRARIES}")
    else()
        message(FATAL_ERROR "❌ SDL2 not found and no fallback configured for this platform.")
    endif()
else()
    message(STATUS "✅ SDL2 found:")
    message(STATUS "✅    SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")
    message(STATUS "✅    SDL2_LIBRARIES    = ${SDL2_LIBRARIES}")
endif()

# ------------------ Include directories ---------------------------
include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/includes
    ${CMAKE_SOURCE_DIR}/includes/Nuklear
    ${CMAKE_SOURCE_DIR}/includes/SDL
)

# ------------------ Executable -------------------------------------
add_executable(genetic_art
    ${CMAKE_SOURCE_DIR}/src/embedded_font.c
    ${CMAKE_SOURCE_DIR}/src/system_tools.c
    ${CMAKE_SOURCE_DIR}/src/main.c
    ${CMAKE_SOURCE_DIR}/src/genetic_art.c
    ${CMAKE_SOURCE_DIR}/src/genetic_structs.c
    ${CMAKE_SOURCE_DIR}/src/bmp_validator.c
    ${CMAKE_SOURCE_DIR}/src/ga_renderer.c
    ${CMAKE_SOURCE_DIR}/src/nuklear.c
    ${CMAKE_SOURCE_DIR}/src/nuklear_sdl_renderer.c
    ${CMAKE_SOURCE_DIR}/src/main_runtime.c
)

# ------------------ Linking -----------------------------------------
target_link_libraries(genetic_art
    PRIVATE
        ${SDL2_LIBRARIES}
        Threads::Threads
)

if(WIN32)
    target_link_libraries(genetic_art PRIVATE SDL2::SDL2main)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(genetic_art PRIVATE m)
endif()

# ------------------ Final Summary -----------------------------------
message(STATUS "✅ Build setup complete.")
message(STATUS "💡 To change SDL2 path: set SDL2_INCLUDE_DIRS and SDL2_LIBRARIES manually.")
