# Makefile for Clod
CC ?= gcc
CFLAGS ?= -O2 -pipe -Wall -Wextra -std=c11
LDFLAGS ?= 

# Core Dependencies
PKGS := gtk4 sqlite3

# Modular Feature Toggles
WITH_PDF ?= 1
WITH_ARCHIVE ?= 1

ifeq ($(WITH_PDF), 1)
    CFLAGS += -DWITH_PDF
    PKGS += poppler-glib
endif

ifeq ($(WITH_ARCHIVE), 1)
    CFLAGS += -DWITH_ARCHIVE
    PKGS += libarchive
endif

# Resolve pkg-config flags
GTK_CFLAGS := $(shell pkg-config --cflags $(PKGS))
GTK_LIBS := $(shell pkg-config --libs $(PKGS))

SRC = src/main.c src/archive_handler.c src/keybindings.c
OBJ = $(SRC:.c=.o)
TARGET = clod

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS) $(GTK_LIBS)

clean:
	rm -f $(OBJ) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)

.PHONY: all clean install
