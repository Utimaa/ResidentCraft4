[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'common.ps1')
$projectRoot = Get-RE4CraftRoot
$dependencies = @(
    [pscustomobject]@{
        RelativePath = '.tools\fabric-api\fabric-api-0.161.0+26.3.jar'
        Url = 'https://maven.fabricmc.net/net/fabricmc/fabric-api/fabric-api/0.161.0+26.3/fabric-api-0.161.0+26.3.jar'
        Sha256 = '86f16178a3cecc887a85a4cfe9a79d92fa7341d8f39b5951a4d6ad800ab657a6'
    },
    [pscustomobject]@{
        RelativePath = '.tools\fabric-installer\fabric-installer-1.1.2.jar'
        Url = 'https://maven.fabricmc.net/net/fabricmc/fabric-installer/1.1.2/fabric-installer-1.1.2.jar'
        Sha256 = '61e035bf7bf70153e127440ce34de47c9036f0a2d0c65d1529454bd35ceefe4f'
    }
)
foreach ($dependency in $dependencies) {
    $path = Join-Path $projectRoot $dependency.RelativePath
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($path)) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $path)) { Invoke-WebRequest $dependency.Url -OutFile $path }
    if ((Get-Sha256 $path) -ne $dependency.Sha256) { throw "Pinned Fabric dependency SHA256 mismatch: $path" }
}
Write-Host 'Pinned official Fabric API and installer cached. No Minecraft installation or accounts were changed.'
