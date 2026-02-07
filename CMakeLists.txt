cmake_minimum_required(VERSION 3.31)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)

file(GLOB SOURCES "*.cpp" "*.h")

add_executable(IRIS ${SOURCES})