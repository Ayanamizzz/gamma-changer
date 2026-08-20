$ErrorActionPreference = "Stop"

function Find-CMakeExecutable {
    $pathCommand = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $pathCommand) {
        return $pathCommand.Source
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $installations = @(& $vswhere -products * -requires `
            Microsoft.VisualStudio.Component.VC.CMake.Project `
            -property installationPath)
        foreach ($installation in $installations) {
            if ([string]::IsNullOrWhiteSpace($installation)) { continue }
            $candidate = Join-Path $installation `
                "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $programFiles = [Environment]::GetFolderPath("ProgramFiles")
    foreach ($root in @($programFiles, $programFilesX86)) {
        foreach ($edition in @("BuildTools", "Community", "Professional", "Enterprise")) {
            $candidate = Join-Path $root `
                "Microsoft Visual Studio\2022\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    throw @"
CMake 3.21 or newer was not found.
Install the Visual Studio 2022 workload 'Desktop development with C++' and
the component 'C++ CMake tools for Windows', or add cmake.exe to PATH.
"@
}

$cmake = Find-CMakeExecutable
$ctest = Join-Path (Split-Path -Parent $cmake) "ctest.exe"
if (-not (Test-Path -LiteralPath $ctest)) {
    throw "CTest was not found next to CMake: $ctest"
}

$project_root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $project_root
try {
    Write-Host "Using CMake: $cmake"
    & $cmake --preset vs2022-x64
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset release-gui
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset release-checks
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $ctest --preset release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host "Build complete: build\vs2022-x64\Release\gamma_changer_gui.exe"
}
finally {
    Pop-Location
}
