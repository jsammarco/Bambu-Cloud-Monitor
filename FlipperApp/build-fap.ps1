param(
    [string]$FirmwareDir = "C:\Users\jasammarco.ENG\Projects\Momentum-Firmware",
    [switch]$PreviewSync,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $scriptDir "build.ps1"

if(!(Test-Path $buildScript -PathType Leaf)) {
    throw "Missing build script: $buildScript"
}

& $buildScript -FirmwareDir $FirmwareDir -PreviewSync:$PreviewSync -SkipBuild:$SkipBuild
if($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
