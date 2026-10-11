# Private portable Java fallback: never changes the launcher, registry or global Java.
$script:RuntimeArchiveName='OpenJDK25U-jre_x64_windows_hotspot_25.0.4.1_1.zip'
$script:RuntimeArchiveHash='4c95451cea98556def2c54f7782933f52a26d4a36bd85e1d59f0364464828b07'
$script:RuntimeDirectoryName='jre-25.0.4.1+1'
$script:RuntimeArchiveDirectoryName='jdk-25.0.4.1+1-jre'
$script:RuntimeArchiveUrl='https://github.com/adoptium/temurin25-binaries/releases/download/jdk-25.0.4.1%2B1/OpenJDK25U-jre_x64_windows_hotspot_25.0.4.1_1.zip'
function Test-RuntimeArchivePaths([string] $ArchivePath) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($ArchivePath)
    $seen=@{}; $total=0L; $entries=@()
    try {
        if($archive.Entries.Count -gt 4096){throw 'Runtime archive has too many entries.'}
        foreach($entry in $archive.Entries){
            $name=$entry.FullName
            if($name -notmatch ('^'+[regex]::Escape($script:RuntimeArchiveDirectoryName)+'/') -or $name -match '(^|[\\/])\.\.([\\/]|$)|:|\\|[\r\n]' -or $seen.ContainsKey($name)){throw 'Unsafe or duplicate runtime archive entry.'}
            $attributes=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$entry.ExternalAttributes),0)
            if((($attributes -shr 16) -band 0xf000) -eq 0xa000){throw 'Runtime archive contains a symbolic link.'}
            $seen[$name]=$true; $total+=$entry.Length
            if($total -gt 256MB){throw 'Runtime archive is too large.'}
            $entries+=$name
        }
        if(-not $seen.ContainsKey($script:RuntimeArchiveDirectoryName+'/bin/javaw.exe') -or -not $seen.ContainsKey($script:RuntimeArchiveDirectoryName+'/release')){throw 'Runtime archive is incomplete.'}
    } finally {$archive.Dispose()}
    $entries
}
function Read-PrivateRuntime([string] $Root) {
    $base=Assert-ChildPath $Root '.re4craft/runtime'
    $manifestPath=Assert-ChildPath $base 'runtime.json'
    $target=Assert-ChildPath $base $script:RuntimeDirectoryName
    if(-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)){
        if(Test-Path -LiteralPath $target){throw 'An untracked private Java runtime exists and was preserved.'}
        return $null
    }
    $manifest=Read-JsonFile $manifestPath
    if($manifest.SchemaVersion -ne 1 -or $manifest.ArchiveSha256 -ne $script:RuntimeArchiveHash -or $manifest.Directory -ne $script:RuntimeDirectoryName){throw 'Private Java ownership manifest is invalid.'}
    $seen=@{}
    foreach($entry in $manifest.Files){
        if($seen.ContainsKey($entry.Path)){throw 'Duplicate private runtime ownership entry.'}
        $seen[$entry.Path]=$true
        Assert-FileHash (Assert-ChildPath $target $entry.Path) $entry.Sha256
    }
    if(-not $seen.ContainsKey('bin/javaw.exe') -or -not $seen.ContainsKey('release')){throw 'Private runtime ownership is incomplete.'}
    $java=Assert-ChildPath $target 'bin/javaw.exe'
    if(-not (Test-Java25 $java)){throw 'Private Java runtime is unsupported and was preserved.'}
    $java
}
function Install-PortableRuntime([string] $Root, [string] $CachedArchive) {
    $existing=Read-PrivateRuntime $Root
    if($existing){return $existing}
    $base=Assert-ChildPath $Root '.re4craft/runtime'
    New-Item -ItemType Directory -Path $base -Force | Out-Null
    $archivePath=Assert-ChildPath $base $script:RuntimeArchiveName
    if(Test-Path -LiteralPath $archivePath -PathType Leaf){Assert-FileHash $archivePath $script:RuntimeArchiveHash}
    elseif($CachedArchive){Assert-FileHash $CachedArchive $script:RuntimeArchiveHash; Copy-Item -LiteralPath $CachedArchive -Destination $archivePath}
    else {
        $partial=Assert-ChildPath $base ($script:RuntimeArchiveName+'.'+[Guid]::NewGuid().ToString('N')+'.partial')
        Write-Host 'Downloading the pinned portable Java runtime (about 58 MB). Your global Java installation stays unchanged.'
        try {Invoke-WebRequest -Uri $script:RuntimeArchiveUrl -OutFile $partial; Assert-FileHash $partial $script:RuntimeArchiveHash; Move-Item -LiteralPath $partial -Destination $archivePath}
        finally {if(Test-Path -LiteralPath $partial){Remove-Item -LiteralPath $partial}}
    }
    Assert-FileHash $archivePath $script:RuntimeArchiveHash
    $null=Test-RuntimeArchivePaths $archivePath
    $stage=Assert-ChildPath $base ('stage-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $archive=[IO.Compression.ZipFile]::OpenRead($archivePath)
    try {foreach($entry in $archive.Entries){
        $destination=Assert-ChildPath $stage $entry.FullName
        if($entry.FullName.EndsWith('/')){New-Item -ItemType Directory -Path $destination -Force | Out-Null;continue}
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        $inputStream=$entry.Open();$outputStream=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {$inputStream.CopyTo($outputStream)} finally {$outputStream.Dispose();$inputStream.Dispose()}
    }} finally {$archive.Dispose()}
    $stagedRuntime=Assert-ChildPath $stage $script:RuntimeArchiveDirectoryName
    $java=Assert-ChildPath $stagedRuntime 'bin/javaw.exe'
    if(-not (Test-Java25 $java)){throw 'Verified archive did not contain the expected x64 patched Java. Staging was retained.'}
    $files=@(Get-ChildItem -LiteralPath $stagedRuntime -Recurse -File | ForEach-Object {[pscustomobject]@{Path=$_.FullName.Substring($stagedRuntime.Length+1).Replace('\','/');Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
    $target=Assert-ChildPath $base $script:RuntimeDirectoryName
    if(Test-Path -LiteralPath $target){throw 'Private runtime destination appeared during extraction; it was preserved.'}
    # Both move endpoints were normalized and checked within this private root.
    Move-Item -LiteralPath $stagedRuntime -Destination $target
    Write-ReleaseJson ([pscustomobject]@{SchemaVersion=1;ArchiveSha256=$script:RuntimeArchiveHash;Directory=$script:RuntimeDirectoryName;Files=$files}) (Assert-ChildPath $base 'runtime.json')
    Read-PrivateRuntime $Root
}
function Ensure-ReleaseJava([string] $Root,[string] $MinecraftPath,[string] $JavaPath,[bool] $ExplicitJavaPath=$false,[string] $CachedArchive=''){
    if($ExplicitJavaPath){return Resolve-Java25 $MinecraftPath $JavaPath}
    $private=Read-PrivateRuntime $Root
    if($private){return $private}
    if($JavaPath -and (Test-Java25 $JavaPath)){return Get-FullPath $JavaPath}
    try {return Resolve-Java25 $MinecraftPath ''} catch { }
    Install-PortableRuntime $Root $CachedArchive
}
