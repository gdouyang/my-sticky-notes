#pragma once
#include <windows.h>
#include <string>

namespace AutostartManager {
    inline bool IsAutostartEnabled() {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH];
            DWORD size = sizeof(path);
            DWORD type = 0;
            LONG res = RegQueryValueExW(hKey, L"MyStickyNotes", NULL, &type, (LPBYTE)path, &size);
            RegCloseKey(hKey);
            return (res == ERROR_SUCCESS);
        }
        return false;
    }

    inline bool SetAutostartEnabled(bool enable) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            if (enable) {
                wchar_t exePath[MAX_PATH];
                GetModuleFileNameW(NULL, exePath, MAX_PATH);
                std::wstring cmd = L"\"" + std::wstring(exePath) + L"\"";
                RegSetValueExW(hKey, L"MyStickyNotes", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.length() + 1) * sizeof(wchar_t)));
            } else {
                RegDeleteValueW(hKey, L"MyStickyNotes");
            }
            RegCloseKey(hKey);
            return true;
        }
        return false;
    }
}
