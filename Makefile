# SMozN64 Engine
#
#   make                  debug build   -> engine-debug.z64  (asserts, RDP validator, profiler)
#   make BUILD=release    release build -> engine.z64        (debug code compiled out)
#   make BENCH=1          debug build that boots straight into the full benchmark
#                         -> engine-debug-bench.z64 (own build dir: build/debug-bench)
#
# Via the Docker toolchain: `libdragon make` / `libdragon make BUILD=release`.

BUILD ?= debug
ifeq ($(filter $(BUILD),debug release),)
$(error BUILD must be 'debug' or 'release' (got '$(BUILD)'))
endif

# BENCH=1 boots straight into the full benchmark run (unattended capture). It
# builds into its own directory and ROM: make does not track CFLAGS, so a
# shared build directory would mix objects with and without the flag.
BENCH_SUFFIX := $(if $(filter 1,$(BENCH)),-bench,)

BUILD_DIR  = build/$(BUILD)$(BENCH_SUFFIX)
SOURCE_DIR = src

include $(N64_INST)/include/n64.mk

ifeq ($(BUILD),release)
ROM_NAME = engine$(BENCH_SUFFIX)
CFLAGS  += -DNDEBUG -DLIBDRAGON_PROFILE=0 -DENGINE_DEBUG=0 -DENGINE_PROFILE=0
else
ROM_NAME = engine-debug$(BENCH_SUFFIX)
CFLAGS  += -DENGINE_DEBUG=1 -DENGINE_PROFILE=1
endif

N64_ROM_TITLE    = "SMozN64 Engine"
N64_ROM_SAVETYPE = none

CFLAGS += -I$(SOURCE_DIR)

ifeq ($(BENCH),1)
CFLAGS += -DENGINE_BOOT_BENCHMARK=1
endif

# All sources under src/ (one directory level deep)
SRCS := $(wildcard $(SOURCE_DIR)/*.c $(SOURCE_DIR)/*/*.c)
OBJS := $(SRCS:$(SOURCE_DIR)/%.c=$(BUILD_DIR)/%.o)

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
	@$(N64_AUDIOCONV) -o $(dir $@) "$<"

filesystem/audio/music/%.wav64: assets/audio/music/%.wav
	@mkdir -p $(dir $@)
	@echo "    [WAV64] $@"
	@$(N64_AUDIOCONV) -o $(dir $@) "$<"

filesystem/audio/music/%.xm64: assets/audio/music/%.xm
	@mkdir -p $(dir $@)
	@echo "    [XM64] $@"
	@$(N64_AUDIOCONV) -o $(dir $@) "$<"

# Hot text (ROADMAP_v2 D25): the per-triangle render path is linked as one
# contiguous block so it never collides with itself in the 16 KB direct-mapped
# I-cache. The link script is libdragon's n64.ld with src/engine/hot_text.ld
# inserted after the boot code; it is regenerated when either changes, and the
# build fails if the anchor is missing. tools/hot_text.py checks the result.
ENGINE_LD    := $(BUILD_DIR)/engine.ld
HOT_TEXT_LD  := $(SOURCE_DIR)/engine/hot_text.ld
N64_LDFLAGS  := $(subst -Tn64.ld,-T$(ENGINE_LD),$(N64_LDFLAGS))

$(ENGINE_LD): $(N64_LIBDIR)/n64.ld $(HOT_TEXT_LD)
	@mkdir -p $(dir $@)
	@echo "    [LDSCRIPT] $@"
	@awk -v frag="$(HOT_TEXT_LD)" '{ print } \
		/\*\(\.boot\)/ { boot = 1; next } \
		boot == 1 && /ALIGN\(16\)/ { while ((getline line < frag) > 0) print line; boot = 2 } \
		END { if (boot != 2) { print "engine.ld: *(.boot) + ALIGN(16) anchor not found in n64.ld" > "/dev/stderr"; exit 1 } }' \
		$(N64_LIBDIR)/n64.ld > $@.tmp && mv $@.tmp $@

$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

$(BUILD_DIR)/$(ROM_NAME).dfs: $(assets_conv) $(assets_sfx_wav64) $(assets_music_wav64) $(assets_music_xm64)
$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS) $(ENGINE_LD)

# Header dependency tracking (n64.mk compiles with -MMD)
-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/*/*.d)

clean:
	rm -rf build *.z64 *.elf *.dfs filesystem/*.sprite filesystem/audio

.PHONY: all clean
