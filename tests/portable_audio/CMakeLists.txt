cmake_minimum_required(VERSION 3.25)
project(GamesPortableAudioTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GAMES_AUDIO_SOURCE_DIR "" CACHE PATH "Development GUI.Forms source root; leave empty for installed Audio SDK")
if(GAMES_AUDIO_SOURCE_DIR)
    set(GUI_FORMS_BUILD_AUDIO_LOOP_TRANSPORT ON CACHE BOOL "Development transport" FORCE)
    set(GUI_FORMS_AUDIO_VORBIS_BACKEND stx CACHE STRING "Vorbis decoder: stx (GUI.Forms) or stb")
    # Audio.cmake links GUIForms::Threading, which GUI.Forms' full build defines.
    # This audio-only build defines it from the same two sources.
    find_package(Threads REQUIRED)
    add_library(gui_forms_threading SHARED
        "${GAMES_AUDIO_SOURCE_DIR}/src/core/threading/atomic_pool/atomic_pool.c"
        "${GAMES_AUDIO_SOURCE_DIR}/src/core/threading/worker.cpp")
    add_library(GUIForms::Threading ALIAS gui_forms_threading)
    target_include_directories(gui_forms_threading PUBLIC "${GAMES_AUDIO_SOURCE_DIR}/include")
    target_compile_features(gui_forms_threading PUBLIC cxx_std_20)
    target_link_libraries(gui_forms_threading PRIVATE Threads::Threads)
    if(WIN32)
        target_compile_definitions(gui_forms_threading PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
        target_link_libraries(gui_forms_threading PRIVATE winpthread)
        target_link_options(gui_forms_threading PRIVATE "-Wl,--export-all-symbols")
    endif()
    # Audio.cmake exports gui_forms_audio, which needs its Threading dependency in
    # an export set too; this project is never installed.
    include(GNUInstallDirs)
    install(TARGETS gui_forms_threading EXPORT GUIFormsAudioTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
    "${games_root}/games/eggy/src/audio.cpp"
    "${games_root}/games/switchbox/src/platform/audio.cpp"
    "${games_root}/games/atomprobe/src/platform/audio.cpp")
target_compile_features(game_audio_adapters PUBLIC cxx_std_20)
target_include_directories(game_audio_adapters PUBLIC "${games_root}/src")
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio)
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
