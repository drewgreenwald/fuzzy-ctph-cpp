cmake_minimum_required(VERSION 3.20)
project(fuzzy-hash-tool LANGUAGES CXX)

# 1. Standard Requirements (C++20 is required for std::execution and std::filesystem)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 2. Find Dependencies via vcpkg
find_package(argparse CONFIG REQUIRED)
find_package(Threads REQUIRED)

# On Linux/Unix, std::execution::par often requires TBB
if(UNIX)
    find_package(TBB REQUIRED)
endif()

# 3. Create the 'ctph' Library
# This builds your fuzzy.cpp into a reusable library
add_library(ctph_lib STATIC 
    fuzzy-ctph/fuzzy.cpp 
    fuzzy-ctph/fuzzy.h
)

# 4. Create the Executable
add_executable(fuzzy-ctph fuzzy-ctph/fuzzy-ctph.cpp)

# 5. Linking
# We link the CLI tool to your library, argparse, and threading
target_link_libraries(fuzzy-ctph 
    PRIVATE 
        ctph_lib 
        argparse::argparse 
        Threads::Threads
)

# Linux-specific threading/TBB link
if(UNIX)
    target_link_libraries(fuzzy-ctph PRIVATE TBB::tbb)
endif()

# 6. Include Directories
target_include_directories(ctph_lib PUBLIC fuzzy-ctph)
target_include_directories(fuzzy-ctph PRIVATE fuzzy-ctph)

# 7. Compiler-specific flags
if(MSVC)
    target_compile_options(fuzzy-ctph PRIVATE /W4 /utf-8)
else()
    target_compile_options(fuzzy-ctph PRIVATE -Wall -Wextra -pthread)
endif()