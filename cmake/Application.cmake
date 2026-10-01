add_library(eggy_ui STATIC vendor/eggy/src/eggy_view.cpp vendor/eggy/src/text.mm vendor/eggy/src/audio.mm)
target_link_libraries(eggy_ui PUBLIC eggy_core GUIForms::Application "-framework AppKit" "-framework AVFoundation" "-framework CoreText")
target_compile_definitions(eggy_ui PRIVATE EGGY_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
add_library(game_ui src/table.cpp src/presentation.cpp src/puzzle_view.cpp src/puzzle_render.cpp src/puzzle_image.cpp src/collection.cpp src/sudoku_view.cpp src/audio.mm vendor/paint/carpet.cpp vendor/paint/image.cpp)
add_executable(games MACOSX_BUNDLE src/main.cpp)
target_include_directories(game_ui PUBLIC src PRIVATE vendor/paint)
target_link_libraries(game_ui PUBLIC game_rules sudoku_generator eggy_ui GUIForms::Application "-framework AppKit" "-framework AVFoundation")
target_link_libraries(games PRIVATE game_ui)
target_compile_options(game_ui PRIVATE -Wall -Wextra)
target_compile_definitions(game_ui PRIVATE GAMES_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
set_target_properties(games PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "Games" MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.games" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(games PRIVATE ${fonts})
if(APPLE)
set_target_properties(games PROPERTIES BUILD_RPATH "@executable_path/../Frameworks")
add_custom_command(TARGET games POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:games>/Contents/Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:games>/Contents/Frameworks/libgui_forms_application.0.dylib"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:games>/Contents/Resources/assets")

endif()
add_executable(ui_tests tests/ui_tests.cpp)
target_link_libraries(ui_tests PRIVATE game_ui)
add_test(NAME ui_routing COMMAND ui_tests)
if(APPLE)
set_source_files_properties(assets/Games.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
target_sources(games PRIVATE assets/Games.icns)
set_target_properties(games PROPERTIES MACOSX_BUNDLE_ICON_FILE "Games.icns")
add_custom_command(TARGET games POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:games>/Contents/Resources/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/vendor/paint/LICENSE" "$<TARGET_BUNDLE_DIR:games>/Contents/Resources/licenses/Plan-Paint-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${GUIForms_FONT_DIR}/../../licenses/GUIForms" "$<TARGET_BUNDLE_DIR:games>/Contents/Resources/licenses/GUIForms")
endif()
add_executable(audio_tests tests/audio_tests.mm)
target_link_libraries(audio_tests PRIVATE game_ui)
add_test(NAME audio COMMAND audio_tests)
add_executable(collection_ui_tests tests/collection_ui_tests.cpp)
target_link_libraries(collection_ui_tests PRIVATE game_ui)
add_test(NAME collection_ui COMMAND collection_ui_tests)
add_executable(raster_bench tools/raster_bench.cpp)
target_link_libraries(raster_bench PRIVATE game_ui)

