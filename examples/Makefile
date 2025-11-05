# SPDX-License-Identifier: GPL-2.0
#
# Makefile for WireGuard Init Example

CC ?= gcc
CFLAGS ?= -O2
CFLAGS += -std=gnu99 -D_GNU_SOURCE
CFLAGS += -Wall -Wextra
CFLAGS += -I../src

# WireGuard tools object files we need
WG_SRC_DIR = ../src
WG_OBJS = \
	$(WG_SRC_DIR)/config.o \
	$(WG_SRC_DIR)/encoding.o \
	$(WG_SRC_DIR)/ipc.o

.PHONY: all clean

all: wireguard-init

# Build wireguard-tools objects first
$(WG_SRC_DIR)/%.o: $(WG_SRC_DIR)/%.c
	$(MAKE) -C $(WG_SRC_DIR) $(@F)

# Dynamic build
wireguard-init: wireguard-init.c $(WG_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(WG_OBJS)

# Static build (recommended)
wireguard-init-static: wireguard-init.c
	$(MAKE) -C $(WG_SRC_DIR) STATIC=yes config.o encoding.o ipc.o
	$(CC) $(CFLAGS) -static -o $@ $< \
		$(WG_SRC_DIR)/config.o \
		$(WG_SRC_DIR)/encoding.o \
		$(WG_SRC_DIR)/ipc.o

clean:
	rm -f wireguard-init wireguard-init-static *.o
