# Bundles the emoji plugin for end users: the signed .owopkg + a one-click
# install-plugin.ps1/.bat + a readme, zipped into web/plugins/.
# ASCII-only; localized files are re-encoded to UTF-8 with BOM when staged.

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$res = $PSScriptRoot
$owopkg = Join-Path $projectRoot 'web\plugins\owo-emoji-0.3.0.owopkg'
if (-not (Test-Path -LiteralPath $owopkg)) {
    throw "Missing $owopkg (build the emoji .owopkg first)."
}

$pkgName = 'OwO-Emoji-Plugin-0.3.0'
$stagingRoot = Join-Path $projectRoot 'build\_plugin_zip_staging'
if (Test-Path $stagingRoot) { Remove-Item -Recurse -Force $stagingRoot }
$staging = Join-Path $stagingRoot $pkgName
New-Item -ItemType Directory -Path $staging -Force | Out-Null

Copy-Item -LiteralPath $owopkg -Destination (Join-Path $staging 'owo-emoji-0.3.0.owopkg') -Force

$utf8Bom = New-Object System.Text.UTF8Encoding($true)
function Copy-Bom($src, $dst) {
    [IO.File]::WriteAllText($dst, [IO.File]::ReadAllText($src), $utf8Bom)
}
Copy-Bom (Join-Path $res 'install-plugin.ps1')(Join-Path $staging 'install-plugin.ps1')
Copy-Bom (Join-Path $res 'plugin-install-readme.txt')   (Join-Path $staging 'plugin-install-readme.txt')

$ascii = New-Object System.Text.ASCIIEncoding
$crlf = [string][char]13 + [string][char]10
$bat = '@echo off' + $crlf +
       'powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-plugin.ps1"' + $crlf +
       'echo.' + $crlf + 'pause' + $crlf
[IO.File]::WriteAllText((Join-Path $staging 'install-plugin.bat'), $bat, $ascii)

# validate the staged install script parses
$perr = $null; $tok = $null
[void][System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $staging 'install-plugin.ps1'), [ref]$tok, [ref]$perr)
if ($perr -and $perr.Count -gt 0) {
    foreach ($e in $perr) { Write-Output ("SYNTAX line " + $e.Extent.StartLineNumber + ": " + $e.Message) }
    throw 'install-plugin.ps1 failed to parse; zip not produced.'
}
Write-Output 'install-plugin.ps1 syntax: ok'

$zipPath = Join-Path $projectRoot 'web\plugins\owo-emoji-plugin-0.3.0.zip'
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stagingRoot, $zipPath,
    [IO.Compression.CompressionLevel]::Optimal, $false)
Remove-Item -Recurse -Force $stagingRoot

$zip = Get-Item $zipPath
$sha = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToLower()
Write-Output ''
Write-Output '===== plugin zip built ====='
Write-Output ("file   : web/plugins/" + $zip.Name)
Write-Output ("size   : " + [math]::Round($zip.Length/1KB) + " KB (" + $zip.Length + " bytes)")
Write-Output ("sha256 : " + $sha)
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try { foreach ($e in ($archive.Entries | Sort-Object FullName)) {
    Write-Output ("  " + [math]::Round($e.Length/1KB) + " KB  " + $e.FullName) } }
finally { $archive.Dispose() }
