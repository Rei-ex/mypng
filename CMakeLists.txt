cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ~/vcpkg/scripts/buildsystems/vcpkg.cmake)

project(mypng)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(mypng
    src/imread.cpp
    src/imread.hpp
    src/imsave.cpp
    src/imsave.hpp
    test.cpp
)

find_package(zlib-ng CONFIG REQUIRED)
target_link_libraries(mypng PRIVATE zlib-ng::zlib)

target_compile_options(mypng PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-mavx2>
)

set(CLANG_PERFORMANCE_FLAGS 
    -O3 
    -march=native 
    -flto 
    -fomit-frame-pointer 
)

target_compile_options(mypng PRIVATE $<$<CXX_COMPILER_ID:Clang>:${CLANG_PERFORMANCE_FLAGS}>)
target_link_options(mypng PRIVATE $<$<CXX_COMPILER_ID:Clang>:-flto -fuse-ld=lld>)