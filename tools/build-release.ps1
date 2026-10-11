[CmdletBinding()]
param([string] $JavaHome, [switch] $NativeOnly, [switch] $MinecraftOnly, [switch] $SkipChecks)
$ErrorActionPreference='Stop'
if ($NativeOnly -and $MinecraftOnly) { throw 'Choose NativeOnly or MinecraftOnly.' }
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$previousLocation=Get-Location
$previousJava=$env:JAVA_HOME
try {
    Set-Location -LiteralPath $projectRoot
    if (-not $MinecraftOnly) {
        & .\native\build.cmd
        if ($LASTEXITCODE -ne 0) { throw 'Native x86 build failed.' }
        if (-not $SkipChecks) {
            & .\native\build-window-tests.cmd
            if ($LASTEXITCODE -ne 0) { throw 'Native window checks failed.' }
        }
    }
    if (-not $NativeOnly) {
        if ($JavaHome) { $env:JAVA_HOME=[IO.Path]::GetFullPath($JavaHome) }
        if (-not $env:JAVA_HOME -or -not (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin\javac.exe'))) { throw 'Set JAVA_HOME to a Java 25 JDK, or pass -JavaHome.' }
        Set-Location -LiteralPath (Join-Path $projectRoot 'minecraft')
        $gradleArguments=@('--no-daemon','build')
        if ($SkipChecks) { $gradleArguments+=@('-x','test') }
        & .\gradlew.bat @gradleArguments
        if ($LASTEXITCODE -ne 0) { throw 'Minecraft bridge build failed.' }
    }
    Write-Host 'Build prepared. No games were launched or modified.'
} finally { $env:JAVA_HOME=$previousJava; Set-Location -LiteralPath $previousLocation }
