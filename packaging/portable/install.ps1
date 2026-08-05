$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'bin\OwO.TSF.dll'
$exe = Join-Path $root 'bin\owo_core_service.exe'
$lexicon = Get-ChildItem (Join-Path $root 'lexicon') -Filter *.owolx -ErrorAction SilentlyContinue |
    Select-Object -First 1

Write-Host ''
Write-Host '=== OwO 输入法 安装 ===' -ForegroundColor Cyan
Write-Host ''

foreach ($p in @($dll, $exe)) {
    if (-not (Test-Path -LiteralPath $p)) { throw "缺少文件: $p" }
}
if (-not $lexicon) { throw '缺少词典文件 lexicon\*.owolx' }

# 1. 注册输入法。写入 HKEY_CURRENT_USER，不需要管理员权限。
Write-Host '[1/4] 注册输入法 ... ' -NoNewline
$proc = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @('/s', $dll) -WindowStyle Hidden -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    Write-Host '失败' -ForegroundColor Red
    throw "regsvr32 返回退出码 $($proc.ExitCode)"
}
Write-Host '完成' -ForegroundColor Green

# 2. 把 OwO 加入中文 User Language List，使其出现在 Win+空格 切换栏。
#    regsvr32 只注册 TSF 文本服务，不进切换栏；必须加到 User Language List。
Write-Host '[2/4] 加入输入法切换列表 ... ' -NoNewline
$enableScript = Join-Path $root 'tools\enable-owo-language.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $enableScript
Write-Host '完成' -ForegroundColor Green

# 3. 后台服务开机自启。快捷方式放当前用户启动目录，同样不需要管理员权限。
Write-Host '[3/4] 配置后台服务自启 ... ' -NoNewline
$startupDir = [Environment]::GetFolderPath('Startup')
$lnkPath = Join-Path $startupDir 'OwO Core Service.lnk'
$wshell = New-Object -ComObject WScript.Shell
$lnk = $wshell.CreateShortcut($lnkPath)
$lnk.TargetPath = $exe
$lnk.Arguments = '--lexicon "' + $lexicon.FullName + '"'
$lnk.WorkingDirectory = Join-Path $root 'bin'
$lnk.WindowStyle = 7
$lnk.Description = 'OwO Input Method core service'
$lnk.Save()
Write-Host '完成' -ForegroundColor Green

# 4. 立即启动后台服务。
Write-Host '[4/4] 启动后台服务 ... ' -NoNewline
Get-Process -Name 'owo_core_service' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Start-Process -FilePath $exe -ArgumentList @('--lexicon', $lexicon.FullName) -WindowStyle Hidden | Out-Null
Write-Host '完成' -ForegroundColor Green

Write-Host ''
Write-Host '安装完成。' -ForegroundColor Green
Write-Host ''
Write-Host '接下来：'
Write-Host ('  1. 词典约 ' + [math]::Round($lexicon.Length / 1MB) + ' MB，后台服务首次加载需要十几秒，请稍等再打字。')
Write-Host '  2. 用 Win+空格 切换到「OwO 输入法」。'
Write-Host '  3. 若切换列表里没有，打开 设置 - 时间和语言 - 语言和区域，'
Write-Host '     在 中文(简体) 的 语言选项 - 键盘 里添加 OwO 输入法。'
Write-Host '  4. 已经打开的程序需要重启才能加载新输入法（Windows TSF 的限制）。'
Write-Host ''
