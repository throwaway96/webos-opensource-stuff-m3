#cmake_minimum_required (VERSION 2.62) 

project (DRMFILESRC)
include_directories ("${PROJECT_SOURCE_DIR}/drmfilesrc")           # to find header files
include_directories ("${PROJECT_SOURCE_DIR}/gstreamer-1.0.2")
include_directories ("${PROJECT_SOURCE_DIR}/gstreamer-1.0.2/libs")
include_directories ("${PROJECT_SOURCE_DIR}/glib-2.34.1/glib")
include_directories ("${PROJECT_SOURCE_DIR}/glib-2.34.1")
add_definitions(-D HAVE_CONFIG_H)
add_library(drmfilesrc SHARED drmfile/gstdrmfilesrc.c drmfile/gstcompatdrm.c) 

