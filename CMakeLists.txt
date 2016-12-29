cmake_minimum_required (VERSION 2.8)
project(GrubPass)

include_directories( "./include" )

find_package(Threads REQUIRED)
find_package(sendao REQUIRED)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-std=c++11")
include_directories( /usr/local/include/sendao )

add_executable(deploy src/deploy.cpp src/pipe.cpp src/rce.cpp src/scripts.cpp src/prescript.cpp)
target_link_libraries(deploy ${SENDAO_LIBRARY_PATH} ${CMAKE_THREAD_LIBS_INIT})
