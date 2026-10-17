# CupidGBA — Makefile
# GBA emulator in C with SDL2

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -g -Iinclude -MMD -MP
LDFLAGS = $(shell pkg-config --libs sdl2) -lm
SDLCFLAGS = $(shell pkg-config --cflags sdl2)
CFLAGS += $(SDLCFLAGS)
CFLAGS += $(CFLAGS_EXTRA)
CFLAGS += $(CPPFLAGS)
LDFLAGS += $(LDFLAGS_EXTRA)
CONTEXT_LIBRARY = $(shell $(CC) -print-file-name=libboost_context.so)
CONTEXT_VERSION = $(shell $(CC) $(CPPFLAGS) -dM -E -include boost/version.hpp -x c /dev/null 2>/dev/null | sed -n 's/^#define BOOST_VERSION //p')
ifneq ($(wildcard $(CONTEXT_LIBRARY)),)
ifneq ($(shell test "$(CONTEXT_VERSION)" -ge 106100 2>/dev/null && echo yes),)
CFLAGS += -DCUPID_USE_FCONTEXT=1
LDFLAGS += -lboost_context
endif
endif

# Source files
SRC_ARM      = src/arm/arm.c src/arm/isa-arm.c src/arm/isa-thumb.c
SRC_GBA      = src/gba/gba.c src/gba/memory.c src/gba/bus.c src/gba/io.c src/gba/irq.c src/gba/video.c \
               src/gba/audio.c src/gba/dma.c src/gba/prefetch.c src/gba/timer.c src/gba/bios.c \
               src/gba/hle-bios.c src/gba/bios/file.c src/gba/savedata.c src/gba/save/file.c \
               src/gba/serial/serial.c src/gba/serial/joybus.c src/gba/serial/uart.c
SRC_CORE     = src/core/timing.c
SRC_RENDER   = src/gba/render/software.c src/gba/render/composite.c \
               src/gba/render/mode0.c src/gba/render/mode2.c \
               src/gba/render/mode3.c src/gba/render/mode4.c \
               src/gba/render/mode5.c src/gba/render/obj.c
SRC_PLATFORM = src/platform/main.c src/platform/local.c src/platform/window.c src/platform/audio.c src/platform/input.c src/platform/context.c
SRC_LINK     = src/link/cable.c

SRCS = $(SRC_ARM) $(SRC_GBA) $(SRC_CORE) $(SRC_RENDER) $(SRC_PLATFORM) $(SRC_LINK)
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
