@echo off
call C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat >nul
cd /d %~dp0
if not exist build\test mkdir build\test
if "%~1"=="" (set OUT=build\test) else (set OUT=%~1)
cl /nologo /O2 /EHsc /std:c++17 /Fo:build\test\ /Fe:build\test\OfflineTest.exe tests\OfflineTest.cpp || exit /b 1
build\test\OfflineTest.exe %OUT% || exit /b 1
build\test\OfflineTest.exe --voices %OUT% tests\voices\helena_es.wav tests\voices\zira_en.wav || exit /b 1
echo ALL TESTS OK
