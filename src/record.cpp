// FeedKit - record.cpp
#include "record.h"
#include "json_lite.h"
#include "util.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace fk {

namespace {

// FeedKit <= 1.1.1 wrote these files as raw UTF-16LE (wide-string bytes) while
// the reader assumed UTF-8, so no record could ever be loaded again. New
// writes are UTF-8; reads accept both.
std::string normalize_json_bytes(const std::string& data, bool& ok) {
    ok = false;
    if (data.empty())
        return data;
    if (data.size() % 2 == 0 && data[1] == '\0') {
        std::wstring w(data.size() / 2, L'\0');
        memcpy(w.data(), data.data(), data.size());
        ok = true;
        return to_utf8(w);
    }
    ok = true;
    return data;
}

bool get_bool(const Json& j, const wchar_t* key) {
    const Json* v = j.find(key);
    return v && v->type == Json::Type::Bool ? v->boolean : false;
}

} // namespace

static std::wstring json_escape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
        case L'"': out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x20)
                out += fmt(L"\\u%04x", c);
            else
                out += c;
        }
    }
    return out;
}

static std::wstring q(const std::wstring& s) { return L"\"" + json_escape(s) + L"\""; }

bool record_save(const InstallRecord& rec) {
    std::wstring j = L"{\n";
    j += L"  \"tool\": " + q(L"FeedKit") + L",\n";
    j += L"  \"version\": " + q(rec.tool_version) + L",\n";
    j += L"  \"timestamp\": " + q(rec.timestamp) + L",\n";
    j += L"  \"game_exe\": " + q(rec.game_exe) + L",\n";
    j += L"  \"game_dir\": " + q(rec.game_dir) + L",\n";
    j += L"  \"is_32bit\": " + std::wstring(rec.is_32bit ? L"true" : L"false") + L",\n";
    j += L"  \"reshade_by_us\": " + std::wstring(rec.reshade_by_us ? L"true" : L"false") + L",\n";
    j += L"  \"vulkan_layer\": " + std::wstring(rec.vulkan_layer ? L"true" : L"false") + L",\n";
    j += L"  \"files\": [\n";
    for (size_t i = 0; i < rec.files.size(); i++) {
        const auto& f = rec.files[i];
        j += L"    { \"path\": " + q(f.path) + L", \"backup\": " + q(f.backup) + L" }";
        j += (i + 1 < rec.files.size()) ? L",\n" : L"\n";
    }
    j += L"  ]\n";
    if (!rec.ini_touched.empty()) {
        j += L"  ,\"ini_touched\": [\n";
        for (size_t i = 0; i < rec.ini_touched.size(); i++) {
            const auto& t = rec.ini_touched[i];
            j += L"    { \"path\": " + q(t.path) + L", \"section\": " + q(t.section) +
                 L", \"key\": " + q(t.key) + L", \"original\": " + q(t.original) + L" }";
            j += (i + 1 < rec.ini_touched.size()) ? L",\n" : L"\n";
        }
        j += L"  ]\n";
    }
    j += L"}\n";
    const std::string out = to_utf8(j);
    return write_file(rec.record_path(), out.c_str(), out.size());
}

static bool rec_from_json(const Json& j, InstallRecord& out) {
    out.tool_version = j.get_str(L"version", L"1.0.0");
    out.timestamp = j.get_str(L"timestamp");
    out.game_exe = j.get_str(L"game_exe");
    out.game_dir = j.get_str(L"game_dir");
    out.is_32bit = get_bool(j, L"is_32bit");
    out.reshade_by_us = get_bool(j, L"reshade_by_us");
    out.vulkan_layer = get_bool(j, L"vulkan_layer");
    if (const Json* files = j.find(L"files"); files && files->is(Json::Type::Array)) {
        for (const auto& f : files->items) {
            RecordedFile rf;
            rf.path = f.get_str(L"path");
            rf.backup = f.get_str(L"backup");
            if (!rf.path.empty()) out.files.push_back(std::move(rf));
        }
    }
    if (const Json* inis = j.find(L"ini_touched"); inis && inis->is(Json::Type::Array)) {
        for (const auto& t : inis->items) {
            IniTouch it;
            it.path = t.get_str(L"path");
            it.section = t.get_str(L"section", L"GENERAL");
            it.key = t.get_str(L"key");
            it.original = t.get_str(L"original");
            if (!it.path.empty() && !it.key.empty()) out.ini_touched.push_back(std::move(it));
        }
    }
    return !out.game_dir.empty();
}

