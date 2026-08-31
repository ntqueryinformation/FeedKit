// FeedKit - install.cpp
#include "install.h"
#include "ini_text.h"
#include "sources.h"
#include "record.h"
#include "pe_bitness.h"
#include "util.h"

#include <windows.h>

#include <stdexcept>

#pragma comment(lib, "version.lib")

namespace fk {

namespace {

[[noreturn]] void fail(const std::wstring& msg) { throw std::runtime_error(to_utf8(msg)); }

// --- ReShade helpers ---------------------------------------------------------

bool is_reshade_dll(const std::wstring& dll_path) {
    DWORD handle = 0, size = GetFileVersionInfoSizeW(dll_path.c_str(), &handle);
    if (!size) return false;
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(dll_path.c_str(), 0, size, data.data())) return false;
    struct LANGANDCODEPAGE { WORD lang, codepage; } *xlate = nullptr;
    UINT xlate_len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&xlate, &xlate_len) || !xlate_len)
        return false;
    wchar_t sub[256];
    swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\FileDescription", xlate[0].lang, xlate[0].codepage);
    wchar_t* desc = nullptr;
    UINT desc_len = 0;
    if (!VerQueryValueW(data.data(), sub, (LPVOID*)&desc, &desc_len) || !desc) {
        // Fall back to ProductName.
        swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\ProductName", xlate[0].lang, xlate[0].codepage);
        if (!VerQueryValueW(data.data(), sub, (LPVOID*)&desc, &desc_len) || !desc) return false;
    }
    return lower(desc).find(L"reshade") != std::wstring::npos;
}

// Runs a console program hidden, captures its output, returns its exit code.
int run_capture(const std::wstring& exe, const std::wstring& args, std::wstring& out) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;

    std::wstring cmd = fmt(L"\"%s\" %s", exe.c_str(), args.c_str());
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }

    std::string buf;
    char chunk[4096];
    DWORD n = 0;
    while (ReadFile(rd, chunk, sizeof(chunk), &n, nullptr) && n) buf.append(chunk, n);

    WaitForSingleObject(pi.hProcess, 180000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    out = to_wide(buf);
    return (int)code;
}

bool reshade_headless_install(const std::wstring& setup_exe, const std::wstring& target_exe, const LogFn& log) {
    log(fmt(L"Running ReShade setup (unattended) for %s...", path_filename(target_exe).c_str()));
    std::wstring args = fmt(L"--headless \"%s\" --api dxgi", target_exe.c_str());
    std::wstring output;
    int code = run_capture(setup_exe, args, output);
    for (const auto& line : split(output, L'\n')) {
        std::wstring t = trim(line);
        if (!t.empty()) log(L"  reshade: " + t);
    }
    if (code != 0) {
        log(L"ReShade setup failed (exit code " + std::to_wstring(code) + L").");
        return false;
    }
    return true;
}

// --- ReShade.ini editing -------------------------------------------------------
// All edits go through fk::ini_*_exact text functions: ReShade's ini parser is
// case-sensitive while the Windows profile APIs are not, so WritePrivateProfile-
// String can silently edit the wrong key (e.g. PreProcessorDefinitions vs the
// PreprocessorDefinitions ReShade actually reads).

// Points DLSS5_Feed.fx's DLSS5_MV_PROVIDER at LumeniteFX Kernel (= 3) and
// remembers the previous value so uninstall can put it back.
void ensure_mv_provider_def(const std::wstring& ini_path, InstallRecord& rec, const LogFn& log) {
    std::wstring orig;
    ini_get_exact(ini_path, L"GENERAL", L"PreprocessorDefinitions", orig);

    std::wstring kept;
    for (const auto& tok : split(orig, L',')) {
        std::wstring t = trim(tok);
        if (t.empty()) continue;
        if (starts_with(lower(t), L"dlss5_mv_provider")) continue;
        kept += (kept.empty() ? L"" : L",") + t;
    }
    std::wstring newval = kept.empty() ? std::wstring(L"DLSS5_MV_PROVIDER=3")
                                       : kept + L",DLSS5_MV_PROVIDER=3";
    if (newval == orig) {
        log(L"ReShade.ini already points DLSS5_MV_PROVIDER at LumeniteFX Kernel.");
        return;
    }
    if (!ini_set_exact(ini_path, L"GENERAL", L"PreprocessorDefinitions", newval))
        fail(L"Cannot update PreprocessorDefinitions in " + ini_path);
    rec.ini_touched.push_back({ini_path, L"GENERAL", L"PreprocessorDefinitions", orig});
    log(L"Set DLSS5_MV_PROVIDER=3 (LumeniteFX Kernel) in " + path_filename(ini_path));

    // Remove the wrong-case key older FeedKit versions wrote (dead weight that
    // ReShade's case-sensitive parser never read).
    std::wstring wrong_case;
    if (ini_get_exact(ini_path, L"GENERAL", L"PreProcessorDefinitions", wrong_case)) {
        ini_set_exact(ini_path, L"GENERAL", L"PreProcessorDefinitions", L"");
        log(L"Removed obsolete PreProcessorDefinitions key (wrong case) from " + path_filename(ini_path));
    }
}

