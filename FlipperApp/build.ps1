param(
    [string]$SourceDir = (Split-Path -Parent $MyInvocation.MyCommand.Path),
    [string]$FirmwareDir = $env:FLIPPER_FW_PATH,
    [string]$TargetDir,
    [string]$AppSrc = "applications_user/bambu_fleet",
    [string]$DistDir,
    [switch]$PreviewSync,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Write-Step {
    param([string]$Message)
    Write-Host "[build] $Message"
}

function Find-DefaultFirmwareDir {
    param([string]$Root)

    $candidates = @(
        $env:FLIPPER_FW_PATH,
        (Join-Path (Split-Path -Parent $Root) "Momentum-Firmware"),
        (Join-Path (Split-Path -Parent $Root) "flipperzero-firmware"),
        "C:\Users\Joe\Projects\Momentum-Firmware",
        "C:\Users\Joe\Projects\flipperzero-firmware"
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach($candidate in $candidates) {
        if(Test-Path $candidate -PathType Container) {
            return (Resolve-FullPath $candidate)
        }
    }

    throw "Could not locate a Flipper firmware checkout. Set FLIPPER_FW_PATH or pass -FirmwareDir."
}

function Find-BuiltFap {
    param(
        [Parameter(Mandatory = $true)][string]$FirmwareRoot,
        [Parameter(Mandatory = $true)][string]$AppId
    )

    $searchRoots = @(
        (Join-Path $FirmwareRoot "build"),
        (Join-Path $FirmwareRoot "dist")
    )

    foreach($root in $searchRoots) {
        if(-not (Test-Path $root -PathType Container)) {
            continue
        }

        $match = Get-ChildItem -Path $root -Filter "$AppId.fap" -Recurse -File |
            Select-Object -First 1
        if($match) {
            return $match.FullName
        }
    }

    throw "Build completed, but $AppId.fap was not found under $FirmwareRoot\build or $FirmwareRoot\dist."
}

$SourceDir = Resolve-FullPath $SourceDir
if([string]::IsNullOrWhiteSpace($FirmwareDir)) {
    $FirmwareDir = Find-DefaultFirmwareDir -Root $SourceDir
} else {
    $FirmwareDir = Resolve-FullPath $FirmwareDir
}

if([string]::IsNullOrWhiteSpace($TargetDir)) {
    $TargetDir = Join-Path $FirmwareDir "applications_user\bambu_fleet"
} else {
    $TargetDir = Resolve-FullPath $TargetDir
}

if([string]::IsNullOrWhiteSpace($DistDir)) {
    $DistDir = Join-Path $SourceDir "dist"
} else {
    $DistDir = Resolve-FullPath $DistDir
}

if(!(Test-Path $SourceDir -PathType Container)) {
    throw "SourceDir does not exist or is not a directory: $SourceDir"
}

if(!(Test-Path $FirmwareDir -PathType Container)) {
    throw "FirmwareDir does not exist or is not a directory: $FirmwareDir"
}

if(!(Test-Path (Join-Path $FirmwareDir "applications_user") -PathType Container)) {
    throw "FirmwareDir does not look like a Flipper firmware checkout: missing applications_user"
}

$fbtCmd = Join-Path $FirmwareDir "fbt.cmd"
if(!(Test-Path $fbtCmd -PathType Leaf)) {
    throw "FirmwareDir is missing fbt.cmd: $fbtCmd"
}

Write-Step "SourceDir : $SourceDir"
Write-Step "FirmwareDir: $FirmwareDir"
Write-Step "TargetDir : $TargetDir"
Write-Step "AppSrc    : $AppSrc"
Write-Step "DistDir   : $DistDir"

if($TargetDir.TrimEnd('\') -eq $SourceDir.TrimEnd('\')) {
    throw "Refusing to mirror because SourceDir and TargetDir are the same path."
}

$firmwareRoot = $FirmwareDir.TrimEnd('\') + '\'
$normalizedTargetDir = $TargetDir.TrimEnd('\') + '\'
if(-not $normalizedTargetDir.StartsWith($firmwareRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to mirror outside FirmwareDir. TargetDir must be inside $FirmwareDir"
}

if(!(Test-Path $TargetDir -PathType Container)) {
    New-Item -ItemType Directory -Path $TargetDir | Out-Null
}

$robocopyArgs = @(
    $SourceDir
    $TargetDir
    "/MIR"
    "/XD"
    ".git"
    "dist"
    "esp32_bridge"
    "/XF"
    "README.md"
    "build.sh"
    "deploy.sh"
    "build.ps1"
    "deploy.ps1"
    ".gitignore"
)

if($PreviewSync) {
    $robocopyArgs += "/L"
    Write-Step "Preview mode enabled. No files will be copied, deleted, or built."
}

Write-Step "Syncing app files with robocopy"
& robocopy @robocopyArgs | Out-Host
if($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

if($PreviewSync -or $SkipBuild) {
    Write-Step "Sync step complete."
    if($SkipBuild -and -not $PreviewSync) {
        Write-Step "Build skipped."
    }
    return
}

Push-Location $FirmwareDir
try {
    Write-Step "Building Bambu Fleet"
    & $fbtCmd "fap_bambu_fleet"
    if($LASTEXITCODE -ne 0) {
        throw "fbt.cmd failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

if(!(Test-Path $DistDir -PathType Container)) {
    New-Item -ItemType Directory -Path $DistDir | Out-Null
}

$builtFap = Find-BuiltFap -FirmwareRoot $FirmwareDir -AppId "bambu_fleet"
$distFap = Join-Path $DistDir "bambu_fleet.fap"
Copy-Item -Path $builtFap -Destination $distFap -Force

Write-Step "Copied artifact to $distFap"
Write-Step "Build complete."
