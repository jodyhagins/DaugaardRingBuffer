cmake_minimum_required(VERSION 3.26)

project(DaugaardRingBuffer
        VERSION 1.0
        DESCRIPTION "Kaspar Daugaard's RingBuffer"
        LANGUAGES CXX)

# clang on apple has a bug where __GCC_DESTRUCTIVE_SIZE and
# __GCC_CONSTRUCTIVE_SIZE have the wrong values on some platforms,
# which means that std::hardware_destructive_interference_size is also wrong
# and yields the wrong cache line size.
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND ${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
    option(DAUGAARD_RING_BUFFER_DETERMINE_CACHE_LINE_SIZE
        "Compute value for DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE if not set"
        ON)
else ()
    option(DAUGAARD_RING_BUFFER_DETERMINE_CACHE_LINE_SIZE
        "Compute value for DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE if not set"
        OFF)
endif ()

if (NOT DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE AND DAUGAARD_RING_BUFFER_DETERMINE_CACHE_LINE_SIZE)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        execute_process(
            COMMAND sysctl -n hw.cachelinesize
            OUTPUT_VARIABLE DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        execute_process(
            COMMAND getconf LEVEL1_DCACHE_LINESIZE
            OUTPUT_VARIABLE DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    else ()
        message(FATAL_ERROR "Unrecognized CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
    endif ()
    message(STATUS "Computed DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE=${DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE}")
endif ()

add_library(DaugaardRingBuffer
    INTERFACE
        src/daugaard/ring_buffer.hpp
)
add_library(daugaard::ring_buffer ALIAS DaugaardRingBuffer)

target_include_directories(DaugaardRingBuffer
    INTERFACE "${PROJECT_SOURCE_DIR}/src")

if (DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE)
    target_compile_definitions(DaugaardRingBuffer
        INTERFACE
            DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE=${DAUGAARD_RING_BUFFER_CACHE_LINE_SIZE}
    )
endif ()