bool record_load(const std::wstring& game_dir, InstallRecord& out) {
    std::wstring path = game_dir + L"\\feedkit.install.json";
    std::string data;
    if (!read_file(path, data)) return false;
    bool enc_ok = false;
    data = normalize_json_bytes(data, enc_ok);
    if (!enc_ok) return false;
    Json j;
    if (!Json::parse(data, j)) return false;
    if (!rec_from_json(j, out)) return false;
    return true;
}

bool record_exists(const std::wstring& game_dir) {
    return file_exists(game_dir + L"\\feedkit.install.json");
}

std::wstring index_path() {
    return local_appdata() + L"\\FeedKit\\installs.json";
}

std::vector<IndexEntry> index_load() {
    std::vector<IndexEntry> out;
    std::string data;
    if (!read_file(index_path(), data)) return out;
    bool enc_ok = false;
    data = normalize_json_bytes(data, enc_ok);
    if (!enc_ok) return out;
    Json j;
    if (!Json::parse(data, j) || !j.is(Json::Type::Array)) return out;
    for (const auto& e : j.items) {
        IndexEntry ie;
        ie.game_exe = e.get_str(L"game_exe");
        ie.game_dir = e.get_str(L"game_dir");
        ie.timestamp = e.get_str(L"timestamp");
        ie.is_32bit = get_bool(e, L"is_32bit");
        if (!ie.game_dir.empty()) out.push_back(std::move(ie));
    }
    return out;
}

bool index_add(const InstallRecord& rec) {
    auto entries = index_load();
    // Replace existing entry for the same dir (case-insensitive on Windows paths).
    std::wstring dir_l = lower(rec.game_dir);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const IndexEntry& e) { return lower(e.game_dir) == dir_l; }),
                  entries.end());
    IndexEntry ie;
    ie.game_exe = rec.game_exe;
    ie.game_dir = rec.game_dir;
    ie.timestamp = rec.timestamp;
    ie.is_32bit = rec.is_32bit;
    entries.push_back(ie);

    std::wstring j = L"[\n";
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        j += L"  { \"game_exe\": " + q(e.game_exe) + L", \"game_dir\": " + q(e.game_dir) +
             L", \"timestamp\": " + q(e.timestamp) +
             L", \"is_32bit\": " + std::wstring(e.is_32bit ? L"true" : L"false") + L" }";
        j += (i + 1 < entries.size()) ? L",\n" : L"\n";
    }
    j += L"]\n";

    std::wstring path = index_path();
    make_dirs(path_parent(path));
    const std::string out = to_utf8(j);
    return write_file(path, out.c_str(), out.size());
}

bool index_remove(const std::wstring& game_dir) {
    auto entries = index_load();
    std::wstring dir_l = lower(game_dir);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const IndexEntry& e) { return lower(e.game_dir) == dir_l; }),
                  entries.end());
    std::wstring j = L"[\n";
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        j += L"  { \"game_exe\": " + q(e.game_exe) + L", \"game_dir\": " + q(e.game_dir) +
             L", \"timestamp\": " + q(e.timestamp) +
             L", \"is_32bit\": " + std::wstring(e.is_32bit ? L"true" : L"false") + L" }";
        j += (i + 1 < entries.size()) ? L",\n" : L"\n";
    }
    j += L"]\n";
    std::wstring path = index_path();
    make_dirs(path_parent(path));
    const std::string out = to_utf8(j);
    return write_file(path, out.c_str(), out.size());
}

} // namespace fk
