# Development Workflow

This guide covers the daily development workflow for N64 homebrew.

## Build Cycle

### Basic Workflow

```bash
# 1. Edit code
# 2. Build
libdragon make

# 3. Test in emulator
open -a ares hello_cube.z64

# 4. (Optional) Test on hardware
sc64deployer upload hello_cube.z64
```

### VSCode Tasks

Use VSCode tasks (Cmd+Shift+B) for integrated workflow:

| Task | Shortcut | Action |
|------|----------|--------|
| Build ROM | Cmd+Shift+B | Compile and link |
| Clean Build | - | Remove build artifacts |
| Rebuild | - | Clean + Build |
| Run in ares | - | Build then launch emulator |
| Upload to SummerCart64 | - | Build then upload to cart |

## Debugging

### USB Logging

The main.c includes USB debug initialization:
```c
debug_init_isviewer();
debug_init_usblog();
```

To view logs:
```bash
sc64deployer debug
```

Use `debugf()` in your code for output:
```c
debugf("Player position: %d, %d\n", x, y);
```

### Emulator Debugging

ares has built-in debugging features:
1. Open ares
2. Tools > Tracer (for CPU traces)
3. Tools > Memory (for memory inspection)

### Common Debug Patterns

**Crash on startup:**
- Check memory allocations (use `malloc_uncached` for DMA buffers)
- Verify display initialization parameters

**Graphics issues:**
- Check viewport setup
- Verify matrix stack (push/pop balanced)
- Ensure proper depth buffer attachment

**Input not working:**
- Verify `joypad_init()` called
- Check controller port (JOYPAD_PORT_1)
- Test with different input (analog vs d-pad)

## Performance Profiling

### Frame Timing

Add timing code:
```c
#include <timer.h>

uint32_t start = timer_ticks();
// ... rendering code ...
uint32_t elapsed = timer_ticks() - start;
float ms = (float)elapsed / (TICKS_PER_SECOND / 1000.0f);
debugf("Frame time: %.2f ms\n", ms);
```

### RDP Statistics

Use t3d debug features:
```c
#include <t3d/t3ddebug.h>

// After rdpq_detach_show()
t3d_debug_print_stats();
```

## Asset Pipeline

### ROM Filesystem

Place assets in `filesystem/` directory. They'll be packed into the ROM:

```
filesystem/
├── models/
│   └── player.t3dm
├── textures/
│   └── grass.sprite
└── sounds/
    └── jump.wav64
```

Build with filesystem:
```makefile
# Makefile already includes:
$(BUILD_DIR)/$(TARGET).dfs: $(wildcard filesystem/*) | $(BUILD_DIR)
    $(N64_MKDFS) $@ filesystem/
```

### Loading Assets

```c
// In code:
dfs_init(DFS_DEFAULT_LOCATION);
int fp = dfs_open("/models/player.t3dm");
// ... load data ...
dfs_close(fp);
```

## Version Control

### What to Commit

- `src/` - all source files
- `filesystem/` - game assets
- `Makefile` - build config
- `.vscode/` - editor config
- `docs/` - documentation

### What to Ignore

The libdragon submodule is initialized separately. Your `.gitignore` should include:

```
# Build artifacts
build/
*.z64
*.elf
*.dfs

# Editor
.vscode/c_cpp_properties.json

# System
.DS_Store
```

### Workflow with Git

```bash
# Start feature
git checkout -b feature/player-movement

# Work...
libdragon make
# Test...

# Commit
git add src/
git commit -m "Add player movement"

# Push
git push -u origin feature/player-movement
```

## Real Hardware Testing

### SummerCart64 Workflow

1. **Build ROM:**
   ```bash
   libdragon make
   ```

2. **Connect cart:**
   - Power on N64 (or use USB power)
   - Connect USB cable

3. **Upload:**
   ```bash
   sc64deployer upload hello_cube.z64
   ```

4. **Debug (optional):**
   ```bash
   # In separate terminal
   sc64deployer debug
   ```

### Hardware vs Emulator Differences

| Aspect | Emulator | Hardware |
|--------|----------|----------|
| Speed | May be faster/slower | Real timing |
| Accuracy | ~99% (ares) | 100% |
| Input | Keyboard/gamepad | Real controller |
| Debug | Full access | USB log only |

Always test on hardware before considering a feature complete.

## Continuous Development Tips

1. **Build frequently** - Catch errors early
2. **Test in emulator first** - Faster iteration
3. **Use debug logging** - Essential for hardware testing
4. **Profile regularly** - N64 is performance-constrained
5. **Commit working states** - Easy rollback if needed
