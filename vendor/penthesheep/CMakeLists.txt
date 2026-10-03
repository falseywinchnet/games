cmake_minimum_required(VERSION 3.25)
project(PenTheSheep VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

add_library(sh_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp
  src/field.cpp src/pasture.cpp)
target_include_directories(sh_core PUBLIC src)
target_compile_options(sh_core PRIVATE -Wall -Wextra -O2)

add_executable(levelstats tools/levelstats.cpp)
target_link_libraries(levelstats PRIVATE sh_core)

enable_testing()
add_executable(sheep_tests tests/sheep_tests.cpp)
target_link_libraries(sheep_tests PRIVATE sh_core)
add_test(NAME sheep COMMAND sheep_tests)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE sh_core)

add_library(sh_ui STATIC src/sheep_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(sh_ui PUBLIC sh_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(sh_ui PRIVATE SH_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(sh_ui PRIVATE -Wall -Wextra)

add_executable(PenTheSheep MACOSX_BUNDLE src/main.cpp)
target_link_libraries(PenTheSheep PRIVATE sh_ui)
set_target_properties(PenTheSheep PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Pen the Sheep"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.penthesheep"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(PenTheSheep PRIVATE ${fonts})
add_custom_command(TARGET PenTheSheep POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PenTheSheep>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:PenTheSheep>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:PenTheSheep>/Contents/Resources/assets")
