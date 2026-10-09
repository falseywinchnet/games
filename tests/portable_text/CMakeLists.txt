cmake_minimum_required(VERSION 3.25)
project(GamesTextRequestTests LANGUAGES CXX)
# Windows finds a program's DLLs beside it: build every program and DLL, GUI.Forms'
# Application and Threading included, into one folder.
if(WIN32)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
set(GAMES_TEXT_SOURCE_DIR "" CACHE PATH "GUI.Forms development source with TextMaskService")
if(NOT EXISTS "${GAMES_TEXT_SOURCE_DIR}/include/gui_forms/text_mask.hpp")
    message(FATAL_ERROR "Set GAMES_TEXT_SOURCE_DIR to the reviewed GUI.Forms text-mask source")
endif()
set(GUI_FORMS_ENABLE_SKIA OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_ENABLE_MACOS_HOST OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_ENABLE_WINDOWS_HOST OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_ENABLE_LINUX_HOST OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_ENABLE_HARFBUZZ_TEXT ON CACHE BOOL "" FORCE)
set(GUI_FORMS_BUILD_PREPARED_TEXT OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_BUILD_TEXT_MASKS ON CACHE BOOL "" FORCE)
set(GUI_FORMS_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
set(GUI_FORMS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${GAMES_TEXT_SOURCE_DIR}" toolkit EXCLUDE_FROM_ALL)
get_filename_component(games_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${games_root}/tools/game_catalog.py"
    --root "${games_root}" --generate "${CMAKE_CURRENT_BINARY_DIR}/catalog"
    RESULT_VARIABLE catalog_result ERROR_VARIABLE catalog_error OUTPUT_QUIET)
if(NOT catalog_result EQUAL 0)
    message(FATAL_ERROR "Game catalog: ${catalog_error}")
endif()
include_directories("${CMAKE_CURRENT_BINARY_DIR}/catalog")

add_library(game_text_requests STATIC "${games_root}/src/text_requests.cpp")
target_include_directories(game_text_requests PUBLIC "${games_root}/src")
target_compile_features(game_text_requests PUBLIC cxx_std_20)
target_link_libraries(game_text_requests PUBLIC gui_forms_text_masks)
add_executable(text_request_tests "${games_root}/tests/text_request_tests.cpp")
target_link_libraries(text_request_tests PRIVATE game_text_requests)
enable_testing()
add_test(NAME text_request_lifecycle COMMAND text_request_tests "${GAMES_TEXT_SOURCE_DIR}/assets/fonts/Carlito-Regular.ttf")
set_tests_properties(text_request_lifecycle PROPERTIES TIMEOUT 30)

add_executable(text_native_consumer_tests "${games_root}/tests/text_native_consumer_tests.cpp")
target_link_libraries(text_native_consumer_tests PRIVATE game_text_requests)
add_test(NAME text_native_consumer COMMAND text_native_consumer_tests "${GAMES_TEXT_SOURCE_DIR}/assets/fonts")
set_tests_properties(text_native_consumer PROPERTIES TIMEOUT 30)

add_library(game_text_frames STATIC "${games_root}/src/game_text.cpp")
target_link_libraries(game_text_frames PUBLIC game_text_requests)
add_executable(game_text_frame_tests "${games_root}/tests/game_text_frame_tests.cpp")
target_link_libraries(game_text_frame_tests PRIVATE game_text_frames)
add_test(NAME game_text_frame_preparation COMMAND game_text_frame_tests "${GAMES_TEXT_SOURCE_DIR}/assets/fonts")
set_tests_properties(game_text_frame_preparation PROPERTIES TIMEOUT 30)
add_library(vendor_text_views OBJECT
    "${games_root}/games/eggy/src/eggy_view.cpp"
    "${games_root}/games/fourpegs/src/fourpegs_view.cpp"
    "${games_root}/games/atomprobe/src/atomprobe_view.cpp"
    "${games_root}/games/switchbox/src/switchbox_view.cpp")
target_link_libraries(vendor_text_views PRIVATE game_text_frames GUIForms::Controls)
# The views draw through the shared renderer's canvas, 3D renderer and meshes.
target_include_directories(vendor_text_views PRIVATE "${games_root}/shared/render/src")

option(GAMES_TEST_VENDOR_FRAMES "Render complete vendor game frames with real portable services" OFF)
if(GAMES_TEST_VENDOR_FRAMES)
    set(GAMES_BUILD_APPLICATION OFF CACHE BOOL "" FORCE)
    add_subdirectory("${games_root}" game_cores EXCLUDE_FROM_ALL)
    set(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT ON CACHE BOOL "" FORCE)
    include("${GAMES_TEXT_SOURCE_DIR}/cmake/Audio.cmake")
    add_library(fourpegs_frame_audio STATIC
        "${games_root}/src/audio_loader.cpp"
        "${games_root}/src/pcm_player.cpp"
        "${games_root}/src/fourpegs_audio.cpp"
        "${games_root}/src/scene_audio.cpp"
        "${games_root}/games/eggy/src/audio.cpp"
        "${games_root}/games/atomprobe/src/platform/audio.cpp"
        "${games_root}/games/switchbox/src/platform/audio.cpp"
        "${games_root}/games/fourpegs/src/platform/audio.cpp")
    target_include_directories(fourpegs_frame_audio PUBLIC "${games_root}/src")
    target_link_libraries(fourpegs_frame_audio PUBLIC GUIForms::Audio game_paths)
    add_executable(vendor_game_frame_tests "${games_root}/tests/vendor_game_frame_tests.cpp")
    target_compile_definitions(vendor_game_frame_tests PRIVATE GAMES_FRAME_fourpegs GAMES_FRAME_atomprobe GAMES_FRAME_switchbox GAMES_FRAME_eggy)
    target_link_libraries(vendor_game_frame_tests PRIVATE vendor_text_views
        game_text_frames fp_core ap_core sbx_core eggy_core fourpegs_frame_audio GUIForms::Controls)
    foreach(game IN ITEMS fourpegs atomprobe switchbox eggy)
        add_test(NAME ${game}_native_frames COMMAND vendor_game_frame_tests "${games_root}/assets" ${game})
        set_tests_properties(${game}_native_frames PROPERTIES TIMEOUT 45)
    endforeach()
endif()
