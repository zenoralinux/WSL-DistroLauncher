//
//    Copyright (C) Microsoft.  All rights reserved.
// Licensed under the terms described in the LICENSE file in the root of this project.
//

#include "stdafx.h"
#include <Windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>  // حتما کتابخانه nlohmann/json را به پروژه اضافه کنید

// Commandline arguments: 
#define ARG_CONFIG              L"config"
#define ARG_CONFIG_DEFAULT_USER L"--default-user"
#define ARG_INSTALL             L"install"
#define ARG_INSTALL_ROOT        L"--root"
#define ARG_RUN                 L"run"
#define ARG_RUN_C               L"-c"

// Helper class for calling WSL Functions:
// https://msdn.microsoft.com/en-us/library/windows/desktop/mt826874(v=vs.85).aspx
WslApiLoader g_wslApi(DistributionInfo::Name);

using json = nlohmann::json;

// تابع نصب فونت‌ها
void InstallFontsFromWSL()
{
    // مسیر پوشه فونت‌ها - این را با مسیر واقعی برنامه خودتان تنظیم کنید
    std::wstring appDir = L""; // اگر راهی برای تعیین دایرکتوری اپ دارید اینجا مقدار دهید
    std::wstring fontsDir = appDir + L"fonts";

    std::wstring fontsFolder = L"C:\\Windows\\Fonts";

    if (!std::filesystem::exists(fontsDir)) {
        // پوشه فونت‌ها وجود ندارد، نصب انجام نمی‌شود
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(fontsDir)) {
        if (entry.is_regular_file() && entry.path().extension() == L".ttf") {
            std::wstring fontFile = entry.path().wstring();
            std::wstring destFile = fontsFolder + L"\\" + entry.path().filename().wstring();

            // کپی فایل فونت (با overwrite)
            if (CopyFile(fontFile.c_str(), destFile.c_str(), FALSE)) {
                // اضافه کردن به رجیستری فونت‌ها
                std::wstring fontName = entry.path().filename().stem().wstring();
                std::wstring regKeyPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
                HKEY hKey;

                if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, regKeyPath.c_str(), 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
                    std::wstring regValueName = fontName + L" (TrueType)";
                    std::wstring regValueData = entry.path().filename().wstring();
                    RegSetValueEx(hKey, regValueName.c_str(), 0, REG_SZ,
                                  (const BYTE*)regValueData.c_str(),
                                  (regValueData.size() + 1) * sizeof(wchar_t));
                    RegCloseKey(hKey);
                }
            }
        }
    }
}

// تابع اضافه کردن پروفایل ویندوز ترمینال
void AddTerminalProfile()
{
    // مسیر فایل تنظیمات ویندوز ترمینال
    std::wstring userProfile = _wgetenv(L"USERPROFILE");
    if (userProfile.empty()) {
        return;
    }

    std::wstring settingsPath = userProfile + L"\\AppData\\Local\\Packages\\Microsoft.WindowsTerminal_8wekyb3d8bbwe\\LocalState\\settings.json";

    std::ifstream settingsFile(settingsPath);
    if (!settingsFile.is_open()) {
        return;
    }

    json settings;
    try {
        settingsFile >> settings;
    } catch (...) {
        settingsFile.close();
        return;
    }
    settingsFile.close();

    // بررسی وجود پروفایل با GUID مشخص
    std::string targetGuid = "{a79cd884-4081-569f-9e90-570201e5b7c4}";
    bool profileExists = false;

    for (auto& profile : settings["profiles"]["list"]) {
        if (profile.contains("guid") && profile["guid"] == targetGuid) {
            profileExists = true;
            break;
        }
    }

    if (!profileExists) {
        json newProfile = {
            {"guid", targetGuid},
            {"name", "Zenora"},
            {"source", "Windows.Terminal.Wsl"},
            {"font", {
                {"face", "0xProto Nerd Font Mono"},
                {"size", 11}
            }},
            {"hidden", false}
        };

        settings["profiles"]["list"].push_back(newProfile);

        std::ofstream outFile(settingsPath);
        if (outFile.is_open()) {
            outFile << settings.dump(4);
            outFile.close();
        }
    }
}

static HRESULT InstallDistribution(bool createUser);
static HRESULT SetDefaultUser(std::wstring_view userName);

