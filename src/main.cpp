#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include "data_store.h"
#include "ui_theme.h"
#include "tray.h"

// 控件与交互常量
#define ID_EDIT_INPUT   1001
#define ID_EDIT_MEMO    1003
#define ID_TIMER_SAVE   1004

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HWND g_hOwnerWnd = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hEditInput = NULL;
static HWND g_hEditMemo = NULL;
static WNDPROC g_oldEditInputProc = NULL;

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontStrike = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontIcon = NULL;

static AppData g_appData;
static int g_scrollOffset = 0;
static int g_hoveredItemIdx = -1;
static int g_hoveredDeleteIdx = -1;
static int g_hoveredHeaderBtn = 0; // 1: Pin, 2: Theme, 3: Close/Min

// 布局常量
static const int HEADER_HEIGHT = 44;
static const int TAB_HEIGHT = 36;
static const int PROGRESS_AREA_HEIGHT = 48;
static const int INPUT_AREA_HEIGHT = 44;
static const int ITEM_HEIGHT = 40;
static const int PADDING = 14;

// 函数声明
LRESULT CALLBACK OwnerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditInputSubclassProc(HWND, UINT, WPARAM, LPARAM);
void SaveDataDeferred(HWND hwnd);
void UpdateControlVisibility();
void LayoutControls(int width, int height);

// 统计进度
static void GetTodoStats(int& completed, int& total, float& percent) {
    total = (int)g_appData.todos.size();
    completed = 0;
    for (const auto& item : g_appData.todos) {
        if (item.completed) completed++;
    }
    percent = (total == 0) ? 0.0f : ((float)completed / (float)total);
}

