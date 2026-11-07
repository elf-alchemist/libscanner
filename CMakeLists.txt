cmake_minimum_required(VERSION 3.23)

project(scanner
  LANGUAGES CXX
)

add_library(scanner STATIC)

target_sources(scanner
  PRIVATE
    src/scanner.cpp
  PUBLIC FILE_SET HEADERS BASE_DIRS
    include
)

target_compile_features(scanner PUBLIC cxx_std_11)
