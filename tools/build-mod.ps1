#requires -Version 7.0
<# 编译一个最小 C/C++ 插件目录；产物可交给 aamod 发现。 #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ModDirectory)
$ErrorActionPreference = 'Stop'
$sdkRoot = Split-Path $PSScriptRoot
$modRoot = (Resolve-Path -LiteralPath $ModDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $modRoot 'mod.json') -Raw | ConvertFrom-Json
if (-not $manifest.id -or $manifest.entry -notmatch '^[^\\/:]+\.dll$') { throw 'mod.json 需要 id 和目录内 DLL 文件名 entry。' }
$sources = @(Get-ChildItem -LiteralPath $modRoot -File | Where-Object Extension -in @('.cpp','.c') | ForEach-Object FullName)
if (-not $sources.Count) { throw '插件目录内没有 C/C++ 源码。' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw '需要 Visual Studio C++ Build Tools。' }
$installation = $installation.Trim()
# 多次独立插件构建可能共享同一 pwsh；重复初始化会不断扩张 PATH。
$compiler = Get-Command cl -ErrorAction SilentlyContinue
$sameToolchain = $compiler -and $compiler.Source.StartsWith($installation + '\', [StringComparison]::OrdinalIgnoreCase) -and
    $compiler.Source -match '\\bin\\Hostx64\\x64\\cl\.exe$' -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64'
if (-not $sameToolchain) {
    & (Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $installation -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$objects = Join-Path $modRoot '.build'
New-Item -ItemType Directory -Path $objects -Force | Out-Null
$language = if(@($sources|Where-Object {[IO.Path]::GetExtension($_) -eq '.cpp'}).Count){'/std:c++17'}else{'/std:c11'}
$arguments = @('/nologo','/LD','/EHsc','/O2','/MD','/utf-8',$language,('/I'+(Join-Path $sdkRoot 'include')),('/Fo:'+ $objects+'\'),('/Fe:'+ (Join-Path $modRoot $manifest.entry))) + $sources
& cl @arguments
if ($LASTEXITCODE) { throw "插件编译失败：$LASTEXITCODE" }
Write-Output (Join-Path $modRoot $manifest.entry)
