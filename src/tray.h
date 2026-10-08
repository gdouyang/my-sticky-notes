#pragma once
#include <windows.h>

#define WM_TRAYICON (WM_USER + 101)
#define ID_TRAY_EXIT 2001
#define ID_TRAY_TOGGLE 2002
#define ID_TRAY_TOPMOST 2003
#define ID_TRAY_NEW_TODO 2004

class TrayManager {
public:
    static bool AddTrayIcon(HWND hwnd);
    static bool RemoveTrayIcon(HWND hwnd);
    static void ShowContextMenu(HWND hwnd, bool isTopMost);
    static HICON CreateTrayIconHandle();
};
