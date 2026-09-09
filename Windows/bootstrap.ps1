[CmdletBinding()]
param(
    [string]$VcpkgRoot
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot

function Require-Command([string]$Name, [string]$InstallHint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found. $InstallHint"
    }
}

Require-Command 'git' 'Install Git for Windows and reopen this shell.'
Require-Command 'cmake' 'Install CMake and reopen this shell.'

function Find-WinFlexBisonExecutable([string]$Name) {
    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $WingetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $WingetPackages) {
        $Executable = Get-ChildItem $WingetPackages -Filter "$Name.exe" -File -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($Executable) {
            return $Executable.FullName
        }
    }

    return $null
}

$WinBison = Find-WinFlexBisonExecutable 'win_bison'
$WinFlex = Find-WinFlexBisonExecutable 'win_flex'
if (-not $WinBison -or -not $WinFlex) {
    Require-Command 'winget' 'Install WinGet/App Installer, then rerun this script.'
    Write-Host 'Installing WinFlexBison...'
    & winget install --id WinFlexBison.win_flex_bison --exact --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) {
        throw "WinFlexBison installation failed with exit code $LASTEXITCODE."
    }

    $WinBison = Find-WinFlexBisonExecutable 'win_bison'
    $WinFlex = Find-WinFlexBisonExecutable 'win_flex'
}

if (-not $WinBison -or -not $WinFlex) {
    throw 'WinFlexBison was installed but win_bison.exe or win_flex.exe could not be located.'
}

$WinFlexBisonDirectory = Split-Path -Parent $WinBison
if (($env:PATH -split ';') -notcontains $WinFlexBisonDirectory) {
    $env:PATH = "$WinFlexBisonDirectory;$env:PATH"
}
$UserPath = [Environment]::GetEnvironmentVariable('PATH', 'User')
if (($UserPath -split ';') -notcontains $WinFlexBisonDirectory) {
    $NewUserPath = if ([string]::IsNullOrWhiteSpace($UserPath)) {
        $WinFlexBisonDirectory
    } else {
        "$WinFlexBisonDirectory;$UserPath"
    }
    [Environment]::SetEnvironmentVariable('PATH', $NewUserPath, 'User')
}

$M4Sugar = Get-ChildItem $WinFlexBisonDirectory -Filter 'm4sugar.m4' -File -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($M4Sugar) {
    $BisonPackageData = Split-Path -Parent (Split-Path -Parent $M4Sugar.FullName)
    $env:BISON_PKGDATADIR = $BisonPackageData
    [Environment]::SetEnvironmentVariable('BISON_PKGDATADIR', $BisonPackageData, 'User')
}

if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    # FFmpeg's vcpkg port refuses to build when VCPKG_ROOT/buildtrees contains
    # a space. Keep the dependency checkout out of commonly spaced game paths
    # such as "HW1 RTX" and share it safely between source-package revisions.
    $VcpkgRoot = Join-Path $env:LOCALAPPDATA 'HomeworldModernDeps\vcpkg'
}

if ($VcpkgRoot -match '\s') {
    throw "The vcpkg path contains spaces: '$VcpkgRoot'. FFmpeg requires a no-space dependency path. Rerun with -VcpkgRoot 'F:\HomeworldModernDeps\vcpkg'."
}

if (-not (Test-Path (Join-Path $VcpkgRoot '.git'))) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $VcpkgRoot) | Out-Null
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgRoot
}

$Bootstrap = Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat'
$Vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
$Toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $Bootstrap) -or
    -not (Test-Path -LiteralPath $Toolchain)) {
    throw "The vcpkg checkout is incomplete: $VcpkgRoot. Remove only that dependency folder and rerun bootstrap.ps1."
}
if (-not (Test-Path $Vcpkg)) {
    & $Bootstrap -disableMetrics
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg bootstrap failed with exit code $LASTEXITCODE."
    }
}

$env:VCPKG_ROOT = $VcpkgRoot
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, 'User')

$StreamlineVersion = '2.12.0'
$StreamlineRoot = Join-Path $RepositoryRoot '.deps\streamline'
$StreamlineHeader = Join-Path $StreamlineRoot 'include\sl.h'
$StreamlineLibrary = Join-Path $StreamlineRoot 'lib\x64\sl.interposer.lib'
$StreamlineDlssD = Join-Path $StreamlineRoot 'bin\x64\sl.dlss_d.dll'
$StreamlineNgxDlssD = Join-Path $StreamlineRoot 'bin\x64\nvngx_dlssd.dll'

