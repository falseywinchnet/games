cmake_minimum_required(VERSION 3.25)
project(AtomProbe VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# Pure rules and rendering (no UI framework): testable headless.
add_library(ap_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp
  src/box.cpp src/chamber.cpp src/save.cpp)
target_include_directories(ap_core PUBLIC src)
target_compile_options(ap_core PRIVATE -Wall -Wextra -O2)

enable_testing()
add_executable(box_tests tests/box_tests.cpp)
target_link_libraries(box_tests PRIVATE ap_core)
add_test(NAME box COMMAND box_tests)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE ap_core)

add_library(ap_ui STATIC src/atomprobe_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(ap_ui PUBLIC ap_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(ap_ui PRIVATE AP_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(ap_ui PRIVATE -Wall -Wextra)

add_executable(AtomProbe MACOSX_BUNDLE src/main.cpp)
target_link_libraries(AtomProbe PRIVATE ap_ui)
set_target_properties(AtomProbe PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Atom Probe"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.atomprobe"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(AtomProbe PRIVATE ${fonts})
add_custom_command(TARGET AtomProbe POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:AtomProbe>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:AtomProbe>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:AtomProbe>/Contents/Resources/assets")
