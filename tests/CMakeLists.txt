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
