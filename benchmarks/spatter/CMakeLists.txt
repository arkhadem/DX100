# CMakeLists.txt
cmake_minimum_required(VERSION 3.25)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

include(DisableIntree)

project(Spatter VERSION 2.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

include(BuildType)
include(CompilerType)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED on)

include(pkgs/JSONSupport)
include(pkgs/MPISupport)
include(pkgs/OpenMPSupport)
include(pkgs/CUDASupport)

# Create gz_read executable
add_executable(gz_read standard-suite/binary-traces/gz_read.cc)
target_link_libraries(gz_read z)

option(BUILD_MAA "Build MAA version" OFF)
option(BUILD_FUNC "Build with FUNC mode" OFF)
option(BUILD_GEM5 "Build with GEM5 mode" OFF)

# Set paths for GEM5 and MAA
get_filename_component(GEM5_HOME "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(GEM5_HOME "${GEM5_HOME}" CACHE PATH "Path to GEM5 home")
message(STATUS "GEM5_HOME set to ${GEM5_HOME}")
set(MAA_HOME "${GEM5_HOME}/benchmarks/API/" CACHE PATH "Path to MAA home")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -g")

# Include directories for GEM5 and MAA
include_directories(${GEM5_HOME}/include/ ${GEM5_HOME}/util/m5/src/ ${MAA_HOME})

if(BUILD_FUNC)
    add_definitions(-DFUNC)
endif()

if(BUILD_GEM5)
    add_definitions(-DGEM5)
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
