@echo off
setlocal
set "PROJECT_ROOT=%~dp0.."
set "BUILD_DIR=%PROJECT_ROOT%\build"
if not "%~1"=="" set "BUILD_DIR=%~f1"
set "CMAKE_EXE="
for /f "delims=" %%P in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%P"
if defined CMAKE_EXE goto tools_found

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto tools_missing
set "VS_INSTALL="
for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%P"
if not defined VS_INSTALL goto tools_missing
set "CMAKE_EXE=%VS_INSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" goto tools_missing

:tools_found
for %%P in ("%CMAKE_EXE%") do set "CTEST_EXE=%%~dpPctest.exe"
"%CMAKE_EXE%" -S "%PROJECT_ROOT%" -B "%BUILD_DIR%"
if errorlevel 1 exit /b 1
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Debug
if errorlevel 1 exit /b 1
"%CTEST_EXE%" --test-dir "%BUILD_DIR%" -C Debug --output-on-failure -V
if errorlevel 1 exit /b 1
set "SIM_EXE=%BUILD_DIR%\Debug\catfeeder_sim.exe"
if not exist "%SIM_EXE%" set "SIM_EXE=%BUILD_DIR%\catfeeder_sim.exe"
"%SIM_EXE%"
exit /b %ERRORLEVEL%

:tools_missing
echo CMake and a C++ compiler are required. Visual Studio C++ Build Tools with the CMake component is also supported.
exit /b 1
