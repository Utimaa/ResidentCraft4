[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4',
    [string] $AsiPath,
    [string] $ConfigPath,
    [string] $LoaderPath
)
. (Join-Path $PSScriptRoot 'common.ps1')
$projectRoot = Get-RE4CraftRoot
if (-not $AsiPath) { $AsiPath = Join-Path $projectRoot 'build\native\RE4Craft.asi' }
if (-not $ConfigPath) { $ConfigPath = Join-Path $projectRoot 'RE4Craft.ini' }
if (-not $LoaderPath) { $LoaderPath = Join-Path $projectRoot '.tools\asi-loader\winmm.dll' }
$gameRoot = Get-NormalizedPath $GamePath
$bin = Join-Path $gameRoot 'Bin32'
$executable = Join-Path $bin 'bio4.exe'
$manifestPath = Join-Path $bin 'RE4Craft.install.json'
Assert-GameStopped $executable
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "No bio4.exe at $executable. Locate or repair the original RE4 UHD installation first." }
if (-not (Get-PeInfo $executable).IsX86) { throw 'RE4Craft targets the x86 original RE4 UHD, not RE4 Remake.' }
foreach ($source in @($AsiPath, $ConfigPath, $LoaderPath)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing build/dependency: $source" }
}
if (-not (Get-PeInfo $AsiPath).IsX86 -or -not (Get-PeInfo $LoaderPath).IsX86) { throw 'ASI and loader must both be x86 PE binaries.' }
$expectedLoaderHash = 'ec2f4824eca58dd40f425756a4a7cec77b8e381f21d11d7c846ec4b339b617ab'
if ((Get-Sha256 $LoaderPath) -ne $expectedLoaderHash) { throw 'Loader hash differs from the pinned official Ultimate ASI Loader v9.7.4 x86 binary.' }
$oldManifest = $null
if (Test-Path -LiteralPath $manifestPath) {
    $oldManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-InstallManifest $oldManifest $gameRoot
}
$sources = @(
    [pscustomobject]@{ Name = 'RE4Craft.asi'; Source = $AsiPath; PreserveConfig = $false },
    [pscustomobject]@{ Name = 'RE4Craft.ini'; Source = $ConfigPath; PreserveConfig = $true },
    [pscustomobject]@{ Name = 'winmm.dll'; Source = $LoaderPath; PreserveConfig = $false }
)
$operations = @()
$entries = @()
foreach ($item in $sources) {
    $destination = Assert-PathWithin (Join-Path $bin $item.Name) $bin
    $sourceHash = Get-Sha256 $item.Source
    $previous = if ($oldManifest) { @($oldManifest.Files | Where-Object Name -eq $item.Name) | Select-Object -First 1 } else { $null }
    if (Test-Path -LiteralPath $destination) {
        $existing = Get-Item -LiteralPath $destination
        if ($existing.PSIsContainer -or ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing non-regular destination: $destination" }
        $currentHash = Get-Sha256 $destination
        if ($item.PreserveConfig) {
            # Retain custom settings, including changes made after installation.
            $owned = $null -ne $previous -and $previous.Owned
            $trackedHash = if ($owned) { $previous.Sha256 } else { $currentHash }
            $entries += [pscustomobject]@{ Name = $item.Name; Owned = $owned; Sha256 = $trackedHash }
            continue
        }
        if ($currentHash -eq $sourceHash) {
            $entries += [pscustomobject]@{ Name = $item.Name; Owned = ($null -ne $previous -and $previous.Owned); Sha256 = $sourceHash }
            continue
        }
        if (-not $previous -or -not $previous.Owned -or $currentHash -ne $previous.Sha256) {
            throw "Existing $($item.Name) is not an unchanged file owned by RE4Craft. It has been left untouched. Resolve the conflict manually before installing."
        }
        $operations += [pscustomobject]@{ Source = $item.Source; Destination = $destination; Existed = $true; Backup = $null; Sha256 = $sourceHash }
    } else {
        $operations += [pscustomobject]@{ Source = $item.Source; Destination = $destination; Existed = $false; Backup = $null; Sha256 = $sourceHash }
    }
    $entries += [pscustomobject]@{ Name = $item.Name; Owned = $true; Sha256 = $sourceHash }
}
if (-not $PSCmdlet.ShouldProcess($bin, 'Install RE4Craft ASI, settings and pinned ASI Loader')) { return }
$backupDirectory = Join-Path $projectRoot ('backups\install-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
$completed = @()
try {
    if ($oldManifest) { Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $backupDirectory 'RE4Craft.install.json') }
    foreach ($operation in $operations) {
        if ($operation.Existed) {
            $operation.Backup = Assert-PathWithin (Join-Path $backupDirectory ([IO.Path]::GetFileName($operation.Destination))) $backupDirectory
            Copy-Item -LiteralPath $operation.Destination -Destination $operation.Backup
        }
        # Add before copy so a partial copy is also rolled back on an error.
        $completed += $operation
        Copy-Item -LiteralPath $operation.Source -Destination $operation.Destination -Force
        if ((Get-Sha256 $operation.Destination) -ne $operation.Sha256) { throw "Post-copy hash mismatch: $($operation.Destination)" }
    }
    $manifest = [pscustomobject]@{
        SchemaVersion = 1
        GamePath = $gameRoot
        InstalledUtc = [DateTime]::UtcNow.ToString('o')
        ExecutableSha256 = Get-Sha256 $executable
        BackupDirectory = $backupDirectory
        Files = $entries
    }
    Write-AtomicJson $manifest $manifestPath
} catch {
    foreach ($operation in $completed) {
        if ($operation.Existed) { Copy-Item -LiteralPath $operation.Backup -Destination $operation.Destination -Force }
        elseif (Test-Path -LiteralPath $operation.Destination -PathType Leaf) { Remove-Item -LiteralPath $operation.Destination }
    }
    throw
}
Write-Host "Installed RE4Craft into $bin. Existing dinput8.dll/ReShade files were not changed."
Write-Host 'The bridge starts disabled. F8 selects the Minecraft instance; F7 selects the isolated BeamNG Covet instance.'
