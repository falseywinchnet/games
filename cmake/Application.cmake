set(GAMES_RUNTIME_ASSET_DIR "" CACHE PATH "Complete verified portable runtime assets")
if(NOT EXISTS "${GAMES_RUNTIME_ASSET_DIR}/audio/fp_music_t1.ogg")
    message(FATAL_ERROR "Prepare portable assets and set GAMES_RUNTIME_ASSET_DIR to their directory")
endif()
add_library(game_audio_adapters STATIC
    src/audio_loader.cpp src/pcm_player.cpp src/scene_audio.cpp src/fourpegs_audio.cpp src/audio.cpp
    vendor/eggy/src/audio.cpp vendor/switchbox/src/platform/audio.cpp
    vendor/fourpegs/src/platform/audio.cpp vendor/atomprobe/src/platform/audio.cpp)
target_include_directories(game_audio_adapters PUBLIC src)
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio game_paths)
add_library(game_text_frames STATIC src/text_requests.cpp src/game_text.cpp)
target_include_directories(game_text_frames PUBLIC src)
target_link_libraries(game_text_frames PUBLIC gui_forms_text_masks)
add_library(vendor_game_ui STATIC
    vendor/eggy/src/eggy_view.cpp vendor/switchbox/src/switchbox_view.cpp
    vendor/fourpegs/src/fourpegs_view.cpp vendor/atomprobe/src/atomprobe_view.cpp)
target_link_libraries(vendor_game_ui PUBLIC eggy_core sbx_core fp_core ap_core
    game_audio_adapters game_text_frames GUIForms::Application)
add_library(game_ui src/table.cpp src/presentation.cpp src/puzzle_view.cpp src/collection.cpp
    src/sudoku_view.cpp vendor/paint/carpet.cpp vendor/paint/image.cpp)
target_include_directories(game_ui PUBLIC src PRIVATE vendor/paint)
target_link_libraries(game_ui PUBLIC game_rules game_raster sudoku_generator vendor_game_ui)
if(NOT MSVC)
    target_compile_options(game_ui PRIVATE -Wall -Wextra)
endif()
add_executable(games MACOSX_BUNDLE src/main.cpp)
if(WIN32)
    set_target_properties(games PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_link_libraries(games PRIVATE game_ui)
set_target_properties(games PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Games"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.games"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE)
    set(games_resources "$<TARGET_BUNDLE_DIR:games>/Contents/Resources")
    set(games_libraries "$<TARGET_BUNDLE_DIR:games>/Contents/Frameworks")
    set_target_properties(games PROPERTIES BUILD_RPATH "@executable_path/../Frameworks")
    set_source_files_properties(assets/Games.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(games PRIVATE assets/Games.icns)
    set_target_properties(games PROPERTIES MACOSX_BUNDLE_ICON_FILE "Games.icns")
else()
    set(games_resources "$<TARGET_FILE_DIR:games>")
    set(games_libraries "$<TARGET_FILE_DIR:games>")
    if(UNIX)
        set_target_properties(games PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endif()
add_custom_command(TARGET games POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${games_libraries}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GUIForms::Application>" "${games_libraries}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GAMES_RUNTIME_ASSET_DIR}" "${games_resources}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}" "${games_resources}/fonts"
    VERBATIM)
if(UNIX)
    add_custom_command(TARGET games POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GUIForms::Application>"
            "${games_libraries}/$<TARGET_SONAME_FILE_NAME:GUIForms::Application>"
        VERBATIM)
endif()
add_executable(ui_tests tests/ui_tests.cpp)
target_link_libraries(ui_tests PRIVATE game_ui)
add_test(NAME ui_routing COMMAND ui_tests)
add_executable(collection_ui_tests tests/collection_ui_tests.cpp)
target_link_libraries(collection_ui_tests PRIVATE game_ui)
add_test(NAME collection_ui COMMAND collection_ui_tests)
set_tests_properties(ui_routing collection_ui PROPERTIES
    ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
add_executable(audio_tests tests/audio_tests.cpp)
target_link_libraries(audio_tests PRIVATE GUIForms::Audio)
add_test(NAME audio COMMAND audio_tests "${GAMES_RUNTIME_ASSET_DIR}" ogg)
add_executable(audio_policy_tests tests/audio_policy_tests.cpp)
target_link_libraries(audio_policy_tests PRIVATE game_audio_adapters)
add_test(NAME audio_policy COMMAND audio_policy_tests)
add_executable(fourpegs_audio_tests tests/fourpegs_audio_tests.cpp)
target_link_libraries(fourpegs_audio_tests PRIVATE game_audio_adapters)
add_test(NAME fourpegs_bar_audio COMMAND fourpegs_audio_tests "${GAMES_RUNTIME_ASSET_DIR}")
add_executable(vendor_game_frame_tests tests/vendor_game_frame_tests.cpp)
target_link_libraries(vendor_game_frame_tests PRIVATE vendor_game_ui)
foreach(game IN ITEMS fourpegs atomprobe switchbox eggy)
    add_test(NAME ${game}_native_frames COMMAND vendor_game_frame_tests "${GAMES_RUNTIME_ASSET_DIR}" ${game})
    set_tests_properties(${game}_native_frames PROPERTIES TIMEOUT 45)
endforeach()
add_executable(raster_bench tools/raster_bench.cpp)
target_link_libraries(raster_bench PRIVATE game_ui)
