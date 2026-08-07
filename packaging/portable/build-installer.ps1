# Compiles the OwO Input Method installer (setup.exe) with Inno Setup.
#
# Prerequisites:
#   * Release binaries in build\release-portable (build them first).
#   * Inno Setup 6 (ISCC.exe) installed.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File packaging\portable\build-installer.ps1

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resourceDir = $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build\release-portable'
$issPath = Join-Path $resourceDir 'installer.iss'

$binaries = @('OwO.TSF.dll', 'owo_core_service.exe', 'owo_config_shell.exe', 'owo_plugin_shell.exe', 'owo_voice_input_plugin.exe')

# ---- preflight: binaries exist and are NOT debug-CRT ----
foreach ($name in $binaries) {
    $path = Join-Path $buildDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing build artifact: $path (build the release-portable target first)"
    }
    $ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path))
    foreach ($debugDll in 'ucrtbased.dll', 'vcruntime140d.dll', 'MSVCP140D.dll') {
        if ($ascii.IndexOf($debugDll, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "$name links the debug CRT ($debugDll) and must not be shipped."
        }
    }
}
$lexicon = Join-Path $projectRoot 'build\windows-release\rime-ice-cn-2026.06.30.owolx'
if (-not (Test-Path -LiteralPath $lexicon -PathType Leaf)) {
    throw "Missing lexicon: $lexicon"
}

# ---- readme (ASCII filename for the .iss; content stays Chinese) ----
# Do not hardcode the Chinese filename here: Windows PowerShell 5.1 reads a
# BOM-less .ps1 as ANSI and would corrupt it. Discover the .txt instead, but
# exclude data/aux .txt files: the alphabetically-first entry wins, so
# en_words.txt (English word list shipped into bin\) would otherwise be
# installed as README.txt instead of the real 使用说明.txt.
$auxTextFiles = @('plugin-install-readme.txt', 'en_words.txt')
$readmeSrc = Get-ChildItem -LiteralPath $resourceDir -Filter '*.txt' -File |
    Where-Object { $auxTextFiles -notcontains $_.Name } |
    Select-Object -First 1
if (-not $readmeSrc) {
    throw "No readme .txt found in $resourceDir"
}
Copy-Item -LiteralPath $readmeSrc.FullName -Destination (Join-Path $buildDir 'readme.txt') -Force
# PowerShell script that adds OwO to the zh User Language List (run by [Run]).
Copy-Item -LiteralPath (Join-Path $resourceDir 'enable-owo-language.ps1') -Destination (Join-Path $buildDir 'enable-owo-language.ps1') -Force
# PowerShell script that registers Core as a scheduled task (run by [Run]).
Copy-Item -LiteralPath (Join-Path $resourceDir 'install-service.ps1') -Destination (Join-Path $buildDir 'install-service.ps1') -Force
Copy-Item -LiteralPath (Join-Path $resourceDir 'en_words.txt') -Destination (Join-Path $buildDir 'en_words.txt') -Force

# ---- locate ISCC ----
$isccCandidates = @(
    'C:\Users\henrytan\AppData\Local\Programs\Inno Setup 6\ISCC.exe',
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)
$iscc = $isccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $iscc) {
    $cmd = Get-Command ISCC -ErrorAction SilentlyContinue
    if ($cmd) { $iscc = $cmd.Source }
}
if (-not $iscc) { throw 'ISCC.exe (Inno Setup 6) not found.' }
Write-Output("ISCC: " + $iscc)

# ---- compile ----
$releasesDir = Join-Path $projectRoot 'web\releases'
New-Item -ItemType Directory -Path $releasesDir -Force | Out-Null

& $iscc $issPath
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

# ---- report ----
# Read the version from installer.iss instead of hardcoding it: a stale literal
# here makes the script hash the PREVIOUS release's installer, so the sha256
# published on the site would not match the file users download.
$issText = [IO.File]::ReadAllText($issPath)
$versionMatch = [regex]::Match($issText, '#define\s+AppVersion\s+"([^"]+)"')
if (-not $versionMatch.Success) { throw "Cannot read AppVersion from $issPath" }
$appVersion = $versionMatch.Groups[1].Value
$setup = Join-Path $releasesDir "OwO-InputMethod-Setup-$appVersion-win-x64.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    throw "Expected installer not produced: $setup"
}
$item = Get-Item -LiteralPath $setup
$sha = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLower()
Write-Output ''
Write-Output '===== installer built ====='
Write-Output ("file   : web/releases/" + $item.Name)
Write-Output ("size   : " + [math]::Round($item.Length / 1MB, 2) + " MB (" + $item.Length + " bytes)")
Write-Output ("sha256 : " + $sha)
