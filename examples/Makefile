# SPDX-License-Identifier: GPL-2.0
#
# Makefile for WireGuard Init Example

CC ?= gcc
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -D_GNU_SOURCE
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wmissing-prototypes -Wstrict-prototypes
CFLAGS += -Wdeclaration-after-statement
CFLAGS += -I../src

# Linker flags - note: linker warnings (like getaddrinfo) cannot be made errors
LDFLAGS ?=

# WireGuard tools object files we need
WG_SRC_DIR = ../src
WG_OBJS = \
	$(WG_SRC_DIR)/config.o \
	$(WG_SRC_DIR)/encoding.o \
	$(WG_SRC_DIR)/ipc.o \
	$(WG_SRC_DIR)/curve25519.o

.PHONY: all clean test

all: wireguard-init

# Test programs
test: test-netlink-create

# Build wireguard-tools objects first
$(WG_SRC_DIR)/%.o: $(WG_SRC_DIR)/%.c
	$(MAKE) -C $(WG_SRC_DIR) $(@F)

# Dynamic build
wireguard-init: wireguard-init.c $(WG_OBJS)
	@echo "Building dynamic wireguard-init..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(WG_OBJS)
	@echo "Build successful!"

# Static build (recommended)
wireguard-init-static: wireguard-init.c $(WG_OBJS)
	@echo "Building static wireguard-init..."
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $< \
		$(WG_SRC_DIR)/config.o \
		$(WG_SRC_DIR)/encoding.o \
		$(WG_SRC_DIR)/ipc.o \
		$(WG_SRC_DIR)/curve25519.o
	@echo "Build successful!"
	@echo ""
	@echo "NOTE: You may see a linker warning about getaddrinfo."
	@echo "This is harmless for server-only configs (no Endpoint fields)."
	@echo ""
	@ldd $@ 2>&1 | head -1
	@ls -lh $@

# Netlink test program (standalone, no dependencies)
test-netlink-create: test-netlink-create.c
	@echo "Building netlink test program..."
	$(CC) -O2 -Wall -Wextra -o $@ $<
	@echo "Build successful!"
	@echo ""
	@echo "To run the test (as root):"
	@echo "  sudo ./test-netlink-create"
	@echo ""
	@echo "See TEST-NETLINK.md for details."

clean:
	rm -f wireguard-init wireguard-init-static test-netlink-create *.o
