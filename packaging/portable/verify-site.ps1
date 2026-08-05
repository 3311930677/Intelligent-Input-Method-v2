# Validates that every download the website advertises actually resolves to a
# real file on disk, and that the JSON/JS the page loads is well formed.
$ErrorActionPreference = 'Stop'
$web = 'd:\UGit\OwO-v2\web'
$failures = 0

function Fail([string]$message) {
  Write-Output ("FAIL  " + $message)
    $script:failures++
}
function Pass([string]$message) {
    Write-Output ("ok    " + $message)
}

# ---- catalog.json ----
$catalogPath = Join-Path $web 'catalog.json'
try {
    $catalog = Get-Content -LiteralPath $catalogPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Pass 'catalog.json parses as JSON'
} catch {
    Fail ("catalog.json is not valid JSON: " + $_.Exception.Message)
    $catalog = $null
}

if ($catalog) {
    foreach ($plugin in $catalog.plugins) {
      $hasPackage = $plugin.PSObject.Properties.Name -contains 'package'
        if ($hasPackage) {
         $target = Join-Path $web $plugin.package
     if (Test-Path -LiteralPath $target -PathType Leaf) {
   $kb = [math]::Round((Get-Item -LiteralPath $target).Length / 1KB)
             Pass ("plugin '" + $plugin.name + "' -> " + $plugin.package + " ($kb KB)")
 } else {
       Fail ("plugin '" + $plugin.name + "' points at missing file: " + $plugin.package)
        }
        } else {
            Pass ("plugin '" + $plugin.name + "' has no package (button greyed out by design)")
        }

        # icon must exist too
        if ($plugin.icon) {
          $icon = Join-Path $web $plugin.icon
       if (-not (Test-Path -LiteralPath $icon -PathType Leaf)) {
     Fail ("plugin '" + $plugin.name + "' icon missing: " + $plugin.icon)
            }
        }
    }
}

# ---- config.js installer url ----
$configPath = Join-Path $web 'config.js'
$configText = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8
$urlMatch = [regex]::Match($configText, 'url:\s*"([^"]+)"')
if (-not $urlMatch.Success) {
    Fail 'could not find INSTALLER.url in config.js'
} else {
    $url = $urlMatch.Groups[1].Value
    if ($url -match '^https?://') {
        Pass ("installer url is external: " + $url)
    } else {
      $target = Join-Path $web $url
      if (Test-Path -LiteralPath $target -PathType Leaf) {
          $item = Get-Item -LiteralPath $target
         $mb = [math]::Round($item.Length / 1MB, 2)
         Pass ("installer url -> " + $url + " ($mb MB)")

   # cross-check the advertised size label against the real size
      $labelMatch = [regex]::Match($configText, 'sizeLabel:\s*"([^"]+)"')
       if ($labelMatch.Success) {
          $label = $labelMatch.Groups[1].Value
     $numberMatch = [regex]::Match($label, '([0-9]+(\.[0-9]+)?)')
                if ($numberMatch.Success) {
           $claimed = [double]$numberMatch.Groups[1].Value
      $delta = [math]::Abs($claimed - $mb)
        if ($delta -le 0.6) {
        Pass ("sizeLabel '" + $label + "' matches real size ($mb MB)")
            } else {
       Fail ("sizeLabel '" + $label + "' disagrees with real size ($mb MB)")
}
                }
       }
  } else {
         Fail ("installer url points at missing file: " + $url)
      }
    }
}

# ---- referenced assets in index.html ----
$html = Get-Content -LiteralPath (Join-Path $web 'index.html') -Raw -Encoding UTF8
foreach ($m in [regex]::Matches($html, '(?:src|href)="(?!https?://|#)([^"]+)"')) {
    $rel = $m.Groups[1].Value
    $target = Join-Path $web $rel
  if (-not (Test-Path -LiteralPath $target)) {
        Fail ("index.html references missing file: " + $rel)
    }
}
Pass 'index.html local references checked'

Write-Output ''
if ($failures -eq 0) {
    Write-Output '===== ALL CHECKS PASSED ====='
} else {
    Write-Output ("===== $failures CHECK(S) FAILED =====")
    exit 1
}
