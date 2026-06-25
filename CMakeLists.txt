cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /DNDEBUG /fp:fast /GL /Gw /Gy")
    if(IRIS_MSVC_PGO STREQUAL "GEN")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /GENPROFILE")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/LTCG /GENPROFILE")
    elseif(IRIS_MSVC_PGO STREQUAL "USE")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /USEPROFILE")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/LTCG /USEPROFILE")
    else()
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")
    endif()
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -fomit-frame-pointer")
    if(NOT MINGW)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fno-plt")
    endif()
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mllvm -inline-threshold=1000")
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
    "src/*.cpp"
    "src/*.h"
)

# Separate main.cpp for the IRIS executable
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Shared library target (all sources except main.cpp)
add_library(iris_core STATIC ${SOURCES})

target_include_directories(iris_core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/asmjit/src
)
target_link_libraries(iris_core PUBLIC asmjit::asmjit)

# Main executable
add_executable(IRIS src/main.cpp)
target_link_libraries(IRIS PRIVATE iris_core)

# --- Tests ---
option(IRIS_BUILD_TESTS "Build Iris unit tests" ON)
if(IRIS_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES "tests/test_*.cpp")
    add_executable(test_iris ${TEST_SOURCES})
    target_include_directories(test_iris PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/asmjit/src
    )
    target_link_libraries(test_iris PRIVATE iris_core)
endif()

if(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(NOT MSVC)
        target_link_options(iris_core PRIVATE -static)
    endif()
endif()

if(WIN32)
    target_link_libraries(iris_core PUBLIC user32 ws2_32)
endif()
