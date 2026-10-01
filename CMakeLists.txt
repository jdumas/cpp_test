cmake_minimum_required(VERSION 3.20)
project(embree_ubsan_repro LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(FATAL_ERROR "This reproducer requires GCC or Clang with UBSan support")
endif()

set(EMBREE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/embree" CACHE PATH "Embree checkout")
option(REPRO_NARROW_EXCLUSIONS "Disable only Embree's function and vptr checks" OFF)
if(NOT EXISTS "${EMBREE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Check out RenderKit/embree into ${EMBREE_SOURCE_DIR} first")
endif()

# Embree's Debug flags enable -O3; override them for a faster, unoptimized MWE build.
add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -O0 -g1)
add_link_options(-fsanitize=undefined)

set(EMBREE_IGNORE_CMAKE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(EMBREE_STATIC_LIB ON CACHE BOOL "" FORCE)
set(EMBREE_MAX_ISA SSE2 CACHE STRING "" FORCE)
set(EMBREE_TASKING_SYSTEM INTERNAL CACHE STRING "" FORCE)
set(EMBREE_RAY_PACKETS OFF CACHE BOOL "" FORCE)
set(EMBREE_ISPC_SUPPORT OFF CACHE BOOL "" FORCE)
set(EMBREE_SYCL_SUPPORT OFF CACHE BOOL "" FORCE)
set(EMBREE_TUTORIALS OFF CACHE BOOL "" FORCE)
set(EMBREE_TESTING OFF CACHE BOOL "" FORCE)
foreach(geometry IN ITEMS CURVE SUBDIVISION GRID POINT QUAD)
    set(EMBREE_GEOMETRY_${geometry} OFF CACHE BOOL "" FORCE)
endforeach()

add_subdirectory("${EMBREE_SOURCE_DIR}" embree-build EXCLUDE_FROM_ALL)

if(REPRO_NARROW_EXCLUSIONS)
    # Retain UBSan except for the two affected checks on Embree's kernel targets.
    # The reproducer and auxiliary libraries remain fully instrumented.
    foreach(target IN ITEMS embree embree_sse42 embree_avx embree_avx2 embree_avx512 embree_apx)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE
                $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-sanitize=vptr>
                $<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:-fno-sanitize=function>
            )
        endif()
    endforeach()
endif()

add_executable(probe main.cpp)
target_include_directories(probe PRIVATE "${EMBREE_SOURCE_DIR}/include")
target_link_libraries(probe PRIVATE embree)

enable_testing()
add_test(NAME embree_ubsan_probe COMMAND probe)
set_tests_properties(embree_ubsan_probe PROPERTIES
    ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1"
    FAIL_REGULAR_EXPRESSION "runtime error:"
)
