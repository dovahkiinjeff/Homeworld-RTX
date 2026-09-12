param(
    [Parameter(Mandatory = $true)][string]$Deploy,
    [Parameter(Mandatory = $true)][string]$Output,
    [Parameter(Mandatory = $true)][string]$Manual
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$packageName = 'Homeworld-RTX-v0.92.0-beta.1-Windows-x64'
$stageRoot = Join-Path $Output $packageName
$archive = Join-Path $Output ($packageName + '.zip')

if (-not (Test-Path -LiteralPath (Join-Path $Deploy 'HomeworldModern.exe'))) {
    throw 'Tested deployment was not found.'
}
if (-not (Test-Path -LiteralPath $manual)) {
    throw 'The 0.92.0 manual was not generated.'
}

if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $archive) {
    Remove-Item -LiteralPath $archive -Force
}
New-Item -ItemType Directory -Path $stageRoot | Out-Null

$runtimeFiles = @(
    'HomeworldModern.exe', 'HomeworldModern.pdb', 'HomeworldTextures.hwt',
    'kas2c.exe', 'ship_texture_dds.exe', 'ui_scale_tests.exe',
    'SDL2.dll', 'avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll',
    'swscale-10.dll', 'DirectXTex.dll', 'libxess.dll',
    'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll',
    'amd_fidelityfx_framegeneration_dx12.dll',
    'nvngx_dlss.dll', 'nvngx_dlssd.dll', 'nvngx_dlssg.dll',
    'sl.common.dll', 'sl.dlss.dll', 'sl.dlss_d.dll', 'sl.dlss_g.dll',
    'sl.interposer.dll', 'sl.pcl.dll', 'sl.reflex.dll'
)
foreach ($name in $runtimeFiles) {
    $source = Join-Path $Deploy $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing runtime: $name" }
    Copy-Item -LiteralPath $source -Destination $stageRoot
}

$assetDirectories = @('Asteroids', 'Cursors', 'Missions', 'p1', 'p2', 'p3', 'Planets', 'R1', 'r2', 'traders', 'UI')
foreach ($name in $assetDirectories) {
    $source = Join-Path $Deploy $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing asset directory: $name" }
    Copy-Item -LiteralPath $source -Destination $stageRoot -Recurse
}

$repoFiles = @('README.md', 'CHANGELOG', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.md')
foreach ($name in $repoFiles) {
    Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $stageRoot
}

$docsOut = Join-Path $stageRoot 'docs'
$licensesOut = Join-Path $stageRoot 'licenses'
New-Item -ItemType Directory -Path $docsOut, $licensesOut | Out-Null
foreach ($name in @('USER_GUIDE.md', 'EDITING_GUIDE.md', 'LIMITATIONS.md', 'SHIP_TEXTURE_UPSCALING.md')) {
    Copy-Item -LiteralPath (Join-Path $repo ('docs\' + $name)) -Destination $docsOut
}
Copy-Item -LiteralPath $manual -Destination (Join-Path $docsOut 'Homeworld-RTX-0.92.0-Beta-Manual.pdf')
$licenseSource = Join-Path $Deploy 'licenses'
if (-not (Test-Path -LiteralPath $licenseSource)) {
    throw 'Runtime license directory is missing from the deployment.'
}
Get-ChildItem -LiteralPath $licenseSource -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $licensesOut
}

Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Where-Object {
    $_.Name -ieq 'HW_Music.wxd' -or $_.Name -ieq 'HW_Comp.vce'
} | ForEach-Object { throw "Retail archive entered package: $($_.FullName)" }

Compress-Archive -LiteralPath $stageRoot -DestinationPath $archive -CompressionLevel Optimal

$hashes = @(
    Get-FileHash -Algorithm SHA256 -LiteralPath $archive
    Get-FileHash -Algorithm SHA256 -LiteralPath $manual
)
$checksum = Join-Path $Output 'SHA256SUMS-v0.92.0-beta.1.txt'
$hashes | ForEach-Object { "$($_.Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($_.Path))" } |
    Set-Content -LiteralPath $checksum -Encoding ascii

[pscustomobject]@{
    Archive = $archive
    ArchiveBytes = (Get-Item -LiteralPath $archive).Length
    Manual = $manual
    Checksum = $checksum
    Files = (Get-ChildItem -LiteralPath $stageRoot -Recurse -File).Count
}
