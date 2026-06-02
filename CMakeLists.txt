cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG /fp:fast /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native -ffast-math")
    if(NOT MINGW)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fno-plt")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fomit-frame-pointer -mllvm -inline-threshold=1000")
    endif()
endif()

# Include AsmJit
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asmjit/CMakeLists.txt")
    message(FATAL_ERROR "asmjit not found. Please run 'git clone https://github.com/asmjit/asmjit.git'")
endif()

set(ASMJIT_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(asmjit)

# Source files
file(GLOB_RECURSE SOURCES 
    "lang/*.cpp"
    "lang/*.h"
)

add_executable(IRIS ${SOURCES})

# Include paths
target_include_directories(IRIS PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/asmjit/src
)

# Link libraries
target_link_libraries(IRIS PRIVATE asmjit::asmjit)

if(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(NOT MSVC)
        target_link_options(IRIS PRIVATE -static)
    endif()
endif()

if(WIN32)
    target_link_libraries(IRIS PRIVATE user32)
endif()
