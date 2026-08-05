# Builds and signs the emoji plugin .owopkg (Release) into web/plugins/.
#
# Prerequisites:
#   cmake --build build/release-portable --target owo_emoji_plugin owo_plugin_pack
#
# The signature uses scripts/build-dev-plugin.ps1 (self-signed developer cert).
# End users install the resulting package via install-plugin.ps1 which takes the
# "accept risk" path for non-trusted publishers.

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDir = Join-Path $projectRoot 'build\release-portable'
$emojiExe = Join-Path $buildDir 'owo_emoji_plugin.exe'
$packTool = Join-Path $buildDir 'owo_plugin_pack.exe'
$pkgSrc   = Join-Path $projectRoot 'apps\emoji_plugin\package'
$outPkg   = Join-Path $projectRoot 'web\plugins\owo-emoji-0.3.0.owopkg'

foreach ($p in @($emojiExe, $packTool)) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) {
        throw "Missing $p. Build: cmake --build build/release-portable --target owo_emoji_plugin owo_plugin_pack"
    }
}
# refuse debug-CRT plugin exe
$ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($emojiExe))
foreach ($d in 'ucrtbased.dll', 'vcruntime140d.dll', 'MSVCP140D.dll') {
    if ($ascii.IndexOf($d, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "owo_emoji_plugin.exe links the debug CRT ($d); build Release."
    }
}

$staging = Join-Path $env:TEMP ("owo-emoji-stage-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path (Join-Path $staging 'bin') -Force | Out-Null
Copy-Item (Join-Path $pkgSrc 'manifest.json') (Join-Path $staging 'manifest.json')
Copy-Item (Join-Path $pkgSrc 'config.json')   (Join-Path $staging 'config.json')
Copy-Item $emojiExe (Join-Path $staging 'bin\owo_emoji_plugin.exe')

New-Item -ItemType Directory -Force -Path (Split-Path $outPkg) | Out-Null
try {
    & (Join-Path $projectRoot 'scripts\build-dev-plugin.ps1') `
        -SourceDir $staging -Output $outPkg -PackTool $packTool -Configuration Release | Out-Null
} finally {
    Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue
}
if (-not (Test-Path $outPkg)) { throw'owopkg not produced' }
$item = Get-Item $outPkg
$sha = (Get-FileHash $outPkg -Algorithm SHA256).Hash.ToLower()
Write-Output ("owopkg : web/plugins/" + $item.Name + "  " + [math]::Round($item.Length/1KB) + " KB")
Write-Output ("sha256 : " + $sha)
