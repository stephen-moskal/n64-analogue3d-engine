# Host unit tests for the engine's pure-logic modules (ROADMAP_v2 P1.9).
#
#   libdragon exec make -C tests/host run      # inside the build container (has gcc)
#   make -C tests/host run                     # any machine with gcc / clang
#
# Modules are compiled against tests/host/shim/libdragon.h instead of the real
# libdragon, so only code without rendering/RSP dependencies belongs here.

CC      ?= gcc
SRC     := ../../src
BUILD   := build
CFLAGS  := -std=gnu17 -Wall -Werror -O1 -g -I shim -I $(SRC) -I . -DENGINE_STATS=1
LDLIBS  := -lm

ENGINE_SRCS := \
	$(SRC)/collision/collision.c \
	$(SRC)/physics/physics.c \
	$(SRC)/input/action.c \
	$(SRC)/render/camera.c \
	$(SRC)/render/mesh_build.c \
	$(SRC)/render/mesh_defs.c \
	$(SRC)/render/particle.c \
	$(SRC)/debug/frametime.c \
	$(SRC)/debug/stats.c

TEST_SRCS := test_main.c test_vec3.c test_collision.c test_physics.c \
             test_action.c test_camera.c test_frametime.c test_mesh.c test_particle.c shim/shim.c

OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(TEST_SRCS) $(ENGINE_SRCS)))
vpath %.c . shim $(sort $(dir $(ENGINE_SRCS)))

all: $(BUILD)/run_tests

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/run_tests: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $@

run: $(BUILD)/run_tests
	./$(BUILD)/run_tests

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
