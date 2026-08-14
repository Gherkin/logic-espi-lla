cmake_minimum_required(VERSION 3.16)

project(EspiAnalyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ESPI_BUILD_TESTS  "Build the test suite"          ON)
option(ESPI_BUILD_PLUGIN "Build the Logic 2 plugin"      OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
#  espi_core -- protocol decoding. Never includes a Saleae header.
#
#  The include layout enforces rule R3 (see docs/PLAN.md): the mapping tables
#  transcribed from the specification live in a PRIVATE include directory that
#  is a *sibling* of the public one, not a subdirectory of it. Test and encode
#  code therefore cannot reach them -- not by convention, but because the
#  include path does not exist from those targets.
# ---------------------------------------------------------------------------

add_library(espi_core STATIC
    src/core/src/Crc8.cpp
    src/core/src/LaneCodec.cpp
)

target_include_directories(espi_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/tables
)

# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------

if(ESPI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
#  Logic 2 plugin -- needs the Saleae SDK. Off by default so the core and its
#  tests build with no network access and no SDK present.
# ---------------------------------------------------------------------------

if(ESPI_BUILD_PLUGIN)
    include(cmake/AnalyzerSDK.cmake)
    message(STATUS "Plugin target not yet implemented (phase 0 in progress)")
endif()
