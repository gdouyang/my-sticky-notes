#pragma once
#include <windows.h>
#include <richedit.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <string>

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

inline void LoadRichTextToControl(HWND hRich, const std::string& rtf) {
    if (rtf.empty()) {
        SetWindowTextW(hRich, L"");
        return;
    }
    RtfStreamInCookie cookie = { rtf.data(), 0, rtf.size() };
    EDITSTREAM es = { (DWORD_PTR)&cookie, 0, EditStreamCallbackIn };
    SendMessageW(hRich, EM_STREAMIN, SF_RTF, (LPARAM)&es);
}

inline std::string GetRichTextFromControl(HWND hRich) {
    if (!hRich) return "";
    std::string rtf;
    EDITSTREAM es = { (DWORD_PTR)&rtf, 0, EditStreamCallbackOut };
    SendMessageW(hRich, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    return rtf;
}

// 格式切换
inline void ApplyFormatToSelection(HWND hRich, DWORD mask, DWORD effect) {
    CHARFORMAT2W cf = { sizeof(cf) };
    cf.dwMask = mask;
    SendMessageW(hRich, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    
    if (cf.dwEffects & effect) {
        cf.dwEffects &= ~effect;
    } else {
        cf.dwEffects |= effect;
    }
    SendMessageW(hRich, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SetFocus(hRich);
}

// 方案1：项目符号前置留出约2个空格的缩进 (dxStartIndent = 360 twips, dxOffset = 240 twips)
inline void ToggleBullet(HWND hRich) {
    PARAFORMAT2 pf = { sizeof(pf) };
    pf.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET;
    SendMessageW(hRich, EM_GETPARAFORMAT, 0, (LPARAM)&pf);

    if (pf.wNumbering == PFN_BULLET) {
        // 取消项目符号，恢复段落缩进
        pf.wNumbering = 0;
        pf.dxStartIndent = 0;
        pf.dxOffset = 0;
    } else {
        // 开启项目符号：段落起始缩进 360 twips（约 2~3 个半角空格），圆点与正文间隔 240 twips
        pf.wNumbering = PFN_BULLET;
        pf.dxStartIndent = 360;
        pf.dxOffset = 240;
    }
    SendMessageW(hRich, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SetFocus(hRich);
}

// 插入图片到富文本笔记
inline void InsertImageFromFile(HWND hwnd, HWND hRich) {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"图像文件 (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        Gdiplus::Bitmap bmp(szFile);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            HBITMAP hBmp = NULL;
            Gdiplus::Color bg(255, 255, 255);
            bmp.GetHBITMAP(bg, &hBmp);
            if (hBmp) {
                if (OpenClipboard(hwnd)) {
                    EmptyClipboard();
                    if (!SetClipboardData(CF_BITMAP, hBmp)) {
                        DeleteObject(hBmp);
                    }
                    CloseClipboard();
                    SendMessageW(hRich, WM_PASTE, 0, 0);
                } else {
                    DeleteObject(hBmp);
                }
            }
            SetFocus(hRich);
        }
    }
}
