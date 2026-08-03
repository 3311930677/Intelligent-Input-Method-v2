<#
.SYNOPSIS
    Developer-only: remove the OwO plugin developer test certificate and its temporary trust.

.DESCRIPTION
    Reverses scripts/build-dev-plugin.ps1. Removes the self-signed code-signing test
    certificate from both the CurrentUser personal (My) store and the CurrentUser Root
    trust store, so the machine no longer trusts developer-signed .owopkg packages.

    This does not touch installed plugins or any production trust configuration.
#>
param()

$ErrorActionPreference = 'Stop'
$certSubject = 'CN=OwO Plugin Developer Test'

function Remove-FromStore {
    param([string]$StoreName)
    $store = New-Object System.Security.Cryptography.X509Certificates.X509Store($StoreName, 'CurrentUser')
    $store.Open('ReadWrite')
    try {
        $matches = $store.Certificates | Where-Object { $_.Subject -eq $certSubject }
        foreach ($cert in $matches) {
            Write-Output "Removing $($cert.Thumbprint) from CurrentUser\$StoreName"
            $store.Remove($cert)
        }
        if (-not $matches) {
            Write-Output "No matching certificate in CurrentUser\$StoreName"
        }
    }
    finally {
        $store.Close()
    }
}

Remove-FromStore -StoreName 'Root'
Remove-FromStore -StoreName 'My'

Write-Output ""
Write-Output "Developer plugin test certificate and temporary trust removed."
