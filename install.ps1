<#
.SYNOPSIS
    Installs or removes the aamod loader in the Astral Ascent game directory.

.DESCRIPTION
    The loader is injected through a proxy DLL (the "shim"). The game imports
    winmm.dll statically, and the Windows loader searches the application
    directory before System32, so a winmm.dll next to the game executable is
    loaded instead of the system one. The shim forwards every export to a
    renamed copy of the real DLL (<shim>Hooked.dll) and loads aamod_core.dll.

    Nothing is written outside the game directory. A manifest (aamod/install.json)
    records every file and its hash so -Uninstall can remove exactly what was
    installed. -WhatIf prints the plan and changes nothing.

.EXAMPLE
    .\install.ps1 -WhatIf
    .\install.ps1
    .\install.ps1 -Shim version -Force
    .\install.ps1 -Uninstall
#>
[CmdletBinding(SupportsShouldProcess = $true, DefaultParameterSetName = 'Install')]
param(
    [Parameter(ParameterSetName = 'Install')]
    [Parameter(ParameterSetName = 'Uninstall')]
    [string]$GameDir = '',

    [Parameter(ParameterSetName = 'Install')]
    [ValidateSet('winmm', 'version', 'd3d11')]
    [string]$Shim = 'winmm',

    [Parameter(ParameterSetName = 'Install')]
    [switch]$Force,

    [Parameter(ParameterSetName = 'Uninstall')]
    [switch]$Uninstall,

    [Parameter(ParameterSetName = 'Uninstall')]
    [switch]$KeepData
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'out'
$gameExeName = 'Astral Ascent.exe'
$manifestName = 'install.json'
$dataDirName = 'aamod'

function Get-GameDir {
    if ($GameDir) { return (Resolve-Path -LiteralPath $GameDir).Path }

    # Try the Steam library folders, then a couple of well-known locations.
    $steam = 'C:\Program Files (x86)\Steam'
    $candidates = @()
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s+"([^"]+)"')) {
            $p = $m.Groups[1].Value -replace '\\\\', '\'
            $candidates += (Join-Path $p 'steamapps\common\Astral Ascent')
        }
    }
    $candidates += (Join-Path $steam 'steamapps\common\Astral Ascent')
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c $gameExeName)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    throw "Could not find '$gameExeName'. Pass -GameDir <path>."
}

function Assert-GameNotRunning {
    $proc = Get-Process -Name 'Astral Ascent' -ErrorAction SilentlyContinue
    if ($proc) { throw "'Astral Ascent' is running (pid $($proc.Id -join ', ')). Close it first." }
}

function Assert-KnownDllOk([string]$name) {
    # A DLL listed under KnownDLLs is always resolved from System32 and can
    # never be proxied.
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs'
    if (Test-Path $key) {
        $values = (Get-ItemProperty $key).PSObject.Properties |
            Where-Object { $_.Name -notlike 'PS*' } |
            Select-Object -ExpandProperty Value
        if ($values -contains "$name.dll") {
            throw "$name.dll is a KnownDLL on this system - pick another shim."
        }
    }
}

function Write-Plan([string[]]$lines) {
    Write-Host ''
    foreach ($l in $lines) { Write-Host "  $l" }
    Write-Host ''
}

