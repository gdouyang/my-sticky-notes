#pragma once
#include <windows.h>

// DPI 与缩放计算辅助
inline UINT GetWindowDpi(HWND hwnd) {
    typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pfn && hwnd) {
        UINT dpi = pfn(hwnd);
        if (dpi > 0) return dpi;
    }
    HDC hdc = GetDC(hwnd);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    return (dpi > 0) ? (UINT)dpi : 96;
}

inline UINT GetDpiForPoint(POINT pt) {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (hMon) {
        typedef HRESULT(WINAPI* PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
        static PFN_GetDpiForMonitor pfnGetDpiForMon = 
            (PFN_GetDpiForMonitor)GetProcAddress(GetModuleHandleW(L"shcore.dll"), "GetDpiForMonitor");
        if (pfnGetDpiForMon) {
            UINT dpiX = 96, dpiY = 96;
            if (SUCCEEDED(pfnGetDpiForMon(hMon, 0, &dpiX, &dpiY)) && dpiX > 0) {
                return dpiX;
            }
        }
    }
    return GetWindowDpi(NULL);
}

inline int ScaleDpi(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

struct LayoutMetrics {
    UINT dpi;
    int headerH;
    int tabH;
    int progressH;
    int inputCardH;
    int itemH;
    int formatBarH;
    int padding;
    int headerBtnW;
    int headerBtnH;
    int fmtBtnW;
    int fmtBtnH;
    int fmtBtnPad;
};

inline LayoutMetrics GetMetrics(UINT dpi) {
    LayoutMetrics m;
    m.dpi = dpi;
    m.headerH = ScaleDpi(44, dpi);
    m.tabH = ScaleDpi(38, dpi);
    m.progressH = ScaleDpi(48, dpi);
    m.inputCardH = ScaleDpi(36, dpi);
    m.itemH = ScaleDpi(42, dpi);
    m.formatBarH = ScaleDpi(40, dpi);
    m.padding = ScaleDpi(14, dpi);
    m.headerBtnW = ScaleDpi(26, dpi);
    m.headerBtnH = ScaleDpi(26, dpi);
    m.fmtBtnW = ScaleDpi(30, dpi);
    m.fmtBtnH = ScaleDpi(28, dpi);
    m.fmtBtnPad = ScaleDpi(4, dpi);
    return m;
}

inline RECT GetFormatBtnRect(int width, int height, int index, UINT dpi) {
    LayoutMetrics m = GetMetrics(dpi);
    int btnY = height - m.formatBarH + (m.formatBarH - m.fmtBtnH) / 2;
    int x = m.padding + (index - 1) * (m.fmtBtnW + m.fmtBtnPad);
    return { x, btnY, x + m.fmtBtnW, btnY + m.fmtBtnH };
}

inline RECT GetHeaderBtnRect(int width, int index, UINT dpi) {
    LayoutMetrics m = GetMetrics(dpi);
    int btnY = (m.headerH - m.headerBtnH) / 2;
    int x4 = width - ScaleDpi(32, dpi);
    int x3 = width - ScaleDpi(64, dpi);
    int x2 = width - ScaleDpi(96, dpi);
    int x1 = width - ScaleDpi(128, dpi);
    if (index == 1) return { x1, btnY, x1 + m.headerBtnW, btnY + m.headerBtnH };
    if (index == 2) return { x2, btnY, x2 + m.headerBtnW, btnY + m.headerBtnH };
    if (index == 3) return { x3, btnY, x3 + m.headerBtnW, btnY + m.headerBtnH };
    if (index == 4) return { x4, btnY, x4 + m.headerBtnW, btnY + m.headerBtnH };
    return { 0, 0, 0, 0 };
}
