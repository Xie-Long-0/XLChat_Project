<#
.SYNOPSIS
    免维护地加载 Visual Studio 开发环境（INCLUDE / LIB / PATH）并配置 / 构建 XYChat。

.DESCRIPTION
    通过 vswhere.exe 动态定位最新的、带 C++ 工具集的 Visual Studio 安装，
    再调用官方 Launch-VsDevShell.ps1 载入 x64 工具链（自动选择最新 MSVC 与
    Windows SDK）。因此无需在 CMakeUserPresets.json 中硬编码 MSVC / SDK 版本号，
    VS 升级后本脚本仍然可用。

    注意：为使脚本注入的动态环境生效，msvc2026 预设的 environment 必须保持为空，
    否则预设中硬编码的 INCLUDE / LIB / PATH 会覆盖脚本载入的值。

.PARAMETER Preset
    CMake 配置预设名，默认 Qt-Debug（可选 Qt-Release）。

.PARAMETER Build
    指定后在配置完成后执行 cmake --build --preset <Preset>。

.PARAMETER EnvOnly
    仅载入 VS 开发环境，不运行 cmake。配合点源使用可把环境注入当前终端：
        . ./Build.ps1 -EnvOnly

.EXAMPLE
    ./Build.ps1                             # 仅配置 Qt-Debug
    ./Build.ps1 -Preset Qt-Release -Build   # 配置并构建 Release
    . ./Build.ps1 -EnvOnly                  # 只把 VS 环境载入当前 shell
#>
[CmdletBinding()]
param(
    [ValidateSet('Qt-Debug', 'Qt-Release')]
    [string]$Preset = 'Qt-Debug',
    [switch]$Build,
    [switch]$EnvOnly
)

$ErrorActionPreference = 'Stop'

# 1) 用 vswhere 定位最新且带 VC 工具集的 VS 安装（不硬编码路径 / 版本）
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "未找到 vswhere.exe，请确认已安装 Visual Studio（含 C++ 桌面开发工作负载）。"
}

$vsPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1

if (-not $vsPath) {
    throw "vswhere 未找到带 C++ 工具集的 Visual Studio 安装。"
}

# 2) 载入 VS 开发者环境（自动挑选最新 MSVC + Windows SDK，设置 INCLUDE / LIB / PATH）
$devShell = Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1'
if (-not (Test-Path $devShell)) {
    throw "未找到 Launch-VsDevShell.ps1：$devShell"
}

Write-Host "载入 VS 开发环境: $vsPath" -ForegroundColor Cyan
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

# 校验工具链是否可用
$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    throw "VS 环境载入后仍找不到 cl.exe，请检查 Visual Studio C++ 组件是否完整。"
}
Write-Host "cl.exe -> $($cl.Source)" -ForegroundColor Green

if ($EnvOnly) {
    Write-Host "已仅载入环境（-EnvOnly），未执行 cmake。" -ForegroundColor Yellow
    return
}

# 3) 配置 / 构建
Set-Location $PSScriptRoot

Write-Host "cmake --preset $Preset" -ForegroundColor Cyan
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "cmake 配置失败（退出码 $LASTEXITCODE）。" }

if ($Build) {
    Write-Host "cmake --build --preset $Preset" -ForegroundColor Cyan
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "cmake 构建失败（退出码 $LASTEXITCODE）。" }
}
