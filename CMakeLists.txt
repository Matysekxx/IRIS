cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)



if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

add_executable(IRIS
    main.cpp
    execute/Executor.cpp
    execute/Executor.h
    node/ASTNode.h
    log/Logger.cpp
    log/Logger.h
    parser/Parser.cpp
    parser/Parser.h
    parser/NodeFactory.cpp
    parser/NodeFactory.h
    device/Win32Driver.cpp
    device/Win32Driver.h
    bytecode/OpCode.h
    bytecode/Chunk.h
    bytecode/Compiler.h
    bytecode/Compiler.cpp
    bytecode/VM.h
    bytecode/VM.cpp
)

if(MINGW)
    target_link_options(IRIS PRIVATE -static)
endif()