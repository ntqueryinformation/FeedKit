# FeedKit

**One-click installer for [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder).**

FeedKit brings DLSS 5 neural rendering (DLAA) to games that ship without any DLSS - **D3D9, D3D11, D3D12, Vulkan and OpenGL** - without the usual manual file shuffle. Pick a game executable, click **Install**, and FeedKit downloads the current files from their official sources and puts everything where it belongs. Click **Uninstall** and it's all gone, with any replaced files restored from backup.

> Native Windows app (Win32 C++ + Dear ImGui). No runtime dependencies - just download `FeedKit.exe` and run it.

![FeedKit](docs/screenshot.png)

---

## Built-in updater

FeedKit checks GitHub for a newer release when it starts. When one exists, an **"Update to vX.Y.Z"** button appears next to the version label - one click downloads the new build, verifies it against the SHA-256 published in the release notes, swaps it in place of the running exe, and offers to restart. Uninstalling the update is as simple as restoring the `FeedKit.exe.old` file the swap leaves behind (deleted automatically on the next start).

## Requirements

- Windows 10 / 11 (x64)
- An NVIDIA RTX GPU supported by DLSS 5 neural rendering
- A game rendering through **Direct3D 9, 11, 12, Vulkan or OpenGL** - 64-bit or 32-bit
- Internet access (FeedKit downloads everything at install time)

### Rendering APIs

| API | Support |
|---|---|
| **D3D11 / D3D12** | Fully automated. ReShade installs as `dxgi.dll`, which covers the whole DXGI family; bitness (32/64-bit) is auto-detected. |
| **OpenGL** | Fully automated. ReShade installs as `opengl32.dll`. The game must run on the NVIDIA GPU (OpenGL interop requires it) - on hybrid graphics force it via Windows Settings > Display > Graphics. |
| **Vulkan** | Supported by DLSS5-Feeder itself - tick the **Vulkan layer** option in FeedKit. If a game misses the required Vulkan interop extensions, launch it through the `run-with-feed-layer.bat` fallback that the layer installs (upstream guidance). |
| **D3D9** | Fully automated via the **D3D9 game** option - FeedKit installs [dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2) (D3D9 -> D3D11 translation), configures it for DLSS5-Feeder, and on first launch you should see the dgVoodoo watermark confirming the translation is active. |

## Quick start

