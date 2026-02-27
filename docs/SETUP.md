# Environment Setup Guide

This guide covers setting up N64 homebrew development on macOS.

## Prerequisites

### 1. Docker Desktop

The libdragon toolchain runs in a Docker container, which provides a consistent cross-compilation environment.

```bash
brew install --cask docker
```

After installation, launch Docker Desktop and ensure it's running.

### 2. ares Emulator

ares is an accurate multi-system emulator with excellent N64 support.

```bash
brew install --cask ares
```

### 3. libdragon CLI

The libdragon CLI manages the Docker container and build process.

```bash
# Install via npm (requires Node.js)
npm install -g libdragon
```

Verify installation:
```bash
which libdragon
```

### 4. SummerCart64 Tools (Optional)

If you have a SummerCart64 flash cart for real hardware testing:

1. Download `sc64deployer` from [SummerCart64 Releases](https://github.com/Polprzewodnikowy/SummerCart64/releases)
2. Extract and move to a directory in your PATH:
   ```bash
   mv sc64deployer /usr/local/bin/
   chmod +x /usr/local/bin/sc64deployer
   ```

## Project Setup

### Initialize libdragon

```bash
cd /path/to/n64-dev-engine

# Initialize project (downloads Docker image ~2GB)
libdragon init
```

This will:
- Pull the libdragon Docker image
- Set up the build environment

## VSCode Setup

### DevContainer (Recommended)

The project includes DevContainer configuration for VSCode:

1. Install the "Dev Containers" extension in VSCode
2. Open the project folder
3. Click "Reopen in Container" when prompted (or use Command Palette: "Dev Containers: Reopen in Container")

This provides:
- Full IntelliSense for libdragon APIs
- Integrated terminal with toolchain
- Proper include paths

### Manual Setup

If not using DevContainer, configure VSCode manually:

1. Install "C/C++" extension
2. The provided `.vscode/settings.json` includes basic paths
3. Note: Full IntelliSense requires the Docker container running

## Verification

### Test Build

```bash
libdragon make
```

Expected output:
```
    [CC] src/main.c
    [CC] src/cube.c
    [CC] src/lighting.c
    [CC] src/input.c
    [LD] build/hello_cube.elf
    [DFS] build/hello_cube.dfs
    [Z64] hello_cube.z64
```

### Test in Emulator

```bash
open -a ares hello_cube.z64
```

You should see a rotating colored cube.

### Test on Hardware (if available)

```bash
sc64deployer upload hello_cube.z64
```

## Troubleshooting

### Docker Issues

**"Cannot connect to Docker daemon"**
- Ensure Docker Desktop is running
- Try: `docker info` to verify

**"No matching manifest for linux/arm64"**
- Some Docker images don't support Apple Silicon
- The latest libdragon image should work

### Build Errors

**"n64.mk: No such file"**
- Run `libdragon init` to set up the project

**Compilation errors in source**
- Check that all source files use libdragon APIs correctly
- Verify include paths in Makefile

### Emulator Issues

**ares won't load ROM**
- Ensure file is `.z64` format (not `.n64` or `.v64`)
- Check ROM size (should be multiple of 4KB)

**ares not found**
- Install via: `brew install --cask ares`
- Or download from: https://ares-emu.net/

### SummerCart64 Issues

**"Device not found"**
- Check USB connection
- Try different USB port
- Verify cart is powered (N64 on, or use external power)

## Apple Silicon Notes

On M1/M2/M3 Macs:
- Docker Desktop runs Linux containers via emulation
- Build times may be slightly slower than Intel
- The standard libdragon Docker image works (linux/amd64 via Rosetta)
- Some experimental features (like tiny3d preview images) may not be available
