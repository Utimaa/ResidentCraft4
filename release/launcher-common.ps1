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
    if ($manifest.SchemaVersion -ne 1 -or $manifest.Version -ne '0.2.0-alpha.3') { throw 'Unsupported release payload.' }
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
function Get-PrismClassifierPath([object] $Library) {
    # Prism splits modern native classifiers into their own Maven artifacts.
    # The pinned Mojang hash/size still validates exactly the original jar.
    if ($Library.name -match '^([A-Za-z0-9_.-]+):([A-Za-z0-9_.-]+):([A-Za-z0-9+_.-]+):([A-Za-z0-9_.-]+)$') {
        $artifact = $Matches[2] + '-' + $Matches[4]
        return $Matches[1].Replace('.','/') + '/' + $artifact + '/' + $Matches[3] + '/' + $artifact + '-' + $Matches[3] + '.jar'
    }
    $null
}
function Resolve-MinecraftLibraries([object] $Metadata, [string] $MinecraftPath, [string] $Layout = 'Official') {
    $libraryRoot = Assert-ChildPath $MinecraftPath 'libraries'
    $paths = @(); $seen = @{}
    foreach ($library in $Metadata.libraries) {
        if (-not (Test-WindowsLibrary $library)) { continue }
        if (-not $library.PSObject.Properties['downloads'] -or -not $library.downloads.PSObject.Properties['artifact']) { throw 'Minecraft library download metadata is missing. Launch 26.3 once in the official launcher.' }
        if ($library.PSObject.Properties['natives']) { throw 'Legacy extracted native layout is unsupported for Minecraft 26.3.' }
        $artifact = $library.downloads.artifact
        $path = Assert-ChildPath $libraryRoot $artifact.path
        if ($Layout -eq 'Prism') {
            $prismRelative = Get-PrismClassifierPath $library
            if ($prismRelative) {
                $prismPath = Assert-ChildPath $libraryRoot $prismRelative
                if (Test-Path -LiteralPath $prismPath -PathType Leaf) { $path = $prismPath }
            }
        }
        if ($artifact.path -notmatch '\.jar$' -or $seen.ContainsKey($path)) { throw 'Invalid or duplicate Minecraft library.' }
        $seen[$path] = $true
        Assert-FileHash $path $artifact.sha1 'SHA1'
        if ((Get-Item -LiteralPath $path).Length -ne $artifact.size) { throw "Minecraft library size differs: $path" }
        $paths += $path
    }
    if ($paths.Count -eq 0) { throw 'Minecraft libraries are missing.' }
    $paths
}
function Resolve-MinecraftLocation([string] $MinecraftPath) {
    $selected = Get-FullPath $MinecraftPath
    Assert-NoReparse $selected
    if (Test-Path -LiteralPath (Assert-ChildPath $selected 'versions/26.3/26.3.json') -PathType Leaf) {
        return [pscustomobject]@{Root=$selected;Layout='Official';Instance=$null}
    }
    # Prism's instance game folder has no versions directory. Libraries and
    # assets are shared at the launcher data root, including portable installs.
    $instance = $selected
    if ([IO.Path]::GetFileName($instance) -in @('minecraft','.minecraft')) { $instance = [IO.Path]::GetDirectoryName($instance) }
    $packPath = Assert-ChildPath $instance 'mmc-pack.json'
    if (Test-Path -LiteralPath $packPath -PathType Leaf) {
        $pack = Read-JsonFile $packPath
        $components = @($pack.components | Where-Object uid -eq 'net.minecraft')
        if ($components.Count -ne 1 -or $components[0].version -ne '26.3') { throw 'Select a Prism instance using Minecraft 26.3, then launch it once in Prism.' }
        $cursor = [IO.Path]::GetDirectoryName($instance)
        while ($cursor) {
            if ((Test-Path -LiteralPath (Join-Path $cursor 'libraries') -PathType Container) -and (Test-Path -LiteralPath (Join-Path $cursor 'assets') -PathType Container)) {
                Assert-NoReparse $cursor
                return [pscustomobject]@{Root=$cursor;Layout='Prism';Instance=$instance}
            }
            $cursor = [IO.Path]::GetDirectoryName($cursor)
        }
        throw 'Prism shared libraries/assets were not found. Supply -MinecraftPath with its launcher data root.'
    }
    if ((Test-Path -LiteralPath (Assert-ChildPath $selected 'libraries/com/mojang/minecraft/26.3/minecraft-26.3-client.jar') -PathType Leaf) -and (Test-Path -LiteralPath (Assert-ChildPath $selected 'assets') -PathType Container)) {
        return [pscustomobject]@{Root=$selected;Layout='Prism';Instance=$null}
    }
    throw 'Minecraft 26.3 was not found here. Choose the official .minecraft folder, a Prism data root, or a Prism 26.3 instance folder after launching it once.'
}
function Get-MinecraftMetadataPath([object] $Location, [string] $CacheRoot, [bool] $AllowDownload) {
    if ($Location.Layout -eq 'Official') { return Assert-ChildPath $Location.Root 'versions/26.3/26.3.json' }
    if (-not $CacheRoot) { throw 'Prism needs the verified Minecraft metadata cache. Run Setup.cmd.' }
    $path = Assert-ChildPath $CacheRoot '.re4craft/metadata/26.3.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        if (-not $AllowDownload) { throw 'Minecraft metadata cache is missing. Run Setup.cmd again.' }
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force | Out-Null
        $temporary = $path + '.' + [Guid]::NewGuid().ToString('N') + '.partial'
        try {
            # Only public version metadata is fetched. Game jars, libraries and
            # assets must already belong to the user's launched installation.
            Invoke-WebRequest -UseBasicParsing -Uri 'https://piston-meta.mojang.com/v1/packages/702fe59163c6ee6578607daa85811d9bc9c7cc40/26.3.json' -OutFile $temporary
            Assert-FileHash $temporary '702fe59163c6ee6578607daa85811d9bc9c7cc40' 'SHA1'
            Move-Item -LiteralPath $temporary -Destination $path
        } finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary } }
    }
    $path
}
function Select-MinecraftPath {
    $official = Join-Path $env:APPDATA '.minecraft'
    if (Test-Path -LiteralPath (Join-Path $official 'versions/26.3/26.3.json') -PathType Leaf) { return $official }
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [Windows.Forms.FolderBrowserDialog]::new()
    $dialog.Description = 'Choose your Minecraft folder, Prism Launcher data root, or Prism 26.3 instance folder.'
    $dialog.ShowNewFolderButton = $false
    try {
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { throw 'No Minecraft installation selected. Run Setup.cmd -MinecraftPath "your Minecraft or Prism folder".' }
        $dialog.SelectedPath
    } finally { $dialog.Dispose() }
}
function Resolve-OwnedMinecraft([string] $MinecraftPath, [bool] $CheckAssetObjects = $true, [string] $CacheRoot = '', [bool] $AllowMetadataDownload = $false) {
    $location = Resolve-MinecraftLocation $MinecraftPath
    $minecraft = $location.Root
    $metadataPath = Get-MinecraftMetadataPath $location $CacheRoot $AllowMetadataDownload
    Assert-FileHash $metadataPath '702fe59163c6ee6578607daa85811d9bc9c7cc40' 'SHA1'
    $metadata = Read-JsonFile $metadataPath
    if ($metadata.id -ne '26.3' -or $metadata.mainClass -ne 'net.minecraft.client.main.Main' -or $metadata.javaVersion.majorVersion -ne 25 -or $metadata.downloads.client.sha1 -ne 'e877b6a07acd633fb3bb475002175cec036e7b87') { throw 'Install and launch the official Minecraft Java 26.3 version first.' }
    $clientRelative = if ($location.Layout -eq 'Prism') { 'libraries/com/mojang/minecraft/26.3/minecraft-26.3-client.jar' } else { 'versions/26.3/26.3.jar' }
    $client = Assert-ChildPath $minecraft $clientRelative
    Assert-FileHash $client $metadata.downloads.client.sha1 'SHA1'
    $libraries = @(Resolve-MinecraftLibraries $metadata $minecraft $location.Layout)
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
    [pscustomobject]@{ Root = $minecraft; Layout = $location.Layout; SourceInstance = $location.Instance; Client = $client; Libraries = $libraries; Assets = $assets; AssetIndex = $metadata.assetIndex.id }
}
function Test-Java25([string] $JavaPath) {
    if (-not (Test-Path -LiteralPath $JavaPath -PathType Leaf) -or [IO.Path]::GetFileName($JavaPath) -notin @('javaw.exe','java.exe')) { return $false }
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
    if ($JavaPath) { $full = Get-FullPath $JavaPath; if (Test-Java25 $full) { return $full }; throw 'JavaPath must name a 64-bit Java 25.0.4 or newer bin/javaw.exe or bin/java.exe.' }
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
    $matches = @($libraries | Where-Object { Test-Path -LiteralPath $_ -PathType Container } | ForEach-Object { Join-Path $_ 'steamapps/common/Resident Evil 4' } | Where-Object { Test-Path -LiteralPath (Join-Path $_ 'Bin32/bio4.exe') -PathType Leaf } | Sort-Object -Unique)
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
function Initialize-NativeLauncher {
    if ('RE4Craft.NativeLauncher' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
namespace RE4Craft {
    public sealed class BackgroundProcess : IDisposable {
        private IntPtr process;
        public int Id { get; private set; }
        public IntPtr Handle { get { return process; } }
        internal BackgroundProcess(IntPtr handle, uint id) { process = handle; Id = (int)id; }
        public bool HasExited { get { return NativeLauncher.WaitForSingleObject(process, 0) == 0; } }
        public int ExitCode { get { uint code; if (!NativeLauncher.GetExitCodeProcess(process, out code)) throw new Win32Exception(); return unchecked((int)code); } }
        public void WaitForExit() { NativeLauncher.WaitForSingleObject(process, 0xffffffff); }
        public void Dispose() { if (process != IntPtr.Zero) { NativeLauncher.CloseHandle(process); process = IntPtr.Zero; } }
    }
    public static class NativeLauncher {
        [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] private struct StartupInfo {
            public int cb; public string reserved, desktop, title; public uint x,y,xSize,ySize,xChars,yChars,fill,flags; public short show, reservedBytes; public IntPtr reserved2, input, output, error;
        }
        [StructLayout(LayoutKind.Sequential)] private struct ProcessInfo { public IntPtr process, thread; public uint processId, threadId; }
        [DllImport("kernel32.dll")] public static extern uint GetACP();
        [DllImport("kernel32.dll", SetLastError=true)] public static extern uint WaitForSingleObject(IntPtr handle, uint timeout);
        [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr handle, out uint exitCode);
        [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError=true)] private static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern IntPtr CreateFileW(string name, uint access, uint sharing, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] private static extern bool CreateProcessW(string application, StringBuilder command, IntPtr processAttributes, IntPtr threadAttributes, bool inherit, uint flags, IntPtr environment, string directory, ref StartupInfo startup, out ProcessInfo info);
        public static BackgroundProcess Start(string executable, string argumentFile, string directory, string stdout, string stderr) {
            using (var output = new FileStream(stdout, FileMode.CreateNew, FileAccess.Write, FileShare.ReadWrite))
            using (var error = new FileStream(stderr, FileMode.CreateNew, FileAccess.Write, FileShare.ReadWrite)) {
                IntPtr input = CreateFileW("NUL", 0x80000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
                if (input == new IntPtr(-1)) throw new Win32Exception();
                var handles = new [] { output.SafeFileHandle.DangerousGetHandle(), error.SafeFileHandle.DangerousGetHandle(), input };
                try {
                    foreach (var handle in handles) if (!SetHandleInformation(handle, 1, 1)) throw new Win32Exception();
                    var startup = new StartupInfo { cb=Marshal.SizeOf(typeof(StartupInfo)), flags=0x100, input=handles[2], output=handles[0], error=handles[1] };
                    ProcessInfo info;
                    var command = new StringBuilder("\"" + executable + "\" \"@" + argumentFile + "\"");
                    if (!CreateProcessW(executable, command, IntPtr.Zero, IntPtr.Zero, true, 0x08000000, IntPtr.Zero, directory, ref startup, out info)) throw new Win32Exception();
                    CloseHandle(info.thread);
                    return new BackgroundProcess(info.process, info.processId);
                } finally { foreach (var handle in handles) SetHandleInformation(handle, 1, 0); CloseHandle(input); }
            }
        }
    }
}
'@
}
function Get-JavaArgumentEncoding {
    Initialize-NativeLauncher
    [Text.Encoding]::GetEncoding([int][RE4Craft.NativeLauncher]::GetACP(),[Text.EncoderExceptionFallback]::new(),[Text.DecoderExceptionFallback]::new())
}
function Write-JavaArgumentFile([string] $Path, [string[]] $Arguments) {
    $text = ($Arguments | ForEach-Object { ConvertTo-JavaArgument $_ }) -join [Environment]::NewLine
    try { $null = (Get-JavaArgumentEncoding).GetBytes($text + [Environment]::NewLine + $Path) }
    catch [Text.EncoderFallbackException] { throw 'A launch path contains characters unsupported by this Windows Java launcher code page. Move the RE4Craft release/private instance or choose an owned Minecraft installation under a path representable in your Windows system language, then run Setup again. Existing files and saves were preserved.' }
    # Java's native Windows @file reader uses the active ANSI code page, even
    # when Java's default file.encoding is UTF-8. PowerShell 7 Encoding.Default
    # is also UTF-8 and must not determine this format.
    [IO.File]::WriteAllBytes($Path, (Get-JavaArgumentEncoding).GetBytes($text))
}
function Start-BackgroundJava([string] $JavaPath, [string] $ArgumentFile, [string] $InstancePath, [string] $Stdout, [string] $Stderr) {
    Initialize-NativeLauncher
    $console = Join-Path ([IO.Path]::GetDirectoryName($JavaPath)) 'java.exe'
    try { $null = (Get-JavaArgumentEncoding).GetBytes($console) }
    catch [Text.EncoderFallbackException] { throw 'The Java runtime path contains characters unsupported by the Windows Java launcher code page. Choose a Java installation under a path representable in the Windows system language, then run Setup again.' }
    if (-not (Test-Java25 $console)) { throw 'The selected Java runtime lacks a verified x64 bin/java.exe. Run Setup with a complete Java 25.0.4 or newer runtime.' }
    # Inherited disk handles capture errors before Fabric/Log4j without a pipe
    # owned by this PowerShell process. CREATE_NO_WINDOW keeps the client hidden.
    [RE4Craft.NativeLauncher]::Start($console,$ArgumentFile,$InstancePath,$Stdout,$Stderr)
}
function Get-MinecraftExitMessage([object] $Process, [string] $InstancePath, [object] $Logs) {
    $code = $Process.ExitCode
    $unsigned = [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$code),0)
    $message = ('Background Minecraft exited with code {0} (0x{1:X8}).' -f $code,$unsigned)
    if ($Logs) { $message += [Environment]::NewLine + 'Java stderr: ' + $Logs.Stderr + [Environment]::NewLine + 'Java stdout: ' + $Logs.Stdout }
    $message += [Environment]::NewLine + 'Minecraft log (if created): ' + (Assert-ChildPath $InstancePath 'logs/latest.log') + [Environment]::NewLine + 'JVM crash reports (if created): ' + (Assert-ChildPath $InstancePath '.re4craft/hs_err_pid%p.log')
    $message
}
function New-MinecraftArguments([object] $Owned, [object] $Payload, [object] $Settings) {
    if ($Settings.ShutdownToken -notmatch '^[A-Za-z0-9_-]{32,96}$') { throw 'Invalid owned launch shutdown token.' }
    $natives = Assert-ChildPath $Settings.InstancePath '.natives'
    $classpathEntries = @($Payload.FabricLibraries) + @($Owned.Libraries) + @($Owned.Client)
    foreach ($entry in $classpathEntries) { if ($entry.Contains(';')) { throw 'A library path contains a semicolon, which Java treats as a classpath separator. Choose an installation/release path without semicolons and run Setup again; original files were preserved.' } }
    $classpath = $classpathEntries -join ';'
    # Minecraft 26.3's pinned Mojang arguments.jvm requires this exact shadow
    # page count. Keep launch parity; this is not a larger Java stack setting.
    @('-Xms256M','-Xmx2G','-XX:StackShadowPages=32','--enable-native-access=ALL-UNNAMED','--add-exports','java.base/jdk.internal.misc=ALL-UNNAMED',
      ('-XX:ErrorFile=' + (Assert-ChildPath $Settings.InstancePath '.re4craft/hs_err_pid%p.log')),
      '-Dre4craft.background=true','-Dre4craft.testWorld=true','-Dre4craft.voidMirror=true','-Dre4craft.testExitSeconds=0',
      ('-Dre4craft.worldName=' + $Settings.WorldName),'-Dre4craft.grantStarterBow=true',
      ('-Dre4craft.shutdownFile=' + (Assert-ChildPath $Settings.InstancePath '.re4craft/shutdown.request')),
      ('-Dre4craft.shutdownToken=' + $Settings.ShutdownToken),
      '-Dminecraft.launcher.brand=RE4Craft','-Dminecraft.launcher.version=0.2.0-alpha.3',
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
    $allowedJava = @($Settings.JavaPath,(Join-Path ([IO.Path]::GetDirectoryName($Settings.JavaPath)) 'java.exe'))
    if ($Receipt.SchemaVersion -ne 1 -or $Receipt.InstancePath -ne $Settings.InstancePath -or $Receipt.JavaPath -notin $allowedJava) { return $null }
    if ($Receipt.ShutdownToken -notmatch '^[A-Za-z0-9_-]{32,96}$') { return $null }
    if ($Receipt.ArgumentFile -ne (Assert-ChildPath $Settings.InstancePath '.re4craft/launch.args')) { return $null }
    $process = Get-CimInstance Win32_Process -Filter ('ProcessId=' + [int]$Receipt.ProcessId) -ErrorAction SilentlyContinue
    if (-not $process -or -not $process.ExecutablePath -or -not $process.CommandLine -or (Get-FullPath $process.ExecutablePath) -ne $Receipt.JavaPath -or (ConvertTo-CreatedUtc $process.CreationDate) -ne (ConvertTo-CreatedUtc $Receipt.CreatedUtc)) { return $null }
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
