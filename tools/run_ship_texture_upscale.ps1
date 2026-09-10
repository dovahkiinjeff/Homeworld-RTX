[CmdletBinding()]
param(
    [string]$Source,
    [string]$Output,
    [ValidateSet(2,3,4)][int]$Scale = 4,
    [ValidateSet('realesrgan','pillow')][string]$Backend = 'realesrgan',
    [string]$RealEsrgan,
    [int]$MaxDimension = 4096,
    [int]$Tile = 256,
    [ValidateSet('png','tga')][string]$OutputFormat = 'png',
    [switch]$BatchAI,
    [switch]$PlanOnly,
    [switch]$Overwrite
)

$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Source) { $Source = Join-Path $toolRoot '..\assets' }
if (-not $Output) { $Output = Join-Path $toolRoot '..\out\upscaled-ships' }
if (-not $RealEsrgan) { $RealEsrgan = Join-Path $toolRoot '.texture-tools\realesrgan\realesrgan-ncnn-vulkan.exe' }
$python = Get-Command py -ErrorAction SilentlyContinue
if (-not $python) { throw 'Python launcher (py.exe) is required.' }

$arguments = @('-3', (Join-Path $toolRoot 'ship_texture_pipeline.py'),
    '--source', (Resolve-Path -LiteralPath $Source).Path,
    '--output', [IO.Path]::GetFullPath($Output),
    '--scale', $Scale, '--max-dimension', $MaxDimension,
    '--backend', $Backend, '--tile', $Tile, '--output-format', $OutputFormat)
if ($Backend -eq 'realesrgan') { $arguments += @('--realesrgan', [IO.Path]::GetFullPath($RealEsrgan)) }
if (-not $PlanOnly) { $arguments += '--execute' }
if ($BatchAI) { $arguments += '--batch-ai' }
if ($Overwrite) { $arguments += '--overwrite' }

& $python.Source @arguments
exit $LASTEXITCODE
