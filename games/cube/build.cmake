# Nature Cube plays on the shared puzzle engine. Its music and effects are synthesized
# live: the application compiles them through GAME.json's audio_sources, beside the
# toolkit's audio. This core library builds the same synthesis without the toolkit, for
# the tests and for the offline renderer (cube_audio_render).
if(NOT TARGET cube_audio_core)
  add_library(cube_audio_core STATIC
    "${GAME_MODULE_DIR}/src/glass_music.cpp"
    "${GAME_MODULE_DIR}/src/glass_effects.cpp")
  target_include_directories(cube_audio_core PUBLIC "${GAME_MODULE_DIR}/src")
  target_compile_features(cube_audio_core PUBLIC cxx_std_20)
  if(NOT MSVC)
    target_compile_options(cube_audio_core PRIVATE -Wall -Wextra)
  endif()
  add_executable(cube_audio_tests "${GAME_MODULE_DIR}/tests/audio_tests.cpp")
  target_link_libraries(cube_audio_tests PRIVATE cube_audio_core)
  add_test(NAME cube_audio COMMAND cube_audio_tests)
  set_tests_properties(cube_audio PROPERTIES TIMEOUT 180)
  add_executable(cube_audio_render "${GAME_MODULE_DIR}/tools/cube_audio_render.cpp")
  target_link_libraries(cube_audio_render PRIVATE cube_audio_core)
endif()
