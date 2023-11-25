cmake_minimum_required(VERSION 3.12...3.27)

project(gnuplot_i
  DESCRIPTION "A gnuplot interface library written in C."
  LANGUAGES C
)

# This project is an OBJECT-library, used by other git submodules and the main project.
add_library(gnuplot_i_ObjLib OBJECT)

# The list of source files.
target_sources(gnuplot_i_ObjLib
  PRIVATE
    "gnuplot_i.c"
    "gnuplot_i.h"
)

target_include_directories(gnuplot_i_ObjLib
  INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Create an ALIAS target.
add_library(gnuplot_i::gnuplot_i ALIAS gnuplot_i_ObjLib)
