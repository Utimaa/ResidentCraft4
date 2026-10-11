Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../launcher-common.ps1')
. (Join-Path $PSScriptRoot '../runtime.ps1')
$script:Checks=0
function Check([bool] $Condition,[string] $Name) { if(-not $Condition){throw "FAILED: $Name"}; $script:Checks++ }
function Reject([scriptblock] $Action,[string] $Name) { $rejected=$false; try { & $Action | Out-Null } catch { $rejected=$true }; Check $rejected $Name }
$testParent=Join-Path (Get-ReleaseRoot) 'build/release-tests'
$testRoot=Join-Path $testParent ('RE4Craft-release-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
try {
    Check (Test-WindowsLibrary ([pscustomobject]@{name='generic'})) 'Generic artifact accepted'
    Check (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow';os=[pscustomobject]@{name='windows'}})})) 'Windows artifact accepted'
    Check (-not (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow';os=[pscustomobject]@{name='osx'}})}))) 'macOS artifact excluded'
    Check (-not (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow';os=[pscustomobject]@{name='linux'}})}))) 'Linux artifact excluded'
    Check (-not (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow';os=[pscustomobject]@{name='windows';arch='arm64'}})}))) 'ARM artifact excluded'
    Check (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow'},[pscustomobject]@{action='disallow';os=[pscustomobject]@{name='linux'}})})) 'Nonmatching disallow ignored'
    Check (-not (Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='allow'},[pscustomobject]@{action='disallow';os=[pscustomobject]@{name='windows'}})}))) 'Last matching disallow wins'
    Reject { Test-WindowsLibrary ([pscustomobject]@{rules=@([pscustomobject]@{action='unknown'})}) } 'Unknown rule rejected'
    foreach($unsafe in @('../escape.jar','nested/../../escape.jar','C:/escape.jar','file.jar:stream','/escape.jar','..\escape.jar')) { Reject { Assert-ChildPath $testRoot $unsafe } "Unsafe path rejected: $unsafe" }
    $jar=Assert-ChildPath $testRoot 'libraries/a/a.jar'
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($jar)) -Force | Out-Null
    [IO.File]::WriteAllText($jar,'fixture')
    $hash=(Get-FileHash -LiteralPath $jar -Algorithm SHA1).Hash
    $library=[pscustomobject]@{name='a:a:1';downloads=[pscustomobject]@{artifact=[pscustomobject]@{path='a/a.jar';sha1=$hash;size=7}}}
    $meta=[pscustomobject]@{libraries=@($library,[pscustomobject]@{name='wrong-os';rules=@([pscustomobject]@{action='allow';os=[pscustomobject]@{name='linux'}})})}
    $resolved=@(Resolve-MinecraftLibraries $meta $testRoot)
    Check ($resolved.Count -eq 1 -and $resolved[0] -eq $jar) 'Resolver uses local matching artifact only'
    $meta.libraries=@($library,$library); Reject { Resolve-MinecraftLibraries $meta $testRoot } 'Duplicate artifacts rejected'
    $meta.libraries=@($library); $library.downloads.artifact.sha1='0'*40; Reject { Resolve-MinecraftLibraries $meta $testRoot } 'Modified artifact rejected'
    $library.downloads.artifact.sha1=$hash; $library.downloads.artifact.size=8; Reject { Resolve-MinecraftLibraries $meta $testRoot } 'Wrong artifact size rejected'
    $library.downloads.artifact.size=7; $library.downloads.artifact.path='../escape.jar'; Reject { Resolve-MinecraftLibraries $meta $testRoot } 'Metadata traversal rejected'
    $instance=Join-Path $testRoot '.instance'
    $fresh=Read-WorldSelection $instance ''
    Check ($fresh.New -and $fresh.Name -eq 'RE4Craft_MirrorWorld') 'Fresh install creates one persistent mirror selection'
    Write-ReleaseJson ([pscustomobject]@{SchemaVersion=1;WorldName='Existing_World'}) (Join-Path $instance 'RE4Craft.world.json')
    Reject { Read-WorldSelection $instance '' } 'Missing selected save cannot be recreated'
    Check ((Read-WorldSelection $instance '' $true).New) 'Explicit first-launch pending selection retained'
    New-Item -ItemType Directory -Path (Join-Path $instance 'saves/Existing_World') -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $instance 'saves/Existing_World/level.dat'),'existing-save')
    Check (-not (Read-WorldSelection $instance '').New) 'Existing selected save preserved'
    $firstSettings=[pscustomobject]@{InstancePath=$instance;WorldName='Existing_World';AllowNewWorld=$true;ShutdownToken='a'*32}
    $firstSettingsFile=Join-Path $testRoot '.re4craft/settings.json'
    Preserve-CreatedWorld $firstSettings $firstSettingsFile
    $preservedSettings=Read-JsonFile $firstSettingsFile
    Check (-not $preservedSettings.AllowNewWorld) 'Actual created save disables future automatic recreation'
    Check (-not $preservedSettings.PSObject.Properties['ShutdownToken']) 'Per-launch shutdown token is not retained in settings'
    Reject { Read-WorldSelection $instance 'Different' } 'Selection cannot silently change'
    $unselected=Join-Path $testRoot 'unselected/.instance'
    New-Item -ItemType Directory -Path (Join-Path $unselected 'saves/Existing') -Force | Out-Null
    Reject { Read-WorldSelection $unselected '' } 'Unselected existing saves require explicit choice'
    Reject { Read-WorldSelection $unselected '../Bad' } 'Unsafe world name rejected'
    Check ((ConvertTo-JavaArgument 'C:\Path with spaces\"quote"') -eq '"C:\\Path with spaces\\\"quote\""') 'Java argfile spaces and quotes escaped'
    Reject { ConvertTo-JavaArgument "bad`nargument" } 'Argfile line injection rejected'
    $timestamp='2026-10-11T14:05:21.3970940Z'
    Check ((ConvertTo-CreatedUtc $timestamp) -eq $timestamp) 'Windows PowerShell JSON string process time preserved'
    Check ((ConvertTo-CreatedUtc ([DateTime]::Parse($timestamp))) -eq $timestamp) 'PowerShell 7 JSON DateTime process time preserved'
    Check ((ConvertTo-CreatedUtc ([DateTimeOffset]::Parse($timestamp))) -eq $timestamp) 'DateTimeOffset process time preserves exact ticks'
    Check ($null -eq (ConvertTo-CreatedUtc 'not-a-time')) 'Invalid process generation rejected'
    Check (Test-InstanceCommandLine 'javaw --gameDir "C:/Project/minecraft/../.dev-instance"' 'C:/Project/.dev-instance') 'Loom lexical game directory recognized'
    Check (Test-InstanceCommandLine 'javaw @"C:/Project/.instance/.re4craft/launch.args"' 'C:/Project/.instance') 'Exact private argfile instance recognized'
    Check (-not (Test-InstanceCommandLine 'javaw --gameDir C:/Other/.dev-instance' 'C:/Project/.dev-instance')) 'Unrelated workspace source is not blocked'
    $owned=[pscustomobject]@{Libraries=@('C:\Minecraft libs\original.jar');Client='C:\Minecraft\26.3.jar';Assets='C:\Minecraft\assets';AssetIndex='34'}
    $payload=[pscustomobject]@{FabricLibraries=@('C:\Release\fabric.jar');MainClass='net.fabricmc.loader.impl.launch.knot.KnotClient'}
    $settings=[pscustomobject]@{InstancePath=$instance;WorldName='Existing_World';ShutdownToken='a'*32}
    $args=@(New-MinecraftArguments $owned $payload $settings)
    Check ($args -contains '-Dre4craft.background=true') 'Background mode requested'
    Check ($args -contains '-Dre4craft.testExitSeconds=0') 'Production client has no timed QA exit'
    Check ($args -contains '-Dre4craft.grantStarterBow=true') 'One-time saved starter bow requested'
    Check ($args -contains ('-Dre4craft.shutdownToken='+('a'*32))) 'Owned shutdown token passed to source'
    Check ($args -contains ('-Dre4craft.worldName=Existing_World')) 'Exact world selected'
    Check ($args -contains 'C:\Release\fabric.jar;C:\Minecraft libs\original.jar;C:\Minecraft\26.3.jar') 'Knot classpath built from owned files and Fabric'
    Check ($args -notcontains '--demo') 'No demo or multiplayer substitution'
    $fakeJava=Join-Path $testRoot 'java/bin/javaw.exe'
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($fakeJava)) -Force | Out-Null
    $bytes=New-Object byte[] 128; $bytes[0]=0x4d;$bytes[1]=0x5a;$bytes[0x3c]=0x40;$bytes[0x40]=0x50;$bytes[0x41]=0x45;$bytes[0x44]=0x64;$bytes[0x45]=0x86
    [IO.File]::WriteAllBytes($fakeJava,$bytes)
    [IO.File]::WriteAllText((Join-Path $testRoot 'java/release'),'JAVA_VERSION="25.0.4"')
    Check (Test-Java25 $fakeJava) 'Official stripped release file accepted with proven AMD64 PE'
    $bytes[0x44]=0x4c;$bytes[0x45]=0x01;[IO.File]::WriteAllBytes($fakeJava,$bytes);Check (-not (Test-Java25 $fakeJava)) 'x86 runtime rejected'
    $bytes[0x44]=0x64;$bytes[0x45]=0x86;[IO.File]::WriteAllBytes($fakeJava,$bytes)
    [IO.File]::WriteAllText((Join-Path $testRoot 'java/release'),'JAVA_VERSION="25.0.1"');Check (-not (Test-Java25 $fakeJava)) 'Unpatched Java 25.0.1 rejected'
    [IO.File]::WriteAllText((Join-Path $testRoot 'java/release'),'JAVA_VERSION="21.0.1"');Check (-not (Test-Java25 $fakeJava)) 'Old Java rejected'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    function New-FakeRuntimeZip([string] $Path,[string[]] $Names){
        $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew)
        $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
        try{foreach($name in $Names){$entry=$zip.CreateEntry($name);$writer=[IO.StreamWriter]::new($entry.Open());try{$writer.Write('fixture')}finally{$writer.Dispose()}}}finally{$zip.Dispose();$stream.Dispose()}
    }
    $validNames=@(($script:RuntimeArchiveDirectoryName+'/bin/javaw.exe'),($script:RuntimeArchiveDirectoryName+'/release'))
    $zipPath=Join-Path $testRoot 'valid.zip';New-FakeRuntimeZip $zipPath $validNames
    Check (@(Test-RuntimeArchivePaths $zipPath).Count -eq 2) 'Runtime archive entries resolved within authentic root layout'
    $unsafeZip=Join-Path $testRoot 'unsafe.zip';New-FakeRuntimeZip $unsafeZip ($validNames+@($script:RuntimeArchiveDirectoryName+'/../escape'))
    Reject {Test-RuntimeArchivePaths $unsafeZip} 'Runtime ZIP traversal rejected'
    $absoluteZip=Join-Path $testRoot 'absolute.zip';New-FakeRuntimeZip $absoluteZip ($validNames+@('C:/escape'))
    Reject {Test-RuntimeArchivePaths $absoluteZip} 'Runtime ZIP absolute path rejected'
    $duplicateZip=Join-Path $testRoot 'duplicate.zip';New-FakeRuntimeZip $duplicateZip ($validNames+@($validNames[0]))
    Reject {Test-RuntimeArchivePaths $duplicateZip} 'Duplicate runtime ZIP file rejected'
    $badCacheRoot=Join-Path $testRoot 'bad-cache'
    $badArchive=Join-Path $badCacheRoot ('.re4craft/runtime/'+$script:RuntimeArchiveName)
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($badArchive)) -Force|Out-Null
    Copy-Item -LiteralPath $zipPath -Destination $badArchive
    $badHash=(Get-FileHash -LiteralPath $badArchive -Algorithm SHA256).Hash
    Reject {Install-PortableRuntime $badCacheRoot ''} 'Runtime archive cache must match pinned SHA256'
    Check ((Get-FileHash -LiteralPath $badArchive -Algorithm SHA256).Hash -eq $badHash) 'Wrong-hash cached runtime archive preserved'
    $untrackedRoot=Join-Path $testRoot 'untracked-runtime'
    New-Item -ItemType Directory -Path (Join-Path $untrackedRoot ('.re4craft/runtime/'+$script:RuntimeDirectoryName)) -Force|Out-Null
    Reject {Read-PrivateRuntime $untrackedRoot} 'Untracked private runtime cannot be overwritten'
    $sourcePayload=Join-Path $testRoot 'payload'
    New-Item -ItemType Directory -Path $sourcePayload -Force | Out-Null
    foreach($name in @('re4craft-fabric.jar','fabric-api.jar')){[IO.File]::WriteAllText((Join-Path $sourcePayload $name),$name+'-v1')}
    $installPayload=[pscustomobject]@{Path=$sourcePayload}
    Install-SourceMods $installPayload $instance
    $installed=Read-JsonFile (Join-Path $instance 'RE4Craft.instance.json')
    Check ($installed.SchemaVersion -eq 2 -and @($installed.Files | Where-Object Owned).Count -eq 2) 'Fresh private source files have explicit ownership'
    [IO.File]::WriteAllText((Join-Path $sourcePayload 're4craft-fabric.jar'),'updated-bridge')
    Install-SourceMods $installPayload $instance
    Check ((Get-Content -LiteralPath (Join-Path $instance 'mods/re4craft-fabric.jar') -Raw) -eq 'updated-bridge') 'Unchanged owned source bridge can update'
    [IO.File]::WriteAllText((Join-Path $instance 'mods/re4craft-fabric.jar'),'user-modified')
    Reject { Install-SourceMods $installPayload $instance } 'Modified bridge cannot be overwritten'
    Check ((Get-Content -LiteralPath (Join-Path $instance 'mods/re4craft-fabric.jar') -Raw) -eq 'user-modified') 'Modified bridge content preserved'
    [IO.File]::WriteAllText((Join-Path $instance 'mods/foreign.jar'),'foreign')
    Reject { Install-SourceMods $installPayload $instance } 'Foreign source mod is not removed'
    Check (Test-Path -LiteralPath (Join-Path $instance 'mods/foreign.jar')) 'Foreign mod remains intact'
    $saveHash=(Get-FileHash -LiteralPath (Join-Path $instance 'saves/Existing_World/level.dat') -Algorithm SHA256).Hash
    Check ($saveHash -eq (Get-FileHash -LiteralPath (Join-Path $instance 'saves/Existing_World/level.dat') -Algorithm SHA256).Hash) 'All checks preserve selected save'
    Write-Host "$script:Checks release launcher checks passed."
} finally {
    $fullTestRoot=Get-FullPath $testRoot
    $fullTestParent=Get-FullPath $testParent
    if (-not $fullTestRoot.StartsWith($fullTestParent+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($fullTestRoot) -notlike 'RE4Craft-release-tests-*') { throw 'Unsafe test cleanup target.' }
    Remove-Item -LiteralPath $fullTestRoot -Recurse -Force
}
