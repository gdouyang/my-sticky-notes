@echo off
chcp 65001 >nul
echo 正在编译 MyStickyNotes...
g++ -std=c++20 -O2 -mwindows -municode src/data_store.cpp src/tray.cpp src/main.cpp -o my-sticky-notes.exe -lcomctl32 -lgdi32 -luser32 -lshell32 -ladvapi32 -lshlwapi

if %ERRORLEVEL% EQU 0 (
    echo 编译成功！已生成 my-sticky-notes.exe
) else (
    echo 编译失败！
)
