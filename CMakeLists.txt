cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)

add_executable(IRIS
    main.cpp
    Executor.cpp
    Executor.h
    LTSNode.h
    LTSNode.cpp
    log/Logger.cpp
    log/Logger.h
    parser/Parser.cpp
    parser/Parser.h
)

if(MINGW)
    target_link_options(IRIS PRIVATE -static)
endif()