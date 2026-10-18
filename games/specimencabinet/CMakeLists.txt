# Specimen Cabinet's own headless build: its rules, its tests and the preview tools,
# on the stones engine. The PlaySuite window is built by the application.
cmake_minimum_required(VERSION 3.25)
project(SpecimenCabinet VERSION 0.1.0 LANGUAGES CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/StandaloneGame.cmake")
