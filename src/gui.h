// FeedKit - gui.h
// Dear ImGui front end: shared app state + drawing.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct HWND__;

namespace ui {

struct LogLine {
    std::string ts;     // UTF-8 "HH:MM:SS"
    std::string text;   // UTF-8
    uint32_t color;     // IM_COL32
};

enum class Arch { Unknown, X86, X64, Arm64 };

// Shared between the UI thread and the worker thread. Guarded by mtx.
struct AppState {
    std::mutex mtx;

    std::wstring exe_path;
    Arch arch = Arch::Unknown;
    bool installed_here = false;

    bool lumenite = true;
    bool vulkan = false;
    bool d3d9 = false;
    bool opengl = false;

    bool busy = false;
    std::vector<LogLine> log;
    bool log_auto_scroll = true;

    std::vector<std::wstring> prev_dirs;
    int prev_sel = -1;

    bool has_done = false;
    bool done_ok = false;
    std::string done_msg; // UTF-8
};

// One-shot actions collected by draw_ui, consumed by the host each frame.
struct AppActions {
    bool browse = false;
    bool install = false;
    bool uninstall = false;
    bool open_folder = false;
    bool open_downloads = false;
    bool clear_log = false;
    int pick_prev = -1;
    std::wstring open_url;
};

// Loads Segoe UI / Consolas from the system font directory (falls back to the
// built-in font). `scale` = dpi/96.
void load_fonts(float scale);
void set_font_scale(float scale);
void apply_theme();
void draw_ui(AppState& s, AppActions& out);

} // namespace ui
