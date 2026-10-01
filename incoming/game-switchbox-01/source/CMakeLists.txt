cmake_minimum_required(VERSION 3.25)
project(Switchbox VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# Pure rules, character, and rendering (no UI framework): testable headless.
add_library(sbx_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp src/platform/lines.cpp
  src/puzzle.cpp src/face.cpp src/girl.cpp src/stage.cpp src/actor.cpp src/save.cpp src/mole.cpp)
target_include_directories(sbx_core PUBLIC src)
target_compile_options(sbx_core PRIVATE -Wall -Wextra -O2)

add_library(sbx_ui STATIC src/switchbox_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(sbx_ui PUBLIC sbx_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(sbx_ui PRIVATE SBX_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(sbx_ui PRIVATE -Wall -Wextra)

add_executable(Switchbox MACOSX_BUNDLE src/main.cpp)
target_link_libraries(Switchbox PRIVATE sbx_ui)
set_target_properties(Switchbox PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Switchbox"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.switchbox"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(Switchbox PRIVATE ${fonts})
add_custom_command(TARGET Switchbox POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Switchbox>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:Switchbox>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:Switchbox>/Contents/Resources/assets")

enable_testing()
add_executable(puzzle_tests tests/puzzle_tests.cpp)
target_link_libraries(puzzle_tests PRIVATE sbx_core)
add_test(NAME puzzle COMMAND puzzle_tests)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE sbx_core)
