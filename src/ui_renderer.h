#pragma once
#include <windows.h>
#include <string>
#include "data_store.h"
#include "dpi_layout.h"
#include "ui_theme.h"

// 绘制平滑圆角矩形
inline void DrawModernRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF fillColor, COLORREF borderColor) {
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

// 统计待办进度
inline void GetTodoStats(const NoteCardData& appData, int& completed, int& total, float& percent) {
    total = (int)appData.todos.size();
    completed = 0;
    for (const auto& item : appData.todos) {
        if (item.completed) completed++;
    }
    percent = (total == 0) ? 0.0f : ((float)completed / (float)total);
}

// 绘制主界面各个区域与卡片
inline void RenderView(
    HDC memDC,
    const RECT& clientRc,
    const NoteCardData& appData,
    int scrollOffset,
    int hoveredItemIdx,
    int hoveredDeleteIdx,
    int hoveredHeaderBtn,
    int hoveredFmtBtn,
    HFONT hFontTitle,
    HFONT hFontNormal,
    HFONT hFontStrike,
    HFONT hFontSmall,
    HFONT hFontFmtB,
    HFONT hFontFmtI,
    HFONT hFontFmtU,
    HFONT hFontFmtAB,
    UINT dpi
) {
    int width = clientRc.right - clientRc.left;
    int height = clientRc.bottom - clientRc.top;

    LayoutMetrics m = GetMetrics(dpi);
    ColorScheme colors = GetThemeColors(appData.colorTheme);

    // 1. 底色
    HBRUSH bodyBrush = CreateSolidBrush(colors.bodyBg);
    FillRect(memDC, &clientRc, bodyBrush);
    DeleteObject(bodyBrush);

    // 2. 顶部 Fluent Header
    RECT headerRc = { 0, 0, width, m.headerH };
    HBRUSH headerBrush = CreateSolidBrush(colors.headerBg);
    FillRect(memDC, &headerRc, headerBrush);
    DeleteObject(headerBrush);

    // 绘制标题
    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, hFontTitle);
    SetTextColor(memDC, colors.textPrimary);
    RECT titleRc = { m.padding, 0, width - ScaleDpi(145, dpi), m.headerH };
    DrawTextW(memDC, L"便笺与进度", -1, &titleRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    // 顶部按钮区: [+] [📌 置顶] [🎨 换色] [× 隐藏/关闭]
    int btnRadius = ScaleDpi(6, dpi);

    // 1. 新建便签 [+]
    RECT addBtnRc = GetHeaderBtnRect(width, 1, dpi);
    if (hoveredHeaderBtn == 1) {
        DrawModernRoundRect(memDC, addBtnRc, btnRadius, colors.hoverOverlay, colors.cardBorder);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    SelectObject(memDC, hFontTitle);
    DrawTextW(memDC, L"+", -1, &addBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 2. 置顶 [📌]
    RECT pinBtnRc = GetHeaderBtnRect(width, 2, dpi);
    if (hoveredHeaderBtn == 2 || appData.isAlwaysOnTop) {
        DrawModernRoundRect(memDC, pinBtnRc, btnRadius, appData.isAlwaysOnTop ? colors.accent : colors.hoverOverlay, colors.cardBorder);
        SetTextColor(memDC, appData.isAlwaysOnTop ? RGB(255, 255, 255) : colors.textPrimary);
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    SelectObject(memDC, hFontNormal);
    DrawTextW(memDC, L"📌", -1, &pinBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 3. 换色 [🎨]
    RECT themeBtnRc = GetHeaderBtnRect(width, 3, dpi);
    if (hoveredHeaderBtn == 3) {
        DrawModernRoundRect(memDC, themeBtnRc, btnRadius, colors.hoverOverlay, colors.cardBorder);
    }
    SetTextColor(memDC, colors.textSecondary);
    DrawTextW(memDC, L"🎨", -1, &themeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 4. 关闭/隐藏 [×]
    RECT closeBtnRc = GetHeaderBtnRect(width, 4, dpi);
    if (hoveredHeaderBtn == 4) {
        DrawModernRoundRect(memDC, closeBtnRc, btnRadius, RGB(254, 226, 226), RGB(248, 113, 113));
        SetTextColor(memDC, RGB(220, 38, 38));
    } else {
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"×", -1, &closeBtnRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // 3. Tab 栏
    int tabY = m.headerH;
    int tabWidth = width / 2;
    RECT tab1Rc = { 0, tabY, tabWidth, tabY + m.tabH };
    RECT tab2Rc = { tabWidth, tabY, width, tabY + m.tabH };

    int indHalfW = ScaleDpi(34, dpi);
    int indH = ScaleDpi(3, dpi);

    // Tab 1 (待办进度)
    if (appData.currentTab == 0) {
        RECT indRc = { tabWidth / 2 - indHalfW, tabY + m.tabH - indH, tabWidth / 2 + indHalfW, tabY + m.tabH };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"待办清单", -1, &tab1Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 2 (速记笔记)
    if (appData.currentTab == 1) {
        RECT indRc = { tabWidth + tabWidth / 2 - indHalfW, tabY + m.tabH - indH, tabWidth + tabWidth / 2 + indHalfW, tabY + m.tabH };
        HBRUSH indBrush = CreateSolidBrush(colors.accent);
        FillRect(memDC, &indRc, indBrush);
        DeleteObject(indBrush);

        SelectObject(memDC, hFontTitle);
        SetTextColor(memDC, colors.textPrimary);
    } else {
        SelectObject(memDC, hFontNormal);
        SetTextColor(memDC, colors.textSecondary);
    }
    DrawTextW(memDC, L"速记便笺", -1, &tab2Rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Tab 底部细分界线
    HPEN splitPen = CreatePen(PS_SOLID, 1, colors.cardBorder);
    HPEN oldPen = (HPEN)SelectObject(memDC, splitPen);
    MoveToEx(memDC, 0, tabY + m.tabH - 1, NULL);
    LineTo(memDC, width, tabY + m.tabH - 1);
    SelectObject(memDC, oldPen);
    DeleteObject(splitPen);

    // 4. Tab 0: 待办清单
    if (appData.currentTab == 0) {
        int compCount = 0, totalCount = 0;
        float percent = 0.0f;
        GetTodoStats(appData, compCount, totalCount, percent);

        // 4.1 进度条区域卡片
        int progY = tabY + m.tabH + ScaleDpi(10, dpi);
        RECT progCardRc = { m.padding, progY, width - m.padding, progY + m.progressH };
        DrawModernRoundRect(memDC, progCardRc, ScaleDpi(8, dpi), colors.cardBg, colors.cardBorder);

        RECT progTextRc = { m.padding + ScaleDpi(12, dpi), progY + ScaleDpi(6, dpi), width - m.padding - ScaleDpi(12, dpi), progY + ScaleDpi(24, dpi) };
        SelectObject(memDC, hFontSmall);
        SetTextColor(memDC, colors.textSecondary);

        wchar_t statText[64];
        swprintf_s(statText, L"完成进度：%d / %d 项 (%.0f%%)", compCount, totalCount, percent * 100.0f);
        DrawTextW(memDC, statText, -1, &progTextRc, DT_SINGLELINE | DT_LEFT | DT_VCENTER);

        // 进度槽
        int barY = progY + ScaleDpi(28, dpi);
        int barH = ScaleDpi(8, dpi);
        int barW = width - m.padding * 2 - ScaleDpi(24, dpi);
        RECT barBgRc = { m.padding + ScaleDpi(12, dpi), barY, m.padding + ScaleDpi(12, dpi) + barW, barY + barH };
        DrawModernRoundRect(memDC, barBgRc, ScaleDpi(4, dpi), colors.progressBg, colors.progressBg);

        int fillW = (int)(barW * percent);
        if (fillW > 0) {
            RECT barFillRc = { m.padding + ScaleDpi(12, dpi), barY, m.padding + ScaleDpi(12, dpi) + fillW, barY + barH };
            DrawModernRoundRect(memDC, barFillRc, ScaleDpi(4, dpi), colors.accent, colors.accent);
        }

        // 4.2 输入框外卡片
        int inputCardY = progY + m.progressH + ScaleDpi(8, dpi);
        RECT inputCardRc = { m.padding, inputCardY, width - m.padding, inputCardY + m.inputCardH };
        DrawModernRoundRect(memDC, inputCardRc, ScaleDpi(8, dpi), colors.cardBg, colors.cardBorder);

        // 4.3 待办事项列表
        int listStartY = inputCardY + m.inputCardH + ScaleDpi(10, dpi);
        int listEndY = height - m.padding;
        int currentY = listStartY - scrollOffset;

        if (appData.todos.empty()) {
            RECT emptyRc = { 0, listStartY + ScaleDpi(30, dpi), width, listStartY + ScaleDpi(100, dpi) };
            SelectObject(memDC, hFontNormal);
            SetTextColor(memDC, colors.textSecondary);
            DrawTextW(memDC, L"✨ 暂无待办，输入后按回车直接添加", -1, &emptyRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        } else {
            for (size_t i = 0; i < appData.todos.size(); ++i) {
                if (currentY + m.itemH > listStartY && currentY < listEndY) {
                    RECT itemRc = { m.padding, currentY, width - m.padding, currentY + m.itemH - ScaleDpi(4, dpi) };

                    bool isHovered = ((int)i == hoveredItemIdx);
                    COLORREF itemBg = isHovered ? colors.hoverOverlay : colors.cardBg;
                    DrawModernRoundRect(memDC, itemRc, ScaleDpi(8, dpi), itemBg, colors.cardBorder);

                    // 圆形复选框
                    int boxSize = ScaleDpi(18, dpi);
                    int boxX = m.padding + ScaleDpi(12, dpi);
                    int boxY = currentY + (m.itemH - ScaleDpi(4, dpi) - boxSize) / 2;
                    RECT checkRc = { boxX, boxY, boxX + boxSize, boxY + boxSize };

                    if (appData.todos[i].completed) {
                        DrawModernRoundRect(memDC, checkRc, boxSize, colors.accent, colors.accent);
                        SetTextColor(memDC, RGB(255, 255, 255));
                        SelectObject(memDC, hFontSmall);
                        DrawTextW(memDC, L"✓", -1, &checkRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    } else {
                        DrawModernRoundRect(memDC, checkRc, boxSize, colors.cardBg, colors.cardBorder);
                    }

                    // 待办文本
                    int delSize = ScaleDpi(22, dpi);
                    int delX = width - m.padding - ScaleDpi(30, dpi);
                    RECT textRc = { boxX + boxSize + ScaleDpi(12, dpi), currentY, delX - ScaleDpi(6, dpi), currentY + m.itemH - ScaleDpi(4, dpi) };
                    if (appData.todos[i].completed) {
                        SelectObject(memDC, hFontStrike);
                        SetTextColor(memDC, colors.completedText);
                    } else {
                        SelectObject(memDC, hFontNormal);
                        SetTextColor(memDC, colors.textPrimary);
                    }
                    DrawTextW(memDC, appData.todos[i].text.c_str(), -1, &textRc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

                    // 悬浮删除按钮
                    if (isHovered) {
                        int delY = currentY + (m.itemH - ScaleDpi(4, dpi) - delSize) / 2;
                        RECT delRc = { delX, delY, delX + delSize, delY + delSize };

                        bool isDelHover = ((int)i == hoveredDeleteIdx);
                        if (isDelHover) {
                            DrawModernRoundRect(memDC, delRc, ScaleDpi(6, dpi), RGB(254, 226, 226), RGB(248, 113, 113));
                            SetTextColor(memDC, RGB(220, 38, 38));
                        } else {
                            SetTextColor(memDC, colors.textSecondary);
                        }
                        SelectObject(memDC, hFontNormal);
                        DrawTextW(memDC, L"×", -1, &delRc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                    }
                }
                currentY += m.itemH;
            }
        }
    }

    // 5. Tab 1: 速记笔记格式工具栏渲染 (1:1 像素级复刻微软便签经典底部工具栏)
    if (appData.currentTab == 1) {
        int fmtY = height - m.formatBarH;
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

        for (int b = 1; b <= 7; ++b) {
            RECT bRc = GetFormatBtnRect(width, height, b, dpi);
            if (hoveredFmtBtn == b) {
                DrawModernRoundRect(memDC, bRc, ScaleDpi(5, dpi), colors.hoverOverlay, colors.cardBorder);
            }
            int cx = (bRc.left + bRc.right) / 2;
            int cy = (bRc.top + bRc.bottom) / 2;
            SetBkMode(memDC, TRANSPARENT);

            if (b == 1) {
                // 加粗 (B)
                SetTextColor(memDC, colors.textPrimary);
                SelectObject(memDC, hFontFmtB);
                RECT r = bRc;
                DrawTextW(memDC, L"B", 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            } else if (b == 2) {
                // 斜体 (I - 衬线斜体)
                SetTextColor(memDC, colors.textPrimary);
                SelectObject(memDC, hFontFmtI);
                RECT r = bRc;
                DrawTextW(memDC, L"I", 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            } else if (b == 3) {
                // 下划线 (U 带清晰底横线)
                SetTextColor(memDC, colors.textPrimary);
                SelectObject(memDC, hFontFmtU);
                RECT r = bRc;
                r.bottom -= ScaleDpi(4, dpi);
                DrawTextW(memDC, L"U", 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                int penThick = ScaleDpi(2, dpi); if (penThick < 1) penThick = 1;
                HPEN pen = CreatePen(PS_SOLID, penThick, colors.textPrimary);
                HGDIOBJ oldP = SelectObject(memDC, pen);
                int lineY = cy + ScaleDpi(8, dpi);
                int lineHalfW = ScaleDpi(6, dpi);
                MoveToEx(memDC, cx - lineHalfW, lineY, NULL);
                LineTo(memDC, cx + lineHalfW + 1, lineY);
                SelectObject(memDC, oldP);
                DeleteObject(pen);
            } else if (b == 4) {
                // 删除线 (ab 中间贯穿横线)
                SetTextColor(memDC, colors.textPrimary);
                SelectObject(memDC, hFontFmtAB);
                RECT r = bRc;
                r.bottom -= ScaleDpi(1, dpi);
                DrawTextW(memDC, L"ab", 2, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                int penThick = ScaleDpi(2, dpi); if (penThick < 1) penThick = 1;
                HPEN pen = CreatePen(PS_SOLID, penThick, colors.textPrimary);
                HGDIOBJ oldP = SelectObject(memDC, pen);
                int lineY = cy + ScaleDpi(2, dpi);
                int lineHalfW = ScaleDpi(9, dpi);
                MoveToEx(memDC, cx - lineHalfW, lineY, NULL);
                LineTo(memDC, cx + lineHalfW + 1, lineY);
                SelectObject(memDC, oldP);
                DeleteObject(pen);
            } else if (b == 5) {
                // 项目符号列表 (3组 圆点+横线)
                HBRUSH dotBrush = CreateSolidBrush(colors.textPrimary);
                int lineThick = ScaleDpi(2, dpi); if (lineThick < 1) lineThick = 1;
                HPEN linePen = CreatePen(PS_SOLID, lineThick, colors.textPrimary);
                HGDIOBJ oldP = SelectObject(memDC, linePen);
                int dotW = ScaleDpi(3, dpi); if (dotW < 2) dotW = 2;
                int dotLeft = cx - ScaleDpi(9, dpi);
                int lineLeft = cx - ScaleDpi(3, dpi);
                int lineRight = cx + ScaleDpi(9, dpi);
                int spacing = ScaleDpi(5, dpi);
                int rowYs[3] = { cy - spacing, cy, cy + spacing };
                for (int i = 0; i < 3; ++i) {
                    RECT dotRc = { dotLeft, rowYs[i] - dotW / 2, dotLeft + dotW, rowYs[i] + dotW / 2 + 1 };
                    FillRect(memDC, &dotRc, dotBrush);
                    MoveToEx(memDC, lineLeft, rowYs[i], NULL);
                    LineTo(memDC, lineRight, rowYs[i]);
                }
                SelectObject(memDC, oldP);
                DeleteObject(linePen);
                DeleteObject(dotBrush);
            } else if (b == 6) {
                // 荧光高亮笔 (A 字母底下带荧光黄色粗底条)
                SetTextColor(memDC, colors.textPrimary);
                SelectObject(memDC, hFontFmtB);
                RECT r = bRc;
                r.bottom -= ScaleDpi(5, dpi);
                DrawTextW(memDC, L"A", 1, &r, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

                int barH = ScaleDpi(3, dpi); if (barH < 2) barH = 2;
                int barW = ScaleDpi(14, dpi);
                int barY = cy + ScaleDpi(6, dpi);
                RECT hlRc = { cx - barW / 2, barY, cx + barW / 2, barY + barH };
                HBRUSH hlBrush = CreateSolidBrush(RGB(253, 224, 71));
                FillRect(memDC, &hlRc, hlBrush);
                DeleteObject(hlBrush);
            } else if (b == 7) {
                // 添加图像 (精致相框 + 山峰)
                int fHalfW = ScaleDpi(8, dpi);
                int fHalfH = ScaleDpi(6, dpi);
                int fL = cx - fHalfW, fT = cy - fHalfH, fR = cx + fHalfW, fB = cy + fHalfH;
                HPEN framePen = CreatePen(PS_SOLID, 1, colors.textPrimary);
                HGDIOBJ oldP = SelectObject(memDC, framePen);
                HGDIOBJ oldB = SelectObject(memDC, GetStockObject(HOLLOW_BRUSH));
                RoundRect(memDC, fL, fT, fR, fB, ScaleDpi(3, dpi), ScaleDpi(3, dpi));

                // 左侧大山峰
                MoveToEx(memDC, fL + ScaleDpi(2, dpi), fB - 1, NULL);
                LineTo(memDC, fL + ScaleDpi(6, dpi), fT + ScaleDpi(4, dpi));
                LineTo(memDC, fL + ScaleDpi(10, dpi), fB - 1);
                // 右侧小山峰
                MoveToEx(memDC, fL + ScaleDpi(8, dpi), fB - 1, NULL);
                LineTo(memDC, fL + ScaleDpi(11, dpi), fT + ScaleDpi(6, dpi));
                LineTo(memDC, fR - ScaleDpi(2, dpi), fB - 1);

                SelectObject(memDC, oldB);
                SelectObject(memDC, oldP);
                DeleteObject(framePen);
            }
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
}
