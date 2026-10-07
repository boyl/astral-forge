<#
.SYNOPSIS
    Builds aamod (core loader, proxy shims, test host, sample mod) with MSVC.

.DESCRIPTION
    通过 Visual Studio Developer PowerShell 初始化环境，直接调用 cl.exe 与 ml64.exe。 Artefacts land
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
    [string[]]$Shims = @('winmm', 'version', 'd3d11'),
    [string]$PythonPath = $env:AAMOD_PYTHON
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$out = Join-Path $root 'out'
$obj = Join-Path $out 'obj'
$testDir = Join-Path $out 'test'
$py = $PythonPath
if ($PSVersionTable.PSVersion.Major -lt 7) { throw '需要 PowerShell 7。' }
if (-not $py) {
    $pythonCommand = Get-Command python -ErrorAction SilentlyContinue
    if ($pythonCommand -and $pythonCommand.Source -notlike '*WindowsApps*') { $py = $pythonCommand.Source }
}
if (-not $py -or -not (Test-Path -LiteralPath $py -PathType Leaf)) { throw '请通过 -PythonPath 或 AAMOD_PYTHON 指定真实 Python 3.11+。' }
& $py -c 'import sys; assert sys.version_info >= (3,11), "Python 3.11+ required"'
if ($LASTEXITCODE) { throw '需要 Python 3.11+。' }
if ($Test -and ('winmm' -notin $Shims -or 'd3d11' -notin $Shims)) { throw '-Test 需要 winmm 和 d3d11。' }

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
    if (Test-Path $out) {
        if ((Resolve-Path -LiteralPath $out).Path -ne [IO.Path]::GetFullPath((Join-Path $root 'out'))) { throw '清理路径异常。' }
        Remove-Item -LiteralPath $out -Recurse -Force
    }
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

