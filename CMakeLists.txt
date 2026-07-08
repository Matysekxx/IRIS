cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags - aggressive for LuaJIT-level performance
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -fomit-frame-pointer -march=native -mtune=native")
if(NOT MINGW)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto -fno-plt")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mllvm -inline-threshold=1000 -mllvm -unroll-threshold=100")
endif()

# Include AsmJit
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asmjit/CMakeLists.txt")
    message(FATAL_ERROR "asmjit not found. Please run 'git clone https://github.com/asmjit/asmjit.git'")
endif()

set(ASMJIT_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(asmjit)

# Source files - organized by module
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Separate main.cpp for the IRIS executable
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Static library target (all sources except main.cpp)
add_library(iris_core STATIC ${SOURCES})

target_include_directories(iris_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/std
    ${CMAKE_CURRENT_SOURCE_DIR}/asmjit/src
)
target_compile_definitions(iris_core PRIVATE IRIS_CORE_EXPORTS)
target_link_libraries(iris_core PUBLIC asmjit::asmjit)

# Main executable
add_executable(IRIS src/main.cpp)
target_link_libraries(IRIS PRIVATE iris_core)

# --- Tests ---
option(IRIS_BUILD_TESTS "Build Iris unit tests" ON)
if(IRIS_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES "tests/unit/*.cpp")
    add_executable(test_iris ${TEST_SOURCES})
    target_include_directories(test_iris PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/asmjit/src
    )
    target_link_libraries(test_iris PRIVATE iris_core)
    add_test(NAME iris_unit_tests COMMAND test_iris)
endif()

# Static linking for MinGW/Clang
if(MINGW OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_link_options(IRIS PRIVATE -static -pthread)
    if(IRIS_BUILD_TESTS)
        target_link_options(test_iris PRIVATE -static -pthread)
    endif()
endif()

if(WIN32)
    target_link_libraries(iris_core PUBLIC user32 ws2_32)
endif()

# Install
install(TARGETS IRIS DESTINATION bin)
