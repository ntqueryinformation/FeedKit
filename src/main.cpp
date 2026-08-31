// FeedKit - main.cpp
// Host application: Win32 window + DirectX 11 + Dear ImGui front end.

#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <dwmapi.h>

#include <d3d11.h>
#include <dxgi.h>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <thread>

#include "gui.h"
#include "install.h"
#include "pe_bitness.h"
#include "record.h"
#include "sources.h"
#include "util.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

using fk::lower;
using fk::to_utf8;

// Must be declared at global scope to match the backend's exported symbol.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

// --- D3D rendering state (standard ImGui win32/dx11 boilerplate) ---
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapchain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;

bool create_device_d3d(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    sd.Flags = 0;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &g_swapchain,
                                               &g_device, &fl, &g_context);
    if (FAILED(hr))
        return false;
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (!back) return false;
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return g_rtv != nullptr;
}

void cleanup_device_d3d() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

// --- App state ---
constexpr wchar_t kWindowClass[] = L"FeedKitMainWindow";
constexpr wchar_t kWindowTitle[] = L"FeedKit - DLSS5-Feeder installer";

ui::AppState g_state;
std::wstring g_last_computed_path;

// --- Log plumbing ---

uint32_t classify_log(const std::wstring& t) {
    auto has = [&](const wchar_t* needle) { return lower(t).find(lower(needle)) != std::wstring::npos; };
    if (has(L"failed"))
        return IM_COL32(248, 81, 73, 255);
    if (has(L"install complete") || has(L"uninstall complete") || has(L"restored ") ||
        has(L"install complete."))
        return IM_COL32(63, 185, 80, 255);
    if (has(L"warning") || has(L"note:") || has(L"skipped"))
        return IM_COL32(214, 158, 46, 255);
    if (t.rfind(L"----", 0) == 0)
        return IM_COL32(93, 158, 245, 255);
    if (t.rfind(L"  ", 0) == 0)
        return IM_COL32(139, 148, 158, 255);
    return IM_COL32(201, 209, 217, 255);
}

void log_line(const std::wstring& s) {
    // "HH:MM:SS" timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[16];
    swprintf_s(ts, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    std::lock_guard<std::mutex> lk(g_state.mtx);
    g_state.log.push_back({to_utf8(ts), to_utf8(s), classify_log(s)});
    if (g_state.log.size() > 4000)
        g_state.log.erase(g_state.log.begin(), g_state.log.begin() + 1000);
}

// --- Worker thread ---

struct Job {
    bool uninstall = false;
    fk::InstallOptions opts;
};

void run_job(std::shared_ptr<Job> job) {
    fk::LogFn log = [](const std::wstring& line) { log_line(line); };
    fk::ProgressFn progress = [](uint64_t, uint64_t) {};

    fk::InstallResult res;
    if (job->uninstall)
        res = fk::run_uninstall(fk::path_parent(job->opts.game_exe), log);
    else
        res = fk::run_install(job->opts, log, progress);

    std::lock_guard<std::mutex> lk(g_state.mtx);
    g_state.busy = false;
    g_state.has_done = true;
    g_state.done_ok = res.ok;
    g_state.done_msg = to_utf8(res.message);
    // Refresh "installed here" marker after the job touched the folder.
    g_state.installed_here = fk::record_exists(fk::path_parent(job->opts.game_exe));
}

void start_job(bool uninstall, const fk::InstallOptions& opts) {
    auto job = std::make_shared<Job>();
    job->uninstall = uninstall;
    job->opts = opts;
    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.busy = true;
    }
    log_line(uninstall ? L"---- Starting uninstall ----" : L"---- Starting install ----");
    std::thread(run_job, job).detach();
}

// --- State helpers (UI thread) ---

void refresh_selection() {
    std::wstring exe;
    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        exe = g_state.exe_path;
        if (exe == g_last_computed_path) return;
        g_last_computed_path = exe;
    }
    ui::Arch arch = ui::Arch::Unknown;
    bool installed = false;
    if (!exe.empty() && fk::file_exists(exe)) {
        switch (fk::pe_arch(exe)) {
        case fk::PeArch::X86:   arch = ui::Arch::X86; break;
        case fk::PeArch::X64:   arch = ui::Arch::X64; break;
        case fk::PeArch::Arm64: arch = ui::Arch::Arm64; break;
        default:                arch = ui::Arch::Unknown; break;
        }
        installed = fk::record_exists(fk::path_parent(exe));
    }
    std::lock_guard<std::mutex> lk(g_state.mtx);
    g_state.arch = arch;
    g_state.installed_here = installed;
}

void refresh_prev_installs() {
    auto entries = fk::index_load();
    std::lock_guard<std::mutex> lk(g_state.mtx);
    g_state.prev_dirs.clear();
    for (const auto& e : entries) g_state.prev_dirs.push_back(e.game_dir);
    g_state.prev_sel = -1;
}

