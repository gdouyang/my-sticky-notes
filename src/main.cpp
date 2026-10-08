#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include "data_store.h"
#include "ui_theme.h"
#include "tray.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

// 控件与交互常量
#define ID_EDIT_INPUT   1001
#define ID_BTN_ADD      1002
#define ID_EDIT_MEMO    1003
#define ID_TIMER_SAVE   1004

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HWND g_hOwnerWnd = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hEditInput = NULL;
static HWND g_hBtnAdd = NULL;
static HWND g_hEditMemo = NULL;

static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontStrike = NULL;
static HFONT g_hFontSmall = NULL;

static AppData g_appData;
static int g_scrollOffset = 0;
static int g_hoveredItemIdx = -1;
static int g_hoveredDeleteIdx = -1;

// 布局常量
static const int HEADER_HEIGHT = 38;
static const int TAB_HEIGHT = 32;
static const int PROGRESS_HEIGHT = 46;
static const int INPUT_AREA_HEIGHT = 44;
static const int ITEM_HEIGHT = 36;
static const int PADDING = 12;

// 函数声明
LRESULT CALLBACK OwnerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
void SaveDataDeferred(HWND hwnd);
void UpdateControlVisibility();
void LayoutControls(int width, int height);

// 计算待办进度
static void GetTodoStats(int& completed, int& total, float& percent) {
    total = (int)g_appData.todos.size();
    completed = 0;
    for (const auto& item : g_appData.todos) {
        if (item.completed) completed++;
    }
    percent = (total == 0) ? 0.0f : ((float)completed / (float)total);
}

