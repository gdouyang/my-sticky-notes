#pragma once
#include <windows.h>

#define WM_TRAYICON (WM_USER + 101)

#define ID_TRAY_NEW_NOTE    2000
#define ID_TRAY_SHOW        2001
#define ID_TRAY_HIDE        2002
#define ID_TRAY_SHOW_ALL    2003
#define ID_TRAY_HIDE_ALL    2004
#define ID_TRAY_TOPMOST     2005
#define ID_TRAY_AUTOSTART   2006
#define ID_TRAY_OPACITY_100 2010
#define ID_TRAY_OPACITY_90  2011
#define ID_TRAY_OPACITY_80  2012
#define ID_TRAY_COLOR_BASE  2100 // 2100 ~ 2105
#define ID_TRAY_EXIT        2099

class TrayManager {
public:
    static bool AddTrayIcon(HWND hwnd, int colorTheme);
    static bool UpdateTrayIcon(HWND hwnd, int colorTheme);
    static bool RemoveTrayIcon(HWND hwnd);
    static void ShowContextMenu(HWND hwnd, bool isTopMost, int currentTheme, int opacity, bool autostart);
    static HICON CreateModernTrayIcon(int colorTheme);
};
