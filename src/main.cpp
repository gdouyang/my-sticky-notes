#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>

#include "data_store.h"
#include "ui_theme.h"
#include "tray.h"
#include "dpi_layout.h"
#include "rich_text_utils.h"
#include "ui_renderer.h"
#include "autostart.h"

// 控件与交互常量
#define ID_EDIT_INPUT       1001
#define ID_RICH_MEMO        1003
#define ID_EDIT_INLINE_TODO 1004
#define ID_TIMER_SAVE       1005
#define ID_TIMER_EDGE_CHECK 1006
#define ID_TIMER_EDGE_ANIM  1007

// 格式工具栏按钮提示文字 (7 个格式按钮)
static const wchar_t* FMT_BTN_TITLES[] = {
    L"加粗",
    L"斜体",
    L"下划线",
    L"删除线",
    L"切换项目符号",
    L"荧光高亮笔",
    L"添加图像"
};

// 顶部 Header 按钮提示文字 (4 个按钮: 1: [+], 2: [📌], 3: [🎨], 4: [×])
static const wchar_t* HEADER_BTN_TITLES[] = {
    L"新建便签",
    L"置顶窗口",
    L"切换便签颜色",
    L"关闭/收起便签"
};

// 边缘贴边与自动收起状态机
enum EdgeDockState {
    EDGE_NONE = 0,
    EDGE_LEFT = 1,
    EDGE_RIGHT = 2,
    EDGE_TOP = 3
};

// 单便签窗口上下文结构体
struct NoteWindowContext {
    HWND hWnd = NULL;
    HWND hEditInput = NULL;
    HWND hRichMemo = NULL;
    HWND hInlineEdit = NULL;
    WNDPROC oldEditInputProc = NULL;
    WNDPROC oldInlineEditProc = NULL;
    NoteCardData* pData = NULL;

    int scrollOffset = 0;
    int hoveredItemIdx = -1;
    int hoveredDeleteIdx = -1;
    int hoveredHeaderBtn = 0; // 1: Add, 2: Pin, 3: Theme, 4: Close
    int hoveredFmtBtn = 0;    // 1..7
    int editingTodoIdx = -1;

    EdgeDockState dockState = EDGE_NONE;
    bool isCollapsed = false;
    bool isMoving = false;
    RECT expandedRect = { 0 };
    int leaveCounter = 0;

    int animStartX = 0, animStartY = 0;
    int animTargetX = 0, animTargetY = 0;
    int animStep = 0;
};

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HWND g_hOwnerWnd = NULL;
static HWND g_hTooltipWnd = NULL;
static HMODULE g_hMsftEdit = NULL;
static std::wstring g_tooltipText = L"";

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontStrike = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontFmtB = NULL;
static HFONT g_hFontFmtI = NULL;
static HFONT g_hFontFmtU = NULL;
static HFONT g_hFontFmtAB = NULL;

static AppData g_appData;
static std::vector<NoteWindowContext*> g_noteWindows;
static const int DOCK_ANIM_TOTAL_STEPS = 5;

// 函数声明
LRESULT CALLBACK OwnerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TooltipWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditInputSubclassProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK InlineEditSubclassProc(HWND, UINT, WPARAM, LPARAM);

void SaveDataDeferred(HWND hwnd);
void UpdateControlVisibility(NoteWindowContext* pCtx);
void LayoutControls(NoteWindowContext* pCtx, int width, int height);
void UpdateFontsForDpi(UINT dpi);
void ApplyThemeToControls(NoteWindowContext* pCtx);
void ApplyOpacity(NoteWindowContext* pCtx);
void ShowTooltip(HWND hParent, const std::wstring& text, RECT targetRect, bool above, UINT dpi);
void HideTooltip();

HWND CreateNoteWindow(HINSTANCE hInstance, NoteCardData* pCardData);
void FinishInlineEdit(NoteWindowContext* pCtx, bool commit);

// 边缘贴边辅助方法
static RECT GetCurrentMonitorWorkArea(HWND hwnd) {
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        return mi.rcWork;
    }
    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);
    return rc;
}

static POINT GetCollapsedPosition(HWND hwnd, EdgeDockState dock, const RECT& expRect, UINT dpi) {
    RECT workRc = GetCurrentMonitorWorkArea(hwnd);
    int width = expRect.right - expRect.left;
    int height = expRect.bottom - expRect.top;
    int peek = ScaleDpi(5, dpi);

    POINT pt = { expRect.left, expRect.top };
    if (dock == EDGE_LEFT) {
        pt.x = workRc.left - width + peek;
        pt.y = expRect.top;
    } else if (dock == EDGE_RIGHT) {
        pt.x = workRc.right - peek;
        pt.y = expRect.top;
    } else if (dock == EDGE_TOP) {
        pt.x = expRect.left;
        pt.y = workRc.top - height + peek;
    }
    return pt;
}

static void ExpandDockedWindow(NoteWindowContext* pCtx) {
    if (!pCtx || !pCtx->isCollapsed) return;
    pCtx->isCollapsed = false;
    KillTimer(pCtx->hWnd, ID_TIMER_EDGE_ANIM);

    RECT curRc;
    GetWindowRect(pCtx->hWnd, &curRc);
    pCtx->animStartX = curRc.left;
    pCtx->animStartY = curRc.top;
    pCtx->animTargetX = pCtx->expandedRect.left;
    pCtx->animTargetY = pCtx->expandedRect.top;
    pCtx->animStep = 0;

    SetTimer(pCtx->hWnd, ID_TIMER_EDGE_ANIM, 12, NULL);
}

