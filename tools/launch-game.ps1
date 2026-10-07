#requires -Version 7.0
[CmdletBinding()]
param([string]$GameDir,[switch]$Render,[switch]$Commands,[switch]$GameplayEvents)
$ErrorActionPreference='Stop'
if(-not $GameDir){$steam=(Get-ItemProperty 'HKCU:\Software\Valve\Steam').SteamPath;$GameDir=Join-Path $steam 'steamapps\common\Astral Ascent'}
$game=(Resolve-Path -LiteralPath $GameDir).Path
if(Get-Process -Name 'Astral Ascent' -ErrorAction SilentlyContinue){throw '游戏已经在运行。'}
$start=[Diagnostics.ProcessStartInfo]::new((Join-Path $game 'Astral Ascent.exe'))
$start.WorkingDirectory=$game
$start.UseShellExecute=$false
$start.Environment['SteamAppId']='1280930'
$start.Environment['SteamGameId']='1280930'
$start.Environment['AAMOD_RENDER']=if($Render){'1'}else{'0'}
$start.Environment['AAMOD_EXPERIMENTAL_COMMANDS']=if($Commands){'1'}else{'0'}
$start.Environment['AAMOD_EXPERIMENTAL_EVENTS']=if($GameplayEvents){'1'}else{'0'}
$process=[Diagnostics.Process]::Start($start)
Write-Output "游戏启动请求已提交，PID=$($process.Id)。启动请求不等于运行验收。"
$process.Dispose()
