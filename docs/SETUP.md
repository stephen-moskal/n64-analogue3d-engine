# Environment Setup Guide

This guide sets up the N64 homebrew toolchain for this engine on **Windows 11** (primary, verified 2026-09-12) and **macOS** (the original development platform). Both use the same build path: the `libdragon` CLI runs `make` inside a Docker container that holds the MIPS toolchain and the vendored libdragon submodule.

```
edit src/ -> libdragon make -> engine-debug.z64 -> ares (emulator)
                                              -> sc64deployer upload -> reset the console (Analogue 3D + SummerCart64)
```

---

## Windows 11

### 0. Requirements

- Windows 11 with hardware virtualization enabled in firmware (Docker Desktop needs it; if `wsl --status` later says "virtualization is not enabled", enable Intel VT-x / AMD-V in the BIOS).
- [Git for Windows](https://git-scm.com/download/win) and `winget` (built into Windows 11).
- A terminal: Windows PowerShell 5.1 or PowerShell 7. Commands below are PowerShell unless noted. Steps marked **(admin)** need an elevated prompt (UAC).

### 1. WSL2 (admin, reboot)

Docker Desktop uses the WSL2 backend. No Linux distribution is needed.

```powershell
wsl --install --no-distribution
# "Changes will not be effective until the system is rebooted." -> reboot now
wsl --status        # after the reboot: "Default Version: 2"
```

### 2. Docker Desktop (admin)

```powershell
winget install -e --id Docker.DockerDesktop
```

Launch Docker Desktop, accept the terms, keep the default WSL2 engine. Verify from a new terminal:

```powershell
docker --version              # 27.2 or newer is required by the libdragon CLI
docker run --rm hello-world
```

### 3. Node.js 24 + libdragon CLI

```powershell
winget install -e --id OpenJS.NodeJS.LTS      # Node >= 24 is required by the CLI
# open a NEW terminal so PATH picks up Node
npm install -g libdragon
libdragon version                              # e.g. libdragon-cli v12.2.1
```

`libdragon` is installed as `%APPDATA%\npm\libdragon.ps1`; that folder is on the user PATH.

### 4. ares emulator

```powershell
winget install -e --id ares-emulator.ares
```

winget installs the portable build under `%LOCALAPPDATA%\Microsoft\WinGet\Packages\ares-emulator.ares_...\ares-v148\` and links `ares.exe` into `%LOCALAPPDATA%\Microsoft\WinGet\Links` (on PATH in new terminals). In ares, open **Settings -> Options** and turn on **Homebrew Mode** (libdragon needs it for ISViewer log output and other developer features).

### 5. SummerCart64 deployer + USB driver

1. Download `sc64-deployer-windows-vX.Y.Z.zip` from the [SummerCart64 releases](https://github.com/Polprzewodnikowy/SummerCart64/releases) (v2.20.2 verified), extract it to `C:\tools\sc64deployer\`, and add that folder to your user PATH:
   ```powershell
   [Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path','User') + ';C:\tools\sc64deployer', 'User')
   ```
2. **USB driver.** The cart's FT232H shows up as an unknown "SC64" device (Device Manager problem code 28) until the FTDI driver is installed. Download the **CDM WHQL Certified** driver zip from [ftdichip.com -> Drivers -> VCP](https://ftdichip.com/drivers/vcp-drivers/) (2.12.36.20 verified; the site blocks scripted downloads, use a browser), extract it, and install both INFs from an **admin** PowerShell:
   ```powershell
   pnputil /add-driver "$env:USERPROFILE\Downloads\CDM-v2.12.36.20-WHQL-Certified\ftdibus.inf"  /install
   pnputil /add-driver "$env:USERPROFILE\Downloads\CDM-v2.12.36.20-WHQL-Certified\ftdiport.inf" /install
   ```
   Device Manager should now show **USB Serial Converter** and **USB Serial Port (COMx)**. (Right-clicking each INF -> *Install* does the same.)
3. Verify with the cart connected (the console can be off; the cart runs on USB power):
   ```powershell
   sc64deployer list
   #  1: [SC64XXXXXXA] at port [serial://COM3] (using "serial" backend)
   sc64deployer info      # firmware version, boot mode, save type...
   ```

### 6. Clone and fix line endings (important)

The container is Linux: `make` and `bash` read `Makefile`, `n64.mk` and `build.sh` straight from the bind-mounted working tree, and **CRLF line endings break them**. Git for Windows defaults to `core.autocrlf=true`. The repo's `.gitattributes` forces LF for the project files, but the libdragon submodule has its own attributes, so configure both once:

```powershell
git clone --recurse-submodules https://github.com/<you>/n64-analogue3d-engine.git
cd n64-analogue3d-engine

git config core.autocrlf false; git config core.eol lf
git -C libdragon config core.autocrlf false; git -C libdragon config core.eol lf

# re-checkout the working trees with LF (safe on a clean tree)
git rm -r --cached -q . ; git reset --hard -q
git -C libdragon rm -r --cached -q . ; git -C libdragon reset --hard -q

git ls-files --eol Makefile                    # want: i/lf  w/lf
git -C libdragon ls-files --eol n64.mk         # want: i/lf  w/lf
```

(Alternative: `git config --global core.autocrlf false` before cloning.)

### 7. Build

```powershell
libdragon init      # existing project: pulls ghcr.io/dragonminded/libdragon:latest, creates the
                    # container, and compiles the vendored libdragon submodule into it (~1 min on 16 cores)
libdragon make                  # debug build   -> engine-debug.z64 (~5 s warm)
libdragon make BUILD=release    # release build -> engine.z64
```

Expected tail of the build output:

```
    [CC] src/main.c
    ... (22 modules) ...
    [LD] build/debug/engine-debug.elf
      text       data        bss      total filename
    311992      95640      35320     442952 build/debug/engine-debug.elf
    [DFS] build/debug/engine-debug.dfs
    [Z64] engine-debug.z64
```

`libdragon make` does **not** rebuild libdragon itself; after changing the submodule (or its commit) run `libdragon install`, then `libdragon make clean` so the generated assets are rebuilt with the matching tools.

### 8. Run

```powershell
ares .\engine-debug.z64                       # emulator
sc64deployer upload .\engine-debug.z64        # cart (0.2 s); then power on / reset the console
sc64deployer debug                          # in a second terminal: shows debugf()/usblog output from the ROM
```

The ROM prints `SMozN64 Dev Engine` at startup (after the texture/audio load lines), so `sc64deployer debug` (cart) and the ares terminal with Homebrew Mode (ISViewer) should both show it.

### 9. VS Code

`.vscode/tasks.json` has per-OS commands: **Build ROM** (`Ctrl+Shift+B`), **Clean Build**, **Rebuild**, **Run in ares**, **Upload to SummerCart64**, **Debug (USB Log)**. Restart VS Code after installing tools so its terminals see the new PATH.

The `.devcontainer/` config (image `ghcr.io/dragonminded/libdragon:preview`) is an alternative for editing with IntelliSense inside the container; note its image tag differs from `.libdragon/config.json` (`:latest`). The container is only a toolchain; `sc64deployer` and ares always run on the Windows host.

---

## macOS

```bash
brew install --cask docker        # Docker Desktop
brew install --cask ares          # emulator (enable Homebrew Mode in Settings -> Options)
brew install node                 # Node >= 24
npm install -g libdragon
# sc64deployer: download the macOS tgz from the SummerCart64 releases, then
mv sc64deployer /usr/local/bin/ && chmod +x /usr/local/bin/sc64deployer

git clone --recurse-submodules <repo> && cd n64-analogue3d-engine
libdragon init && libdragon make
open -a ares engine-debug.z64
sc64deployer upload engine-debug.z64   # then reset the console
```

Apple Silicon: the image is linux/amd64 and runs under Rosetta; builds are a little slower but work.

---

## Troubleshooting

**`ASSERTION FAILED: wav64 rom:/audio/sfx/...: invalid version` (or a sprite/DFS assert) at boot, in ares and on hardware.** Generated assets in `filesystem/` are stale: their binary format is tied to the libdragon version that produced them. Rebuild them with the container's tools: `libdragon make clean; libdragon make`. Generated `*.sprite` / `*.wav64` files are no longer tracked in git for this reason.

**`libdragon` is not recognized.** Open a new terminal (PATH), check that `%APPDATA%\npm` is on the user PATH, and that PowerShell's execution policy allows the `libdragon.ps1` shim (`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`).

**`docker: error during connect` / "WSL2 is unable to start since virtualization is not enabled".** Docker Desktop is not running, or the reboot after `wsl --install` has not happened, or virtualization is off in firmware. `wsl --status` should report default version 2.

**`make: *** missing separator`, paths ending in `\r`, `bad interpreter: ^M`.** CRLF line endings reached the container. Redo step 6, including the submodule.

**`n64.mk: No such file` / `mips64-elf-gcc: not found`.** The container has no libdragon installed: run `libdragon init` (or `libdragon install`).

**libdragon itself fails to compile during `libdragon init` with `-Werror` warnings.** The image's GCC is newer than the toolchain the pinned submodule commit was written for (GCC 16.2 compiled the 2026-02 commit cleanly, so this is only a fallback). From `libdragon/`, run `libdragon exec bash -c "make -j8 install-mk && make -j8 libdragon tools LIBDRAGON_CFLAGS=-Wno-error && make install tools-install"`, or switch `imageName` in `.libdragon/config.json` to `ghcr.io/dragonminded/libdragon:preview`.

**`sc64deployer list` -> "No SC64 devices found".** No FTDI driver (Device Manager: "SC64" with problem code 28): do step 5. If only "USB Serial Converter" appears without a COM port: Device Manager -> USB Serial Converter -> Properties -> Advanced -> tick **Load VCP**, then unplug/replug. Also check the USB cable (data, not charge-only).

**`sc64deployer debug` prints `Started` then `Stopped` immediately.** It exits when its stdin closes; run it in an interactive terminal (or `sleep 900 | sc64deployer debug` from a script).

**Upload fails with the port busy.** Another `sc64deployer debug` is holding the COM port; stop it first.

**ares boots the ROM but shows no log output.** Enable Homebrew Mode (Settings -> Options); ISViewer output is only routed with it on.

**Slow builds.** Windows bind mounts are slower than native, but this project builds in about 5 s warm. If it ever matters, clone the repo inside the WSL2 filesystem and use VS Code Remote-WSL; the CLI works the same there.
