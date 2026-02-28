BUILD_DIR = build
SOURCE_DIR = src

include $(N64_INST)/include/n64.mk

N64_ROM_TITLE = "Hello Cube"
N64_ROM_SAVETYPE = none

CFLAGS += -I$(SOURCE_DIR)

# Asset conversion
assets_png = $(wildcard assets/*.png)
assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))

MKSPRITE_FLAGS ?= --format RGBA16

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/render/cube.o \
       $(BUILD_DIR)/render/lighting.o \
       $(BUILD_DIR)/render/texture.o \
       $(BUILD_DIR)/input/input.o \
       $(BUILD_DIR)/ui/text.o

# Sprite conversion rule: assets/*.png -> filesystem/*.sprite
filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) $(MKSPRITE_FLAGS) -o filesystem "$<"

hello_cube.z64: N64_ROM_TITLE = "Hello Cube"
hello_cube.z64: $(BUILD_DIR)/hello_cube.dfs

$(BUILD_DIR)/hello_cube.dfs: $(assets_conv)
$(BUILD_DIR)/hello_cube.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64 *.elf *.dfs filesystem/*.sprite

.PHONY: clean