// 绘制双缓冲窗口内容
static void Render(HWND hwnd, HDC hdc) {
    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    // 内存 DC 双缓冲防止闪烁
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 1. 背景填充
    HBRUSH bgBrush = CreateSolidBrush(Theme::BG_COLOR);
    FillRect(memDC, &clientRc, bgBrush);
    DeleteObject(bgBrush);

    // 2. 顶部拖拽标题栏 (Header)
    RECT headerRc = { 0, 0, width, HEADER_HEIGHT };
    HBRUSH headerBrush = CreateSolidBrush(Theme::HEADER_BG);
    FillRect(memDC, &headerRc, headerBrush);
    DeleteObject(headerBrush);

    // 绘制标题与图标文字
    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, g_hFontBold);
    SetTextColor(memDC, Theme::TEXT_MAIN);
    RECT titleRc = { PADDING, 0, width - 100, HEADER_HEIGHT };
    DrawTextW(memDC, L"📌 随手便签 & 进度", -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    // 置顶按钮指示 & 最小化按钮
    SelectObject(memDC, g_hFontNormal);
    RECT pinBtnRc = { width - 66, 6, width - 36, HEADER_HEIGHT - 6 };
    RECT minBtnRc = { width - 32, 6, width - 8, HEADER_HEIGHT - 6 };
    
    SetTextColor(memDC, g_appData.isAlwaysOnTop ? RGB(220, 38, 38) : Theme::TEXT_MUTED);
    DrawTextW(memDC, g_appData.isAlwaysOnTop ? L"[顶]" : L" 置顶", -1, &pinBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SetTextColor(memDC, Theme::TEXT_MUTED);
    DrawTextW(memDC, L" 一 ", -1, &minBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 顶部与内容分隔线
    HPEN borderPen = CreatePen(PS_SOLID, 1, Theme::BORDER_COLOR);
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    MoveToEx(memDC, 0, HEADER_HEIGHT - 1, NULL);
    LineTo(memDC, width, HEADER_HEIGHT - 1);

    // 3. 标签切换栏 (Tab 0: 待办跟踪, Tab 1: 自由速记)
    int tabY = HEADER_HEIGHT;
    RECT tab1Rc = { 0, tabY, width / 2, tabY + TAB_HEIGHT };
    RECT tab2Rc = { width / 2, tabY, width, tabY + TAB_HEIGHT };

    // 绘制 Tab 状态
    if (g_appData.currentTab == 0) {
        HBRUSH activeBrush = CreateSolidBrush(Theme::BG_COLOR);
        FillRect(memDC, &tab1Rc, activeBrush);
        DeleteObject(activeBrush);
    } else {
        HBRUSH inActiveBrush = CreateSolidBrush(Theme::HEADER_BG);
        FillRect(memDC, &tab1Rc, inActiveBrush);
        DeleteObject(inActiveBrush);
    }

    if (g_appData.currentTab == 1) {
        HBRUSH activeBrush = CreateSolidBrush(Theme::BG_COLOR);
        FillRect(memDC, &tab2Rc, activeBrush);
        DeleteObject(activeBrush);
    } else {
        HBRUSH inActiveBrush = CreateSolidBrush(Theme::HEADER_BG);
        FillRect(memDC, &tab2Rc, inActiveBrush);
        DeleteObject(inActiveBrush);
    }

    SelectObject(memDC, g_appData.currentTab == 0 ? g_hFontBold : g_hFontNormal);
    SetTextColor(memDC, g_appData.currentTab == 0 ? Theme::TEXT_MAIN : Theme::TEXT_MUTED);
    DrawTextW(memDC, L"📋 待办进度", -1, &tab1Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(memDC, g_appData.currentTab == 1 ? g_hFontBold : g_hFontNormal);
    SetTextColor(memDC, g_appData.currentTab == 1 ? Theme::TEXT_MAIN : Theme::TEXT_MUTED);
    DrawTextW(memDC, L"📝 自由速记", -1, &tab2Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 下方边线
    MoveToEx(memDC, 0, tabY + TAB_HEIGHT - 1, NULL);
    LineTo(memDC, width, tabY + TAB_HEIGHT - 1);
    MoveToEx(memDC, width / 2, tabY, NULL);
    LineTo(memDC, width / 2, tabY + TAB_HEIGHT);

    // 4. Tab 0: 待办清单与进度条
    if (g_appData.currentTab == 0) {
        int compCount = 0, totalCount = 0;
        float percent = 0.0f;
        GetTodoStats(compCount, totalCount, percent);

        // 4.1 进度条区域
        int progY = tabY + TAB_HEIGHT + 8;
        RECT progTextRc = { PADDING, progY, width - PADDING, progY + 18 };
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, Theme::TEXT_MUTED);

        wchar_t statText[64];
        swprintf_s(statText, L"当前进度：%d / %d 项 (%.0f%%)", compCount, totalCount, percent * 100.0f);
        DrawTextW(memDC, statText, -1, &progTextRc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        // 绘制进度条外槽
        int barY = progY + 22;
        int barHeight = 8;
        int barWidth = width - PADDING * 2;
        RECT barBgRc = { PADDING, barY, PADDING + barWidth, barY + barHeight };
        HBRUSH barBgBrush = CreateSolidBrush(Theme::PROGRESS_BG);
        FillRect(memDC, &barBgRc, barBgBrush);
        DeleteObject(barBgBrush);

        // 绘制进度条填充（平滑圆角或整齐条）
        int fillWidth = (int)(barWidth * percent);
        if (fillWidth > 0) {
            RECT barFillRc = { PADDING, barY, PADDING + fillWidth, barY + barHeight };
            HBRUSH barFillBrush = CreateSolidBrush(Theme::ACCENT_GREEN);
            FillRect(memDC, &barFillRc, barFillBrush);
            DeleteObject(barFillBrush);
        }

        // 4.2 待办列表项渲染
        int listStartY = HEADER_HEIGHT + TAB_HEIGHT + PROGRESS_HEIGHT + INPUT_AREA_HEIGHT + 8;
        int listEndY = height - PADDING;
        int currentY = listStartY - g_scrollOffset;

        if (g_appData.todos.empty()) {
            RECT emptyRc = { 0, listStartY + 40, width, listStartY + 100 };
            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, Theme::TEXT_MUTED);
            DrawTextW(memDC, L"暂无待办，在上方的输入框添加一个吧 ~", -1, &emptyRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            for (size_t i = 0; i < g_appData.todos.size(); ++i) {
                if (currentY + ITEM_HEIGHT > listStartY && currentY < listEndY) {
                    RECT itemRc = { PADDING, currentY, width - PADDING, currentY + ITEM_HEIGHT };

                    // 悬浮高亮
                    if ((int)i == g_hoveredItemIdx) {
                        HBRUSH hoverBrush = CreateSolidBrush(Theme::ITEM_HOVER);
                        FillRect(memDC, &itemRc, hoverBrush);
                        DeleteObject(hoverBrush);
                    }

                    // 绘制复选框外框 (16x16)
                    int boxSize = 16;
                    int boxX = PADDING + 6;
                    int boxY = currentY + (ITEM_HEIGHT - boxSize) / 2;
                    RECT checkRc = { boxX, boxY, boxX + boxSize, boxY + boxSize };
                    
                    HBRUSH checkBg = CreateSolidBrush(g_appData.todos[i].completed ? Theme::ACCENT_GREEN : RGB(255, 255, 255));
                    FillRect(memDC, &checkRc, checkBg);
                    DeleteObject(checkBg);

                    HPEN boxPen = CreatePen(PS_SOLID, 1, g_appData.todos[i].completed ? Theme::ACCENT_GREEN : Theme::CHECKBOX_BORDER);
                    HPEN oldBoxPen = (HPEN)SelectObject(memDC, boxPen);
                    Rectangle(memDC, checkRc.left, checkRc.top, checkRc.right, checkRc.bottom);
                    SelectObject(memDC, oldBoxPen);
                    DeleteObject(boxPen);

                    // 勾选标记 '✓'
                    if (g_appData.todos[i].completed) {
                        SetTextColor(memDC, RGB(255, 255, 255));
                        SelectObject(memDC, g_hFontSmall);
                        DrawTextW(memDC, L"✓", -1, &checkRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    }

                    // 文本
                    RECT textRc = { boxX + boxSize + 10, currentY, width - PADDING - 32, currentY + ITEM_HEIGHT };
                    if (g_appData.todos[i].completed) {
                        SelectObject(memDC, g_hFontStrike);
                        SetTextColor(memDC, Theme::COMPLETED_TEXT);
                    } else {
                        SelectObject(memDC, g_hFontNormal);
                        SetTextColor(memDC, Theme::TEXT_MAIN);
                    }
                    DrawTextW(memDC, g_appData.todos[i].text.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    // 删除按钮 [×]（仅悬浮行时展示）
                    if ((int)i == g_hoveredItemIdx) {
                        RECT delRc = { width - PADDING - 26, currentY + 6, width - PADDING - 4, currentY + ITEM_HEIGHT - 6 };
                        SelectObject(memDC, g_hFontNormal);
                        SetTextColor(memDC, ((int)i == g_hoveredDeleteIdx) ? RGB(239, 68, 68) : Theme::TEXT_MUTED);
                        DrawTextW(memDC, L"×", -1, &delRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    }
                }
                currentY += ITEM_HEIGHT;
            }
        }
    }

    // 5. 窗口最外层 1px 优雅描边
    SelectObject(memDC, borderPen);
    MoveToEx(memDC, 0, 0, NULL);
    LineTo(memDC, width - 1, 0);
    LineTo(memDC, width - 1, height - 1);
    LineTo(memDC, 0, height - 1);
    LineTo(memDC, 0, 0);

    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);

    // 一次性贴回屏幕
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

// 控件显隐管理
void UpdateControlVisibility() {
    if (g_appData.currentTab == 0) {
        ShowWindow(g_hEditInput, SW_SHOW);
        ShowWindow(g_hBtnAdd, SW_SHOW);
        ShowWindow(g_hEditMemo, SW_HIDE);
    } else {
        ShowWindow(g_hEditInput, SW_HIDE);
        ShowWindow(g_hBtnAdd, SW_HIDE);
        ShowWindow(g_hEditMemo, SW_SHOW);
    }
}

// 控件布局调整
void LayoutControls(int width, int height) {
    int inputY = HEADER_HEIGHT + TAB_HEIGHT + PROGRESS_HEIGHT + 8;
    int btnW = 54;
    int inputW = width - PADDING * 2 - btnW - 8;
    MoveWindow(g_hEditInput, PADDING, inputY, inputW, 28, TRUE);
    MoveWindow(g_hBtnAdd, PADDING + inputW + 8, inputY, btnW, 28, TRUE);

    int memoY = HEADER_HEIGHT + TAB_HEIGHT + 8;
    int memoHeight = height - memoY - PADDING;
    MoveWindow(g_hEditMemo, PADDING, memoY, width - PADDING * 2, memoHeight, TRUE);
}

// 延时防抖落盘数据
void SaveDataDeferred(HWND hwnd) {
    SetTimer(hwnd, ID_TIMER_SAVE, 300, NULL);
}

// 主窗口 WndProc
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // 创建字体
        g_hFontNormal = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontStrike = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, TRUE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        // 待办添加单行输入框
        g_hEditInput = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_INPUT, g_hInstance, NULL
        );
        SendMessage(g_hEditInput, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // 添加按钮
        g_hBtnAdd = CreateWindowExW(
            0, L"BUTTON", L"添加",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)ID_BTN_ADD, g_hInstance, NULL
        );
        SendMessage(g_hBtnAdd, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // 自由速记多行文本框
        g_hEditMemo = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", g_appData.memoText.c_str(),
            WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)ID_EDIT_MEMO, g_hInstance, NULL
        );
        SendMessage(g_hEditMemo, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        UpdateControlVisibility();
        TrayManager::AddTrayIcon(hwnd);
        return 0;
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
            // 同步 Memo 文本
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

        if (id == ID_BTN_ADD && code == BN_CLICKED) {
            int len = GetWindowTextLengthW(g_hEditInput);
            if (len > 0) {
                std::vector<wchar_t> buf(len + 1);
                GetWindowTextW(g_hEditInput, buf.data(), len + 1);
                std::wstring text = buf.data();
                if (!text.empty()) {
                    TodoItem item;
                    item.id = (int)GetTickCount64();
                    item.text = text;
                    item.completed = false;
                    g_appData.todos.push_back(item);
                    SetWindowTextW(g_hEditInput, L"");
                    InvalidateRect(hwnd, NULL, FALSE);
                    SaveDataDeferred(hwnd);
                }
            }
            SetFocus(g_hEditInput);
            return 0;
        }

        if (id == ID_EDIT_MEMO && code == EN_CHANGE) {
            SaveDataDeferred(hwnd);
            return 0;
        }

        // 托盘菜单响应
        if (id == ID_TRAY_TOGGLE) {
            if (IsWindowVisible(hwnd)) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }
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

        if (id == ID_TRAY_NEW_TODO) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
            g_appData.currentTab = 0;
            UpdateControlVisibility();
            SetFocus(g_hEditInput);
            InvalidateRect(hwnd, NULL, FALSE);
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
            TrayManager::ShowContextMenu(hwnd, g_appData.isAlwaysOnTop);
        }
        return 0;
    }

    case WM_NCHITTEST: {
        // 自定义边框调整大小（8方向自由缩放）与无边框风格兼顾
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int border = 6;
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

        // 1. 顶部 Header 拖拽与按钮响应
        if (y < HEADER_HEIGHT) {
            // 置顶按钮区
            if (x >= width - 66 && x < width - 36) {
                g_appData.isAlwaysOnTop = !g_appData.isAlwaysOnTop;
                SetWindowPos(hwnd, g_appData.isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            // 最小化到托盘
            if (x >= width - 36 && x <= width - 8) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            // 移动窗口
            ReleaseCapture();
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        // 2. Tab 切换区
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

        // 3. Tab 0 列表项点击（勾选完成 / 删除项）
        if (g_appData.currentTab == 0) {
            int listStartY = HEADER_HEIGHT + TAB_HEIGHT + PROGRESS_HEIGHT + INPUT_AREA_HEIGHT + 8;
            if (y >= listStartY) {
                int clickedIdx = (y - listStartY + g_scrollOffset) / ITEM_HEIGHT;
                if (clickedIdx >= 0 && clickedIdx < (int)g_appData.todos.size()) {
                    // 判断是否点击了删除按钮
                    if (x >= width - PADDING - 26 && x <= width - PADDING - 4) {
                        g_appData.todos.erase(g_appData.todos.begin() + clickedIdx);
                        InvalidateRect(hwnd, NULL, FALSE);
                        SaveDataDeferred(hwnd);
                        return 0;
                    }
                    // 点击复选框或文字切换完成状态
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

        // 追踪鼠标离开窗口
        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        if (g_appData.currentTab == 0) {
            int listStartY = HEADER_HEIGHT + TAB_HEIGHT + PROGRESS_HEIGHT + INPUT_AREA_HEIGHT + 8;
            int prevHovered = g_hoveredItemIdx;
            int prevDel = g_hoveredDeleteIdx;

            if (y >= listStartY) {
                int hoverIdx = (y - listStartY + g_scrollOffset) / ITEM_HEIGHT;
                if (hoverIdx >= 0 && hoverIdx < (int)g_appData.todos.size()) {
                    g_hoveredItemIdx = hoverIdx;
                    if (x >= width - PADDING - 26 && x <= width - PADDING - 4) {
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

            if (prevHovered != g_hoveredItemIdx || prevDel != g_hoveredDeleteIdx) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_hoveredItemIdx = -1;
        g_hoveredDeleteIdx = -1;
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
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontBold) DeleteObject(g_hFontBold);
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

    // 单实例检查（防止多次打开多开图标冲突）
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\MyStickyNotes_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existingWnd = FindWindowW(L"MyStickyNotes_Main", NULL);
        if (existingWnd) {
            ShowWindow(existingWnd, SW_SHOW);
            SetForegroundWindow(existingWnd);
        }
        return 0;
    }

    // 初始化通用控件
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // 加载已保存的数据
    DataStore::Load(g_appData);

    // 1. 注册不可见的 Owner 窗口类（任务栏隐藏的关键底层机制）
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

    // 2. 注册主便签窗口类
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyStickyNotes_Main";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    // 使用 WS_EX_TOOLWINDOW 与 Owner 机制，彻底规避 Windows 任务栏图标
    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (g_appData.isAlwaysOnTop) {
        exStyle |= WS_EX_TOPMOST;
    }

    g_hMainWnd = CreateWindowExW(
        exStyle,
        L"MyStickyNotes_Main",
        L"My Sticky Notes",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        g_appData.windowX, g_appData.windowY,
        g_appData.windowWidth, g_appData.windowHeight,
        g_hOwnerWnd, // 关键：Owner 指向隐藏窗口
        NULL, hInstance, NULL
    );

    if (!g_hMainWnd) return 0;

    ShowWindow(g_hMainWnd, SW_SHOW);
    UpdateWindow(g_hMainWnd);

    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        // 回车快捷添加待办
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && msg.hwnd == g_hEditInput) {
            SendMessage(g_hMainWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_ADD, BN_CLICKED), (LPARAM)g_hBtnAdd);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    return (int)msg.wParam;
}
