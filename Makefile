# Makefile for sensor-bme680 (C version)
# Build BME680 sensor reader for Raspberry Pi using system libraries

# Extract version from debian/changelog using dpkg-parsechangelog
VERSION := $(shell dpkg-parsechangelog -S Version 2>/dev/null || echo "0.0.0")

CC = gcc
CFLAGS = $(EXTRA_CFLAGS) -Wall -Wextra -O2 -std=c99 -I/usr/include/ws -DVERSION=\"$(VERSION)\"
LDFLAGS = $(EXTRA_LDFLAGS) -lwildlifesystems

# /usr, not /usr/local: sr looks for drivers in /usr/bin only, so a driver
# installed by hand anywhere else is never found.
PREFIX ?= /usr
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

SRCDIR = src
TARGET = sensor-bme680
SOURCES = $(SRCDIR)/main.c $(SRCDIR)/bme680.c
HEADERS = $(SRCDIR)/bme680.h

.PHONY: all clean install uninstall debug deb

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(LDFLAGS)

# Build with debug symbols
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

# Install the binary and man page
install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)
	install -m 644 man/sensor-bme680.1 $(DESTDIR)$(MANDIR)/

# Uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/sensor-bme680.1

# Clean build artifacts
clean:
	rm -f $(TARGET)

# For Debian packaging
deb:
	dpkg-buildpackage -us -uc -b
