// FeedKit - ini_text.cpp
#include "ini_text.h"
#include "util.h"

#include <vector>

namespace fk {

namespace {

std::vector<std::wstring> split_lines(const std::string& data, bool& trailing_newline) {
    std::wstring w = to_wide(data);
    std::vector<std::wstring> lines;
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
    if (lines.empty()) trailing_newline = false;
    return lines;
}

std::string join_lines(const std::vector<std::wstring>& lines, bool trailing_newline) {
    std::wstring w;
    for (size_t i = 0; i < lines.size(); i++) {
        w += lines[i];
        if (i + 1 < lines.size() || trailing_newline)
            w += L"\r\n";
    }
    return to_utf8(w);
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
        if (in_section && starts_with(lines[i], key + L"=")) {
            out_idx = i;
            return true;
        }
    }
    return false;
}

} // namespace

bool ini_get_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, std::wstring& value) {
    value.clear();
    std::string data;
    if (!read_file(file, data))
        return false;
    bool trailing;
    std::vector<std::wstring> lines = split_lines(data, trailing);
    size_t idx = 0;
    if (!find_key_line(lines, section, key, idx))
        return false;
    const std::wstring& line = lines[idx];
    value = trim(line.substr(line.find(L'=') + 1));
    return true;
}

bool ini_set_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, const std::wstring& value) {
    std::string data;
    if (!read_file(file, data))
        return false;
    bool trailing;
    std::vector<std::wstring> lines = split_lines(data, trailing);

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

    const std::string out = join_lines(lines, trailing);
    return write_file(file, out.c_str(), out.size());
}

} // namespace fk
