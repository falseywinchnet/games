add_library(mz_core STATIC ${GAME_MODULE_DIR}/src/soft3d.cpp
 ${GAME_MODULE_DIR}/src/textures.cpp ${GAME_MODULE_DIR}/src/maze.cpp
 ${GAME_MODULE_DIR}/src/idle.cpp ${GAME_MODULE_DIR}/src/world.cpp ${GAME_MODULE_DIR}/src/session.cpp
 ${GAME_MODULE_DIR}/src/rewards.cpp ${GAME_MODULE_DIR}/src/cast.cpp ${GAME_MODULE_DIR}/src/save.cpp
 ${GAME_MODULE_DIR}/src/art.cpp ${GAME_MODULE_DIR}/src/paints.cpp
 ${GAME_MODULE_DIR}/src/snail.cpp
 ${GAME_MODULE_DIR}/src/png.cpp
 ${GAME_MODULE_DIR}/src/residents.cpp
 ${GAME_MODULE_DIR}/src/band.cpp
 ${GAME_MODULE_DIR}/src/wanderer.cpp
 ${GAME_MODULE_DIR}/src/maze_cues.cpp)
target_include_directories(mz_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(mz_core PUBLIC render_core ambient_core)  # the 2D canvas (shared/render); the inflater (shared/ambient)
target_link_libraries(mz_core PUBLIC voice_core)  # the effects and jingles, rendered live (shared/voice)
target_compile_definitions(mz_core PRIVATE _USE_MATH_DEFINES)
if(NOT MSVC)
  set_source_files_properties(${GAME_MODULE_DIR}/src/maze_cues.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
add_executable(maze_rules_tests ${GAME_MODULE_DIR}/tests/maze_tests.cpp)
target_link_libraries(maze_rules_tests PRIVATE mz_core)
add_test(NAME maze_rules COMMAND maze_rules_tests)
set_tests_properties(maze_rules PROPERTIES LABELS design)
add_executable(maze_idle_tests ${GAME_MODULE_DIR}/tests/idle_tests.cpp)
target_link_libraries(maze_idle_tests PRIVATE mz_core)
add_test(NAME maze_idle COMMAND maze_idle_tests)
set_tests_properties(maze_idle PROPERTIES LABELS design)
# The live effects and jingles render, and (with --reference) equal the NumPy masters.
add_executable(maze_cues_tests ${GAME_MODULE_DIR}/tests/maze_cues_tests.cpp)
target_link_libraries(maze_cues_tests PRIVATE mz_core)
add_test(NAME maze_cues COMMAND maze_cues_tests)
add_executable(maze_preview ${GAME_MODULE_DIR}/tests/preview.cpp)
target_link_libraries(maze_preview PRIVATE mz_core)
if(GAMES_BUILD_APPLICATION)
  add_executable(maze_view_contract_tests ${GAME_MODULE_DIR}/tests/view_contract_tests.cpp)
  target_include_directories(maze_view_contract_tests PRIVATE ${PROJECT_SOURCE_DIR}/new-games/kit)
  target_link_libraries(maze_view_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME maze_view_contract COMMAND maze_view_contract_tests)
  set_tests_properties(maze_view_contract PROPERTIES TIMEOUT 90 ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
