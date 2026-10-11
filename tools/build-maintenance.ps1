#requires -Version 7.0
<# 单独构建只读维护扫描器，不重建或改写已验收的核心 DLL。 #>
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(-not(Test-Path -LiteralPath $vswhere)){throw '需要 Visual Studio C++ Build Tools。'}
$installation=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw '没有找到 x64 C++ 编译工具。'}
$installation=$installation.Trim()
$compiler=Get-Command cl -ErrorAction SilentlyContinue
if(-not ($compiler -and $compiler.Source.StartsWith($installation+'\',[StringComparison]::OrdinalIgnoreCase) -and $compiler.Source -match '\\bin\\Hostx64\\x64\\cl\.exe$' -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64')){
    & (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -VsInstallationPath $installation -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}
$out=Join-Path $root 'out/maintenance'
$obj=Join-Path $out 'obj'
New-Item -ItemType Directory -Path $obj -Force | Out-Null
$arguments=@('/nologo','/std:c++17','/EHsc','/O2','/MD','/W3','/utf-8',('/I'+(Join-Path $root 'include')),('/Fo:'+$obj+'\'),('/Fe:'+(Join-Path $out 'layout-probe.exe')),
    (Join-Path $root 'tools/maintenance/layout-probe.cpp'),(Join-Path $root 'src/core/game_layout.cpp'),(Join-Path $root 'src/core/log.cpp'),'/link','shell32.lib')
& cl @arguments
if($LASTEXITCODE){throw "维护扫描器构建失败：$LASTEXITCODE"}
Write-Output (Join-Path $out 'layout-probe.exe')
