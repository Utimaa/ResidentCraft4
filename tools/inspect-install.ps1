[CmdletBinding()]
param(
    [string] $GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4',
    [string] $MinecraftPath = (Join-Path $env:APPDATA '.minecraft'),
    [switch] $Json
)
. (Join-Path $PSScriptRoot 'common.ps1')

# Inspect only known game binaries and version metadata. Do not enumerate Minecraft
# mods, saves, account files, launcher logs, or arbitrary user content.
$gameRoot = Get-NormalizedPath $GamePath
$bin = Join-Path $gameRoot 'Bin32'
$executables = @()
foreach ($name in @('bio4.exe', 'bio4.exe.bak')) {
    $path = Join-Path $bin $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try { $pe = Get-PeInfo $path } catch { $pe = [pscustomobject]@{ Error = $_.Exception.Message } }
        $executables += [pscustomobject]@{ Name = $name; Bytes = (Get-Item -LiteralPath $path).Length; Sha256 = Get-Sha256 $path; PE = $pe }
    }
}
$proxies = @()
foreach ($name in @('dinput8.dll', 'd3d9.dll', 'dxgi.dll', 'winmm.dll', 'version.dll', 'RE4Craft.asi')) {
    $path = Join-Path $bin $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $proxies += [pscustomobject]@{ Name = $name; Bytes = (Get-Item -LiteralPath $path).Length; Sha256 = Get-Sha256 $path }
    }
}
$versions = @()
$versionInspectionError = $null
$versionRoot = Join-Path (Get-NormalizedPath $MinecraftPath) 'versions'
if (Test-Path -LiteralPath $versionRoot -PathType Container) {
    try { $directories = @(Get-ChildItem -LiteralPath $versionRoot -Directory) } catch { $directories = @(); $versionInspectionError = $_.Exception.Message }
    foreach ($directory in $directories) {
        $metadataPath = Join-Path $directory.FullName ($directory.Name + '.json')
        if (-not (Test-Path -LiteralPath $metadataPath -PathType Leaf)) { continue }
        try {
            $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
            $java = if ($metadata.PSObject.Properties['javaVersion']) { $metadata.javaVersion.majorVersion } else { $null }
            $versions += [pscustomobject]@{
                Folder = $directory.Name
                Id = $metadata.id
                JavaMajor = $java
                JarPresent = Test-Path -LiteralPath (Join-Path $directory.FullName ($directory.Name + '.jar')) -PathType Leaf
            }
        } catch {
            $versions += [pscustomobject]@{ Folder = $directory.Name; MetadataError = $_.Exception.Message }
        }
    }
}
$report = [pscustomobject]@{
    GamePath = $gameRoot
    GameReady = Test-Path -LiteralPath (Join-Path $bin 'bio4.exe') -PathType Leaf
    Executables = $executables
    ProxyDlls = $proxies
    ReShadePresent = ((Test-Path -LiteralPath (Join-Path $bin 'ReShade.ini')) -or (Test-Path -LiteralPath (Join-Path $bin 'reshade-shaders')))
    MinecraftPath = Get-NormalizedPath $MinecraftPath
    MinecraftVersions = $versions
    VersionInspectionError = $versionInspectionError
    Minecraft26_3Present = Test-Path -LiteralPath (Join-Path $versionRoot '26.3\26.3.jar') -PathType Leaf
    ManifestPresent = Test-Path -LiteralPath (Join-Path $bin 'RE4Craft.install.json') -PathType Leaf
}
if ($Json) { $report | ConvertTo-Json -Depth 8 } else { $report | Format-List }