function Find-StreamlineProductionFile([string]$Name) {
    if (-not (Test-Path -LiteralPath $StreamlineRoot)) { return $null }
    $Candidates = @(Get-ChildItem -LiteralPath $StreamlineRoot -Filter $Name -File -Recurse -ErrorAction SilentlyContinue)
    if ($Candidates.Count -eq 0) { return $null }
    return $Candidates |
        Sort-Object @(
            @{ Expression = { if ($_.FullName -match '[\/](development|debug)[\/]') { 1 } else { 0 } }; Ascending = $true },
            @{ Expression = { if ($_.FullName -match '[\/](bin[\/]x64|production_x64|features)[\/]') { 0 } else { 1 } }; Ascending = $true },
            @{ Expression = { $_.FullName.Length }; Ascending = $true }
        ) | Select-Object -First 1
}

function Normalize-StreamlineRuntimeFile([string]$Name, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { return }
    $Source = Find-StreamlineProductionFile $Name
    if (-not $Source) { return }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $Source.FullName -Destination $Destination -Force
    Write-Host "Normalized Streamline runtime: $Name <- $($Source.FullName)"
}
if (-not (Test-Path -LiteralPath $StreamlineHeader) -or
    -not (Test-Path -LiteralPath $StreamlineLibrary) -or
    -not (Test-Path -LiteralPath $StreamlineDlssD) -or
    -not (Test-Path -LiteralPath $StreamlineNgxDlssD)) {
    $DependencyRoot = Join-Path $RepositoryRoot '.deps'
    $StreamlineArchive = Join-Path $DependencyRoot "streamline-sdk-v$StreamlineVersion.zip"
    $StreamlineUri = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v$StreamlineVersion/streamline-sdk-v$StreamlineVersion.zip"
    $ExpectedHash = 'F5C0A3D870707DDDC3570FB4BCD3655CF48A8A68C3A9D342910CFA21B77DCF48'
    New-Item -ItemType Directory -Force -Path $DependencyRoot | Out-Null
    if (-not (Test-Path -LiteralPath $StreamlineArchive) -or
        (Get-FileHash -LiteralPath $StreamlineArchive -Algorithm SHA256).Hash -ne $ExpectedHash) {
        Write-Host "Downloading NVIDIA Streamline SDK v$StreamlineVersion (DLSS/DLAA/Ray Reconstruction runtime)..."
        Invoke-WebRequest -Uri $StreamlineUri -OutFile $StreamlineArchive -UseBasicParsing
    }
    $ActualHash = (Get-FileHash -LiteralPath $StreamlineArchive -Algorithm SHA256).Hash
    if ($ActualHash -ne $ExpectedHash) {
        throw "Streamline SDK checksum mismatch. Expected $ExpectedHash, received $ActualHash."
    }
    if (Test-Path -LiteralPath $StreamlineRoot) {
        Remove-Item -LiteralPath $StreamlineRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $StreamlineRoot | Out-Null
    Expand-Archive -LiteralPath $StreamlineArchive -DestinationPath $StreamlineRoot -Force
}

# NVIDIA release layouts vary. Normalize the signed production DLSS-RR feature
# binaries into the canonical location used by CMake and the runtime check.
Normalize-StreamlineRuntimeFile 'sl.dlss_d.dll' $StreamlineDlssD
Normalize-StreamlineRuntimeFile 'nvngx_dlssd.dll' $StreamlineNgxDlssD
if (-not (Test-Path -LiteralPath $StreamlineDlssD) -or
    -not (Test-Path -LiteralPath $StreamlineNgxDlssD)) {
    $FoundPlugin = Find-StreamlineProductionFile 'sl.dlss_d.dll'
    $FoundNgx = Find-StreamlineProductionFile 'nvngx_dlssd.dll'
    throw ("NVIDIA Streamline v$StreamlineVersion was extracted, but the DLSS Ray Reconstruction production runtime could not be normalized. " +
           "sl.dlss_d.dll=" + $(if ($FoundPlugin) { $FoundPlugin.FullName } else { '<missing>' }) + "; " +
           "nvngx_dlssd.dll=" + $(if ($FoundNgx) { $FoundNgx.FullName } else { '<missing>' }))
}

Write-Host "VCPKG_ROOT=$VcpkgRoot"
Write-Host "WinFlexBison=$WinFlexBisonDirectory"
Write-Host "NVIDIA Streamline=$StreamlineRoot"
Write-Host "DLSS Ray Reconstruction runtime=$(Test-Path -LiteralPath $StreamlineDlssD) / $(Test-Path -LiteralPath $StreamlineNgxDlssD)"

function Install-VerifiedArchive([string]$Name, [string]$Uri, [string]$Hash, [string]$Archive, [string]$ExtractRoot) {
    if (-not (Test-Path -LiteralPath $Archive) -or
        (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $Hash) {
        Write-Host "Downloading $Name..."
        Invoke-WebRequest -Uri $Uri -OutFile $Archive -UseBasicParsing
    }
    $Actual = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash
    if ($Actual -ne $Hash) { throw "$Name checksum mismatch. Expected $Hash, received $Actual." }
    if (Test-Path -LiteralPath $ExtractRoot) { Remove-Item -LiteralPath $ExtractRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $ExtractRoot | Out-Null
    Expand-Archive -LiteralPath $Archive -DestinationPath $ExtractRoot -Force
}

$DependencyRoot = Join-Path $RepositoryRoot '.deps'
$FidelityRoot = Join-Path $DependencyRoot 'fidelityfx'
$FidelityArchive = Join-Path $DependencyRoot 'FidelityFX-SDK-v2.3.0.zip'
$FidelityExtract = Join-Path $DependencyRoot '_fidelityfx_extract'
if (-not (Test-Path (Join-Path $FidelityRoot 'bin\amd_fidelityfx_upscaler_dx12.dll'))) {
    Install-VerifiedArchive 'AMD FidelityFX SDK v2.3.0' `
        'https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/archive/refs/tags/v2.3.0.zip' `
        'E7F274930086405071C01B4E7E1DE8CD3BB9FBCB987D711EBF65169464C28455' `
        $FidelityArchive $FidelityExtract
    $FidelitySource = Get-ChildItem $FidelityExtract -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Force "$FidelityRoot\Kits\FidelityFX","$FidelityRoot\lib","$FidelityRoot\bin" | Out-Null
    Copy-Item "$($FidelitySource.FullName)\Kits\FidelityFX\api" "$FidelityRoot\Kits\FidelityFX" -Recurse -Force
    Copy-Item "$($FidelitySource.FullName)\Kits\FidelityFX\upscalers" "$FidelityRoot\Kits\FidelityFX" -Recurse -Force
    Copy-Item "$($FidelitySource.FullName)\Kits\FidelityFX\signedbin\amd_fidelityfx_loader_dx12.lib" "$FidelityRoot\lib" -Force
    Copy-Item "$($FidelitySource.FullName)\Kits\FidelityFX\signedbin\amd_fidelityfx_loader_dx12.dll","$($FidelitySource.FullName)\Kits\FidelityFX\signedbin\amd_fidelityfx_upscaler_dx12.dll" "$FidelityRoot\bin" -Force
}

$XessRoot = Join-Path $DependencyRoot 'xess'
$XessArchive = Join-Path $DependencyRoot 'XeSS_SDK_3.0.2.zip'
$XessExtract = Join-Path $DependencyRoot '_xess_extract'
if (-not (Test-Path (Join-Path $XessRoot 'bin\libxess.dll'))) {
    Install-VerifiedArchive 'Intel XeSS SDK 3.0.2' `
        'https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip' `
        '88B8A373F30E33F3558A77A93E634F11B8132FC3047EA1A8EDEEAD32B8471990' `
        $XessArchive $XessExtract
    $XessSource = Get-ChildItem $XessExtract -Directory | Select-Object -First 1
    New-Item -ItemType Directory -Force "$XessRoot\include\xess","$XessRoot\lib","$XessRoot\bin" | Out-Null
    Copy-Item "$($XessSource.FullName)\inc\xess\*" "$XessRoot\include\xess" -Force
    Copy-Item "$($XessSource.FullName)\lib\libxess.lib" "$XessRoot\lib" -Force
    Copy-Item "$($XessSource.FullName)\bin\libxess.dll" "$XessRoot\bin" -Force
    Copy-Item "$($XessSource.FullName)\LICENSE.txt" "$XessRoot\LICENSE.txt" -Force
}

Write-Host "AMD FidelityFX=$FidelityRoot"
Write-Host "Intel XeSS=$XessRoot"
Write-Host 'Bootstrap complete. Run .\Windows\build.ps1 next.'
