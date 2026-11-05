@echo off
setlocal
set "PIO_EXE=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO_EXE%" (
    echo PlatformIO was not found at its default Windows installation path.
    exit /b 1
)
if not "%~1"=="" if /I not "%~1"=="build" if /I not "%~1"=="upload" goto usage
if /I "%~1"=="upload" if "%~2"=="" goto usage
set "PROJECT_ROOT=%~dp0.."
set "SOURCE_COPY=%TEMP%\CatFeeder-firmware-source"
set "PLATFORMIO_WORKSPACE_DIR=%TEMP%\CatFeeder-firmware-build"
if not exist "%SOURCE_COPY%" mkdir "%SOURCE_COPY%"
for %%F in (CMakeLists.txt platformio.ini sdkconfig.defaults partitions.csv) do (
    copy /Y "%PROJECT_ROOT%\%%F" "%SOURCE_COPY%\%%F" >nul
    if errorlevel 1 exit /b 1
)
for %%D in (src include simulation web) do (
    xcopy "%PROJECT_ROOT%\%%D" "%SOURCE_COPY%\%%D\" /E /I /Y /Q >nul
    if errorlevel 1 exit /b 1
)
if /I "%~1"=="upload" (
    "%PIO_EXE%" run --project-dir "%SOURCE_COPY%" -e esp32c6-mock -t upload --upload-port "%~2"
) else (
    "%PIO_EXE%" run --project-dir "%SOURCE_COPY%" -e esp32c6-mock
)
exit /b %ERRORLEVEL%

:usage
echo Usage: scripts\firmware.cmd [build] or scripts\firmware.cmd upload COM3
exit /b 1
