// FeedKit - gui.cpp
// Dark-themed Dear ImGui front end.
#include "gui.h"

#include "imgui.h"
#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

using fk::to_utf8;

namespace ui {

// ---------------------------------------------------------------------------
// Palette

namespace {
constexpr ImU32 C_BG        = IM_COL32(13, 16, 22, 255);
constexpr ImU32 C_PANEL     = IM_COL32(22, 27, 35, 255);
constexpr ImU32 C_PANEL_HI  = IM_COL32(31, 38, 48, 255);
constexpr ImU32 C_BORDER    = IM_COL32(46, 55, 68, 255);
constexpr ImU32 C_TEXT      = IM_COL32(230, 237, 243, 255);
constexpr ImU32 C_TEXT_SOFT = IM_COL32(201, 209, 217, 255);
constexpr ImU32 C_DIM       = IM_COL32(139, 148, 158, 255);
constexpr ImU32 C_FAINT     = IM_COL32(99, 110, 123, 255);
constexpr ImU32 C_ACCENT    = IM_COL32(35, 168, 110, 255);
constexpr ImU32 C_ACCENT_HI = IM_COL32(52, 199, 137, 255);
constexpr ImU32 C_RED       = IM_COL32(199, 76, 70, 255);
constexpr ImU32 C_RED_HI    = IM_COL32(228, 96, 89, 255);
constexpr ImU32 C_AMBER     = IM_COL32(214, 158, 46, 255);
constexpr ImU32 C_LINK      = IM_COL32(93, 158, 245, 255);

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* mono = nullptr;
    float scale = 1.0f;
};
Fonts g;
ImFont* F_REG() { return g.regular ? g.regular : ImGui::GetIO().FontDefault; }
ImFont* F_BOLD() { return g.bold ? g.bold : F_REG(); }
ImFont* F_MONO() { return g.mono ? g.mono : F_REG(); }

// ---------------------------------------------------------------------------
// Small helpers

void push_disabled(bool off) {
    if (off) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);
    }
}
void pop_disabled(bool off) {
    if (off) {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
}

void card_begin(const char* id, const ImVec2& size = {0, 0}, ImGuiWindowFlags flags = 0) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, C_BORDER);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f * g.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * g.scale, 13 * g.scale});
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, flags);
}
void card_end() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void label_caps(const char* txt) {
    ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
    ImGui::PushFont(F_BOLD(), 11.5f * g.scale);
    ImGui::TextUnformatted(txt);
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

// Rounded pill badge
void pill(const char* txt, ImU32 fg) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight() + 7 * g.scale;
    float w = ImGui::CalcTextSize(txt).x + 15 * g.scale;
    dl->AddRectFilled(p, {p.x + w, p.y + h}, (fg & 0x00FFFFFF) | IM_COL32(0, 0, 0, 36), h * 0.5f);
    dl->AddText({p.x + 8 * g.scale, p.y + 3.5f * g.scale}, fg, txt);
    ImGui::Dummy({w, h});
}

bool accent_button(const char* txt, float w, ImU32 bg = C_ACCENT, ImU32 bg_hi = C_ACCENT_HI) {
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg_hi);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * g.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {w * 0.5f - ImGui::CalcTextSize(txt).x * 0.5f, 9 * g.scale});
    bool r = ImGui::Button(txt, {w, 34 * g.scale});
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return r;
}

bool ghost_button(const char* txt, float w, ImU32 border = C_BORDER, ImU32 text_col = C_TEXT_SOFT) {
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_PANEL_HI);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, C_PANEL_HI);
    ImGui::PushStyleColor(ImGuiCol_Text, text_col);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8 * g.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 9 * g.scale});
    bool hovered = false;
    bool r = false;
    ImGui::PushID(txt);
    r = ImGui::Button(txt, {w, 34 * g.scale});
    hovered = ImGui::IsItemHovered();
    ImGui::PopID();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    dl->AddRect(a, b, hovered ? C_DIM : border, 8 * g.scale, 0, 1.2f);
    return r;
}

