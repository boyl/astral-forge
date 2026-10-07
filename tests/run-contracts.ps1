#requires -Version 7.0
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'tests\failing_mod')
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'mods\template')
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'mods\diagnostics')
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'mods\state_watch')
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'mods\event_watch')
& (Join-Path $root 'tools\build-mod.ps1') -ModDirectory (Join-Path $root 'mods\image_sample')
foreach($name in @('data_sample','equipment_watch')) {
    & (Join-Path $root 'tools/build-mod.ps1') -ModDirectory (Join-Path $root "mods/$name")
}
foreach($name in @('legacy128','legacy208','guard_mod')) {
    & (Join-Path $root 'tools/build-mod.ps1') -ModDirectory (Join-Path $root "tests/$name")
}
$testDir = Join-Path $root 'out\test'
foreach($name in @('data_sample','equipment_watch')) {
    $destination=Join-Path $testDir "aamod/mods/$name"
    New-Item -ItemType Directory -Path $destination -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $root "mods/$name/$name.dll"),(Join-Path $root "mods/$name/mod.json") -Destination $destination -Force
}
$diagnosticDir = Join-Path $testDir 'aamod\mods\diagnostics'
New-Item -ItemType Directory -Path $diagnosticDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'mods\diagnostics\diagnostics.dll'),(Join-Path $root 'mods\diagnostics\mod.json') -Destination $diagnosticDir -Force
$watchDir = Join-Path $testDir 'aamod\mods\state_watch'
New-Item -ItemType Directory -Path $watchDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'mods\state_watch\state_watch.dll'),(Join-Path $root 'mods\state_watch\mod.json') -Destination $watchDir -Force
$eventDir = Join-Path $testDir 'aamod\mods\event_watch'
New-Item -ItemType Directory -Path $eventDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'mods\event_watch\event_watch.dll'),(Join-Path $root 'mods\event_watch\mod.json') -Destination $eventDir -Force
$imageDir = Join-Path $testDir 'aamod\mods\image_sample'
New-Item -ItemType Directory -Path $imageDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'mods\image_sample\image_sample.dll'),(Join-Path $root 'mods\image_sample\mod.json') -Destination $imageDir -Force
Copy-Item -LiteralPath (Join-Path $root 'mods\image_sample\assets') -Destination $imageDir -Recurse -Force
$failingDir = Join-Path $testDir 'aamod\mods\failing_mod'
$unicodeDir = Join-Path $testDir 'aamod\mods\中文插件'
$invalidDir = Join-Path $testDir 'aamod\mods\invalid_entry'
foreach ($dir in @($failingDir,$unicodeDir,$invalidDir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
Copy-Item -LiteralPath (Join-Path $root 'tests\failing_mod\failing_mod.dll'),(Join-Path $root 'tests\failing_mod\mod.json') -Destination $failingDir -Force
Copy-Item -LiteralPath (Join-Path $root 'mods\image_sample\assets') -Destination $failingDir -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'mods\template\template.dll') -Destination (Join-Path $unicodeDir '中文.dll') -Force
@{id='unicode';entry='中文.dll';enabled=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $unicodeDir 'mod.json') -Encoding utf8NoBOM
@{id='invalid_entry';entry='..\hello\hello.dll'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $invalidDir 'mod.json') -Encoding utf8NoBOM
foreach($name in @('legacy128','legacy208')) {
    $destination=Join-Path $testDir "aamod/mods/$name"
    New-Item -ItemType Directory -Path $destination -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $root "tests/$name/$name.dll"),(Join-Path $root "tests/$name/mod.json") -Destination $destination -Force
}
foreach($case in @(@{id='bad_abi';api_version=2},@{id='bad_size';min_api_size=329},@{id='bad_caps';required_capabilities=1024})) {
    $destination=Join-Path $testDir "aamod/mods/$($case.id)"
    New-Item -ItemType Directory -Path $destination -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'tests/guard_mod/guard_mod.dll') -Destination $destination -Force
    $case.entry='guard_mod.dll';$case|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $destination 'mod.json') -Encoding utf8NoBOM
}
function Run-Host([string]$mode,[string]$throwing,[string[]]$arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $testDir 'host.exe'))
    $info.WorkingDirectory = $testDir
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.Environment['AAMOD_RENDER'] = $mode
    $info.Environment['AAMOD_TEST_THROW_INIT'] = $throwing
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(30000)) { $process.Kill(); $process.Dispose(); throw '契约 host 超时。' }
    $exitCode = $process.ExitCode
    $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    $process.Dispose()
    if ($exitCode -ne 0 -or $text -notmatch 'RESULT[=:]OK') { throw "契约 host 失败：$text" }
    return $text
}
$logPath = Join-Path $testDir 'aamod\logs\aamod.log'
$startLength = (Get-Item -LiteralPath $logPath).Length
$result = Run-Host '0' '' @('--loader-only')
if ($result -notmatch 'loader-only state=0') { throw '纯加载模式启用了图形挂钩。' }
$result = Run-Host '1' '' @()
if ($result -notmatch 'CreateSwapChainForHwnd = 0x00000000') { throw 'HWND 接口回归失败。' }
$result = Run-Host '1' '1' @()
$bytes = [IO.File]::ReadAllBytes($logPath)
$text = [Text.Encoding]::UTF8.GetString($bytes, [int]$startLength, $bytes.Length - [int]$startLength)
foreach ($pattern in @('failing_mod: registered hook and frame; intentionally failing',
                      'AAMOD_Init raised exception 0xe0000001',
                      "mods: 'unicode' initialised", 'entry must be a DLL filename inside the mod directory')) {
    if ($text -notmatch $pattern) { throw "未观察到契约结果：$pattern" }
}
if ($text -match 'FAILED_MOD_CALLBACK_MUST_NOT_RUN') { throw '失败插件仍收到帧回调。' }
if ($text -notmatch 'diagnostics: identity=0 profile= sha256=[0-9a-f]{64}' -or $text -notmatch 'template: available services=2791') { throw '游戏身份查询或能力声明失败。' }
$hostHash = (Get-FileHash -LiteralPath (Join-Path $testDir 'host.exe')).Hash.ToLowerInvariant()
if ($text -notmatch "mods: 'state_watch' initialised" -or $text -match 'game state: attached native') { throw '未知宿主上的示例加载或适配禁用失败。' }
if ($text -notmatch "mods: 'event_watch' initialised" -or $text -match 'event_watch: query failed') { throw '事件示例加载或关闭契约失败。' }
if ($text -notmatch 'image_sample: RGBA8 width=32 height=32 stride=128 bytes=4096 first=0,180,255,128' -or
    $text -notmatch 'image_sample: release=0' -or $text -notmatch 'failing_mod: owned image loaded') { throw '独立 PNG 示例或失败插件资源回滚未验证。' }
