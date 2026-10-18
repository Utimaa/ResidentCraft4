[CmdletBinding()]
param([switch] $NoRE4, [switch] $NoMonitor)
. (Join-Path $PSScriptRoot 'launcher-common.ps1')
. (Join-Path $PSScriptRoot 'runtime.ps1')
trap {
    $message = $_.Exception.Message
    $errorPath = Assert-ChildPath (Get-ReleaseRoot) '.re4craft/last-error.txt'
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($errorPath)) -Force | Out-Null
    Set-Content -LiteralPath $errorPath -Value $message -Encoding UTF8
    if($NoRE4){Write-Error $message;exit 1}
    Add-Type -AssemblyName System.Windows.Forms
    [void][System.Windows.Forms.MessageBox]::Show($message + [Environment]::NewLine + 'Launcher error record: ' + $errorPath,'RE4Craft')
    exit 1
}
$root = Get-ReleaseRoot
$settings = Read-JsonFile (Assert-ChildPath $root '.re4craft/settings.json')
if ($settings.SchemaVersion -ne 1 -or $settings.ReleaseVersion -ne '0.2.0-alpha.3') { throw 'Run Setup.cmd first.' }
$instance = Get-FullPath $settings.InstancePath
if ([IO.Path]::GetFileName($instance) -notin @('.instance','.dev-instance')) { throw 'Invalid private instance.' }
$hasher = [Security.Cryptography.SHA256]::Create()
try { $instanceKey = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($instance.ToLowerInvariant()))).Replace('-','') }
finally { $hasher.Dispose() }
$launchMutex = [Threading.Mutex]::new($false,('Local\RE4Craft-' + $instanceKey))
if (-not $launchMutex.WaitOne(0)) { Write-Host 'This background bridge is already supervised. Load your RE4 save and press F8.'; return }
$selection = Read-JsonFile (Assert-ChildPath $instance 'RE4Craft.world.json')
if ($selection.SchemaVersion -ne 1 -or $selection.WorldName -ne $settings.WorldName -or $settings.WorldName -notmatch '^[A-Za-z0-9][A-Za-z0-9_-]{0,95}$') { throw 'World selection changed. Run Setup to review it; no save was reset.' }
$world = Assert-ChildPath $instance ('saves/' + $settings.WorldName + '/level.dat')
if (-not (Test-Path -LiteralPath $world -PathType Leaf) -and -not $settings.AllowNewWorld) { throw 'The selected existing world is missing. Restore it before launching.' }
Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
$payload = Read-ReleasePayload $root
$owned = Resolve-OwnedMinecraft $settings.MinecraftPath $false $root
$settings.JavaPath = Resolve-Java25 $settings.MinecraftPath $settings.JavaPath
if($settings.JavaPath.StartsWith((Assert-ChildPath $root '.re4craft/runtime')+'\',[StringComparison]::OrdinalIgnoreCase)){
    $verifiedJava=Read-PrivateRuntime $root
    if(-not $verifiedJava -or $verifiedJava -ne $settings.JavaPath){throw 'Private runtime ownership changed. Run Setup to review it.'}
}
$sourceManifest = Read-JsonFile (Assert-ChildPath $instance 'RE4Craft.instance.json')
if ($sourceManifest.SchemaVersion -ne 2 -or $sourceManifest.InstancePath -ne $instance) { throw 'Run Setup again to prepare this private source profile.' }
$sourceSeen=@{}
foreach ($entry in $sourceManifest.Files) {
    if ($entry.Name -notin @('re4craft-fabric.jar','fabric-api.jar') -or $sourceSeen.ContainsKey($entry.Name)) { throw 'Invalid source mod filename.' }
    $sourceSeen[$entry.Name]=$true
    $expected=@($payload.Manifest.Files | Where-Object Path -eq $entry.Name) | Select-Object -First 1
    if ($entry.Sha256 -ne $expected.Sha256) { throw 'The prepared source mod is from another release. Run Setup again.' }
    Assert-FileHash (Assert-ChildPath $instance ('mods/' + $entry.Name)) $entry.Sha256
}
if ($sourceSeen.Count -ne 2) { throw 'The source instance is incomplete. Run Setup again.' }
$receiptPath = Assert-ChildPath $instance '.re4craft/source-process.json'
$source = $null
if (Test-Path -LiteralPath $receiptPath -PathType Leaf) { $source = Get-OwnedSourceProcess (Read-JsonFile $receiptPath) $settings }
if (-not $source) {
    Assert-InstanceStopped $instance
    $argumentFile = Assert-ChildPath $instance '.re4craft/launch.args'
    $shutdown = Assert-ChildPath $instance '.re4craft/shutdown.request'
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($argumentFile)) -Force | Out-Null
    if (Test-Path -LiteralPath $shutdown -PathType Leaf) { Remove-Item -LiteralPath $shutdown }
    $settings | Add-Member -MemberType NoteProperty -Name ShutdownToken -Value ([Guid]::NewGuid().ToString('N')) -Force
    $arguments = New-MinecraftArguments $owned $payload $settings
    Write-JavaArgumentFile $argumentFile $arguments
    $launchId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $logs = [pscustomobject]@{Stdout=(Assert-ChildPath $instance ('.re4craft/java-' + $launchId + '.stdout.log'));Stderr=(Assert-ChildPath $instance ('.re4craft/java-' + $launchId + '.stderr.log'))}
    Write-ReleaseJson ([pscustomobject]@{SchemaVersion=1;StartedUtc=[DateTime]::UtcNow.ToString('o');JavaPath=(Join-Path ([IO.Path]::GetDirectoryName($settings.JavaPath)) 'java.exe');InstancePath=$instance;MinecraftRoot=$owned.Root;SourceLayout=$owned.Layout;ArgumentCodePage=(Get-JavaArgumentEncoding).CodePage;Stdout=$logs.Stdout;Stderr=$logs.Stderr}) (Assert-ChildPath $instance '.re4craft/last-launch.json')
    try { $process = Start-BackgroundJava $settings.JavaPath $argumentFile $instance $logs.Stdout $logs.Stderr }
    catch { throw ('Unable to start background Java: ' + $_.Exception.Message + [Environment]::NewLine + 'Java stderr: ' + $logs.Stderr + [Environment]::NewLine + 'Java stdout: ' + $logs.Stdout) }
    Start-Sleep -Milliseconds 500
    if ($process.HasExited) { throw (Get-MinecraftExitMessage $process $instance $logs) }
    $source = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $process.Id)
    if (-not $source -or $source.ExecutablePath -ne (Join-Path ([IO.Path]::GetDirectoryName($settings.JavaPath)) 'java.exe')) {
        if ($process.HasExited) { throw (Get-MinecraftExitMessage $process $instance $logs) }
        throw ('The Minecraft background process could not be verified. Java stderr: ' + $logs.Stderr)
    }
    $receipt = [pscustomobject]@{SchemaVersion=1;ProcessId=$source.ProcessId;CreatedUtc=$source.CreationDate.ToUniversalTime().ToString('o');JavaPath=$source.ExecutablePath;InstancePath=$instance;ArgumentFile=$argumentFile;ShutdownToken=$settings.ShutdownToken;Stdout=$logs.Stdout;Stderr=$logs.Stderr}
    Write-ReleaseJson $receipt $receiptPath
} else {
    $receipt = Read-JsonFile $receiptPath; $process = [Diagnostics.Process]::GetProcessById([int]$receipt.ProcessId)
    $logs = if ($receipt.PSObject.Properties['Stderr'] -and $receipt.PSObject.Properties['Stdout']) { $receipt } else { $null }
}
$retainedHandle = $process.Handle
if (-not (Get-OwnedSourceProcess $receipt $settings)) { throw 'The Minecraft process changed during startup. No other process was managed.' }
if (-not $NoRE4) {
    $runningExecutable = Get-FullPath (Join-Path $settings.GamePath 'Bin32/bio4.exe')
    $alreadyRunning = @(Get-CimInstance Win32_Process -Filter "Name='bio4.exe'" | Where-Object { $_.ExecutablePath -and (Get-FullPath $_.ExecutablePath) -eq $runningExecutable })
    if (-not $alreadyRunning.Count) { Start-Process 'steam://rungameid/254700' }
}
Write-Host 'Minecraft is starting in the background. Once its world loads, load your RE4 save and press F8.'
if ($NoMonitor) { return }
if ($NoRE4) {
    $process.WaitForExit()
    if($process.ExitCode -ne 0){throw (Get-MinecraftExitMessage $process $instance $logs)}
    Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
    return
}
$executable = Get-FullPath (Join-Path $settings.GamePath 'Bin32/bio4.exe')
$game = $null; $deadline = [DateTime]::UtcNow.AddSeconds(120)
while (-not $game -and [DateTime]::UtcNow -lt $deadline) {
    if ($process.HasExited) { throw (Get-MinecraftExitMessage $process $instance $logs) }
    $game = @(Get-CimInstance Win32_Process -Filter "Name='bio4.exe'" | Where-Object { $_.ExecutablePath -and (Get-FullPath $_.ExecutablePath) -eq $executable }) | Select-Object -First 1
    Start-Sleep -Milliseconds 500
}
if ($game) {
    $gameId = $game.ProcessId; $gameCreated = $game.CreationDate
    while ($true) {
        $current = Get-CimInstance Win32_Process -Filter ('ProcessId=' + $gameId)
        if (-not $current -or $current.ExecutablePath -ne $executable -or $current.CreationDate -ne $gameCreated) { break }
        if ($process.HasExited) {
            Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
            if ($process.ExitCode -ne 0) { throw (Get-MinecraftExitMessage $process $instance $logs) }
            return
        }
        Start-Sleep -Seconds 2
    }
}
Preserve-CreatedWorld $settings (Assert-ChildPath $root '.re4craft/settings.json')
& (Join-Path $PSScriptRoot 'StopMinecraft.ps1')
