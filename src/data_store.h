#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct TodoItem {
    int id = 0;
    std::wstring text;
    bool completed = false;
};

struct AppData {
    int windowX = 100;
    int windowY = 100;
    int windowWidth = 360;
    int windowHeight = 520;
    bool isAlwaysOnTop = false;
    int currentTab = 0; // 0: 待办跟踪, 1: 自由速记
    std::wstring memoText;
    std::vector<TodoItem> todos;
};

class DataStore {
public:
    static std::wstring GetDataFilePath();
    static bool Load(AppData& data);
    static bool Save(const AppData& data);
};
