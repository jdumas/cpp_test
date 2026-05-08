cmake_minimum_required(VERSION 3.20)
project(alignment_probe CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Make MSVC report the actual __cplusplus value
if(MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

option(WITH_EIGEN "Pull Eigen 5.0.1 and include it in the probe" ON)

include(FetchContent)
if(WITH_EIGEN)
    FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 5.0.1
    )
    FetchContent_GetProperties(eigen)
    if(NOT eigen_POPULATED)
        FetchContent_Populate(eigen)
    endif()
endif()

add_executable(probe main.cpp)
if(WITH_EIGEN AND eigen_SOURCE_DIR)
    target_include_directories(probe SYSTEM PRIVATE ${eigen_SOURCE_DIR})
endif()
