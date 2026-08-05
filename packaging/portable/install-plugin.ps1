param(
    [string]$Package,
    [string]$Shell
)
$ErrorActionPreference = 'Stop'

Write-Host ''
Write-Host '=== OwO 插件安装 ===' -ForegroundColor Cyan
Write-Host ''

# 1. 定位插件包（默认取脚本同目录里的 *.owopkg）
if (-not $Package) {
    $found = Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.owopkg' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $Package = $found.FullName }
}
if (-not $Package -or -not (Test-Path -LiteralPath $Package)) {
    throw '找不到插件包 (.owopkg)。请把本脚本和.owopkg 放在同一目录，或用 -Package 指定。'
}

# 2. 定位 owo_plugin_shell.exe（随本体一起安装）
if (-not $Shell) {
    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\OwO\bin\owo_plugin_shell.exe'),
        (Join-Path $PSScriptRoot 'owo_plugin_shell.exe'),
        (Join-Path $PSScriptRoot '..\bin\owo_plugin_shell.exe')
    )
    $Shell = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $Shell) {
    throw '找不到 owo_plugin_shell.exe。请先安装 OwO 输入法本体，或用 -Shell 指定本体 bin 目录下的 owo_plugin_shell.exe。'
}

# 3. 插件仓库目录（与后台服务默认加载路径一致）
$store = Join-Path $env:LOCALAPPDATA 'OwO\InputMethod\plugins'
New-Item -ItemType Directory -Force -Path $store | Out-Null

Write-Host ("插件包 : " + $Package)
Write-Host ("管理器 : " + $Shell)
Write-Host ("仓库   : " + $store)
Write-Host ''

# 4. 检查包
$inspect = (& $Shell $store 'inspect-install' $Package | Out-String).Trim() | ConvertFrom-Json
if (-not $inspect.ok) { throw ('插件包检查失败：' + $inspect.diagnostic) }
Write-Host ('插件   : ' + $inspect.name + ' (' + $inspect.plugin_id + ') v' + $inspect.version)
Write-Host ('信任级 : ' + $inspect.trust_tier + '    风险: ' + $inspect.risk_level)

# 5. 安装：受信发布者可静默装；否则走“接受风险”路径
if ($inspect.trust_tier -eq 'trusted_publisher') {
    $result = (& $Shell $store 'install' $Package | Out-String).Trim() | ConvertFrom-Json
} else {
    Write-Host ''
    Write-Host '注意：该插件由开发者证书签名（非受信 CA），将以「接受风险」方式安装。' -ForegroundColor Yellow
    $result = (& $Shell $store 'install-risk' $Package $inspect.inventory_sha256 '1' 'I_ACCEPT_PLUGIN_RISK_V1' | Out-String).Trim() | ConvertFrom-Json
}
if (-not $result.version_published) { throw ('安装失败：' + $result.diagnostic) }

# 6. 激活
$activate = (& $Shell $store 'activate' $inspect.plugin_id $inspect.version | Out-String).Trim() | ConvertFrom-Json
if (-not $activate.ok) { throw ('激活失败：' + $activate.diagnostic) }

Write-Host ''
Write-Host '插件已安装并激活。' -ForegroundColor Green
Write-Host ''
Write-Host '提示：'
Write-Host '  1. 重启 OwO 后台服务（或重新登录 Windows）后插件才会被加载。'
Write-Host '  2. 已经打开的程序需要重启才能感知变化。'
Write-Host ''
