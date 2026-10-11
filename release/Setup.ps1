[CmdletBinding(SupportsShouldProcess)]
param([string] $GamePath, [string] $MinecraftPath, [string] $JavaPath, [string] $InstancePath, [string] $WorldName)
. (Join-Path $PSScriptRoot 'launcher-common.ps1')
. (Join-Path $PSScriptRoot 'runtime.ps1')
$explicitJava = [bool]$JavaPath
$root = Get-ReleaseRoot
$settingsPath = Assert-ChildPath $root '.re4craft/settings.json'
$old = if (Test-Path -LiteralPath $settingsPath -PathType Leaf) { Read-JsonFile $settingsPath } else { $null }
if ($old -and $old.SchemaVersion -ne 1) { throw 'Unsupported saved launcher settings.' }
if (-not $GamePath) { $GamePath = if ($old) { $old.GamePath } else { Find-RE4Installation } }
if (-not $MinecraftPath) { $MinecraftPath = if ($old) { $old.MinecraftPath } else { Join-Path $env:APPDATA '.minecraft' } }
if (-not $JavaPath -and $old) { $JavaPath = $old.JavaPath }
if (-not $InstancePath) { $InstancePath = if ($old) { $old.InstancePath } else { Join-Path $root '.instance' } }
$game = Get-FullPath $GamePath; $minecraft = Get-FullPath $MinecraftPath; $instance = Get-FullPath $InstancePath
Assert-NoReparse $instance
if ([IO.Path]::GetFileName($instance) -notin @('.instance','.dev-instance') -or $instance -eq $minecraft -or $instance.StartsWith($minecraft+'\',[StringComparison]::OrdinalIgnoreCase) -or $instance.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose a separate .instance folder outside both original game installations.' }
$payload = Read-ReleasePayload $root
$owned = Resolve-OwnedMinecraft $minecraft
Assert-InstanceStopped $instance
$newSelected = $old -and $old.InstancePath -eq $instance -and $old.AllowNewWorld
if ($newSelected) {
    $savedSelection = Read-JsonFile (Assert-ChildPath $instance 'RE4Craft.world.json')
    $newSelected = $savedSelection.WorldName -eq $old.WorldName
}
$world = Read-WorldSelection $instance $WorldName ([bool]$newSelected)
# A new selection written by a previous Setup is permitted before the first launch.
$allowNew = $world.New
if ($old -and $old.InstancePath -eq $instance -and $old.WorldName -eq $world.Name) { $allowNew = $old.AllowNewWorld -and $world.New }
if (-not $PSCmdlet.ShouldProcess($game,'Install RE4Craft and prepare its private background Minecraft profile')) { return }
$java = Ensure-ReleaseJava $root $minecraft $JavaPath $explicitJava
$settings = [pscustomobject]@{SchemaVersion=1;ReleaseVersion='0.2.0-alpha.2';GamePath=$game;MinecraftPath=$minecraft;JavaPath=$java;InstancePath=$instance;WorldName=$world.Name;AllowNewWorld=[bool]$allowNew}
& (Join-Path $root 'tools/install.ps1') -GamePath $game -AsiPath (Join-Path $payload.Path 'RE4Craft.asi') -ConfigPath (Join-Path $payload.Path 'RE4Craft.ini') -LoaderPath (Join-Path $payload.Path 'winmm.dll')
Install-SourceMods $payload $instance
if (-not $world.SelectionExists) { Write-ReleaseJson ([pscustomobject]@{SchemaVersion=1;WorldName=$world.Name}) (Assert-ChildPath $instance 'RE4Craft.world.json') }
Write-ReleaseJson $settings $settingsPath
Write-Host 'Setup complete. Double-click Play.cmd, load your RE4 save, then press F8.'
Write-Host 'Minecraft runs in the background. Your original Minecraft mods and worlds are untouched.'
