#include "data_store.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>

static std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &strTo[0], sizeNeeded, NULL, NULL);
    return strTo;
}

static std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstrTo[0], sizeNeeded);
    return wstrTo;
}

static std::string EscapeJsonString(const std::string& str) {
    std::string out;
    for (char c : str) {
        if (c == '\"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static std::string UnescapeJsonString(const std::string& str) {
    std::string out;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[i + 1];
            if (next == '\"') { out += '\"'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else if (next == 'n') { out += '\n'; i++; }
            else if (next == 'r') { out += '\r'; i++; }
            else if (next == 't') { out += '\t'; i++; }
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
    std::ifstream file(WideToUtf8(path), std::ios::binary);
    if (!file.is_open()) return false;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    auto findInt = [&](const std::string& key, int defaultVal) -> int {
        size_t pos = content.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t start = pos + key.length() + 3;
            size_t end = content.find_first_of(",}\n", start);
            if (end != std::string::npos) {
                try {
                    return std::stoi(content.substr(start, end - start));
                } catch (...) {}
            }
        }
        return defaultVal;
    };

    auto findBool = [&](const std::string& key, bool defaultVal) -> bool {
        size_t pos = content.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t start = pos + key.length() + 3;
            if (content.substr(start, 4) == "true") return true;
            if (content.substr(start, 5) == "false") return false;
        }
        return defaultVal;
    };

    auto findString = [&](const std::string& key) -> std::string {
        size_t pos = content.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t firstQuote = content.find('\"', pos + key.length() + 3);
            if (firstQuote != std::string::npos) {
                size_t secondQuote = firstQuote + 1;
                while (secondQuote < content.length()) {
                    if (content[secondQuote] == '\"' && content[secondQuote - 1] != '\\') {
                        break;
                    }
                    secondQuote++;
                }
                if (secondQuote < content.length()) {
                    return UnescapeJsonString(content.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                }
            }
        }
        return "";
    };

    data.windowX = findInt("windowX", 150);
    data.windowY = findInt("windowY", 150);
    data.windowWidth = findInt("windowWidth", 360);
    data.windowHeight = findInt("windowHeight", 520);
    data.isAlwaysOnTop = findBool("isAlwaysOnTop", false);
    data.colorTheme = findInt("colorTheme", COLOR_YELLOW);
    data.currentTab = findInt("currentTab", 0);
    data.memoText = Utf8ToWide(findString("memoText"));

    // 解析 todos 数组
    data.todos.clear();
    size_t todosPos = content.find("\"todos\":");
    if (todosPos != std::string::npos) {
        size_t arrStart = content.find('[', todosPos);
        size_t arrEnd = content.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string todosArr = content.substr(arrStart, arrEnd - arrStart + 1);
            size_t itemStart = 0;
            while ((itemStart = todosArr.find('{', itemStart)) != std::string::npos) {
                size_t itemEnd = todosArr.find('}', itemStart);
                if (itemEnd == std::string::npos) break;
                std::string itemStr = todosArr.substr(itemStart, itemEnd - itemStart + 1);

                TodoItem item;
                // id
                size_t idPos = itemStr.find("\"id\":");
                if (idPos != std::string::npos) {
                    size_t s = idPos + 5;
                    size_t e = itemStr.find_first_of(",}", s);
                    try { item.id = std::stoll(itemStr.substr(s, e - s)); } catch (...) {}
                }
                // completed
                size_t compPos = itemStr.find("\"completed\":");
                if (compPos != std::string::npos) {
                    item.completed = (itemStr.substr(compPos + 12, 4) == "true");
                }
                // text
                size_t textPos = itemStr.find("\"text\":");
                if (textPos != std::string::npos) {
                    size_t q1 = itemStr.find('\"', textPos + 7);
                    if (q1 != std::string::npos) {
                        size_t q2 = q1 + 1;
                        while (q2 < itemStr.length()) {
                            if (itemStr[q2] == '\"' && itemStr[q2 - 1] != '\\') break;
                            q2++;
                        }
                        item.text = Utf8ToWide(UnescapeJsonString(itemStr.substr(q1 + 1, q2 - q1 - 1)));
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
    std::ofstream file(WideToUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"windowX\": " << data.windowX << ",\n";
    file << "  \"windowY\": " << data.windowY << ",\n";
    file << "  \"windowWidth\": " << data.windowWidth << ",\n";
    file << "  \"windowHeight\": " << data.windowHeight << ",\n";
    file << "  \"isAlwaysOnTop\": " << (data.isAlwaysOnTop ? "true" : "false") << ",\n";
    file << "  \"colorTheme\": " << data.colorTheme << ",\n";
    file << "  \"currentTab\": " << data.currentTab << ",\n";
    file << "  \"memoText\": \"" << EscapeJsonString(WideToUtf8(data.memoText)) << "\",\n";
    file << "  \"todos\": [\n";

    for (size_t i = 0; i < data.todos.size(); ++i) {
        const auto& item = data.todos[i];
        file << "    {\"id\": " << item.id 
             << ", \"completed\": " << (item.completed ? "true" : "false")
             << ", \"text\": \"" << EscapeJsonString(WideToUtf8(item.text)) << "\"}";
        if (i + 1 < data.todos.size()) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
    file.close();
    return true;
}
