# @file      Makefile
# @author    cupidthecat
# @brief     Build the C11 SDL2 frontend and invoke the CMake regression suite.
# @date      2026-11-16
#
# @copyright Copyright (c) 2026 cupidthecat
#
# This file is part of CupidGBA.
#
# CupidGBA is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CupidGBA is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CupidGBA.  If not, see <https://gnu.org>.

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
SRC_ARM      = src/arm/arm.c src/arm/isa-arm.c src/arm/isa-thumb.c src/arm/multiply.c
SRC_GBA      = src/gba/gba.c src/gba/memory.c src/gba/memory/feedback.c src/gba/memory/control.c src/gba/cartridge/image.c src/gba/cartridge/paged-rom.c src/gba/save/detection.c src/gba/bus.c src/gba/io.c src/gba/irq.c src/gba/video.c \
               src/gba/audio.c src/gba/audio/wave.c src/gba/dma.c src/gba/prefetch.c src/gba/timer.c src/gba/bios.c \
               src/gba/hle-bios.c src/gba/bios/file.c src/gba/savedata.c src/gba/save/file.c src/gba/save/programming.c \
               src/gba/serial/serial.c src/gba/serial/joybus.c src/gba/serial/uart.c \
               src/gba/cartridge/rtc.c src/gba/cartridge/gpio.c src/gba/cartridge/battery.c src/gba/cartridge/solar.c \
               src/gba/cartridge/gyro.c src/gba/cartridge/tilt.c \
               src/gba/cartridge/card-reader/cards.c src/gba/cartridge/card-reader/camera.c
SRC_CORE     = src/core/timing.c src/core/file.c src/core/file-path.c src/platform/context.c src/platform/key-script.c src/platform/paths.c
SRC_RENDER   = src/gba/render/fetch.c src/gba/render/scanout.c src/gba/render/object-fetch.c
SRC_PLATFORM = src/platform/main.c src/platform/local.c src/platform/window.c src/platform/audio.c src/platform/input.c \
               src/platform/rtc.c src/platform/peripherals.c src/platform/save.c src/platform/rumble.c
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
