#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include "data_store.h"
#include "ui_theme.h"
#include "tray.h"

// 控件与交互常量
#define ID_EDIT_INPUT       1001
#define ID_RICH_MEMO        1003
#define ID_TIMER_SAVE       1004

// 格式工具栏按钮 ID
#define ID_BTN_BOLD         1101
#define ID_BTN_ITALIC       1102
#define ID_BTN_UNDERLINE    1103
#define ID_BTN_STRIKE       1104
#define ID_BTN_BULLET       1105

// 全局变量
static HINSTANCE g_hInstance = NULL;
static HWND g_hOwnerWnd = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hEditInput = NULL;
static HWND g_hRichMemo = NULL;
static WNDPROC g_oldEditInputProc = NULL;
static HMODULE g_hMsftEdit = NULL;

static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontStrike = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontFmt = NULL;

static AppData g_appData;
static int g_scrollOffset = 0;
static int g_hoveredItemIdx = -1;
static int g_hoveredDeleteIdx = -1;
static int g_hoveredHeaderBtn = 0; // 1: Pin, 2: Theme, 3: Close/Min
static int g_hoveredFmtBtn = 0;    // 1: B, 2: I, 3: U, 4: S, 5: Bullet

// 布局常量
static const int HEADER_HEIGHT = 44;
static const int TAB_HEIGHT = 38;
static const int PROGRESS_AREA_HEIGHT = 48;
static const int INPUT_AREA_HEIGHT = 44;
static const int ITEM_HEIGHT = 42;
static const int FORMAT_BAR_HEIGHT = 36;
static const int PADDING = 14;

// 函数声明
LRESULT CALLBACK OwnerWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditInputSubclassProc(HWND, UINT, WPARAM, LPARAM);
void SaveDataDeferred(HWND hwnd);
void UpdateControlVisibility();
void LayoutControls(int width, int height);
void ApplyFormatToSelection(HWND hRich, DWORD mask, DWORD effect);
void ToggleBullet(HWND hRich);
void SyncRichTextSave(HWND hwnd);
void LoadRichTextToControl(HWND hRich, const std::string& rtf);

// RTF 流入流出回调结构体
struct RtfStreamInCookie {
    const char* data;
    size_t offset;
    size_t length;
};

static DWORD CALLBACK EditStreamCallbackIn(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) {
    RtfStreamInCookie* p = (RtfStreamInCookie*)dwCookie;
    size_t left = p->length - p->offset;
    LONG toRead = (LONG)(cb < left ? cb : left);
    if (toRead > 0) {
        memcpy(pbBuff, p->data + p->offset, toRead);
        p->offset += toRead;
        *pcb = toRead;
    } else {
        *pcb = 0;
    }
    return 0;
}

static DWORD CALLBACK EditStreamCallbackOut(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb) {
    std::string* pStr = (std::string*)dwCookie;
    pStr->append((char*)pbBuff, cb);
    *pcb = cb;
    return 0;
}

