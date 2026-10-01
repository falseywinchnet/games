cmake_minimum_required(VERSION 3.25)
project(GamesPortableAudioTests LANGUAGES CXX)
set(GAMES_AUDIO_SOURCE_DIR "" CACHE PATH "Development GUI.Forms source root; leave empty for installed Audio SDK")
if(GAMES_AUDIO_SOURCE_DIR)
    set(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT ON CACHE BOOL "Development transport" FORCE)
    include("${GAMES_AUDIO_SOURCE_DIR}/cmake/Audio.cmake")
else()
    find_package(GUIForms CONFIG REQUIRED COMPONENTS Audio)
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
    "${games_root}/vendor/eggy/src/audio.cpp"
    "${games_root}/vendor/switchbox/src/platform/audio.cpp"
    "${games_root}/vendor/atomprobe/src/platform/audio.cpp")
target_compile_features(game_audio_adapters PUBLIC cxx_std_20)
target_include_directories(game_audio_adapters PUBLIC "${games_root}/src")
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio)
if(GAMES_AUDIO_SOURCE_DIR)
    target_sources(game_audio_adapters PRIVATE
        "${games_root}/src/fourpegs_audio.cpp"
        "${games_root}/vendor/fourpegs/src/platform/audio.cpp")
    add_executable(fourpegs_audio "${games_root}/tests/fourpegs_audio_tests.cpp")
    target_link_libraries(fourpegs_audio PRIVATE game_audio_adapters)
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
