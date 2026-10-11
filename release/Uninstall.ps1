[CmdletBinding(SupportsShouldProcess)]
param()
. (Join-Path $PSScriptRoot 'launcher-common.ps1')
$root = Get-ReleaseRoot
$settings = Read-JsonFile (Assert-ChildPath $root '.re4craft/settings.json')
if ($settings.SchemaVersion -ne 1) { throw 'Invalid launcher settings.' }
Assert-InstanceStopped $settings.InstancePath
if (-not $PSCmdlet.ShouldProcess($settings.GamePath,'Remove unchanged RE4Craft files and preserve saves')) { return }
& (Join-Path $root 'tools/uninstall.ps1') -GamePath $settings.GamePath
$manifestPath = Assert-ChildPath $settings.InstancePath 'RE4Craft.instance.json'
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $manifest = Read-JsonFile $manifestPath
    if ($manifest.SchemaVersion -ne 2 -or $manifest.InstancePath -ne $settings.InstancePath) { throw 'Invalid source ownership manifest; source files were left untouched.' }
    $retained = @()
    foreach ($entry in $manifest.Files) {
        if ($entry.Name -notin @('re4craft-fabric.jar','fabric-api.jar') -or $entry.Owned -isnot [bool] -or $entry.Sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid source manifest entry.' }
        $path = Assert-ChildPath $settings.InstancePath ('mods/' + $entry.Name)
        if ($entry.Owned -and (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $entry.Sha256) { Remove-Item -LiteralPath $path }
        else { $retained += $entry }
    }
    $manifest.Files = $retained; Write-ReleaseJson $manifest $manifestPath
}
Write-Host 'RE4Craft uninstalled. Your private world, settings and backups were kept.'
