#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>

#include "data_store.h"
#include "ui_theme.h"
#include "tray.h"
#include "dpi_layout.h"
#include "rich_text_utils.h"
#include "ui_renderer.h"

// 控件与交互常量
#define ID_EDIT_INPUT       1001
#define ID_RICH_MEMO        1003
#define ID_TIMER_SAVE       1004

// 格式工具栏按钮提示文字
static const wchar_t* FMT_BTN_TITLES[] = {
    L"加粗",
    L"斜体",
    L"下划线",
    L"删除线",
    L"切换项目符号",
    L"添加图像"
};

static const wchar_t* HEADER_BTN_TITLES[] = {
    L"置顶窗口",
    L"切换便签颜色",
    L"最小化到托盘"
};

// 全局句柄与状态
static HINSTANCE g_hInstance = NULL;
static HWND g_hOwnerWnd = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hTooltipWnd = NULL;
static HWND g_hEditInput = NULL;
static HWND g_hRichMemo = NULL;
static WNDPROC g_oldEditInputProc = NULL;
static HMODULE g_hMsftEdit = NULL;

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontStrike = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontFmtB = NULL;
static HFONT g_hFontFmtI = NULL;
static HFONT g_hFontFmtU = NULL;
static HFONT g_hFontFmtAB = NULL;

static std::wstring g_tooltipText = L"";

static AppData g_appData;
static int g_scrollOffset = 0;
static int g_hoveredItemIdx = -1;
static int g_hoveredDeleteIdx = -1;
static int g_hoveredHeaderBtn = 0; // 1: Pin, 2: Theme, 3: Close/Min
static int g_hoveredFmtBtn = 0;    // 1..6

// 函数声明
LRESULT CALLBACK OwnerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TooltipWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditInputSubclassProc(HWND, UINT, WPARAM, LPARAM);
void SaveDataDeferred(HWND hwnd);
void UpdateControlVisibility();
void LayoutControls(int width, int height);
void UpdateFontsForDpi(UINT dpi);
void ShowTooltip(HWND hParent, const std::wstring& text, RECT targetRect, bool above, UINT dpi);
void HideTooltip();