// Small underlined-ish link text; returns true when clicked.
bool link(const char* txt) {
    ImGui::PushStyleColor(ImGuiCol_Text, C_LINK);
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
    return ImGui::IsItemHovered() && ImGui::IsMouseClicked(0);
}

void status_dot(ImU32 col, const char* txt, ImU32 txt_col = C_DIM) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float r = 4.5f * g.scale;
    float y = p.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddCircleFilled({p.x + r + 1.0f, y}, r, col);
    ImGui::Dummy({r * 2 + 8 * g.scale, ImGui::GetTextLineHeight()});
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, txt_col);
    ImGui::TextUnformatted(txt);
    ImGui::PopStyleColor();
}

void marquee_bar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 7 * g.scale;
    dl->AddRectFilled(p, {p.x + w, p.y + h}, IM_COL32(255, 255, 255, 16), h * 0.5f);
    float t = (float)fmod(ImGui::GetTime() * 0.55, 1.0);
    float fw = w * 0.30f;
    float x0 = p.x + t * (w - fw);
    dl->AddRectFilled({x0, p.y}, {x0 + fw, p.y + h}, C_ACCENT_HI, h * 0.5f);
    ImGui::Dummy({w, h});
}

// Result modal
void done_modal(AppState& s) {
    if (s.has_done) {
        ImGui::OpenPopup("##result");
        s.has_done = false;
    }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f},
                            ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Border, C_BORDER);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12 * g.scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {22 * g.scale, 18 * g.scale});
    if (ImGui::BeginPopupModal("##result", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
        ImGui::PushFont(F_BOLD(), 17 * g.scale);
        ImGui::PushStyleColor(ImGuiCol_Text, s.done_ok ? C_ACCENT_HI : C_RED_HI);
        ImGui::TextUnformatted(s.done_ok ? "Done" : "Finished with errors");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, C_TEXT_SOFT);
        ImGui::PushTextWrapPos(460 * g.scale);
        ImGui::TextUnformatted(s.done_msg.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Dummy({0, 2 * g.scale});
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - 100 * g.scale);
        if (accent_button("OK", 100 * g.scale))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace

// ---------------------------------------------------------------------------
// Fonts + theme

void load_fonts(float scale) {
    g.scale = scale;
    ImGuiIO& io = ImGui::GetIO();
    char windir[MAX_PATH] = {};
    GetWindowsDirectoryA(windir, MAX_PATH);
    std::string base = std::string(windir) + "\\Fonts\\";
    g.regular = io.Fonts->AddFontFromFileTTF((base + "segoeui.ttf").c_str(), 17.0f * scale);
    if (!g.regular)
        g.regular = io.Fonts->AddFontDefault();
    g.bold = io.Fonts->AddFontFromFileTTF((base + "segoeuib.ttf").c_str(), 17.0f * scale);
    g.mono = io.Fonts->AddFontFromFileTTF((base + "consola.ttf").c_str(), 15.0f * scale);
    io.FontDefault = g.regular;
}

void set_font_scale(float scale) { g.scale = scale; }

void apply_theme() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.ChildRounding = 9.0f;
    st.FrameRounding = 6.0f;
    st.GrabRounding = 6.0f;
    st.PopupRounding = 10.0f;
    st.ScrollbarRounding = 8.0f;
    st.WindowBorderSize = 0.0f;
    st.ChildBorderSize = 1.0f;
    st.FrameBorderSize = 0.0f;
    st.WindowPadding = {0, 0};
    st.FramePadding = {10, 7};
    st.ItemSpacing = {8, 8};
    st.ItemInnerSpacing = {6, 5};
    st.ScrollbarSize = 13.0f;

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg]        = ImColor(C_BG);
    c[ImGuiCol_ChildBg]         = ImColor(C_PANEL);
    c[ImGuiCol_PopupBg]         = ImColor(C_PANEL);
    c[ImGuiCol_Border]          = ImColor(C_BORDER);
    c[ImGuiCol_Text]            = ImColor(C_TEXT);
    c[ImGuiCol_TextDisabled]    = ImColor(C_DIM);
    c[ImGuiCol_FrameBg]         = ImColor(C_PANEL_HI);
    c[ImGuiCol_FrameBgHovered]  = ImColor(38, 46, 58, 255);
    c[ImGuiCol_FrameBgActive]   = ImColor(44, 53, 66, 255);
    c[ImGuiCol_CheckMark]       = ImColor(C_ACCENT_HI);
    c[ImGuiCol_Button]          = ImColor(C_PANEL_HI);
    c[ImGuiCol_ButtonHovered]   = ImColor(42, 51, 64, 255);
    c[ImGuiCol_ButtonActive]    = ImColor(38, 46, 58, 255);
    c[ImGuiCol_Header]          = ImColor(C_PANEL_HI);
    c[ImGuiCol_HeaderHovered]   = ImColor(42, 51, 64, 255);
    c[ImGuiCol_HeaderActive]    = ImColor(44, 53, 66, 255);
    c[ImGuiCol_Separator]       = ImColor(C_BORDER);
    c[ImGuiCol_ScrollbarBg]     = ImColor(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]   = ImColor(52, 62, 77, 255);
    c[ImGuiCol_ScrollbarGrabHovered] = ImColor(66, 78, 95, 255);
    c[ImGuiCol_ScrollbarGrabActive]  = ImColor(80, 94, 114, 255);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

