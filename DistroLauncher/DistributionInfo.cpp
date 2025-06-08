#include <windows.h>
#include <shlobj.h> // برای SHGetKnownFolderPath
#include <string>
#include "stdafx.h"

// تابع برای اجرای دستور PowerShell و بازگرداندن خروجی
bool RunPowerShellCommand(const std::wstring& command, DWORD* exitCode = nullptr) {
    DWORD dwExitCode = 0;
    HRESULT hr = g_wslApi.WslLaunchInteractive(command.c_str(), true, &dwExitCode);
    
    if (exitCode) {
        *exitCode = dwExitCode;
    }
    
    return SUCCEEDED(hr) && (dwExitCode == 0);
}

// تابع اصلی برای اضافه کردن پروفایل Zenora به Windows Terminal
bool AddZenoraProfileToWindowsTerminal() {
    // دستور PowerShell برای ویرایش settings.json
    const std::wstring psScript = LR"(
        $settingsPath = "$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json"
        
        # اگر فایل وجود نداشت، یک ساختار پایه ایجاد کن
        if (!(Test-Path $settingsPath)) {
            $defaultSettings = @{
                '$schema' = "https://aka.ms/terminal-profiles-schema"
                profiles = @{
                    list = @()
                }
            }
            $defaultSettings | ConvertTo-Json -Depth 3 | Set-Content $settingsPath
        }
        
        # خواندن تنظیمات فعلی
        $settings = Get-Content $settingsPath -Raw | ConvertFrom-Json
        
        # بررسی وجود پروفایل Zenora (برای جلوگیری از تکرار)
        $profileExists = $settings.profiles.list | Where-Object { $_.name -eq "Zenora" }
        
        if (-not $profileExists) {
            # ایجاد پروفایل جدید
            $zenoraProfile = @{
                guid = "{a79cd884-4081-569f-9e90-570201e5b7c4}"
                name = "Zenora"
                fontFace = "0xProto Nerd Font Mono"
                fontSize = 12
                useAcrylic = $true
                acrylicOpacity = 0.8
                hidden = $false
                source = "Windows.Terminal.Wsl"
            }
            
            # اضافه کردن پروفایل به لیست
            $settings.profiles.list += $zenoraProfile
            
            # ذخیره تغییرات
            $settings | ConvertTo-Json -Depth 10 | Set-Content $settingsPath
        }
    )";

    // اجرای اسکریپت PowerShell
    return RunPowerShellCommand(L"powershell -Command \"" + psScript + L"\"");
}
