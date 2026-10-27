cmake_minimum_required(VERSION 3.25)
project(FourPegs VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# Pure rules, villain, and rendering (no UI framework): testable headless.
add_library(fp_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp src/platform/lines.cpp
  src/board.cpp src/vface.cpp src/villain.cpp src/lair.cpp src/script.cpp src/vactor.cpp src/save.cpp)
target_include_directories(fp_core PUBLIC src)
target_compile_options(fp_core PRIVATE -Wall -Wextra -O2)

add_library(fp_ui STATIC src/fourpegs_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(fp_ui PUBLIC fp_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(fp_ui PRIVATE FP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(fp_ui PRIVATE -Wall -Wextra)

add_executable(FourPegs MACOSX_BUNDLE src/main.cpp)
target_link_libraries(FourPegs PRIVATE fp_ui)
set_target_properties(FourPegs PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Four Pegs"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.fourpegs"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(FourPegs PRIVATE ${fonts})
add_custom_command(TARGET FourPegs POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:FourPegs>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:FourPegs>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:FourPegs>/Contents/Resources/assets")

enable_testing()
add_executable(board_tests tests/board_tests.cpp)
target_link_libraries(board_tests PRIVATE fp_core)
add_test(NAME board COMMAND board_tests)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE fp_core)
