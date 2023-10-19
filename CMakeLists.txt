cmake_minimum_required(VERSION 3.13)
project(video_switch)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0) 
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)

add_executable(showcase "video_switch_pipeline.cpp")
target_include_directories(showcase PRIVATE ${GSTREAMER_INCLUDE_DIRS} ${GST_APP_INCLUDE_DIRS} ${GLIB_INCLUDEDIR})
target_link_libraries(showcase ${GSTREAMER_LIBRARIES} ${GST_APP_LIBRARIES})