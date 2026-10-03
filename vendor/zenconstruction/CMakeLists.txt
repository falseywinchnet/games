cmake_minimum_required(VERSION 3.25)
project(ZenConstruction VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# the physics engine (portable, no dependencies; its own acceptance tests)
add_subdirectory(phys)

# rocks, the site and the run: portable, no UI framework
add_library(zc_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp
  src/rocks.cpp src/run.cpp src/site.cpp src/shapes.cpp src/crane_model.cpp src/stones.cpp)
target_include_directories(zc_core PUBLIC src)
target_link_libraries(zc_core PUBLIC zc_phys)
target_compile_options(zc_core PRIVATE -Wall -Wextra -O2 -ffp-contract=off)

add_executable(rockstats tools/rockstats.cpp)
target_link_libraries(rockstats PRIVATE zc_core)

add_executable(preview tools/preview.cpp)
target_link_libraries(preview PRIVATE zc_core)

enable_testing()
add_executable(zen_tests tests/zen_tests.cpp)
target_link_libraries(zen_tests PRIVATE zc_core)
add_test(NAME zen COMMAND zen_tests)

add_executable(bench tools/bench.cpp)
target_link_libraries(bench PRIVATE zc_core)
add_executable(site_preview tools/site_preview.cpp)
target_link_libraries(site_preview PRIVATE zc_core)

# the Mac app
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)
add_library(zc_ui STATIC src/zen_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(zc_ui PUBLIC zc_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(zc_ui PRIVATE ZC_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(zc_ui PRIVATE -Wall -Wextra)

add_executable(ZenConstruction MACOSX_BUNDLE src/main.cpp)
target_link_libraries(ZenConstruction PRIVATE zc_ui)
set_target_properties(ZenConstruction PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Zen Construction"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.zenconstruction"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(ZenConstruction PRIVATE ${fonts})
add_custom_command(TARGET ZenConstruction POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:ZenConstruction>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:ZenConstruction>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:ZenConstruction>/Contents/Resources/assets")
