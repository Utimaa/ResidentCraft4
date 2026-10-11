Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ReleaseRoot { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
function Get-FullPath([string] $Path) { [IO.Path]::GetFullPath($Path).TrimEnd('\', '/') }
function Assert-ChildPath([string] $Root, [string] $Relative) {
    if ([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative -match ':') { throw 'Invalid relative file path.' }
    $rootPath = Get-FullPath $Root
    $path = Get-FullPath (Join-Path $rootPath $Relative)
    if (-not $path.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'File escapes its expected directory.' }
    Assert-NoReparse $path
    $path
}
function Assert-NoReparse([string] $Path) {
    $cursor = Get-FullPath $Path
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Symbolic link/junction is not supported here: $cursor" }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ($parent -eq $cursor) { break }; $cursor = $parent
    }
}
function Read-JsonFile([string] $Path) { Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
function Write-ReleaseJson([object] $Value, [string] $Path) {
    Assert-NoReparse $Path
    $parent = [IO.Path]::GetDirectoryName($Path)
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.new'
    try { $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding UTF8; Move-Item -LiteralPath $temporary -Destination $Path -Force }
    finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
}
function Assert-FileHash([string] $Path, [string] $Hash, [string] $Algorithm = 'SHA256') {
    Assert-NoReparse $Path
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required file is missing: $Path" }
    $pattern = if ($Algorithm -eq 'SHA1') { '^[0-9a-fA-F]{40}$' } else { '^[0-9a-fA-F]{64}$' }
    if ($Hash -notmatch $pattern -or (Get-FileHash -LiteralPath $Path -Algorithm $Algorithm).Hash -ne $Hash) { throw "File verification failed: $Path" }
}
function Read-ReleasePayload([string] $Root) {
    $payload = Assert-ChildPath $Root 'payload'
    $manifest = Read-JsonFile (Assert-ChildPath $payload 'manifest.json')
    if ($manifest.SchemaVersion -ne 1 -or $manifest.Version -ne '0.2.0-alpha.2') { throw 'Unsupported release payload.' }
    $seen = @{}
    foreach ($entry in $manifest.Files) {
        if ($entry.Path -notmatch '^(RE4Craft\.(asi|ini)|winmm\.dll|re4craft-fabric\.jar|fabric-api\.jar|fabric-launch\.json|fabric-libraries/[A-Za-z0-9+._-]+\.jar)$' -or $seen.ContainsKey($entry.Path)) { throw 'Unexpected or duplicate release payload file.' }
        $seen[$entry.Path] = $true
        Assert-FileHash (Assert-ChildPath $payload $entry.Path) $entry.Sha256
    }
    foreach ($required in @('RE4Craft.asi','RE4Craft.ini','winmm.dll','re4craft-fabric.jar','fabric-api.jar','fabric-launch.json')) { if (-not $seen.ContainsKey($required)) { throw "Release lacks $required" } }
    $fabric = Read-JsonFile (Assert-ChildPath $payload 'fabric-launch.json')
    if ($fabric.SchemaVersion -ne 1 -or $fabric.MinecraftVersion -ne '26.3' -or $fabric.FabricLoader -ne '0.19.5' -or $fabric.MainClass -ne 'net.fabricmc.loader.impl.launch.knot.KnotClient') { throw 'Unexpected Fabric runtime definition.' }
    $libraries = @(); $fabricSeen = @{}
    foreach ($library in $fabric.Libraries) {
        if ($library.Path -notmatch '^fabric-libraries/[A-Za-z0-9+._-]+\.jar$' -or -not $seen.ContainsKey($library.Path) -or $fabricSeen.ContainsKey($library.Path)) { throw 'Unexpected Fabric library.' }
        $fabricSeen[$library.Path] = $true
        $path = Assert-ChildPath $payload $library.Path
        Assert-FileHash $path $library.Sha256
        $libraries += $path
    }
    if ($libraries.Count -lt 3) { throw 'Fabric runtime is incomplete.' }
    [pscustomobject]@{ Path = $payload; Manifest = $manifest; FabricLibraries = $libraries; MainClass = $fabric.MainClass }
}
function Test-WindowsLibraryRule([object] $Rule) {
    if ($Rule.PSObject.Properties['features']) { return $false }
    if (-not $Rule.PSObject.Properties['os']) { return $true }
    $os = $Rule.os
    if ($os.PSObject.Properties['name'] -and $os.name -ne 'windows') { return $false }
    if ($os.PSObject.Properties['arch'] -and $os.arch -notin @('x86_64','amd64')) { return $false }
    if ($os.PSObject.Properties['version'] -and [Environment]::OSVersion.Version.ToString() -notmatch $os.version) { return $false }
    $true
}
function Test-WindowsLibrary([object] $Library) {
    if (-not $Library.PSObject.Properties['rules']) { return $true }
    $allowed = $false
    foreach ($rule in $Library.rules) {
        if ($rule.action -notin @('allow','disallow')) { throw 'Unknown Minecraft library rule.' }
        if (Test-WindowsLibraryRule $rule) { $allowed = $rule.action -eq 'allow' }
    }
    $allowed
}
function Resolve-MinecraftLibraries([object] $Metadata, [string] $MinecraftPath) {
    $libraryRoot = Assert-ChildPath $MinecraftPath 'libraries'
    $paths = @(); $seen = @{}
    foreach ($library in $Metadata.libraries) {
        if (-not (Test-WindowsLibrary $library)) { continue }
        if (-not $library.PSObject.Properties['downloads'] -or -not $library.downloads.PSObject.Properties['artifact']) { throw 'Minecraft library download metadata is missing. Launch 26.3 once in the official launcher.' }
        if ($library.PSObject.Properties['natives']) { throw 'Legacy extracted native layout is unsupported for Minecraft 26.3.' }
        $artifact = $library.downloads.artifact
        $path = Assert-ChildPath $libraryRoot $artifact.path
        if ($artifact.path -notmatch '\.jar$' -or $seen.ContainsKey($path)) { throw 'Invalid or duplicate Minecraft library.' }
        $seen[$path] = $true
        Assert-FileHash $path $artifact.sha1 'SHA1'
        if ((Get-Item -LiteralPath $path).Length -ne $artifact.size) { throw "Minecraft library size differs: $path" }
        $paths += $path
    }
    if ($paths.Count -eq 0) { throw 'Minecraft libraries are missing.' }
    $paths
}
function Resolve-OwnedMinecraft([string] $MinecraftPath, [bool] $CheckAssetObjects = $true) {
    $minecraft = Get-FullPath $MinecraftPath
    $metadataPath = Assert-ChildPath $minecraft 'versions/26.3/26.3.json'
    Assert-FileHash $metadataPath '702fe59163c6ee6578607daa85811d9bc9c7cc40' 'SHA1'
    $metadata = Read-JsonFile $metadataPath
    if ($metadata.id -ne '26.3' -or $metadata.mainClass -ne 'net.minecraft.client.main.Main' -or $metadata.javaVersion.majorVersion -ne 25 -or $metadata.downloads.client.sha1 -ne 'e877b6a07acd633fb3bb475002175cec036e7b87') { throw 'Install and launch the official Minecraft Java 26.3 version first.' }
    $client = Assert-ChildPath $minecraft 'versions/26.3/26.3.jar'
    Assert-FileHash $client $metadata.downloads.client.sha1 'SHA1'
    $libraries = @(Resolve-MinecraftLibraries $metadata $minecraft)
    $assets = Assert-ChildPath $minecraft 'assets'
    if ($metadata.assetIndex.id -notmatch '^[A-Za-z0-9._-]+$') { throw 'Invalid asset index name.' }
    $index = Assert-ChildPath $assets ('indexes/' + $metadata.assetIndex.id + '.json')
    Assert-FileHash $index $metadata.assetIndex.sha1 'SHA1'
    $objects = if ($CheckAssetObjects) { Read-JsonFile $index } else { $null }
    if ($CheckAssetObjects) {
    $objectRoot = Assert-ChildPath $assets 'objects'
    $checkedBuckets = @{}
    foreach ($property in $objects.objects.PSObject.Properties) {
        $asset = $property.Value
        if ($asset.hash -notmatch '^[0-9a-f]{40}$') { throw 'Invalid asset hash.' }
        $bucket = $asset.hash.Substring(0,2)
        if (-not $checkedBuckets.ContainsKey($bucket)) { $checkedBuckets[$bucket] = Assert-ChildPath $objectRoot $bucket }
        # The pinned asset index supplies only a 40-digit hash. Check each of its
        # 256 possible parent directories once, then inspect the exact regular file.
        $assetPath = Join-Path $checkedBuckets[$bucket] $asset.hash
        $assetFile = [IO.FileInfo]::new($assetPath)
        if (-not $assetFile.Exists -or $assetFile.Length -ne $asset.size -or ($assetFile.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Minecraft assets are incomplete. Launch 26.3 once in the official Minecraft Launcher, then run Setup again.' }
    } }
    [pscustomobject]@{ Root = $minecraft; Client = $client; Libraries = $libraries; Assets = $assets; AssetIndex = $metadata.assetIndex.id }
}
function Test-Java25([string] $JavaPath) {
    if (-not (Test-Path -LiteralPath $JavaPath -PathType Leaf) -or [IO.Path]::GetFileName($JavaPath) -ne 'javaw.exe') { return $false }
    Assert-NoReparse $JavaPath
    $releaseFile = Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($JavaPath))) 'release'
    if (-not (Test-Path -LiteralPath $releaseFile -PathType Leaf)) { return $false }
    $releaseText = Get-Content -LiteralPath $releaseFile -Raw
    if ($releaseText -notmatch '(?m)^JAVA_VERSION="(25(?:\.[0-9]+){0,3})[+\-"]') { return $false }
    $versionText=$Matches[1]; if($versionText -notmatch '\.'){$versionText+='.0'}
    if ([Version]$versionText -lt [Version]'25.0.4') { return $false }
    # Mojang's stripped runtime release file omits OS_ARCH; prove AMD64 from the real PE header.
    $stream = [IO.File]::OpenRead($JavaPath); $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { return $false }
        $stream.Position=0x3c; $offset=$reader.ReadUInt32()
        if ([long]$offset+6 -gt $stream.Length) { return $false }
        $stream.Position=$offset
        return $reader.ReadUInt32() -eq 0x4550 -and $reader.ReadUInt16() -eq 0x8664
    } finally { $reader.Dispose(); $stream.Dispose() }
}
function Resolve-Java25([string] $MinecraftPath, [string] $JavaPath) {
    if ($JavaPath) { $full = Get-FullPath $JavaPath; if (Test-Java25 $full) { return $full }; throw 'JavaPath must name a 64-bit Java 25.0.4 or newer bin/javaw.exe.' }
    $roots = @(
        (Join-Path $MinecraftPath 'runtime'),
        (Join-Path $env:LOCALAPPDATA 'Packages/Microsoft.4297127D64EC6_8wekyb3d8bbwe/LocalCache/Local/runtime'),
        (Join-Path $env:LOCALAPPDATA 'Minecraft Launcher/runtime'),
        (Join-Path $env:ProgramFiles 'Minecraft Launcher/runtime')
    )
    $roamingParent = [IO.Path]::GetDirectoryName((Get-FullPath $MinecraftPath))
    if ([IO.Path]::GetFileName($roamingParent) -eq 'Roaming') {
        $userLocal = Join-Path ([IO.Path]::GetDirectoryName($roamingParent)) 'Local'
        $roots += Join-Path $userLocal 'Packages/Microsoft.4297127D64EC6_8wekyb3d8bbwe/LocalCache/Local/runtime'
    }
    if (${env:ProgramFiles(x86)}) { $roots += Join-Path ${env:ProgramFiles(x86)} 'Minecraft Launcher/runtime' }
    if ($env:JAVA_HOME) { $candidate = Join-Path $env:JAVA_HOME 'bin/javaw.exe'; if (Test-Java25 $candidate) { return Get-FullPath $candidate } }
    foreach ($runtimeRoot in $roots) {
        if (-not (Test-Path -LiteralPath $runtimeRoot -PathType Container)) { continue }
        Assert-NoReparse $runtimeRoot
        foreach ($candidate in @(Get-ChildItem -LiteralPath $runtimeRoot -Filter javaw.exe -File -Recurse | Sort-Object FullName)) { if (Test-Java25 $candidate.FullName) { return $candidate.FullName } }
    }
    throw 'Java 25 was not found. Launch Minecraft 26.3 once in its official launcher. Advanced setup can supply -JavaPath ".../bin/javaw.exe".'
}
function Find-RE4Installation {
    $steamRoots = @()
    try { $steamRoots += (Get-ItemProperty -LiteralPath 'HKCU:/Software/Valve/Steam').SteamPath } catch { }
    if (${env:ProgramFiles(x86)}) { $steamRoots += Join-Path ${env:ProgramFiles(x86)} 'Steam' }
    $libraries = @($steamRoots | Where-Object { $_ })
    foreach ($steamRoot in $steamRoots) {
        if (-not $steamRoot) { continue }
        $vdf = Join-Path $steamRoot 'steamapps/libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf -PathType Leaf) {
            $contents = Get-Content -LiteralPath $vdf -Raw
            foreach ($match in [regex]::Matches($contents, '"path"\s+"([^"\r\n]+)"')) { $libraries += $match.Groups[1].Value.Replace('\\','\') }
        }
    }
    $matches = @($libraries | ForEach-Object { Join-Path $_ 'steamapps/common/Resident Evil 4' } | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Bin32/bio4.exe') -PathType Leaf } | Sort-Object -Unique)
    if ($matches.Count -eq 1) { return Get-FullPath $matches[0] }
    if ($matches.Count -gt 1) { throw 'Several RE4 installations were found. Run Setup with -GamePath to choose one.' }
    throw 'RE4 UHD was not found. Run Setup with -GamePath pointing to the Resident Evil 4 folder.'
}
function Read-WorldSelection([string] $InstancePath, [string] $WorldName, [bool] $AllowNewSelected = $false) {
    $selection = Assert-ChildPath $InstancePath 'RE4Craft.world.json'
    if (Test-Path -LiteralPath $selection -PathType Leaf) {
        $saved = Read-JsonFile $selection
        if ($saved.SchemaVersion -ne 1 -or $saved.WorldName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') { throw 'Invalid saved world selection; it was left untouched.' }
        if ($WorldName -and $WorldName -ne $saved.WorldName) { throw 'Setup preserves the existing world selection. Select another instance explicitly to use another world.' }
        $world = Assert-ChildPath $InstancePath ('saves/' + $saved.WorldName)
        $exists = Test-Path -LiteralPath (Join-Path $world 'level.dat') -PathType Leaf
        if (-not $exists -and -not $AllowNewSelected) { throw 'The selected existing world is missing. Restore its save before continuing; Setup will not recreate it.' }
        return [pscustomobject]@{ Name = $saved.WorldName; New = -not $exists; SelectionExists = $true }
    }
    $saves = Assert-ChildPath $InstancePath 'saves'
    if (-not $WorldName) {
        if ((Test-Path -LiteralPath $saves) -and @(Get-ChildItem -LiteralPath $saves -Directory).Count -gt 0) { throw 'Existing saves have no selection. Supply -WorldName to select one; no world was changed.' }
        $WorldName = 'RE4Craft_MirrorWorld'
    }
    if ($WorldName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') { throw 'Invalid world name.' }
    $world = Assert-ChildPath $InstancePath ('saves/' + $WorldName)
    [pscustomobject]@{ Name = $WorldName; New = -not (Test-Path -LiteralPath (Join-Path $world 'level.dat') -PathType Leaf); SelectionExists = $false }
}
function ConvertTo-JavaArgument([string] $Argument) {
    if ($Argument -match '[\r\n]') { throw 'Invalid launch argument.' }
    '"' + $Argument.Replace('\','\\').Replace('"','\"') + '"'
}
function New-MinecraftArguments([object] $Owned, [object] $Payload, [object] $Settings) {
    if ($Settings.ShutdownToken -notmatch '^[A-Za-z0-9_-]{32,96}$') { throw 'Invalid owned launch shutdown token.' }
    $natives = Assert-ChildPath $Settings.InstancePath '.natives'
    $classpath = (@($Payload.FabricLibraries) + @($Owned.Libraries) + @($Owned.Client)) -join ';'
    @('-Xms256M','-Xmx2G','--enable-native-access=ALL-UNNAMED','--add-exports','java.base/jdk.internal.misc=ALL-UNNAMED',
      ('-XX:ErrorFile=' + (Assert-ChildPath $Settings.InstancePath '.re4craft/hs_err_pid%p.log')),
      '-Dre4craft.background=true','-Dre4craft.testWorld=true','-Dre4craft.voidMirror=true','-Dre4craft.testExitSeconds=0',
      ('-Dre4craft.worldName=' + $Settings.WorldName),'-Dre4craft.grantStarterBow=true',
      ('-Dre4craft.shutdownFile=' + (Assert-ChildPath $Settings.InstancePath '.re4craft/shutdown.request')),
      ('-Dre4craft.shutdownToken=' + $Settings.ShutdownToken),
      '-Dminecraft.launcher.brand=RE4Craft','-Dminecraft.launcher.version=0.2.0-alpha.2',
      ('-Djava.library.path=' + $natives + '/java'),('-Djna.tmpdir=' + $natives + '/jna'),
      ('-Dorg.lwjgl.system.SharedLibraryExtractPath=' + $natives + '/lwjgl'),('-Dio.netty.native.workdir=' + $natives + '/netty'),
      '-cp',$classpath,$Payload.MainClass,'--username','Steve','--version','26.3','--gameDir',$Settings.InstancePath,
      '--assetsDir',$Owned.Assets,'--assetIndex',$Owned.AssetIndex,'--uuid','5627dd98e6be3c21b8a8e92344183641',
      '--accessToken','0','--userType','legacy','--versionType','release','--width','960','--height','540')
}
function ConvertTo-CreatedUtc([object] $Value) {
    # Windows PowerShell keeps JSON date strings; PowerShell 7 may deserialize
    # them as DateTime. Compare the exact UTC instant in either host.
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime().ToString('o') }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime.ToString('o') }
    $parsed=[DateTimeOffset]::MinValue
    if ($Value -isnot [string] -or -not [DateTimeOffset]::TryParseExact($Value,'o',[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::None,[ref]$parsed)) { return $null }
    $parsed.UtcDateTime.ToString('o')
}
function Get-OwnedSourceProcess([object] $Receipt, [object] $Settings) {
    if ($Receipt.SchemaVersion -ne 1 -or $Receipt.InstancePath -ne $Settings.InstancePath -or $Receipt.JavaPath -ne $Settings.JavaPath) { return $null }
    if ($Receipt.ShutdownToken -notmatch '^[A-Za-z0-9_-]{32,96}$') { return $null }
    if ($Receipt.ArgumentFile -ne (Assert-ChildPath $Settings.InstancePath '.re4craft/launch.args')) { return $null }
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$Receipt.ProcessId) -ErrorAction SilentlyContinue
    if (-not $process -or -not $process.ExecutablePath -or -not $process.CommandLine -or (Get-FullPath $process.ExecutablePath) -ne $Settings.JavaPath -or (ConvertTo-CreatedUtc $process.CreationDate) -ne (ConvertTo-CreatedUtc $Receipt.CreatedUtc)) { return $null }
    # The source is launched by one private argfile; PID alone never proves ownership.
    if ($process.CommandLine -notmatch [regex]::Escape('@' + $Receipt.ArgumentFile)) { return $null }
    $process
}
function Preserve-CreatedWorld([object] $Settings, [string] $SettingsPath) {
    if (-not $Settings.AllowNewWorld) { return }
    if ($Settings.WorldName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') { throw 'Invalid selected world.' }
    $level = Assert-ChildPath $Settings.InstancePath ('saves/' + $Settings.WorldName + '/level.dat')
    if (Test-Path -LiteralPath $level -PathType Leaf) {
        $Settings.AllowNewWorld=$false
        $Settings.PSObject.Properties.Remove('ShutdownToken')
        Write-ReleaseJson $Settings $SettingsPath
    }
}
function Assert-InstanceStopped([string] $InstancePath) {
    $instance = Get-FullPath $InstancePath
    foreach ($process in @(Get-CimInstance Win32_Process -Filter "Name='java.exe' OR Name='javaw.exe'")) {
        if ($process.CommandLine -and (Test-InstanceCommandLine $process.CommandLine $instance)) { throw 'Close the Minecraft bridge using StopMinecraft.cmd before updating this instance.' }
    }
}
function Test-InstanceCommandLine([string] $CommandLine,[string] $InstancePath){
    $instance=Get-FullPath $InstancePath
    $normalized=$CommandLine.Replace('/','\')
    if($normalized.IndexOf($instance,[StringComparison]::OrdinalIgnoreCase) -ge 0){return $true}
    # Loom may retain minecraft/../.dev-instance rather than the normalized
    # folder. Match the same workspace and exact private basename as well.
    $parent=[IO.Path]::GetDirectoryName($instance)
    $name=[regex]::Escape([IO.Path]::GetFileName($instance))
    $normalized.IndexOf($parent,[StringComparison]::OrdinalIgnoreCase) -ge 0 -and $normalized -match ('(?i)(^|[\\"\s])'+$name+'([\\"\s]|$)')
}
function Install-SourceMods([object] $Payload, [string] $InstancePath) {
    $mods = Assert-ChildPath $InstancePath 'mods'
    $manifestPath = Assert-ChildPath $InstancePath 'RE4Craft.instance.json'
    $previous = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) { Read-JsonFile $manifestPath } else { $null }
    if ($previous -and ($previous.SchemaVersion -notin @(1,2) -or (Get-FullPath $previous.InstancePath) -ne $InstancePath)) { throw 'Invalid previous source manifest.' }
    $oldEntries = @{}
    if ($previous -and $previous.SchemaVersion -eq 2) {
        foreach ($entry in $previous.Files) {
            if ($entry.Name -notin @('re4craft-fabric.jar','fabric-api.jar') -or $entry.Sha256 -notmatch '^[0-9a-fA-F]{64}$' -or $entry.Owned -isnot [bool] -or $oldEntries.ContainsKey($entry.Name)) { throw 'Invalid source file ownership.' }
            $oldEntries[$entry.Name] = $entry
        }
    }
    $obsolete = @()
    foreach ($existing in @(Get-ChildItem -LiteralPath $mods -Filter '*.jar' -File -ErrorAction SilentlyContinue)) {
        if ($existing.Name -in @('re4craft-fabric.jar','fabric-api.jar')) { continue }
        if ($previous -and $previous.SchemaVersion -eq 1 -and $existing.Name -like 're4craft-fabric-*.jar' -and (Get-FileHash -LiteralPath $existing.FullName -Algorithm SHA256).Hash -eq $previous.JarSha256) { $obsolete += $existing.FullName }
        elseif ($previous -and $previous.SchemaVersion -eq 1 -and $existing.Name -eq 'fabric-api-0.161.0+26.3.jar' -and (Get-FileHash -LiteralPath $existing.FullName -Algorithm SHA256).Hash -eq $previous.FabricApiSha256) { $obsolete += $existing.FullName }
        else { throw 'This private instance contains additional or modified mods. Choose a clean separate .instance folder; all files were preserved.' }
    }
    $operations = @(); $entries = @()
    foreach ($name in @('re4craft-fabric.jar','fabric-api.jar')) {
        $source = Assert-ChildPath $Payload.Path $name
        $destination = Assert-ChildPath $mods $name
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $owned = $true; $existed = Test-Path -LiteralPath $destination -PathType Leaf
        if ($existed) {
            $current = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
            if ($current -eq $hash) { $owned = $oldEntries.ContainsKey($name) -and $oldEntries[$name].Owned }
            elseif (-not $oldEntries.ContainsKey($name) -or -not $oldEntries[$name].Owned -or $current -ne $oldEntries[$name].Sha256) { throw "Modified or untracked $name was preserved." }
        }
        $operations += [pscustomobject]@{ Source = $source; Destination = $destination; Hash = $hash; Existed = $existed; Backup = $null }
        $entries += [pscustomobject]@{ Name = $name; Sha256 = $hash; Owned = $owned }
    }
    $backup = Assert-ChildPath $InstancePath ('.re4craft/backups/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $mods,$backup -Force | Out-Null
    $completed = @(); $removed = @()
    try {
        foreach ($operation in $operations) {
            if ($operation.Existed) { $operation.Backup = Assert-ChildPath $backup ([IO.Path]::GetFileName($operation.Destination)); Copy-Item -LiteralPath $operation.Destination -Destination $operation.Backup }
            $completed += $operation
            Copy-Item -LiteralPath $operation.Source -Destination $operation.Destination -Force
            Assert-FileHash $operation.Destination $operation.Hash
        }
        foreach ($oldPath in $obsolete) {
            $oldBackup = Assert-ChildPath $backup ([IO.Path]::GetFileName($oldPath))
            Copy-Item -LiteralPath $oldPath -Destination $oldBackup
            Remove-Item -LiteralPath $oldPath
            $removed += [pscustomobject]@{ Path = $oldPath; Backup = $oldBackup }
        }
        Write-ReleaseJson ([pscustomobject]@{SchemaVersion=2;InstancePath=$InstancePath;Files=$entries}) $manifestPath
    } catch {
        foreach ($operation in $completed) { if ($operation.Existed) { Copy-Item -LiteralPath $operation.Backup -Destination $operation.Destination -Force } elseif (Test-Path -LiteralPath $operation.Destination) { Remove-Item -LiteralPath $operation.Destination } }
        foreach ($old in $removed) { Copy-Item -LiteralPath $old.Backup -Destination $old.Path }
        throw
    }
}
