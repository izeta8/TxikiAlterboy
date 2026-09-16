@echo off
call C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat >nul
if not exist build\test mkdir build\test
cl /nologo /O2 /EHsc /std:c++17 /Fo:build\test\ /Fe:build\test\OfflineTest.exe tests\OfflineTest.cpp || exit /b 1
build\test\OfflineTest.exe %1
