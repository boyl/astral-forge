#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path $PSScriptRoot
$build=Get-Content -LiteralPath (Join-Path $repoRoot 'out/build.json') -Raw|ConvertFrom-Json
$packageRoot=[IO.Path]::GetFullPath($OutputDirectory)
foreach($folder in @('src','include','mods','tests','tools','docs','schemas','.github')){
    $protected=[IO.Path]::GetFullPath((Join-Path $repoRoot $folder))
    if($packageRoot -eq $protected -or $packageRoot.StartsWith($protected+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw '交付目录不能位于源文件目录内'}
}
New-Item -ItemType Directory -Path $packageRoot -Force|Out-Null
$sourcePath=Join-Path $packageRoot ("aamod-$($build.version)-source.zip")
$runtimePath=Join-Path $packageRoot ("aamod-$($build.version)-runtime.zip")
$manifestPath=Join-Path $packageRoot ("aamod-$($build.version)-sha256.json")
foreach($path in @($sourcePath,$runtimePath,$manifestPath)){if(Test-Path -LiteralPath $path){throw "交付文件已存在，未覆盖：$path"}}
$required=@('aamod_core.dll','winmm.dll','d3d11.dll','sdk-host.exe')
foreach($name in $required){
    $file=Join-Path $repoRoot "out/$name"
    if((Get-FileHash -LiteralPath $file).Hash -ne $build.files.$name){throw "候选文件与构建清单不一致：$name"}
}
$examples=@('hello','template','diagnostics','state_watch','event_watch','heal_once','image_sample','native_image_sample','data_sample','equipment_watch','content_smoke','build_selector','native_event_watch','event_responder')
foreach($name in $examples){
    $folder=Join-Path $repoRoot "mods/$name"
    $mod=Get-Content -LiteralPath (Join-Path $folder 'mod.json') -Raw|ConvertFrom-Json
    if(-not(Test-Path -LiteralPath (Join-Path $folder $mod.entry))){throw "示例尚未编译：$name"}
}
function Write-Zip([string]$Destination,[array]$Items){
    $stream=[IO.File]::Open($Destination,[IO.FileMode]::CreateNew)
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
    try{
        foreach($item in $Items){
            $entry=$archive.CreateEntry($item.Name,[IO.Compression.CompressionLevel]::Optimal)
            $target=$entry.Open();$inputStream=[IO.File]::OpenRead($item.Path)
            try{$inputStream.CopyTo($target)}finally{$inputStream.Dispose();$target.Dispose()}
        }
    }finally{$archive.Dispose();$stream.Dispose()}
}
function Source-Items([string[]]$Folders){
    foreach($folder in $Folders){
        foreach($file in Get-ChildItem -LiteralPath (Join-Path $repoRoot $folder) -File -Recurse){
            $relative=[IO.Path]::GetRelativePath($repoRoot,$file.FullName).Replace('\','/')
            if($relative -match '(^|/)(\.build|__pycache__|\.git)(/|$)' -or $file.Extension -in '.dll','.exe','.lib','.exp','.obj','.pdb','.ilk','.pyc'){continue}
            @{Name=$relative;Path=$file.FullName}
        }
    }
}
$rootFiles=@('README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','CONTRIBUTING.md','SUPPORT.md','build.ps1','install.ps1','.gitignore')
$source=@(Source-Items @('src','include','mods','tests','tools','docs','schemas','.github'))
$source+=@($rootFiles|ForEach-Object {@{Name=$_;Path=(Join-Path $repoRoot $_)}})
$runtime=@($required|ForEach-Object {@{Name="out/$_";Path=(Join-Path $repoRoot "out/$_")}})
$runtime+=@{Name='out/build.json';Path=(Join-Path $repoRoot 'out/build.json')}
$runtime+=@($rootFiles|Where-Object {$_ -in 'README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','SUPPORT.md','install.ps1'}|ForEach-Object {@{Name=$_;Path=(Join-Path $repoRoot $_)}})
$runtime+=@(Source-Items @('include','docs','schemas'))
$runtime+=@{Name='licenses/nlohmann-json/LICENSE.MIT';Path=(Join-Path $repoRoot 'src/vendor/nlohmann/LICENSE.MIT')}
foreach($tool in @('launch-game.ps1','build-mod.ps1','test-mod.ps1','diagnose.ps1','audit-preview.py','create-image-fixture.py','image-bank-info.py','resource-plan.py')){
    $runtime+=@{Name="tools/$tool";Path=(Join-Path $repoRoot "tools/$tool")}
}
foreach($name in $examples){
    $folder=Join-Path $repoRoot "mods/$name";$mod=Get-Content (Join-Path $folder 'mod.json') -Raw|ConvertFrom-Json
    foreach($leaf in @('mod.json',$mod.entry)){$runtime+=@{Name="mods/$name/$leaf";Path=(Join-Path $folder $leaf)}}
    foreach($file in Get-ChildItem -LiteralPath $folder -File|Where-Object Extension -in '.cpp','.c','.h'){$runtime+=@{Name="mods/$name/$($file.Name)";Path=$file.FullName}}
    $assets=Join-Path $folder 'assets'
    if(Test-Path -LiteralPath $assets){foreach($file in Get-ChildItem -LiteralPath $assets -File -Recurse){$runtime+=@{Name=[IO.Path]::GetRelativePath($repoRoot,$file.FullName).Replace('\','/');Path=$file.FullName}}}
}
# 离线计划工具通过本地模块导入索引解析器，必须一并交付。
foreach($package in @(@{Path=$sourcePath;Items=$source},@{Path=$runtimePath;Items=$runtime})){
    if(@($package.Items.Name|Group-Object|Where-Object Count -gt 1).Count){throw '归档中存在重复路径'}
    if(@($package.Items.Name|Where-Object {$_ -match '(^|/)(work|SavedGames|game-probe-|state-save-baseline|\.git)(/|$)' -or $_ -match '\.(sav|dmp|bin)$' }).Count){throw '归档中出现研究或存档负载'}
    Write-Zip $package.Path $package.Items
}
$archives=@($sourcePath,$runtimePath)|ForEach-Object {@{file=[IO.Path]::GetFileName($_);sha256=(Get-FileHash -LiteralPath $_).Hash;bytes=(Get-Item -LiteralPath $_).Length}}
$sourceHashes=@($source|Sort-Object Name|ForEach-Object {@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.Path).Hash}})
$runtimeHashes=@($runtime|Sort-Object Name|ForEach-Object {@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.Path).Hash}})
$snapshotText=($sourceHashes|ForEach-Object {"$($_.name):$($_.sha256)"}) -join "`n"
$snapshot=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($snapshotText)))
# 没有远端也能准备本地预览；未提交快照不能冒充正式版本提交。
$baseHead=& git -C $repoRoot rev-parse HEAD
if($LASTEXITCODE){throw '无法记录源码基线 HEAD。'}
@{version=$build.version;releaseKind='local-developer-preview';baseGitHead=$baseHead;sourceSnapshotSha256=$snapshot;toolchain=$build.toolchain;archives=@($archives);sourceFiles=$source.Count;runtimeFiles=$runtime.Count;sourceEntries=$sourceHashes;runtimeEntries=$runtimeHashes;coreSha256=$build.files.'aamod_core.dll'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
Write-Output "已生成 $($build.version) 本地预览、逐文件哈希与源码快照标识。"
