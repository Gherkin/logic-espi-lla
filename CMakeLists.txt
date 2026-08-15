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

# The shell's objects end up inside a loadable module, so everything they are
# linked with has to be position independent -- including the otherwise-static
# core. Harmless when the plugin is off.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
    src/core/src/Decode.cpp
    src/core/src/Opcodes.cpp
    src/core/src/Responses.cpp
    src/core/src/Status.cpp
    src/core/src/PacketShapes.cpp
    src/core/src/ConfigRegisters.cpp
    src/core/src/VirtualWires.cpp
    src/core/src/CycleTypes.cpp
    src/core/src/LinkDecoder.cpp
)

target_include_directories(espi_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core/tables
)

# ---------------------------------------------------------------------------
#  Saleae shell and Logic 2 plugin -- need the SDK. Off by default so the core
#  and its tests build with no network access and no SDK present.
#
#  This block comes BEFORE add_subdirectory(tests) because the T2 test links
#  espi_shell and the SDK's offline harness, both defined here.
#
#  THE LOGIC2 SPLIT. FrameV2 is declared only behind #ifdef LOGIC2 in the SDK's
#  AnalyzerResults.h and is defined nowhere in testlib/, so a translation unit
#  that names it cannot appear in a test binary's link (docs/PLAN.md section 6,
#  gotcha 3). Exactly one small file names FrameV2 and only the plugin compiles
#  it. Everything else compiles once, as an OBJECT library, and is reused by
#  both binaries (gotcha 4).
#
#  The split is ABI-safe: LOGIC2 guards only the FrameV2 class itself and the
#  non-virtual AddFrameV2, so it changes no vtable.
# ---------------------------------------------------------------------------

if(ESPI_BUILD_PLUGIN)
    include(cmake/AnalyzerSDK.cmake)

    add_library(espi_shell OBJECT
        src/saleae/SamplingByteSource.cpp
        src/saleae/EspiAnalyzerSettings.cpp
        src/saleae/EspiAnalyzerResults.cpp
        src/saleae/EspiAnalyzer.cpp
    )

    # SDK headers, but deliberately NOT the SDK library: which implementation
    # of those headers gets linked is the consumer's choice, and that is the
    # whole point of the seam.
    target_include_directories(espi_shell PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/saleae
        ${ESPI_SDK_ROOT}/include
    )
    target_link_libraries(espi_shell PUBLIC espi_core)

    add_library(espi_analyzer MODULE
        src/saleae/FrameV2SinkLogic2.cpp
    )
    target_link_libraries(espi_analyzer PRIVATE espi_shell Saleae::AnalyzerSDK)
    target_compile_definitions(espi_analyzer PRIVATE LOGIC2)
endif()

# ---------------------------------------------------------------------------
#  Tests
# ---------------------------------------------------------------------------

if(ESPI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
