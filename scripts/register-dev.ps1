param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$dllPath = Join-Path $projectRoot "build/$preset/$Configuration/OwO.TSF.dll"

if (-not (Test-Path -LiteralPath $dllPath)) {
    throw "TSF DLL not found: $dllPath. Build the selected preset first."
}

$process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" `
    -ArgumentList @('/s', $dllPath) -WindowStyle Hidden -Wait -PassThru
if ($process.ExitCode -ne 0) {
    throw "regsvr32 failed with exit code $($process.ExitCode)"
}
Write-Output "Registered development TSF: $dllPath"
