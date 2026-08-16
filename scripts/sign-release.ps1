param(
    [Parameter(Mandatory = $true)]
    [string]$ExePath,

    [string]$CertificatePath = "",
    [string]$CertificateBase64 = $env:SIGNING_CERT_BASE64,
    [string]$CertificatePassword = $env:SIGNING_CERT_PASSWORD,
    [string]$TimestampServer = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"

$exe = (Resolve-Path -LiteralPath $ExePath).Path
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Executable was not found: $exe"
}

$temporaryCertificate = ""
try {
    if (-not $CertificatePath) {
        if ([string]::IsNullOrWhiteSpace($CertificateBase64)) {
            Write-Host "No signing certificate configured; skipping code signing."
            exit 0
        }
        $temporaryCertificate = Join-Path ([System.IO.Path]::GetTempPath()) `
            ("gamma-changer-signing-" + [guid]::NewGuid().ToString("N") + ".pfx")
        [System.IO.File]::WriteAllBytes(
            $temporaryCertificate,
            [Convert]::FromBase64String($CertificateBase64))
        $CertificatePath = $temporaryCertificate
    }

    if (-not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
        throw "Signing certificate was not found: $CertificatePath"
    }

    $kitsRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
    if (-not (Test-Path -LiteralPath $kitsRoot)) {
        throw "Windows SDK was not found at $kitsRoot"
    }
    $signtool = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter signtool.exe |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $signtool) {
        throw "signtool.exe was not found under $kitsRoot"
    }

    Write-Host "Signing $exe with $($signtool.FullName)"
    $arguments = @(
        "sign",
        "/fd", "SHA256",
        "/f", $CertificatePath,
        "/p", [string]$CertificatePassword,
        "/tr", $TimestampServer,
        "/td", "SHA256",
        "/d", "Gamma Changer",
        "/du", "https://github.com/Ayanamizzz/gamma-changer",
        $exe
    )
    & $signtool.FullName $arguments
    if ($LASTEXITCODE -ne 0) {
        throw "signtool failed with exit code $LASTEXITCODE"
    }
    Write-Host "Code signing completed."
}
finally {
    if ($temporaryCertificate -and (Test-Path -LiteralPath $temporaryCertificate)) {
        Remove-Item -LiteralPath $temporaryCertificate -Force
    }
}
