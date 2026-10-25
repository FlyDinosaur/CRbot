# 构建并打包 Windows 预发布版
#
# 用法：
#   pwsh -File tools\package.ps1
#   pwsh -File tools\package.ps1 -Config Release
#
# 产物：dist\CRBot-<版本>-win64\ 和 dist\CRBot-<版本>-win64.zip
# 注意：需要 MSVC。脚本会自己找 VS 的 vcvars64.bat、CLion 自带的 cmake/ninja。

param(
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$version = (Get-Content (Join-Path $root "VERSION") -Raw).Trim()
$buildDir = Join-Path $root "build-release"
$distDir = Join-Path $root "dist"
$pkgName = "CRBot-$version-win64"
$pkgDir = Join-Path $distDir $pkgName

Write-Host "==> CRBot $version ($Config)"

# ---- 找 cmake / ninja ----
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = Join-Path $env:LOCALAPPDATA "Programs\CLion\bin\cmake\win\x64\bin\cmake.exe"
}
if (-not (Test-Path $cmake)) {
    throw "找不到 cmake。装一个 CMake 或者把它加进 PATH。"
}
$ninjaDir = Join-Path $env:LOCALAPPDATA "Programs\CLion\bin\ninja\win\x64"
if (Test-Path (Join-Path $ninjaDir "ninja.exe")) {
    $env:PATH = "$ninjaDir;$env:PATH"
}

# ---- 找 MSVC 环境 ----
$vcvars = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\17\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\17\Professional\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
) | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $vcvars) {
    throw "找不到 vcvars64.bat，请先安装 Visual Studio（C++ 桌面开发）。"
}
Write-Host "==> MSVC: $vcvars"

# ---- 配置 + 构建 ----
$cmd = "call `"$vcvars`" >nul && `"$cmake`" -S `"$root`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=$Config && `"$cmake`" --build `"$buildDir`""
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "构建失败（exit $LASTEXITCODE）" }

# ---- 打包 ----
$exe = Join-Path $buildDir "CRbot.exe"
if (-not (Test-Path $exe)) { throw "没有找到 $exe" }

if (Test-Path $pkgDir) { Remove-Item $pkgDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $pkgDir | Out-Null

Copy-Item $exe $pkgDir
Copy-Item (Join-Path $root "assets\templates") $pkgDir -Recurse
Copy-Item (Join-Path $root "assets\README.md") $pkgDir
Copy-Item (Join-Path $root "README.md"), (Join-Path $root "CHANGELOG.md"), (Join-Path $root "VERSION") $pkgDir
New-Item -ItemType Directory -Force -Path (Join-Path $pkgDir "docs") | Out-Null
Copy-Item (Join-Path $root "docs\圣水识别方案.md") (Join-Path $pkgDir "docs")

$zip = "$pkgDir.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $pkgDir -DestinationPath $zip

Write-Host ""
Write-Host "==> 完成"
Write-Host "    目录: $pkgDir"
Write-Host "    压缩包: $zip"
