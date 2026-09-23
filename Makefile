# SMozN64 Engine
#
#   make                  debug build   -> engine-debug.z64  (asserts, RDP validator, profiler)
#   make BUILD=release    release build -> engine.z64        (debug code compiled out)
#
# Via the Docker toolchain: `libdragon make` / `libdragon make BUILD=release`.

BUILD ?= debug
ifeq ($(filter $(BUILD),debug release),)
$(error BUILD must be 'debug' or 'release' (got '$(BUILD)'))
endif

BUILD_DIR  = build/$(BUILD)
SOURCE_DIR = src

include $(N64_INST)/include/n64.mk

ifeq ($(BUILD),release)
ROM_NAME = engine
CFLAGS  += -DNDEBUG -DLIBDRAGON_PROFILE=0 -DENGINE_DEBUG=0 -DENGINE_PROFILE=0
else
ROM_NAME = engine-debug
CFLAGS  += -DENGINE_DEBUG=1 -DENGINE_PROFILE=1
endif

N64_ROM_TITLE    = "SMozN64 Engine"
N64_ROM_SAVETYPE = none

CFLAGS += -I$(SOURCE_DIR)

# BENCH=1 boots straight into the full benchmark run (unattended capture)
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

$(ROM_NAME).z64: $(BUILD_DIR)/$(ROM_NAME).dfs

$(BUILD_DIR)/$(ROM_NAME).dfs: $(assets_conv) $(assets_sfx_wav64) $(assets_music_wav64) $(assets_music_xm64)
$(BUILD_DIR)/$(ROM_NAME).elf: $(OBJS)

# Header dependency tracking (n64.mk compiles with -MMD)
-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/*/*.d)

clean:
	rm -rf build *.z64 *.elf *.dfs filesystem/*.sprite filesystem/audio

.PHONY: all clean