// 绘制平滑圆角矩形
static void DrawModernRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fillColor, COLORREF borderColor) {
    HBRUSH brush = CreateSolidBrush(fillColor);
    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

// 双缓冲渲染现代化 Microsoft 便笺界面
static void Render(HWND hwnd, HDC hdc) {
    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    ColorScheme colors = GetThemeColors(g_appData.colorTheme);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 1. 整体底色填充
    HBRUSH bodyBrush = CreateSolidBrush(colors.bodyBg);
    FillRect(memDC, &clientRc, bodyBrush);
    DeleteObject(bodyBrush);

    // 2. 顶部 Fluent 导航栏
    RECT headerRc = { 0, 0, width, HEADER_HEIGHT };
    HBRUSH headerBrush = CreateSolidBrush(colors.headerBg);
    FillRect(memDC, &headerRc, headerBrush);
    DeleteObject(headerBrush);

    // 绘制标题
    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, g_hFontTitle);
    SetTextColor(memDC, colors.textPrimary);
    RECT titleRc = { PADDING, 0, width - 120, HEADER_HEIGHT };
    DrawTextW(memDC, L"便笺 & 进度跟踪", -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    // 顶部右侧现代化小胶囊按钮: [📌 置顶] [🎨 换色] [× 隐藏]
    int btnY = (HEADER_HEIGHT - 26) / 2;
    int btnH = 26;

    // 置顶按钮
    RECT pinBtnRc = { width - 100, btnY, width - 70, btnY + btnH };
    if (g_hoveredHeaderBtn == 1 || g_appData.isAlwaysOnTop) {
        DrawModernRoundRect(memDC, pinBtnRc, 6, g_appData.isAlwaysOnTop ? colors.accent : colors.hoverOverlay, colors.cardBorder);
        SetTextColor(memDC, g_appData.isAlwaysOnTop ? RGB(255, 255, 255) : colors.textPrimary);
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    SelectObject(memDC, g_hFontNormal);
    DrawTextW(memDC, L"📌", -1, &pinBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 切换颜色按钮
    RECT themeBtnRc = { width - 66, btnY, width - 38, btnY + btnH };
    if (g_hoveredHeaderBtn == 2) {
        DrawModernRoundRect(memDC, themeBtnRc, 6, colors.hoverOverlay, colors.cardBorder);
    }
    SetTextColor(memDC, colors.textSecondary);
    DrawTextW(memDC, L"🎨", -1, &themeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 隐藏/最小化按钮
    RECT closeBtnRc = { width - 34, btnY, width - 8, btnY + btnH };
    if (g_hoveredHeaderBtn == 3) {
        DrawModernRoundRect(memDC, closeBtnRc, 6, RGB(254, 226, 226), RGB(248, 113, 113));
        SetTextColor(memDC, RGB(220, 38, 38));
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"×", -1, &closeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 3. 现代化 Tab 切换栏
    int tabY = HEADER_HEIGHT;
    int tabWidth = width / 2;
    RECT tab1Rc = { 0, tabY, tabWidth, tabY + TAB_HEIGHT };
    RECT tab2Rc = { tabWidth, tabY, width, tabY + TAB_HEIGHT };

    // Tab 1 (待办进度)
    if (g_appData.currentTab == 0) {
        HBRUSH tBrush = CreateSolidBrush(colors.bodyBg);
        FillRect(memDC, &tab1Rc, tBrush);
        DeleteObject(tBrush);

        // 下划指示胶囊条
        RECT indRc = { tabWidth / 2 - 32, tabY + TAB_HEIGHT - 3, tabWidth / 2 + 32, tabY + TAB_HEIGHT };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, g_hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"待办进度", -1, &tab1Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 2 (速记笔记)
    if (g_appData.currentTab == 1) {
        HBRUSH tBrush = CreateSolidBrush(colors.bodyBg);
        FillRect(memDC, &tab2Rc, tBrush);
        DeleteObject(tBrush);

        RECT indRc = { tabWidth + tabWidth / 2 - 32, tabY + TAB_HEIGHT - 3, tabWidth + tabWidth / 2 + 32, tabY + TAB_HEIGHT };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, g_hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"速记笔记", -1, &tab2Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 底部柔和分割线
    HPEN splitPen = CreatePen(PS_SOLID, 1, colors.cardBorder);
    HPEN oldPen = (HPEN)SelectObject(memDC, splitPen);
    MoveToEx(memDC, 0, tabY + TAB_HEIGHT - 1, NULL);
    LineTo(memDC, width, tabY + TAB_HEIGHT - 1);
    SelectObject(memDC, oldPen);
    DeleteObject(splitPen);

    // 4. Tab 0: 待办进度
    if (g_appData.currentTab == 0) {
        int compCount = 0, totalCount = 0;
        float percent = 0.0f;
        GetTodoStats(compCount, totalCount, percent);

        // 4.1 进度条区域卡片化
        int progY = tabY + TAB_HEIGHT + 8;
        RECT progCardRc = { PADDING, progY, width - PADDING, progY + PROGRESS_AREA_HEIGHT };
        DrawModernRoundRect(memDC, progCardRc, 8, colors.cardBg, colors.cardBorder);

        // 文字
        RECT progTextRc = { PADDING + 12, progY + 6, width - PADDING - 12, progY + 24 };
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, colors.textSecondary);

        wchar_t statText[64];
        swprintf_s(statText, L"今日进度：%d / %d 项 (%.0f%%)", compCount, totalCount, percent * 100.0f);
        DrawTextW(memDC, statText, -1, &progTextRc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        // 进度条槽
        int barY = progY + 28;
        int barH = 8;
        int barW = width - PADDING * 2 - 24;
        RECT barBgRc = { PADDING + 12, barY, PADDING + 12 + barW, barY + barH };
        DrawModernRoundRect(memDC, barBgRc, 4, colors.progressBg, colors.progressBg);

        int fillW = (int)(barW * percent);
        if (fillW > 0) {
            RECT barFillRc = { PADDING + 12, barY, PADDING + 12 + fillW, barY + barH };
            DrawModernRoundRect(memDC, barFillRc, 4, colors.accent, colors.accent);
        }

        // 4.2 输入框外框卡片渲染
        int inputCardY = progY + PROGRESS_AREA_HEIGHT + 8;
        RECT inputCardRc = { PADDING, inputCardY, width - PADDING, inputCardY + 36 };
        DrawModernRoundRect(memDC, inputCardRc, 8, colors.cardBg, colors.cardBorder);

        // 4.3 待办事项列表卡片渲染
        int listStartY = inputCardY + 36 + 10;
        int listEndY = height - PADDING;
        int currentY = listStartY - g_scrollOffset;

        if (g_appData.todos.empty()) {
            RECT emptyRc = { 0, listStartY + 30, width, listStartY + 100 };
            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, colors.textSecondary);
            DrawTextW(memDC, L"✨ 暂无待办，输入后按回车快速添加", -1, &emptyRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            for (size_t i = 0; i < g_appData.todos.size(); ++i) {
                if (currentY + ITEM_HEIGHT > listStartY && currentY < listEndY) {
                    RECT itemRc = { PADDING, currentY, width - PADDING, currentY + ITEM_HEIGHT - 4 };

                    // 悬浮微阴影与高亮
                    bool isHovered = ((int)i == g_hoveredItemIdx);
                    COLORREF itemBg = isHovered ? colors.hoverOverlay : colors.cardBg;
                    DrawModernRoundRect(memDC, itemRc, 8, itemBg, colors.cardBorder);

                    // 圆形复选框
                    int boxSize = 18;
                    int boxX = PADDING + 10;
                    int boxY = currentY + (ITEM_HEIGHT - 4 - boxSize) / 2;
                    RECT checkRc = { boxX, boxY, boxX + boxSize, boxY + boxSize };

                    if (g_appData.todos[i].completed) {
                        DrawModernRoundRect(memDC, checkRc, boxSize, colors.accent, colors.accent);
                        SetTextColor(memDC, RGB(255, 255, 255));
                        SelectObject(memDC, g_hFontSmall);
                        DrawTextW(memDC, L"✓", -1, &checkRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    } else {
                        DrawModernRoundRect(memDC, checkRc, boxSize, colors.cardBg, colors.cardBorder);
                    }

                    // 文本
                    RECT textRc = { boxX + boxSize + 12, currentY, width - PADDING - 36, currentY + ITEM_HEIGHT - 4 };
                    if (g_appData.todos[i].completed) {
                        SelectObject(memDC, g_hFontStrike);
                        SetTextColor(memDC, colors.completedText);
                    } else {
                        SelectObject(memDC, g_hFontNormal);
                        SetTextColor(memDC, colors.textPrimary);
                    }
                    DrawTextW(memDC, g_appData.todos[i].text.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    // 悬浮删除小圆钮
                    if (isHovered) {
                        int delSize = 22;
                        int delX = width - PADDING - 30;
                        int delY = currentY + (ITEM_HEIGHT - 4 - delSize) / 2;
                        RECT delRc = { delX, delY, delX + delSize, delY + delSize };

                        bool isDelHover = ((int)i == g_hoveredDeleteIdx);
                        if (isDelHover) {
                            DrawModernRoundRect(memDC, delRc, 6, RGB(254, 226, 226), RGB(248, 113, 113));
                            SetTextColor(memDC, RGB(220, 38, 38));
                        } else {
                            SetTextColor(memDC, colors.textSecondary);
                        }
                        SelectObject(memDC, g_hFontNormal);
                        DrawTextW(memDC, L"×", -1, &delRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    }
                }
                currentY += ITEM_HEIGHT;
            }
        }
    }

    // 5. 最外圈轻量精致边框
    HPEN outPen = CreatePen(PS_SOLID, 1, colors.cardBorder);
    HPEN prevPen = (HPEN)SelectObject(memDC, outPen);
    MoveToEx(memDC, 0, 0, NULL);
    LineTo(memDC, width - 1, 0);
    LineTo(memDC, width - 1, height - 1);
    LineTo(memDC, 0, height - 1);
    LineTo(memDC, 0, 0);
    SelectObject(memDC, prevPen);
    DeleteObject(outPen);

    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

// 控件显隐管理
void UpdateControlVisibility() {
    if (g_appData.currentTab == 0) {
        ShowWindow(g_hEditInput, SW_SHOW);
        ShowWindow(g_hEditMemo, SW_HIDE);
    } else {
        ShowWindow(g_hEditInput, SW_HIDE);
        ShowWindow(g_hEditMemo, SW_SHOW);
    }
}

// 控件布局适配
void LayoutControls(int width, int height) {
    // 单行添加输入框嵌入圆角卡片内部
    int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 8 + PROGRESS_AREA_HEIGHT + 8;
    MoveWindow(g_hEditInput, PADDING + 12, inputCardY + 6, width - PADDING * 2 - 24, 24, TRUE);

    // 多行笔记文本域
    int memoY = HEADER_HEIGHT + TAB_HEIGHT + 8;
    int memoH = height - memoY - PADDING;
    MoveWindow(g_hEditMemo, PADDING, memoY, width - PADDING * 2, memoH, TRUE);
}

// 延时防抖落盘
void SaveDataDeferred(HWND hwnd) {
    SetTimer(hwnd, ID_TIMER_SAVE, 300, NULL);
}

// 输入框子类化过程（专门拦截 Enter 键添加待办，避免 Windows Edit 控件哔声并防止退出）
LRESULT CALLBACK EditInputSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN && wParam == VK_RETURN) {
        // 读取输入文本
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
        return 0; // 彻底吞掉回车，避免默认控件 beep 或退出
    }
    return CallWindowProcW(g_oldEditInputProc, hwnd, uMsg, wParam, lParam);
}

// 主窗口 WndProc
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // 创建 Fluent 字体
        g_hFontTitle = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        if (!g_hFontTitle) {
            g_hFontTitle = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        }

        g_hFontNormal = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        g_hFontStrike = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, TRUE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        g_hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

        // 待办单行输入框（平坦无边框，嵌入卡片背景）
        g_hEditInput = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INPUT, g_hInstance, NULL
        );
        SendMessage(g_hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);
        SendMessage(g_hEditInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入待办后按回车直接添加...");
        // 子类化输入框
        g_oldEditInputProc = (WNDPROC)SetWindowLongPtrW(g_hEditInput, GWLP_WNDPROC, (LONG_PTR)EditInputSubclassProc);

        // 自由速记多行文本域
        g_hEditMemo = CreateWindowExW(
            0, L"EDIT", g_appData.memoText.c_str(),
            WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_MEMO, g_hInstance, NULL
        );
        SendMessage(g_hEditMemo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        UpdateControlVisibility();
        TrayManager::AddTrayIcon(hwnd, g_appData.colorTheme);
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
        } else if (hCtl == g_hEditMemo) {
            SetBkColor(hdcCtl, colors.bodyBg);
            static HBRUSH hBrushBody = NULL;
            if (hBrushBody) DeleteObject(hBrushBody);
            hBrushBody = CreateSolidBrush(colors.bodyBg);
            return (LRESULT)hBrushBody;
        }
        break;
    }

    case WM_SIZE: {
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
            int memoLen = GetWindowTextLengthW(g_hEditMemo);
            std::vector<wchar_t> memoBuf(memoLen + 1);
            GetWindowTextW(g_hEditMemo, memoBuf.data(), memoLen + 1);
            g_appData.memoText = memoBuf.data();
            DataStore::Save(g_appData);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == ID_EDIT_MEMO && code == EN_CHANGE) {
            SaveDataDeferred(hwnd);
            return 0;
        }

        // 托盘菜单处理
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
        int border = 8;
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
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int width = rc.right - rc.left;

        // 1. 顶部 Header
        if (y < HEADER_HEIGHT) {
            int btnY = (HEADER_HEIGHT - 26) / 2;
            int btnH = 26;

            // 置顶
            if (x >= width - 100 && x < width - 70 && y >= btnY && y <= btnY + btnH) {
                g_appData.isAlwaysOnTop = !g_appData.isAlwaysOnTop;
                SetWindowPos(hwnd, g_appData.isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            // 切换主题色 (循环切换)
            if (x >= width - 66 && x < width - 38 && y >= btnY && y <= btnY + btnH) {
                g_appData.colorTheme = (g_appData.colorTheme + 1) % 6;
                TrayManager::UpdateTrayIcon(hwnd, g_appData.colorTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            // 隐藏/最小化到托盘
            if (x >= width - 34 && x <= width - 8 && y >= btnY && y <= btnY + btnH) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }

            // 拖动窗口
            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        // 2. Tab 切换
        if (y >= HEADER_HEIGHT && y < HEADER_HEIGHT + TAB_HEIGHT) {
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
            int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 8 + PROGRESS_AREA_HEIGHT + 8;
            int listStartY = inputCardY + 36 + 10;
            if (y >= listStartY) {
                int clickedIdx = (y - listStartY + g_scrollOffset) / ITEM_HEIGHT;
                if (clickedIdx >= 0 && clickedIdx < (int)g_appData.todos.size()) {
                    // 删除
                    if (x >= width - PADDING - 30 && x <= width - PADDING - 8) {
                        g_appData.todos.erase(g_appData.todos.begin() + clickedIdx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        SaveDataDeferred(hwnd);
                        return 0;
                    }
                    // 完成状态切换
                    g_appData.todos[clickedIdx].completed = !g_appData.todos[clickedIdx].completed;
                    InvalidateRect(hwnd, NULL, FALSE);
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

        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        // Header 按钮悬浮检查
        int prevHeaderBtn = g_hoveredHeaderBtn;
        g_hoveredHeaderBtn = 0;
        if (y < HEADER_HEIGHT) {
            int btnY = (HEADER_HEIGHT - 26) / 2;
            int btnH = 26;
            if (y >= btnY && y <= btnY + btnH) {
                if (x >= width - 100 && x < width - 70) g_hoveredHeaderBtn = 1;
                else if (x >= width - 66 && x < width - 38) g_hoveredHeaderBtn = 2;
                else if (x >= width - 34 && x <= width - 8) g_hoveredHeaderBtn = 3;
            }
        }

        if (g_appData.currentTab == 0) {
            int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 8 + PROGRESS_AREA_HEIGHT + 8;
            int listStartY = inputCardY + 36 + 10;
            int prevHovered = g_hoveredItemIdx;
            int prevDel = g_hoveredDeleteIdx;

            if (y >= listStartY) {
                int hoverIdx = (y - listStartY + g_scrollOffset) / ITEM_HEIGHT;
                if (hoverIdx >= 0 && hoverIdx < (int)g_appData.todos.size()) {
                    g_hoveredItemIdx = hoverIdx;
                    if (x >= width - PADDING - 30 && x <= width - PADDING - 8) {
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
            if (prevHeaderBtn != g_hoveredHeaderBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_hoveredItemIdx = -1;
        g_hoveredDeleteIdx = -1;
        g_hoveredHeaderBtn = 0;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (g_appData.currentTab == 0) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_scrollOffset -= (delta / WHEEL_DELTA) * ITEM_HEIGHT;
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
        TrayManager::RemoveTrayIcon(hwnd);
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontStrike) DeleteObject(g_hFontStrike);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 隐藏 Owner 窗口 Proc（用于彻底隐藏任务栏图标）
LRESULT CALLBACK OwnerWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 程序入口 WinMain
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    g_hInstance = hInstance;

    // 单实例检查（若已运行则直接唤醒并置顶）
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\MyStickyNotes_SingleInstance_Mutex_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowW(L"MyStickyNotes_Main", NULL);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_SHOW);
            SetForegroundWindow(existingWnd);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    DataStore::Load(g_appData);

    // 1. 注册不可见的 Owner 窗口类
    WNDCLASSEXW owc = { sizeof(owc) };
    owc.lpfnWndProc = OwnerWndProc;
    owc.hInstance = hInstance;
    owc.lpszClassName = L"MyStickyNotes_Owner";
    RegisterClassExW(&owc);

    g_hOwnerWnd = CreateWindowExW(
        0, L"MyStickyNotes_Owner", L"",
        WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    // 2. 注册主便笺窗口类
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyStickyNotes_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
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

    // 启用现代 Win11 圆角和微柔和投影
    DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hMainWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hMainWnd, &margins);

    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    // 标准消息循环（输入框回车由子类化 EditInputSubclassProc 完全自主处理）
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}
