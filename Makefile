# SMozN64 Engine
#
#   make                  debug build   -> engine-debug.z64  (asserts, RDP validator, profiler)
#   make BUILD=release    release build -> engine.z64        (debug code compiled out)
#   make BENCH=1          debug build that boots straight into the full benchmark
#                         -> engine-debug-bench.z64 (own build dir: build/debug-bench)
#   make BENCH=1 BENCH_KIND=AUDIO   ... into one benchmark kind instead of All
#   make BENCH=1 BENCH_VALIDATOR=1  ... with the RDP validator on (Debug > RDP Check)
#   make BENCH=1 BENCH_RDPLOG=1     ... capturing one frame of RDP commands per step
#   make LAYOUT_PAD=448   moves every function and static variable that is not pinned
#                         by 448 bytes: a layout-stability test (docs/HARDWARE.md, D34, D35)
#   make HEAP_PAD=N       moves every heap block (framebuffers, command buffers) by N bytes
#                         of RDRAM: a placement test (D37)
#   make TOUR=1           debug build that walks the demo through a scripted screenshot
#                         tour -> engine-debug-tour.z64 (build/debug-tour; docs/DEBUGGING.md)
#
# Via the Docker toolchain: `libdragon make` / `libdragon make BUILD=release`.

BUILD ?= debug
ifeq ($(filter $(BUILD),debug release),)
$(error BUILD must be 'debug' or 'release' (got '$(BUILD)'))
endif

# BENCH=1 boots straight into a benchmark run (unattended capture): All, or
# the kind named by BENCH_KIND (a BenchKind name without BENCH_, e.g. AUDIO).
# It builds into its own directory and ROM, so the normal build is untouched.
BENCH_SUFFIX := $(if $(filter 1,$(BENCH)),-bench,)
BENCH_KIND ?= ALL
BENCH_VALIDATOR ?= 0
BENCH_RDPLOG ?= 0

# TOUR=1: the demo walks through a fixed list of states on a timer (options,
# the Start menu, overlay pages, the dialog, a benchmark) for screenshots
# without a controller (src/scenes/demo_tour.c). Own directory and ROM too.
TOUR_SUFFIX := $(if $(filter 1,$(TOUR)),-tour,)
VARIANT_SUFFIX := $(BENCH_SUFFIX)$(TOUR_SUFFIX)

BUILD_DIR  = build/$(BUILD)$(VARIANT_SUFFIX)
SOURCE_DIR = src

# libdragon APIs still marked "preview" are allowed, but each use warns, so
# the engine's dependence on unstable API stays visible (0 = error, 2 = silent).
LIBDRAGON_PREVIEW = 1

include $(N64_INST)/include/n64.mk

ifeq ($(BUILD),release)
ROM_NAME = engine$(VARIANT_SUFFIX)
CFLAGS  += -DNDEBUG -DLIBDRAGON_PROFILE=0 -DENGINE_DEBUG=0 -DENGINE_PROFILE=0
else
ROM_NAME = engine-debug$(VARIANT_SUFFIX)
CFLAGS  += -DENGINE_DEBUG=1 -DENGINE_PROFILE=1
endif
CFLAGS  += -DENGINE_TOUR=$(if $(filter 1,$(TOUR)),1,0)

N64_ROM_TITLE    = "SMozN64 Engine"
N64_ROM_SAVETYPE = none

CFLAGS += -I$(SOURCE_DIR)

# Build options (make VAR=value):
#   SND_OPUS=1   links libdragon's Opus decoder (about 94 KB more RAM) so music
#                encoded with --wav-compress 3 plays (AUDIOCONV_MUSIC_FLAGS
#                below); the audio benchmark then measures Opus too
SND_OPUS ?= 0
CFLAGS += -DSND_ENABLE_OPUS=$(SND_OPUS)
#   LAYOUT_PAD=N adds N unused bytes (a multiple of 4) after the hot text and
#                after each pinned data group (src/engine/layout_pad.c), moving
#                all other code and static data: with the render path pinned
#                (hot_text.ld, hot_data.ld) nothing measured should change
LAYOUT_PAD ?= 0
CFLAGS += -DENGINE_LAYOUT_PAD=$(LAYOUT_PAD)
#   HEAP_PAD=N   allocates N bytes first thing in engine_init, so every later
#                heap block (framebuffers, libdragon's command buffers) sits N
#                bytes further into RDRAM; code and static data do not move
HEAP_PAD ?= 0
CFLAGS += -DENGINE_HEAP_PAD=$(HEAP_PAD)

