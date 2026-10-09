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

std::wstring DataStore::GetMemoFilePath(const std::string& noteId) {
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        std::wstring dir = std::wstring(appDataPath) + L"\\MyStickyNotes";
        CreateDirectoryW(dir.c_str(), NULL);
        if (noteId.empty() || noteId == "default") {
            return dir + L"\\memo_notes.rtf";
        }
        return dir + L"\\memo_notes_" + Utf8ToWide(noteId) + L".rtf";
    }
    if (noteId.empty() || noteId == "default") return L"memo_notes.rtf";
    return L"memo_notes_" + Utf8ToWide(noteId) + L".rtf";
}

static std::vector<TodoItem> ParseTodosFromArrayStr(const std::string& todosArr) {
    std::vector<TodoItem> todos;
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
            todos.push_back(item);
        }
        itemStart = itemEnd + 1;
    }
    return todos;
}

bool DataStore::Load(AppData& data) {
    data.notes.clear();

    std::wstring path = GetDataFilePath();
    std::ifstream file(WideToUtf8(path), std::ios::binary);
    std::string content = "";
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
        file.close();
    }

    auto findIntIn = [&](const std::string& str, const std::string& key, int defaultVal) -> int {
        size_t pos = str.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t start = pos + key.length() + 3;
            size_t end = str.find_first_of(",}\n", start);
            if (end != std::string::npos) {
                try {
                    return std::stoi(str.substr(start, end - start));
                } catch (...) {}
            }
        }
        return defaultVal;
    };

    auto findBoolIn = [&](const std::string& str, const std::string& key, bool defaultVal) -> bool {
        size_t pos = str.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t start = pos + key.length() + 3;
            if (str.substr(start, 4) == "true") return true;
            if (str.substr(start, 5) == "false") return false;
        }
        return defaultVal;
    };

    auto findStringIn = [&](const std::string& str, const std::string& key) -> std::string {
        size_t pos = str.find("\"" + key + "\":");
        if (pos != std::string::npos) {
            size_t firstQuote = str.find('\"', pos + key.length() + 3);
            if (firstQuote != std::string::npos) {
                size_t secondQuote = firstQuote + 1;
                while (secondQuote < str.length()) {
                    if (str[secondQuote] == '\"' && str[secondQuote - 1] != '\\') {
                        break;
                    }
                    secondQuote++;
                }
                if (secondQuote < str.length()) {
                    return UnescapeJsonString(str.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                }
            }
        }
        return "";
    };

    data.globalOpacity = findIntIn(content, "globalOpacity", 100);
    data.nextNoteId = findIntIn(content, "nextNoteId", 1);

    // 检查是否存在 "notes": [ 数组
    size_t notesPos = content.find("\"notes\":");
    if (notesPos != std::string::npos) {
        size_t arrStart = content.find('[', notesPos);
        size_t arrEnd = content.rfind(']');
        if (arrStart != std::string::npos && arrEnd != std::string::npos && arrEnd > arrStart) {
            std::string notesArr = content.substr(arrStart, arrEnd - arrStart + 1);
            size_t nStart = 0;
            while ((nStart = notesArr.find('{', nStart)) != std::string::npos) {
                // 寻找匹配的 '}'
                int braceCount = 0;
                size_t nEnd = nStart;
                bool inStr = false;
                for (; nEnd < notesArr.length(); ++nEnd) {
                    if (notesArr[nEnd] == '\"' && (nEnd == 0 || notesArr[nEnd - 1] != '\\')) {
                        inStr = !inStr;
                    } else if (!inStr) {
                        if (notesArr[nEnd] == '{') braceCount++;
                        else if (notesArr[nEnd] == '}') {
                            braceCount--;
                            if (braceCount == 0) break;
                        }
                    }
                }
                if (braceCount != 0 || nEnd >= notesArr.length()) break;
                std::string noteJson = notesArr.substr(nStart, nEnd - nStart + 1);

                NoteCardData card;
                card.noteId = findStringIn(noteJson, "noteId");
                if (card.noteId.empty()) card.noteId = "default";
                card.windowX = findIntIn(noteJson, "windowX", 150);
                card.windowY = findIntIn(noteJson, "windowY", 150);
                card.windowWidth = findIntIn(noteJson, "windowWidth", 360);
                card.windowHeight = findIntIn(noteJson, "windowHeight", 520);
                card.isAlwaysOnTop = findBoolIn(noteJson, "isAlwaysOnTop", true);
                card.opacity = findIntIn(noteJson, "opacity", 100);
                card.colorTheme = findIntIn(noteJson, "colorTheme", COLOR_YELLOW);
                card.currentTab = findIntIn(noteJson, "currentTab", 0);

                // 读取 todos
                size_t tPos = noteJson.find("\"todos\":");
                if (tPos != std::string::npos) {
                    size_t ts = noteJson.find('[', tPos);
                    size_t te = noteJson.find(']', ts);
                    if (ts != std::string::npos && te != std::string::npos) {
                        card.todos = ParseTodosFromArrayStr(noteJson.substr(ts, te - ts + 1));
                    }
                }

                // 读取对应的 RTF 文件
                std::wstring memoPath = GetMemoFilePath(card.noteId);
                std::ifstream memoFile(WideToUtf8(memoPath), std::ios::binary);
                if (memoFile.is_open()) {
                    std::stringstream memoBuf;
                    memoBuf << memoFile.rdbuf();
                    card.memoRtf = memoBuf.str();
                    memoFile.close();
                } else {
                    card.memoRtf = findStringIn(noteJson, "memoRtf");
                }

                data.notes.push_back(card);
                nStart = nEnd + 1;
            }
        }
    } else if (!content.empty()) {
        // 向后兼容旧版单便签 JSON 格式
        NoteCardData card;
        card.noteId = "default";
        card.windowX = findIntIn(content, "windowX", 150);
        card.windowY = findIntIn(content, "windowY", 150);
        card.windowWidth = findIntIn(content, "windowWidth", 360);
        card.windowHeight = findIntIn(content, "windowHeight", 520);
        card.isAlwaysOnTop = findBoolIn(content, "isAlwaysOnTop", true);
        card.opacity = 100;
        card.colorTheme = findIntIn(content, "colorTheme", COLOR_YELLOW);
        card.currentTab = findIntIn(content, "currentTab", 0);

        size_t tPos = content.find("\"todos\":");
        if (tPos != std::string::npos) {
            size_t ts = content.find('[', tPos);
            size_t te = content.find(']', ts);
            if (ts != std::string::npos && te != std::string::npos) {
                card.todos = ParseTodosFromArrayStr(content.substr(ts, te - ts + 1));
            }
        }

        std::wstring memoPath = GetMemoFilePath("default");
        std::ifstream memoFile(WideToUtf8(memoPath), std::ios::binary);
        if (memoFile.is_open()) {
            std::stringstream memoBuf;
            memoBuf << memoFile.rdbuf();
            card.memoRtf = memoBuf.str();
            memoFile.close();
        } else {
            card.memoRtf = findStringIn(content, "memoRtf");
        }

        data.notes.push_back(card);
    }

    if (data.notes.empty()) {
        NoteCardData card;
        card.noteId = "default";
        // 尝试加载 default 的 rtf
        std::wstring memoPath = GetMemoFilePath("default");
        std::ifstream memoFile(WideToUtf8(memoPath), std::ios::binary);
        if (memoFile.is_open()) {
            std::stringstream memoBuf;
            memoBuf << memoFile.rdbuf();
            card.memoRtf = memoBuf.str();
            memoFile.close();
        }
        data.notes.push_back(card);
    }

    return true;
}

