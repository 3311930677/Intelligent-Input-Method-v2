# Builds the LingXi (灵犀) overlay portable ZIP for distribution on the site.
#
# LingXi lives in the sibling repo `cross-app-assistant` (Rust/Tauri). This
# script only assembles an already-built Release overlay.exe into a portable
# ZIP under OwO-v2/web/plugins/. Build the overlay first:
#   cd ..\..\..\cross-app-assistant\apps\overlay && cargo build --release
#
# ASCII-only on purpose (Windows PowerShell 5.1 decodes a BOM-less .ps1 as ANSI
# and would corrupt non-ASCII). The Chinese readme is a separate file copied
# verbatim (re-encoded UTF-8 with BOM).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File packaging\lingxi\build-lingxi-zip.ps1

param(
    [string]$Version = '0.1.0',
    [string]$OverlayExe = ''
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # OwO-v2
$workspaceRoot = Split-Path -Parent $projectRoot                        # d:\UGit
$resourceDir = $PSScriptRoot
if (-not $OverlayExe) {
    $OverlayExe = Join-Path $workspaceRoot 'cross-app-assistant\apps\overlay\target\release\overlay.exe'
}
$packageName = "lingxi-overlay-$Version-win-x64"

# ---- preflight -----------------------------------------------------------
if (-not (Test-Path -LiteralPath $OverlayExe -PathType Leaf)) {
    throw "Missing overlay.exe: $OverlayExe (build it: cargo build --release in apps/overlay)"
}
$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($OverlayExe))
foreach ($debugDll in 'ucrtbased.dll', 'vcruntime140d.dll', 'MSVCP140D.dll') {
    if ($ascii.IndexOf($debugDll, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "overlay.exe links the debug CRT ($debugDll) and must not be distributed. Build Release."
    }
}
# The retired low-level keyboard hook must stay excluded from the shipped build.
if ($ascii.IndexOf('install keyboard hook', [StringComparison]::Ordinal) -ge 0) {
    throw "overlay.exe still contains the low-level keyboard hook code; refusing to ship."
}
$readme = Join-Path $resourceDir 'README-lingxi.txt'
if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) { throw "Missing readme: $readme" }

# ---- staging tree --------------------------------------------------------
$stagingRoot = Join-Path $projectRoot 'build\_lingxi_staging'
if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
$staging = Join-Path $stagingRoot $packageName
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item -LiteralPath $OverlayExe -Destination (Join-Path $staging 'overlay.exe') -Force

# Chinese readme, re-encoded as UTF-8 with BOM.
$utf8WithBom = New-Object System.Text.UTF8Encoding($true)
[IO.File]::WriteAllText((Join-Path $staging 'README-lingxi.txt'),
    [IO.File]::ReadAllText($readme), $utf8WithBom)

# ASCII launcher.
$asciiEncoding = New-Object System.Text.ASCIIEncoding
$crlf = [string][char]13 + [string][char]10
$bat = '@echo off' + $crlf +
    'start "" "%~dp0overlay.exe"' + $crlf
[IO.File]::WriteAllText((Join-Path $staging 'run-lingxi.bat'), $bat, $asciiEncoding)

# ---- zip -----------------------------------------------------------------
$pluginsDir = Join-Path $projectRoot 'web\plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
$zipPath = Join-Path $pluginsDir "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $stagingRoot, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -LiteralPath $stagingRoot -Recurse -Force

# ---- report --------------------------------------------------------------
$zip = Get-Item -LiteralPath $zipPath
$sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
Write-Output ''
Write-Output '===== lingxi package built ====='
Write-Output ("file   : web/plugins/" + $zip.Name)
Write-Output ("size   : " + [math]::Round($zip.Length / 1MB, 2) + " MB (" + $zip.Length + " bytes)")
Write-Output ("sha256 : " + $sha256)
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    foreach ($entry in ($archive.Entries | Sort-Object FullName)) {
        Write-Output ("  {0,9} KB  {1}" -f [math]::Round($entry.Length / 1KB), $entry.FullName)
    }
} finally { $archive.Dispose() }