# make does not track CFLAGS: the option values are kept in a stamp file,
# rewritten only when they change, that every object and the DFS depend on
BUILD_OPTIONS := SND_OPUS=$(SND_OPUS) LAYOUT_PAD=$(LAYOUT_PAD) HEAP_PAD=$(HEAP_PAD)$(if $(filter 1,$(BENCH)), BENCH_KIND=$(BENCH_KIND) BENCH_VALIDATOR=$(BENCH_VALIDATOR) BENCH_RDPLOG=$(BENCH_RDPLOG))
OPTIONS_STAMP := $(BUILD_DIR)/options.stamp
$(shell mkdir -p $(BUILD_DIR) && (echo '$(BUILD_OPTIONS)' | cmp -s - $(OPTIONS_STAMP) || echo '$(BUILD_OPTIONS)' > $(OPTIONS_STAMP)))

ifeq ($(BENCH),1)
CFLAGS += -DENGINE_BOOT_BENCHMARK=1 -DENGINE_BOOT_BENCHMARK_KIND=BENCH_$(BENCH_KIND) -DENGINE_BOOT_VALIDATOR=$(BENCH_VALIDATOR)
CFLAGS += -DENGINE_BENCH_RDPLOG=$(BENCH_RDPLOG)
endif

# All sources under src/ (one directory level deep)
SRCS := $(wildcard $(SOURCE_DIR)/*.c $(SOURCE_DIR)/*/*.c)
OBJS := $(SRCS:$(SOURCE_DIR)/%.c=$(BUILD_DIR)/%.o)
$(OBJS): $(OPTIONS_STAMP)

