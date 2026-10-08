#pragma once
#include <windows.h>

#define WM_TRAYICON (WM_USER + 101)

#define ID_TRAY_SHOW        2001
#define ID_TRAY_HIDE        2002
#define ID_TRAY_TOPMOST     2003
#define ID_TRAY_COLOR_BASE  2100 // 2100 ~ 2105
#define ID_TRAY_EXIT        2099

class TrayManager {
public:
    static bool AddTrayIcon(HWND hwnd, int colorTheme);
    static bool UpdateTrayIcon(HWND hwnd, int colorTheme);
    static bool RemoveTrayIcon(HWND hwnd);
    static void ShowContextMenu(HWND hwnd, bool isTopMost, int currentTheme);
    static HICON CreateModernTrayIcon(int colorTheme);
};
