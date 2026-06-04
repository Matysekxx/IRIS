@echo off
setlocal enabledelayedexpansion

set IRIS_EXE=build_msvc\Release\IRIS.exe
set LUA_EXE=lua
set PYTHON_EXE=python

echo ========================================
echo IRIS vs LUA vs PYTHON BENCHMARKS
echo ========================================
echo.

echo [1] FIBONACCI (RECURSION STRESS)
echo ----------------------------------------
echo IRIS:
"%IRIS_EXE%" benchmarks\fib.iris | findstr "VM"
echo LUA:
"%LUA_EXE%" benchmarks\fib.lua
echo PYTHON:
"%PYTHON_EXE%" benchmarks\fib.py
echo.

echo [2] LOOP (ARITHMETIC STRESS)
echo ----------------------------------------
echo IRIS:
"%IRIS_EXE%" benchmarks\loop.iris | findstr "VM"
echo LUA:
"%LUA_EXE%" benchmarks\loop.lua
echo PYTHON:
"%PYTHON_EXE%" benchmarks\loop.py
echo.

echo [3] OBJECTS (ALLOCATION STRESS)
echo ----------------------------------------
echo IRIS:
"%IRIS_EXE%" benchmarks\stress_objects.iris | findstr "VM"
echo LUA:
"%LUA_EXE%" benchmarks\stress_objects.lua
echo PYTHON:
"%PYTHON_EXE%" benchmarks\stress_objects.py
echo.

echo ========================================
echo Done.
pause
