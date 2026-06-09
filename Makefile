###############################################################################
# Makefile for loop_charger
#
# Supports cross-compilation for x86 and ARM platforms.
# Build output is placed in x86/ or arm/ respectively.
#
# Usage
# ─────
# 1. Build for x86 (host):
#      make ARCH=x86
#
# 2. Build for ARM (requires aarch64-linux-gnu-gcc on PATH):
#      sudo apt install gcc-aarch64-linux-gnu   # if not installed
#      make ARCH=arm
#
# 3. Debug build:
#      make ARCH=x86 DEBUG=DEBUG
#
# 4. Clean all output:
#      make clean
#
# 5. Rebuild from scratch:
#      make ARCH=<x86|arm> rebuild
#
# Notes
# ─────
# - ARM uses the system aarch64-linux-gnu-gcc cross-compiler.
# - json-c must be installed for the target (libjson-c-dev:arm64 on Debian/Ubuntu).
###############################################################################

# ── Build options ─────────────────────────────────────────────────────────
ARCH  ?= x86
DEBUG ?= RELEASE

ARCH  := $(strip $(ARCH))
DEBUG := $(strip $(DEBUG))

# ── Target binary name ────────────────────────────────────────────────────
TARGET = loop_charger

# ── Source files ──────────────────────────────────────────────────────────
SRCS = \
	src/main.c \
	src/config_loader.c \
	src/log.c \
	src/modbus_tcp.c \
	src/modbus_tcp_client.c \
	src/device_register_map.c \
	src/ups_module.c \
	src/ups_cmos_bridge.c \
	devices/ups/ups_map.c \
	src/cmos_pub.c \
	src/cmos_sub.c

OBJS = $(SRCS:.c=.o)

# ── ARM toolchain ─────────────────────────────────────────────────────────
ARM_CC          = aarch64-linux-gnu-gcc
ARM_LIBJSONC_A  = lib/arm64/libjson-c.a

# ── Common flags ──────────────────────────────────────────────────────────
CFLAGS_COMMON  = -Wall -Wextra -std=c11 -D_GNU_SOURCE
CFLAGS_COMMON += -Iinclude -Idevices

# x86: link against system libjson-c dynamically
LDLIBS_X86 = -lpthread -ljson-c

ifeq ($(DEBUG), DEBUG)
    CFLAGS_COMMON += -g -O0 -DDEBUG_MODE
else
    CFLAGS_COMMON += -O2
endif

# ── Architecture-specific settings ────────────────────────────────────────
ifeq ($(ARCH), x86)
    CC         = gcc
    CFLAGS     = $(CFLAGS_COMMON)
    LDFLAGS    =
    LDLIBS     = $(LDLIBS_X86)
    OUTPUT_DIR = x86

else ifeq ($(ARCH), arm)
    CC         = $(ARM_CC)
    CFLAGS     = $(CFLAGS_COMMON)
    LDFLAGS    =
    # Link libjson-c statically from the bundled prebuilt; system may not have arm64 package.
    LDLIBS     = -lpthread $(ARM_LIBJSONC_A)
    OUTPUT_DIR = arm

else
    $(error Unsupported ARCH "$(ARCH)". Use ARCH=x86 or ARCH=arm.)
endif

# ── Build rules ───────────────────────────────────────────────────────────
.PHONY: all clean rebuild

all: $(OUTPUT_DIR)/$(TARGET)

# Link: object files come BEFORE -l flags so the linker resolves symbols correctly.
$(OUTPUT_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(OUTPUT_DIR)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	rm -rf x86 arm

rebuild: clean all
