# Core tests link espi_core only. No Saleae headers reach this directory --
# that is what keeps the core honest about its dependencies.

function(espi_add_core_test NAME)
    add_executable(${NAME} ${NAME}.cpp)
    target_link_libraries(${NAME} PRIVATE espi_core)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

espi_add_core_test(test_crc)
espi_add_core_test(test_lanes)
espi_add_core_test(test_opcodes)
espi_add_core_test(test_link)

# Fixtures and their expected decodes are read at run time rather than baked
# into the binary, so a reviewer can read the literal bytes and the literal
# text side by side without going through C++ string escaping.
target_compile_definitions(test_link PRIVATE
    ESPI_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")

# ---------------------------------------------------------------------------
#  T2 -- sampling and integration. Needs the SDK, so it only exists when the
#  plugin is enabled. The core tests above stay buildable with no SDK present.
#
#  This links espi_shell (the analyzer sources, compiled once) against the SDK's
#  offline STUBS -- never Saleae::AnalyzerSDK, which only the plugin links.
#  FrameV2SinkRecording.cpp replaces FrameV2SinkLogic2.cpp here: testlib defines
#  no FrameV2 symbols at all, so the Logic 2 sink would fail to link.
#  docs/PLAN.md section 6, gotchas 3 and 4.
#
#  R3 still holds: espi_core exposes only src/core/include, so src/core/tables/
#  is no more reachable from this test than from any other.
# ---------------------------------------------------------------------------

if(ESPI_BUILD_PLUGIN)
    espi_add_test_harness(espi_test_harness)

    add_executable(test_sampling
        test_sampling.cpp
        ${CMAKE_SOURCE_DIR}/src/saleae/FrameV2SinkRecording.cpp
    )
    target_link_libraries(test_sampling PRIVATE espi_shell espi_test_harness)
    target_include_directories(test_sampling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_sampling PRIVATE
        ESPI_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
    add_test(NAME test_sampling COMMAND test_sampling)
endif()

# ---------------------------------------------------------------------------
#  Rule R3: the specification-derived tables must be unreachable from here.
#
#  r3_seam_probe.cpp includes a private table header. It is excluded from the
#  default build and compiled only by this test, which expects the compile to
#  FAIL. A passing `r3_seam` therefore means "the seam holds".
# ---------------------------------------------------------------------------

add_executable(r3_seam_probe EXCLUDE_FROM_ALL r3_seam_probe.cpp)
target_link_libraries(r3_seam_probe PRIVATE espi_core)

add_test(NAME r3_seam
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                                  --target r3_seam_probe
                                  --config $<CONFIG>)
set_tests_properties(r3_seam PROPERTIES
    WILL_FAIL TRUE
    LABELS "seam")
