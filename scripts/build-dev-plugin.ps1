<#
.SYNOPSIS
    Developer-only: build and sign an .owopkg with a local self-signed test certificate.

.DESCRIPTION
    P3C intentionally ships no unsigned developer mode. The production installer only
    accepts packages whose detached CMS signature chains to a certificate trusted by the
    local machine and carries the code-signing EKU. This script keeps that security model
    completely intact: it does NOT modify any verification code. Instead it:

      1. Creates a self-signed code-signing test certificate (if absent).
      2. Temporarily trusts that certificate's root on THIS machine only.
      3. Computes the canonical inventory hash with owo_plugin_pack (byte-identical to
         inspect_package).
      4. Produces a detached CMS SHA-256 signature over "OwOPackageInventoryV1:<hash>\n".
      5. Writes signature.json and packs the final .owopkg.

    The certificate lives only in the current user's store and can be removed with
    scripts/remove-dev-plugin-cert.ps1. Never use this flow to sign packages for release.

.PARAMETER SourceDir
    Directory containing manifest.json and the plugin payload (e.g. bin/*.exe).

.PARAMETER Output
    Destination .owopkg path.

.PARAMETER PackTool
    Path to owo_plugin_pack.exe. Defaults to the Debug build output.

.PARAMETER Configuration
    Debug or Release, used to locate owo_plugin_pack.exe when -PackTool is omitted.
#>
param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$PackTool,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq 'Debug') { 'windows-debug' } else { 'windows-release' }

# The PKCS #7 / CMS types live in System.Security (Windows PowerShell) or must be loaded
# explicitly; ensure the assembly is available before using SignedCms.
Add-Type -AssemblyName System.Security -ErrorAction SilentlyContinue

if ([string]::IsNullOrWhiteSpace($PackTool)) {
    $ninjaTool = Join-Path $projectRoot 'build/windows-ninja-debug/owo_plugin_pack.exe'
    if ($Configuration -eq 'Debug' -and (Test-Path -LiteralPath $ninjaTool -PathType Leaf)) {
        $PackTool = $ninjaTool
    } else {
        $PackTool = Join-Path $projectRoot "build/$preset/$Configuration/owo_plugin_pack.exe"
    }
}
$PackTool = [System.IO.Path]::GetFullPath($PackTool)
if (-not (Test-Path -LiteralPath $PackTool -PathType Leaf)) {
    throw "owo_plugin_pack.exe not found: $PackTool. Build the '$preset' preset first."
}

$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Source directory not found: $SourceDir"
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir 'manifest.json') -PathType Leaf)) {
    throw "Source directory must contain manifest.json: $SourceDir"
}

$certSubject = 'CN=OwO Plugin Developer Test'

# 1. Ensure a self-signed code-signing test certificate exists in the current user store.
$cert = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $certSubject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if ($null -eq $cert) {
    Write-Output "Creating self-signed code-signing test certificate: $certSubject"
    $cert = New-SelfSignedCertificate `
        -Subject $certSubject `
        -Type CodeSigningCert `
        -KeyUsage DigitalSignature `
        -KeyExportPolicy Exportable `
        -CertStoreLocation Cert:\CurrentUser\My `
        -NotAfter (Get-Date).AddYears(2)
}
$thumbprint = $cert.Thumbprint
Write-Output "Using certificate thumbprint: $thumbprint"

# 2. Temporarily trust the certificate as a root on THIS machine (current user root store).
$rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store('Root', 'CurrentUser')
$rootStore.Open('ReadWrite')
try {
    $already = $rootStore.Certificates | Where-Object { $_.Thumbprint -eq $thumbprint }
    if (-not $already) {
        Write-Output "Adding test certificate to CurrentUser Root store (development trust only)."
        $rootStore.Add($cert)
    }
}
finally {
    $rootStore.Close()
}

# 3. Compute the canonical inventory hash using the C++ packer (byte-identical to inspect_package).
Write-Output "Computing canonical inventory hash..."
$inventory = (& $PackTool --inventory $SourceDir).Trim()
if ($LASTEXITCODE -ne 0 -or $inventory -notmatch '^[0-9a-f]{64}$') {
    throw "owo_plugin_pack --inventory failed or returned an invalid hash: '$inventory'"
}
Write-Output "inventory_sha256 = $inventory"

# 4. Produce a detached CMS SHA-256 signature over the domain-separated content bytes.
$content = [System.Text.Encoding]::ASCII.GetBytes("OwOPackageInventoryV1:$inventory`n")
$contentInfo = New-Object System.Security.Cryptography.Pkcs.ContentInfo(, $content)
$signedCms = New-Object System.Security.Cryptography.Pkcs.SignedCms($contentInfo, $true)  # detached.
$signer = New-Object System.Security.Cryptography.Pkcs.CmsSigner($cert)
$signer.DigestAlgorithm = New-Object System.Security.Cryptography.Oid('2.16.840.1.101.3.4.2.1')  # SHA-256.
$signer.IncludeOption = [System.Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
$signedCms.ComputeSignature($signer)
$cmsDer = $signedCms.Encode()
$signatureBase64 = [System.Convert]::ToBase64String($cmsDer)

# 5. Write signature.json and pack the final .owopkg.
$signatureJson = "{`"schema_version`":1,`"inventory_sha256`":`"$inventory`",`"format`":`"cms-detached-sha256`",`"signature_base64`":`"$signatureBase64`"}"
$signatureFile = [System.IO.Path]::Combine([System.IO.Path]::GetTempPath(), "owo-signature-$([System.Guid]::NewGuid().ToString('N')).json")
[System.IO.File]::WriteAllText($signatureFile, $signatureJson, (New-Object System.Text.UTF8Encoding($false)))

try {
    $Output = [System.IO.Path]::GetFullPath($Output)
    Write-Output "Packing signed package -> $Output"
    & $PackTool --pack $SourceDir $Output --signature $signatureFile
    if ($LASTEXITCODE -ne 0) { throw "owo_plugin_pack --pack failed with exit code $LASTEXITCODE" }
}
finally {
    Remove-Item -LiteralPath $signatureFile -Force -ErrorAction SilentlyContinue
}

Write-Output ""
Write-Output "Done. Signed development package created: $Output"
Write-Output "Remove development trust when finished: scripts/remove-dev-plugin-cert.ps1"
