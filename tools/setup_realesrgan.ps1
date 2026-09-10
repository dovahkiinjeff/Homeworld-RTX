[CmdletBinding()]
param([string]$Destination)

$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Destination) { $Destination = Join-Path $toolRoot '.texture-tools\realesrgan' }
$assetUrl = 'https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip'
$expectedSha256 = 'ABC02804E17982A3BE33675E4D471E91EA374E65B70167ABC09E31ACB412802D'
$archive = Join-Path ([IO.Path]::GetTempPath()) 'hwrtx-realesrgan-20220424.zip'
$destinationPath = [IO.Path]::GetFullPath($Destination)

Invoke-WebRequest -Uri $assetUrl -OutFile $archive
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $expectedSha256) {
    Remove-Item -LiteralPath $archive -Force
    throw "Real-ESRGAN archive checksum mismatch. Expected $expectedSha256, got $actual"
}
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $destinationPath -Force
Remove-Item -LiteralPath $archive -Force
Write-Host "Installed verified Real-ESRGAN portable runtime in $destinationPath"
