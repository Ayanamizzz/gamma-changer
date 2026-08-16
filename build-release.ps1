$ErrorActionPreference = "Stop"
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at $cmake"
}

$project_root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $project_root
try {
    & $cmake --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset release-gui
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset release-checks
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
    & $ctest --preset release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Build complete: build\vs2022-x64\Release\gamma_changer_gui.exe"
}
finally {
    Pop-Location
}
