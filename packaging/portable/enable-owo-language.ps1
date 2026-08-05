# Adds the OwO TSF input method to the user's zh User Language List so it shows
# up in the Win+Space input switcher after install. Idempotent and non-fatal.
#
# Why this is needed: regsvr32 / DllRegisterServer registers the TSF text service
# and its language profile, but Windows 10/11's switcher is driven by the User
# Language List (Get/Set-WinUserLanguageList). A TIP that is only registered at
# the TSF layer will not appear in the switcher until it is added to that list,
# which is why users saw "no OwO in the input list" after installing.
#
# ASCII-only on purpose (Windows PowerShell 5.1 decodes a BOM-less .ps1 as ANSI).

$owoTip = '0804:{6d31c9b1-8978-4f49-89b4-66eb1e741591}{5d9f39c3-bdb4-453c-a7ba-b9ef82487629}'

try {
    $list = Get-WinUserLanguageList
    $zh = $list | Where-Object { $_.LanguageTag -like 'zh-*' } | Select-Object -First 1
    if (-not $zh) {
        $list.Add('zh-CN')
        Set-WinUserLanguageList $list -Force
        $list = Get-WinUserLanguageList
        $zh = $list | Where-Object { $_.LanguageTag -like 'zh-*' } | Select-Object -First 1
    }
    if ($zh -and $zh.InputMethodTips -notcontains $owoTip) {
        $zh.InputMethodTips.Add($owoTip)
        Set-WinUserLanguageList $list -Force
    }
    exit 0
} catch {
    # If we cannot adjust the language list, do not fail the whole install;
    # the user can still add OwO manually via Settings -> Language options.
    Write-Output ('enable-owo-language warning: ' + $_.Exception.Message)
    exit 0
}
