cmake_minimum_required(VERSION 3.20)
project(IRIS)

set(CMAKE_CXX_STANDARD 20)

add_executable(IRIS
    main.cpp
    Executor.cpp
    Executor.h
    log/Logger.cpp
    log/Logger.h
    parser/Instruction.h
    parser/InstructionFactory.cpp
    parser/InstructionFactory.h
    parser/Parser.cpp
    parser/Parser.h
    run/Runner.cpp
    run/Runner.h
    run/action/Action.h
    run/action/ClickAction.cpp
    run/action/ClickAction.h
    run/action/KeyPressAction.cpp
    run/action/KeyPressAction.h
    run/action/LogAction.cpp
    run/action/LogAction.h
    run/action/MoveMouseAction.cpp
    run/action/MoveMouseAction.h
    run/action/WaitAction.cpp
    run/action/WaitAction.h
)