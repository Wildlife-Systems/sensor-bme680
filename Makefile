# Makefile for sensor-bme680 (C version)
# Build BME680 sensor reader for Raspberry Pi using system libraries

# Extract version from debian/changelog
VERSION := $(shell head -n1 debian/changelog | sed 's/.*(//' | sed 's/).*//')

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -I/usr/include/ws -DVERSION=\"$(VERSION)\"
LDFLAGS = -lwildlifesystems

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

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

# Install the binary
install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/

# Uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET)

# For Debian packaging
deb:
	dpkg-buildpackage -us -uc -b
