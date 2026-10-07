add_library(kk_core STATIC ${GAME_MODULE_DIR}/src/koikoi.cpp ${GAME_MODULE_DIR}/src/platform/raster.cpp)
target_include_directories(kk_core PUBLIC ${GAME_MODULE_DIR}/src)
target_link_libraries(kk_core PUBLIC game_paths Threads::Threads)
target_compile_definitions(kk_core PRIVATE _USE_MATH_DEFINES)
add_executable(koikoi_rules_tests ${GAME_MODULE_DIR}/tests/koikoi_tests.cpp)
target_link_libraries(koikoi_rules_tests PRIVATE kk_core)
add_test(NAME koikoi_rules COMMAND koikoi_rules_tests)

list(APPEND GAMES_NATIVE_FRAME_MODULES koikoi)

# The prepared deck: compressed PNG decoding to the pixels the preparation recorded.
if(GAMES_BUILD_APPLICATION)
  add_executable(koikoi_card_art_tests ${GAME_MODULE_DIR}/tests/card_art_tests.cpp ${GAME_MODULE_DIR}/src/platform/image.cpp)
  target_link_libraries(koikoi_card_art_tests PRIVATE kk_core ambient_core)
  add_test(NAME koikoi_card_art COMMAND koikoi_card_art_tests "${GAMES_RUNTIME_ASSET_DIR}")
  set_tests_properties(koikoi_card_art PROPERTIES TIMEOUT 60)
endif()
