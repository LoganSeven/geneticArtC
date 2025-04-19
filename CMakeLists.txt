cmake_minimum_required(VERSION 3.10)

project(GeneticArt C)        # Name of the project; language is C
set(CMAKE_C_STANDARD 11)     # We'll build with C11 features

# Find SDL2 and threads (pthread)
find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# Create our executable from the two .c files
add_executable(genetic_art
    main.c
    genetic_art.c
)

# Link needed libraries:
#   SDL2::SDL2       -> dynamic SDL2
#   Threads::Threads -> pthread
#   m                -> standard math
target_link_libraries(genetic_art
    PRIVATE
        SDL2::SDL2
        Threads::Threads
        m
)