# ---------------------------------------------------------------- install
if (-not $Uninstall) {
    $game = Get-GameDir
    Assert-GameNotRunning
    Assert-KnownDllOk $Shim

    $shimSrc = Join-Path $out "$Shim.dll"
    $coreSrc = Join-Path $out 'aamod_core.dll'
    foreach ($f in @($shimSrc, $coreSrc)) {
        if (-not (Test-Path $f)) { throw "$f is missing - run .\build.ps1 first." }
    }

    $realDll = Join-Path $env:SystemRoot "System32\$Shim.dll"
    if (-not (Test-Path $realDll)) { throw "$realDll not found." }

    $shimDst = Join-Path $game "$Shim.dll"
    $hookedDst = Join-Path $game "${Shim}Hooked.dll"
    $coreDst = Join-Path $game 'aamod_core.dll'
    $dataDir = Join-Path $game $dataDirName
    $manifestPath = Join-Path $dataDir $manifestName

    if ((Test-Path $shimDst) -and -not $Force) {
        throw "$shimDst already exists (another mod loader?). Use -Force to overwrite."
    }

    $exe = Join-Path $game $gameExeName
    $exeText = [System.IO.File]::ReadAllBytes($exe)
    $exeStr = [System.Text.Encoding]::ASCII.GetString($exeText)
    if ($exeStr -notmatch [regex]::Escape("$Shim.dll")) {
        Write-Warning "The game executable does not appear to import $Shim.dll; injection will not happen."
    }
    $exeText = $null

    Write-Plan @(
        "game directory : $game",
        "shim           : $Shim.dll  (proxy, forwards to ${Shim}Hooked.dll)",
        "files to add   : $Shim.dll, ${Shim}Hooked.dll, aamod_core.dll",
        "data directory : $dataDir  (config.ini, mods\, logs\)"
    )

    if (-not $PSCmdlet.ShouldProcess($game, "install aamod ($Shim shim)")) { return }

    New-Item -ItemType Directory -Force -Path (Join-Path $dataDir 'mods'),
        (Join-Path $dataDir 'logs') | Out-Null

    Copy-Item $shimSrc $shimDst -Force
    Copy-Item $realDll $hookedDst -Force
    Copy-Item $coreSrc $coreDst -Force

    $cfg = Join-Path $dataDir 'config.ini'
    if (-not (Test-Path $cfg)) {
        Set-Content -Path $cfg -Encoding utf8 -Value @'
; aamod configuration - one [section] per mod is optional but tidy
[general]
; extra mod directories, separated by ';'. Defaults:
;   <game dir>\aamod\mods      (this directory)
;   %LOCALAPPDATA%\aamod\mods
; mod_dirs=

[engine]
; Chowdren environment switches. 1/0, or true/false. Empty = leave untouched.
;   CHOWDREN_SHOW_DEBUGGER opens the engine debug console (needs a console
;   attached to the process, so it is only useful when started from a terminal)
show_debugger=0
'@
    }

    $files = @()
    foreach ($p in @($shimDst, $hookedDst, $coreDst)) {
        $files += [pscustomobject]@{
            path   = (Split-Path $p -Leaf)
            sha256 = (Get-FileHash $p -Algorithm SHA256).Hash
            bytes  = (Get-Item $p).Length
        }
    }
    $manifest = [pscustomobject]@{
        aamod       = '0.1.0-m0'
        installed   = (Get-Date).ToString('s')
        game_dir    = $game
        game_exe    = (Get-Item $exe).VersionInfo.FileVersion
        shim        = $Shim
        real_dll    = $realDll
        files       = $files
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -Path $manifestPath -Encoding utf8

    Write-Host "installed. start the game; the log appears at $dataDir\logs\aamod.log" -ForegroundColor Green
    Write-Host "uninstall with: .\install.ps1 -Uninstall" -ForegroundColor DarkGray
    return
}

# ---------------------------------------------------------------- uninstall
$game = Get-GameDir
Assert-GameNotRunning
$dataDir = Join-Path $game $dataDirName
$manifestPath = Join-Path $dataDir $manifestName
if (-not (Test-Path $manifestPath)) { throw "$manifestPath not found - nothing to uninstall." }
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json

$toRemove = @()
foreach ($f in $manifest.files) {
    $p = Join-Path $game $f.path
    if (-not (Test-Path $p)) { continue }
    $hash = (Get-FileHash $p -Algorithm SHA256).Hash
    if ($hash -ne $f.sha256) {
        Write-Warning "$($f.path) changed since installation (modded by hand?); leaving it in place."
        continue
    }
    $toRemove += $p
}

Write-Plan @(
    "game directory : $game",
    "installed      : $($manifest.installed)  (shim $($manifest.shim))",
    "files to remove: $(($toRemove | ForEach-Object { Split-Path $_ -Leaf }) -join ', ')",
    "data directory : $(if ($KeepData) { 'kept' } else { "removed: $dataDir (mods, logs, install.json)" })"
)

if (-not $PSCmdlet.ShouldProcess($game, 'uninstall aamod')) { return }

foreach ($p in $toRemove) { Remove-Item $p -Force }
if ($KeepData) {
    Remove-Item $manifestPath -Force
} else {
    Remove-Item $dataDir -Recurse -Force
}
Write-Host 'uninstalled.' -ForegroundColor Green
