#include "tray.h"
#include "ui_theme.h"
#include <shellapi.h>

HICON TrayManager::CreateModernTrayIcon(int colorTheme) {
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmColor);

    ColorScheme colors = GetThemeColors(colorTheme);

    // 绘制圆角现代便笺图标
    HBRUSH bgBrush = CreateSolidBrush(colors.bodyBg);
    HBRUSH headerBrush = CreateSolidBrush(colors.headerBg);
    HPEN borderPen = CreatePen(PS_SOLID, 1, colors.cardBorder);

    RECT rc = { 1, 1, size - 1, size - 1 };
    FillRect(hdcMem, &rc, bgBrush);

    RECT rcH = { 1, 1, size - 1, size / 3 };
    FillRect(hdcMem, &rcH, headerBrush);

    HPEN oldPen = (HPEN)SelectObject(hdcMem, borderPen);
    // 外边框
    MoveToEx(hdcMem, 0, 0, NULL);
    LineTo(hdcMem, size - 1, 0);
    LineTo(hdcMem, size - 1, size - 1);
    LineTo(hdcMem, 0, size - 1);
    LineTo(hdcMem, 0, 0);

    // 内部文字线
    HPEN linePen = CreatePen(PS_SOLID, 1, colors.textSecondary);
    SelectObject(hdcMem, linePen);
    MoveToEx(hdcMem, 3, size / 2, NULL);
    LineTo(hdcMem, size - 3, size / 2);
    MoveToEx(hdcMem, 3, (size * 3) / 4, NULL);
    LineTo(hdcMem, size - 5, (size * 3) / 4);

    SelectObject(hdcMem, oldPen);
    DeleteObject(linePen);
    DeleteObject(borderPen);
    DeleteObject(headerBrush);
    DeleteObject(bgBrush);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, NULL);
    ICONINFO ii = { 0 };
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;

    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hIcon;
}

bool TrayManager::AddTrayIcon(HWND hwnd, int colorTheme) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = CreateModernTrayIcon(colorTheme);
    wcscpy_s(nid.szTip, L"便签与进度跟踪 (Sticky Notes)");

    BOOL res = Shell_NotifyIconW(NIM_ADD, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
    return res != FALSE;
}

bool TrayManager::UpdateTrayIcon(HWND hwnd, int colorTheme) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON;
    nid.hIcon = CreateModernTrayIcon(colorTheme);

    BOOL res = Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
    return res != FALSE;
}

bool TrayManager::RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    return Shell_NotifyIconW(NIM_DELETE, &nid) != FALSE;
}

void TrayManager::ShowContextMenu(HWND hwnd, bool isTopMost, int currentTheme) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool isVisible = IsWindowVisible(hwnd) != FALSE;
    AppendMenuW(hMenu, MF_STRING, isVisible ? ID_TRAY_HIDE : ID_TRAY_SHOW, isVisible ? L"隐藏便签" : L"显示便签");
    AppendMenuW(hMenu, MF_STRING | (isTopMost ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOPMOST, L"总是置顶");

    // 颜色主题二级子菜单
    HMENU hColorSubMenu = CreatePopupMenu();
    const wchar_t* colorNames[] = { L"经典黄", L"薄荷绿", L"柔和粉", L"丁香紫", L"天空蓝", L"炭灰暗色" };
    for (int i = 0; i <= 5; ++i) {
        UINT flags = MF_STRING | ((currentTheme == i) ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hColorSubMenu, flags, ID_TRAY_COLOR_BASE + i, colorNames[i]);
    }
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hColorSubMenu, L"主题色彩");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}
