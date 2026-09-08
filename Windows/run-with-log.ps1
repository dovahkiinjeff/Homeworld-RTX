[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$DataPath = $env:HW_Data,
    [switch]$Windowed,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArguments
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$ExecutableDirectory = Join-Path $RepositoryRoot "out\build\windows-x64\$Configuration"
$Executable = Join-Path $ExecutableDirectory 'HomeworldModern.exe'
$ProgramDatabase = Join-Path $ExecutableDirectory 'HomeworldModern.pdb'

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "HomeworldModern.exe was not found at '$Executable'. Build the game first."
}

if ([string]::IsNullOrWhiteSpace($DataPath)) {
    throw 'HW_Data is not set. Pass -DataPath or set the HW_Data environment variable.'
}
$DataPath = (Resolve-Path -LiteralPath $DataPath).Path

$RequiredAssets = @('Homeworld.big')
foreach ($Asset in $RequiredAssets) {
    $AssetPath = Join-Path $DataPath $Asset
    if (-not (Test-Path -LiteralPath $AssetPath)) {
        throw "Required asset was not found: $AssetPath"
    }
}

$OptionalAudioAssets = @('HW_Comp.vce', 'HW_Music.wxd')
foreach ($Asset in $OptionalAudioAssets) {
    $AssetPath = Join-Path $DataPath $Asset
    if (-not (Test-Path -LiteralPath $AssetPath)) {
        Write-Warning "Optional audio archive was not found: $AssetPath"
    }
}

$Timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$LogRoot = Join-Path $RepositoryRoot 'out\logs'
$RunDirectory = Join-Path $LogRoot "run-$Timestamp"
New-Item -ItemType Directory -Path $RunDirectory -Force | Out-Null

$StandardOutputPath = Join-Path $RunDirectory 'stdout.log'
$StandardErrorPath = Join-Path $RunDirectory 'stderr.log'
$CombinedLogPath = Join-Path $RunDirectory 'HomeworldModern.log'
$MetadataPath = Join-Path $RunDirectory 'system.txt'
$ArchivePath = "$RunDirectory.zip"

$LaunchArguments = @()
if ($Windowed) {
    $LaunchArguments += '/window'
}
if ($GameArguments) {
    $LaunchArguments += $GameArguments
}

$env:HW_Data = $DataPath
$env:HW_CRASH_DIR = $RunDirectory

$WerKey = 'HKCU:\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps\HomeworldModern.exe'
$WerStatus = 'not configured'
try {
    New-Item -Path $WerKey -Force | Out-Null
    New-ItemProperty -Path $WerKey -Name 'DumpFolder' -PropertyType ExpandString -Value $RunDirectory -Force | Out-Null
    New-ItemProperty -Path $WerKey -Name 'DumpType' -PropertyType DWord -Value 2 -Force | Out-Null
    New-ItemProperty -Path $WerKey -Name 'DumpCount' -PropertyType DWord -Value 4 -Force | Out-Null
    $WerStatus = "configured at $WerKey"
}
catch {
    $WerStatus = "configuration failed: $($_.Exception.Message)"
}

$Metadata = @(
    "Captured: $(Get-Date -Format o)",
    "Executable: $Executable",
    "Symbols: $ProgramDatabase",
    "Data path: $DataPath",
    "Arguments: $($LaunchArguments -join ' ')",
    "PowerShell: $($PSVersionTable.PSVersion)",
    "OS: $([Environment]::OSVersion.VersionString)",
    "Process architecture: $env:PROCESSOR_ARCHITECTURE",
    "Windows Error Reporting: $WerStatus"
)

try {
    $VideoControllers = Get-CimInstance Win32_VideoController -ErrorAction Stop
    foreach ($Controller in $VideoControllers) {
        $Metadata += "GPU: $($Controller.Name)"
        $Metadata += "Driver: $($Controller.DriverVersion)"
        $Metadata += "Display: $($Controller.CurrentHorizontalResolution)x$($Controller.CurrentVerticalResolution)@$($Controller.CurrentRefreshRate)"
    }
}
catch {
    $Metadata += "GPU query failed: $($_.Exception.Message)"
}
$Metadata | Set-Content -LiteralPath $MetadataPath -Encoding UTF8

Write-Host "Launching Homeworld Modern. Logs will be written to:"
Write-Host $RunDirectory

$StartParameters = @{
    FilePath = $Executable
    WorkingDirectory = $ExecutableDirectory
    PassThru = $true
    Wait = $true
    RedirectStandardOutput = $StandardOutputPath
    RedirectStandardError = $StandardErrorPath
}
if ($LaunchArguments.Count -gt 0) {
    $StartParameters.ArgumentList = $LaunchArguments
}

$StartedAt = Get-Date
$Process = Start-Process @StartParameters
$FinishedAt = Get-Date

# Windows Error Reporting writes fast-fail dumps asynchronously after process
# termination. Give it a brief opportunity to finish before packaging the run.
Start-Sleep -Milliseconds 2000

@(
    "Homeworld Modern diagnostic run",
    "Started: $($StartedAt.ToString('o'))",
    "Finished: $($FinishedAt.ToString('o'))",
    "Exit code: $($Process.ExitCode)",
    "",
    '===== SYSTEM ====='
) | Set-Content -LiteralPath $CombinedLogPath -Encoding UTF8
Get-Content -LiteralPath $MetadataPath | Add-Content -LiteralPath $CombinedLogPath -Encoding UTF8

Add-Content -LiteralPath $CombinedLogPath -Value "`n===== STDOUT =====" -Encoding UTF8
if (Test-Path -LiteralPath $StandardOutputPath) {
    Get-Content -LiteralPath $StandardOutputPath | Add-Content -LiteralPath $CombinedLogPath -Encoding UTF8
}

Add-Content -LiteralPath $CombinedLogPath -Value "`n===== STDERR =====" -Encoding UTF8
if (Test-Path -LiteralPath $StandardErrorPath) {
    Get-Content -LiteralPath $StandardErrorPath | Add-Content -LiteralPath $CombinedLogPath -Encoding UTF8
}

# Keep every crash archive self-contained and exactly matched to its symbols.
Copy-Item -LiteralPath $Executable -Destination $RunDirectory -Force
if (Test-Path -LiteralPath $ProgramDatabase) {
    Copy-Item -LiteralPath $ProgramDatabase -Destination $RunDirectory -Force
}

Compress-Archive -Path (Join-Path $RunDirectory '*') -DestinationPath $ArchivePath -Force

Write-Host "Process exit code: $($Process.ExitCode)"
Write-Host "Combined log: $CombinedLogPath"
Write-Host "Upload this diagnostic archive: $ArchivePath"
