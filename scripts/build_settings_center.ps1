[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('win-x64')]
    [string]$RuntimeIdentifier = 'win-x64',
    [switch]$NoRestore
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$configShell = Join-Path $projectRoot "build/$preset/$Configuration/owo_config_shell.exe"
$project = Join-Path $projectRoot 'apps/settings_center/OwO.Settings.csproj'

cmake --build --preset $preset --target owo_config_shell
if ($LASTEXITCODE -ne 0) { throw "owo_config_shell build failed: $LASTEXITCODE" }

if (-not $NoRestore) {
    dotnet restore $project -r $RuntimeIdentifier --locked-mode
    if ($LASTEXITCODE -ne 0) { throw "settings restore failed: $LASTEXITCODE" }
}

dotnet build $project -c $Configuration -r $RuntimeIdentifier --no-restore `
    "-p:OwOConfigShellPath=$configShell"
if ($LASTEXITCODE -ne 0) { throw "settings build failed: $LASTEXITCODE" }

$output = Join-Path $projectRoot "apps/settings_center/bin/$Configuration/net10.0-windows10.0.26100.0/$RuntimeIdentifier"
$required = @('OwO.Settings.exe', 'OwO.Settings.dll', 'owo_config_shell.exe', 'AppxManifest.xml')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $name) -PathType Leaf)) {
        throw "settings output missing: $name"
    }
}
$forbidden = @(Get-ChildItem -LiteralPath $output -File | Where-Object {
    $_.Name -match '(^|\.)AI\.|MachineLearning|OnnxRuntime|Widgets'
})
if ($forbidden.Count -ne 0) {
    throw "settings output contains unused SDK components: $($forbidden.Name -join ', ')"
}

$sizeMiB = [math]::Round(((Get-ChildItem -LiteralPath $output -File |
    Measure-Object -Property Length -Sum).Sum / 1MB), 2)
Write-Output "OwO settings center ready: $output"
Write-Output "Top-level payload: $sizeMiB MiB"
