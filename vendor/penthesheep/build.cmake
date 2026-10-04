add_library(sh_core STATIC ${GAME_MODULE_DIR}/src/field.cpp ${GAME_MODULE_DIR}/src/pasture.cpp ${GAME_MODULE_DIR}/src/platform/raster.cpp ${GAME_MODULE_DIR}/src/platform/r3d.cpp ${GAME_MODULE_DIR}/src/platform/mesh.cpp)
target_include_directories(sh_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(sh_core PUBLIC game_paths Threads::Threads)
target_compile_definitions(sh_core PRIVATE _USE_MATH_DEFINES)
add_executable(penthesheep_rules_tests ${GAME_MODULE_DIR}/tests/sheep_tests.cpp)
target_link_libraries(penthesheep_rules_tests PRIVATE sh_core)
add_test(NAME penthesheep_rules COMMAND penthesheep_rules_tests)

list(APPEND GAMES_NATIVE_FRAME_MODULES penthesheep)
