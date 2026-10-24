# The PlaySuite studio. Included once by the generated registry (and by a game's own
# standalone build) with ENGINE_DIR set to this folder.
if(TARGET studio_core)
  return()
endif()
# Its cues reach the shell through the live voice bank (shared/voice), built first.
if(NOT TARGET voice_core)
  set(STUDIO_ENGINE_DIR "${ENGINE_DIR}")
  get_filename_component(ENGINE_DIR "${STUDIO_ENGINE_DIR}/../voice" ABSOLUTE)
  include("${ENGINE_DIR}/build.cmake")
  set(ENGINE_DIR "${STUDIO_ENGINE_DIR}")
endif()
add_library(studio_core STATIC
  "${ENGINE_DIR}/src/studio_card_sfx.cpp"
  "${ENGINE_DIR}/src/studio_crew.cpp"
  "${ENGINE_DIR}/src/studio_cues.cpp"
  "${ENGINE_DIR}/src/studio_mix.cpp"
  "${ENGINE_DIR}/src/studio_score.cpp"
  "${ENGINE_DIR}/src/studio_song_cards.cpp"
  "${ENGINE_DIR}/src/studio_song_menu.cpp"
  "${ENGINE_DIR}/src/studio_song_puzzles.cpp"
  "${ENGINE_DIR}/src/studio_songs_common.cpp"
  "${ENGINE_DIR}/src/studio_stingers.cpp"
  "${ENGINE_DIR}/src/studio_voice.cpp")
target_include_directories(studio_core PUBLIC "${ENGINE_DIR}/src")
target_link_libraries(studio_core PUBLIC voice_core)
target_compile_features(studio_core PUBLIC cxx_std_20)
# The studio's arithmetic is reproduced exactly; contracted multiply-adds would change it.
if(MSVC)
  target_compile_options(studio_core PRIVATE /utf-8 /fp:precise)
else()
  target_compile_options(studio_core PRIVATE -ffp-contract=off)
endif()
# register_studio_cues(): the cues with GUI.Forms Workers as their render crew, for the shell.
if(TARGET GUIForms::Threading)
  add_library(studio_workers STATIC "${ENGINE_DIR}/ui/studio_workers.cpp")
  target_link_libraries(studio_workers PUBLIC studio_core PRIVATE GUIForms::Threading)
endif()
include(CTest)
add_executable(studio_cues_tests "${ENGINE_DIR}/tests/studio_cues_tests.cpp")
target_link_libraries(studio_cues_tests PRIVATE studio_core)
if(TARGET studio_workers)
  target_link_libraries(studio_cues_tests PRIVATE studio_workers)
  target_compile_definitions(studio_cues_tests PRIVATE STUDIO_WORKERS=1)
endif()
if(NOT MSVC)
  target_compile_options(studio_cues_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME studio_cues COMMAND studio_cues_tests)
set_tests_properties(studio_cues PROPERTIES TIMEOUT 300)
