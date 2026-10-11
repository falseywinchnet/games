cmake_minimum_required(VERSION 3.25)
project(EngineReview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REVIEW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(REVIEW_BUILD "${REVIEW_ROOT}/.build/app-dogfood" CACHE PATH "Existing complete application build")
add_executable(clip_probe clip_probe.cpp)
target_include_directories(clip_probe PRIVATE "${REVIEW_ROOT}/shared/ambient/src")
target_compile_options(clip_probe PRIVATE -ffp-contract=off)
add_executable(site_probe site_probe.cpp)
target_include_directories(site_probe PRIVATE
  "${REVIEW_ROOT}/games/zenconstruction/src"
  "${REVIEW_ROOT}/games/zenconstruction/phys"
  "${REVIEW_ROOT}/shared/render/src")
target_compile_options(site_probe PRIVATE -ffp-contract=off)
target_link_libraries(site_probe PRIVATE
  "${REVIEW_BUILD}/libzc_core.a" "${REVIEW_BUILD}/librender_core.a"
  "${REVIEW_BUILD}/libzc_phys.a" "${REVIEW_BUILD}/libgame_paths.a")
