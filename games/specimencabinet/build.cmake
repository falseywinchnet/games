# Included by the generated registry, with GAME_MODULE_DIR set to this folder.
add_library(sc_core STATIC
  "${GAME_MODULE_DIR}/src/rules.cpp"
  "${GAME_MODULE_DIR}/src/save.cpp"
  "${GAME_MODULE_DIR}/src/stage.cpp"
  "${GAME_MODULE_DIR}/src/tumbler.cpp")
target_include_directories(sc_core PUBLIC "${GAME_MODULE_DIR}/src")
# Saves and assets come through PlaySuite (src/game_data.hpp); the stones, the cabinet and
# the tumbler are drawn by the stones engine and the render engine (shared/stones, shared/render).
target_link_libraries(sc_core PUBLIC game_paths stones_core render_core)
target_compile_definitions(sc_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(sc_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(sc_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(specimencabinet_rules_tests "${GAME_MODULE_DIR}/tests/rules_tests.cpp")
target_link_libraries(specimencabinet_rules_tests PRIVATE sc_core)
add_test(NAME specimencabinet_rules COMMAND specimencabinet_rules_tests)
set_tests_properties(specimencabinet_rules PROPERTIES LABELS design)
if(GAMES_BUILD_APPLICATION)
  add_executable(specimencabinet_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(specimencabinet_contract_tests PRIVATE "${CMAKE_SOURCE_DIR}/new-games/kit")
  target_link_libraries(specimencabinet_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME specimencabinet_view_contract COMMAND specimencabinet_contract_tests)
  set_tests_properties(specimencabinet_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
else()
  # The preview, only in the game's own build: the scene with headless text and the card
  # tables' felt from the suite's fibre renderer.
  set(SC_KIT_DIR "${PLAYSUITE_SOURCE_DIR}/new-games/kit")
  add_library(sc_headless_text STATIC "${SC_KIT_DIR}/headless_text.cpp")
  target_include_directories(sc_headless_text PUBLIC "${SC_KIT_DIR}")
  target_compile_definitions(sc_headless_text PRIVATE KIT_FONT_DIR="${PLAYSUITE_SOURCE_DIR}/assets/fonts")
  add_library(sc_felt STATIC "${PLAYSUITE_SOURCE_DIR}/shared/felt/carpet.cpp" "${PLAYSUITE_SOURCE_DIR}/shared/felt/image.cpp")
  target_include_directories(sc_felt PUBLIC "${PLAYSUITE_SOURCE_DIR}/shared/felt")
  add_library(sc_headless STATIC "${GAME_MODULE_DIR}/src/scene.cpp" "${GAME_MODULE_DIR}/dev/text_headless.cpp"
    "${GAME_MODULE_DIR}/dev/audio_silent.cpp")
  target_include_directories(sc_headless PUBLIC "${GAME_MODULE_DIR}/dev" "${GAME_MODULE_DIR}/src/platform")
  target_link_libraries(sc_headless PUBLIC sc_core sc_headless_text)
  add_executable(sc_preview "${GAME_MODULE_DIR}/tools/preview.cpp")
  target_link_libraries(sc_preview PRIVATE sc_headless sc_felt)
endif()
