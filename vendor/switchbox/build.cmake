if(NOT DEFINED PLAYSUITE_SOURCE_DIR)
  set(PLAYSUITE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
endif()
add_library(sbx_core STATIC
  ${GAME_MODULE_DIR}/src/platform/raster.cpp ${GAME_MODULE_DIR}/src/platform/r3d.cpp
  ${GAME_MODULE_DIR}/src/platform/mesh.cpp ${GAME_MODULE_DIR}/src/platform/lines.cpp
  ${GAME_MODULE_DIR}/src/puzzle.cpp ${GAME_MODULE_DIR}/src/face.cpp ${GAME_MODULE_DIR}/src/girl.cpp
  ${GAME_MODULE_DIR}/src/stage.cpp ${GAME_MODULE_DIR}/src/actor.cpp ${GAME_MODULE_DIR}/src/save.cpp
  ${GAME_MODULE_DIR}/src/mole.cpp)
target_include_directories(sbx_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(sbx_core PUBLIC game_paths)
add_executable(switchbox_rules_tests ${GAME_MODULE_DIR}/tests/puzzle_tests.cpp)
target_link_libraries(switchbox_rules_tests PRIVATE sbx_core)
add_test(NAME switchbox_rules COMMAND switchbox_rules_tests)
add_executable(switchbox_storage_tests ${PLAYSUITE_SOURCE_DIR}/tests/switchbox_storage_tests.cpp)
target_link_libraries(switchbox_storage_tests PRIVATE sbx_core)
add_test(NAME switchbox_storage COMMAND switchbox_storage_tests)
add_executable(switchbox_preview ${GAME_MODULE_DIR}/tests/preview.cpp)
target_link_libraries(switchbox_preview PRIVATE sbx_core)
add_executable(switchbox_actor_tests ${PLAYSUITE_SOURCE_DIR}/tests/switchbox_actor_tests.cpp)
target_link_libraries(switchbox_actor_tests PRIVATE sbx_core)
add_test(NAME switchbox_actor COMMAND switchbox_actor_tests)

target_compile_definitions(sbx_core PRIVATE _USE_MATH_DEFINES)

list(APPEND GAMES_NATIVE_FRAME_MODULES switchbox)
