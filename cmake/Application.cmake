set(GAMES_RUNTIME_ASSET_DIR "" CACHE PATH "Complete verified portable runtime assets")
if(NOT EXISTS "${GAMES_RUNTIME_ASSET_DIR}/audio/fp_music_t1.ogg")
    message(FATAL_ERROR "Prepare portable assets and set GAMES_RUNTIME_ASSET_DIR to their directory")
endif()
add_library(game_audio_adapters STATIC
    vendor/zenconstruction/src/platform/audio.cpp
    src/audio_loader.cpp src/pcm_player.cpp src/scene_audio.cpp src/fourpegs_audio.cpp src/audio.cpp
    vendor/eggy/src/audio.cpp vendor/switchbox/src/platform/audio.cpp
    vendor/fourpegs/src/platform/audio.cpp vendor/atomprobe/src/platform/audio.cpp
    vendor/parrots/src/platform/audio.cpp vendor/liarsdice/src/platform/audio.cpp vendor/penthesheep/src/platform/audio.cpp vendor/koikoi/src/platform/audio.cpp)
target_include_directories(game_audio_adapters PUBLIC src)
target_link_libraries(game_audio_adapters PUBLIC GUIForms::Audio game_paths)
add_library(game_text_frames STATIC src/text_requests.cpp src/game_text.cpp)
target_include_directories(game_text_frames PUBLIC src)
target_link_libraries(game_text_frames PUBLIC gui_forms_text_masks)
add_library(game_felt STATIC vendor/paint/carpet.cpp vendor/paint/image.cpp)
add_library(vendor_game_ui STATIC
    vendor/zenconstruction/src/zen_view.cpp vendor/zenconstruction/src/platform/text.cpp
    vendor/zenconstruction/src/platform/present.cpp
    vendor/eggy/src/eggy_view.cpp vendor/switchbox/src/switchbox_view.cpp
    vendor/fourpegs/src/fourpegs_view.cpp vendor/atomprobe/src/atomprobe_view.cpp
    vendor/parrots/src/table_view.cpp vendor/parrots/src/platform/text.cpp vendor/parrots/src/platform/present.cpp vendor/liarsdice/src/dice_view.cpp vendor/liarsdice/src/platform/text.cpp vendor/liarsdice/src/platform/present.cpp vendor/penthesheep/src/sheep_view.cpp vendor/penthesheep/src/platform/text.cpp vendor/penthesheep/src/platform/present.cpp vendor/koikoi/src/koi_view.cpp vendor/koikoi/src/platform/text.cpp vendor/koikoi/src/platform/present.cpp
    vendor/koikoi/src/card_finish.cpp vendor/koikoi/src/platform/image.cpp)
target_link_libraries(vendor_game_ui PUBLIC eggy_core sbx_core fp_core ap_core zc_core
    game_audio_adapters game_text_frames game_felt pt_core ld_core sh_core kk_core GUIForms::Application)
target_compile_definitions(vendor_game_ui PRIVATE _USE_MATH_DEFINES)
add_library(game_ui src/table.cpp src/presentation.cpp src/puzzle_view.cpp src/collection.cpp
    src/sudoku_view.cpp src/suite.cpp src/text_sprites.cpp src/shelf.cpp src/capsule.cpp src/kitten.cpp
    src/help_book.cpp src/help_content.cpp)
target_include_directories(game_ui PUBLIC src PRIVATE vendor/paint)
target_link_libraries(game_ui PUBLIC game_rules game_solver game_raster sudoku_generator vendor_game_ui)
if(NOT MSVC)
    target_compile_options(game_ui PRIVATE -Wall -Wextra)
endif()
add_executable(games MACOSX_BUNDLE src/main.cpp)
if(WIN32)
    set_target_properties(games PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(games PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
target_link_libraries(games PRIVATE game_ui)
set_target_properties(games PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "PlaySuite"
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
add_executable(puzzle_frame_tests tests/puzzle_frame_tests.cpp)
add_executable(puzzle_native_framebuffer_tests MACOSX_BUNDLE tests/puzzle_native_framebuffer_tests.cpp)
target_link_libraries(puzzle_native_framebuffer_tests PRIVATE game_ui)
if(APPLE)
    set(framebuffer_test_resources "$<TARGET_BUNDLE_DIR:puzzle_native_framebuffer_tests>/Contents/Resources")
else()
    set(framebuffer_test_resources "$<TARGET_FILE_DIR:puzzle_native_framebuffer_tests>")
endif()
add_custom_command(TARGET puzzle_native_framebuffer_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}" "${framebuffer_test_resources}/fonts")
if(UNIX AND NOT APPLE)
    find_program(GAMES_XVFB_RUN xvfb-run)
endif()
if(GAMES_XVFB_RUN)
    add_test(NAME puzzle_native_framebuffers COMMAND ${GAMES_XVFB_RUN} -a $<TARGET_FILE:puzzle_native_framebuffer_tests>)
else()
    add_test(NAME puzzle_native_framebuffers COMMAND $<TARGET_FILE:puzzle_native_framebuffer_tests>)
endif()
set_tests_properties(puzzle_native_framebuffers PROPERTIES TIMEOUT 120 ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
target_link_libraries(puzzle_frame_tests PRIVATE game_ui)
add_test(NAME puzzle_frames COMMAND puzzle_frame_tests)
set_tests_properties(puzzle_frames PROPERTIES ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
add_executable(fourpegs_pointer_tests tests/fourpegs_pointer_tests.cpp)
target_link_libraries(fourpegs_pointer_tests PRIVATE vendor_game_ui)
add_test(NAME fourpegs_pointer COMMAND fourpegs_pointer_tests)
set_tests_properties(fourpegs_pointer PROPERTIES ENVIRONMENT "GAMES_ASSET_DIR=${GAMES_RUNTIME_ASSET_DIR}")
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
target_compile_definitions(vendor_game_frame_tests PRIVATE GAMES_EXTENDED_VENDOR_FRAMES)
target_link_libraries(vendor_game_frame_tests PRIVATE vendor_game_ui)
foreach(game IN ITEMS fourpegs atomprobe switchbox eggy parrots liarsdice penthesheep koikoi)
    add_test(NAME ${game}_native_frames COMMAND vendor_game_frame_tests "${GAMES_RUNTIME_ASSET_DIR}" ${game})
    set_tests_properties(${game}_native_frames PROPERTIES TIMEOUT 45)
endforeach()
add_executable(raster_bench tools/raster_bench.cpp)
target_link_libraries(raster_bench PRIVATE game_ui)
