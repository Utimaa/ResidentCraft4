Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RE4CraftRoot {
    return [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
}

function Get-NormalizedPath([string] $Path) {
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Assert-PathWithin([string] $Path, [string] $Root) {
    $resolved = Get-NormalizedPath $Path
    $boundary = (Get-NormalizedPath $Root) + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside expected directory: $resolved"
    }
    return $resolved
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-PortableJavaHome([string] $ProjectRoot) {
    $jdkRoot = Join-Path $ProjectRoot '.tools\jdk-25'
    if (Test-Path -LiteralPath (Join-Path $jdkRoot 'bin\java.exe')) { return $jdkRoot }
    if (Test-Path -LiteralPath $jdkRoot) {
        $candidates = @(Get-ChildItem -LiteralPath $jdkRoot -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\java.exe') })
        if ($candidates.Count -eq 1) { return $candidates[0].FullName }
    }
    throw 'Portable JDK25 not found in .tools/jdk-25. See README setup.'
}

function Get-PeInfo([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'Not a PE executable.' }
        $stream.Position = 0x3c
        $offset = $reader.ReadUInt32()
        if ([long] $offset + 24 -gt $stream.Length) { throw 'Invalid PE header offset.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid PE signature.' }
        $machine = $reader.ReadUInt16()
        $sections = $reader.ReadUInt16()
        $stamp = $reader.ReadUInt32()
        return [pscustomobject]@{ Machine = ('0x{0:x4}' -f $machine); IsX86 = $machine -eq 0x14c; Sections = $sections; TimeDateStamp = $stamp }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Assert-GameStopped([string] $ExecutablePath) {
    $running = @(Get-Process -Name bio4 -ErrorAction SilentlyContinue)
    foreach ($process in $running) {
        $processPath = $null
        try { $processPath = $process.Path } catch { }
        # Fail closed when Windows does not permit process-path inspection.
        if (-not $ExecutablePath -or -not $processPath -or (Get-NormalizedPath $processPath) -eq (Get-NormalizedPath $ExecutablePath)) {
            throw 'Close this Resident Evil 4 installation before installing or removing RE4Craft.'
        }
    }
}

function Assert-InstallManifest([object] $Manifest, [string] $GameRoot) {
    if ($Manifest.SchemaVersion -ne 1 -or (Get-NormalizedPath $Manifest.GamePath) -ne $GameRoot) { throw 'Invalid install manifest location or schema.' }
    $seen = @{}
    foreach ($entry in $Manifest.Files) {
        if ($entry.Name -notin @('RE4Craft.asi', 'RE4Craft.ini', 'winmm.dll') -or $seen.ContainsKey($entry.Name)) { throw "Invalid or duplicate manifest filename: $($entry.Name)" }
        if ($entry.Owned -isnot [bool] -or $entry.Sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Invalid manifest ownership/hash for $($entry.Name)" }
        $seen[$entry.Name] = $true
    }
}

function Write-AtomicJson([object] $Value, [string] $Path) {
    $temporary = $Path + '.new'
    if (Test-Path -LiteralPath $temporary) { throw "Temporary manifest already exists: $temporary" }
    try {
        if (Test-Path -LiteralPath $Path) {
            $item = Get-Item -LiteralPath $Path
            if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing non-regular manifest: $Path" }
        }
        $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding UTF8
        Move-Item -LiteralPath $temporary -Destination $Path -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
