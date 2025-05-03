cmake_minimum_required(VERSION 3.22)
project(TeensyUploader)

set(CMAKE_CXX_STANDARD 20)

add_executable(TeensyUploader src/main.cpp)
if (WIN32)
    target_link_libraries(TeensyUploader ws2_32)
endif()