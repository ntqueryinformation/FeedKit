// FeedKit - main.cpp
// FeedKit: one-click installer/uninstaller for DLSS5-Feeder game setups.
// Native Win32, no external UI dependencies.

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <thread>
#include <memory>

#include "install.h"
#include "pe_bitness.h"
#include "record.h"
#include "sources.h"
#include "util.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// Control IDs
enum {
    IDC_EXE_EDIT = 101,
    IDC_BROWSE,
    IDC_INSTALL,
    IDC_UNINSTALL,
    IDC_OPENFOLDER,
    IDC_VULKAN,
    IDC_LOG,
    IDC_PROGRESS,
    IDC_BITNESS,
    IDC_PREV,
    IDC_OPENLOG,
    IDC_NEXTSTEPS,
};

// Worker -> UI messages
enum {
    WM_APP_LOG = WM_APP + 1,      // wParam: heap wchar_t* (freed by UI)
    WM_APP_DONE,                  // wParam: ok, lParam: heap wchar_t* (freed by UI)
};

constexpr wchar_t kWindowClass[] = L"FeedKitMainWindow";
constexpr wchar_t kWindowTitle[] = L"FeedKit - DLSS5-Feeder installer";

HWND g_exe_edit, g_browse, g_install, g_uninstall, g_openfolder, g_vulkan;
HWND g_log, g_progress, g_bitness, g_prev, g_openlog, g_nextsteps;
HFONT g_ui_font, g_mono_font;
bool g_busy = false;

// ---------------------------------------------------------------------------
// Layout helpers (96 DPI design coordinates, scaled to the window's DPI)

int g_dpi = 96;
int S(int v) { return MulDiv(v, g_dpi, 96); }

HWND make_control(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
                  int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                             parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
    return c;
}

// ---------------------------------------------------------------------------
// UI state helpers

std::wstring current_exe() {
    int len = GetWindowTextLengthW(g_exe_edit);
    std::wstring s(len, L'\0');
    GetWindowTextW(g_exe_edit, s.data(), len + 1);
    return s;
}

void update_bitness_label() {
    std::wstring exe = current_exe();
    if (exe.empty() || !fk::file_exists(exe)) {
        SetWindowTextW(g_bitness, L"");
        return;
    }
    fk::PeArch arch = fk::pe_arch(exe);
    std::wstring text = fk::fmt(L"Detected: %s", fk::pe_arch_name(arch));
    if (arch == fk::PeArch::X64 || arch == fk::PeArch::X86) {
        std::wstring dir = fk::path_parent(exe);
        if (fk::record_exists(dir))
            text += L"   |   FeedKit already installed here";
    }
    SetWindowTextW(g_bitness, text.c_str());
}

void update_buttons() {
    std::wstring exe = current_exe();
    bool valid = !exe.empty() && fk::file_exists(exe);
    EnableWindow(g_install, valid && !g_busy);
    EnableWindow(g_uninstall, valid && !g_busy && fk::record_exists(fk::path_parent(exe)));
    EnableWindow(g_openfolder, valid && !g_busy);
    EnableWindow(g_browse, !g_busy);
    EnableWindow(g_vulkan, !g_busy);
}

void refresh_prev_installs() {
    if (auto* old = (std::vector<std::wstring>*)GetPropW(g_prev, L"dirs")) {
        RemovePropW(g_prev, L"dirs");
        delete old;
    }
    SendMessageW(g_prev, CB_RESETCONTENT, 0, 0);
    auto entries = fk::index_load();
    auto* stored = new std::vector<std::wstring>();
    for (const auto& e : entries) {
        std::wstring display = e.game_dir + L"   [" + e.timestamp + L"]";
        SendMessageW(g_prev, CB_ADDSTRING, 0, (LPARAM)display.c_str());
        stored->push_back(e.game_dir);
    }
    SetPropW(g_prev, L"dirs", (HANDLE)stored);
}

void on_prev_selected() {
    auto* stored = (std::vector<std::wstring>*)GetPropW(g_prev, L"dirs");
    if (!stored) return;
    int sel = (int)SendMessageW(g_prev, CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)stored->size()) return;
    std::wstring dir = (*stored)[sel];
    // Find an exe inside: prefer recorded game_exe from the record file.
    fk::InstallRecord rec;
    std::wstring exe;
    if (fk::record_load(dir, rec) && fk::file_exists(rec.game_exe))
        exe = rec.game_exe;
    if (exe.empty()) {
        // Fall back: any exe in the folder.
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*.exe").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            exe = fk::path_combine(dir, fd.cFileName);
            FindClose(h);
        }
    }
    if (!exe.empty()) {
        SetWindowTextW(g_exe_edit, exe.c_str());
        update_bitness_label();
        update_buttons();
    }
}

