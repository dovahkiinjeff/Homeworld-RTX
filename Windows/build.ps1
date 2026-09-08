[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$VcpkgRoot,
    [string]$VcpkgInstalledRoot,
    [switch]$EnableNetwork,
    [switch]$Sanitize,
    [switch]$ShowWarnings
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot

function Find-VSWhere {
    $Candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )

    return $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

function Get-VisualStudioGenerator {
    $VSWhere = Find-VSWhere
    if (-not $VSWhere) {
        throw 'Visual Studio Installer (vswhere.exe) was not found.'
    }

    $InstanceJson = & $VSWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($InstanceJson)) {
        throw 'No Visual Studio installation with the MSVC x64 build tools was found.'
    }

    $Instance = $InstanceJson | ConvertFrom-Json | Select-Object -First 1
    $MajorVersion = ([version]$Instance.installationVersion).Major
    switch ($MajorVersion) {
        18 { return 'Visual Studio 18 2026' }
        17 { return 'Visual Studio 17 2022' }
        default { throw "Visual Studio $MajorVersion is not supported by this build script." }
    }
}

Push-Location $RepositoryRoot
try {
    $SharedVcpkgRoot = Join-Path $env:LOCALAPPDATA 'HomeworldModernDeps\vcpkg'
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $VcpkgRoot = $env:VCPKG_ROOT
    }
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or $VcpkgRoot -match '\s') {
        $VcpkgRoot = $SharedVcpkgRoot
    }
    $RequestedToolchain = if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $null
    }
    else {
        Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    }
    if ($VcpkgRoot -match '\s') {
        throw "The vcpkg path contains spaces: '$VcpkgRoot'. FFmpeg requires a no-space dependency path."
    }
    if (-not $RequestedToolchain -or -not (Test-Path -LiteralPath $RequestedToolchain)) {
        throw "A valid no-space vcpkg toolchain was not found at '$VcpkgRoot'. Run .\Windows\bootstrap.ps1 first."
    }
    # Ray Reconstruction ships as a separate signed Streamline plugin.
    # Refresh the pinned SDK before configure if either production binary is
    # absent so deployed builds cannot silently regress to RUNTIME MISSING.
    $StreamlineRoot = Join-Path $RepositoryRoot '.deps\streamline'
    $StreamlineDlssD = Join-Path $StreamlineRoot 'bin\x64\sl.dlss_d.dll'
    $StreamlineNgxDlssD = Join-Path $StreamlineRoot 'bin\x64\nvngx_dlssd.dll'
    if (-not (Test-Path -LiteralPath $StreamlineDlssD) -or
        -not (Test-Path -LiteralPath $StreamlineNgxDlssD)) {
        Write-Host 'DLSS Ray Reconstruction runtime missing; refreshing signed NVIDIA Streamline v2.12.0...'
        & (Join-Path $PSScriptRoot 'bootstrap.ps1') -VcpkgRoot $VcpkgRoot
    }
    if (-not (Test-Path -LiteralPath $StreamlineDlssD) -or
        -not (Test-Path -LiteralPath $StreamlineNgxDlssD)) {
        throw "DLSS Ray Reconstruction bootstrap completed but the production runtime is still missing: '$StreamlineDlssD' / '$StreamlineNgxDlssD'."
    }

    $env:VCPKG_ROOT = $VcpkgRoot
    Write-Host "Using no-space vcpkg: $VcpkgRoot"

    if ([string]::IsNullOrWhiteSpace($VcpkgInstalledRoot)) {
        $VcpkgInstalledRoot = Join-Path (Split-Path -Parent $VcpkgRoot) 'installed\homeworldmodern-0.91.2'
    }
    if ($VcpkgInstalledRoot -match '\s') {
        throw "The vcpkg installed path contains spaces: '$VcpkgInstalledRoot'. FFmpeg requires a no-space install path."
    }
    New-Item -ItemType Directory -Force -Path $VcpkgInstalledRoot | Out-Null
    Write-Host "Using no-space vcpkg install tree: $VcpkgInstalledRoot"

    $Generator = Get-VisualStudioGenerator
    $BuildDirectory = Join-Path $RepositoryRoot 'out\build\windows-x64'
    # Always regenerate this project from a clean build tree.  Homeworld uses a
    # large mixed C/C++ source set and stale Visual Studio projects can otherwise
    # retain an incomplete source graph (most visibly, a missing Mesh.obj).
    if (Test-Path -LiteralPath $BuildDirectory) {
        Write-Host "Removing stale generated build tree: $BuildDirectory"
        Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
    }
    $ToolchainFile = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    $ConfigureArguments = @(
        '-S', $RepositoryRoot,
        '-B', $BuildDirectory,
        '-G', $Generator,
        '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
        "-DVCPKG_INSTALLED_DIR=$VcpkgInstalledRoot",
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DVCPKG_HOST_TRIPLET=x64-windows'
    )
    if ($EnableNetwork) {
        $ConfigureArguments += '-DHW_ENABLE_NETWORK=ON'
    }
    if ($Sanitize) {
        $ConfigureArguments += '-DHW_ENABLE_ASAN=ON'
    }
    else {
        $ConfigureArguments += '-DHW_ENABLE_ASAN=OFF'
    }

    Write-Host "Using $Generator"
    & cmake @ConfigureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }

    $BuildArguments = @('--build', $BuildDirectory, '--config', $Configuration, '--parallel')
    if (-not $ShowWarnings) {
        $BuildArguments += @('--', '/v:minimal', '/clp:ErrorsOnly;Summary')
    }

    & cmake @BuildArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }

    $OutputDirectory = Join-Path $RepositoryRoot "out\build\windows-x64\$Configuration"
    if ($Sanitize) {
        $VSWhere = Find-VSWhere
        $VisualStudioPath = & $VSWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $AsanRuntime = Get-ChildItem (Join-Path $VisualStudioPath 'VC\Tools') `
            -Filter 'clang_rt.asan_dynamic-x86_64.dll' -File -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\Hostx64\\x64\\' } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if (-not $AsanRuntime) {
            throw 'MSVC AddressSanitizer runtime was not found. Install the MSVC AddressSanitizer component.'
        }
        Copy-Item -LiteralPath $AsanRuntime.FullName -Destination $OutputDirectory -Force
        Write-Host "Copied AddressSanitizer runtime: $($AsanRuntime.Name)"
    }

    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Tests failed with exit code $LASTEXITCODE."
    }

    Write-Host "Build complete: $OutputDirectory\HomeworldModern.exe"
}
finally {
    Pop-Location
}
