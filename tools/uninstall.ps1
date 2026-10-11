[CmdletBinding(SupportsShouldProcess)]
param([string] $GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4')
. (Join-Path $PSScriptRoot 'common.ps1')
$gameRoot = Get-NormalizedPath $GamePath
$bin = Join-Path $gameRoot 'Bin32'
Assert-GameStopped (Join-Path $bin 'bio4.exe')
$manifestPath = Join-Path $bin 'RE4Craft.install.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "No RE4Craft install manifest at $manifestPath. No files were removed." }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-InstallManifest $manifest $gameRoot
$allowedNames = @('RE4Craft.asi', 'RE4Craft.ini', 'winmm.dll')
$retained = @()
$removable = @()
foreach ($entry in $manifest.Files) {
    if ($entry.Name -notin $allowedNames) { throw "Unexpected manifest filename: $($entry.Name)" }
    $path = Assert-PathWithin (Join-Path $bin $entry.Name) $bin
    if (-not $entry.Owned -or -not (Test-Path -LiteralPath $path)) { continue }
    $item = Get-Item -LiteralPath $path
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or (Get-Sha256 $path) -ne $entry.Sha256) {
        $retained += $entry
        Write-Warning "Preserving modified or non-regular file: $path"
    } else { $removable += [pscustomobject]@{ Entry = $entry; Path = $path } }
}
if (-not $PSCmdlet.ShouldProcess($bin, 'Remove unchanged RE4Craft-owned files')) { return }
foreach ($item in $removable) {
    # Recheck immediately before removal; never remove a changed file.
    if ((Get-Sha256 $item.Path) -eq $item.Entry.Sha256) { Remove-Item -LiteralPath $item.Path }
    else { $retained += $item.Entry; Write-Warning "Preserving file changed during uninstall: $($item.Path)" }
}
if ($retained.Count -gt 0) {
    $manifest.Files = $retained
    Write-AtomicJson $manifest $manifestPath
    Write-Host 'Unchanged owned files removed. Modified files and their manifest have been retained.'
} else {
    Remove-Item -LiteralPath $manifestPath
    Write-Host 'RE4Craft-owned files removed. Pre-existing files were preserved.'
}
