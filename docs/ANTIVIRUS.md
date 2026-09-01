# Antivirus flagging

FeedKit is a game-modding tool: it downloads ReShade and DLSS5-Feeder components,
and installs proxy DLLs (`dxgi.dll`, `d3d9.dll`, `opengl32.dll`) into game folders -
which is exactly how ReShade itself has always worked. Some antivirus engines
classify any tool with this behavior profile under generic game-hack or
reputation-based labels.

**The detection is a false positive.** FeedKit is fully open source (MIT), builds
publicly on GitHub Actions from this repository, performs no telemetry, no
persistence, no network communication other than downloading the mod components
listed in the README, and never touches anything outside the game folder you
select.

## Current status

| Vendor | Detection | Status |
|---|---|---|
| Microsoft | Trojan:Win32/Wacatac.B!ml | False positive submitted |
| ESET-NOD32 | Win64/GameHack_AGen.BHH | False positive submitted |
| Sophos | Generic Reputation PUA | False positive submitted |
| SecureAge | Malicious | False positive submitted |
| Arctic Wolf | Unsafe | Uses third-party feeds - clears when the above clear |

These are ML/reputation verdicts on an unsigned binary from a new project, not
signatures of actual malicious behavior. We are in contact with the vendors and
plan to ship code-signed releases (free SignPath Foundation certificate for open
source) to address this permanently.

## What you can do

- Verify the binary: releases state the exact SHA-256 in the notes, and every
  release is built by this repository's public CI.
- Restore the file from your antivirus quarantine and add an exclusion for the
  FeedKit folder if your AV keeps removing it.
- Wait for the vendor review - detections based purely on reputation/ML are
  routinely removed once reviewed.

---

# False-positive submission drafts

Replace `94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76` with the hash of the exact file you scanned (each release lists
it in the notes), and fill in where you downloaded it from.

## Common information

```
Project:        FeedKit (https://github.com/ntqueryinformation/FeedKit)
File:           FeedKit.exe (also distributed as a GitHub release asset)
SHA-256:        94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76
License:        MIT, full source code public
Purpose:        FeedKit is an installer/uninstaller for the open-source
                "DLSS5-Feeder" mod (github.com/jlrouzies-fr/DLSS5-Feeder), which
                adds NVIDIA DLSS features to games that do not ship with them.

Why it is flagged: The tool performs actions that are common to game modding and
unfortunately also to malware: it downloads mod components from their official
sources (reshade.me, GitHub releases), installs ReShade's proxy DLLs
(dxgi.dll / opengl32.dll / d3d9.dll) into a user-selected game folder, and runs
the official ReShade installer unattended against that game. It performs no
telemetry, no persistence, no privilege escalation, and writes only inside the
selected game folder and its own log/cache directories under %LOCALAPPDATA%.

Detection appears to be reputation/ML-based (unsigned binary, new project) and
the behavioral category "game modification", not any actual malicious capability.
The full source is available for review and the binary is built publicly on
GitHub Actions from this source.
```

## Microsoft (Trojan:Win32/Wacatac.B!ml)

Submit at: https://www.microsoft.com/en-us/wdsi/filesubmission
(choose "Software developer", sign in with a Microsoft account)

```
FeedKit is an open-source (MIT-licensed) installer for the DLSS5-Feeder game
mod, distributed from https://github.com/ntqueryinformation/FeedKit and built
publicly on this repository's GitHub Actions. Source code:
https://github.com/ntqueryinformation/FeedKit

The detection appears to be a machine-learning false positive based on the
application's purpose: it installs the well-known ReShade injector
(reshade.me) and the open-source DLSS5-Feeder add-on into a user-selected game
folder. This is standard game-modding behavior (same as the official ReShade
installer, which is not detected). The tool performs no telemetry, no
persistence, no privilege escalation and no communication other than
downloading mod components over HTTPS from their official sources.

Please remove the detection for 94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76. I am the developer and can provide
build reproducibility information on request.
```

## ESET-NOD32 (Win64/GameHack_AGen.BHH Potentially Unsafe Application)

Submit at: https://www.eset.com/int/false-positive-report/

```
Sample: FeedKit.exe, 94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76
Source: https://github.com/ntqueryinformation/FeedKit (MIT license, public
GitHub Actions build)

FeedKit is an open-source installer for the DLSS5-Feeder ReShade add-on
(github.com/jlrouzies-fr/DLSS5-Feeder). The GameHack classification appears to
be behavioral: the tool installs ReShade's proxy DLLs (dxgi.dll / opengl32.dll)
into a user-selected single-player game folder so the DLSS5-Feeder add-on can
load, which is standard game-modding practice (ReShade itself is distributed
this way and is not detected).

The tool is signed-agnostic open source, performs no injection into running
processes, no persistence, no telemetry, and only writes inside the selected
game folder and %LOCALAPPDATA%\FeedKit. Please re-classify or whitelist as a
legitimate game modding utility. I am the developer.
```

## Sophos (Generic Reputation PUA)

Submit at: https://support.sophos.com/support/s/filesubmission

```
FeedKit.exe (94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76) is a false positive "Generic Reputation PUA". It is an
open-source (MIT) game-mod installer from https://github.com/ntqueryinformation/FeedKit,
built publicly on GitHub Actions. The low reputation score reflects a new
project, not malicious behavior: the application installs the well-known
ReShade mod loader into a user-selected game folder on explicit user action.
No telemetry, no persistence, no hidden behavior - full source is available
for review. Please clear the reputation detection. I am the developer.
```

## SecureAge (Malicious)

Submit via: https://www.secureage.com/ (contact / false positive form)

```
FeedKit.exe (94F3AF218851D3D3A18B1364AC40F5934D2450B51F8FD161999BA9AD3622DF76) is falsely detected as malicious. It is an open-source
(MIT) installer for the DLSS5-Feeder game mod, source and public CI builds at
https://github.com/ntqueryinformation/FeedKit. It installs ReShade
(reshade.me) proxy DLLs into a user-selected game folder on explicit user
action - standard game-modding behavior. No telemetry, no persistence, no
network activity beyond downloading mod components from their official GitHub
sources. I am the developer; happy to provide any additional information.
```
