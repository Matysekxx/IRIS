cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
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

if(MINGW)
    target_link_options(IRIS PRIVATE -static)
endif()

if(WIN32)
    target_link_libraries(IRIS PRIVATE user32)
endif()
