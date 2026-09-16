@echo off
call C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat >nul
set PATH=C:\Program Files\CMake\bin;%PATH%
cd /d %~dp0
cmake -S . -B build\vs -G "Visual Studio 17 2022" -A x64 || exit /b 1
cmake --build build\vs --config Release --target TxikiAlterboy_VST3 TxikiAlterboy_Standalone -- /m /v:minimal || exit /b 1