# Asset conversion — sprites
assets_png  = $(wildcard assets/*.png)
assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))

MKSPRITE_FLAGS ?= --format RGBA16

# Asset conversion — audio
assets_sfx_wav     = $(wildcard assets/audio/sfx/*.wav)
assets_music_wav   = $(wildcard assets/audio/music/*.wav)
assets_music_xm    = $(wildcard assets/audio/music/*.xm)
assets_sfx_wav64   = $(addprefix filesystem/audio/sfx/,$(notdir $(assets_sfx_wav:%.wav=%.wav64)))
assets_music_wav64 = $(addprefix filesystem/audio/music/,$(notdir $(assets_music_wav:%.wav=%.wav64)))
assets_music_xm64  = $(addprefix filesystem/audio/music/,$(notdir $(assets_music_xm:%.xm=%.xm64)))

# Asset conversion — fonts. Monochrome and without outline: outlined fonts
# fake transparency with coverage blending, which leaves the alpha bit clear
# when text is rendered into a cached UI layer (docs/UI.md)
assets_font_ttf  = $(wildcard assets/fonts/*.ttf)
assets_font64    = $(addprefix filesystem/fonts/,$(notdir $(assets_font_ttf:%.ttf=%.font64)))
MKFONT_FLAGS    ?= --monochrome --range 20-7F

# Dialog: assets/dialog/*.json -> filesystem/dialog/*.dlg (tools/dialog_build.py,
# format in docs/DIALOG.md). The tool checks every reference and fails the build
# with the JSON location of the mistake.
assets_dialog_json = $(wildcard assets/dialog/*.json)
assets_dialog      = $(addprefix filesystem/dialog/,$(notdir $(assets_dialog_json:%.json=%.dlg)))

# Debug-only data, built under $(DEBUG_FS)/ instead of filesystem/: a debug ROM
# packs a staging copy of filesystem/ plus these files, a release ROM packs
# filesystem/ alone. Now: the demo track in the other encodings the audio
# benchmark compares (Bench = Audio; it skips a track that is not packed).
DEBUG_FS  := $(BUILD_DIR)/fs-debug
DFS_STAGE := $(BUILD_DIR)/fs-stage
ifeq ($(BUILD),debug)
debug_assets := $(DEBUG_FS)/audio/bench/demo_raw.wav64
ifeq ($(SND_OPUS),1)
debug_assets += $(DEBUG_FS)/audio/bench/demo_opus.wav64
endif
endif

# Audio encodings (audioconv64 --wav-compress: 0 raw, 1 VADPCM, 2 ULC, 3 Opus).
# VADPCM is libdragon's default: about 4:1 smaller than raw and decoded by the
# RSP mixer. docs/AUDIO.md has the measured cost of each (Bench = Audio).
AUDIOCONV_SFX_FLAGS   ?= --wav-compress 1
AUDIOCONV_MUSIC_FLAGS ?= --wav-compress 1

all: $(ROM_NAME).z64
.DEFAULT_GOAL := all

# Sprite conversion rule: assets/*.png -> filesystem/*.sprite
filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"

# Audio conversion rules
filesystem/audio/sfx/%.wav64: assets/audio/sfx/%.wav
	@mkdir -p $(dir $@)
	@echo "    [WAV64] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_SFX_FLAGS) -o $(dir $@) "$<"

filesystem/audio/music/%.wav64: assets/audio/music/%.wav
	@mkdir -p $(dir $@)
	@echo "    [WAV64] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_MUSIC_FLAGS) -o $(dir $@) "$<"

# audioconv64 names its output after the input file: convert into a scratch
# directory, then rename
$(DEBUG_FS)/audio/bench/demo_raw.wav64: assets/audio/music/demo.wav
	@mkdir -p $(dir $@) $(BUILD_DIR)/audio_raw
	@echo "    [WAV64] $@"
	@$(N64_AUDIOCONV) --wav-compress 0 -o $(BUILD_DIR)/audio_raw "$<"
	@mv $(BUILD_DIR)/audio_raw/demo.wav64 $@

$(DEBUG_FS)/audio/bench/demo_opus.wav64: assets/audio/music/demo.wav
	@mkdir -p $(dir $@) $(BUILD_DIR)/audio_opus
	@echo "    [WAV64] $@"
	@$(N64_AUDIOCONV) --wav-compress 3 -o $(BUILD_DIR)/audio_opus "$<"
	@mv $(BUILD_DIR)/audio_opus/demo.wav64 $@

filesystem/fonts/%.font64: assets/fonts/%.ttf
	@mkdir -p $(dir $@)
	@echo "    [FONT] $@"
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o $(dir $@) "$<"

filesystem/dialog/%.dlg: assets/dialog/%.json tools/dialog_build.py
	@mkdir -p $(dir $@)
	@echo "    [DIALOG] $@"
	@python3 tools/dialog_build.py "$<" -o $@

filesystem/audio/music/%.xm64: assets/audio/music/%.xm
	@mkdir -p $(dir $@)
	@echo "    [XM64] $@"
	@$(N64_AUDIOCONV) -o $(dir $@) "$<"

# Hot text and hot data (ROADMAP_v2 D25, D34). The per-triangle render path is
# linked as one contiguous block so it never collides with itself in the 16 KB
# direct-mapped I-cache (src/engine/hot_text.ld), and the static data the
# drawing loops touch is pinned to fixed colours of the 8 KB direct-mapped
# D-cache, away from the render stack (src/engine/hot_data.ld). The link script
# is libdragon's n64.ld with both inserted by src/engine/engine_ld.awk; it is
# regenerated when any of them changes, and the build fails if an anchor is
# missing. tools/hot_text.py and tools/hot_data.py check the result.
ENGINE_LD    := $(BUILD_DIR)/engine.ld
HOT_TEXT_LD  := $(SOURCE_DIR)/engine/hot_text.ld
HOT_DATA_LD  := $(SOURCE_DIR)/engine/hot_data.ld
ENGINE_LD_AWK := $(SOURCE_DIR)/engine/engine_ld.awk
N64_LDFLAGS  := $(subst -Tn64.ld,-T$(ENGINE_LD),$(N64_LDFLAGS))

$(ENGINE_LD): $(N64_LIBDIR)/n64.ld $(HOT_TEXT_LD) $(HOT_DATA_LD) $(ENGINE_LD_AWK)
	@mkdir -p $(dir $@)
	@echo "    [LDSCRIPT] $@"
	@awk -v hot_text="$(HOT_TEXT_LD)" -v hot_data="$(HOT_DATA_LD)" -f $(ENGINE_LD_AWK) \
		$(N64_LIBDIR)/n64.ld > $@.tmp && mv $@.tmp $@

$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

$(BUILD_DIR)/$(ROM_NAME).dfs: $(assets_conv) $(assets_sfx_wav64) $(assets_music_wav64) $(assets_music_xm64) $(assets_font64) $(assets_dialog) \
                              $(debug_assets) $(OPTIONS_STAMP)
ifeq ($(BUILD),debug)
	@mkdir -p $(dir $@)
	@echo "    [DFS] $@"
	@rm -rf $(DFS_STAGE) && mkdir -p $(DFS_STAGE)
	@cp -r $(N64_MKDFS_ROOT)/. $(DFS_STAGE)/
	$(if $(debug_assets),@cd $(DEBUG_FS) && cp --parents $(patsubst $(DEBUG_FS)/%,%,$(debug_assets)) $(abspath $(DFS_STAGE))/)
	@$(N64_MKDFS) $@ "$(DFS_STAGE)" >/dev/null
endif
# (release: n64.mk's rule packs $(N64_MKDFS_ROOT), i.e. filesystem/)
$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS) $(ENGINE_LD)

# Header dependency tracking (n64.mk compiles with -MMD)
-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/*/*.d)

clean:
	rm -rf build *.z64 *.elf *.dfs filesystem/*.sprite filesystem/audio filesystem/fonts filesystem/dialog

.PHONY: all clean