// ReShade Setup 6.8 writes search paths as "Shaders\**\**" (double glob), which
// ReShade's own resolver cannot canonicalize (Win32 rejects wildcards -> error
// 123) and skips the path entirely, so no effects are ever found. Collapse it
// to the single trailing glob ReShade handles.
void normalize_search_paths(const std::wstring& ini_path, InstallRecord& rec, const LogFn& log) {
    if (!file_exists(ini_path))
        return;
    for (const wchar_t* key : {L"EffectSearchPaths", L"TextureSearchPaths"}) {
        std::wstring orig;
        if (!ini_get_exact(ini_path, L"GENERAL", key, orig) || orig.empty())
            continue;
        std::wstring fixed = orig;
        size_t pos;
        while ((pos = fixed.find(L"**\\**")) != std::wstring::npos)
            fixed.replace(pos, 6, L"**");
        while ((pos = fixed.find(L"**/**")) != std::wstring::npos)
            fixed.replace(pos, 5, L"**");
        if (fixed == orig)
            continue;
        if (!ini_set_exact(ini_path, L"GENERAL", key, fixed))
            fail(std::wstring(L"Cannot update ") + key + L" in " + ini_path);
        rec.ini_touched.push_back({ini_path, L"GENERAL", key, orig});
        log(std::wstring(L"Fixed malformed search path (") + key + L") in " +
            path_filename(ini_path) + L" - ReShade Setup 6.8 writes a glob ReShade cannot resolve");
    }
}

// --- file placement with backup + record -------------------------------------

struct Sink {
    InstallRecord* rec = nullptr;
    const LogFn* log = nullptr;

    // Copies `src` to `dst`, backing up any existing file as <dst>.feedkit.bak.
    void place(const std::wstring& src, const std::wstring& dst) {
        std::wstring backup;
        if (file_exists(dst)) {
            backup = dst + L".feedkit.bak";
            if (!copy_file(dst, backup, true))
                fail(L"Cannot back up existing file: " + dst);
            if (*log) (*log)(L"Backed up existing " + path_filename(dst) + L" -> " + path_filename(backup));
        }
        if (!make_dirs(path_parent(dst)))
            fail(L"Cannot create directory: " + path_parent(dst));
        if (!copy_file(src, dst, true))
            fail(L"Cannot write " + dst + L" (is the game running, or does the folder need admin rights?)");
        rec->files.push_back({dst, backup});
        if (*log) (*log)(L"Installed " + dst);
    }

    void record_new(const std::wstring& dst) { rec->files.push_back({dst, {}}); }
};

} // namespace

bool reshade_present(const std::wstring& game_dir) {
    return file_exists(path_combine(game_dir, L"dxgi.dll")) ||
           file_exists(path_combine(game_dir, L"ReShade.ini"));
}

