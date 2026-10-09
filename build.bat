@echo off
windres src/resource.rc -O coff -o src/resource.o
if %ERRORLEVEL% NEQ 0 (
    echo Resource build failed!
    exit /b %ERRORLEVEL%
)

g++ -std=c++20 -O2 -mwindows -municode src/data_store.cpp src/tray.cpp src/main.cpp src/resource.o -o my-sticky-notes.exe -lcomctl32 -lgdi32 -luser32 -lshell32 -ladvapi32 -lshlwapi -lgdiplus -lole32 -ldwmapi -lcomdlg32
if %ERRORLEVEL% EQU 0 (
    echo Build succeeded: my-sticky-notes.exe
) else (
    echo Build failed!
)
