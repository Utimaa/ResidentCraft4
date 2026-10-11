[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $SeedLog,
    [Parameter(Mandatory)][string] $ReloadLog,
    [Parameter(Mandatory)][string] $SavedWorld,
    [Parameter(Mandatory)][string] $PythonPath,
    [string] $Output = (Join-Path $PSScriptRoot '..\build\chapter-loot-saved-engine-proof.json')
)
$ErrorActionPreference='Stop'
# The old five-tier Gradle/game launcher is retired. This helper only validates
# the separately run guarded seed/check fixture and reads its saved NBT.
& $PythonPath (Join-Path $PSScriptRoot 'probe_loot.py') --seed-log $SeedLog --reload-log $ReloadLog --saved-world $SavedWorld --output $Output
if($LASTEXITCODE -ne 0){throw 'Actual saved chapter loot proof failed.'}
