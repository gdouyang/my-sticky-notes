#include "tray.h"
#include <shellapi.h>

HICON TrayManager::CreateTrayIconHandle() {
    // 动态生成一个精致的黄色迷你便签 16x16 / 32x32 图标，免除外部 ico 资源文件依赖
    int size = GetSystemMetrics(SM_CXSMICON);
    if (size <= 0) size = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, size, size);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmColor);

    // 绘制便签底色
    HBRUSH bgBrush = CreateSolidBrush(RGB(253, 224, 71)); // 亮黄
    HBRUSH foldBrush = CreateSolidBrush(RGB(234, 179, 8)); // 折角深黄
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(161, 98, 7)); // 深色外框

    RECT rc = { 1, 1, size - 1, size - 1 };
    FillRect(hdcMem, &rc, bgBrush);

    // 绘制横线条代表文字
    HPEN penLine = CreatePen(PS_SOLID, 1, RGB(113, 63, 18));
    HPEN oldPen = (HPEN)SelectObject(hdcMem, penLine);
    MoveToEx(hdcMem, 3, size / 3, NULL);
    LineTo(hdcMem, size - 3, size / 3);
    MoveToEx(hdcMem, 3, size / 2, NULL);
    LineTo(hdcMem, size - 4, size / 2);
    MoveToEx(hdcMem, 3, (size * 2) / 3, NULL);
    LineTo(hdcMem, size - 5, (size * 2) / 3);

    SelectObject(hdcMem, oldPen);
    DeleteObject(penLine);
    DeleteObject(borderPen);
    DeleteObject(bgBrush);
    DeleteObject(foldBrush);
    SelectObject(hdcMem, hbmOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // 创建单色掩码位图
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

bool TrayManager::AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = CreateTrayIconHandle();
    wcscpy_s(nid.szTip, L"便签进度跟踪 (My Sticky Notes)");

    BOOL res = Shell_NotifyIconW(NIM_ADD, &nid);
    if (nid.hIcon) DestroyIcon(nid.hIcon);
    return res != FALSE;
}

bool TrayManager::RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid = { sizeof(nid) };
    nid.hWnd = hwnd;
    nid.uID = 1;
    return Shell_NotifyIconW(NIM_DELETE, &nid) != FALSE;
}

void TrayManager::ShowContextMenu(HWND hwnd, bool isTopMost) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    bool isVisible = IsWindowVisible(hwnd) != FALSE;
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE, isVisible ? L"隐藏便签" : L"显示便签");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_NEW_TODO, L"快速新建待办");
    AppendMenuW(hMenu, MF_STRING | (isTopMost ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_TOPMOST, L"总是置顶");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}
