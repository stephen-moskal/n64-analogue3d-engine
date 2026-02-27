BUILD_DIR = build
SOURCE_DIR = src

include $(N64_INST)/include/n64.mk

N64_ROM_TITLE = "Hello Cube"
N64_ROM_SAVETYPE = none

OBJS = $(BUILD_DIR)/main.o \
       $(BUILD_DIR)/cube.o \
       $(BUILD_DIR)/lighting.o \
       $(BUILD_DIR)/input.o

hello_cube.z64: N64_ROM_TITLE = "Hello Cube"
hello_cube.z64: $(BUILD_DIR)/hello_cube.dfs

$(BUILD_DIR)/hello_cube.dfs: $(wildcard filesystem/*)
$(BUILD_DIR)/hello_cube.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64 *.elf *.dfs

.PHONY: clean
