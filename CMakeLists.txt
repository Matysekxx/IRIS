cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)



if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
endif()

add_executable(IRIS
        lang/main.cpp
        lang/execute/Executor.cpp
        lang/execute/Executor.h
        lang/node/ASTNode.h
        lang/log/Logger.cpp
        lang/log/Logger.h
        lang/parser/Parser.cpp
        lang/parser/Parser.h
        lang/parser/NodeFactory.cpp
        lang/parser/NodeFactory.h
        lang/device/Win32Driver.cpp
        lang/device/Win32Driver.h
        lang/bytecode/OpCode.h
        lang/bytecode/Chunk.h
        lang/bytecode/Compiler.h
        lang/bytecode/Compiler.cpp
        lang/bytecode/VM.h
        lang/bytecode/VM.cpp
        lang/core/ArrayData.cpp
        lang/core/MemoryPool.h
)

if(MINGW)
    target_link_options(IRIS PRIVATE -static)
endif()