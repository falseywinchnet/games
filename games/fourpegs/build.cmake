if(NOT DEFINED PLAYSUITE_SOURCE_DIR)
  set(PLAYSUITE_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
endif()
add_library(fp_core STATIC ${GAME_MODULE_DIR}/src/platform/lines.cpp
  ${GAME_MODULE_DIR}/src/board.cpp ${GAME_MODULE_DIR}/src/vface.cpp ${GAME_MODULE_DIR}/src/villain.cpp
  ${GAME_MODULE_DIR}/src/lair.cpp ${GAME_MODULE_DIR}/src/script.cpp ${GAME_MODULE_DIR}/src/vactor.cpp
  ${GAME_MODULE_DIR}/src/save.cpp)
target_include_directories(fp_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(fp_core PUBLIC render_core)  # the 2D canvas, 3D renderer and meshes (shared/render)
target_link_libraries(fp_core PUBLIC game_paths)
target_compile_definitions(fp_core PRIVATE _USE_MATH_DEFINES)
add_executable(fourpegs_rules_tests ${GAME_MODULE_DIR}/tests/board_tests.cpp)
target_link_libraries(fourpegs_rules_tests PRIVATE fp_core)
add_executable(fourpegs_preview ${GAME_MODULE_DIR}/tests/preview.cpp)
target_link_libraries(fourpegs_preview PRIVATE fp_core)
add_executable(fourpegs_storage_tests ${PLAYSUITE_SOURCE_DIR}/tests/fourpegs_storage_tests.cpp)
target_link_libraries(fourpegs_storage_tests PRIVATE fp_core)
add_test(NAME fourpegs_rules COMMAND fourpegs_rules_tests)
add_test(NAME fourpegs_storage COMMAND fourpegs_storage_tests)
target_compile_definitions(fp_core PRIVATE _USE_MATH_DEFINES)

list(APPEND GAMES_NATIVE_FRAME_MODULES fourpegs)
