# Included by the generated registry (and by CMakeLists.txt), with GAME_MODULE_DIR set
# to this folder. The ambient, coverage and grass engines are built first.
#
# The mown turf comes from the suite's felt generator (shared/felt), linked once by
# the application as game_felt; headless builds compile the same sources here.
if(GAMES_BUILD_APPLICATION)
  set(MM_FELT game_felt)
else()
  if(NOT TARGET mm_felt)
    add_library(mm_felt STATIC "${GAME_MODULE_DIR}/../../shared/felt/carpet.cpp" "${GAME_MODULE_DIR}/../../shared/felt/image.cpp")
    target_compile_features(mm_felt PUBLIC cxx_std_20)
  endif()
  set(MM_FELT mm_felt)
endif()
add_library(mm_core STATIC
  "${GAME_MODULE_DIR}/src/garden.cpp"
  "${GAME_MODULE_DIR}/src/sim.cpp"
  "${GAME_MODULE_DIR}/src/grass_art.cpp"
  "${GAME_MODULE_DIR}/src/mower_voice.cpp"
  "${GAME_MODULE_DIR}/src/chipper_voice.cpp"
  "${GAME_MODULE_DIR}/src/ambience_voice.cpp"
  "${GAME_MODULE_DIR}/src/banjo_voice.cpp"
  "${GAME_MODULE_DIR}/src/granny_voice.cpp"
  "${GAME_MODULE_DIR}/src/art.cpp"
  "${GAME_MODULE_DIR}/src/model3d.cpp"
  "${GAME_MODULE_DIR}/src/stones.cpp"
  "${GAME_MODULE_DIR}/src/models_shed.cpp"
  "${GAME_MODULE_DIR}/src/models_garden.cpp"
  "${GAME_MODULE_DIR}/src/models_figures.cpp"
  "${GAME_MODULE_DIR}/src/models_granny.cpp"
  "${GAME_MODULE_DIR}/src/models_gnome.cpp"
  "${GAME_MODULE_DIR}/src/models_mower.cpp"
  "${GAME_MODULE_DIR}/src/models_common.cpp"
  "${GAME_MODULE_DIR}/src/yard.cpp"
  "${GAME_MODULE_DIR}/src/platform/raster.cpp")
target_include_directories(mm_core PUBLIC "${GAME_MODULE_DIR}/src" "${GAME_MODULE_DIR}/../../shared/felt")
find_package(Threads REQUIRED)
target_link_libraries(mm_core PUBLIC coverage_core grass_core ${MM_FELT} Threads::Threads)
target_compile_definitions(mm_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(mm_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(mm_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()
include(CTest)
add_executable(mowingman_rules_tests "${GAME_MODULE_DIR}/tests/mowing_tests.cpp")
target_link_libraries(mowingman_rules_tests PRIVATE mm_core)
add_test(NAME mowingman_rules COMMAND mowingman_rules_tests)
set_tests_properties(mowingman_rules PROPERTIES TIMEOUT 180)
# Whole gardens mowed to the end, one test each so they run in parallel.
foreach(garden 1 2 3 4)
  add_test(NAME mowingman_complete_${garden} COMMAND mowingman_rules_tests complete ${garden})
  set_tests_properties(mowingman_complete_${garden} PROPERTIES TIMEOUT 300)
endforeach()

# The mower's voice: live in the game where the toolkit can play a generator, and
# rendered to clips by mm_mower_loops for where it cannot (audio_src/README.md).
add_executable(mowingman_voice_tests "${GAME_MODULE_DIR}/tests/mower_voice_tests.cpp")
target_link_libraries(mowingman_voice_tests PRIVATE mm_core)
add_test(NAME mowingman_voice COMMAND mowingman_voice_tests)
set_tests_properties(mowingman_voice PROPERTIES TIMEOUT 120)
# The music: the banjo band improvising live, and its renderer (any style, one part alone).
add_executable(mowingman_banjo_tests "${GAME_MODULE_DIR}/tests/banjo_voice_tests.cpp")
target_link_libraries(mowingman_banjo_tests PRIVATE mm_core)
add_test(NAME mowingman_banjo COMMAND mowingman_banjo_tests)
set_tests_properties(mowingman_banjo PROPERTIES TIMEOUT 120)
add_executable(mm_mower_loops "${GAME_MODULE_DIR}/tools/mower_loops.cpp")
target_link_libraries(mm_mower_loops PRIVATE mm_core)
add_executable(mm_bee_render "${GAME_MODULE_DIR}/tools/bee_render.cpp")
target_link_libraries(mm_bee_render PRIVATE mm_core)
add_executable(mm_banjo_render "${GAME_MODULE_DIR}/tools/banjo_render.cpp")
target_link_libraries(mm_banjo_render PRIVATE mm_core)
add_executable(mm_mower_voice_render "${GAME_MODULE_DIR}/tools/mower_voice_render.cpp")
target_link_libraries(mm_mower_voice_render PRIVATE mm_core)

get_filename_component(MM_KIT_DIR "${GAME_MODULE_DIR}/../../new-games/kit" ABSOLUTE)
if(EXISTS "${MM_KIT_DIR}/png_writer.hpp")
  add_executable(mm_preview "${GAME_MODULE_DIR}/tools/preview.cpp")
  target_include_directories(mm_preview PRIVATE "${MM_KIT_DIR}")
  target_link_libraries(mm_preview PRIVATE mm_core)
  add_executable(mm_models_preview "${GAME_MODULE_DIR}/tools/models_preview.cpp")
  target_include_directories(mm_models_preview PRIVATE "${MM_KIT_DIR}")
  target_link_libraries(mm_models_preview PRIVATE mm_core)
  add_executable(mm_gnome_film "${GAME_MODULE_DIR}/tools/gnome_film.cpp")
  target_include_directories(mm_gnome_film PRIVATE "${MM_KIT_DIR}")
  target_link_libraries(mm_gnome_film PRIVATE mm_core)
endif()

if(GAMES_BUILD_APPLICATION)
  add_executable(mowingman_view_contract_tests "${GAME_MODULE_DIR}/tests/view_contract_tests.cpp")
  target_include_directories(mowingman_view_contract_tests PRIVATE "${PROJECT_SOURCE_DIR}/new-games/kit")
  target_link_libraries(mowingman_view_contract_tests PRIVATE vendor_game_ui)
  add_test(NAME mowingman_view_contract COMMAND mowingman_view_contract_tests)
  set_tests_properties(mowingman_view_contract PROPERTIES TIMEOUT 90
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
endif()