void LoadRichTextToControl(HWND hRich, const std::string& rtf) {
    if (rtf.empty()) {
        SetWindowTextW(hRich, L"");
        return;
    }
    RtfStreamInCookie cookie = { rtf.data(), 0, rtf.size() };
    EDITSTREAM es = { (DWORD_PTR)&cookie, 0, EditStreamCallbackIn };
    SendMessageW(hRich, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

void SyncRichTextSave(HWND hwnd) {
    if (!g_hRichMemo) return;
    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, EditStreamCallbackOut };
    SendMessageW(g_hRichMemo, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    g_appData.memoRtf = rtf;
}

// 格式切换
void ApplyFormatToSelection(HWND hRich, DWORD mask, DWORD effect) {
    CHARFORMAT2W cf = { sizeof(cf) };
    cf.dwMask = mask;
    SendMessageW(hRich, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    
    // 切换状态
    if (cf.dwEffects & effect) {
        cf.dwEffects &= ~effect;
    } else {
        cf.dwEffects |= effect;
    }
    SendMessageW(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SetFocus(hRich);
    SaveDataDeferred(g_hMainWnd);
}

void ToggleBullet(HWND hRich) {
    PARAFORMAT2 pf = { sizeof(pf) };
    pf.dwMask = PFM_NUMBERING;
    SendMessageW(hRich, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    pf.dwMask = PFM_NUMBERING;
    if (pf.wNumbering == PFN_BULLET) {
        pf.wNumbering = 0;
    } else {
        pf.wNumbering = PFN_BULLET;
    }
    SendMessageW(hRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SetFocus(hRich);
    SaveDataDeferred(g_hMainWnd);
}

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

// 双缓冲渲染
static void Render(HWND hwnd, HDC hdc) {
    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    ColorScheme colors = GetThemeColors(g_appData.colorTheme);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

    // 1. 底色
    HBRUSH bodyBrush = CreateSolidBrush(colors.bodyBg);
    FillRect(memDC, &clientRc, bodyBrush);
    DeleteObject(bodyBrush);

    // 2. 顶部 Fluent Header
    RECT headerRc = { 0, 0, width, HEADER_HEIGHT };
    HBRUSH headerBrush = CreateSolidBrush(colors.headerBg);
    FillRect(memDC, &headerRc, headerBrush);
    DeleteObject(headerBrush);

    // 绘制标题
    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, g_hFontTitle);
    SetTextColor(memDC, colors.textPrimary);
    RECT titleRc = { PADDING, 0, width - 120, HEADER_HEIGHT };
    DrawTextW(memDC, L"便笺与进度", -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    // 顶部按钮区: [📌 置顶] [🎨 换色] [× 隐藏]
    int btnY = (HEADER_HEIGHT - 28) / 2;
    int btnH = 28;

    // 置顶
    RECT pinBtnRc = { width - 102, btnY, width - 72, btnY + btnH };
    if (g_hoveredHeaderBtn == 1 || g_appData.isAlwaysOnTop) {
        DrawModernRoundRect(memDC, pinBtnRc, 6, g_appData.isAlwaysOnTop ? colors.accent : colors.hoverOverlay, colors.cardBorder);
        SetTextColor(memDC, g_appData.isAlwaysOnTop ? RGB(255, 255, 255) : colors.textPrimary);
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    SelectObject(memDC, g_hFontNormal);
    DrawTextW(memDC, L"📌", -1, &pinBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 换色
    RECT themeBtnRc = { width - 68, btnY, width - 38, btnY + btnH };
    if (g_hoveredHeaderBtn == 2) {
        DrawModernRoundRect(memDC, themeBtnRc, 6, colors.hoverOverlay, colors.cardBorder);
    }
    SetTextColor(memDC, colors.textSecondary);
    DrawTextW(memDC, L"🎨", -1, &themeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 隐藏
    RECT closeBtnRc = { width - 34, btnY, width - 6, btnY + btnH };
    if (g_hoveredHeaderBtn == 3) {
        DrawModernRoundRect(memDC, closeBtnRc, 6, RGB(254, 226, 226), RGB(248, 113, 113));
        SetTextColor(memDC, RGB(220, 38, 38));
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"×", -1, &closeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 3. Tab 栏
    int tabY = HEADER_HEIGHT;
    int tabWidth = width / 2;
    RECT tab1Rc = { 0, tabY, tabWidth, tabY + TAB_HEIGHT };
    RECT tab2Rc = { tabWidth, tabY, width, tabY + TAB_HEIGHT };

    // Tab 1 (待办进度)
    if (g_appData.currentTab == 0) {
        RECT indRc = { tabWidth / 2 - 34, tabY + TAB_HEIGHT - 3, tabWidth / 2 + 34, tabY + TAB_HEIGHT };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, g_hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"待办清单", -1, &tab1Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 2 (速记笔记)
    if (g_appData.currentTab == 1) {
        RECT indRc = { tabWidth + tabWidth / 2 - 34, tabY + TAB_HEIGHT - 3, tabWidth + tabWidth / 2 + 34, tabY + TAB_HEIGHT };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, g_hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, g_hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"速记便笺", -1, &tab2Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 底部细分界线
    HPEN splitPen = CreatePen(PS_SOLID, 1, colors.cardBorder);
    HPEN oldPen = (HPEN)SelectObject(memDC, splitPen);
    MoveToEx(memDC, 0, tabY + TAB_HEIGHT - 1, NULL);
    LineTo(memDC, width, tabY + TAB_HEIGHT - 1);
    SelectObject(memDC, oldPen);
    DeleteObject(splitPen);

    // 4. Tab 0: 待办清单
    if (g_appData.currentTab == 0) {
        int compCount = 0, totalCount = 0;
        float percent = 0.0f;
        GetTodoStats(compCount, totalCount, percent);

        // 4.1 进度条区域卡片
        int progY = tabY + TAB_HEIGHT + 10;
        RECT progCardRc = { PADDING, progY, width - PADDING, progY + PROGRESS_AREA_HEIGHT };
        DrawModernRoundRect(memDC, progCardRc, 8, colors.cardBg, colors.cardBorder);

        RECT progTextRc = { PADDING + 12, progY + 6, width - PADDING - 12, progY + 24 };
        SelectObject(memDC, g_hFontSmall);
        SetTextColor(memDC, colors.textSecondary);

        wchar_t statText[64];
        swprintf_s(statText, L"完成进度：%d / %d 项 (%.0f%%)", compCount, totalCount, percent * 100.0f);
        DrawTextW(memDC, statText, -1, &progTextRc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        // 进度槽
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

        // 4.2 输入框外卡片
        int inputCardY = progY + PROGRESS_AREA_HEIGHT + 8;
        RECT inputCardRc = { PADDING, inputCardY, width - PADDING, inputCardY + 36 };
        DrawModernRoundRect(memDC, inputCardRc, 8, colors.cardBg, colors.cardBorder);

        // 4.3 待办事项列表
        int listStartY = inputCardY + 36 + 10;
        int listEndY = height - PADDING;
        int currentY = listStartY - g_scrollOffset;

        if (g_appData.todos.empty()) {
            RECT emptyRc = { 0, listStartY + 30, width, listStartY + 100 };
            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, colors.textSecondary);
            DrawTextW(memDC, L"✨ 暂无待办，输入后按回车直接添加", -1, &emptyRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            for (size_t i = 0; i < g_appData.todos.size(); ++i) {
                if (currentY + ITEM_HEIGHT > listStartY && currentY < listEndY) {
                    RECT itemRc = { PADDING, currentY, width - PADDING, currentY + ITEM_HEIGHT - 4 };

                    bool isHovered = ((int)i == g_hoveredItemIdx);
                    COLORREF itemBg = isHovered ? colors.hoverOverlay : colors.cardBg;
                    DrawModernRoundRect(memDC, itemRc, 8, itemBg, colors.cardBorder);

                    // 圆形复选框
                    int boxSize = 18;
                    int boxX = PADDING + 12;
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

                    // 待办文本
                    RECT textRc = { boxX + boxSize + 12, currentY, width - PADDING - 36, currentY + ITEM_HEIGHT - 4 };
                    if (g_appData.todos[i].completed) {
                        SelectObject(memDC, g_hFontStrike);
                        SetTextColor(memDC, colors.completedText);
                    } else {
                        SelectObject(memDC, g_hFontNormal);
                        SetTextColor(memDC, colors.textPrimary);
                    }
                    DrawTextW(memDC, g_appData.todos[i].text.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    // 悬浮删除按钮
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

    // 5. Tab 1: 速记笔记格式工具栏渲染 (位于底端，完美对齐微软便签经典底部工具栏)
    if (g_appData.currentTab == 1) {
        int fmtY = height - FORMAT_BAR_HEIGHT;
        RECT fmtBarRc = { 0, fmtY, width, height };

        HBRUSH fmtBrush = CreateSolidBrush(colors.headerBg);
        FillRect(memDC, &fmtBarRc, fmtBrush);
        DeleteObject(fmtBrush);

        HPEN fmtPen = CreatePen(PS_SOLID, 1, colors.cardBorder);
        HPEN prevFmtPen = (HPEN)SelectObject(memDC, fmtPen);
        MoveToEx(memDC, 0, fmtY, NULL);
        LineTo(memDC, width, fmtY);
        SelectObject(memDC, prevFmtPen);
        DeleteObject(fmtPen);

        // 5 个工具按钮: [B 粗体] [I 斜体] [U 下划线] [S 删除线] [• 列表]
        const wchar_t* btnLabels[] = { L"B", L"I", L"U", L"S", L"•" };
        int btnW = 34;
        int btnPad = 6;
        int startX = PADDING;

        for (int b = 1; b <= 5; ++b) {
            RECT bRc = { startX, fmtY + 4, startX + btnW, height - 4 };
            if (g_hoveredFmtBtn == b) {
                DrawModernRoundRect(memDC, bRc, 6, colors.hoverOverlay, colors.cardBorder);
            }
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, colors.textPrimary);
            SelectObject(memDC, g_hFontFmt);
            DrawTextW(memDC, btnLabels[b - 1], -1, &bRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            startX += btnW + btnPad;
        }
    }

    // 6. 最外圈轻量精致边框
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
        ShowWindow(g_hRichMemo, SW_HIDE);
    } else {
        ShowWindow(g_hEditInput, SW_HIDE);
        ShowWindow(g_hRichMemo, SW_SHOW);
    }
}

// 控件布局适配
void LayoutControls(int width, int height) {
    int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 10 + PROGRESS_AREA_HEIGHT + 8;
    MoveWindow(g_hEditInput, PADDING + 12, inputCardY + 6, width - PADDING * 2 - 24, 24, TRUE);

    // 富文本 RichEdit (留出顶部 Tab 和底部格式栏的高度)
    int memoY = HEADER_HEIGHT + TAB_HEIGHT + 6;
    int memoH = height - memoY - FORMAT_BAR_HEIGHT - 6;
    MoveWindow(g_hRichMemo, PADDING, memoY, width - PADDING * 2, memoH, TRUE);
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

// 主窗口 WndProc
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hFontTitle = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");
        if (!g_hFontTitle) {
            g_hFontTitle = CreateFontW(17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        }

        g_hFontNormal = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontStrike = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, TRUE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

        g_hFontFmt = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

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

        // 设置 RichEdit 边距与颜色
        RECT marginsRc = { 4, 4, 4, 4 };
        SendMessage(g_hRichMemo, EM_SETRECT, 0, (LPARAM)&marginsRc);

        ColorScheme colors = GetThemeColors(g_appData.colorTheme);
        SendMessage(g_hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);

        // 加载 RTF 格式
        LoadRichTextToControl(g_hRichMemo, g_appData.memoRtf);

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
            SyncRichTextSave(hwnd);
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
        int height = rc.bottom - rc.top;

        // 1. 顶部 Header
        if (y < HEADER_HEIGHT) {
            int btnY = (HEADER_HEIGHT - 28) / 2;
            int btnH = 28;

            // 置顶
            if (x >= width - 102 && x < width - 72 && y >= btnY && y <= btnY + btnH) {
                g_appData.isAlwaysOnTop = !g_appData.isAlwaysOnTop;
                SetWindowPos(hwnd, g_appData.isAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                InvalidateRect(hwnd, NULL, FALSE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            // 切换主题色
            if (x >= width - 68 && x < width - 38 && y >= btnY && y <= btnY + btnH) {
                g_appData.colorTheme = (g_appData.colorTheme + 1) % 6;
                ColorScheme colors = GetThemeColors(g_appData.colorTheme);
                SendMessage(g_hRichMemo, EM_SETBKGNDCOLOR, 0, (LPARAM)colors.bodyBg);
                TrayManager::UpdateTrayIcon(hwnd, g_appData.colorTheme);
                InvalidateRect(hwnd, NULL, TRUE);
                SaveDataDeferred(hwnd);
                return 0;
            }
            // 隐藏到托盘
            if (x >= width - 34 && x <= width - 6 && y >= btnY && y <= btnY + btnH) {
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
            int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 10 + PROGRESS_AREA_HEIGHT + 8;
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
                    // 勾选切换
                    g_appData.todos[clickedIdx].completed = !g_appData.todos[clickedIdx].completed;
                    InvalidateRect(hwnd, NULL, FALSE);
                    SaveDataDeferred(hwnd);
                    return 0;
                }
            }
        }

        // 4. Tab 1 富文本底部工具栏点击响应
        if (g_appData.currentTab == 1 && y >= height - FORMAT_BAR_HEIGHT) {
            int btnW = 34;
            int btnPad = 6;
            int startX = PADDING;

            for (int b = 1; b <= 5; ++b) {
                if (x >= startX && x <= startX + btnW) {
                    if (b == 1) ApplyFormatToSelection(g_hRichMemo, CFM_BOLD, CFE_BOLD);
                    else if (b == 2) ApplyFormatToSelection(g_hRichMemo, CFM_ITALIC, CFE_ITALIC);
                    else if (b == 3) ApplyFormatToSelection(g_hRichMemo, CFM_UNDERLINE, CFE_UNDERLINE);
                    else if (b == 4) ApplyFormatToSelection(g_hRichMemo, CFM_STRIKEOUT, CFE_STRIKEOUT);
                    else if (b == 5) ToggleBullet(g_hRichMemo);
                    return 0;
                }
                startX += btnW + btnPad;
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

        TRACKMOUSEEVENT tme = { sizeof(tme) };
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);

        int prevHeaderBtn = g_hoveredHeaderBtn;
        g_hoveredHeaderBtn = 0;
        if (y < HEADER_HEIGHT) {
            int btnY = (HEADER_HEIGHT - 28) / 2;
            int btnH = 28;
            if (y >= btnY && y <= btnY + btnH) {
                if (x >= width - 102 && x < width - 72) g_hoveredHeaderBtn = 1;
                else if (x >= width - 68 && x < width - 38) g_hoveredHeaderBtn = 2;
                else if (x >= width - 34 && x <= width - 6) g_hoveredHeaderBtn = 3;
            }
        }

        int prevFmtBtn = g_hoveredFmtBtn;
        g_hoveredFmtBtn = 0;
        if (g_appData.currentTab == 1 && y >= height - FORMAT_BAR_HEIGHT) {
            int btnW = 34;
            int btnPad = 6;
            int startX = PADDING;
            for (int b = 1; b <= 5; ++b) {
                if (x >= startX && x <= startX + btnW) {
                    g_hoveredFmtBtn = b;
                    break;
                }
                startX += btnW + btnPad;
            }
        }

        if (g_appData.currentTab == 0) {
            int inputCardY = HEADER_HEIGHT + TAB_HEIGHT + 10 + PROGRESS_AREA_HEIGHT + 8;
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
            if (prevHeaderBtn != g_hoveredHeaderBtn || prevFmtBtn != g_hoveredFmtBtn) {
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_hoveredItemIdx = -1;
        g_hoveredDeleteIdx = -1;
        g_hoveredHeaderBtn = 0;
        g_hoveredFmtBtn = 0;
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
        SyncRichTextSave(hwnd);
        DataStore::Save(g_appData);
        TrayManager::RemoveTrayIcon(hwnd);
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontNormal) DeleteObject(g_hFontNormal);
        if (g_hFontStrike) DeleteObject(g_hFontStrike);
        if (g_hFontSmall) DeleteObject(g_hFontSmall);
        if (g_hFontFmt) DeleteObject(g_hFontFmt);
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

    // 加载 Windows 高级富文本支持库
    g_hMsftEdit = LoadLibraryW(L"msftedit.dll");

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\MyStickyNotes_SingleInstance_Mutex_v3");
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

    // 1. Owner Window
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

    // 2. Main Window
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
    return (int)msg.wParam;
}
