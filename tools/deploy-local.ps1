[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$StageDir,
    [string]$QtRoot = $env:QT_ROOT
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $projectRoot "build\windows-mingw-release"
}

if ([string]::IsNullOrWhiteSpace($StageDir)) {
    $StageDir = Join-Path $projectRoot "stage\windows-mingw-release"
}

$exe = Join-Path $BuildDir "appSubRec.exe"
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw "Qt root not set. Pass -QtRoot <Qt kit dir, e.g. .../6.8.0/mingw_64> or set the QT_ROOT environment variable."
}
$windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build output was not found: $exe. Run the Windows MinGW Release build first."
}

if (-not (Test-Path -LiteralPath $windeployqt)) {
    throw "windeployqt was not found: $windeployqt"
}

Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $StageDir -Force
Get-ChildItem -LiteralPath $BuildDir -Filter "*.dll" -File | Copy-Item -Destination $StageDir -Force

& $windeployqt --release --qmldir (Join-Path $projectRoot "qml") --dir $StageDir (Join-Path $StageDir "appSubRec.exe")
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

Write-Host "Local deployment staged at: $StageDir"
