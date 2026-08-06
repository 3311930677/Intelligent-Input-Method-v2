# Regenerates packaging/portable/en_words.txt from the rime-ice English dictionary.
# en_words.txt is checked in (so builds/distribution do not depend on build/upstream),
# but this script documents how to refresh it when rime-ice is updated.
#
# Usage: powershell -File packaging/portable/build-en-words.ps1
# Requires: build/upstream/rime-ice-<ver>/en_dicts/en.dict.yaml
[CmdletBinding()]
param(
    [string]$Source = (Get-ChildItem 'd:\UGit\OwO-v2\build\upstream\rime-ice-*\en_dicts\en.dict.yaml' | Select-Object -First 1).FullName,
    [string]$Destination = 'd:\UGit\OwO-v2\packaging\portable\en_words.txt'
)
$utf8NoBom = New-Object Text.UTF8Encoding $false

$rimeBody = $false
$seen = [System.Collections.Generic.HashSet[string]]::new()
$words = Get-Content $Source -Encoding UTF8 | ForEach-Object {
    $line = $_
    if (-not $rimeBody) { if ($line -eq '...') { $rimeBody = $true }; return }
    if ($line -eq '' -or $line.StartsWith('#')) { return }
    $tab = $line.IndexOf("`t")
    if ($tab -le 0) { return }
    $text = $line.Substring(0, $tab).ToLower()
    # pure a-z, length>=2 (single letters are noise)
    if ($text -match '^[a-z]{2,}$') { if ($seen.Add($text)) { $text } }
}
[System.IO.File]::WriteAllLines($Destination, $words, $utf8NoBom)
Write-Output ('en_words.txt: ' + $words.Count + ' words, ' + (Get-Item $Destination).Length + 'B')