static void CollapseDockedWindow(NoteWindowContext* pCtx) {
    if (!pCtx || pCtx->isCollapsed || pCtx->dockState == EDGE_NONE) return;
    HideTooltip();
    pCtx->isCollapsed = true;
    KillTimer(pCtx->hWnd, ID_TIMER_EDGE_ANIM);

    UINT dpi = GetWindowDpi(pCtx->hWnd);
    POINT targetPt = GetCollapsedPosition(pCtx->hWnd, pCtx->dockState, pCtx->expandedRect, dpi);

    RECT curRc;
    GetWindowRect(pCtx->hWnd, &curRc);
    pCtx->animStartX = curRc.left;
    pCtx->animStartY = curRc.top;
    pCtx->animTargetX = targetPt.x;
    pCtx->animTargetY = targetPt.y;
    pCtx->animStep = 0;

    SetTimer(pCtx->hWnd, ID_TIMER_EDGE_ANIM, 12, NULL);
}

static void ResetDockState(NoteWindowContext* pCtx) {
    if (!pCtx) return;
    if (pCtx->dockState != EDGE_NONE || pCtx->isCollapsed) {
        KillTimer(pCtx->hWnd, ID_TIMER_EDGE_ANIM);
        KillTimer(pCtx->hWnd, ID_TIMER_EDGE_CHECK);
        if (pCtx->isCollapsed && (pCtx->expandedRect.right > pCtx->expandedRect.left)) {
            SetWindowPos(pCtx->hWnd, NULL, pCtx->expandedRect.left, pCtx->expandedRect.top, 0, 0,
                         SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        pCtx->dockState = EDGE_NONE;
        pCtx->isCollapsed = false;
        pCtx->leaveCounter = 0;
    }
}

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

    int titleH = ScaleDpi(16, dpi);
    int normalH = ScaleDpi(15, dpi);
    int smallH = ScaleDpi(12, dpi);
    int fmtBH = ScaleDpi(16, dpi);
    int fmtIH = ScaleDpi(17, dpi);
    int fmtUH = ScaleDpi(15, dpi);
    int fmtABH = ScaleDpi(15, dpi);

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

    for (auto* pCtx : g_noteWindows) {
        if (pCtx->hEditInput) {
            SendMessage(pCtx->hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        }
        if (pCtx->hRichMemo) {
            SendMessage(pCtx->hRichMemo, EM_SETZOOM, 0, 0);
        }
        if (pCtx->hInlineEdit) {
            SendMessage(pCtx->hInlineEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        }
    }
}

// 同步主题配色到内部编辑控件
void ApplyThemeToControls(NoteWindowContext* pCtx) {
    if (!pCtx || !pCtx->pData) return;
    ColorScheme colors = GetThemeColors(pCtx->pData->colorTheme);
    if (pCtx->hRichMemo) {
        SendMessage(pCtx->hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);

        CHARFORMAT2W cf = { sizeof(cf) };
        cf.dwMask = CFM_COLOR;
        cf.crTextColor = colors.textPrimary;
        cf.dwEffects = 0;

        SendMessage(pCtx->hRichMemo, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
        SendMessage(pCtx->hRichMemo, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    if (pCtx->hEditInput) {
        InvalidateRect(pCtx->hEditInput, NULL, TRUE);
    }
    if (pCtx->hInlineEdit) {
        InvalidateRect(pCtx->hInlineEdit, NULL, TRUE);
    }
}

// 应用窗口不透明度 (80% / 90% / 100%)
void ApplyOpacity(NoteWindowContext* pCtx) {
    if (!pCtx || !pCtx->pData || !pCtx->hWnd) return;
    BYTE alpha = (BYTE)((pCtx->pData->opacity * 255) / 100);
    SetLayeredWindowAttributes(pCtx->hWnd, 0, alpha, LWA_ALPHA);
}

// 控件显隐管理
void UpdateControlVisibility(NoteWindowContext* pCtx) {
    if (!pCtx || !pCtx->pData) return;
    if (pCtx->pData->currentTab == 0) {
        ShowWindow(pCtx->hEditInput, SW_SHOW);
        ShowWindow(pCtx->hRichMemo, SW_HIDE);
    } else {
        if (pCtx->editingTodoIdx >= 0) {
            FinishInlineEdit(pCtx, true);
        }
        ShowWindow(pCtx->hEditInput, SW_HIDE);
        ShowWindow(pCtx->hRichMemo, SW_SHOW);
    }
}

// 控件布局适配
void LayoutControls(NoteWindowContext* pCtx, int width, int height) {
    if (!pCtx || !pCtx->hWnd) return;
    UINT dpi = GetWindowDpi(pCtx->hWnd);
    LayoutMetrics m = GetMetrics(dpi);
    int progY = m.headerH + m.tabH + ScaleDpi(10, dpi);
    int inputCardY = progY + m.progressH + ScaleDpi(8, dpi);
    int editPad = ScaleDpi(12, dpi);
    int editH = ScaleDpi(22, dpi);
    int editY = inputCardY + (m.inputCardH - editH) / 2;
    if (pCtx->hEditInput) {
        MoveWindow(pCtx->hEditInput, m.padding + editPad, editY, width - (m.padding + editPad) * 2, editH, TRUE);
    }

    int memoY = m.headerH + m.tabH + ScaleDpi(6, dpi);
    int memoW = width - m.padding * 2;
    int memoH = height - memoY - m.formatBarH - ScaleDpi(6, dpi);
    if (pCtx->hRichMemo) {
        MoveWindow(pCtx->hRichMemo, m.padding, memoY, memoW, memoH, TRUE);
        int margin = ScaleDpi(4, dpi);
        RECT formatRc = { margin, margin, memoW - margin, memoH - margin };
        SendMessage(pCtx->hRichMemo, EM_SETRECT, 0, (LPARAM)&formatRc);
    }
}

// 延时防抖落盘
void SaveDataDeferred(HWND hwnd) {
    SetTimer(hwnd, ID_TIMER_SAVE, 300, NULL);
}

// 待办新增输入框拦截回车
LRESULT CALLBACK EditInputSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NoteWindowContext* pCtx = (NoteWindowContext*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        int len = GetWindowTextLengthW(hwnd);
        if (len > 0 && pCtx && pCtx->pData) {
            std::vector<wchar_t> buf(len + 1);
            GetWindowTextW(hwnd, buf.data(), len + 1);
            std::wstring text = buf.data();
            if (!text.empty()) {
                TodoItem item;
                item.id = (long long)GetTickCount64();
                item.text = text;
                item.completed = false;
                pCtx->pData->todos.push_back(item);
                SetWindowTextW(hwnd, L"");
                InvalidateRect(pCtx->hWnd, NULL, FALSE);
                SaveDataDeferred(pCtx->hWnd);
            }
        }
        return 0;
    }
    if (pCtx) {
        return CallWindowProcW(pCtx->oldEditInputProc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 待办内联编辑完成处理
void FinishInlineEdit(NoteWindowContext* pCtx, bool commit) {
    if (!pCtx || pCtx->editingTodoIdx < 0 || !pCtx->hInlineEdit) return;
    int idx = pCtx->editingTodoIdx;
    pCtx->editingTodoIdx = -1;

    if (commit && pCtx->pData && idx >= 0 && idx < (int)pCtx->pData->todos.size()) {
        int len = GetWindowTextLengthW(pCtx->hInlineEdit);
        std::vector<wchar_t> buf(len + 1);
        GetWindowTextW(pCtx->hInlineEdit, buf.data(), len + 1);
        std::wstring newText = buf.data();
        if (!newText.empty()) {
            pCtx->pData->todos[idx].text = newText;
        }
        SaveDataDeferred(pCtx->hWnd);
    }

    ShowWindow(pCtx->hInlineEdit, SW_HIDE);
    SetFocus(pCtx->hWnd);
    InvalidateRect(pCtx->hWnd, NULL, FALSE);
}

// 待办内联编辑子类化过程
LRESULT CALLBACK InlineEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    NoteWindowContext* pCtx = (NoteWindowContext*)GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA);
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            FinishInlineEdit(pCtx, true);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            FinishInlineEdit(pCtx, false);
            return 0;
        }
    } else if (uMsg == WM_KILLFOCUS) {
        FinishInlineEdit(pCtx, true);
        return 0;
    }
    if (pCtx) {
        return CallWindowProcW(pCtx->oldInlineEditProc, hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 双缓冲渲染
static void Render(NoteWindowContext* pCtx, HDC hdc) {
    if (!pCtx || !pCtx->pData) return;
    RECT clientRc;
    GetClientRect(pCtx->hWnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    UINT dpi = GetWindowDpi(pCtx->hWnd);
    RenderView(
        memDC, clientRc, *(pCtx->pData), pCtx->scrollOffset,
        pCtx->hoveredItemIdx, pCtx->hoveredDeleteIdx, pCtx->hoveredHeaderBtn, pCtx->hoveredFmtBtn,
        g_hFontTitle, g_hFontNormal, g_hFontStrike, g_hFontSmall,
        g_hFontFmtB, g_hFontFmtI, g_hFontFmtU, g_hFontFmtAB, dpi
    );

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

// 创建并激活新的独立便签卡片
HWND CreateNoteWindow(HINSTANCE hInstance, NoteCardData* pCardData) {
    NoteWindowContext* pCtx = new NoteWindowContext();
    pCtx->pData = pCardData;

    DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;
    if (pCardData->isAlwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }

    HWND hwnd = CreateWindowExW(
        exStyle,
        L"MyStickyNotes_Main",
        L"Sticky Notes",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        pCardData->windowX, pCardData->windowY,
        pCardData->windowWidth, pCardData->windowHeight,
        g_hOwnerWnd,
        NULL, hInstance, pCtx
    );

    if (!hwnd) {
        delete pCtx;
        return NULL;
    }

    g_noteWindows.push_back(pCtx);

    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    ApplyOpacity(pCtx);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    RECT rcInit;
    GetClientRect(hwnd, &rcInit);
    LayoutControls(pCtx, rcInit.right - rcInit.left, rcInit.bottom - rcInit.top);

    if (pCardData->isAlwaysOnTop) {
        SendMessage(hwnd, WM_EXITSIZEMOVE, 0, 0);
    }

    return hwnd;
}

// 主窗口 WndProc
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCS = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pCS->lpCreateParams);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    NoteWindowContext* pCtx = (NoteWindowContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (uMsg) {
    case WM_CREATE: {
        if (!pCtx) {
            CREATESTRUCTW* pCS = (CREATESTRUCTW*)lParam;
            pCtx = (NoteWindowContext*)pCS->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pCtx);
        }
        if (pCtx) {
            pCtx->hWnd = hwnd;
        }

        UINT dpi = GetWindowDpi(hwnd);
        UpdateFontsForDpi(dpi);

        // 待办新增输入框
        pCtx->hEditInput = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INPUT, g_hInstance, NULL
        );
        SendMessage(pCtx->hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(pCtx->hEditInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入待办后按回车直接添加...");
        pCtx->oldEditInputProc = (WNDPROC)SetWindowLongPtrW(pCtx->hEditInput, GWLP_WNDPROC, (LONG_PTR)EditInputSubclassProc);

        // 待办内联编辑输入框 (初始隐藏)
        pCtx->hInlineEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INLINE_TODO, g_hInstance, NULL
        );
        SendMessage(pCtx->hInlineEdit, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        pCtx->oldInlineEditProc = (WNDPROC)SetWindowLongPtrW(pCtx->hInlineEdit, GWLP_WNDPROC, (LONG_PTR)InlineEditSubclassProc);

        // 现代富文本 RichEdit 4.1+
        pCtx->hRichMemo = CreateWindowExW(
            0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd, (HMENU)ID_RICH_MEMO, g_hInstance, NULL
        );

        // 启用高级事件监听与超链接点击识别 (ENM_LINK)
        SendMessage(pCtx->hRichMemo, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK);
        SendMessage(pCtx->hRichMemo, EM_AUTOURLDETECT, AURL_ENABLEURL, 0);

        // 加载 RTF 格式
        LoadRichTextToControl(pCtx->hRichMemo, pCtx->pData->memoRtf);

        // 统一应用主题色与不透明度
        ApplyThemeToControls(pCtx);
        ApplyOpacity(pCtx);

        RECT rc;
        GetClientRect(hwnd, &rc);
        LayoutControls(pCtx, rc.right - rc.left, rc.bottom - rc.top);

        UpdateControlVisibility(pCtx);
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* pNmhdr = (NMHDR*)lParam;
        if (pNmhdr && pNmhdr->idFrom == ID_RICH_MEMO && pNmhdr->code == EN_LINK) {
            ENLINK* pLink = (ENLINK*)lParam;
            if (pLink->msg == WM_LBUTTONUP && pCtx && pCtx->hRichMemo) {
                TEXTRANGEW tr = { 0 };
                tr.chrg = pLink->chrg;
                int len = pLink->chrg.cpMax - pLink->chrg.cpMin;
                if (len > 0 && len < 2048) {
                    std::vector<wchar_t> buf(len + 1);
                    tr.lpstrText = buf.data();
                    SendMessageW(pCtx->hRichMemo, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
                    ShellExecuteW(NULL, L"open", buf.data(), NULL, NULL, SW_SHOWNORMAL);
                    return 1;
                }
            }
        }
        break;
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
        if (pCtx) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            LayoutControls(pCtx, rc.right - rc.left, rc.bottom - rc.top);
        }
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        if (!pCtx || !pCtx->pData) break;
        HWND hCtl = (HWND)lParam;
        ColorScheme colors = GetThemeColors(pCtx->pData->colorTheme);
        HDC hdcCtl = (HDC)wParam;
        SetTextColor(hdcCtl, colors.textPrimary);
        if (hCtl == pCtx->hEditInput || hCtl == pCtx->hInlineEdit) {
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
        if (pCtx && pCtx->pData) {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            pCtx->pData->windowWidth = width;
            pCtx->pData->windowHeight = height;
            LayoutControls(pCtx, width, height);
            InvalidateRect(hwnd, NULL, FALSE);
            SaveDataDeferred(hwnd);
        }
        return 0;
    }

    case WM_MOVE: {
        HideTooltip();
        if (pCtx && pCtx->pData && !pCtx->isCollapsed) {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            pCtx->pData->windowX = rc.left;
            pCtx->pData->windowY = rc.top;
            SaveDataDeferred(hwnd);
        }
        return 0;
    }

    case WM_ENTERSIZEMOVE: {
        if (pCtx) {
            pCtx->isMoving = true;
            if (pCtx->isCollapsed) {
                pCtx->isCollapsed = false;
                KillTimer(hwnd, ID_TIMER_EDGE_ANIM);
                SetWindowPos(hwnd, NULL, pCtx->expandedRect.left, pCtx->expandedRect.top, 0, 0,
                             SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        break;
    }

    case WM_EXITSIZEMOVE: {
        if (!pCtx || !pCtx->pData) break;
        pCtx->isMoving = false;
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (pCtx->pData->isAlwaysOnTop) {
            UINT dpi = GetWindowDpi(hwnd);
            RECT workRc = GetCurrentMonitorWorkArea(hwnd);
            int threshold = ScaleDpi(20, dpi);

            EdgeDockState newDock = EDGE_NONE;
            if (rc.left <= workRc.left + threshold) {
                newDock = EDGE_LEFT;
                rc.left = workRc.left;
                rc.right = rc.left + w;
            } else if (rc.right >= workRc.right - threshold) {
                newDock = EDGE_RIGHT;
                rc.left = workRc.right - w;
                rc.right = workRc.right;
            } else if (rc.top <= workRc.top + threshold) {
                newDock = EDGE_TOP;
                rc.top = workRc.top;
                rc.bottom = rc.top + h;
            }

            if (newDock != EDGE_NONE) {
                SetWindowPos(hwnd, NULL, rc.left, rc.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                pCtx->dockState = newDock;
                pCtx->expandedRect = rc;
                pCtx->isCollapsed = false;
                pCtx->leaveCounter = 0;

                pCtx->pData->windowX = rc.left;
                pCtx->pData->windowY = rc.top;
                SaveDataDeferred(hwnd);

                SetTimer(hwnd, ID_TIMER_EDGE_CHECK, 100, NULL);
                return 0;
            }
        }

        ResetDockState(pCtx);
        pCtx->pData->windowX = rc.left;
        pCtx->pData->windowY = rc.top;
        SaveDataDeferred(hwnd);
        return 0;
    }

    case WM_TIMER: {
        if (!pCtx || !pCtx->pData) return 0;

        if (wParam == ID_TIMER_SAVE) {
            KillTimer(hwnd, ID_TIMER_SAVE);
            pCtx->pData->memoRtf = GetRichTextFromControl(pCtx->hRichMemo);
            DataStore::Save(g_appData);
            return 0;
        }

        if (wParam == ID_TIMER_EDGE_ANIM) {
            pCtx->animStep++;
            float t = (float)pCtx->animStep / DOCK_ANIM_TOTAL_STEPS;
            if (t >= 1.0f) {
                t = 1.0f;
                KillTimer(hwnd, ID_TIMER_EDGE_ANIM);
            }
            float ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            int curX = pCtx->animStartX + (int)((pCtx->animTargetX - pCtx->animStartX) * ease);
            int curY = pCtx->animStartY + (int)((pCtx->animTargetY - pCtx->animStartY) * ease);
            SetWindowPos(hwnd, NULL, curX, curY, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }

        if (wParam == ID_TIMER_EDGE_CHECK) {
            if (pCtx->dockState == EDGE_NONE || !pCtx->pData->isAlwaysOnTop || pCtx->isMoving) {
                return 0;
            }

            POINT pt;
            GetCursorPos(&pt);
            RECT curRc;
            GetWindowRect(hwnd, &curRc);

            if (pCtx->isCollapsed) {
                RECT hitRc = curRc;
                InflateRect(&hitRc, 3, 3);
                if (PtInRect(&hitRc, pt)) {
                    ExpandDockedWindow(pCtx);
                }
            } else {
                RECT hitRc = curRc;
                InflateRect(&hitRc, 8, 8);

                bool isMousePressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                HWND hFocus = GetFocus();
                bool isEditing = (hFocus == pCtx->hRichMemo || hFocus == pCtx->hEditInput ||
                                  hFocus == pCtx->hInlineEdit || IsChild(hwnd, hFocus));

                if (!PtInRect(&hitRc, pt) && !isMousePressed && !isEditing) {
                    pCtx->leaveCounter++;
                    if (pCtx->leaveCounter >= 3) {
                        pCtx->leaveCounter = 0;
                        CollapseDockedWindow(pCtx);
                    }
                } else {
                    pCtx->leaveCounter = 0;
                }
            }
            return 0;
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

        if (id == ID_TRAY_NEW_NOTE) {
            // 新建便签卡片
            NoteCardData newCard;
            newCard.noteId = "note_" + std::to_string(g_appData.nextNoteId++);
            if (pCtx && pCtx->pData) {
                newCard.windowX = pCtx->pData->windowX + 30;
                newCard.windowY = pCtx->pData->windowY + 30;
                newCard.colorTheme = (pCtx->pData->colorTheme + 1) % 6;
                newCard.opacity = pCtx->pData->opacity;
                newCard.isAlwaysOnTop = pCtx->pData->isAlwaysOnTop;
            }
            g_appData.notes.push_back(newCard);
            CreateNoteWindow(g_hInstance, &g_appData.notes.back());
            DataStore::Save(g_appData);
            return 0;
        }

        if (id == ID_TRAY_SHOW) {
            ShowWindow(hwnd, SW_SHOW);
            if (pCtx && pCtx->isCollapsed) {
                ExpandDockedWindow(pCtx);
            }
            SetForegroundWindow(hwnd);
            return 0;
        }

        if (id == ID_TRAY_HIDE) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        if (id == ID_TRAY_SHOW_ALL) {
            for (auto* ctx : g_noteWindows) {
                ShowWindow(ctx->hWnd, SW_SHOW);
                if (ctx->isCollapsed) {
                    ExpandDockedWindow(ctx);
                }
            }
            SetForegroundWindow(hwnd);
            return 0;
        }

        if (id == ID_TRAY_HIDE_ALL) {
            for (auto* ctx : g_noteWindows) {
                ShowWindow(ctx->hWnd, SW_HIDE);
            }
            return 0;
        }

        if (id == ID_TRAY_TOPMOST) {
            if (pCtx && pCtx->pData) {
                pCtx->pData->isAlwaysOnTop = !pCtx->pData->isAlwaysOnTop;
                SetWindowPos(hwnd, pCtx->pData->isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                if (!pCtx->pData->isAlwaysOnTop) {
                    ResetDockState(pCtx);
                } else {
                    SendMessage(hwnd, WM_EXITSIZEMOVE, 0, 0);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
            }
            return 0;
        }

        if (id == ID_TRAY_OPACITY_100 || id == ID_TRAY_OPACITY_90 || id == ID_TRAY_OPACITY_80) {
            int op = (id == ID_TRAY_OPACITY_100) ? 100 : ((id == ID_TRAY_OPACITY_90) ? 90 : 80);
            if (pCtx && pCtx->pData) {
                pCtx->pData->opacity = op;
                ApplyOpacity(pCtx);
                SaveDataDeferred(hwnd);
            }
            return 0;
        }

        if (id >= ID_TRAY_COLOR_BASE && id <= ID_TRAY_COLOR_BASE + 5) {
            if (pCtx && pCtx->pData) {
                pCtx->pData->colorTheme = id - ID_TRAY_COLOR_BASE;
                ApplyThemeToControls(pCtx);
                TrayManager::UpdateTrayIcon(hwnd, pCtx->pData->colorTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                SaveDataDeferred(hwnd);
            }
            return 0;
        }

        if (id == ID_TRAY_AUTOSTART) {
            bool current = AutostartManager::IsAutostartEnabled();
            AutostartManager::SetAutostartEnabled(!current);
            return 0;
        }

        if (id == ID_TRAY_EXIT) {
            for (auto* ctx : g_noteWindows) {
                if (ctx->pData) {
                    ctx->pData->memoRtf = GetRichTextFromControl(ctx->hRichMemo);
                }
            }
            DataStore::Save(g_appData);
            TrayManager::RemoveTrayIcon(hwnd);
            PostQuitMessage(0);
            return 0;
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONUP) {
            bool anyVisible = false;
            for (auto* ctx : g_noteWindows) {
                if (IsWindowVisible(ctx->hWnd)) {
                    anyVisible = true;
                    break;
                }
            }
            if (anyVisible) {
                for (auto* ctx : g_noteWindows) {
                    ShowWindow(ctx->hWnd, SW_HIDE);
                }
            } else {
                for (auto* ctx : g_noteWindows) {
                    ShowWindow(ctx->hWnd, SW_SHOW);
                    if (ctx->isCollapsed) {
                        ExpandDockedWindow(ctx);
                    }
                }
                SetForegroundWindow(hwnd);
            }
        } else if (lParam == WM_RBUTTONUP) {
            if (pCtx && pCtx->pData) {
                TrayManager::ShowContextMenu(
                    hwnd,
                    pCtx->pData->isAlwaysOnTop,
                    pCtx->pData->colorTheme,
                    pCtx->pData->opacity,
                    AutostartManager::IsAutostartEnabled()
                );
            }
        }
        return 0;
    }

    case WM_NCHITTEST: {
        if (pCtx && pCtx->isCollapsed) {
            return HTCLIENT;
        }
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

    case WM_LBUTTONDBLCLK: {
        if (!pCtx || !pCtx->pData) return 0;
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;

        UINT dpi = GetWindowDpi(hwnd);
        LayoutMetrics m = GetMetrics(dpi);

        // 待办清单项双击就地编辑
        if (pCtx->pData->currentTab == 0) {
            int inputCardY = m.headerH + m.tabH + ScaleDpi(10, dpi) + m.progressH + ScaleDpi(8, dpi);
            int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
            if (y >= listStartY) {
                int clickedIdx = (y - listStartY + pCtx->scrollOffset) / m.itemH;
                if (clickedIdx >= 0 && clickedIdx < (int)pCtx->pData->todos.size()) {
                    int itemTop = listStartY + clickedIdx * m.itemH - pCtx->scrollOffset;
                    int checkSize = ScaleDpi(18, dpi);
                    int textLeft = m.padding + ScaleDpi(12, dpi) + checkSize + ScaleDpi(12, dpi);
                    int textRight = width - m.padding - ScaleDpi(36, dpi);
                    int editH = ScaleDpi(22, dpi);
                    int editY = itemTop + (m.itemH - editH) / 2;

                    pCtx->editingTodoIdx = clickedIdx;
                    SetWindowTextW(pCtx->hInlineEdit, pCtx->pData->todos[clickedIdx].text.c_str());
                    MoveWindow(pCtx->hInlineEdit, textLeft, editY, textRight - textLeft, editH, TRUE);
                    ShowWindow(pCtx->hInlineEdit, SW_SHOW);
                    SetFocus(pCtx->hInlineEdit);
                    SendMessage(pCtx->hInlineEdit, EM_SETSEL, 0, -1);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        if (!pCtx || !pCtx->pData) return 0;

        if (pCtx->isCollapsed && pCtx->dockState != EDGE_NONE) {
            ExpandDockedWindow(pCtx);
            return 0;
        }

        if (pCtx->editingTodoIdx >= 0) {
            FinishInlineEdit(pCtx, true);
        }

        HideTooltip();
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;

        UINT dpi = GetWindowDpi(hwnd);
        LayoutMetrics m = GetMetrics(dpi);

        // 1. 顶部 Header (4 个按钮: 1: [+], 2: [📌], 3: [🎨], 4: [×])
        if (y < m.headerH) {
            RECT addBtnRc = GetHeaderBtnRect(width, 1, dpi);
            RECT pinBtnRc = GetHeaderBtnRect(width, 2, dpi);
            RECT themeBtnRc = GetHeaderBtnRect(width, 3, dpi);
            RECT closeBtnRc = GetHeaderBtnRect(width, 4, dpi);

            // 1. 新建便签 [+]
            if (x >= addBtnRc.left && x <= addBtnRc.right && y >= addBtnRc.top && y <= addBtnRc.bottom) {
                SendMessage(hwnd, WM_COMMAND, ID_TRAY_NEW_NOTE, 0);
                return 0;
            }

            // 2. 置顶 [📌]
            if (x >= pinBtnRc.left && x <= pinBtnRc.right && y >= pinBtnRc.top && y <= pinBtnRc.bottom) {
                pCtx->pData->isAlwaysOnTop = !pCtx->pData->isAlwaysOnTop;
                SetWindowPos(hwnd, pCtx->pData->isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                if (!pCtx->pData->isAlwaysOnTop) {
                    ResetDockState(pCtx);
                } else {
                    SendMessage(hwnd, WM_EXITSIZEMOVE, 0, 0);
                }
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
                return 0;
            }

            // 3. 换色 [🎨]
            if (x >= themeBtnRc.left && x <= themeBtnRc.right && y >= themeBtnRc.top && y <= themeBtnRc.bottom) {
                pCtx->pData->colorTheme = (pCtx->pData->colorTheme + 1) % 6;
                ApplyThemeToControls(pCtx);
                TrayManager::UpdateTrayIcon(hwnd, pCtx->pData->colorTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                SaveDataDeferred(hwnd);
                return 0;
            }

            // 4. 关闭/隐藏 [×]
            if (x >= closeBtnRc.left && x <= closeBtnRc.right && y >= closeBtnRc.top && y <= closeBtnRc.bottom) {
                if (g_noteWindows.size() > 1) {
                    // 多卡片时：销毁当前便签并从数据中移除
                    std::string deletedId = pCtx->pData ? pCtx->pData->noteId : "";
                    for (auto it = g_appData.notes.begin(); it != g_appData.notes.end(); ++it) {
                        if (it->noteId == deletedId) {
                            g_appData.notes.erase(it);
                            break;
                        }
                    }
                    DestroyWindow(hwnd);
                    DataStore::Save(g_appData);
                } else {
                    // 仅剩最后一张时：隐藏到托盘
                    ShowWindow(hwnd, SW_HIDE);
                }
                return 0;
            }

            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        // 2. Tab 切换
        if (y >= m.headerH && y < m.headerH + m.tabH) {
            int newTab = (x < width / 2) ? 0 : 1;
            if (newTab != pCtx->pData->currentTab) {
                pCtx->pData->currentTab = newTab;
                UpdateControlVisibility(pCtx);
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
            }
            return 0;
        }

        // 3. Tab 0 待办事项交互
        if (pCtx->pData->currentTab == 0) {
            int inputCardY = m.headerH + m.tabH + ScaleDpi(10, dpi) + m.progressH + ScaleDpi(8, dpi);
            int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
            if (y >= listStartY) {
                int clickedIdx = (y - listStartY + pCtx->scrollOffset) / m.itemH;
                if (clickedIdx >= 0 && clickedIdx < (int)pCtx->pData->todos.size()) {
                    int delBtnW = ScaleDpi(22, dpi);
                    int delRight = width - m.padding - ScaleDpi(8, dpi);
                    int delLeft = delRight - delBtnW;
                    // 删除
                    if (x >= delLeft && x <= delRight) {
                        pCtx->pData->todos.erase(pCtx->pData->todos.begin() + clickedIdx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        SaveDataDeferred(hwnd);
                        return 0;
                    }
                    // 勾选切换
                    pCtx->pData->todos[clickedIdx].completed = !pCtx->pData->todos[clickedIdx].completed;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SaveDataDeferred(hwnd);
                    return 0;
                }
            }
        }

        // 4. Tab 1 富文本底部工具栏点击响应 (7 个按钮: 1..7)
        if (pCtx->pData->currentTab == 1 && y >= height - m.formatBarH) {
            for (int b = 1; b <= 7; ++b) {
                RECT bRc = GetFormatBtnRect(width, height, b, dpi);
                if (x >= bRc.left && x <= bRc.right && y >= bRc.top && y <= bRc.bottom) {
                    HideTooltip();
                    if (b == 1) ApplyFormatToSelection(pCtx->hRichMemo, CFM_BOLD, CFE_BOLD);
                    else if (b == 2) ApplyFormatToSelection(pCtx->hRichMemo, CFM_ITALIC, CFE_ITALIC);
                    else if (b == 3) ApplyFormatToSelection(pCtx->hRichMemo, CFM_UNDERLINE, CFE_UNDERLINE);
                    else if (b == 4) ApplyFormatToSelection(pCtx->hRichMemo, CFM_STRIKEOUT, CFE_STRIKEOUT);
                    else if (b == 5) ToggleBullet(pCtx->hRichMemo);
                    else if (b == 6) ToggleHighlight(pCtx->hRichMemo);
                    else if (b == 7) InsertImageFromFile(hwnd, pCtx->hRichMemo);
                    SaveDataDeferred(hwnd);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (!pCtx || !pCtx->pData) return 0;
        if (pCtx->isCollapsed && pCtx->dockState != EDGE_NONE) {
            ExpandDockedWindow(pCtx);
        }
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

        int prevHeaderBtn = pCtx->hoveredHeaderBtn;
        pCtx->hoveredHeaderBtn = 0;
        if (y < m.headerH) {
            for (int h = 1; h <= 4; ++h) {
                RECT hRc = GetHeaderBtnRect(width, h, dpi);
                if (x >= hRc.left && x <= hRc.right && y >= hRc.top && y <= hRc.bottom) {
                    pCtx->hoveredHeaderBtn = h;
                    break;
                }
            }
        }

        int prevFmtBtn = pCtx->hoveredFmtBtn;
        pCtx->hoveredFmtBtn = 0;
        if (pCtx->pData->currentTab == 1 && y >= height - m.formatBarH) {
            for (int b = 1; b <= 7; ++b) {
                RECT bRc = GetFormatBtnRect(width, height, b, dpi);
                if (x >= bRc.left && x <= bRc.right && y >= bRc.top && y <= bRc.bottom) {
                    pCtx->hoveredFmtBtn = b;
                    break;
                }
            }
        }

        // 动态展示/刷新/隐藏 Tooltip 提示
        if (pCtx->hoveredFmtBtn > 0) {
            RECT bRc = GetFormatBtnRect(width, height, pCtx->hoveredFmtBtn, dpi);
            ShowTooltip(hwnd, FMT_BTN_TITLES[pCtx->hoveredFmtBtn - 1], bRc, true, dpi);
        } else if (pCtx->hoveredHeaderBtn > 0) {
            RECT hRc = GetHeaderBtnRect(width, pCtx->hoveredHeaderBtn, dpi);
            std::wstring hText = HEADER_BTN_TITLES[pCtx->hoveredHeaderBtn - 1];
            if (pCtx->hoveredHeaderBtn == 2) {
                hText = pCtx->pData->isAlwaysOnTop ? L"取消置顶" : L"置顶窗口";
            }
            ShowTooltip(hwnd, hText, hRc, false, dpi);
        } else {
            HideTooltip();
        }

        if (pCtx->pData->currentTab == 0) {
            int inputCardY = m.headerH + m.tabH + ScaleDpi(10, dpi) + m.progressH + ScaleDpi(8, dpi);
            int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
            int prevHovered = pCtx->hoveredItemIdx;
            int prevDel = pCtx->hoveredDeleteIdx;

            if (y >= listStartY) {
                int hoverIdx = (y - listStartY + pCtx->scrollOffset) / m.itemH;
                if (hoverIdx >= 0 && hoverIdx < (int)pCtx->pData->todos.size()) {
                    pCtx->hoveredItemIdx = hoverIdx;
                    int delBtnW = ScaleDpi(22, dpi);
                    int delRight = width - m.padding - ScaleDpi(8, dpi);
                    int delLeft = delRight - delBtnW;
                    if (x >= delLeft && x <= delRight) {
                        pCtx->hoveredDeleteIdx = hoverIdx;
                    } else {
                        pCtx->hoveredDeleteIdx = -1;
                    }
                } else {
                    pCtx->hoveredItemIdx = -1;
                    pCtx->hoveredDeleteIdx = -1;
                }
            } else {
                pCtx->hoveredItemIdx = -1;
                pCtx->hoveredDeleteIdx = -1;
            }

            if (prevHovered != pCtx->hoveredItemIdx || prevDel != pCtx->hoveredDeleteIdx || prevHeaderBtn != pCtx->hoveredHeaderBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            if (prevHeaderBtn != pCtx->hoveredHeaderBtn || prevFmtBtn != pCtx->hoveredFmtBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        HideTooltip();
        if (pCtx) {
            pCtx->hoveredItemIdx = -1;
            pCtx->hoveredDeleteIdx = -1;
            pCtx->hoveredHeaderBtn = 0;
            pCtx->hoveredFmtBtn = 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_KILLFOCUS: {
        HideTooltip();
        break;
    }

    case WM_MOUSEWHEEL: {
        if (pCtx && pCtx->pData && pCtx->pData->currentTab == 0) {
            UINT dpi = GetWindowDpi(hwnd);
            LayoutMetrics m = GetMetrics(dpi);
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            pCtx->scrollOffset -= (delta / WHEEL_DELTA) * m.itemH;
            if (pCtx->scrollOffset < 0) pCtx->scrollOffset = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        Render(pCtx, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY: {
        HideTooltip();
        if (pCtx && pCtx->pData) {
            pCtx->pData->memoRtf = GetRichTextFromControl(pCtx->hRichMemo);
        }
        auto it = std::find(g_noteWindows.begin(), g_noteWindows.end(), pCtx);
        if (it != g_noteWindows.end()) {
            g_noteWindows.erase(it);
        }
        if (g_noteWindows.empty()) {
            PostQuitMessage(0);
        }
        return 0;
    }

    case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (pCtx) {
            delete pCtx;
        }
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

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"MyStickyNotes_SingleInstance_Mutex_v5");
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

    if (g_appData.notes.empty()) {
        NoteCardData defaultCard;
        defaultCard.noteId = "default";
        g_appData.notes.push_back(defaultCard);
    }

    // 防止窗口越界
    int vLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    for (auto& card : g_appData.notes) {
        POINT ptTarget = { card.windowX, card.windowY };
        UINT targetDpi = GetDpiForPoint(ptTarget);
        if (targetDpi > 96 && card.windowWidth <= 360) {
            card.windowWidth = ScaleDpi(card.windowWidth, targetDpi);
            card.windowHeight = ScaleDpi(card.windowHeight, targetDpi);
        }
        if (card.windowX < vLeft - 100 || card.windowX > vLeft + vWidth - 100 ||
            card.windowY < vTop - 50 || card.windowY > vTop + vHeight - 100) {
            card.windowX = 100;
            card.windowY = 100;
        }
    }

    // 0. Tooltip 悬浮提示窗口类
    WNDCLASSEXW twc = { sizeof(twc) };
    twc.lpfnWndProc = TooltipWndProc;
    twc.hInstance = hInstance;
    twc.lpszClassName = L"StickyNotes_Tooltip";
    twc.hCursor = LoadCursor(NULL, IDC_ARROW);
    twc.style = CS_DROPSHADOW;
    RegisterClassExW(&twc);

    // 1. Owner Window
    WNDCLASSEXW owc = { sizeof(owc) };
    owc.lpfnWndProc = OwnerWndProc;
    owc.hInstance = hInstance;
    owc.lpszClassName = L"MyStickyNotes_Owner";
    RegisterClassExW(&owc);

    g_hOwnerWnd = CreateWindowExW(
        0, L"MyStickyNotes_Owner", L"",
        WS_OVERLAPPEDWINDOW, g_appData.notes[0].windowX, g_appData.notes[0].windowY, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    // 2. Main Window 窗口类 (加入 CS_DBLCLKS 支持双击就地编辑)
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyStickyNotes_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    wc.hbrBackground = NULL;
    wc.style = CS_DBLCLKS;
    RegisterClassExW(&wc);

    // 创建悬浮提示 Tooltip 窗口
    g_hTooltipWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"StickyNotes_Tooltip", L"",
        WS_POPUP,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    // 创建并实例化所有便签卡片窗口
    for (size_t i = 0; i < g_appData.notes.size(); ++i) {
        CreateNoteWindow(hInstance, &g_appData.notes[i]);
    }

    // 托盘图标绑定主便签窗口
    if (!g_noteWindows.empty()) {
        TrayManager::AddTrayIcon(g_noteWindows[0]->hWnd, g_appData.notes[0].colorTheme);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hTooltipWnd) DestroyWindow(g_hTooltipWnd);
    if (!g_noteWindows.empty()) {
        TrayManager::RemoveTrayIcon(g_noteWindows[0]->hWnd);
    }

    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontStrike) DeleteObject(g_hFontStrike);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);
    if (g_hFontFmtB) DeleteObject(g_hFontFmtB);
    if (g_hFontFmtI) DeleteObject(g_hFontFmtI);
    if (g_hFontFmtU) DeleteObject(g_hFontFmtU);
    if (g_hFontFmtAB) DeleteObject(g_hFontFmtAB);

    if (hMutex) CloseHandle(hMutex);
    if (g_hMsftEdit) FreeLibrary(g_hMsftEdit);
    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}
