#requires -Version 7.0
<# 从交付包独立重建与加载；只共享已安装的 Windows/MSVC/Python，不访问游戏或原工作树。 #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$ManifestPath,[Parameter(Mandatory)][string]$PythonPath,[Parameter(Mandatory)][string]$ReportDirectory)
$ErrorActionPreference='Stop'
$manifestFile=(Resolve-Path -LiteralPath $ManifestPath).Path
$manifest=Get-Content -LiteralPath $manifestFile -Raw|ConvertFrom-Json
$report=[IO.Path]::GetFullPath($ReportDirectory)
if(Test-Path -LiteralPath $report){throw '报告目录已存在，未覆盖。'}
New-Item -ItemType Directory -Path $report|Out-Null
$root=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('aamod-package-'+[guid]::NewGuid().ToString('N'))))
$pwsh=Join-Path $PSHOME 'pwsh.exe'
$steps=@();$passed=$false
function Run-Clean([string]$Name,[string]$Script,[string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new($pwsh)
    $start.WorkingDirectory=$root;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($key in @($start.Environment.Keys)){
        if($key -like 'AAMOD_*' -or $key -like 'VSCMD*' -or $key -like 'VSINSTALL*' -or $key -like 'VCINSTALL*' -or $key -like 'VCTools*' -or $key -in @('INCLUDE','LIB','LIBPATH')){$start.Environment.Remove($key)|Out-Null}
    }
    $start.Environment['PYTHONIOENCODING']='utf-8'
    foreach($arg in @('-NoLogo','-NoProfile','-File',$Script)+$Arguments){$start.ArgumentList.Add($arg)}
    $process=[Diagnostics.Process]::Start($start)
    try {
        $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
        if(-not $process.WaitForExit(180000)){$process.Kill($true);$process.WaitForExit();throw "$Name 超时"}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        Set-Content -LiteralPath (Join-Path $report "$Name.log") -Value $text -Encoding utf8NoBOM
        if($process.ExitCode){throw "$Name 失败；见独立报告日志"}
    } finally {$process.Dispose()}
    $script:steps+=@{name=$Name;passed=$true}
}
try {
    & $PythonPath (Join-Path (Split-Path $PSScriptRoot) 'tools/audit-preview.py') $manifestFile
    if($LASTEXITCODE){throw '归档审计失败'}
    & $PythonPath (Join-Path $PSScriptRoot 'preview-audit-test.py') $manifestFile
    if($LASTEXITCODE){throw '归档破坏测试失败'}
    New-Item -ItemType Directory -Path $root|Out-Null
    foreach($kind in @('source','runtime')) {
        $archive=($manifest.archives|Where-Object file -like "*-$kind.zip").file
        Expand-Archive -LiteralPath (Join-Path (Split-Path $manifestFile) $archive) -DestinationPath (Join-Path $root $kind)
    }
    $source=Join-Path $root 'source';$runtime=Join-Path $root 'runtime'
    Run-Clean 'source-build' (Join-Path $source 'build.ps1') @('-Test','-PythonPath',$PythonPath)
    Run-Clean 'source-contracts' (Join-Path $source 'tests/run-contracts.ps1') @()
    Run-Clean 'source-installer' (Join-Path $source 'tests/run-installer.ps1') @()
    Run-Clean 'source-diagnostics' (Join-Path $source 'tests/run-diagnostics.ps1') @()
    foreach($name in @('hello','template','diagnostics','state_watch','event_watch','heal_once','image_sample','native_image_sample','data_sample','equipment_watch','content_smoke','build_selector','native_event_watch','event_responder')) {
        Run-Clean "sdk-build-$name" (Join-Path $runtime 'tools/build-mod.ps1') @('-ModDirectory',(Join-Path $runtime "mods/$name"))
    }
    foreach($name in @('template','diagnostics','state_watch','event_watch','image_sample')) {
        Run-Clean "sdk-load-$name" (Join-Path $runtime 'tools/test-mod.ps1') @('-ModDirectory',(Join-Path $runtime "mods/$name"),'-ReportPath',(Join-Path $report "$name.json"))
    }
    $fake=Join-Path $root '模拟游戏';New-Item -ItemType Directory -Path $fake|Out-Null
    Copy-Item -LiteralPath (Join-Path $runtime 'out/sdk-host.exe') -Destination (Join-Path $fake 'Astral Ascent.exe')
    Run-Clean 'runtime-install' (Join-Path $runtime 'install.ps1') @('-GameDir',$fake)
    $mods=Join-Path $fake 'aamod/mods'
    Copy-Item -LiteralPath (Join-Path $runtime 'mods/template') -Destination $mods -Recurse
    $start=[Diagnostics.ProcessStartInfo]::new((Join-Path $fake 'Astral Ascent.exe'))
    $start.WorkingDirectory=$fake;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    foreach($key in @($start.Environment.Keys)){if($key -like 'AAMOD_*'){$start.Environment.Remove($key)|Out-Null}}
    $start.Environment['AAMOD_RENDER']='0';$start.Environment['AAMOD_EXPERIMENTAL_COMMANDS']='0'
    $p=[Diagnostics.Process]::Start($start)
    try {
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
        if(-not $p.WaitForExit(30000)){$p.Kill($true);$p.WaitForExit();throw '安装后宿主超时'}
        $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()
        if($p.ExitCode -or $text -notmatch 'RESULT=OK'){throw '安装后加载失败'}
        if((Get-Content (Join-Path $fake 'aamod/logs/aamod.log') -Raw) -notmatch "mods: 'template' initialised"){throw '安装后插件未加载'}
    } finally {$p.Dispose()}
    $steps+=@{name='runtime-installed-loader';passed=$true}
    Run-Clean 'diagnostic' (Join-Path $runtime 'tools/diagnose.ps1') @('-GameDir',$fake,'-OutputPath',(Join-Path $report 'diagnostic.json'))
    $diagnostic=Get-Content (Join-Path $report 'diagnostic.json') -Raw|ConvertFrom-Json
    if($diagnostic.gameSupported -or $diagnostic.files.matchesManifest -contains $false){throw '诊断未正确区分未知宿主或安装漂移'}
    Run-Clean 'runtime-uninstall' (Join-Path $runtime 'install.ps1') @('-GameDir',$fake,'-Uninstall')
    if(Test-Path (Join-Path $fake 'winmm.dll')){throw '卸载残留框架入口'}
    if(-not(Test-Path (Join-Path $fake 'aamod/mods/template/template.dll'))){throw '卸载删除用户插件'}
    $passed=$true
} finally {
    @{passed=$passed;version=$manifest.version;sourceSnapshotSha256=$manifest.sourceSnapshotSha256;steps=$steps;scope='同一Windows主机的独立解压目录及新pwsh进程；不等同于另一台机器或实机验收'}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $report 'result.json') -Encoding utf8NoBOM
    if(Test-Path -LiteralPath $root){
        if((Resolve-Path -LiteralPath $root).Path -ne $root -or -not $root.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)){throw '清理路径异常'}
        if(@(Get-ChildItem -LiteralPath $root -Force -Recurse -Attributes ReparsePoint).Count){throw '临时目录含链接，保留检查'}
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
Write-Output 'PASS: 独立源码重建、SDK示例编译/加载、运行包安装/诊断/卸载。'

