[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cache=Join-Path $root '.tools\release-fabric'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$pins=@(
    @('asm','9.10.1','org/ow2/asm','ed825d10ab1399c8c0cb669e688cf0c8c82629b4c8399b58352b68e92ca10fcb'),
    @('asm-analysis','9.10.1','org/ow2/asm','dede75a21306b65974ecd8f87114ff6970f09fb794157a4ca09ab25c888c2bfc'),
    @('asm-commons','9.10.1','org/ow2/asm','6d0abefb7cbf972ea16edb37ec14835372505063a45f976ab7ea889ed9497895'),
    @('asm-tree','9.10.1','org/ow2/asm','3dfb0d5b6a106cd40b5b250e39935fbf2f927f4477546a5369a3ac609cf0506b'),
    @('asm-util','9.10.1','org/ow2/asm','1bb99d091fba2597dc6d51193e9bbcf0d8447e7ed96bd8f0198b18152f09655c'),
    @('sponge-mixin','0.17.4+mixin.0.8.7','net/fabricmc','1f0ae44db7295f8626f33b1dc0ad7f043d8954a8d6847247875fcc5dfcecc934'),
    @('fabric-loader','0.19.5','net/fabricmc','93044e4dd46de5d8136701292f05e868da096d2c9fddb4793e4fdbcc63efc695')
)
$libraries=@()
foreach($pin in $pins) {
    $filename=$pin[0]+'-'+$pin[1]+'.jar'
    $url='https://maven.fabricmc.net/'+$pin[2]+'/'+$pin[0]+'/'+$pin[1]+'/'+$filename
    $target=Join-Path $cache $filename
    if (-not (Test-Path -LiteralPath $target)) { Invoke-WebRequest -Uri $url -OutFile $target }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $pin[3]) { throw "Pinned official dependency mismatch: $filename" }
    $libraries+=[pscustomobject]@{Path=('fabric-libraries/'+$filename);Sha256=$pin[3];Url=$url}
}
$description=[pscustomobject]@{SchemaVersion=1;MinecraftVersion='26.3';FabricLoader='0.19.5';MainClass='net.fabricmc.loader.impl.launch.knot.KnotClient';Libraries=$libraries}
$metadata=Join-Path $root 'build\release-preparation\fabric-launch.json'
New-Item -ItemType Directory -Path (Split-Path -Parent $metadata) -Force | Out-Null
[IO.File]::WriteAllText($metadata,($description | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Host 'Pinned redistributable Fabric launch libraries cached. No original game files downloaded.'
