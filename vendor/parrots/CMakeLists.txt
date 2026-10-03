cmake_minimum_required(VERSION 3.25)
project(ParrotsTable VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

add_library(pt_core STATIC
  src/platform/raster.cpp src/platform/r3d.cpp src/platform/mesh.cpp
  src/logic.cpp src/script.cpp src/birds.cpp src/parlor.cpp)
target_include_directories(pt_core PUBLIC src)
target_compile_options(pt_core PRIVATE -Wall -Wextra -O2)

add_executable(preview tests/preview.cpp)
target_link_libraries(preview PRIVATE pt_core)

enable_testing()
add_executable(logic_tests tests/logic_tests.cpp)
target_link_libraries(logic_tests PRIVATE pt_core)
add_test(NAME logic COMMAND logic_tests)

add_library(pt_ui STATIC src/table_view.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm)
target_link_libraries(pt_ui PUBLIC pt_core GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore")
target_compile_definitions(pt_ui PRIVATE PT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(pt_ui PRIVATE -Wall -Wextra)

add_executable(ParrotsTable MACOSX_BUNDLE src/main.cpp)
target_link_libraries(ParrotsTable PRIVATE pt_ui)
set_target_properties(ParrotsTable PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "The Parrot's Table"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.parrotstable"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(ParrotsTable PRIVATE ${fonts})
add_custom_command(TARGET ParrotsTable POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:ParrotsTable>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:ParrotsTable>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:ParrotsTable>/Contents/Resources/assets")
