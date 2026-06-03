# 在 c_version 目录执行：编译 Release 并将 build\Release 打成 zip，便于分发。
# 用法: .\pack_release.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build"
$releaseDir = Join-Path $buildDir "Release"
if (-not (Test-Path $buildDir)) {
    Write-Error "请先创建 build 目录并执行 cmake 配置: mkdir build; cd build; cmake .."
}
Push-Location $buildDir
try {
    cmake --build . --config Release --target douyin_qr_gui
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
if (-not (Test-Path (Join-Path $releaseDir "douyin_qr.exe"))) {
    Write-Error "未找到 build\Release\douyin_qr.exe"
}
$stamp = Get-Date -Format "yyyyMMdd_HHmm"
$zipName = "douyin_qr_win_x64_Release_$stamp.zip"
$zipPath = Join-Path $root $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path (Join-Path $releaseDir "*") -DestinationPath $zipPath -CompressionLevel Optimal
Write-Host "已生成: $zipPath"
(Get-Item $zipPath).Length / 1MB | ForEach-Object { Write-Host ("约 {0:N1} MB" -f $_) }
