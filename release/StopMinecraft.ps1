[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'launcher-common.ps1')
$root = Get-ReleaseRoot
$settings = Read-JsonFile (Assert-ChildPath $root '.re4craft/settings.json')
if ($settings.SchemaVersion -ne 1) { throw 'Invalid launcher settings.' }
Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
$receiptPath = Assert-ChildPath $settings.InstancePath '.re4craft/source-process.json'
if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { Write-Host 'No owned background Minecraft process.'; return }
$receipt = Read-JsonFile $receiptPath
if (-not (Get-OwnedSourceProcess $receipt $settings)) { Write-Host 'The recorded Minecraft process is no longer running. No other process was stopped.'; return }
$shutdown = Assert-ChildPath $settings.InstancePath '.re4craft/shutdown.request'
[IO.File]::WriteAllText($shutdown,$receipt.ShutdownToken,[Text.UTF8Encoding]::new($false))
$deadline = [DateTime]::UtcNow.AddSeconds(30)
while ((Get-OwnedSourceProcess $receipt $settings) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 250 }
if (Get-OwnedSourceProcess $receipt $settings) { throw 'Minecraft has not finished saving yet. It was left running; check its log rather than force-closing it.' }
Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
Write-Host 'Background Minecraft saved and stopped.'