HRESULT InstallDistribution(bool createUser)
{
    // Register the distribution.
    Helpers::PrintMessage(MSG_STATUS_INSTALLING);
    HRESULT hr = g_wslApi.WslRegisterDistribution();
    if (FAILED(hr)) {
        return hr;
    }

    // Delete /etc/resolv.conf to allow WSL to generate a version based on Windows networking information.
    DWORD exitCode;
    hr = g_wslApi.WslLaunchInteractive(L"/bin/rm /etc/resolv.conf", true, &exitCode);
    if (FAILED(hr)) {
        return hr;
    }

    // Create a user account.
    if (createUser) {
        Helpers::PrintMessage(MSG_CREATE_USER_PROMPT);
        std::wstring userName;
        do {
            userName = Helpers::GetUserInput(MSG_ENTER_USERNAME, 32);

        } while (!DistributionInfo::CreateUser(userName));

        // Set this user account as the default.
        hr = SetDefaultUser(userName);
        if (FAILED(hr)) {
            return hr;
        }
    }

    // نصب فونت‌ها
    InstallFontsFromWSL();

    // اضافه کردن پروفایل ویندوز ترمینال
    AddTerminalProfile();

    return hr;
}

HRESULT SetDefaultUser(std::wstring_view userName)
{
    // Query the UID of the given user name and configure the distribution
    // to use this UID as the default.
    ULONG uid = DistributionInfo::QueryUid(userName);
    if (uid == UID_INVALID) {
        return E_INVALIDARG;
    }

    HRESULT hr = g_wslApi.WslConfigureDistribution(uid, WSL_DISTRIBUTION_FLAGS_DEFAULT);
    if (FAILED(hr)) {
        return hr;
    }

    return hr;
}

int wmain(int argc, wchar_t const *argv[])
{
    // Update the title bar of the console window.
    SetConsoleTitleW(DistributionInfo::WindowTitle.c_str());

    // Initialize a vector of arguments.
    std::vector<std::wstring_view> arguments;
    for (int index = 1; index < argc; index += 1) {
        arguments.push_back(argv[index]);
    }

    // Ensure that the Windows Subsystem for Linux optional component is installed.
    DWORD exitCode = 1;
    if (!g_wslApi.WslIsOptionalComponentInstalled()) {
        Helpers::PrintErrorMessage(HRESULT_FROM_WIN32(ERROR_LINUX_SUBSYSTEM_NOT_PRESENT));
        if (arguments.empty()) {
            Helpers::PromptForInput();
        }

        return exitCode;
    }

    // Install the distribution if it is not already.
    bool installOnly = ((arguments.size() > 0) && (arguments[0] == ARG_INSTALL));
    HRESULT hr = S_OK;
    if (!g_wslApi.WslIsDistributionRegistered()) {

        // If the "--root" option is specified, do not create a user account.
        bool useRoot = ((installOnly) && (arguments.size() > 1) && (arguments[1] == ARG_INSTALL_ROOT));
        hr = InstallDistribution(!useRoot);
        if (FAILED(hr)) {
            if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
                Helpers::PrintMessage(MSG_INSTALL_ALREADY_EXISTS);
            }

        } else {
            Helpers::PrintMessage(MSG_INSTALL_SUCCESS);
        }

        exitCode = SUCCEEDED(hr) ? 0 : 1;
    }

    // Parse the command line arguments.
    if ((SUCCEEDED(hr)) && (!installOnly)) {
        if (arguments.empty()) {
            hr = g_wslApi.WslLaunchInteractive(L"", false, &exitCode);

            // Check exitCode to see if wsl.exe returned that it could not start the Linux process
            // then prompt users for input so they can view the error message.
            if (SUCCEEDED(hr) && exitCode == UINT_MAX) {
                Helpers::PromptForInput();
            }

        } else if ((arguments[0] == ARG_RUN) ||
                   (arguments[0] == ARG_RUN_C)) {

            std::wstring command;
            for (size_t index = 1; index < arguments.size(); index += 1) {
                command += L" ";
                command += arguments[index];
            }

            hr = g_wslApi.WslLaunchInteractive(command.c_str(), true, &exitCode);

        } else if (arguments[0] == ARG_CONFIG) {
            hr = E_INVALIDARG;
            if (arguments.size() == 3) {
                if (arguments[1] == ARG_CONFIG_DEFAULT_USER) {
                    hr = SetDefaultUser(arguments[2]);
                }
            }

            if (SUCCEEDED(hr)) {
                exitCode = 0;
            }

        } else {
            Helpers::PrintMessage(MSG_USAGE);
            return exitCode;
        }
    }

    // If an error was encountered, print an error message.
    if (FAILED(hr)) {
        if (hr == HCS_E_HYPERV_NOT_INSTALLED) {
            Helpers::PrintMessage(MSG_ENABLE_VIRTUALIZATION);

        } else {
            Helpers::PrintErrorMessage(hr);
        }

        if (arguments.empty()) {
            Helpers::PromptForInput();
        }
    }

    return SUCCEEDED(hr) ? exitCode : 1;
}