InstallResult run_install(const InstallOptions& opts, const LogFn& log, const ProgressFn& progress) {
    InstallResult res;
    InstallRecord rec;
    try {
        const std::wstring game_exe = opts.game_exe;
        if (!file_exists(game_exe))
            fail(L"Game executable not found: " + game_exe);
        const std::wstring game_dir = path_parent(game_exe);
        res.game_dir = game_dir;

        PeArch arch = pe_arch(game_exe);
        if (arch == PeArch::Unknown || arch == PeArch::Arm64)
            fail(L"Unsupported or unreadable executable architecture (" +
                 std::wstring(pe_arch_name(arch)) + L"). x86 and x64 games are supported.");
        rec.is_32bit = (arch == PeArch::X86);
        log(fmt(L"Target: %s [%s]", path_filename(game_exe).c_str(), pe_arch_name(arch)));
        log(L"Game folder: " + game_dir);

        if (!file_is_writable(game_exe))
            fail(L"The game executable is locked - the game is probably still running. Close it and retry.");

        // Refresh path: remove a previous FeedKit install first.
        if (record_exists(game_dir)) {
            log(L"Previous FeedKit install found in this folder - removing it first...");
            InstallResult un = run_uninstall(game_dir, log);
            if (!un.ok)
                fail(L"Cannot refresh over a broken previous install: " + un.message);
        }

        // Existing non-ReShade dxgi.dll would be overwritten - refuse.
        std::wstring dxgi = path_combine(game_dir, L"dxgi.dll");
        if (file_exists(dxgi) && !file_exists(path_combine(game_dir, L"ReShade.ini")) &&
            !is_reshade_dll(dxgi))
            fail(L"An existing dxgi.dll that is not ReShade was found in the game folder. "
                 L"Refusing to overwrite it - remove or rename it first.");

        rec.game_exe = game_exe;
        rec.game_dir = game_dir;
        rec.timestamp = timestamp_now();

        // 1) Fetch everything up front so failures happen before touching the game folder.
        log(L"");
        log(L"== Downloading current files from upstream ==");

        ReshadeBundle reshade;
        bool need_reshade = !reshade_present(game_dir);
        if (need_reshade || rec.is_32bit) // 32-bit games also need ReShade for the host64 helper
            reshade = fetch_reshade(log, progress);
        if (!need_reshade)
            log(L"ReShade already installed in this folder - keeping it.");

        FeederBundle feeder = fetch_feeder(log, progress);
        RenoDxBundle renodx = fetch_renodx_dlss5(log, progress);
        NgxBundle ngx = fetch_ngx_dlls(log, progress);
        LumeniteBundle lumenite;
        if (opts.install_lumenite)
            lumenite = fetch_lumenite(log, progress);

        Sink sink{&rec, &log};

        // 2) ReShade into the game folder.
        log(L"");
        log(L"== Installing ==");
        const std::wstring game_ini = path_combine(game_dir, L"ReShade.ini");
        if (need_reshade) {
            if (!reshade_headless_install(reshade.setup_exe_path, game_exe, log))
                fail(L"ReShade setup did not complete successfully. No files were changed except the download cache.");
            if (!file_exists(dxgi))
                fail(L"ReShade setup reported success but dxgi.dll is missing.");
            rec.reshade_by_us = true;
            sink.record_new(dxgi);
            sink.record_new(game_ini);
        }
        // Repair the search-path globs ReShade Setup 6.8 writes malformed
        // (error 123, effects never found). No-op when already sane.
        normalize_search_paths(game_ini, rec, log);

        // 3) Feeder add-on + shader.
        if (rec.is_32bit)
            sink.place(feeder.addon32.local_path, path_combine(game_dir, L"dlss5-feed.addon32"));
        else
            sink.place(feeder.addon64.local_path, path_combine(game_dir, L"dlss5-feed.addon64"));

        std::wstring shaders_dir = path_combine(game_dir, L"reshade-shaders\\Shaders");
        sink.place(feeder.fx_shader.local_path, path_combine(shaders_dir, L"DLSS5_Feed.fx"));

        // 4) RenoDX DLSS5 add-on + NGX DLLs.
        if (rec.is_32bit) {
            // 32-bit game: the 64-bit stack runs inside the host64 helper.
            std::wstring host_dir = path_combine(game_dir, L"host64");
            sink.place(feeder.host64_exe.local_path, path_combine(host_dir, L"dlss5-feed-host64.exe"));

            std::wstring host_dxgi = path_combine(host_dir, L"dxgi.dll");
            if (!file_exists(host_dxgi)) {
                if (!reshade_headless_install(reshade.setup_exe_path,
                                              path_combine(host_dir, L"dlss5-feed-host64.exe"), log))
                    fail(L"ReShade setup for the host64 helper did not complete.");
                sink.record_new(host_dxgi);
                sink.record_new(path_combine(host_dir, L"ReShade.ini"));
            }
            sink.place(renodx.addon64_path, path_combine(host_dir, L"renodx-dlss5.addon64"));
            sink.place(ngx.nr_dll_path, path_combine(host_dir, L"nvngx_dlssnr.dll"));
            sink.place(ngx.sr_dll_path, path_combine(host_dir, L"nvngx_dlss.dll"));
        } else {
            sink.place(renodx.addon64_path, path_combine(game_dir, L"renodx-dlss5.addon64"));
            sink.place(ngx.nr_dll_path, path_combine(game_dir, L"nvngx_dlssnr.dll"));
            sink.place(ngx.sr_dll_path, path_combine(game_dir, L"nvngx_dlss.dll"));
        }

        // 5) LumeniteFX motion-vector provider (recommended, default on).
        if (opts.install_lumenite) {
            log(L"Installing LumeniteFX shaders...");
            for (const auto& src : lumenite.files) {
                std::wstring rel = src.substr(lumenite.staging_dir.size() + 1);
                sink.place(src, path_combine(game_dir, rel));
            }
        } else {
            log(L"LumeniteFX skipped - install a motion-vector provider yourself and set "
                L"DLSS5_MV_PROVIDER accordingly (Kernel = 3).");
        }

        // Point DLSS5_Feed.fx at the LumeniteFX Kernel vectors.
        if (file_exists(game_ini))
            ensure_mv_provider_def(game_ini, rec, log);
        else if (opts.install_lumenite)
            log(L"NOTE: no ReShade.ini yet - set DLSS5_MV_PROVIDER=3 in ReShade's "
                L"preprocessor definitions after the first game run.");
        if (rec.is_32bit) {
            std::wstring host_ini = path_combine(path_combine(game_dir, L"host64"), L"ReShade.ini");
            normalize_search_paths(host_ini, rec, log);
            if (file_exists(host_ini))
                ensure_mv_provider_def(host_ini, rec, log);
        }

        // 6) Optional Vulkan layer.
        if (opts.install_vulkan_layer) {
            if (feeder.vk_layer_zip.local_path.empty())
                log(L"Vulkan layer requested but not published in this Feeder release - skipped.");
            else {
                log(L"Extracting Vulkan layer...");
                auto files = zip_extract_matching(feeder.vk_layer_zip.local_path, game_dir, {L"*"});
                for (const auto& f : files) sink.record_new(f);
                rec.vulkan_layer = true;
                log(L"Vulkan layer installed. If the game misses Vulkan interop extensions, "
                    L"launch it via run-with-feed-layer.bat (or the bat included in the layer).");
            }
        }

        // 7) Record + index.
        record_save(rec);
        index_add(rec);

        log(L"");
        log(L"Install complete. " + std::to_wstring(rec.files.size()) + L" files placed.");
        res.ok = true;
        res.message = L"Installed into " + game_dir;
    } catch (const std::exception& e) {
        res.ok = false;
        res.message = to_wide(e.what());
        log(L"");
        log(L"INSTALL FAILED: " + res.message);
        if (!rec.files.empty())
            log(L"Some files were already placed - use Uninstall on this folder to undo them.");
    }
    return res;
}

