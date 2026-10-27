# The renderer: r2d, r3d and (later) the mesh library. Included once by the generated
# registry (and by a game's own standalone build) with ENGINE_DIR set to this folder.
if(TARGET render_core)
  return()
endif()
add_library(render_core STATIC "${ENGINE_DIR}/src/r2d.cpp" "${ENGINE_DIR}/src/r2d_canvas.cpp"
  "${ENGINE_DIR}/src/r3d.cpp" "${ENGINE_DIR}/src/r3d_renderer.cpp" "${ENGINE_DIR}/src/r3d_mesh.cpp")
target_include_directories(render_core PUBLIC "${ENGINE_DIR}/src")
target_compile_features(render_core PUBLIC cxx_std_20)
target_compile_definitions(render_core PRIVATE _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(render_core PRIVATE /utf-8 /W4 /fp:precise)
else()
  target_compile_options(render_core PRIVATE -Wall -Wextra -ffp-contract=off)
endif()

# The surface is compiled into the application's module library when a game uses the
# engine; this target carries its headers to that game's code.
add_library(render_ui INTERFACE)
target_include_directories(render_ui INTERFACE "${ENGINE_DIR}/ui")
target_link_libraries(render_ui INTERFACE render_core)

include(CTest)
add_executable(render_tests "${ENGINE_DIR}/tests/render_tests.cpp")
target_link_libraries(render_tests PRIVATE render_core)
add_test(NAME render_engine COMMAND render_tests)
set_tests_properties(render_engine PROPERTIES TIMEOUT 60)

# Times the 3D renderer on a game-like scene: r3d_bench [frames].
add_executable(r3d_bench "${ENGINE_DIR}/tools/r3d_bench.cpp")
target_link_libraries(r3d_bench PRIVATE render_core)