void set_exe_path(const std::wstring& path) {
    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.exe_path = path;
    }
    refresh_selection();
}

// --- WndProc ---

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return 1;

    switch (msg) {
    case WM_SIZE: {
        if (g_device && wp != SIZE_MINIMIZED) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swapchain->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* back = nullptr;
            g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
            if (back) {
                g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
                back->Release();
            }
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        wchar_t buf[MAX_PATH];
        if (DragQueryFileW(drop, 0, buf, MAX_PATH)) {
            std::wstring path = buf;
            if (fk::path_extension(path) != L".exe" && fk::dir_exists(path)) {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((path + L"\\*.exe").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    path = fk::path_combine(path, fd.cFileName);
                    FindClose(h);
                }
            }
            if (fk::path_extension(path) == L".exe")
                set_exe_path(path);
        }
        DragFinish(drop);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        UINT wdpi = GetDpiForWindow(hwnd);
        float s = wdpi / 96.0f;
        mmi->ptMinTrackSize = {LONG(760 * s), LONG(640 * s)};
        return 0;
    }
    case WM_DPICHANGED: {
        float scale = HIWORD(wp) / 96.0f;
        ui::set_font_scale(scale);
        const RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void enable_dark_titlebar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

// --- Actions ---

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
    if (GetOpenFileNameW(&ofn))
        set_exe_path(buf);
}

void consume_actions(HWND hwnd, const ui::AppActions& a) {
    if (a.browse)
        browse_for_exe(hwnd);

    if (a.clear_log) {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.log.clear();
    }
    if (a.open_downloads)
        fk::open_folder(fk::fetch_temp_dir());
    if (a.open_folder && !g_state.exe_path.empty())
        fk::open_folder(fk::path_parent(g_state.exe_path));
    if (!a.open_url.empty())
        fk::open_url(a.open_url);
    if (a.pick_prev >= 0) {
        std::wstring dir;
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            if (a.pick_prev < (int)g_state.prev_dirs.size())
                dir = g_state.prev_dirs[a.pick_prev];
        }
        if (!dir.empty()) {
            fk::InstallRecord rec;
            std::wstring exe;
            if (fk::record_load(dir, rec) && fk::file_exists(rec.game_exe))
                exe = rec.game_exe;
            if (exe.empty()) {
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((dir + L"\\*.exe").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    exe = fk::path_combine(dir, fd.cFileName);
                    FindClose(h);
                }
            }
            if (!exe.empty())
                set_exe_path(exe);
        }
    }

    bool busy = false;
    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        busy = g_state.busy;
    }

    if (a.install && !busy) {
        fk::InstallOptions opts;
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            opts.game_exe = g_state.exe_path;
            opts.install_lumenite = g_state.lumenite;
            opts.d3d9_translate = g_state.d3d9;
            opts.opengl = g_state.opengl;
            opts.install_vulkan_layer = g_state.vulkan;
        }
        if (fk::file_exists(opts.game_exe))
            start_job(false, opts);
    }
    if (a.uninstall && !busy) {
        std::wstring exe;
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            exe = g_state.exe_path;
        }
        std::wstring dir = fk::path_parent(exe);
        if (fk::record_exists(dir) &&
            MessageBoxW(hwnd,
                        L"Remove the FeedKit / DLSS5-Feeder files from this game folder?\n\n"
                        L"Files FeedKit replaced will be restored from their backups.",
                        kWindowTitle, MB_YESNO | MB_ICONQUESTION) == IDYES)
            start_job(true, fk::InstallOptions{exe});
    }
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kWindowClass;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    // Design size 900x720 at 96 DPI, scaled for the monitor's DPI.
    UINT dpi = GetDpiForSystem();
    float ds = dpi / 96.0f;
    RECT rc{0, 0, LONG(900 * ds), LONG(720 * ds)};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;
    enable_dark_titlebar(hwnd);
    DragAcceptFiles(hwnd, TRUE);

    if (!create_device_d3d(hwnd)) {
        MessageBoxW(hwnd, L"Direct3D 11 initialization failed.", kWindowTitle, MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't litter the exe dir with imgui.ini

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    ui::load_fonts(dpi / 96.0f);
    ui::apply_theme();

    {
        std::lock_guard<std::mutex> lk(g_state.mtx);
        g_state.log.push_back({to_utf8(L"00:00:00"),
                               to_utf8(L"FeedKit v1.3 - pick a game .exe (or drop it here), then Install. "
                                       L"Everything is fetched fresh from upstream on each install."),
                               IM_COL32(139, 148, 158, 255)});
    }
    refresh_prev_installs();

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running)
            break;

        refresh_selection();

        // Render frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ui::AppActions actions;
        {
            std::lock_guard<std::mutex> lk(g_state.mtx);
            ui::draw_ui(g_state, actions);
        }
        consume_actions(hwnd, actions);

        ImGui::Render();
        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0); // vsync
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_device_d3d();
    return 0;
}
