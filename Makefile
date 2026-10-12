# CupidGBA — Makefile
# GBA emulator in C with SDL2

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -g -Iinclude -MMD -MP
LDFLAGS = $(shell pkg-config --libs sdl2) -lm
SDLCFLAGS = $(shell pkg-config --cflags sdl2)
CFLAGS += $(SDLCFLAGS)
CFLAGS += $(CFLAGS_EXTRA)

# Source files
SRC_ARM      = src/arm/arm.c src/arm/isa-arm.c src/arm/isa-thumb.c
SRC_GBA      = src/gba/gba.c src/gba/memory.c src/gba/bus.c src/gba/io.c src/gba/irq.c src/gba/video.c \
               src/gba/audio.c src/gba/dma.c src/gba/prefetch.c src/gba/timer.c src/gba/bios.c \
               src/gba/hle-bios.c src/gba/savedata.c
SRC_CORE     = src/core/timing.c
SRC_RENDER   = src/gba/render/software.c src/gba/render/composite.c \
               src/gba/render/mode0.c src/gba/render/mode2.c \
               src/gba/render/mode3.c src/gba/render/mode4.c \
               src/gba/render/mode5.c src/gba/render/obj.c
SRC_PLATFORM = src/platform/main.c src/platform/audio.c src/platform/input.c

SRCS = $(SRC_ARM) $(SRC_GBA) $(SRC_CORE) $(SRC_RENDER) $(SRC_PLATFORM)
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

TARGET = cupidgba

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

test:
	cmake -S . -B build-test -DCUPID_BUILD_FRONTEND=OFF
	cmake --build build-test
	ctest --test-dir build-test --output-on-failure

-include $(DEPS)