if ($text -notmatch ('diagnostics: identity=0 profile= sha256='+$hostHash)) { throw '运行时指纹与独立 Get-FileHash 不一致。' }
foreach($name in @('legacy128','legacy208')) {
    if($text -notmatch "$name`: frozen SDK init OK, core size=328" -or $text -notmatch "$name`: shutdown OK"){throw "冻结旧 SDK 二进制契约未验证：$name"}
}
foreach($name in @('bad_abi','bad_size','bad_caps')) {
    if($text -notmatch "mods: '$name' incompatible before DLL load" -or (Test-Path -LiteralPath (Join-Path $testDir "aamod/mods/$name/executed.txt"))){throw "兼容拒绝发生得太晚：$name"}
}
if($text -match 'GUARD_INIT_MUST_NOT_RUN'){throw '不兼容插件仍执行初始化。'}
if($text -notmatch 'data_sample: read own preset bytes=' -or $text -notmatch 'data_sample: shutdown data write=0'){throw '公共数据示例读取与关闭保存未验证。'}
if($text -notmatch 'equipment_watch: status=0 valid=0'){throw '未知宿主装备适配未禁用。'}
Write-Output 'PASS: 加载/渲染、失败回滚、冻结128/208字节SDK、ABI/尺寸/能力在DLL加载前拒绝、UTF-8路径和重复关闭资源归零。'

