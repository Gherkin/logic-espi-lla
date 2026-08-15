# Fetch the Saleae Analyzer SDK.
#
# Pinned to an exact commit deliberately: the SampleAnalyzer uses
# `GIT_TAG master`, which makes builds non-reproducible and means an upstream
# change can break a previously-green build with no local edit.
#
# To develop against a local SDK checkout without editing this file, use the
# standard CMake override:
#
#   cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_ANALYZERSDK=/path/to/AnalyzerSDK

set(ESPI_SDK_GIT_TAG "e3c4b4ffda1c162c024a5913520aed17c6edb238"
    CACHE STRING "Saleae AnalyzerSDK revision (pinned; 2026-03-26)")

include(FetchContent)
FetchContent_Declare(analyzersdk
    GIT_REPOSITORY https://github.com/saleae/AnalyzerSDK.git
    GIT_TAG        ${ESPI_SDK_GIT_TAG}
    GIT_SHALLOW    False
)
FetchContent_MakeAvailable(analyzersdk)

include("${analyzersdk_SOURCE_DIR}/AnalyzerSDKConfig.cmake")
set(ESPI_SDK_ROOT "${analyzersdk_SOURCE_DIR}")

# The SDK's own testlib/CMakeLists.txt hardcodes ${PROJECT_SOURCE_DIR}/AnalyzerSDK
# for its include path, which is wrong for any project consuming the SDK.
# Build the harness ourselves over its source list instead.
#
# NOTE the harness is header-and-stub only: it deliberately does NOT link
# Saleae::AnalyzerSDK. A test binary linking both would get two definitions of
# every SDK symbol. See docs/PLAN.md section 6, gotcha 4.
function(espi_add_test_harness TARGET)
    set(_h "${ESPI_SDK_ROOT}/testlib")
    add_library(${TARGET} STATIC
        ${_h}/AnalyzerStubs.cpp
        ${_h}/HelperStubs.cpp
        ${_h}/MockChannelData.cpp
        ${_h}/MockSimulatedChannelDescriptor.cpp
        ${_h}/MockResults.cpp
        ${_h}/SettingsStubs.cpp
        ${_h}/StreamHelpers.cpp
        ${_h}/TestInstance.cpp
    )
    target_include_directories(${TARGET} PUBLIC ${_h} ${ESPI_SDK_ROOT}/include)
endfunction()
