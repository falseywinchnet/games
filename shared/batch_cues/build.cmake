# The audio batch 1/2 cues. Included once by the generated registry (and by a game's
# own standalone build) with ENGINE_DIR set to this folder.
if(TARGET batch_cues_core)
  return()
endif()
# The cues render on the live voice engine (shared/voice); it is built first.
if(NOT TARGET voice_core)
  set(BATCH_CUES_ENGINE_DIR "${ENGINE_DIR}")
  get_filename_component(ENGINE_DIR "${BATCH_CUES_ENGINE_DIR}/../voice" ABSOLUTE)
  include("${ENGINE_DIR}/build.cmake")
  set(ENGINE_DIR "${BATCH_CUES_ENGINE_DIR}")
endif()
add_library(batch_cues_core STATIC
  "${ENGINE_DIR}/src/batch_cues.cpp"
  "${ENGINE_DIR}/src/batch_kit.cpp"
  "${ENGINE_DIR}/src/batch_music.cpp"
  "${ENGINE_DIR}/src/batch_sfx.cpp"
  "${ENGINE_DIR}/src/batch_sfx2.cpp")
target_include_directories(batch_cues_core PUBLIC "${ENGINE_DIR}/src")
target_link_libraries(batch_cues_core PUBLIC voice_core)
target_compile_features(batch_cues_core PUBLIC cxx_std_20)
# The ports follow numpy's arithmetic exactly; contracted multiply-adds would change it.
if(MSVC)
  target_compile_options(batch_cues_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(batch_cues_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
# Without arguments every cue renders at its master's length and level; with
# --reference DIR (tools/batch_reference.py) every cue equals its NumPy master.
add_executable(batch_cues_tests "${ENGINE_DIR}/tests/batch_cues_tests.cpp")
target_link_libraries(batch_cues_tests PRIVATE batch_cues_core)
if(NOT MSVC)
  target_compile_options(batch_cues_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME batch_cues COMMAND batch_cues_tests)
set_tests_properties(batch_cues PROPERTIES TIMEOUT 300)