void log_line(const std::wstring& s) {
    std::wstring line = L"[" + fk::timestamp_now() + L"]  " + s;
    int idx = (int)SendMessageW(g_log, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    SendMessageW(g_log, LB_SETTOPINDEX, idx, 0);
    while ((int)SendMessageW(g_log, LB_GETCOUNT, 0, 0) > 2000) {
        SendMessageW(g_log, LB_DELETESTRING, 0, 0);
    }
}

void set_busy(bool busy) {
    g_busy = busy;
    SendMessageW(g_progress, PBM_SETMARQUEE, busy ? 1 : 0, 25);
    update_buttons();
    if (busy) SetWindowTextW(g_bitness, L"Working - see the log below...");
    else update_bitness_label();
}

// ---------------------------------------------------------------------------
// Worker thread

struct Job {
    bool uninstall = false;
    fk::InstallOptions opts;
};

void run_job(HWND hwnd, std::shared_ptr<Job> job) {
    fk::LogFn log = [hwnd](const std::wstring& line) {
        wchar_t* copy = new wchar_t[line.size() + 1];
        wcscpy_s(copy, line.size() + 1, line.c_str());
        PostMessageW(hwnd, WM_APP_LOG, (WPARAM)copy, 0);
    };
    fk::ProgressFn progress = [](uint64_t, uint64_t) {
        // Per-file progress is logged by the pipeline; the bar stays in marquee mode.
    };

    fk::InstallResult res;
    if (job->uninstall)
        res = fk::run_uninstall(fk::path_parent(job->opts.game_exe), log);
    else
        res = fk::run_install(job->opts, log, progress);

    wchar_t* msg = new wchar_t[res.message.size() + 1];
    wcscpy_s(msg, res.message.size() + 1, res.message.c_str());
    PostMessageW(hwnd, WM_APP_DONE, res.ok ? 1 : 0, (LPARAM)msg);
}

// ---------------------------------------------------------------------------
// Actions

void browse_for_exe(HWND hwnd) {
    wchar_t buf[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Programs (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select the game executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_exe_edit, buf);
        update_bitness_label();
        update_buttons();
    }
}

void start_install(HWND hwnd) {
    Job job;
    job.opts.game_exe = current_exe();
    job.opts.install_vulkan_layer = SendMessageW(g_vulkan, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto* shared = new std::shared_ptr<Job>(std::make_shared<Job>(job));
    set_busy(true);
    log_line(L"---- Starting install ----");
    std::thread([hwnd, shared] { run_job(hwnd, *shared); delete shared; }).detach();
}

void start_uninstall(HWND hwnd) {
    Job job;
    job.uninstall = true;
    job.opts.game_exe = current_exe();
    auto* shared = new std::shared_ptr<Job>(std::make_shared<Job>(job));
    set_busy(true);
    log_line(L"---- Starting uninstall ----");
    std::thread([hwnd, shared] { run_job(hwnd, *shared); delete shared; }).detach();
}

// ---------------------------------------------------------------------------

void on_done(HWND hwnd, bool ok, const wchar_t* msg) {
    set_busy(false);
    log_line(ok ? L"---- Done ----" : L"---- Finished with errors ----");
    update_bitness_label();
    update_buttons();
    refresh_prev_installs();
    MessageBoxW(hwnd, msg, L"FeedKit", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_dpi = GetDpiForWindow(hwnd);

        g_ui_font = CreateFontW(S(-16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        g_mono_font = CreateFontW(S(-15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

        make_control(hwnd, L"STATIC", L"Game executable:", SS_LEFT, 12, 12, 200, 20, -1);
        g_exe_edit = make_control(hwnd, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                                  12, 34, 596, 26, IDC_EXE_EDIT);
        g_browse = make_control(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 618, 33, 96, 27, IDC_BROWSE);
        g_bitness = make_control(hwnd, L"STATIC", L"", SS_LEFT, 14, 66, 680, 20, IDC_BITNESS);

        g_vulkan = make_control(hwnd, L"BUTTON",
                                L"Also install the Vulkan layer (fallback for Vulkan games)",
                                BS_AUTOCHECKBOX, 12, 90, 560, 22, IDC_VULKAN);

        make_control(hwnd, L"STATIC", L"Previous installs:", SS_LEFT, 12, 120, 200, 20, -1);
        g_prev = make_control(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                              12, 140, 702, 200, IDC_PREV);

        g_install = make_control(hwnd, L"BUTTON", L"Install", BS_PUSHBUTTON, 12, 174, 130, 34, IDC_INSTALL);
        g_uninstall = make_control(hwnd, L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 150, 174, 130, 34, IDC_UNINSTALL);
        g_openfolder = make_control(hwnd, L"BUTTON", L"Open game folder", BS_PUSHBUTTON, 288, 174, 150, 34, IDC_OPENFOLDER);

        make_control(hwnd, L"STATIC",
                     L"Upstream sources are fetched fresh on every install: reshade.me, "
                     L"github.com/jlrouzies-fr/DLSS5-Feeder, RankFTW/RHI.",
                     SS_LEFT, 14, 216, 700, 34, -1);

        // Log pane
        make_control(hwnd, L"BUTTON", L"Log", BS_GROUPBOX, 12, 252, 702, 260, -1);
        g_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
                                S(20), S(272), S(686), S(232), hwnd, (HMENU)(INT_PTR)IDC_LOG, nullptr, nullptr);
        SendMessageW(g_log, WM_SETFONT, (WPARAM)g_mono_font, TRUE);

        g_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                     S(12), S(520), S(566), S(22), hwnd, (HMENU)(INT_PTR)IDC_PROGRESS, nullptr, nullptr);
        SendMessageW(g_progress, PBM_SETMARQUEE, 0, 25);
        g_openlog = make_control(hwnd, L"BUTTON", L"Open downloads folder", BS_PUSHBUTTON,
                                 586, 517, 128, 28, IDC_OPENLOG);

        // Next steps
        g_nextsteps = make_control(
            hwnd, L"STATIC",
            L"After installing:  (1) install a motion-vector provider - recommended LumeniteFX Kernel - and set "
            L"DLSS5_MV_PROVIDER=3 in the DLSS5_Feed.fx preprocessor definitions.  "
            L"(2) In game, open the ReShade overlay, enable the LUMEN technique, then DLSS 5 Feed, then the "
            L"neural rendering technique; keep MSAA/SSAA off.  (3) Verify via dlss5-feed.log in the game folder.",
            SS_LEFT, 12, 548, 702, 60, IDC_NEXTSTEPS);

        DragAcceptFiles(hwnd, TRUE);
        refresh_prev_installs();
        update_buttons();
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        switch (id) {
        case IDC_BROWSE:
            if (code == BN_CLICKED) browse_for_exe(hwnd);
            break;
        case IDC_INSTALL:
            if (code == BN_CLICKED) start_install(hwnd);
            break;
        case IDC_UNINSTALL:
            if (code == BN_CLICKED && MessageBoxW(hwnd,
                    L"Remove the FeedKit / DLSS5-Feeder files from this game folder?\n\n"
                    L"Files FeedKit replaced will be restored from their backups.",
                    kWindowTitle, MB_YESNO | MB_ICONQUESTION) == IDYES)
                start_uninstall(hwnd);
            break;
        case IDC_OPENFOLDER:
            if (code == BN_CLICKED) fk::open_folder(fk::path_parent(current_exe()));
            break;
        case IDC_OPENLOG:
            if (code == BN_CLICKED) fk::open_folder(fk::fetch_temp_dir());
            break;
        case IDC_EXE_EDIT:
            if (code == EN_CHANGE) {
                update_bitness_label();
                update_buttons();
            }
            break;
        case IDC_PREV:
            if (code == CBN_SELCHANGE) on_prev_selected();
            break;
        }
        break;
    }

    case WM_APP_LOG: {
        wchar_t* s = (wchar_t*)wp;
        log_line(s);
        delete[] s;
        return 0;
    }

    case WM_APP_DONE: {
        wchar_t* s = (wchar_t*)lp;
        on_done(hwnd, wp != 0, s);
        delete[] s;
        return 0;
    }

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t buf[MAX_PATH];
        if (DragQueryFileW(drop, 0, buf, MAX_PATH)) {
            std::wstring path = buf;
            if (fk::path_extension(path) == L".exe") {
                SetWindowTextW(g_exe_edit, path.c_str());
            } else if (fk::dir_exists(path)) {
                // Convenience: pick the first exe found in a dropped folder.
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((path + L"\\*.exe").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    SetWindowTextW(g_exe_edit, fk::path_combine(path, fd.cFileName).c_str());
                    FindClose(h);
                } else {
                    MessageBoxW(hwnd, L"No .exe found in the dropped folder.", kWindowTitle, MB_ICONINFORMATION);
                }
            } else {
                MessageBoxW(hwnd, L"Drop a game .exe (or its folder).", kWindowTitle, MB_ICONINFORMATION);
            }
            update_bitness_label();
            update_buttons();
        }
        DragFinish(drop);
        return 0;
    }

    case WM_DPICHANGED: {
        // Keep it simple: recreate fonts, let the user resize/restart for full rescale.
        g_dpi = HIWORD(wp);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize = {S(760), S(660)};
        return 0;
    }

    case WM_DESTROY:
        if (auto* stored = (std::vector<std::wstring>*)GetPropW(g_prev, L"dirs")) {
            RemovePropW(g_prev, L"dirs");
            delete stored;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // Client area: 736 x 620 at design DPI.
    RECT rc{0, 0, S(736), S(620)};
    AdjustWindowRect(&rc, (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)), FALSE);

    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX),
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
