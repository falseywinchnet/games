# Included by the generated registry, with GAME_MODULE_DIR set to this folder.
add_library(tg_core STATIC
  "${GAME_MODULE_DIR}/src/platform/raster.cpp"
  "${GAME_MODULE_DIR}/src/rules.cpp"
  "${GAME_MODULE_DIR}/src/save.cpp"
  "${GAME_MODULE_DIR}/src/stage.cpp")
target_include_directories(tg_core PUBLIC "${GAME_MODULE_DIR}/src")
# Saves and assets come through PlaySuite (src/game_data.hpp).
target_link_libraries(tg_core PUBLIC game_paths)
target_compile_definitions(tg_core PRIVATE _USE_MATH_DEFINES)
add_executable(templategame_rules_tests "${GAME_MODULE_DIR}/tests/rules_tests.cpp")
target_link_libraries(templategame_rules_tests PRIVATE tg_core)
add_test(NAME templategame_rules COMMAND templategame_rules_tests)
set_tests_properties(templategame_rules PROPERTIES LABELS design)
if(GAMES_BUILD_APPLICATION)
  add_executable(templategame_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(templategame_contract_tests PRIVATE "${CMAKE_SOURCE_DIR}/new-games/kit")
  target_link_libraries(templategame_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME templategame_view_contract COMMAND templategame_contract_tests)
  set_tests_properties(templategame_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
