cmake_minimum_required(VERSION 3.30)
project(Curia VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Executable
add_library(curia_ecs STATIC
        src/World.cpp)
target_include_directories(curia_ecs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(curia_ecs PRIVATE $<$<CONFIG:Release>:/O2 /Ob2 /Oi /Ot /arch:AVX2>)
else()
    target_compile_options(curia_ecs PRIVATE $<$<CONFIG:Release>:-O3 -march=native -ffast-math>)
endif()

include(FetchContent)
FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(curia_benchmark
        tests/benchmark.cpp
)

target_link_libraries(curia_benchmark PRIVATE curia_ecs benchmark::benchmark)

if(MSVC)
    target_compile_options(curia_benchmark PRIVATE $<$<CONFIG:Release>:/O2 /arch:AVX2>)
else()
    target_compile_options(curia_benchmark PRIVATE $<$<CONFIG:Release>:-O3 -march=native>)
endif()