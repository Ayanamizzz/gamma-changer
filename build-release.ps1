$ErrorActionPreference = "Stop"
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at $cmake"
}

& $cmake --preset vs2022-x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build --preset release-gui
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Build complete: build\vs2022-x64\Release\gamma_changer_gui.exe"
