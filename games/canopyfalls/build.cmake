# Included by the generated registry (and by CMakeLists.txt), with GAME_MODULE_DIR set
# to this folder. The ambient, particles and render engines are built first.
add_library(cf_core STATIC
  "${GAME_MODULE_DIR}/src/sky.cpp"
  "${GAME_MODULE_DIR}/src/still.cpp"
  "${GAME_MODULE_DIR}/src/falls_voice.cpp")
target_include_directories(cf_core PUBLIC "${GAME_MODULE_DIR}/src")
target_link_libraries(cf_core PUBLIC particles_core)
if(MSVC)
  target_compile_options(cf_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(cf_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
# The scene drawn and heard headless: baking, frames, the particles and the sound.
add_executable(canopyfalls_tests "${GAME_MODULE_DIR}/tests/falls_tests.cpp")
target_link_libraries(canopyfalls_tests PRIVATE cf_core)
add_test(NAME canopyfalls COMMAND canopyfalls_tests)
set_tests_properties(canopyfalls PROPERTIES TIMEOUT 300)

# Tools: the sound's stems to files.
add_executable(cf_sound_render "${GAME_MODULE_DIR}/tools/sound_render.cpp")
target_link_libraries(cf_sound_render PRIVATE cf_core)
get_filename_component(CF_KIT_DIR "${GAME_MODULE_DIR}/../../new-games/kit" ABSOLUTE)
if(EXISTS "${CF_KIT_DIR}/png_writer.hpp")
  # Stills of candidate looks, and the live scene drawn headless and timed.
  add_executable(cf_candidates "${GAME_MODULE_DIR}/tools/candidates.cpp")
  target_include_directories(cf_candidates PRIVATE "${CF_KIT_DIR}")
  target_link_libraries(cf_candidates PRIVATE cf_core)
  add_executable(cf_live_preview "${GAME_MODULE_DIR}/tools/live_preview.cpp")
  target_include_directories(cf_live_preview PRIVATE "${CF_KIT_DIR}")
  target_link_libraries(cf_live_preview PRIVATE cf_core)
endif()

if(GAMES_BUILD_APPLICATION)
  add_executable(canopyfalls_view_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(canopyfalls_view_contract_tests PRIVATE "${PROJECT_SOURCE_DIR}/new-games/kit")
  target_link_libraries(canopyfalls_view_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME canopyfalls_view_contract COMMAND canopyfalls_view_contract_tests)
  set_tests_properties(canopyfalls_view_contract PROPERTIES TIMEOUT 180
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
