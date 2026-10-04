add_library(eggy_core STATIC
  ${GAME_MODULE_DIR}/src/world.cpp ${GAME_MODULE_DIR}/src/sim.cpp ${GAME_MODULE_DIR}/src/save.cpp ${GAME_MODULE_DIR}/src/lines.cpp
  ${GAME_MODULE_DIR}/src/raster.cpp ${GAME_MODULE_DIR}/src/r3d.cpp ${GAME_MODULE_DIR}/src/mesh.cpp ${GAME_MODULE_DIR}/src/textures.cpp
  ${GAME_MODULE_DIR}/src/ground.cpp ${GAME_MODULE_DIR}/src/flora.cpp ${GAME_MODULE_DIR}/src/critters.cpp ${GAME_MODULE_DIR}/src/duck3d.cpp ${GAME_MODULE_DIR}/src/scene.cpp)
target_include_directories(eggy_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(eggy_core PUBLIC game_paths)
add_executable(eggy_sim_tests ${GAME_MODULE_DIR}/tests/sim_tests.cpp)
target_link_libraries(eggy_sim_tests PRIVATE eggy_core)
add_test(NAME eggy_sim COMMAND eggy_sim_tests)

target_compile_definitions(eggy_core PRIVATE _USE_MATH_DEFINES)

list(APPEND GAMES_NATIVE_FRAME_MODULES eggy)
