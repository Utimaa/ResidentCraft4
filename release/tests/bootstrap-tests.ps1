[CmdletBinding()]
param([Parameter(Mandatory)][string] $JavaPath, [string] $MinecraftPath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../launcher-common.ps1')
$checks=0
function Check([bool] $Condition,[string] $Name) { if (-not $Condition) { throw "FAILED: $Name" }; $script:checks++ }
$parent=Assert-ChildPath (Get-ReleaseRoot) 'build/release-tests'
$root=Assert-ChildPath $parent ('RE4Craft-bootstrap-'+[Guid]::NewGuid().ToString('N'))
$java=Get-FullPath $JavaPath
$javac=Join-Path ([IO.Path]::GetDirectoryName($java)) 'javac.exe'
if (-not (Test-Path -LiteralPath $javac -PathType Leaf)) { throw 'Bootstrap probes require the matching JDK (javac.exe); they never launch Minecraft.' }
New-Item -ItemType Directory -Path $root -Force|Out-Null
try {
    $accent=''
    foreach($codepoint in @(0xe9,0x416,0x4e16,0x3042,0xac00,0x627)) {
        try { $candidate=[string][char]$codepoint; $null=(Get-JavaArgumentEncoding).GetBytes($candidate); $accent=$candidate; break } catch { }
    }
    $instance=Assert-ChildPath $root ('Joueur '+$accent+' avec espaces/.instance')
    $classes=Assert-ChildPath $instance 'probe classes'
    New-Item -ItemType Directory -Path $classes -Force|Out-Null
    $source=Assert-ChildPath $root 'LauncherProbe.java'
    [IO.File]::WriteAllText($source,@'
import java.nio.file.*;
public class LauncherProbe {
    public static void main(String[] args) throws Exception {
        if (args.length != 4 || !args[0].equals("--gameDir") || !args[2].equals("--result")) throw new AssertionError("Argument boundaries changed");
        Path game = Path.of(args[1]);
        if (!Files.isDirectory(game) || !game.equals(Path.of(System.getProperty("re4craft.probe.path")))) throw new AssertionError("Native path encoding changed");
        Files.writeString(Path.of(args[3]), game.toString());
        System.out.println("PROBE_STDOUT"); System.err.println("PROBE_STDERR");
        System.out.println("PROBE_STACK_SHADOW=" + java.lang.management.ManagementFactory.getPlatformMXBean(com.sun.management.HotSpotDiagnosticMXBean.class).getVMOption("StackShadowPages").getValue());
        Thread.sleep(Long.getLong("re4craft.probe.delay", 0));
        System.err.println("PROBE_AFTER_PARENT_EXIT");
    }
}
'@,[Text.UTF8Encoding]::new($false))
    & $javac -encoding UTF-8 -d $classes $source
    if ($LASTEXITCODE -ne 0) { throw 'Probe compilation failed.' }
    $argfile=Assert-ChildPath $instance '.re4craft/launch.args'
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($argfile)) -Force|Out-Null
    $result=Assert-ChildPath $root 'result.txt'
    $arguments=@('-XX:StackShadowPages=32',('-Dre4craft.probe.path='+$instance),'-cp',$classes,'LauncherProbe','--gameDir',$instance,'--result',$result)
    Write-JavaArgumentFile $argfile $arguments
    $stdout=Assert-ChildPath $root 'probe.stdout.log'; $stderr=Assert-ChildPath $root 'probe.stderr.log'
    $process=Start-BackgroundJava $java $argfile $instance $stdout $stderr
    try {
        $process.WaitForExit()
        Check ($process.ExitCode -eq 0) 'Real Java loads classpath with spaces and native non-ASCII path'
        Check ([IO.File]::ReadAllText($result) -ceq $instance) 'Real Java receives exact private gameDir and property'
        Check (([IO.File]::ReadAllText($stdout)).Contains('PROBE_STDOUT')) 'Direct disk stdout survives Java startup and exit'
        Check (([IO.File]::ReadAllText($stdout)).Contains('PROBE_STACK_SHADOW=32')) 'Actual JVM applies the exact official Minecraft shadow-page requirement'
        Check (([IO.File]::ReadAllText($stderr)).Contains('PROBE_STDERR')) 'Direct disk stderr captures pre-Log4j messages'
    } finally { $process.Dispose() }
    Write-JavaArgumentFile $argfile @('-XX:RE4CraftDefinitelyInvalidOption','-version')
    $badOut=Assert-ChildPath $root 'bad.stdout.log'; $badErr=Assert-ChildPath $root 'bad.stderr.log'
    $process=Start-BackgroundJava $java $argfile $instance $badOut $badErr
    try {
        $process.WaitForExit()
        Check ($process.ExitCode -ne 0) 'Actual JVM option rejection retains nonzero exit code'
        Check (([IO.File]::ReadAllText($badErr)).Contains('RE4CraftDefinitelyInvalidOption')) 'Actual pre-Minecraft failure is captured without latest.log'
    } finally { $process.Dispose() }
    # Exit the launching PowerShell before Java writes its final output. No
    # console pipe may be required for an independently retained client.
    $helper=Assert-ChildPath $root 'detached-launch.ps1'
    [IO.File]::WriteAllText($helper,@'
param($Common,$Java,$ArgsFile,$Instance,$Out,$Err)
. $Common
$p=Start-BackgroundJava $Java $ArgsFile $Instance $Out $Err
'@)
    Write-JavaArgumentFile $argfile (@('-Dre4craft.probe.delay=1500')+$arguments)
    $detachedOut=Assert-ChildPath $root 'detached.stdout.log'; $detachedErr=Assert-ChildPath $root 'detached.stderr.log'
    $common=Get-FullPath (Join-Path $PSScriptRoot '../launcher-common.ps1')
    $helperArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$helper,$common,$java,$argfile,$instance,$detachedOut,$detachedErr) | ForEach-Object {'"'+$_.Replace('"','\"')+'"'}
    $supervisor=Start-Process -FilePath 'powershell.exe' -ArgumentList ($helperArgs -join ' ') -WindowStyle Hidden -PassThru -Wait
    Check ($supervisor.ExitCode -eq 0) 'Detached test supervisor exits normally'
    $deadline=[DateTime]::UtcNow.AddSeconds(8)
    while ((-not (Test-Path -LiteralPath $detachedErr) -or -not ([IO.File]::ReadAllText($detachedErr)).Contains('PROBE_AFTER_PARENT_EXIT')) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Check (([IO.File]::ReadAllText($detachedErr)).Contains('PROBE_AFTER_PARENT_EXIT')) 'Java inherited log handles remain valid after PowerShell exits'
    if ($MinecraftPath) {
        $owned=Resolve-OwnedMinecraft $MinecraftPath $false
        $prism=Assert-ChildPath $root 'Prism owned layout'
        $client=Assert-ChildPath $prism 'libraries/com/mojang/minecraft/26.3/minecraft-26.3-client.jar'
        $cache=Assert-ChildPath $root 'release cache'
        $files=@([pscustomobject]@{Source=$owned.Client;Target=$client},[pscustomobject]@{Source=(Join-Path $owned.Assets ('indexes/'+$owned.AssetIndex+'.json'));Target=(Assert-ChildPath $prism ('assets/indexes/'+$owned.AssetIndex+'.json'))},[pscustomobject]@{Source=(Join-Path $owned.Root 'versions/26.3/26.3.json');Target=(Assert-ChildPath $cache '.re4craft/metadata/26.3.json')})
        $meta=Read-JsonFile (Join-Path $owned.Root 'versions/26.3/26.3.json')
        $unconditional=@($meta.arguments.jvm | Where-Object {$_ -is [string]})
        $launchSettings=[pscustomobject]@{InstancePath=$instance;WorldName='Existing_World';ShutdownToken='a'*32}
        $launchPayload=[pscustomobject]@{FabricLibraries=@($classes);MainClass='LauncherProbe'}
        $launchArgs=@(New-MinecraftArguments $owned $launchPayload $launchSettings)
        $missing=@($unconditional | Where-Object {$_ -notmatch '\$\{' -and $_ -notin $launchArgs})
        Check ($unconditional -contains '-XX:StackShadowPages=32' -and $missing.Count -eq 0) 'Every literal unconditional JVM argument from pinned Mojang metadata is retained'
        foreach ($entry in $meta.libraries) {
            if (-not (Test-WindowsLibrary $entry)) { continue }
            $relative = Get-PrismClassifierPath $entry
            if (-not $relative) { $relative=$entry.downloads.artifact.path }
            $files+=[pscustomobject]@{Source=(Join-Path $owned.Root ('libraries/'+$entry.downloads.artifact.path));Target=(Assert-ChildPath $prism ('libraries/'+$relative))}
        }
        foreach ($file in $files) { New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($file.Target)) -Force|Out-Null; Copy-Item -LiteralPath $file.Source -Destination $file.Target }
        $resolved=Resolve-OwnedMinecraft $prism $false $cache
        Check ($resolved.Layout -eq 'Prism' -and $resolved.Client -eq $client) 'Real owned 26.3 client resolves through Prism Maven layout without versions JSON'
        Check ($resolved.Libraries.Count -eq $owned.Libraries.Count -and $resolved.AssetIndex -eq $owned.AssetIndex) 'Every pinned owned library/native and asset index validates through Prism layout'
        Check (-not (Test-Path -LiteralPath (Join-Path $prism 'versions'))) 'Prism verification does not create or require a versions directory'
        $incompleteRejected=$false
        try { $null=Resolve-OwnedMinecraft $prism $true $cache } catch { $incompleteRejected=$_.Exception.Message -like '*assets are incomplete*' }
        Check $incompleteRejected 'Setup rejects missing Prism asset objects without fetching game data'
        $metadataPath=Assert-ChildPath $cache '.re4craft/metadata/26.3.json'
        [IO.File]::WriteAllText($metadataPath,'modified cache')
        $modifiedRejected=$false
        try { $null=Resolve-OwnedMinecraft $prism $false $cache } catch { $modifiedRejected=$_.Exception.Message -like '*verification failed*' }
        Check $modifiedRejected 'Modified cached version metadata cannot alter the pinned client/libraries'
    }
    Write-Host "$checks real Java/bootstrap checks passed."
} finally {
    $full=Get-FullPath $root
    if (-not $full.StartsWith((Get-FullPath $parent)+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($full) -notlike 'RE4Craft-bootstrap-*') { throw 'Unsafe bootstrap fixture cleanup target.' }
    Remove-Item -LiteralPath $full -Recurse -Force
}
