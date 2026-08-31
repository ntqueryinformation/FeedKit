// FeedKit - install.h
// Install / uninstall orchestration for a target game.
#pragma once

#include <string>
#include <functional>

namespace fk {

using LogFn = std::function<void(const std::wstring&)>;
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;

struct InstallOptions {
    std::wstring game_exe;
    bool install_lumenite = true;      // recommended, on by default in the GUI
    bool install_vulkan_layer = false;
};

struct InstallResult {
    bool ok = false;
    std::wstring message;   // human-readable summary / error
    std::wstring game_dir;
};

// Detects whether ReShade (any version) already lives next to the game.
bool reshade_present(const std::wstring& game_dir);

InstallResult run_install(const InstallOptions& opts, const LogFn& log, const ProgressFn& progress);
InstallResult run_uninstall(const std::wstring& game_dir, const LogFn& log);

} // namespace fk
