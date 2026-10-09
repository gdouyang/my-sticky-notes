#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum NoteColor {
    COLOR_YELLOW = 0, // 经典暖黄
    COLOR_GREEN  = 1, // 清新薄荷绿
    COLOR_PINK   = 2, // 浪漫柔粉
    COLOR_PURPLE = 3, // 优雅丁香紫
    COLOR_BLUE   = 4, // 宁静天空蓝
    COLOR_GRAY   = 5  // 低调炭灰
};

struct TodoItem {
    long long id = 0;
    std::wstring text;
    bool completed = false;
};

struct AppData {
    int windowX = 150;
    int windowY = 150;
    int windowWidth = 360;
    int windowHeight = 520;
    bool isAlwaysOnTop = true;
    int colorTheme = COLOR_YELLOW; // 主题色彩
    int currentTab = 0; // 0: 待办清单, 1: 随手速记
    std::string memoRtf; // 富文本 RTF 内容 (格式与排版持久化)
    std::vector<TodoItem> todos;
};

class DataStore {
public:
    static std::wstring GetDataFilePath();
    static std::wstring GetMemoFilePath();
    static bool Load(AppData& data);
    static bool Save(const AppData& data);
};