bool DataStore::Save(const AppData& data) {
    // 1. 保存各便签独立的富文本 RTF 文件
    for (const auto& card : data.notes) {
        std::wstring memoPath = GetMemoFilePath(card.noteId);
        std::ofstream memoFile(WideToUtf8(memoPath), std::ios::binary | std::ios::trunc);
        if (memoFile.is_open()) {
            memoFile.write(card.memoRtf.data(), card.memoRtf.size());
            memoFile.close();
        }
    }

    // 2. 保存轻量多便签配置数据到 notes_data.json
    std::wstring path = GetDataFilePath();
    std::ofstream file(WideToUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"globalOpacity\": " << data.globalOpacity << ",\n";
    file << "  \"nextNoteId\": " << data.nextNoteId << ",\n";
    file << "  \"notes\": [\n";

    for (size_t n = 0; n < data.notes.size(); ++n) {
        const auto& card = data.notes[n];
        file << "    {\n";
        file << "      \"noteId\": \"" << EscapeJsonString(card.noteId) << "\",\n";
        file << "      \"windowX\": " << card.windowX << ",\n";
        file << "      \"windowY\": " << card.windowY << ",\n";
        file << "      \"windowWidth\": " << card.windowWidth << ",\n";
        file << "      \"windowHeight\": " << card.windowHeight << ",\n";
        file << "      \"isAlwaysOnTop\": " << (card.isAlwaysOnTop ? "true" : "false") << ",\n";
        file << "      \"opacity\": " << card.opacity << ",\n";
        file << "      \"colorTheme\": " << card.colorTheme << ",\n";
        file << "      \"currentTab\": " << card.currentTab << ",\n";
        file << "      \"todos\": [\n";

        for (size_t i = 0; i < card.todos.size(); ++i) {
            const auto& item = card.todos[i];
            file << "        {\"id\": " << item.id 
                 << ", \"completed\": " << (item.completed ? "true" : "false")
                 << ", \"text\": \"" << EscapeJsonString(WideToUtf8(item.text)) << "\"}";
            if (i + 1 < card.todos.size()) file << ",";
            file << "\n";
        }

        file << "      ]\n";
        file << "    }";
        if (n + 1 < data.notes.size()) file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";
    file.close();
    return true;
}
