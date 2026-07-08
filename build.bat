@echo off
echo ===================================================
echo Loading Visual Studio Build Tools environment...
echo ===================================================
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64

if %ERRORLEVEL% neq 0 (
    echo Failed to load VS build environment!
    exit /b %ERRORLEVEL%
)

if not exist build mkdir build

echo.
echo ===================================================
echo Compiling dispatcher library sources...
echo ===================================================
cl /EHsc /std:c++17 /W4 /WX /Iinclude src/event_bus.cpp src/handler_registry.cpp src/thread_pool.cpp src/stats_sampler.cpp /c /Fobuild\

if %ERRORLEVEL% neq 0 (
    echo Compilation of library sources failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo Creating static library (dispatcher.lib)...
echo ===================================================
lib /OUT:build\dispatcher.lib build\event_bus.obj build\handler_registry.obj build\thread_pool.obj build\stats_sampler.obj

if %ERRORLEVEL% neq 0 (
    echo Failed to create static library!
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo Compiling simulation executable...
echo ===================================================
cl /EHsc /std:c++17 /W4 /WX /Iinclude src/main.cpp build\dispatcher.lib /Febuild\event_dispatcher.exe

if %ERRORLEVEL% neq 0 (
    echo Compilation of main.cpp failed!
    exit /b %ERRORLEVEL%
)

echo.
echo ===================================================
echo Compiling tests...
echo ===================================================
cl /EHsc /std:c++17 /W4 /WX /Iinclude tests/test_priority_ordering.cpp build\dispatcher.lib /Febuild\test_priority_ordering.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /EHsc /std:c++17 /W4 /WX /Iinclude tests/test_no_starvation.cpp build\dispatcher.lib /Febuild\test_no_starvation.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

cl /EHsc /std:c++17 /W4 /WX /Iinclude tests/test_latency_under_load.cpp build\dispatcher.lib /Febuild\test_latency_under_load.exe
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo.
echo ===================================================
echo Build Succeeded! Executables are in build/
echo ===================================================
