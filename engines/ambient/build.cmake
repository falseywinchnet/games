# The ambient scene engine. Included once by the generated registry (and by a game's
# own standalone build) with ENGINE_DIR set to this folder.
if(TARGET ambient_core)
  return()
endif()
add_library(ambient_core STATIC
  "${ENGINE_DIR}/src/archive.cpp"
  "${ENGINE_DIR}/src/cadence.cpp"
  "${ENGINE_DIR}/src/inflate.cpp"
  "${ENGINE_DIR}/src/motion.cpp"
  "${ENGINE_DIR}/src/present.cpp"
  "${ENGINE_DIR}/src/settings.cpp"
  "${ENGINE_DIR}/src/stage.cpp")
target_include_directories(ambient_core PUBLIC "${ENGINE_DIR}/src")
target_compile_features(ambient_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(ambient_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(ambient_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The engine's view is compiled into the application's module library when a game
# uses the engine; this target carries its headers to that game's code.
add_library(ambient_ui INTERFACE)
target_include_directories(ambient_ui INTERFACE "${ENGINE_DIR}/ui")
target_link_libraries(ambient_ui INTERFACE ambient_core)

include(CTest)
add_executable(ambient_tests "${ENGINE_DIR}/tests/ambient_tests.cpp")
target_link_libraries(ambient_tests PRIVATE ambient_core)
add_test(NAME ambient_engine COMMAND ambient_tests)
set_tests_properties(ambient_engine PROPERTIES TIMEOUT 60)
