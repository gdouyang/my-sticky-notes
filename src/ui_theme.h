#pragma once
#include <windows.h>
#include "data_store.h"

struct ColorScheme {
    COLORREF headerBg;       // 顶部 Fluent 导航栏
    COLORREF bodyBg;         // 内容底色
    COLORREF cardBg;         // 待办卡片/输入框底色
    COLORREF cardBorder;     // 柔和微边框
    COLORREF textPrimary;    // 主标题与内容字
    COLORREF textSecondary;  // 辅助次要文字
    COLORREF accent;         // 品牌强调色（绿色/主题色）
    COLORREF progressBg;     // 进度条底层底槽
    COLORREF hoverOverlay;   // 悬浮时微高亮
    COLORREF completedText;  // 完成项删除线文字色
};

inline ColorScheme GetThemeColors(int theme) {
    switch (theme) {
    case COLOR_GREEN:
        // 薄荷绿主题 (Mint)
        return {
            RGB(226, 246, 230), RGB(238, 252, 241), RGB(255, 255, 255),
            RGB(198, 235, 207), RGB(25, 45, 30),   RGB(105, 130, 110),
            RGB(16, 185, 129),  RGB(212, 240, 220), RGB(220, 245, 226),
            RGB(140, 165, 145)
        };
    case COLOR_PINK:
        // 柔粉主题 (Peach / Pink)
        return {
            RGB(254, 232, 239), RGB(255, 242, 246), RGB(255, 255, 255),
            RGB(248, 205, 220), RGB(50, 25, 35),   RGB(145, 100, 115),
            RGB(236, 72, 153),  RGB(250, 220, 232), RGB(252, 228, 238),
            RGB(170, 135, 148)
        };
    case COLOR_PURPLE:
        // 丁香紫主题 (Lilac)
        return {
            RGB(240, 235, 255), RGB(247, 244, 255), RGB(255, 255, 255),
            RGB(224, 215, 250), RGB(40, 30, 60),   RGB(125, 110, 150),
            RGB(147, 51, 234),  RGB(232, 224, 252), RGB(238, 230, 255),
            RGB(155, 145, 175)
        };
    case COLOR_BLUE:
        // 宁静蓝主题 (Sky Blue)
        return {
            RGB(228, 242, 255), RGB(240, 248, 255), RGB(255, 255, 255),
            RGB(195, 225, 255), RGB(20, 35, 60),   RGB(95, 120, 150),
            RGB(37, 99, 235),   RGB(215, 235, 255), RGB(225, 240, 255),
            RGB(135, 155, 175)
        };
    case COLOR_GRAY:
        // 低调炭灰暗色主题 (Charcoal Dark)
        return {
            RGB(45, 45, 48),    RGB(32, 32, 34),    RGB(50, 50, 54),
            RGB(65, 65, 70),    RGB(240, 240, 240), RGB(160, 160, 165),
            RGB(59, 130, 246),  RGB(60, 60, 65),    RGB(60, 60, 65),
            RGB(120, 120, 125)
        };
    case COLOR_YELLOW:
    default:
        // 微软便笺经典金黄色 (Sticky Classic Yellow: #FFF7D6 / #FFF1B0)
        return {
            RGB(255, 241, 176), // header
            RGB(255, 247, 214), // body
            RGB(255, 255, 255), // card
            RGB(240, 225, 160), // border
            RGB(30, 30, 30),    // textPrimary
            RGB(110, 100, 80),  // textSecondary
            RGB(16, 185, 129),  // progress green (#10B981)
            RGB(240, 226, 170), // progress track
            RGB(250, 236, 170), // hover
            RGB(150, 140, 120)  // completed
        };
    }
}
