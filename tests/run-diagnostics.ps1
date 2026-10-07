#requires -Version 7.0
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$root=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('aamod-diagnose-'+[guid]::NewGuid().ToString('N'))))
try {
    $mods=Join-Path $root 'aamod/mods/private'
    New-Item -ItemType Directory -Path $mods -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $repo 'out/sdk-host.exe') -Destination (Join-Path $root 'Astral Ascent.exe')
    Set-Content -LiteralPath (Join-Path $root 'aamod_core.dll') -Value 'fixture'
    $hash=(Get-FileHash -LiteralPath (Join-Path $root 'aamod_core.dll')).Hash
    @{aamod='fixture';shim='winmm';files=@(@{path='aamod_core.dll';sha256=$hash},@{path='winmm.dll';sha256=$hash})}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $root 'aamod/install.json') -Encoding utf8NoBOM
    Set-Content (Join-Path $mods 'mod.json') -Value '{"id":"test_plugin","entry":"test.dll","enabled":false}' -Encoding utf8NoBOM
    & (Join-Path $repo 'tools/diagnose.ps1') -GameDir $root -OutputPath (Join-Path $root 'first.json')|Out-Null
    $text=Get-Content (Join-Path $root 'first.json') -Raw;$result=$text|ConvertFrom-Json
    if($result.gameSupported -or -not $result.files[0].matchesManifest -or $result.files[1].present -or $result.plugins[0].enabled){throw '正常、未知宿主、缺失文件或禁用插件诊断不符'}
    if($text.Contains($root) -or $text.Contains($env:USERPROFILE) -or $text.Contains('game_dir')){throw '诊断泄露完整路径'}
    Set-Content (Join-Path $root 'aamod_core.dll') -Value 'drift'
    & (Join-Path $repo 'tools/diagnose.ps1') -GameDir $root -OutputPath (Join-Path $root 'second.json')|Out-Null
    if((Get-Content (Join-Path $root 'second.json') -Raw|ConvertFrom-Json).files[0].matchesManifest){throw '未识别安装漂移'}
    $rejected=$false;try{& (Join-Path $repo 'tools/diagnose.ps1') -GameDir $root -OutputPath (Join-Path $root 'second.json')}catch{$rejected=$true}
    if(-not $rejected){throw '覆盖已有诊断'}
    @{files=@(@{path='../outside.dll';sha256=$hash})}|ConvertTo-Json -Depth 3|Set-Content (Join-Path $root 'aamod/install.json') -Encoding utf8NoBOM
    $rejected=$false;try{& (Join-Path $repo 'tools/diagnose.ps1') -GameDir $root -OutputPath (Join-Path $root 'escape.json')}catch{$rejected=$true}
    if(-not $rejected -or (Test-Path (Join-Path $root 'escape.json'))){throw '越界清单未拒绝'}
    Write-Output 'PASS: 只读诊断、未知宿主、漂移/缺失、禁用插件、路径脱敏、拒绝覆盖和越界清单。'
} finally {
    if(Test-Path -LiteralPath $root){
        if((Resolve-Path -LiteralPath $root).Path -ne $root -or -not $root.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase)){throw '清理路径异常'}
        if(@(Get-ChildItem -LiteralPath $root -Force -Recurse -Attributes ReparsePoint).Count){throw '临时目录含链接'}
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
