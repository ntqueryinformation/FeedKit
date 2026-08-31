// FeedKit - record.h
// Install records: per-game feedkit.install.json + global installs index.
#pragma once

#include <string>
#include <vector>

namespace fk {

struct RecordedFile {
    std::wstring path;    // absolute path of the file we placed
    std::wstring backup;  // absolute path of the .feedkit.bak we made, empty if none
};

// A ReShade.ini key we changed (e.g. PreProcessorDefinitions for the
// motion-vector provider). `original` empty = key was absent before us.
struct IniTouch {
    std::wstring path;      // absolute ini path
    std::wstring section;
    std::wstring key;
    std::wstring original;
};

struct InstallRecord {
    std::wstring tool_version = L"1.2.0";
    std::wstring timestamp;
    std::wstring game_exe;
    std::wstring game_dir;
    std::wstring reshade_dir;    // where ReShade actually lives (setup may redirect, e.g. Source games -> bin\)
    bool is_32bit = false;
    bool d3d9_translate = false; // dgVoodoo2 translation installed
    bool reshade_by_us = false;  // we installed ReShade (dxgi.dll + ReShade.ini)
    bool vulkan_layer = false;
    std::vector<RecordedFile> files;
    std::vector<IniTouch> ini_touched;

    std::wstring record_path() const { return game_dir + L"\\feedkit.install.json"; }
    std::wstring effective_reshade_dir() const { return reshade_dir.empty() ? game_dir : reshade_dir; }
};

struct IndexEntry {
    std::wstring game_exe;
    std::wstring game_dir;
    std::wstring timestamp;
    bool is_32bit = false;
};

bool record_save(const InstallRecord& rec);
bool record_load(const std::wstring& game_dir, InstallRecord& out);
bool record_exists(const std::wstring& game_dir);

std::wstring index_path();                        // %LOCALAPPDATA%\FeedKit\installs.json
std::vector<IndexEntry> index_load();
bool index_add(const InstallRecord& rec);
bool index_remove(const std::wstring& game_dir);

} // namespace fk
