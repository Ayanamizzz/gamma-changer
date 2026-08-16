@echo off
setlocal
cd /d "%~dp0"

set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if exist "%CMAKE_EXE%" goto :cmake_found
echo CMake was not found at:
echo %CMAKE_EXE%
echo Please install the C++ CMake tools from Visual Studio Installer.
exit /b 1

:cmake_found

echo Configuring Visual Studio 2022 x64...
"%CMAKE_EXE%" --preset vs2022-x64
if errorlevel 1 exit /b 1

echo Building gamma_changer_gui...
"%CMAKE_EXE%" --build --preset release-gui
if errorlevel 1 exit /b 1

echo Building automated checks...
"%CMAKE_EXE%" --build --preset release-checks
if errorlevel 1 exit /b 1

for %%I in ("%CMAKE_EXE%") do set "CMAKE_DIR=%%~dpI"
echo Running automated checks...
"%CMAKE_DIR%ctest.exe" --preset release
if errorlevel 1 exit /b 1

echo.
echo Build complete:
echo build\vs2022-x64\Release\gamma_changer_gui.exe
endlocal