$installation = Split-Path (Split-Path (Split-Path (Split-Path $vcvars)))
$devShell = Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1'
& $devShell -VsInstallationPath $installation -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
function Invoke-Native {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
$nativeCommon = @('/nologo','/std:c++17','/EHsc','/O2','/MD','/W3','/DNDEBUG','/utf-8',('/I'+(Join-Path $root 'include')))
$sources = @(Get-ChildItem -LiteralPath (Join-Path $root 'src\core') -Filter '*.cpp' | ForEach-Object FullName)
Invoke-Native cl ($nativeCommon + @('/LD',('/Fe:'+ $out+'\aamod_core.dll'),('/Fo:'+ $obj+'\')) + $sources + @('/link','shell32.lib','ole32.lib','version.lib','user32.lib','advapi32.lib','bcrypt.lib','windowscodecs.lib'))
foreach ($shim in $Shims) {
    $shimDir = Join-Path $root ('src\shim\'+$shim)
    $stub = Join-Path $obj ('shim\'+$shim+'_stubs.obj')
    Invoke-Native ml64 @('/nologo','/c',('/Fo'+$stub),(Join-Path $shimDir ($shim+'_stubs.asm')))
    $sources = @((Join-Path $root 'src\shim\shim_common.cpp'),(Join-Path $shimDir ($shim+'_table.cpp')))
    if ($shim -eq 'd3d11') { $sources += Join-Path $shimDir 'd3d11_intercept.cpp' }
    Invoke-Native cl ($nativeCommon + @(('/DAAMOD_SHIM_CONFIG_H="'+$shim+'_config.h"'),('/I'+$shimDir),'/LD',('/Fe:'+ $out+'\'+$shim+'.dll'),('/Fo:'+ $obj+'\shim\')) + $sources + @($stub,'/link',('/DEF:'+ $shimDir+'\'+$shim+'.def'),'kernel32.lib'))
}
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\host.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\host\host.cpp'),'/link','winmm.lib','user32.lib'))
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\sdk-host.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\sdk_host.cpp'),'/link','winmm.lib'))
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\mod_contract_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\mod_contract_test.cpp'),(Join-Path $root 'src\core\mod_contract.cpp'),(Join-Path $root 'src\core\json_min.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'mod_contract_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\content_catalog_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\content_catalog_test.cpp'),(Join-Path $root 'src\core\log.cpp'),'/link','bcrypt.lib'))
if ($Test) { Invoke-Native (Join-Path $out 'content_catalog_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\equipment_commands_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\equipment_commands_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'equipment_commands_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\native_spell_abi_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\native_spell_abi_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'native_spell_abi_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\gameplay_events_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\gameplay_events_test.cpp'),(Join-Path $root 'src\core\gameplay_events.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'gameplay_events_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\native_events_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\native_events_test.cpp'),(Join-Path $root 'src\core\gameplay_events.cpp'),(Join-Path $root 'src\core\log.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'native_events_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\event_responder_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\event_responder_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'event_responder_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\menu_model_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\menu_model_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'menu_model_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\game_combat_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\game_combat_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'game_combat_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\game_equipment_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\game_equipment_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'game_equipment_test.exe') @() }
Invoke-Native cl ($nativeCommon + @('/LD',('/Fe:'+ $out+'\hello.dll'),('/Fo:'+ $obj+'\hello\'),(Join-Path $root 'mods\hello\hello.cpp')))
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\hook_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\hook_test.cpp'),(Join-Path $root 'src\core\log.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'hook_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\native_images_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\native_images_test.cpp'),(Join-Path $root 'src\core\log.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'native_images_test.exe') @() }
Invoke-Native cl @('/nologo','/utf-8','/std:c11','/TC','/c',('/I'+(Join-Path $root 'include')),('/Fo'+$obj+'\abi_c.obj'),(Join-Path $root 'tests\abi_c.c'))
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\game_profile_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\game_profile_test.cpp'),(Join-Path $root 'src\core\game_profile.cpp'),(Join-Path $root 'src\core\log.cpp'),'/link','bcrypt.lib'))
if ($Test) { Invoke-Native (Join-Path $out 'game_profile_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\game_state_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\game_state_test.cpp'),(Join-Path $root 'src\core\equipment_commands.cpp'),(Join-Path $root 'src\core\native_equipment.cpp'),(Join-Path $root 'src\core\content_catalog.cpp'),(Join-Path $root 'src\core\game_combat.cpp'),(Join-Path $root 'src\core\game_equipment.cpp'),(Join-Path $root 'src\core\game_commands.cpp'),(Join-Path $root 'src\core\game_profile.cpp'),(Join-Path $root 'src\core\log.cpp'),'/link','bcrypt.lib'))
if ($Test) { Invoke-Native (Join-Path $out 'game_state_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\game_commands_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\game_commands_test.cpp')))
if ($Test) { Invoke-Native (Join-Path $out 'game_commands_test.exe') @() }
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\plugin_data_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\plugin_data_test.cpp')))
if ($Test) {
    $dataFixtures=Join-Path $out ('data-fixtures-'+[guid]::NewGuid().ToString('N'))
    try { Invoke-Native (Join-Path $out 'plugin_data_test.exe') @($dataFixtures) }
    finally {
        $expected=[IO.Path]::GetFullPath($dataFixtures)
        if (-not $expected.StartsWith([IO.Path]::GetFullPath($out)+[IO.Path]::DirectorySeparatorChar)) { throw '数据测试清理路径异常。' }
        if(Test-Path -LiteralPath $expected) { Remove-Item -LiteralPath $expected -Recurse -Force }
    }
}
Invoke-Native cl ($nativeCommon + @(('/Fe:'+ $out+'\resources_test.exe'),('/Fo:'+ $obj+'\host\'),(Join-Path $root 'tests\resources_test.cpp'),(Join-Path $root 'src\core\log.cpp'),'/link','ole32.lib','windowscodecs.lib'))
if ($Test) {
    $resourceFixtures=Join-Path $out 'resource-fixtures'
    Invoke-Native $py @((Join-Path $root 'tests\create-resource-fixtures.py'),$resourceFixtures)
    $resourceLink=Join-Path $resourceFixtures '插件\escape'
    if (-not (Test-Path -LiteralPath $resourceLink)) { New-Item -ItemType Junction -Path $resourceLink -Target (Join-Path $resourceFixtures 'outside') | Out-Null }
    try { Invoke-Native (Join-Path $out 'resources_test.exe') @((Join-Path $resourceFixtures '插件')) }
    finally {
        if ((Resolve-Path -LiteralPath $resourceLink).Path -ne [IO.Path]::GetFullPath($resourceLink)) { throw '资源测试链接清理路径不符。' }
        Remove-Item -LiteralPath $resourceLink -Force
        if ((Resolve-Path -LiteralPath $resourceFixtures).Path -ne [IO.Path]::GetFullPath((Join-Path $out 'resource-fixtures'))) { throw '资源测试清理路径不符。' }
        Remove-Item -LiteralPath $resourceFixtures -Recurse -Force
    }
    Invoke-Native $py @((Join-Path $root 'tests\image-bank-test.py'))
    Invoke-Native $py @((Join-Path $root 'tests\resource-plan-test.py'))
}

Write-Host '[build] artefacts:' -ForegroundColor Green
Get-ChildItem $out -File | Where-Object { $_.Extension -in @('.dll', '.exe') } |
    Sort-Object Name |
    ForEach-Object { Write-Host ("        {0,-20} {1,12:N0} bytes" -f $_.Name, $_.Length) }

$versionSource = Get-Content -LiteralPath (Join-Path $root 'src\core\core.cpp') -Raw
$version = [regex]::Match($versionSource, '#define AAMOD_VERSION_STR "([^"]+)"').Groups[1].Value
$hashes = @{}
foreach ($name in @('aamod_core.dll','sdk-host.exe') + @($Shims | ForEach-Object {"$_.dll"})) {
    $hashes[$name] = (Get-FileHash -LiteralPath (Join-Path $out $name)).Hash
}
$compilerVersion=(Get-Item -LiteralPath (Get-Command cl).Source).VersionInfo.FileVersion
@{version=$version;files=$hashes;toolchain=@{msvc=$compilerVersion;windowsSdk=$env:WindowsSDKVersion;architecture='x64';python=(& $py -c 'import platform; print(platform.python_version())')}} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $out 'build.json') -Encoding utf8NoBOM
if (-not $Test) { return }

# ---------------------------------------------------------- staging
if (Test-Path -LiteralPath $testDir) {
    if ((Resolve-Path -LiteralPath $testDir).Path -ne [IO.Path]::GetFullPath((Join-Path $root 'out\test'))) { throw '测试清理路径异常。' }
    Remove-Item -LiteralPath $testDir -Recurse -Force
}
Write-Host '[test] staging out\test as a fake game directory' -ForegroundColor Cyan
$modDir = Join-Path $testDir 'aamod\mods\hello'
foreach ($d in @($testDir, (Join-Path $testDir 'aamod'), (Join-Path $testDir 'aamod\mods'), $modDir,
                 (Join-Path $testDir 'aamod\logs'))) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}
Copy-Item (Join-Path $out 'winmm.dll')      $testDir -Force
Copy-Item (Join-Path $out 'd3d11.dll')      $testDir -Force
Copy-Item (Join-Path $out 'aamod_core.dll') $testDir -Force
Copy-Item (Join-Path $out 'host.exe')       $testDir -Force
Copy-Item (Join-Path $out 'hello.dll')      $modDir  -Force
Copy-Item (Join-Path $root 'mods\hello\mod.json') $modDir -Force

# the real winmm, renamed: this is what the shim's jump table points at
$realWinmm = Join-Path $env:SystemRoot 'System32\winmm.dll'
Copy-Item $realWinmm (Join-Path $testDir 'winmmHooked.dll') -Force
# same for d3d11: the host imports d3d11.dll, so the proxy and the real DLL
# (renamed) must both be here for the present-hook test
$realD3d11 = Join-Path $env:SystemRoot 'System32\d3d11.dll'
Copy-Item $realD3d11 (Join-Path $testDir 'd3d11Hooked.dll') -Force

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
$startInfo = [Diagnostics.ProcessStartInfo]::new((Join-Path $testDir 'host.exe'))
$startInfo.WorkingDirectory = $testDir
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.Environment['AAMOD_RENDER'] = '1'
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$testProcess = [Diagnostics.Process]::Start($startInfo)
$stdoutTask = $testProcess.StandardOutput.ReadToEndAsync()
$stderrTask = $testProcess.StandardError.ReadToEndAsync()
if (-not $testProcess.WaitForExit(30000)) {
    $testProcess.Kill()
    $testProcess.WaitForExit()
    $testProcess.Dispose()
    throw '离线 host 测试超过 30 秒；检查 out/test/aamod/logs/aamod.log。'
}
$hostRc = $testProcess.ExitCode
$hostOut = ($stdoutTask.GetAwaiter().GetResult() + $stderrTask.GetAwaiter().GetResult()) -split '\r?\n'
$testProcess.Dispose()
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
        @{ p = 'hello: frame_subscribe = ok';           n = 'mod subscribed to present frames' },
        @{ p = 'hello: present frame 1 [1-9]\d*[xX][1-9]\d*'; n = 'mod got a frame callback through the API' },
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
    @{ p = 'host: anchor negative\s+=\s+not found';                                       n = 'anchor resolver rejected an absent literal' },
    @{ p = 'host: present hook state\s+=\s+3 after chain creation';                       n = 'factory and swap chain both hooked' },
    @{ p = 'host: present\s+0x0+, frames 0 -> 3';                                        n = 'three Present calls dispatched three frames' },
    @{ p = 'host: after detach\s+frames 3 -> 3, state 0';                                n = 'AAMOD_DetachPresent restored the vtable' }
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
