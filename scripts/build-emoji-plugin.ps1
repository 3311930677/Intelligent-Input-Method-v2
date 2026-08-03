<#
.SYNOPSIS
    Developer-only: build, sign and (optionally) install the OwO Emoji plugin end to end.

.DESCRIPTION
    1. Assembles a staging tree: package/manifest.json + package/config.json + bin/owo_emoji_plugin.exe.
    2. Calls scripts/build-dev-plugin.ps1 to compute the canonical inventory hash, produce a
       detached CMS signature with the developer test certificate, and pack the .owopkg.
    3. Optionally installs it through owo_plugin_shell install into a plugin store root.

    Requires the selected CMake preset to be built first (owo_emoji_plugin + owo_plugin_pack,
    and owo_plugin_shell when -Install is used).

.PARAMETER Configuration
    Debug or Release.

.PARAMETER Output
    Destination .owopkg path. Defaults to build/<preset>/owo-emoji.owopkg.

.PARAMETER Install
    When set, installs the signed package via owo_plugin_shell into -StoreRoot.

.PARAMETER StoreRoot
    Plugin store root used when -Install is set.
#>
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$Output,
    [switch]$Install,
    [string]$StoreRoot
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$buildDir = Join-Path $projectRoot "build/$preset/$Configuration"
foreach ($candidate in @('punct-v3', 'voice-ui-v1', 'windows-ninja-debug')) {
    $candidateDir = Join-Path $projectRoot "build/$candidate"
    if ($Configuration -eq 'Debug' -and
        (Test-Path -LiteralPath (Join-Path $candidateDir 'owo_emoji_plugin.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $candidateDir 'owo_plugin_pack.exe') -PathType Leaf)) {
        $preset = $candidate
        $buildDir = $candidateDir
        break
    }
}

$emojiExe = Join-Path $buildDir 'owo_emoji_plugin.exe'
$packTool = Join-Path $buildDir 'owo_plugin_pack.exe'
foreach ($required in @($emojiExe, $packTool)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing build artifact: $required. Build the '$preset' preset first."
    }
}

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $projectRoot "build/$preset/owo-emoji.owopkg"
}
$Output = [System.IO.Path]::GetFullPath($Output)

# 1. Assemble the staging tree from the versioned package sources plus the built exe.
$packageSource = Join-Path $projectRoot 'apps/emoji_plugin/package'
$staging = Join-Path ([System.IO.Path]::GetTempPath()) ("owo-emoji-staging-" + [System.Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $packageSource 'manifest.json') -Destination (Join-Path $staging 'manifest.json')
    Copy-Item -LiteralPath (Join-Path $packageSource 'config.json') -Destination (Join-Path $staging 'config.json')
    $binDir = Join-Path $staging 'bin'
    New-Item -ItemType Directory -Path $binDir | Out-Null
    Copy-Item -LiteralPath $emojiExe -Destination (Join-Path $binDir 'owo_emoji_plugin.exe')

    # 2. Sign and pack.
    & (Join-Path $PSScriptRoot 'build-dev-plugin.ps1') `
        -SourceDir $staging -Output $Output -PackTool $packTool -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "build-dev-plugin.ps1 failed with exit code $LASTEXITCODE" }
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "Signed emoji package: $Output"

# 3. Optional install.
if ($Install) {
    if ([string]::IsNullOrWhiteSpace($StoreRoot)) {
        $StoreRoot = Join-Path $env:LOCALAPPDATA 'OwO\InputMethod\plugins'
    }
    $StoreRoot = [System.IO.Path]::GetFullPath($StoreRoot)
    $shell = Join-Path $buildDir 'owo_plugin_shell.exe'
    if (-not (Test-Path -LiteralPath $shell -PathType Leaf)) {
        throw "owo_plugin_shell.exe not found: $shell. Build the '$preset' preset first."
    }
    Write-Output "Installing into store: $StoreRoot"
    & $shell $StoreRoot install $Output
    if ($LASTEXITCODE -ne 0) { throw "owo_plugin_shell install failed with exit code $LASTEXITCODE" }
    Write-Output "Emoji plugin installed."
}
