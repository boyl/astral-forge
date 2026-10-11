#requires -Version 7.0
[CmdletBinding()]
param([string]$PythonPath=$env:AAMOD_PYTHON)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
if(-not $PythonPath){$PythonPath=(Get-Command python -ErrorAction Stop).Source}
& (Join-Path $root 'tools/build-maintenance.ps1')
foreach($test in @('maintenance-test.py','maintenance-snapshot-test.py','maintenance-cli-test.py')){
    & $PythonPath (Join-Path $PSScriptRoot $test)
    if($LASTEXITCODE){throw "维护契约测试失败：$test"}
}
Write-Output 'PASS: 更新维护工具离线与 Windows 多进程契约；未执行游戏玩法。'