// ---------------------------------------------------------------------------
// Main frame

void draw_ui(AppState& s, AppActions& out) {
    ImGuiIO& io = ImGui::GetIO();
    float sc = g.scale;
    float pad = 18 * sc;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("##host", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    // Page: full-window layer with comfortable padding on all sides.
    // (AlwaysUseWindowPadding: borderless children get WindowPadding zeroed otherwise.)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {pad, pad});
    ImGui::BeginChild("##page", io.DisplaySize, ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    // Content column: centered, capped width so wide windows get real margins.
    float col_w = ImGui::GetContentRegionAvail().x;
    const float max_col = 820 * sc;
    if (col_w > max_col) {
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - max_col) * 0.5f);
        col_w = max_col;
    }
    ImGui::BeginChild("##col", {col_w, 0}, ImGuiChildFlags_None);

    // ---- Header ----
    ImGui::PushFont(F_BOLD(), 23 * sc);
    ImGui::PushStyleColor(ImGuiCol_Text, C_TEXT);
    ImGui::TextUnformatted("FeedKit");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6 * sc);
    ImGui::PushStyleColor(ImGuiCol_Text, C_FAINT);
    ImGui::PushFont(F_REG(), 12.5f * sc);
    ImGui::TextUnformatted("v1.1.3");
    ImGui::PopFont();
    ImGui::PopStyleColor();

    // Repo link, right-aligned in the header row.
    {
        const char* repo = "github.com/ntqueryinformation/FeedKit";
        float rw = ImGui::CalcTextSize(repo).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - rw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6 * sc);
        if (link(repo))
            out.open_url = L"https://github.com/ntqueryinformation/FeedKit";
    }
    ImGui::Dummy({0, 12 * sc});

    // ---- Game executable card ----
    card_begin("##cardgame");
    label_caps("GAME EXECUTABLE");
    ImGui::Spacing();

    static char path_buf[1024] = {};
    {
        std::string u = to_utf8(s.exe_path);
        if (u.size() < sizeof(path_buf)) {
            memset(path_buf, 0, sizeof(path_buf));
            memcpy(path_buf, u.c_str(), u.size());
        }
    }
    float browse_w = 110 * sc;
    ImGui::PushStyleColor(ImGuiCol_Text, s.exe_path.empty() ? C_FAINT : C_TEXT);
    ImGui::PushFont(F_MONO(), 14 * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 9 * sc});
    ImGui::InputText("##exepath", path_buf, sizeof(path_buf), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 8 * sc);
    if (ghost_button("Browse...", browse_w))
        out.browse = true;

    // Status row: bitness pill + installed marker / hint.
    ImGui::Dummy({0, 6 * sc});
    if (s.exe_path.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, C_FAINT);
        ImGui::TextUnformatted("Drop a game .exe on this window, or browse for it.");
        ImGui::PopStyleColor();
    } else {
        switch (s.arch) {
        case Arch::X64:   pill("x64", IM_COL32(63, 185, 80, 255)); break;
        case Arch::X86:   pill("x86 (32-bit)", IM_COL32(210, 153, 34, 255)); break;
        case Arch::Arm64: pill("ARM64 (unsupported)", IM_COL32(248, 81, 73, 255)); break;
        default:          pill("unreadable", IM_COL32(248, 81, 73, 255)); break;
        }
        if (s.installed_here) {
            ImGui::SameLine(0, 10 * sc);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3 * sc);
            ImGui::PushStyleColor(ImGuiCol_Text, C_ACCENT_HI);
            ImGui::TextUnformatted("FeedKit is already installed in this folder");
            ImGui::PopStyleColor();
        }
    }
    card_end();

    ImGui::Dummy({0, 10 * sc});

    // ---- Components card ----
    card_begin("##cardopt");
    label_caps("COMPONENTS");
    ImGui::Spacing();
    ImGui::Checkbox("Install LumeniteFX motion-vector provider", &s.lumenite);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
    ImGui::TextUnformatted("(recommended)");
    ImGui::PopStyleColor();
    ImGui::Checkbox("Also install the Vulkan layer", &s.vulkan);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
    ImGui::TextUnformatted("(fallback for Vulkan games)");
    ImGui::PopStyleColor();

    ImGui::Dummy({0, 4 * sc});
    ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
    ImGui::TextUnformatted("Previous installs:");
    ImGui::PopStyleColor();
    static std::vector<std::string> prev_utf8;
    prev_utf8.clear();
    for (const auto& d : s.prev_dirs) prev_utf8.push_back(to_utf8(d));
    int sel = s.prev_sel;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::Combo("##prev", &sel, [](void* data, int idx) -> const char* {
            auto* v = (std::vector<std::string>*)data;
            return idx >= 0 && idx < (int)v->size() ? (*v)[idx].c_str() : "";
        },
        &prev_utf8, (int)prev_utf8.size())) {
        if (sel >= 0 && sel < (int)prev_utf8.size())
            out.pick_prev = sel;
    }
    card_end();

    ImGui::Dummy({0, 12 * sc});

    // ---- Action row ----
    bool valid = !s.exe_path.empty() && s.arch != Arch::Unknown && s.arch != Arch::Arm64;
    bool dis = s.busy || !valid;
    bool dis_un = s.busy || !valid || !s.installed_here;
    push_disabled(dis);
    if (accent_button("Install", 150 * sc))
        out.install = true;
    pop_disabled(dis);
    ImGui::SameLine(0, 10 * sc);
    push_disabled(dis_un);
    if (ghost_button("Uninstall", 120 * sc, IM_COL32(120, 60, 56, 255), IM_COL32(240, 130, 122, 255)))
        out.uninstall = true;
    pop_disabled(dis_un);
    ImGui::SameLine(0, 10 * sc);
    push_disabled(dis);
    if (ghost_button("Open game folder", 150 * sc))
        out.open_folder = true;
    pop_disabled(dis);

    // Right-aligned upstream note with links, all on the button row's baseline.
    ImGui::SameLine(0, 12 * sc);
    {
        ImGui::PushFont(F_REG(), 12.5f * sc);
        struct Seg { const char* txt; bool is_link; const wchar_t* url; };
        const Seg segs[] = {
            {"fetched fresh:", false, nullptr},
            {"reshade.me", true, L"https://reshade.me"},
            {"-", false, nullptr},
            {"DLSS5-Feeder", true, L"https://github.com/jlrouzies-fr/DLSS5-Feeder"},
            {"-", false, nullptr},
            {"RHI", true, L"https://github.com/RankFTW/RHI"},
            {"-", false, nullptr},
            {"LumeniteFX", true, L"https://github.com/umar-afzaal/LumeniteFX"},
        };
        const float sp = 5 * sc;
        float total = -sp;
        for (const auto& seg : segs) total += ImGui::CalcTextSize(seg.txt).x + sp;
        float left_bound = ImGui::GetCursorPosX();                    // just after the buttons
        float right_edge = ImGui::GetContentRegionMax().x;            // column's right edge
        bool fits = (right_edge - left_bound) >= total;
        if (fits)
            ImGui::SetCursorPosX(right_edge - total);
        else
            ImGui::NewLine(); // too narrow: flow under the buttons instead
        for (const auto& seg : segs) {
            ImGui::SameLine(0, sp);
            if (seg.is_link) {
                ImGui::PushStyleColor(ImGuiCol_Text, C_LINK);
                ImGui::TextUnformatted(seg.txt);
                ImGui::PopStyleColor();
                if (fits && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
                    out.open_url = seg.url;
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, C_FAINT);
                ImGui::TextUnformatted(seg.txt);
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopFont();
    }

    ImGui::Dummy({0, 10 * sc});

    // ---- Status strip ----
    if (s.busy) {
        marquee_bar();
        ImGui::Dummy({0, 4 * sc});
        status_dot(C_AMBER, "Working - see the log below...", C_AMBER);
    } else if (dis) {
        status_dot(C_FAINT, "Pick a game executable to begin.");
    } else {
        status_dot(C_ACCENT, "Ready.");
    }

    ImGui::Dummy({0, 8 * sc});

    // ---- Log card (fills the rest) ----
    // Measure the footer text at its render size so nothing clips at the bottom.
    const char* foot_txt =
        "Open the ReShade overlay (Home key): enable the LUMEN technique, then DLSS 5 Feed, "
        "then the neural rendering technique. Keep MSAA/SSAA off. Verify via dlss5-feed.log "
        "in the game folder.";
    ImGui::PushFont(F_REG(), 13 * sc);
    float foot_w = ImGui::CalcTextSize(foot_txt).x;
    ImGui::PopFont();
    int foot_lines = std::max(1, (int)std::ceil(foot_w / std::max(120.0f, col_w - 32 * sc)));
    float footer_reserve = (34 + foot_lines * 20 + 18) * sc;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float log_h = avail.y - footer_reserve;
    if (log_h < 90 * sc) log_h = 90 * sc;

    card_begin("##cardlog", {0, log_h});
    ImGui::SetCursorPos({16 * sc, 11 * sc});
    label_caps("LOG");
    ImGui::SameLine(0, 0);
    {
        const char* dl_txt = "clear";
        const char* fo_txt = "downloads folder";
        float bw = ImGui::CalcTextSize(dl_txt).x + ImGui::CalcTextSize(fo_txt).x + 34 * sc;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw);
        ImGui::PushFont(F_REG(), 12 * sc);
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        if (ImGui::SmallButton(dl_txt)) out.clear_log = true;
        ImGui::SameLine(0, 14 * sc);
        if (ImGui::SmallButton(fo_txt)) out.open_downloads = true;
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImGui::SetCursorPos({10 * sc, 34 * sc});

    // Scroll region
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6 * sc, 0});
    ImVec2 scroll_size = {ImGui::GetContentRegionAvail().x - 6 * sc,
                          log_h - 44 * sc};
    ImGui::BeginChild("##scroll", scroll_size, ImGuiChildFlags_None);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    bool at_bottom = ImGui::GetScrollY() + ImGui::GetWindowHeight() >= ImGui::GetScrollMaxY() - 4.0f;
    ImGui::PushFont(F_MONO(), 13 * sc);
    for (const auto& l : s.log) {
        ImGui::PushStyleColor(ImGuiCol_Text, C_FAINT);
        ImGui::TextUnformatted(l.ts.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8 * sc);
        ImGui::PushStyleColor(ImGuiCol_Text, l.color);
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    if (s.log_auto_scroll && at_bottom)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    card_end();

    // ---- Footer: in-game steps ----
    ImGui::Dummy({0, 6 * sc});
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (ImGui::CollapsingHeader("After installing - the in-game steps")) {
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        ImGui::PushFont(F_REG(), 13 * sc);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x);
        ImGui::TextUnformatted(foot_txt);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    ImGui::EndChild(); // ##col
    ImGui::EndChild(); // ##page
    ImGui::End();
    done_modal(s);
}

} // namespace ui