// 悬浮提示 Tooltip 窗口过程
LRESULT CALLBACK TooltipWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT; // 鼠标事件穿透
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(210, 210, 210));
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
        Rectangle(memDC, 0, 0, rc.right, rc.bottom);
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(25, 25, 25));
        SelectObject(memDC, g_hFontSmall ? g_hFontSmall : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(memDC, g_tooltipText.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void ShowTooltip(HWND hParent, const std::wstring& text, RECT targetRect, bool above, UINT dpi) {
    if (!g_hTooltipWnd) return;
    g_tooltipText = text;

    HDC hdc = GetDC(g_hTooltipWnd);
    SelectObject(hdc, g_hFontSmall ? g_hFontSmall : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(hdc, text.c_str(), (int)text.length(), &sz);
    ReleaseDC(g_hTooltipWnd, hdc);

    int tipW = sz.cx + ScaleDpi(18, dpi);
    int tipH = sz.cy + ScaleDpi(10, dpi);
    int minTipH = ScaleDpi(24, dpi);
    if (tipH < minTipH) tipH = minTipH;

    POINT ptTarget = { (targetRect.left + targetRect.right) / 2, above ? targetRect.top : targetRect.bottom };
    ClientToScreen(hParent, &ptTarget);

    int gap = ScaleDpi(5, dpi);
    int tipX = ptTarget.x - tipW / 2;
    int tipY = above ? (ptTarget.y - tipH - gap) : (ptTarget.y + gap);

    SetWindowPos(g_hTooltipWnd, HWND_TOPMOST, tipX, tipY, tipW, tipH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_hTooltipWnd, NULL, TRUE);
}

void HideTooltip() {
    if (g_hTooltipWnd && IsWindowVisible(g_hTooltipWnd)) {
        ShowWindow(g_hTooltipWnd, SW_HIDE);
    }
}

// 动态根据当前 DPI 生成超高清晰度的矢量 ClearType 字体
void UpdateFontsForDpi(UINT dpi) {
    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontStrike) DeleteObject(g_hFontStrike);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);
    if (g_hFontFmtB) DeleteObject(g_hFontFmtB);
    if (g_hFontFmtI) DeleteObject(g_hFontFmtI);
    if (g_hFontFmtU) DeleteObject(g_hFontFmtU);
    if (g_hFontFmtAB) DeleteObject(g_hFontFmtAB);

    int titleH = ScaleDpi(17, dpi);
    int normalH = ScaleDpi(16, dpi);
    int smallH = ScaleDpi(13, dpi);
    int fmtBH = ScaleDpi(17, dpi);
    int fmtIH = ScaleDpi(18, dpi);
    int fmtUH = ScaleDpi(16, dpi);
    int fmtABH = ScaleDpi(16, dpi);

    g_hFontTitle = CreateFontW(-titleH, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_hFontNormal = CreateFontW(-normalH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_hFontStrike = CreateFontW(-normalH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, TRUE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_hFontSmall = CreateFontW(-smallH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    g_hFontFmtB = CreateFontW(-fmtBH, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hFontFmtI = CreateFontW(-fmtIH, 0, 0, 0, FW_NORMAL, TRUE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Georgia");

    g_hFontFmtU = CreateFontW(-fmtUH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hFontFmtAB = CreateFontW(-fmtABH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    if (g_hEditInput) {
        SendMessage(g_hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
    }
    // 注意：严禁对富文本 g_hRichMemo 发送 WM_SETFONT 消息！
    // Win32 RichEdit 接收 WM_SETFONT 会彻底重置整个文档为纯文本并擦除加粗、斜体、下划线等所有样式。
}

// 控件显隐管理
void UpdateControlVisibility() {
    if (g_appData.currentTab == 0) {
        ShowWindow(g_hEditInput, SW_SHOW);
        ShowWindow(g_hRichMemo, SW_HIDE);
    } else {
        ShowWindow(g_hEditInput, SW_HIDE);
        ShowWindow(g_hRichMemo, SW_SHOW);
    }
}

// 控件布局适配
void LayoutControls(int width, int height) {
    UINT dpi = GetWindowDpi(g_hMainWnd);
    LayoutMetrics m = GetMetrics(dpi);
    int progY = m.headerH + m.tabH + ScaleDpi(10, dpi);
    int inputCardY = progY + m.progressH + ScaleDpi(8, dpi);
    int editPad = ScaleDpi(12, dpi);
    int editH = ScaleDpi(22, dpi);
    int editY = inputCardY + (m.inputCardH - editH) / 2;
    MoveWindow(g_hEditInput, m.padding + editPad, editY, width - (m.padding + editPad) * 2, editH, TRUE);

    // 富文本 RichEdit (留出顶部 Tab 和底部格式栏的高度)
    int memoY = m.headerH + m.tabH + ScaleDpi(6, dpi);
    int memoH = height - memoY - m.formatBarH - ScaleDpi(6, dpi);
    MoveWindow(g_hRichMemo, m.padding, memoY, width - m.padding * 2, memoH, TRUE);

    RECT marginsRc = { ScaleDpi(4, dpi), ScaleDpi(4, dpi), ScaleDpi(4, dpi), ScaleDpi(4, dpi) };
    SendMessage(g_hRichMemo, EM_SETRECT, 0, (LPARAM)&marginsRc);
}

// 延时防抖落盘
void SaveDataDeferred(HWND hwnd) {
    SetTimer(hwnd, ID_TIMER_SAVE, 300, NULL);
}

// 输入框拦截回车
LRESULT CALLBACK EditInputSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        int len = GetWindowTextLengthW(hwnd);
        if (len > 0) {
            std::vector<wchar_t> buf(len + 1);
            GetWindowTextW(hwnd, buf.data(), len + 1);
            std::wstring text = buf.data();
            if (!text.empty()) {
                TodoItem item;
                item.id = (long long)GetTickCount64();
                item.text = text;
                item.completed = false;
                g_appData.todos.push_back(item);
                SetWindowTextW(hwnd, L"");
                InvalidateRect(g_hMainWnd, NULL, FALSE);
                SaveDataDeferred(g_hMainWnd);
            }
        }
        return 0;
    }
    return CallWindowProcW(g_oldEditInputProc, hwnd, uMsg, wParam, lParam);
}

// 双缓冲渲染
static void Render(HWND hwnd, HDC hdc) {
    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    UINT dpi = GetWindowDpi(hwnd);
    RenderView(
        memDC, clientRc, g_appData, g_scrollOffset,
        g_hoveredItemIdx, g_hoveredDeleteIdx, g_hoveredHeaderBtn, g_hoveredFmtBtn,
        g_hFontTitle, g_hFontNormal, g_hFontStrike, g_hFontSmall,
        g_hFontFmtB, g_hFontFmtI, g_hFontFmtU, g_hFontFmtAB, dpi
    );

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

// 主窗口 WndProc
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        UINT dpi = GetWindowDpi(hwnd);
        UpdateFontsForDpi(dpi);

        // 待办输入框
        g_hEditInput = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INPUT, g_hInstance, NULL
        );
        SendMessage(g_hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hEditInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入待办后按回车直接添加...");
        g_oldEditInputProc = (WNDPROC)SetWindowLongPtrW(g_hEditInput, GWLP_WNDPROC, (LONG_PTR)EditInputSubclassProc);

        // 现代富文本 RichEdit (MSFTEDIT_CLASS: RichEdit 4.1+)
        g_hRichMemo = CreateWindowExW(
            0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd, (HMENU)ID_RICH_MEMO, g_hInstance, NULL
        );

        // 启用高级事件监听与平滑排版
        SendMessage(g_hRichMemo, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        SendMessage(g_hRichMemo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        int margin = ScaleDpi(4, dpi);
        RECT marginsRc = { margin, margin, margin, margin };
        SendMessage(g_hRichMemo, EM_SETRECT, 0, (LPARAM)&marginsRc);

        ColorScheme colors = GetThemeColors(g_appData.colorTheme);
        SendMessage(g_hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);

        // 加载 RTF 格式
        LoadRichTextToControl(g_hRichMemo, g_appData.memoRtf);

        UpdateControlVisibility();
        TrayManager::AddTrayIcon(hwnd, g_appData.colorTheme);
        return 0;
    }

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wParam);
        RECT* prc = (RECT*)lParam;
        if (prc) {
            SetWindowPos(hwnd, NULL, prc->left, prc->top,
                         prc->right - prc->left, prc->bottom - prc->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UpdateFontsForDpi(newDpi);
        RECT rc;
        GetClientRect(hwnd, &rc);
        LayoutControls(rc.right - rc.left, rc.bottom - rc.top);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HWND hCtl = (HWND)lParam;
        ColorScheme colors = GetThemeColors(g_appData.colorTheme);
        HDC hdcCtl = (HDC)wParam;
        SetTextColor(hdcCtl, colors.textPrimary);
        if (hCtl == g_hEditInput) {
            SetBkColor(hdcCtl, colors.cardBg);
            static HBRUSH hBrushCard = NULL;
            if (hBrushCard) DeleteObject(hBrushCard);
            hBrushCard = CreateSolidBrush(colors.cardBg);
            return (LRESULT)hBrushCard;
        }
        break;
    }

    case WM_SIZE: {
        HideTooltip();
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        g_appData.windowWidth = width;
        g_appData.windowHeight = height;
        LayoutControls(width, height);
        InvalidateRect(hwnd, NULL, FALSE);
        SaveDataDeferred(hwnd);
        return 0;
    }

    case WM_MOVE: {
        HideTooltip();
        RECT rc;
        GetWindowRect(hwnd, &rc);
        g_appData.windowX = rc.left;
        g_appData.windowY = rc.top;
        SaveDataDeferred(hwnd);
        return 0;
    }

    case WM_TIMER: {
        if (wParam == ID_TIMER_SAVE) {
            KillTimer(hwnd, ID_TIMER_SAVE);
            g_appData.memoRtf = GetRichTextFromControl(g_hRichMemo);
            DataStore::Save(g_appData);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == ID_RICH_MEMO && code == EN_CHANGE) {
            SaveDataDeferred(hwnd);
            return 0;
        }

        // 托盘菜单
        if (id == ID_TRAY_SHOW) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            return 0;
        }

        if (id == ID_TRAY_HIDE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        if (id == ID_TRAY_TOPMOST) {
            g_appData.isAlwaysOnTop = !g_appData.isAlwaysOnTop;
            SetWindowPos(hwnd, g_appData.isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            InvalidateRect(hwnd, NULL, FALSE);
            SaveDataDeferred(hwnd);
            return 0;
        }

        if (id >= ID_TRAY_COLOR_BASE && id <= ID_TRAY_COLOR_BASE + 5) {
            g_appData.colorTheme = id - ID_TRAY_COLOR_BASE;
            ColorScheme colors = GetThemeColors(g_appData.colorTheme);
            SendMessage(g_hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);
            TrayManager::UpdateTrayIcon(hwnd, g_appData.colorTheme);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveDataDeferred(hwnd);
            return 0;
        }

        if (id == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONUP) {
            if (IsWindowVisible(hwnd)) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }
        } else if (lParam == WM_RBUTTONUP) {
            TrayManager::ShowContextMenu(hwnd, g_appData.isAlwaysOnTop, g_appData.colorTheme);
        }
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;
        GetWindowRect(hwnd, &rc);
        UINT dpi = GetWindowDpi(hwnd);
        int border = ScaleDpi(8, dpi);
        bool left = pt.x >= rc.left && pt.x < rc.left + border;
        bool right = pt.x < rc.right && pt.x >= rc.right - border;
        bool top = pt.y >= rc.top && pt.y < rc.top + border;
        bool bottom = pt.y < rc.bottom && pt.y >= rc.bottom - border;

        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;

        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        HideTooltip();
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        UINT dpi = GetWindowDpi(hwnd);
        LayoutMetrics m = GetMetrics(dpi);

        // 1. 顶部 Header
        if (y < m.headerH) {
            RECT pinBtnRc = GetHeaderBtnRect(width, 1, dpi);
            RECT themeBtnRc = GetHeaderBtnRect(width, 2, dpi);
            RECT closeBtnRc = GetHeaderBtnRect(width, 3, dpi);

            if (x >= pinBtnRc.left && x <= pinBtnRc.right && y >= pinBtnRc.top && y <= pinBtnRc.bottom) {
                g_appData.isAlwaysOnTop = !g_appData.isAlwaysOnTop;
                SetWindowPos(hwnd, g_appData.isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            if (x >= themeBtnRc.left && x <= themeBtnRc.right && y >= themeBtnRc.top && y <= themeBtnRc.bottom) {
                g_appData.colorTheme = (g_appData.colorTheme + 1) % 6;
                ColorScheme colors = GetThemeColors(g_appData.colorTheme);
                SendMessage(g_hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);
                TrayManager::UpdateTrayIcon(hwnd, g_appData.colorTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            if (x >= closeBtnRc.left && x <= closeBtnRc.right && y >= closeBtnRc.top && y <= closeBtnRc.bottom) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }

            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        // 2. Tab 切换
        if (y >= m.headerH && y < m.headerH + m.tabH) {
            int newTab = (x < width / 2) ? 0 : 1;
            if (newTab != g_appData.currentTab) {
                g_appData.currentTab = newTab;
                UpdateControlVisibility();
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
            }
            return 0;
        }

        // 3. Tab 0 待办事项交互
        if (g_appData.currentTab == 0) {
            int inputCardY = m.headerH + m.tabH + ScaleDpi(10, dpi) + m.progressH + ScaleDpi(8, dpi);
            int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
            if (y >= listStartY) {
                int clickedIdx = (y - listStartY + g_scrollOffset) / m.itemH;
                if (clickedIdx >= 0 && clickedIdx < (int)g_appData.todos.size()) {
                    int delBtnW = ScaleDpi(22, dpi);
                    int delRight = width - m.padding - ScaleDpi(8, dpi);
                    int delLeft = delRight - delBtnW;
                    // 删除
                    if (x >= delLeft && x <= delRight) {
                        g_appData.todos.erase(g_appData.todos.begin() + clickedIdx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        SaveDataDeferred(hwnd);
                        return 0;
                    }
                    // 勾选切换
                    g_appData.todos[clickedIdx].completed = !g_appData.todos[clickedIdx].completed;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SaveDataDeferred(hwnd);
                    return 0;
                }
            }
        }

        // 4. Tab 1 富文本底部工具栏点击响应
        if (g_appData.currentTab == 1 && y >= height - m.formatBarH) {
            for (int b = 1; b <= 6; ++b) {
                RECT bRc = GetFormatBtnRect(width, height, b, dpi);
                if (x >= bRc.left && x <= bRc.right && y >= bRc.top && y <= bRc.bottom) {
                    HideTooltip();
                    if (b == 1) ApplyFormatToSelection(g_hRichMemo, CFM_BOLD, CFE_BOLD);
                    else if (b == 2) ApplyFormatToSelection(g_hRichMemo, CFM_ITALIC, CFE_ITALIC);
                    else if (b == 3) ApplyFormatToSelection(g_hRichMemo, CFM_UNDERLINE, CFE_UNDERLINE);
                    else if (b == 4) ApplyFormatToSelection(g_hRichMemo, CFM_STRIKEOUT, CFE_STRIKEOUT);
                    else if (b == 5) ToggleBullet(g_hRichMemo);
                    else if (b == 6) InsertImageFromFile(hwnd, g_hRichMemo);
                    SaveDataDeferred(hwnd);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        UINT dpi = GetWindowDpi(hwnd);
        LayoutMetrics m = GetMetrics(dpi);

        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        int prevHeaderBtn = g_hoveredHeaderBtn;
        g_hoveredHeaderBtn = 0;
        if (y < m.headerH) {
            for (int h = 1; h <= 3; ++h) {
                RECT hRc = GetHeaderBtnRect(width, h, dpi);
                if (x >= hRc.left && x <= hRc.right && y >= hRc.top && y <= hRc.bottom) {
                    g_hoveredHeaderBtn = h;
                    break;
                }
            }
        }

        int prevFmtBtn = g_hoveredFmtBtn;
        g_hoveredFmtBtn = 0;
        if (g_appData.currentTab == 1 && y >= height - m.formatBarH) {
            for (int b = 1; b <= 6; ++b) {
                RECT bRc = GetFormatBtnRect(width, height, b, dpi);
                if (x >= bRc.left && x <= bRc.right && y >= bRc.top && y <= bRc.bottom) {
                    g_hoveredFmtBtn = b;
                    break;
                }
            }
        }

        // 动态展示/刷新/隐藏 Tooltip 提示
        if (g_hoveredFmtBtn > 0) {
            RECT bRc = GetFormatBtnRect(width, height, g_hoveredFmtBtn, dpi);
            ShowTooltip(hwnd, FMT_BTN_TITLES[g_hoveredFmtBtn - 1], bRc, true, dpi);
        } else if (g_hoveredHeaderBtn > 0) {
            RECT hRc = GetHeaderBtnRect(width, g_hoveredHeaderBtn, dpi);
            std::wstring hText = (g_hoveredHeaderBtn == 1) ?
                (g_appData.isAlwaysOnTop ? L"取消置顶" : L"置顶窗口") :
                HEADER_BTN_TITLES[g_hoveredHeaderBtn - 1];
            ShowTooltip(hwnd, hText, hRc, false, dpi);
        } else {
            HideTooltip();
        }

        if (g_appData.currentTab == 0) {
            int inputCardY = m.headerH + m.tabH + ScaleDpi(10, dpi) + m.progressH + ScaleDpi(8, dpi);
            int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
            int prevHovered = g_hoveredItemIdx;
            int prevDel = g_hoveredDeleteIdx;

            if (y >= listStartY) {
                int hoverIdx = (y - listStartY + g_scrollOffset) / m.itemH;
                if (hoverIdx >= 0 && hoverIdx < (int)g_appData.todos.size()) {
                    g_hoveredItemIdx = hoverIdx;
                    int delBtnW = ScaleDpi(22, dpi);
                    int delRight = width - m.padding - ScaleDpi(8, dpi);
                    int delLeft = delRight - delBtnW;
                    if (x >= delLeft && x <= delRight) {
                        g_hoveredDeleteIdx = hoverIdx;
                    } else {
                        g_hoveredDeleteIdx = -1;
                    }
                } else {
                    g_hoveredItemIdx = -1;
                    g_hoveredDeleteIdx = -1;
                }
            } else {
                g_hoveredItemIdx = -1;
                g_hoveredDeleteIdx = -1;
            }

            if (prevHovered != g_hoveredItemIdx || prevDel != g_hoveredDeleteIdx || prevHeaderBtn != g_hoveredHeaderBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            if (prevHeaderBtn != g_hoveredHeaderBtn || prevFmtBtn != g_hoveredFmtBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        HideTooltip();
        g_hoveredItemIdx = -1;
        g_hoveredDeleteIdx = -1;
        g_hoveredHeaderBtn = 0;
        g_hoveredFmtBtn = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KILLFOCUS: {
        HideTooltip();
        break;
    }

    case WM_MOUSEWHEEL: {
        if (g_appData.currentTab == 0) {
            UINT dpi = GetWindowDpi(hwnd);
            LayoutMetrics m = GetMetrics(dpi);
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_scrollOffset -= (delta / WHEEL_DELTA) * m.itemH;
            if (g_scrollOffset < 0) g_scrollOffset = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Render(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        HideTooltip();
        if (g_hTooltipWnd) DestroyWindow(g_hTooltipWnd);
        g_appData.memoRtf = GetRichTextFromControl(g_hRichMemo);
        DataStore::Save(g_appData);
        TrayManager::RemoveTrayIcon(hwnd);
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontStrike) DeleteObject(g_hFontStrike);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        if (g_hFontFmtB) DeleteObject(g_hFontFmtB);
        if (g_hFontFmtI) DeleteObject(g_hFontFmtI);
        if (g_hFontFmtU) DeleteObject(g_hFontFmtU);
        if (g_hFontFmtAB) DeleteObject(g_hFontFmtAB);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Owner Proc
LRESULT CALLBACK OwnerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// WinMain
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    g_hInstance = hInstance;

    // 启用 Per-Monitor V2 高 DPI 感知 (Windows 10 1703+)
    typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    PFN_SetProcessDpiAwarenessContext pfnSetDpiContext = 
        (PFN_SetProcessDpiAwarenessContext)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");
    if (pfnSetDpiContext) {
        pfnSetDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        typedef HRESULT(WINAPI* PFN_SetProcessDpiAwareness)(int);
        PFN_SetProcessDpiAwareness pfnSetDpi = 
            (PFN_SetProcessDpiAwareness)GetProcAddress(GetModuleHandleW(L"shcore.dll"), "SetProcessDpiAwareness");
        if (pfnSetDpi) {
            pfnSetDpi(2); // PROCESS_PER_MONITOR_DPI_AWARE
        } else {
            SetProcessDPIAware();
        }
    }

    // 加载 Windows 高级富文本支持库
    g_hMsftEdit = LoadLibraryW(L"msftedit.dll");

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MyStickyNotes_SingleInstance_Mutex_v4");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowW(L"MyStickyNotes_Main", NULL);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_SHOW);
            SetForegroundWindow(existingWnd);
            CloseHandle(hMutex);
            return 0;
        }
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // 初始化 GDI+ (支持图片插入)
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    DataStore::Load(g_appData);

    // 确定窗口坐标目标显示器的真实 DPI
    POINT ptTarget = { g_appData.windowX, g_appData.windowY };
    UINT targetDpi = GetDpiForPoint(ptTarget);

    // 首次在非 96 DPI（高分屏）下启动时，若读取到的历史尺寸是低分屏 96 DPI 未缩放默认值（如宽 340），等比自适应初次放大
    if (targetDpi > 96 && g_appData.windowWidth <= 360) {
        g_appData.windowWidth = ScaleDpi(g_appData.windowWidth, targetDpi);
        g_appData.windowHeight = ScaleDpi(g_appData.windowHeight, targetDpi);
    }

    // 防止窗口因显示器分辨率或扩展屏变动跑到屏幕可视区域之外
    int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_appData.windowX < vLeft - 100 || g_appData.windowX > vLeft + vWidth - 100 ||
        g_appData.windowY < vTop - 50 || g_appData.windowY > vTop + vHeight - 100) {
        g_appData.windowX = 100;
        g_appData.windowY = 100;
    }

    // 0. Tooltip 悬浮提示窗口类
    WNDCLASSEXW twc = { sizeof(twc) };
    twc.lpfnWndProc = TooltipWndProc;
    twc.hInstance = hInstance;
    twc.lpszClassName = L"StickyNotes_Tooltip";
    twc.hCursor = LoadCursor(NULL, IDC_ARROW);
    twc.style = CS_DROPSHADOW;
    RegisterClassExW(&twc);

    // 1. Owner Window (创建在主窗口同一显示器坐标，避免跨屏幕 Owner 造成 DPI 上下文错乱)
    WNDCLASSEXW owc = { sizeof(owc) };
    owc.lpfnWndProc = OwnerWndProc;
    owc.hInstance = hInstance;
    owc.lpszClassName = L"MyStickyNotes_Owner";
    RegisterClassExW(&owc);

    g_hOwnerWnd = CreateWindowExW(
        0, L"MyStickyNotes_Owner", L"",
        WS_OVERLAPPEDWINDOW, g_appData.windowX, g_appData.windowY, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    // 2. Main Window
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyStickyNotes_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (g_appData.isAlwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }

    g_hMainWnd = CreateWindowExW(
        exStyle,
        L"MyStickyNotes_Main",
        L"Sticky Notes",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        g_appData.windowX, g_appData.windowY,
        g_appData.windowWidth, g_appData.windowHeight,
        g_hOwnerWnd,
        NULL, hInstance, NULL
    );

    if (!g_hMainWnd) return 0;

    // 创建悬浮提示 Tooltip 窗口
    g_hTooltipWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"StickyNotes_Tooltip", L"",
        WS_POPUP,
        0, 0, 0, 0,
        g_hMainWnd, NULL, hInstance, NULL
    );

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hMainWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hMainWnd, &margins);

    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    if (g_hMsftEdit) FreeLibrary(g_hMsftEdit);
    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
