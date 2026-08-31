// FeedKit - ini_text.cpp
#include "ini_text.h"
#include "util.h"

#include <vector>

namespace fk {

namespace {

// ReShade Setup writes ReShade.ini with a UTF-8 BOM; the runtime rewrites it
// without. Track the BOM so section matching sees the real first line.
bool load_ini_lines(const std::wstring& file, std::vector<std::wstring>& lines, bool& trailing_newline, bool& had_bom) {
    std::string data;
    if (!read_file(file, data))
        return false;
    had_bom = data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
              (unsigned char)data[2] == 0xBF;
    if (had_bom)
        data.erase(0, 3);

    std::wstring w = to_wide(data);
    size_t start = 0;
    while (start < w.size()) {
        size_t nl = w.find(L'\n', start);
        std::wstring line = (nl == std::wstring::npos) ? w.substr(start) : w.substr(start, nl - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(line);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    trailing_newline = !w.empty() && w.back() == L'\n';
    return true;
}

bool save_ini_lines(const std::wstring& file, const std::vector<std::wstring>& lines,
                    bool trailing_newline, bool had_bom) {
    std::wstring w;
    for (size_t i = 0; i < lines.size(); i++) {
        w += lines[i];
        if (i + 1 < lines.size() || trailing_newline)
            w += L"\r\n";
    }
    std::string out = to_utf8(w);
    std::string bom = had_bom ? "\xEF\xBB\xBF" : "";
    return write_file(file, (bom + out).c_str(), bom.size() + out.size());
}

bool find_key_line(const std::vector<std::wstring>& lines, const std::wstring& section,
                   const std::wstring& key, size_t& out_idx) {
    const std::wstring header = L"[" + section + L"]";
    bool in_section = false;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::wstring t = trim(lines[i]);
        if (t.size() >= 2 && t.front() == L'[') {
            in_section = _wcsicmp(t.c_str(), header.c_str()) == 0;
            continue;
        }
        if (!in_section)
            continue;
        // Match "key=value" and "key = value" (dgVoodoo.conf pads with spaces).
        if (starts_with(t, key)) {
            size_t eq = t.find(L'=', key.size());
            if (eq != std::wstring::npos && trim(t.substr(key.size(), eq - key.size())).empty()) {
                out_idx = i;
                return true;
            }
        }
    }
    return false;
}

} // namespace

bool ini_get_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, std::wstring& value) {
    value.clear();
    bool trailing = false, had_bom = false;
    std::vector<std::wstring> lines;
    if (!load_ini_lines(file, lines, trailing, had_bom))
        return false;
    size_t idx = 0;
    if (!find_key_line(lines, section, key, idx))
        return false;
    const std::wstring& line = lines[idx];
    value = trim(line.substr(line.find(L'=') + 1));
    return true;
}

bool ini_set_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, const std::wstring& value) {
    bool trailing = false, had_bom = false;
    std::vector<std::wstring> lines;
    if (!load_ini_lines(file, lines, trailing, had_bom))
        return false;

    size_t idx = 0;
    if (find_key_line(lines, section, key, idx)) {
        if (value.empty())
            lines.erase(lines.begin() + idx); // empty value = remove the key
        else
            lines[idx] = key + L"=" + value;
    } else {
        if (value.empty())
            return true; // key absent already; nothing to remove

        // Find the section header, then insert before the next section (or at end).
        const std::wstring header = L"[" + section + L"]";
        size_t insert_at = lines.size();
        bool found_section = false;
        for (size_t i = 0; i < lines.size(); i++) {
            const std::wstring t = trim(lines[i]);
            if (t.size() >= 2 && t.front() == L'[') {
                if (found_section) { insert_at = i; break; }
                found_section = (_wcsicmp(t.c_str(), header.c_str()) == 0);
                if (found_section) insert_at = i + 1;
            }
        }
        if (!found_section) {
            if (!lines.empty() && !lines.back().empty())
                lines.push_back(L"");
            lines.push_back(header);
            insert_at = lines.size();
        }
        lines.insert(lines.begin() + insert_at, key + L"=" + value);
    }

    return save_ini_lines(file, lines, trailing, had_bom);
}

} // namespace fk
