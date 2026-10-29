
cmake_minimum_required(VERSION 3.16)

project(bitdowner
    VERSION 1.0.0
    DESCRIPTION "Standalone BitDowner audio effect"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)

add_executable(bitdowner
    src/main.cpp
)

target_link_libraries(bitdowner
    PRIVATE PkgConfig::SNDFILE
)

target_compile_options(bitdowner PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
