cmake_minimum_required(VERSION 3.25)
project(LiarsDice VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# Rules, minds, wagers and rendering (no UI framework): testable headless.
add_library(ld_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp
  src/rules.cpp src/brain.cpp src/wager.cpp src/crew.cpp src/cabin.cpp src/save.cpp)
target_include_directories(ld_core PUBLIC src)
target_compile_options(ld_core PRIVATE -Wall -Wextra -O2)

enable_testing()
add_executable(dice_tests tests/dice_tests.cpp)
target_link_libraries(dice_tests PRIVATE ld_core)
add_test(NAME dice COMMAND dice_tests)
add_executable(ladder tools/ladder.cpp)
target_link_libraries(ladder PRIVATE ld_core)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE ld_core)

add_library(ld_ui STATIC src/dice_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(ld_ui PUBLIC ld_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(ld_ui PRIVATE LD_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(ld_ui PRIVATE -Wall -Wextra)

add_executable(LiarsDice MACOSX_BUNDLE src/main.cpp)
target_link_libraries(LiarsDice PRIVATE ld_ui)
set_target_properties(LiarsDice PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Liar's Dice"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.liarsdice"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(LiarsDice PRIVATE ${fonts})
add_custom_command(TARGET LiarsDice POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:LiarsDice>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:LiarsDice>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:LiarsDice>/Contents/Resources/assets")
