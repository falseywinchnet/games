cmake_minimum_required(VERSION 3.25)
project(Eggy VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# Pure game logic and rendering (no UI framework): testable headless.
add_library(eggy_core STATIC
  src/world.cpp src/sim.cpp src/save.cpp src/lines.cpp
  src/raster.cpp src/r3d.cpp src/mesh.cpp src/textures.cpp src/ground.cpp src/flora.cpp src/critters.cpp src/duck3d.cpp src/scene.cpp)
target_include_directories(eggy_core PUBLIC src)
target_compile_options(eggy_core PRIVATE -Wall -Wextra -O2)

add_library(eggy_ui STATIC src/eggy_view.cpp src/text.mm src/audio.mm src/present.mm)
target_link_libraries(eggy_ui PUBLIC eggy_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(eggy_ui PRIVATE EGGY_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(eggy_ui PRIVATE -Wall -Wextra)

add_executable(Eggy MACOSX_BUNDLE src/main.cpp)
target_link_libraries(Eggy PRIVATE eggy_ui)
set_target_properties(Eggy PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Eggy"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.eggy"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(Eggy PRIVATE ${fonts})
add_custom_command(TARGET Eggy POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:Eggy>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:Eggy>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:Eggy>/Contents/Resources/assets")

enable_testing()
add_executable(sim_tests tests/sim_tests.cpp)
target_link_libraries(sim_tests PRIVATE eggy_core)
add_test(NAME sim COMMAND sim_tests)
add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE eggy_core)