1. Download `FeedKit.exe` from [Releases](../../releases) (or build it yourself, see below).
2. Run it and pick your game's `.exe` - browse for it or just drag it onto the window.
3. Click **Install** and wait. First install downloads roughly 230 MB (the DLSS neural-rendering DLL alone is ~160 MB), so it can take a few minutes.
4. Launch the game and enable the effects in the ReShade overlay (see [After installing](#after-installing)).

## How it works

Nothing is bundled with FeedKit. **Every install fetches the current versions from upstream**, so you always get the latest Feeder build, the latest RenoDX DLSS5 add-on, and the DLSS DLL versions published by RHI:

| Component | Source | Files |
|---|---|---|
| ReShade (add-on support) | [reshade.me](https://reshade.me) | `dxgi.dll` (D3D11/12) or `opengl32.dll` (OpenGL), installed unattended by ReShade's own setup |
| DLSS5-Feeder | latest GitHub release of [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) | `dlss5-feed.addon64` / `dlss5-feed.addon32`, `DLSS5_Feed.fx` |
| RenoDX DLSS5 add-on | newest release in [RankFTW/rhi-repo](https://github.com/RankFTW/rhi-repo/releases) | `renodx-dlss5.addon64` |
| DLSS DLLs | DLSS versions published via [RHI's](https://github.com/RankFTW/RHI) `dlss_manifest.json` | `nvngx_dlssnr.dll` (ShortFuse build), `nvngx_dlss.dll` |
| LumeniteFX (recommended, on by default) | repo of [umar-afzaal/LumeniteFX](https://github.com/umar-afzaal/LumeniteFX) | `lumenite_*.fx`, `include\*.fxh`, bluenoise texture; also sets `DLSS5_MV_PROVIDER=3` in `ReShade.ini` |
| dgVoodoo2 (D3D9 option) | latest release of [dege-diosg/dgVoodoo2](https://github.com/dege-diosg/dgVoodoo2) | `d3d9.dll`, `dgVoodoo.conf` (pre-configured), `dgVoodooCpl.exe` |

## What gets installed where

**64-bit game** - files land next to the game `.exe`:

```
GameFolder\
├─ dxgi.dll                  (ReShade, add-on support)
├─ ReShade.ini               (DLSS5_MV_PROVIDER=3 preset when LumeniteFX is installed)
├─ dlss5-feed.addon64
├─ renodx-dlss5.addon64
├─ nvngx_dlssnr.dll
├─ nvngx_dlss.dll
└─ reshade-shaders\
   ├─ Shaders\
   │  ├─ DLSS5_Feed.fx
   │  ├─ ReShade.fxh, ReShadeUI.fxh, DrawText.fxh   (standard headers)
   │  ├─ lumenite_*.fx               (LumeniteFX)
   │  └─ include\lumenite_*.fxh      (LumeniteFX)
   └─ Textures\
      └─ lumenite_bluenoise256.png   (LumeniteFX)
```

**32-bit game** - the Feeder add-on runs in-process, and the 64-bit stack (RenoDX add-on + NGX DLLs) runs inside the bundled `host64` helper:

```
GameFolder\
├─ dxgi.dll                  (ReShade 32-bit, add-on support)
├─ ReShade.ini
├─ dlss5-feed.addon32
├─ reshade-shaders\          (DLSS5_Feed.fx + LumeniteFX files, as above)
└─ host64\
   ├─ dlss5-feed-host64.exe
   ├─ dxgi.dll               (ReShade 64-bit, add-on support)
   ├─ ReShade.ini
   ├─ renodx-dlss5.addon64
   ├─ nvngx_dlssnr.dll
   └─ nvngx_dlss.dll
```

Optional: the **Vulkan layer** checkbox adds DLSS5-Feeder's Vulkan support (`VkLayer_feed_vk.dll` and its launcher script) for Vulkan games. If a file already exists (for example an `nvngx_dlss.dll` the game shipped with), FeedKit backs it up as `<name>.feedkit.bak` instead of overwriting it.

Everything FeedKit touches is recorded in `feedkit.install.json` in the game folder.

## After installing

The last steps happen in-game (FeedKit can't click ReShade's overlay for you):

1. **Motion vectors:** with the recommended LumeniteFX checkbox left on, the provider shaders are installed and `DLSS5_MV_PROVIDER=3` (LumeniteFX Kernel) is set in `ReShade.ini` for you - nothing to do. If you unchecked it, install a motion-vector provider yourself (LumeniteFX Kernel, or iMMERSE Launchpad / VORT / anything writing `texMotionVectors`) and set `DLSS5_MV_PROVIDER` accordingly in the preprocessor definitions of `DLSS5_Feed.fx`.
2. **Enable in the ReShade overlay** (Home key): enable the *LUMEN* technique, then *DLSS 5 Feed*, then the neural rendering technique. Keep MSAA/SSAA off.
3. **Verify** via `dlss5-feed.log` in the game folder. On D3D9 games, the dgVoodoo watermark in a corner of the image confirms the translation layer is active.

## Uninstalling

Point FeedKit at the same game `.exe` and click **Uninstall**. It removes exactly what it installed (per `feedkit.install.json`) and restores any backups. If FeedKit installed ReShade itself, that goes too; ReShade you had already, and your shader folders, stay.

## Troubleshooting

- **`dlss5-feed.log`** in the game folder is the DLSS5-Feeder log - the [upstream README](https://github.com/jlrouzies-fr/DLSS5-Feeder#troubleshooting) explains its failure modes (missing motion vectors, silent hook misses, dgVoodoo VRAM errors for D3D9 games).
- **`ReShade.log`** covers ReShade/add-on loading.
- **"The game executable is locked"** - the game is still running. Close it first.
- **Install into `C:\Program Files\...` fails** - run FeedKit as administrator so it can write into the game folder.
- **Antivirus flags FeedKit.exe** - expected for game-modding tools and based on behavior/reputation heuristics, not a real threat. FeedKit is fully open source and built publicly on CI. See [docs/ANTIVIRUS.md](docs/ANTIVIRUS.md) for the full explanation, vendor submission status and how to restore the file.
- **D3D9 games**: if you don't see the dgVoodoo watermark in-game, the translation isn't active - the dgVoodooVRAM default (256 MB) can cause "ran out of video memory" errors on some engines; FeedKit sets it to 1 GB automatically.
- FeedKit is single-player only territory: ReShade add-on support can trip anti-cheat in online games. Don't use it there.

## Building from source

1. Open `FeedKit.sln` in Visual Studio 2022 (Desktop C++ workload).
2. Build - that's it. No NuGet, no vcpkg, no external dependencies (Dear ImGui and miniz are vendored under `external/`, CRT is statically linked).

Or from the command line:

```
msbuild FeedKit.sln /p:Configuration=Release /p:Platform=x64
```

Output: `build\Release\FeedKit.exe`.

## Credits and legal

- [DLSS5-Feeder](https://github.com/jlrouzies-fr/DLSS5-Feeder) by jlrouzies-fr (MIT) - the actual magic. FeedKit is just an installer for it.
- [LumeniteFX](https://github.com/umar-afzaal/LumeniteFX) by umar-afzaal - the recommended motion-vector provider, installed by default.
- [RHI / RenoDX Commander](https://github.com/RankFTW/RHI) and the [rhi-repo](https://github.com/RankFTW/rhi-repo) release feed - source of the RenoDX DLSS5 add-on and the DLSS NR/SR DLL versions.
- [ReShade](https://reshade.me) by crosire - downloaded and installed from the official site; its binaries are not redistributed here.
- [RenoDX](https://github.com/clshortfuse/renodx) by ShortFuse and the RenoDX team.
- [miniz](https://github.com/richgel999/miniz) (MIT), vendored under `external/miniz`.

FeedKit is an unofficial community tool, MIT licensed (see [LICENSE](LICENSE)). Not affiliated with or endorsed by NVIDIA. NVIDIA, DLSS, and NGX are trademarks of NVIDIA Corporation; the DLSS DLLs are downloaded from the RHI project's release feed at install time and are not redistributed by this repository.
