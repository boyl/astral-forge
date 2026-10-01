<#
.SYNOPSIS
    Builds aamod (core loader, proxy shims, test host, sample mod) with MSVC.

.DESCRIPTION
    No CMake, no vcpkg, no third-party dependencies: a generated cmd script
    calls vcvars64.bat and cl.exe / ml64.exe / link.exe directly. Artefacts land
    in out\:
        aamod_core.dll   the loader core (game-agnostic)
        winmm.dll        proxy shim, statically imported by the game
        version.dll      proxy shim (alternative injection point)
        d3d11.dll        proxy shim (alternative; also our overlay handle)
    With -Test the script stages out\test\ as a fake game directory (shim +
    a copy of the real winmm renamed winmmHooked.dll + core + a sample mod) and
    runs tests\host\host.exe inside it, proving the injection path offline
    without launching the game.

.EXAMPLE
    & .\build.ps1 -Test
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$Test,
    [switch]$SkipDef,
    [string[]]$Shims = @('winmm', 'version', 'd3d11')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'out'
$obj = Join-Path $out 'obj'
$testDir = Join-Path $out 'test'
$py = 'C:\Users\lw\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'

function Find-VcVars {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $p = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($p) {
            $c = Join-Path $p.Trim() 'VC\Auxiliary\Build\vcvars64.bat'
            if (Test-Path $c) { return $c }
        }
    }
    throw 'vcvars64.bat not found. Install Visual Studio 2022 Build Tools with the C++ workload.'
}

