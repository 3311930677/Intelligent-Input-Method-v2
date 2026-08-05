$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'bin\OwO.TSF.dll'

Write-Host ''
Write-Host '=== OwO 输入法 卸载 ===' -ForegroundColor Cyan
Write-Host ''

Write-Host '[1/3] 停止后台服务并移除计划任务 ... ' -NoNewline
Get-Process -Name 'owo_core_service' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
try { Unregister-ScheduledTask -TaskName 'OwO Core Service' -Confirm:$false -ErrorAction Stop } catch {}
# 也清理旧版本可能遗留的启动目录快捷方式。
$startupDir = [Environment]::GetFolderPath('Startup')
$lnkPath = Join-Path $startupDir 'OwO Core Service.lnk'
if (Test-Path -LiteralPath $lnkPath) {
    Remove-Item -LiteralPath $lnkPath -Force -ErrorAction SilentlyContinue
}
Write-Host '完成' -ForegroundColor Green

Write-Host '[2/3] 从输入法切换列表移除 OwO ... ' -NoNewline
try { Unregister-ScheduledTask -TaskName 'OwO Core Service' -Confirm:$false -ErrorAction SilentlyContinue } catch {}
Write-Host '完成' -ForegroundColor Green

Write-Host '[3/3] 注销输入法 ... ' -NoNewline
if (Test-Path -LiteralPath $dll) {
    Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/u', '/s', $dll) -WindowStyle Hidden -Wait | Out-Null
}
Write-Host '完成' -ForegroundColor Green

Write-Host ''
Write-Host '卸载完成。' -ForegroundColor Green
Write-Host '用户配置仍保留在 %LOCALAPPDATA%\OwO ，如需彻底清除请手动删除该目录。'
Write-Host ''
