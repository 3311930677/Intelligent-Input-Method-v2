<#
.SYNOPSIS
    Developer-only: start the OwO core service with the best available lexicon in one step.

.DESCRIPTION
    Picks the most complete compiled lexicon it can find (preferring the merged "cn" dictionary
    that contains both single characters and phrases), locates the built owo_core_service.exe,
    stops any running instance, and starts a fresh one.

    Preference order (most complete first):
      rime-ice-cn  ->  rime-ice-base  ->  any *.owolx

.PARAMETER Lexicon
    Explicit .owolx path. Overrides auto-selection.

.PARAMETER Background
    Start the service in a separate window so the current shell stays free.
#>
param(
    [string]$Lexicon,
    [switch]$Background
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

# 1. Locate the built core service exe (prefer the ninja-debug layout, then VS layouts).
$exeCandidates = @(
    "build\windows-ninja-debug\owo_core_service.exe",
    "build\windows-release\Release\owo_core_service.exe",
    "build\windows-debug\Debug\owo_core_service.exe"
) | ForEach-Object { Join-Path $projectRoot $_ }
$exe = $exeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $exe) {
    throw "owo_core_service.exe not found. Build the project first (e.g. preset windows-ninja-debug)."
}

$shell = Join-Path (Split-Path -Parent $exe) 'owo_ipc_shell.exe'

# 2. Pick the lexicon.
if ([string]::IsNullOrWhiteSpace($Lexicon)) {
    $allLexicons = Get-ChildItem -Path (Join-Path $projectRoot 'build') -Recurse -Filter '*.owolx' -File -ErrorAction SilentlyContinue
    if (-not $allLexicons) {
        throw "No compiled .owolx lexicon found under build\. Compile a lexicon first (owo_lexicon_compiler)."
    }
    # Prefer the merged 'cn' dictionary (single chars + phrases), then 'base', then largest.
    $Lexicon = ($allLexicons | Where-Object { $_.Name -like 'rime-ice-cn-*' } | Select-Object -First 1).FullName
    if (-not $Lexicon) {
        $Lexicon = ($allLexicons | Where-Object { $_.Name -like 'rime-ice-base-*' } | Select-Object -First 1).FullName
    }
    if (-not $Lexicon) {
        $Lexicon = ($allLexicons | Sort-Object Length -Descending | Select-Object -First 1).FullName
    }
}
$Lexicon = [System.IO.Path]::GetFullPath($Lexicon)
if (-not (Test-Path -LiteralPath $Lexicon -PathType Leaf)) {
    throw "Lexicon not found: $Lexicon"
}

# 3. Stop any running instance gracefully, then force-kill leftovers. A missing IPC server is
# expected on the first launch, so a native shutdown failure must not abort this script.
if (Test-Path -LiteralPath $shell -PathType Leaf) {
    $oldErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $shell --shutdown 2>$null | Out-Null
    $ErrorActionPreference = $oldErrorPreference
    Start-Sleep -Milliseconds 400
}
Get-Process -Name 'owo_core_service' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

# 4. Start.
Write-Output "Core service : $exe"
Write-Output "Lexicon      : $Lexicon  ($([math]::Round((Get-Item $Lexicon).Length / 1MB, 1)) MB)"
if ($Background) {
    $process = Start-Process -FilePath $exe -ArgumentList @('--lexicon', $Lexicon) -PassThru
    Write-Output "Loading the lexicon and waiting for the IPC service..."
    if (Test-Path -LiteralPath $shell -PathType Leaf) {
        $ready = $false
        for ($attempt = 0; $attempt -lt 30; $attempt++) {
            if ($process.HasExited) {
                throw "Core service exited during startup with code $($process.ExitCode)."
            }
            $oldErrorPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            & $shell nihao *> $null
            $probeExitCode = $LASTEXITCODE
            $ErrorActionPreference = $oldErrorPreference
            if ($probeExitCode -eq 0) {
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if (-not $ready) {
            throw "Core service did not become ready within the startup timeout."
        }
    }
    Write-Output "OwO is ready. PID=$($process.Id)"
} else {
    Write-Output "Starting (Ctrl+C to stop)..."
    & $exe --lexicon $Lexicon
}