InstallResult run_uninstall(const std::wstring& game_dir, const LogFn& log) {
    InstallResult res;
    res.game_dir = game_dir;
    try {
        InstallRecord rec;
        if (!record_load(game_dir, rec))
            fail(L"No FeedKit install record found in " + game_dir + L" - nothing to uninstall.");

        log(L"Uninstalling FeedKit files from " + game_dir);

        // Remove in reverse placement order, restoring backups.
        for (auto it = rec.files.rbegin(); it != rec.files.rend(); ++it) {
            if (delete_file(it->path)) {
                log(L"Removed " + it->path);
            } else {
                log(L"Could not remove (locked?) " + it->path);
                continue;
            }
            if (!it->backup.empty() && file_exists(it->backup)) {
                if (copy_file(it->backup, it->path, true)) {
                    delete_file(it->backup);
                    log(L"Restored original " + path_filename(it->path));
                } else {
                    log(L"WARNING: could not restore backup " + it->backup);
                }
            }
        }

        // ReShade leftovers created by its setup that we did not record.
        if (rec.reshade_by_us) {
            for (const wchar_t* extra : {L"ReShade.log", L"ReShade.ini.bak", L"dxgi.log"})
                delete_file(path_combine(game_dir, extra));
        }

        // Put back any ReShade.ini keys we changed (exact-case text restore;
        // skip inis that are gone - recreating them would be wrong).
        for (auto it = rec.ini_touched.rbegin(); it != rec.ini_touched.rend(); ++it) {
            if (!file_exists(it->path))
                continue;
            ini_set_exact(it->path, it->section, it->key, it->original);
            log(L"Restored " + it->key + L" in " + path_filename(it->path));
        }

        // Tidy empty folders we created.
        remove_dir_if_empty(path_combine(game_dir, L"reshade-shaders\\Shaders\\include"));
        remove_dir_if_empty(path_combine(game_dir, L"reshade-shaders\\Shaders"));
        remove_dir_if_empty(path_combine(game_dir, L"reshade-shaders"));
        remove_dir_if_empty(path_combine(game_dir, L"host64"));

        delete_file(rec.record_path());
        index_remove(game_dir);

        log(L"Uninstall complete.");
        res.ok = true;
        res.message = L"Removed FeedKit files from " + game_dir;
    } catch (const std::exception& e) {
        res.ok = false;
        res.message = to_wide(e.what());
        log(L"");
        log(L"UNINSTALL FAILED: " + res.message);
    }
    return res;
}

} // namespace fk