if ($Clean) {
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
}
foreach ($d in @($out, $obj, (Join-Path $obj 'shim'), (Join-Path $obj 'host'),
                 (Join-Path $obj 'hello'), $testDir)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

# ---------------------------------------------------------- generated sources
if (-not $SkipDef) {
    Write-Host '[build] generating shim sources from the system DLL export tables' -ForegroundColor Cyan
    & $py (Join-Path $root 'tools\gen_shim_def.py') @Shims
    if ($LASTEXITCODE -ne 0) { throw 'gen_shim_def.py failed' }
}

# ---------------------------------------------------------- build script
$vcvars = Find-VcVars
Write-Host "[build] vcvars: $vcvars" -ForegroundColor DarkGray

$common = '/nologo /std:c++17 /EHsc /O2 /MD /W3 /DNDEBUG /I"' + (Join-Path $root 'include') + '"'

$lines = @(
    '@echo off',
    'setlocal',
    ('call "{0}" >nul' -f $vcvars),
    'if errorlevel 1 exit /b 1',
    'echo === core (aamod_core.dll) ===',
    ('cl {0} /LD /Fe:"{1}\aamod_core.dll" /Fo:"{2}\\" /Fd:"{2}\core.pdb" "{3}\src\core\*.cpp" /link /LIBPATH:"{1}" shell32.lib ole32.lib version.lib user32.lib advapi32.lib' -f $common, $out, $obj, $root),
    'if errorlevel 1 exit /b 1'
)

foreach ($shim in $Shims) {
    $sdir = Join-Path $root "src\shim\$shim"
    $lines += @(
        ('echo === shim {0}.dll (proxy, {1} stubs) ===' -f $shim, $shim),
        ('ml64 /nologo /c /Fo "{0}\shim\{1}_stubs.obj" "{2}\{1}_stubs.asm"' -f $obj, $shim, $sdir),
        'if errorlevel 1 exit /b 1',
        ('cl {0} /D AAMOD_SHIM_CONFIG_H=\"{1}_config.h\" /I"{2}" /LD /Fe:"{3}\{1}.dll" /Fo:"{4}\shim\\" /Fd:"{4}\shim_{1}.pdb" "{5}\src\shim\shim_common.cpp" "{2}\{1}_table.cpp" "{4}\shim\{1}_stubs.obj" /link /DEF:"{2}\{1}.def" /LIBPATH:"{3}" kernel32.lib' -f $common, $shim, $sdir, $out, $obj, $root),
        'if errorlevel 1 exit /b 1'
    )
}

$lines += @(
    'echo === test host ===',
    ('cl {0} /Fe:"{1}\host.exe" /Fo:"{2}\host\\" /Fd:"{2}\host.pdb" "{3}\tests\host\host.cpp" /link winmm.lib user32.lib' -f $common, $out, $obj, $root),
    'if errorlevel 1 exit /b 1',
    'echo === hello sample mod ===',
    ('cl {0} /LD /Fe:"{1}\hello.dll" /Fo:"{2}\hello\\" /Fd:"{2}\hello.pdb" "{3}\mods\hello\hello.cpp"' -f $common, $out, $obj, $root),
    'if errorlevel 1 exit /b 1',
    'echo === build ok ===',
    'endlocal',
    'exit /b 0'
)
$cmdPath = Join-Path $out 'build.cmd'
Set-Content -Path $cmdPath -Value ($lines -join "`r`n") -Encoding ascii

Write-Host '[build] compiling' -ForegroundColor Cyan
& cmd.exe /c $cmdPath
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

Write-Host '[build] artefacts:' -ForegroundColor Green
Get-ChildItem $out -File | Where-Object { $_.Extension -in @('.dll', '.exe') } |
    Sort-Object Name |
    ForEach-Object { Write-Host ("        {0,-20} {1,12:N0} bytes" -f $_.Name, $_.Length) }

if (-not $Test) { return }

# ---------------------------------------------------------- staging
Write-Host '[test] staging out\test as a fake game directory' -ForegroundColor Cyan
$modDir = Join-Path $testDir 'aamod\mods\hello'
foreach ($d in @($testDir, (Join-Path $testDir 'aamod'), (Join-Path $testDir 'aamod\mods'), $modDir,
                 (Join-Path $testDir 'aamod\logs'))) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Copy-Item (Join-Path $out 'winmm.dll')      $testDir -Force
Copy-Item (Join-Path $out 'aamod_core.dll') $testDir -Force
Copy-Item (Join-Path $out 'host.exe')       $testDir -Force
Copy-Item (Join-Path $out 'hello.dll')      $modDir  -Force
Copy-Item (Join-Path $root 'mods\hello\mod.json') $modDir -Force

# the real winmm, renamed: this is what the shim's jump table points at
$realWinmm = Join-Path $env:SystemRoot 'System32\winmm.dll'
Copy-Item $realWinmm (Join-Path $testDir 'winmmHooked.dll') -Force

$cfg = @'
; aamod test configuration
[hello]
greeting=hello from config.ini
; an engine anchor: the literal the resolver must map back to a function
anchor=host: anchor probe message %d
'@
Set-Content -Path (Join-Path $testDir 'aamod\config.ini') -Value $cfg -Encoding utf8

$log = Join-Path $testDir 'aamod\logs\aamod.log'
if (Test-Path $log) { Remove-Item $log -Force }

Write-Host '[test] running the host (whose winmm.dll resolves to our shim)' -ForegroundColor Cyan
$hostOut = & (Join-Path $testDir 'host.exe') 2>&1
$hostRc = $LASTEXITCODE
$hostOut | ForEach-Object { Write-Host "        $_" }

# ---------------------------------------------------------- assertions
Write-Host '[test] assertions' -ForegroundColor Cyan
$fails = @()
if ($hostRc -ne 0) { $fails += "host.exe exited $hostRc" }
if (-not (Test-Path $log)) {
    $fails += "core did not create $log"
} else {
    $text = Get-Content -Raw $log
    $expected = @(
        @{ p = 'aamod core .* starting';                n = 'core bootstrapped' },
        @{ p = 'mods\s+:\s+1 discovered';               n = '1 mod discovered' },
        @{ p = 'hello: AAMOD_Init';                     n = 'mod init called' },
        @{ p = 'hello: greeting=hello from config.ini'; n = 'config read through the API' },
        @{ p = 'hello: hook self-test 3/3 OK';           n = 'inline hook install/call/remove works on 3 prologues' },
        @{ p = 'hello: anchor -> [0-9A-Fa-f]{8,} \+\d+ bytes'; n = 'mod resolved an engine anchor through the API' },
        @{ p = 'mods\s+:\s+1 loaded';                   n = '1 mod loaded' },
        @{ p = 'aamod core ready';                      n = 'core reached ready state' }
    )
    foreach ($e in $expected) {
        if ($text -notmatch $e.p) { $fails += "log is missing: $($e.n)  (pattern: $($e.p))" }
    }
    Write-Host ("        log: {0} bytes, {1} lines" -f (Get-Item $log).Length,
                (Get-Content $log | Measure-Object).Count)
}

# host stdout assertions: the anchor resolver must map a message back to the
# function that prints it, and must refuse a literal that is not in the image.
$hostText = ($hostOut | Out-String)
$expectedOut = @(
    @{ p = 'host: anchor probe\s+=\s+[0-9A-F]+ \+[0-9]+, probe at [0-9A-F]+ -> inside'; n = 'anchor resolver found the probe function' },
    @{ p = 'host: anchor negative\s+=\s+not found';                                       n = 'anchor resolver rejected an absent literal' }
)
foreach ($e in $expectedOut) {
    if ($hostText -notmatch $e.p) { $fails += "host output is missing: $($e.n)  (pattern: $($e.p))" }
}

Write-Host '[test] export parity of every shim' -ForegroundColor Cyan
& $py (Join-Path $root 'tools\gen_shim_def.py') @Shims '--check'
if ($LASTEXITCODE -ne 0) { $fails += 'export parity check failed' }

Write-Host '[test] anchor tooling selftest (host.exe as ground truth)' -ForegroundColor Cyan
& $py (Join-Path $root 'tools\find_anchors.py') --selftest
if ($LASTEXITCODE -ne 0) { $fails += 'anchor tooling selftest failed' }

if ($fails.Count) {
    Write-Host ''
    Write-Host 'FAILED:' -ForegroundColor Red
    $fails | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
Write-Host ''
Write-Host 'ALL TESTS PASSED' -ForegroundColor Green
Write-Host "[test] full log: $log" -ForegroundColor DarkGray
