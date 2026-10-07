#requires -Version 7.0
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$fake=Join-Path $root ('out\install-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fake | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'out\host.exe') -Destination (Join-Path $fake 'Astral Ascent.exe')
$data=Join-Path $fake 'aamod'
New-Item -ItemType Directory -Path (Join-Path $data 'mods\personal') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $data 'config.ini') -Value 'personal configuration' -Encoding utf8NoBOM
Set-Content -LiteralPath (Join-Path $data 'mods\personal\keep.txt') -Value 'personal plugin' -Encoding utf8NoBOM
$installer=Join-Path $root 'install.ps1'
& $installer -GameDir $fake -WhatIf
if(Test-Path -LiteralPath (Join-Path $fake 'winmm.dll')){throw 'WhatIf 写入了文件。'}
Set-Content -LiteralPath (Join-Path $fake 'winmm.dll') -Value 'foreign loader'
$foreignHash=(Get-FileHash -LiteralPath (Join-Path $fake 'winmm.dll')).Hash
$rejected=$false
try {& $installer -GameDir $fake} catch {$rejected=$true}
if(-not $rejected -or (Get-FileHash -LiteralPath (Join-Path $fake 'winmm.dll')).Hash -ne $foreignHash){throw '未知加载器未受保护。'}
Remove-Item -LiteralPath (Join-Path $fake 'winmm.dll')
& $installer -GameDir $fake
Set-Content -LiteralPath (Join-Path $fake 'winmm.dll') -Value 'old owned loader'
$manifest=Get-Content -LiteralPath (Join-Path $data 'install.json') -Raw | ConvertFrom-Json
($manifest.files | Where-Object path -eq 'winmm.dll').sha256=(Get-FileHash -LiteralPath (Join-Path $fake 'winmm.dll')).Hash
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $data 'install.json') -Encoding utf8NoBOM
$beforeHash=(Get-FileHash -LiteralPath (Join-Path $fake 'winmm.dll')).Hash
$locked=[IO.File]::Open((Join-Path $fake 'aamod_core.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$rejected=$false
try {& $installer -GameDir $fake} catch {$rejected=$true} finally {$locked.Dispose()}
if(-not $rejected -or (Get-FileHash -LiteralPath (Join-Path $fake 'winmm.dll')).Hash -ne $beforeHash){throw '部分安装失败没有恢复旧加载器。'}
& $installer -GameDir $fake
$manifest=Get-Content -LiteralPath (Join-Path $data 'install.json') -Raw | ConvertFrom-Json
if($manifest.aamod -ne (Get-Content -LiteralPath (Join-Path $root 'out\build.json') -Raw | ConvertFrom-Json).version){throw '版本记录错误。'}
& $installer -GameDir $fake
Set-Content -LiteralPath (Join-Path $fake 'aamod_core.dll') -Value 'drifted'
$rejected=$false
try {& $installer -GameDir $fake -Uninstall} catch {$rejected=$true}
if(-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $fake 'winmm.dll'))){throw '漂移清单导致部分卸载。'}
Copy-Item -LiteralPath (Join-Path $root 'out\aamod_core.dll') -Destination (Join-Path $fake 'aamod_core.dll') -Force
$locked=[IO.File]::Open((Join-Path $fake 'aamod_core.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$rejected=$false
try {& $installer -GameDir $fake -Uninstall} catch {$rejected=$true} finally {$locked.Dispose()}
if(-not $rejected -or -not (Test-Path -LiteralPath (Join-Path $fake 'winmm.dll')) -or -not (Test-Path -LiteralPath (Join-Path $data 'install.json'))){throw '部分卸载失败没有恢复。'}
& $installer -GameDir $fake -Uninstall
if(Test-Path -LiteralPath (Join-Path $fake 'aamod_core.dll')){throw '核心未卸载。'}
if((Get-Content -LiteralPath (Join-Path $data 'config.ini') -Raw).Trim() -ne 'personal configuration' -or -not (Test-Path -LiteralPath (Join-Path $data 'mods\personal\keep.txt'))){throw '用户数据被修改。'}
Write-Output 'PASS: WhatIf、未知加载器拒绝、安装/升级校验、部分安装与卸载失败回滚、漂移卸载拒绝、用户插件与配置保留。'
if ((Split-Path (Resolve-Path -LiteralPath $fake).Path) -ne [IO.Path]::GetFullPath((Join-Path $root 'out')) -or
    (Split-Path $fake -Leaf) -notlike 'install-test-*') { throw '安装测试清理路径异常。' }
Remove-Item -LiteralPath $fake -Recurse -Force
