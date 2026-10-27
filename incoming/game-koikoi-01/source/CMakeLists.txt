cmake_minimum_required(VERSION 3.25)
project(KoiKoi VERSION 0.1.0 LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(GUIForms 0.1 CONFIG REQUIRED COMPONENTS Application)

# The rules (the module delivered to PlaySuite's card table): portable, no UI.
add_library(koi_rules STATIC src/koikoi.cpp)
target_include_directories(koi_rules PUBLIC src)
target_compile_options(koi_rules PRIVATE -Wall -Wextra -O2)

enable_testing()
add_executable(koikoi_tests tests/koikoi_tests.cpp)
target_link_libraries(koikoi_tests PRIVATE koi_rules)
add_test(NAME koikoi COMMAND koikoi_tests)

# A Mac table for playing it now.
add_library(kk_ui STATIC src/koi_view.cpp src/card_finish.cpp src/paint/carpet.cpp src/paint/image.cpp src/platform/raster.cpp src/platform/text.mm src/platform/audio.mm src/platform/present.mm src/platform/image.mm)
target_include_directories(kk_ui PUBLIC src)
target_link_libraries(kk_ui PUBLIC koi_rules GUIForms::Application
  "-framework AppKit" "-framework AVFoundation" "-framework CoreText" "-framework QuartzCore" "-framework ImageIO" "-framework CoreGraphics")
target_compile_definitions(kk_ui PRIVATE KK_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_options(kk_ui PRIVATE -Wall -Wextra)

add_executable(KoiKoi MACOSX_BUNDLE src/main.cpp)
target_link_libraries(KoiKoi PRIVATE kk_ui)
set_target_properties(KoiKoi PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Koi-Koi"
  MACOSX_BUNDLE_GUI_IDENTIFIER "org.rainstar.koikoi"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
  BUILD_RPATH "@executable_path/../Frameworks")
file(GLOB fonts "${GUIForms_FONT_DIR}/*")
set_source_files_properties(${fonts} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
target_sources(KoiKoi PRIVATE ${fonts})
add_custom_command(TARGET KoiKoi POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:KoiKoi>/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:GUIForms::Application>" "$<TARGET_BUNDLE_DIR:KoiKoi>/Contents/Frameworks/libgui_forms_application.0.dylib"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:KoiKoi>/Contents/Resources/assets")
