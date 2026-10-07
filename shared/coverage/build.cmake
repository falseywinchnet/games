# The coverage engine. Included once by the generated registry (and by a game's own
# standalone build) with ENGINE_DIR set to this folder.
if(TARGET coverage_core)
  return()
endif()
add_library(coverage_core STATIC "${ENGINE_DIR}/src/coverage.cpp")
target_include_directories(coverage_core PUBLIC "${ENGINE_DIR}/src")
target_compile_features(coverage_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(coverage_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(coverage_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(coverage_tests "${ENGINE_DIR}/tests/coverage_tests.cpp")
target_link_libraries(coverage_tests PRIVATE coverage_core)
add_test(NAME coverage_engine COMMAND coverage_tests)
# Thirteen whole lawns are mown: about 20 s optimised, several times that unoptimised.
set_tests_properties(coverage_engine PROPERTIES TIMEOUT 300)
