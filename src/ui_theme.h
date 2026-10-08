#pragma once
#include <windows.h>

namespace Theme {
    // 经典暖黄极简护眼便签配色
    constexpr COLORREF BG_COLOR           = RGB(254, 252, 232); // 浅淡柔和暖黄底色 #FEFCE8
    constexpr COLORREF HEADER_BG          = RGB(250, 245, 205); // 顶部栏稍深暖黄
    constexpr COLORREF BORDER_COLOR       = RGB(225, 215, 170); // 柔和外边框
    constexpr COLORREF TEXT_MAIN          = RGB(35, 35, 35);    // 主文字深灰黑
    constexpr COLORREF TEXT_MUTED         = RGB(120, 115, 105); // 次要文字灰
    constexpr COLORREF ACCENT_GREEN       = RGB(34, 197, 94);   // 进度条健康绿 #22C55E
    constexpr COLORREF PROGRESS_BG        = RGB(230, 225, 200); // 进度条底槽色
    constexpr COLORREF ITEM_HOVER         = RGB(248, 243, 218); // 悬浮项底色
    constexpr COLORREF COMPLETED_TEXT     = RGB(160, 155, 145); // 已完成删除线字体色
    constexpr COLORREF BUTTON_HOVER       = RGB(238, 230, 190); // 按钮悬浮色
    constexpr COLORREF CHECKBOX_BORDER    = RGB(180, 170, 140); // 复选框边框
}
