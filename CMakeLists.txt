cmake_minimum_required(VERSION 3.5)
project(USBCamCapture)
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

set(BUILD_SHARED_LIBS ON)

# Still toying with multithreaded application
#add_executable(test_multithreaded_cam multithreaded_camera.cpp)
#target_link_libraries(test_multithreaded_cam ${OpenCV_LIBS} Threads::Threads)

set(SOURCES v4l2_camera.cpp)

add_library(v4l2_camera SHARED ${SOURCES})

add_executable(test_v4l2_cam v4l2_camera.cpp)
target_link_libraries(test_v4l2_cam)
