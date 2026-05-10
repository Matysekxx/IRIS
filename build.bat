@echo off
cmake -B cmake-build-release -S . -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release --config Release
echo Build complete.