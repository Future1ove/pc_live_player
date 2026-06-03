# Package build\Release for distribution. Run from c_version: .\package_release.ps1

param(
    [string]$SourceDir = "",
    [string]$DistRoot = "",
    [switch]$SkipWindeployqt
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
if (-not $SourceDir) { $SourceDir = Join-Path $Root "build\Release" }
if (-not $DistRoot) { $DistRoot = Join-Path $Root "dist" }

$exePath = Join-Path $SourceDir "douyin_qr.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "ERROR: missing $exePath" -ForegroundColor Red
    Write-Host "Build first: cmake --build build --config Release --target douyin_qr_gui"
    exit 1
}

$stamp = Get-Date -Format "yyyyMMdd_HHmm"
$pkgName = "douyin_qr_Windows_x64_$stamp"
$dest = Join-Path $DistRoot $pkgName

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Write-Host "Copying -> $dest"
Copy-Item -Path (Join-Path $SourceDir "*") -Destination $dest -Recurse -Force

Get-ChildItem -Path $dest -Recurse -Include *.lib, *.exp, *.pdb -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

if (-not $SkipWindeployqt) {
    $candidates = @(
        (Join-Path $Root "build\vcpkg_installed\x64-windows\tools\Qt6\bin\windeployqt.exe"),
        (Join-Path $Root "build\vcpkg_installed\x64-windows\tools\Qt6\bin\windeployqt6.exe")
    )
    $wdqt = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($wdqt) {
        Write-Host "windeployqt: $wdqt"
        & $wdqt --release --compiler-runtime (Join-Path $dest "douyin_qr.exe")
    } else {
        Write-Host "WARN: windeployqt not found, skipped" -ForegroundColor Yellow
    }
}

$launcherDir = Join-Path $Root "packaging"
Get-ChildItem -Path $launcherDir -Filter "*.bat" -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination $dest -Force
}

$readmePath = Join-Path $dest "README.txt"
$readmeText = @(
    "========================================",
    "  Douyin QR - Windows x64 Portable",
    "========================================",
    "",
    "1. Extract this folder.",
    "2. Run the .bat launcher or douyin_qr.exe.",
    "3. Enter license key when prompted.",
    "",
    "Requires Windows 10/11 x64.",
    "If startup fails, install VC++ Redistributable x64."
) -join "`r`n"
Set-Content -Path $readmePath -Value $readmeText -Encoding UTF8

$zipPath = Join-Path $DistRoot "$pkgName.zip"
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Write-Host "Zipping -> $zipPath"
Compress-Archive -Path $dest -DestinationPath $zipPath -CompressionLevel Optimal

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "Folder: $dest"
Write-Host "Zip:    $zipPath"
$mb = [math]::Round((Get-Item $zipPath).Length / 1MB, 2)
Write-Host "Size:   $mb MB"
