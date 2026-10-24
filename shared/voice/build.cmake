# The live voice engine. Included once by the generated registry (and by a game's own
# standalone build) with ENGINE_DIR set to this folder.
if(TARGET voice_core)
  return()
endif()
add_library(voice_core STATIC
  "${ENGINE_DIR}/src/voice_bank.cpp"
  "${ENGINE_DIR}/src/voice_fft.cpp"
  "${ENGINE_DIR}/src/voice_random.cpp"
  "${ENGINE_DIR}/src/voice_synth.cpp")
target_include_directories(voice_core PUBLIC "${ENGINE_DIR}/src")
target_compile_features(voice_core PUBLIC cxx_std_20)
# The ports follow numpy's arithmetic exactly; contracted multiply-adds would change it.
if(MSVC)
  target_compile_options(voice_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(voice_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(voice_tests "${ENGINE_DIR}/tests/voice_tests.cpp")
target_link_libraries(voice_tests PRIVATE voice_core)
if(NOT MSVC)
  target_compile_options(voice_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME voice_engine COMMAND voice_tests)
set_tests_properties(voice_engine PROPERTIES TIMEOUT 120)
