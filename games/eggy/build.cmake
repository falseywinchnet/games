add_library(eggy_core STATIC
  ${GAME_MODULE_DIR}/src/world.cpp ${GAME_MODULE_DIR}/src/sim.cpp ${GAME_MODULE_DIR}/src/save.cpp ${GAME_MODULE_DIR}/src/lines.cpp ${GAME_MODULE_DIR}/src/textures.cpp
  ${GAME_MODULE_DIR}/src/ground.cpp ${GAME_MODULE_DIR}/src/flora.cpp ${GAME_MODULE_DIR}/src/critters.cpp ${GAME_MODULE_DIR}/src/duck3d.cpp ${GAME_MODULE_DIR}/src/scene.cpp
  ${GAME_MODULE_DIR}/src/eggy_synth.cpp ${GAME_MODULE_DIR}/src/eggy_effects.cpp ${GAME_MODULE_DIR}/src/eggy_compose.cpp
  ${GAME_MODULE_DIR}/src/eggy_fft.cpp ${GAME_MODULE_DIR}/src/eggy_music.cpp)
target_include_directories(eggy_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(eggy_core PUBLIC render_core)  # the 2D canvas, 3D renderer and meshes (shared/render)
target_link_libraries(eggy_core PUBLIC game_paths)
target_link_libraries(eggy_core PUBLIC voice_core)  # the effects, beds and music, rendered live (shared/voice)
if(NOT MSVC)
  set_source_files_properties(${GAME_MODULE_DIR}/src/eggy_synth.cpp ${GAME_MODULE_DIR}/src/eggy_effects.cpp
    ${GAME_MODULE_DIR}/src/eggy_compose.cpp ${GAME_MODULE_DIR}/src/eggy_fft.cpp ${GAME_MODULE_DIR}/src/eggy_music.cpp
    PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
add_executable(eggy_sim_tests ${GAME_MODULE_DIR}/tests/sim_tests.cpp)
target_link_libraries(eggy_sim_tests PRIVATE eggy_core)
add_test(NAME eggy_sim COMMAND eggy_sim_tests)
set_tests_properties(eggy_sim PROPERTIES LABELS design)
# The live sounds, and (with --reference) their equality to the authoring scripts' masters.
add_executable(eggy_cues_tests ${GAME_MODULE_DIR}/tests/eggy_cues_tests.cpp)
target_link_libraries(eggy_cues_tests PRIVATE eggy_core)
if(NOT MSVC)
  target_compile_options(eggy_cues_tests PRIVATE -ffp-contract=off)
endif()
add_test(NAME eggy_cues COMMAND eggy_cues_tests)
set_tests_properties(eggy_cues PROPERTIES TIMEOUT 300)

target_compile_definitions(eggy_core PRIVATE _USE_MATH_DEFINES)

list(APPEND GAMES_NATIVE_FRAME_MODULES eggy)
