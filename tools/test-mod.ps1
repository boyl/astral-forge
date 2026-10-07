#requires -Version 7.0
<# 在独立目录验证公开 SDK 插件的加载与清理；不启动或修改游戏。 #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ModDirectory,[string]$ReportPath)
$ErrorActionPreference='Stop'
$sdkRoot=Split-Path $PSScriptRoot
$modRoot=(Resolve-Path -LiteralPath $ModDirectory).Path
$manifest=Get-Content -LiteralPath (Join-Path $modRoot 'mod.json') -Raw|ConvertFrom-Json
if(-not $manifest.id -or $manifest.entry -notmatch '^[^\\/:]+\.dll$'){throw '需要非空 id 与目录内 DLL entry。'}
if($manifest.enabled -eq $false){throw '插件已禁用。请在测试副本中启用后验收，不自动改变原清单。'}
$build=Get-Content -LiteralPath (Join-Path $sdkRoot 'out/build.json') -Raw|ConvertFrom-Json
foreach($name in @('aamod_core.dll','winmm.dll','sdk-host.exe')){
    if((Get-FileHash -LiteralPath (Join-Path $sdkRoot "out/$name")).Hash -ne $build.files.$name){throw "SDK 文件与构建清单不一致：$name"}
}
$root=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('aamod-sdk-'+[guid]::NewGuid().ToString('N'))))
$process=$null
try {
    $mods=Join-Path $root 'aamod/mods'
    New-Item -ItemType Directory -Path $mods -Force|Out-Null
    Copy-Item -LiteralPath $modRoot -Destination (Join-Path $mods 'candidate') -Recurse
    foreach($name in @('aamod_core.dll','winmm.dll','sdk-host.exe')){Copy-Item -LiteralPath (Join-Path $sdkRoot "out/$name") -Destination $root}
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32/winmm.dll') -Destination (Join-Path $root 'winmmHooked.dll')
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $root 'sdk-host.exe'))
    $start.WorkingDirectory=$root;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($key in @($start.Environment.Keys)){if($key -like 'AAMOD_*'){$start.Environment.Remove($key)|Out-Null}}
    $start.Environment['AAMOD_DATA_DIR']=Join-Path $root 'aamod'
    $start.Environment['AAMOD_RENDER']='0';$start.Environment['AAMOD_EXPERIMENTAL_COMMANDS']='0'
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(30000)){$process.Kill($true);$process.WaitForExit();throw '插件离线验收超时。'}
    $output=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
    if($process.ExitCode -ne 0 -or $output -notmatch 'RESULT=OK'){
        $code='{0:X8}' -f $process.ExitCode
        throw "SDK host 失败，退出码 0x$code：$output"
    }
    $log=Get-Content -LiteralPath (Join-Path $root 'aamod/logs/aamod.log') -Raw
    $escaped=[regex]::Escape($manifest.id)
    if($log -notmatch "mods: '$escaped' initialised"){throw '插件未成功初始化；请在游戏中另行验收所需原生能力，离线宿主不提供游戏适配。'}
    $result=[ordered]@{status='passed';version=$build.version;plugin=$manifest.id;pluginSha256=(Get-FileHash -LiteralPath (Join-Path $modRoot $manifest.entry)).Hash;coreSha256=$build.files.'aamod_core.dll';scope='独立宿主加载、初始化、重复关闭和资源清理；不证明游戏兼容'}
    if($ReportPath){$result|ConvertTo-Json|Set-Content -LiteralPath $ReportPath -Encoding utf8NoBOM}
    $result|ConvertTo-Json
} finally {
    if($process){if(-not $process.HasExited){$process.Kill($true);$process.WaitForExit()};$process.Dispose()}
    if(Test-Path -LiteralPath $root){
        $resolved=(Resolve-Path -LiteralPath $root).Path
        if($resolved -ne $root -or -not $resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)){throw '测试清理路径异常。'}
        if(@(Get-ChildItem -LiteralPath $root -Force -Recurse -Attributes ReparsePoint).Count){throw '测试目录含链接，保留检查。'}
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
