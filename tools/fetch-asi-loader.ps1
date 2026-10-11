[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'common.ps1')
$projectRoot = Get-RE4CraftRoot
$cache = Join-Path $projectRoot '.tools\asi-loader'
$archive = Join-Path $cache 'Ultimate-ASI-Loader-v9.7.4-x86.zip'
$url = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader-NoPDB.zip'
$archiveHash = '14b3a1ad018899571ac9aa01482977f3c6d49e6cba99f552d01c5acacd1315e1'
$binaryHash = 'ec2f4824eca58dd40f425756a4a7cec77b8e381f21d11d7c846ec4b339b617ab'
New-Item -ItemType Directory -Path $cache -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest $url -OutFile $archive }
if ((Get-Sha256 $archive) -ne $archiveHash) { throw 'Pinned ASI Loader archive SHA256 mismatch. Cache preserved for inspection.' }
$extract = Join-Path $cache 'extracted'
Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
$binary = Join-Path $extract 'dinput8.dll'
if ((Get-Sha256 $binary) -ne $binaryHash -or -not (Get-PeInfo $binary).IsX86) { throw 'Unexpected ASI Loader binary.' }
# The official loader is a universal proxy. RE4 UHD imports winmm.dll.
# Copy under that name in the cache; leave game proxy DLLs untouched.
Copy-Item -LiteralPath $binary -Destination (Join-Path $cache 'winmm.dll') -Force
Invoke-WebRequest 'https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/v9.7.4/license' -OutFile (Join-Path $cache 'LICENSE')
Write-Host 'Pinned ASI Loader v9.7.4 x86 cached. No game files were changed.'
