#requires -Version 7.0
<# 仅安装清单内文件；拒绝覆盖未知加载器，默认保留插件、配置和日志。 #>
[CmdletBinding(SupportsShouldProcess=$true)]
param([string]$GameDir,[ValidateSet('winmm','d3d11')][string]$Shim='winmm',[switch]$Uninstall)
$ErrorActionPreference='Stop'
if(-not $GameDir) {
    $steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
    $GameDir=Join-Path $steam 'steamapps\common\Astral Ascent'
}
$game=(Resolve-Path -LiteralPath $GameDir).Path
if(-not (Test-Path -LiteralPath (Join-Path $game 'Astral Ascent.exe'))) {throw '目标不是星界战士目录。'}
if(Get-Process -Name 'Astral Ascent' -ErrorAction SilentlyContinue) {throw '请先退出游戏。'}
$data=Join-Path $game 'aamod'
$manifestPath=Join-Path $data 'install.json'
$oldManifest=if(Test-Path -LiteralPath $manifestPath){Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json}else{$null}
$allowed=@('winmm.dll','winmmHooked.dll','d3d11.dll','d3d11Hooked.dll','aamod_core.dll')
if($oldManifest) {
    foreach($item in $oldManifest.files){
        if($item.path -notin $allowed){throw '安装清单包含非框架文件，未执行。'}
        $target=Join-Path $game $item.path
        if(Test-Path -LiteralPath $target){
            if((Get-FileHash -LiteralPath $target).Hash -ne $item.sha256){throw "已安装文件漂移：$($item.path)"}
        }
    }
}
if($Uninstall) {
    if(-not $oldManifest){throw '没有安装清单。'}
    if(-not $PSCmdlet.ShouldProcess($game,'卸载已核对哈希的框架文件，保留插件和配置')){return}
    $backup=Join-Path $game ('.install-backups\aamod-uninstall-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    foreach($item in $oldManifest.files){$target=Join-Path $game $item.path;if(Test-Path -LiteralPath $target){Copy-Item -LiteralPath $target -Destination (Join-Path $backup $item.path)}}
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backup 'install.json')
    try {
        foreach($item in $oldManifest.files){$target=Join-Path $game $item.path;if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}}
        Remove-Item -LiteralPath $manifestPath
    } catch {
        foreach($item in $oldManifest.files){$target=Join-Path $game $item.path;$saved=Join-Path $backup $item.path;if(-not (Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $saved)){Copy-Item -LiteralPath $saved -Destination $target}}
        if(-not (Test-Path -LiteralPath $manifestPath)){Copy-Item -LiteralPath (Join-Path $backup 'install.json') -Destination $manifestPath}
        throw
    }
    Write-Output '框架已卸载，插件、配置和日志保留。'
    return
}
if($oldManifest -and $oldManifest.shim -ne $Shim){throw '切换加载入口前，请先卸载原入口。'}
$out=Join-Path $PSScriptRoot 'out'
$build=Get-Content -LiteralPath (Join-Path $out 'build.json') -Raw | ConvertFrom-Json
$sources=@(@{Name="$Shim.dll";Path=(Join-Path $out "$Shim.dll")},
           @{Name="${Shim}Hooked.dll";Path=(Join-Path $env:SystemRoot "System32\$Shim.dll")},
           @{Name='aamod_core.dll';Path=(Join-Path $out 'aamod_core.dll')})
$items=@()
foreach($source in $sources){
    $hash=(Get-FileHash -LiteralPath $source.Path).Hash
    if($source.Name -notlike '*Hooked.dll' -and $build.files.($source.Name) -ne $hash){throw '构建文件与 build.json 不一致，请重新构建。'}
    $target=Join-Path $game $source.Name
    $exists=Test-Path -LiteralPath $target
    if($exists -and (-not $oldManifest -or $source.Name -notin @($oldManifest.files.path))){throw "拒绝覆盖未知文件：$($source.Name)"}
    $items+=@{Name=$source.Name;Source=$source.Path;Hash=$hash;Existed=$exists;OldHash=if($exists){(Get-FileHash -LiteralPath $target).Hash}else{$null};Written=$false}
}
if(-not $PSCmdlet.ShouldProcess($game,"安装 $($build.version)，入口 $Shim，默认关闭渲染挂钩")){return}
$backup=Join-Path $game ('.install-backups\aamod-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
foreach($item in $items){if($item.Existed){Copy-Item -LiteralPath (Join-Path $game $item.Name) -Destination (Join-Path $backup $item.Name)}}
if($oldManifest){Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backup 'install.json')}
$items | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'transaction.json') -Encoding utf8NoBOM
try {
    foreach($item in $items){
        $target=Join-Path $game $item.Name
        if($item.Existed -and (Get-FileHash -LiteralPath $target).Hash -ne $item.OldHash){throw '目标在备份后改变。'}
        $item.Written=$true
        Copy-Item -LiteralPath $item.Source -Destination $target -Force
        if((Get-FileHash -LiteralPath $target).Hash -ne $item.Hash){throw '安装哈希校验失败。'}
    }
    New-Item -ItemType Directory -Path (Join-Path $data 'mods'),(Join-Path $data 'logs') -Force | Out-Null
    $files=@($items | ForEach-Object {@{path=$_.Name;sha256=$_.Hash;bytes=(Get-Item -LiteralPath (Join-Path $game $_.Name)).Length}})
    @{aamod=$build.version;installed=(Get-Date).ToString('s');game_dir=$game;shim=$Shim;files=$files} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
} catch {
    foreach($item in $items){
        if(-not $item.Written){continue}
        $target=Join-Path $game $item.Name
        if($item.Existed){
            if(-not (Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne $item.OldHash){Copy-Item -LiteralPath (Join-Path $backup $item.Name) -Destination $target -Force}
        } elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
    }
    if($oldManifest){Copy-Item -LiteralPath (Join-Path $backup 'install.json') -Destination $manifestPath -Force}
    elseif(Test-Path -LiteralPath $manifestPath){Remove-Item -LiteralPath $manifestPath}
    throw
}
Write-Output "已安装并验证 $($build.version)。备份：$backup"
