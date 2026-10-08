cmake_minimum_required(VERSION 3.25)
project(GamesPortableAudioTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <stop_token>
int main() {
    std::stop_source source;
    const bool requested = source.request_stop();
    return requested && source.get_token().stop_requested() ? 0 : 1;
}" GAMES_HAS_CXX20_CANCELLATION)
if(NOT GAMES_HAS_CXX20_CANCELLATION)
    message(FATAL_ERROR "Games audio requires a C++20 library with std::stop_token. On macOS use GUI.Forms' LLVM 22 contract (gui_forms/cmake/llvm22.cmake and its macOS 14 runtime); see docs/BUILDING.md.")
endif()
set(GAMES_AUDIO_SOURCE_DIR "" CACHE PATH "Development GUI.Forms source root; leave empty for installed Audio SDK")
if(GAMES_AUDIO_SOURCE_DIR)
    set(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT ON CACHE BOOL "Development transport" FORCE)
    include("${GAMES_AUDIO_SOURCE_DIR}/cmake/Audio.cmake")
    # The audio loader runs on GUI.Forms' Worker. Audio.cmake builds only audio, so
    # compile GUI.Forms' two self-contained threading sources here rather than
    # its whole core.
    find_package(Threads REQUIRED)
    add_library(games_gui_threading STATIC
        "${GAMES_AUDIO_SOURCE_DIR}/src/core/threading/worker.cpp"
        "${GAMES_AUDIO_SOURCE_DIR}/src/core/threading/atomic_pool/atomic_pool.c")
    target_compile_features(games_gui_threading PUBLIC cxx_std_20)
    target_include_directories(games_gui_threading PUBLIC "${GAMES_AUDIO_SOURCE_DIR}/include"
        PRIVATE "${GAMES_AUDIO_SOURCE_DIR}/src/core/threading")
    target_link_libraries(games_gui_threading PUBLIC Threads::Threads)
    set(games_gui_threading_target games_gui_threading)
else()
    find_package(GUIForms CONFIG REQUIRED COMPONENTS Audio Core)
    set(games_gui_threading_target GUIForms::Core)
endif()
get_filename_component(games_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GAMES_AUDIO_ASSET_DIR "" CACHE PATH "Prepared Games runtime assets containing audio/")
set(GAMES_AUDIO_FORMAT "ogg" CACHE STRING "Prepared audio format: ogg or wav")
if(NOT IS_DIRECTORY "${GAMES_AUDIO_ASSET_DIR}/audio")
    message(FATAL_ERROR "Set GAMES_AUDIO_ASSET_DIR to a complete prepared runtime asset directory")
endif()
if(NOT GAMES_AUDIO_FORMAT STREQUAL "ogg" AND NOT GAMES_AUDIO_FORMAT STREQUAL "wav")
    message(FATAL_ERROR "GAMES_AUDIO_FORMAT must be ogg or wav")
endif()
add_library(game_audio_adapters STATIC
    "${games_root}/src/audio_loader.cpp"
    "${games_root}/src/pcm_player.cpp"
    "${games_root}/src/scene_audio.cpp"
    "${games_root}/src/audio.cpp"
    "${games_root}/src/runtime_paths.cpp"
    "${games_root}/games/eggy/src/audio.cpp"
    "${games_root}/games/switchbox/src/platform/audio.cpp"
    "${games_root}/games/atomprobe/src/platform/audio.cpp")
target_compile_features(game_audio_adapters PUBLIC cxx_std_20)
target_include_directories(game_audio_adapters PUBLIC "${games_root}/src")
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio ${games_gui_threading_target})
if(GAMES_AUDIO_SOURCE_DIR)
    target_sources(game_audio_adapters PRIVATE
        "${games_root}/src/fourpegs_audio.cpp"
        "${games_root}/games/fourpegs/src/platform/audio.cpp")
    add_executable(fourpegs_audio "${games_root}/tests/fourpegs_audio_tests.cpp")
    target_link_libraries(fourpegs_audio PRIVATE game_audio_adapters)
    add_executable(audio_device_smoke "${games_root}/tools/audio_device_smoke.cpp")
    target_link_libraries(audio_device_smoke PRIVATE game_audio_adapters)
endif()
add_executable(audio_assets "${games_root}/tests/audio_tests.cpp")
target_link_libraries(audio_assets PRIVATE GUIForms::Audio)
add_executable(audio_policy "${games_root}/tests/audio_policy_tests.cpp")
target_link_libraries(audio_policy PRIVATE game_audio_adapters)
enable_testing()
add_test(NAME prepared_audio_assets COMMAND audio_assets "${GAMES_AUDIO_ASSET_DIR}" "${GAMES_AUDIO_FORMAT}")
add_test(NAME game_audio_policy COMMAND audio_policy)
if(TARGET fourpegs_audio)
    add_test(NAME fourpegs_bar_audio COMMAND fourpegs_audio "${GAMES_AUDIO_ASSET_DIR}")
    set_tests_properties(fourpegs_bar_audio PROPERTIES TIMEOUT 30)
endif()
