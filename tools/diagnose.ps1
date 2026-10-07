#requires -Version 7.0
<# 只读自助诊断；输出不含完整路径、配置、存档、Steam ID 或原始日志。 #>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$GameDir,[Parameter(Mandatory)][string]$OutputPath)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputPath){throw '诊断输出已存在，未覆盖。'}
$game=(Resolve-Path -LiteralPath $GameDir).Path
$exe=Join-Path $game 'Astral Ascent.exe'
$expected='CA376D2B741F65C75C416317517D24C316DD8A32A4EAE6559030797B3B9AA88B'
$identity=if(Test-Path -LiteralPath $exe){(Get-FileHash -LiteralPath $exe).Hash}else{$null}
$records=@()
$installed=Join-Path $game 'aamod/install.json'
$manifest=if(Test-Path -LiteralPath $installed){Get-Content -LiteralPath $installed -Raw|ConvertFrom-Json}else{$null}
$allowed=@('aamod_core.dll','winmm.dll','winmmHooked.dll','d3d11.dll','d3d11Hooked.dll')
if($manifest){foreach($file in $manifest.files){
    if($file.path -notin $allowed){throw '安装清单含非框架路径，停止诊断。'}
    $path=Join-Path $game $file.path;$hash=if(Test-Path -LiteralPath $path){(Get-FileHash -LiteralPath $path).Hash}else{$null}
    $records+=@{name=$file.path;present=($null -ne $hash);sha256=$hash;matchesManifest=($hash -eq $file.sha256)}
}}
$mods=Join-Path $game 'aamod/mods';$plugins=@()
if(Test-Path -LiteralPath $mods){foreach($folder in Get-ChildItem -LiteralPath $mods -Directory){
    $path=Join-Path $folder.FullName 'mod.json'
    if(-not(Test-Path -LiteralPath $path)){continue}
    try{$m=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;$plugins+=@{id=$m.id;version=$m.version;enabled=($m.enabled -ne $false);apiVersion=$m.api_version;minApiSize=$m.min_api_size;requiredCapabilities=$m.required_capabilities}}
    catch{$plugins+=@{id='unreadable-manifest';error='invalid JSON'}}
}}
$runtime=@('vcruntime140.dll','msvcp140.dll')|ForEach-Object {
    $path=Join-Path $env:SystemRoot "System32/$_"
    @{name=$_;present=(Test-Path -LiteralPath $path);version=if(Test-Path -LiteralPath $path){(Get-Item -LiteralPath $path).VersionInfo.FileVersion}else{$null}}
}
$result=[ordered]@{format=1;frameworkVersion=$manifest.aamod;frameworkVersionSource='install.json，不证明实际加载';shim=$manifest.shim;gameSha256=$identity;gameSupported=($identity -eq $expected);osVersion=[Environment]::OSVersion.Version.ToString();processArchitecture=[Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString();powerShell=$PSVersionTable.PSVersion.ToString();gameProcessCount=@(Get-Process -Name 'Astral Ascent' -ErrorAction SilentlyContinue).Count;visualCppRuntime=@($runtime);files=$records;plugins=$plugins;limits='只核对标准安装清单；自定义 AAMOD_DATA_DIR 与加载成功须另行核对。'}
$result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $OutputPath -Encoding utf8NoBOM
Write-Output "已生成只读诊断：$OutputPath。分享前仍请检查插件名称等内容。"
