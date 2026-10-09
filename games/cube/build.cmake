# Included by the generated registry (and by the standalone build), with GAME_MODULE_DIR
# set to this folder. The core is portable: rules, solver, generator, saves, motion and
# the cube's scene. The view is compiled only into the application.
if(NOT DEFINED PLAYSUITE_SOURCE_DIR)
  set(PLAYSUITE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
endif()
add_library(cube_core STATIC
  "${GAME_MODULE_DIR}/src/cube.cpp" "${GAME_MODULE_DIR}/src/solver.cpp"
  "${GAME_MODULE_DIR}/src/generator.cpp" "${GAME_MODULE_DIR}/src/session.cpp"
  "${GAME_MODULE_DIR}/src/stage.cpp" "${GAME_MODULE_DIR}/src/scene.cpp"
  "${GAME_MODULE_DIR}/src/picture.cpp")
target_include_directories(cube_core PUBLIC "${GAME_MODULE_DIR}/src")
# The ambient engine's inflate decodes the lake picture's PNG; the shared renderer draws.
target_link_libraries(cube_core PUBLIC ambient_core render_core)
target_compile_features(cube_core PUBLIC cxx_std_20)
target_compile_definitions(cube_core PRIVATE _USE_MATH_DEFINES)
if(NOT MSVC)
  target_compile_options(cube_core PRIVATE -Wall -Wextra)
endif()
add_executable(cube_rules_tests "${GAME_MODULE_DIR}/tests/rules_tests.cpp")
target_link_libraries(cube_rules_tests PRIVATE cube_core)
add_test(NAME cube_rules COMMAND cube_rules_tests rules)
set_tests_properties(cube_rules PROPERTIES LABELS design)
add_test(NAME cube_generator COMMAND cube_rules_tests generator)
set_tests_properties(cube_generator PROPERTIES LABELS design)
add_test(NAME cube_saves COMMAND cube_rules_tests saves "${GAME_MODULE_DIR}/tests/fixtures")
add_test(NAME cube_tiers COMMAND cube_rules_tests tiers)
set_tests_properties(cube_tiers PROPERTIES LABELS design)
set_tests_properties(cube_generator cube_tiers PROPERTIES TIMEOUT 240)
add_executable(cube_preview "${GAME_MODULE_DIR}/tools/preview.cpp")
target_include_directories(cube_preview PRIVATE "${PLAYSUITE_SOURCE_DIR}/new-games/kit")
target_link_libraries(cube_preview PRIVATE cube_core)
add_executable(cube_bench "${GAME_MODULE_DIR}/tools/bench.cpp")
target_link_libraries(cube_bench PRIVATE cube_core)
add_executable(cube_tiers "${GAME_MODULE_DIR}/tools/tiers.cpp")
target_link_libraries(cube_tiers PRIVATE cube_core)
if(GAMES_BUILD_APPLICATION)
  add_executable(cube_view_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(cube_view_contract_tests PRIVATE "${PLAYSUITE_SOURCE_DIR}/new-games/kit")
  target_link_libraries(cube_view_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME cube_view_contract COMMAND cube_view_contract_tests)
  set_tests_properties(cube_view_contract PROPERTIES TIMEOUT 120
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()

# Nature Cube's music and effects are synthesized
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
