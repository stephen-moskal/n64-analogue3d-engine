BUILD_DIR = build
SOURCE_DIR = src

include $(N64_INST)/include/n64.mk

N64_ROM_TITLE = "Hello Cube"
N64_ROM_SAVETYPE = none

CFLAGS += -I$(SOURCE_DIR)

# Asset conversion — sprites
assets_png = $(wildcard assets/*.png)
assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))

MKSPRITE_FLAGS ?= --format RGBA16

# Asset conversion — audio
assets_sfx_wav   = $(wildcard assets/audio/sfx/*.wav)
assets_music_wav = $(wildcard assets/audio/music/*.wav)
assets_music_xm  = $(wildcard assets/audio/music/*.xm)
assets_sfx_wav64   = $(addprefix filesystem/audio/sfx/,$(notdir $(assets_sfx_wav:%.wav=%.wav64)))
assets_music_wav64 = $(addprefix filesystem/audio/music/,$(notdir $(assets_music_wav:%.wav=%.wav64)))
assets_music_xm64  = $(addprefix filesystem/audio/music/,$(notdir $(assets_music_xm:%.xm=%.xm64)))

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/render/mesh.o \
       $(BUILD_DIR)/render/mesh_defs.o \
       $(BUILD_DIR)/render/cube.o \
       $(BUILD_DIR)/render/lighting.o \
       $(BUILD_DIR)/render/texture.o \
       $(BUILD_DIR)/render/camera.o \
       $(BUILD_DIR)/render/floor.o \
       $(BUILD_DIR)/render/billboard.o \
       $(BUILD_DIR)/render/shadow.o \
       $(BUILD_DIR)/render/particle.o \
       $(BUILD_DIR)/render/atmosphere.o \
       $(BUILD_DIR)/input/input.o \
       $(BUILD_DIR)/input/action.o \
       $(BUILD_DIR)/ui/text.o \
       $(BUILD_DIR)/ui/menu.o \
       $(BUILD_DIR)/collision/collision.o \
       $(BUILD_DIR)/scene/scene.o \
       $(BUILD_DIR)/scenes/demo_scene.o \
       $(BUILD_DIR)/audio/audio.o \
       $(BUILD_DIR)/audio/sound_bank.o

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

hello_cube.z64: N64_ROM_TITLE = "Hello Cube"
hello_cube.z64: $(BUILD_DIR)/hello_cube.dfs

$(BUILD_DIR)/hello_cube.dfs: $(assets_conv) $(assets_sfx_wav64) $(assets_music_wav64) $(assets_music_xm64)
$(BUILD_DIR)/hello_cube.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64 *.elf *.dfs filesystem/*.sprite filesystem/audio

.PHONY: clean
