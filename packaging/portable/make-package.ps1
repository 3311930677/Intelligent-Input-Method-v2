# Builds the OwO Input Method portable ZIP for distribution.
#
# This script is intentionally ASCII-only: Windows PowerShell 5.1 decodes a .ps1
# without a BOM as ANSI, which corrupts non-ASCII source. All localized text
# therefore lives in separate files under this directory and is copied verbatim
# (re-encoded as UTF-8 with BOM so PowerShell and Notepad both read it correctly).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File packaging\portable\make-package.ps1

param(
    [string]$Version = '0.9.0',
    [string]$BuildDir = 'build\release-portable',
  [string]$Lexicon = 'build\windows-release\rime-ice-cn-2026.06.30.owolx'
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resourceDir = $PSScriptRoot
$buildDir = Join-Path $projectRoot $BuildDir
$lexiconPath = Join-Path $projectRoot $Lexicon
$packageName = "OwO-InputMethod-$Version-win-x64"

$binaries = @(
    'OwO.TSF.dll',
    'owo_core_service.exe',
    'owo_config_shell.exe',
    'owo_plugin_shell.exe',
    # SAPI voice backend. VoiceProcessController launches this as a sibling of
    # owo_core_service.exe, so it must live next to it in bin\ for F9 voice input.
    'owo_voice_input_plugin.exe'
)

# ---- preflight -----------------------------------------------------------
foreach ($name in $binaries) {
    $path = Join-Path $buildDir $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing build artifact: $path"
    }
    # Refuse to ship debug-CRT binaries: they need ucrtbased/vcruntime140d,
    # which are not redistributable and absent on end-user machines.
    $ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path))
    foreach ($debugDll in 'ucrtbased.dll', 'vcruntime140d.dll', 'MSVCP140D.dll') {
      if ($ascii.IndexOf($debugDll, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw "$name links the debug CRT ($debugDll) and must not be distributed. Build a Release configuration."
        }
    }
}
if (-not (Test-Path -LiteralPath $lexiconPath -PathType Leaf)) {
    throw "Missing lexicon: $lexiconPath"
}

# ---- staging tree --------------------------------------------------------
$stagingRoot = Join-Path $projectRoot 'build\_pkg_staging'
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
$staging = Join-Path $stagingRoot $packageName
foreach ($sub in '', 'bin', 'lexicon', 'tools') {
    $dir = if ($sub -eq '') { $staging } else { Join-Path $staging $sub }
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

foreach ($name in $binaries) {
    Copy-Item -LiteralPath (Join-Path $buildDir $name) -Destination (Join-Path $staging "bin\$name") -Force
}
Copy-Item -LiteralPath $lexiconPath -Destination (Join-Path $staging 'lexicon') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination (Join-Path $staging 'LICENSE') -Force

# ---- localized resources, re-encoded as UTF-8 with BOM -------------------
$utf8WithBom = New-Object System.Text.UTF8Encoding($true)

function Copy-AsUtf8Bom([string]$source, [string]$destination) {
    $text = [IO.File]::ReadAllText($source)
    [IO.File]::WriteAllText($destination, $text, $utf8WithBom)
}

Copy-AsUtf8Bom (Join-Path $resourceDir 'install.ps1')   (Join-Path $staging 'tools\install.ps1')
Copy-AsUtf8Bom (Join-Path $resourceDir 'uninstall.ps1') (Join-Path $staging 'tools\uninstall.ps1')
Copy-AsUtf8Bom (Join-Path $resourceDir 'enable-owo-language.ps1') (Join-Path $staging 'tools\enable-owo-language.ps1')
Copy-AsUtf8Bom (Join-Path $resourceDir 'install-service.ps1') (Join-Path $staging 'tools\install-service.ps1')

# The readme filename is localized; discover it instead of hardcoding non-ASCII.
# Exclude the plugin-specific readme (ASCII name) so the body's使用说明.txt wins
# even though several .txt files now live in this directory.
$readme = Get-ChildItem -LiteralPath $resourceDir -Filter '*.txt' -File |
    Where-Object { $_.Name -ne 'plugin-install-readme.txt' } |
    Select-Object -First 1
if (-not $readme) { throw "No readme .txt found in $resourceDir" }
Copy-AsUtf8Bom $readme.FullName (Join-Path $staging $readme.Name)

# ---- ASCII batch wrappers ------------------------------------------------
$asciiEncoding = New-Object System.Text.ASCIIEncoding
$crlf = [string][char]13 + [string][char]10

$installBat = '@echo off' + $crlf +
    'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\install.ps1"' + $crlf +
    'echo.' + $crlf +
    'pause' + $crlf
[IO.File]::WriteAllText((Join-Path $staging 'install.bat'), $installBat, $asciiEncoding)

$uninstallBat = '@echo off' + $crlf +
    'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\uninstall.ps1"' + $crlf +
    'echo.' + $crlf +
    'pause' + $crlf
[IO.File]::WriteAllText((Join-Path $staging 'uninstall.bat'), $uninstallBat, $asciiEncoding)

# ---- validate the staged scripts actually parse --------------------------
$syntaxFailed = $false
foreach ($script in 'tools\install.ps1', 'tools\uninstall.ps1') {
    $path = Join-Path $staging $script
    $parseErrors = $null
    $tokens = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors -and $parseErrors.Count -gt 0) {
        $syntaxFailed = $true
        Write-Output "SYNTAX ERROR in $script :"
        foreach ($parseError in $parseErrors) {
      Write-Output ("  line " + $parseError.Extent.StartLineNumber + ": " + $parseError.Message)
        }
    } else {
Write-Output "syntax OK : $script"
    }
}
if ($syntaxFailed) { throw 'Staged PowerShell scripts failed to parse; package not produced.' }

# ---- zip -----------------------------------------------------------------
$releasesDir = Join-Path $projectRoot 'web\releases'
New-Item -ItemType Directory -Path $releasesDir -Force | Out-Null
$zipPath = Join-Path $releasesDir "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory(
    $stagingRoot,
    $zipPath,
  [IO.Compression.CompressionLevel]::Optimal,
    $false)

Remove-Item -LiteralPath $stagingRoot -Recurse -Force

# ---- report --------------------------------------------------------------
$zip = Get-Item -LiteralPath $zipPath
$sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
$sizeMb = [math]::Round($zip.Length / 1MB, 2)

Write-Output ''
Write-Output '===== package built ====='
Write-Output ("file   : web/releases/" + $zip.Name)
Write-Output ("size   : $sizeMb MB ($($zip.Length) bytes)")
Write-Output ("sha256 : $sha256")
Write-Output ''
Write-Output 'entries:'
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    foreach ($entry in ($archive.Entries | Sort-Object FullName)) {
        Write-Output ("  {0,9} KB  {1}" -f [math]::Round($entry.Length / 1KB), $entry.FullName)
    }
}
finally {
    $archive.Dispose()
}
