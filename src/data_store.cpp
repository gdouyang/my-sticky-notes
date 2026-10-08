#include "data_store.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>

static std::wstring EscapeJsonString(const std::wstring& str) {
    std::wstring out;
    for (wchar_t c : str) {
        if (c == L'\"') out += L"\\\"";
        else if (c == L'\\') out += L"\\\\";
        else if (c == L'\b') out += L"\\b";
        else if (c == L'\f') out += L"\\f";
        else if (c == L'\n') out += L"\\n";
        else if (c == L'\r') out += L"\\r";
        else if (c == L'\t') out += L"\\t";
        else out += c;
    }
    return out;
}

static std::wstring UnescapeJsonString(const std::wstring& str) {
    std::wstring out;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == L'\\' && i + 1 < str.length()) {
            wchar_t next = str[i + 1];
            if (next == L'\"') { out += L'\"'; i++; }
            else if (next == L'\\') { out += L'\\'; i++; }
            else if (next == L'n') { out += L'\n'; i++; }
            else if (next == L'r') { out += L'\r'; i++; }
            else if (next == L't') { out += L'\t'; i++; }
            else { out += next; i++; }
        } else {
            out += str[i];
        }
    }
    return out;
}

std::wstring DataStore::GetDataFilePath() {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        std::wstring dir = std::wstring(appDataPath) + L"\\MyStickyNotes";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir + L"\\notes_data.json";
    }
    return L"notes_data.json";
}

bool DataStore::Load(AppData& data) {
    std::wstring path = GetDataFilePath();
    std::wifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open()) return false;

    // 简单高效轻量 JSON 解析器（避免引入庞大第三方库保持 <3MB 极低开销）
    std::wstringstream buffer;
    buffer << file.rdbuf();
    std::wstring content = buffer.str();
    file.close();

    auto findInt = [&](const std::wstring& key, int defaultVal) -> int {
        size_t pos = content.find(L"\"" + key + L"\":");
        if (pos != std::wstring::npos) {
            size_t start = pos + key.length() + 3;
            size_t end = content.find_first_of(L",}\n", start);
            if (end != std::wstring::npos) {
                try {
                    return std::stoi(content.substr(start, end - start));
                } catch (...) {}
            }
        }
        return defaultVal;
    };

    auto findBool = [&](const std::wstring& key, bool defaultVal) -> bool {
        size_t pos = content.find(L"\"" + key + L"\":");
        if (pos != std::wstring::npos) {
            size_t start = pos + key.length() + 3;
            if (content.substr(start, 4) == L"true") return true;
            if (content.substr(start, 5) == L"false") return false;
        }
        return defaultVal;
    };

    auto findString = [&](const std::wstring& key) -> std::wstring {
        size_t pos = content.find(L"\"" + key + L"\":");
        if (pos != std::wstring::npos) {
            size_t firstQuote = content.find(L'\"', pos + key.length() + 3);
            if (firstQuote != std::wstring::npos) {
                size_t secondQuote = firstQuote + 1;
                while (secondQuote < content.length()) {
                    if (content[secondQuote] == L'\"' && content[secondQuote - 1] != L'\\') {
                        break;
                    }
                    secondQuote++;
                }
                if (secondQuote < content.length()) {
                    return UnescapeJsonString(content.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                }
            }
        }
        return L"";
    };

    data.windowX = findInt(L"windowX", 100);
    data.windowY = findInt(L"windowY", 100);
    data.windowWidth = findInt(L"windowWidth", 360);
    data.windowHeight = findInt(L"windowHeight", 520);
    data.isAlwaysOnTop = findBool(L"isAlwaysOnTop", false);
    data.currentTab = findInt(L"currentTab", 0);
    data.memoText = findString(L"memoText");

    // 解析 todos 数组
    data.todos.clear();
    size_t todosPos = content.find(L"\"todos\":");
    if (todosPos != std::wstring::npos) {
        size_t arrStart = content.find(L'[', todosPos);
        size_t arrEnd = content.find(L']', arrStart);
        if (arrStart != std::wstring::npos && arrEnd != std::wstring::npos) {
            std::wstring todosArr = content.substr(arrStart, arrEnd - arrStart + 1);
            size_t itemStart = 0;
            while ((itemStart = todosArr.find(L'{', itemStart)) != std::wstring::npos) {
                size_t itemEnd = todosArr.find(L'}', itemStart);
                if (itemEnd == std::wstring::npos) break;
                std::wstring itemStr = todosArr.substr(itemStart, itemEnd - itemStart + 1);

                TodoItem item;
                // id
                size_t idPos = itemStr.find(L"\"id\":");
                if (idPos != std::wstring::npos) {
                    size_t s = idPos + 5;
                    size_t e = itemStr.find_first_of(L",}", s);
                    try { item.id = std::stoi(itemStr.substr(s, e - s)); } catch (...) {}
                }
                // completed
                size_t compPos = itemStr.find(L"\"completed\":");
                if (compPos != std::wstring::npos) {
                    item.completed = (itemStr.substr(compPos + 12, 4) == L"true");
                }
                // text
                size_t textPos = itemStr.find(L"\"text\":");
                if (textPos != std::wstring::npos) {
                    size_t q1 = itemStr.find(L'\"', textPos + 7);
                    if (q1 != std::wstring::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < itemStr.length()) {
                            if (itemStr[q2] == L'\"' && itemStr[q2 - 1] != L'\\') break;
                            q2++;
                        }
                        item.text = UnescapeJsonString(itemStr.substr(q1 + 1, q2 - q1 - 1));
                    }
                }
                if (!item.text.empty() || item.id > 0) {
                    data.todos.push_back(item);
                }
                itemStart = itemEnd + 1;
            }
        }
    }
    return true;
}

bool DataStore::Save(const AppData& data) {
    std::wstring path = GetDataFilePath();
    std::wofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << L"{\n";
    file << L"  \"windowX\": " << data.windowX << L",\n";
    file << L"  \"windowY\": " << data.windowY << L",\n";
    file << L"  \"windowWidth\": " << data.windowWidth << L",\n";
    file << L"  \"windowHeight\": " << data.windowHeight << L",\n";
    file << L"  \"isAlwaysOnTop\": " << (data.isAlwaysOnTop ? L"true" : L"false") << L",\n";
    file << L"  \"currentTab\": " << data.currentTab << L",\n";
    file << L"  \"memoText\": \"" << EscapeJsonString(data.memoText) << L"\",\n";
    file << L"  \"todos\": [\n";

    for (size_t i = 0; i < data.todos.size(); ++i) {
        const auto& item = data.todos[i];
        file << L"    {\"id\": " << item.id 
             << L", \"completed\": " << (item.completed ? L"true" : L"false")
             << L", \"text\": \"" << EscapeJsonString(item.text) << L"\"}";
        if (i + 1 < data.todos.size()) file << L",";
        file << L"\n";
    }

    file << L"  ]\n";
    file << L"}\n";
    file.close();
    return true;
}
