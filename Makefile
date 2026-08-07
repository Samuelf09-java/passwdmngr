CC = gcc
CFLAGS = -Wall -Wextra -g -I./include `pkg-config --cflags gtk4 json-glib-1.0`
LDFLAGS = `pkg-config --libs gtk4 json-glib-1.0` -lsodium -lcrypto -lssl

BLUEPRINT = blueprint-compiler

SRC = $(shell find src -name '*.c')
OBJ = $(patsubst src/%.c, build/%.o, $(SRC))

BLP = $(wildcard resources/ui/*.blp)
UI = $(patsubst resources/ui/%.blp, resources/generated/%.ui, $(BLP))

TARGET = passwdmngr

PREFIX ?= /usr/local
BINDIR := $(PREFIX)/bin
SHAREDIR := $(PREFIX)/share
APPDIR := $(SHAREDIR)/passwdmngr
DESKTOPDIR := $(SHAREDIR)/applications
ICONDIR := $(SHAREDIR)/icons/hicolor/128x128/apps

all: $(TARGET)

# LINUX BUILD

build:
	mkdir -p build

# Compile c
build/%.o: src/%.c | build
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile blueprint
resources/generated/%.ui: resources/ui/%.blp | build
	mkdir -p resources/generated
	$(BLUEPRINT) compile $< > $@

# Compile gresources
build/resources.c: resources/resources.xml $(UI)
	cd resources && glib-compile-resources resources.xml \
		--target=../build/resources.c \
		--generate-source && cd ..

build/resources.o: build/resources.c
	$(CC) $(CFLAGS) -c build/resources.c -o build/resources.o

# Link
$(TARGET): $(OBJ) build/resources.o
	$(CC) $(OBJ) build/resources.o -o $(TARGET) $(LDFLAGS)

clean:
	rm -rf build resources/generated $(TARGET) $(TARGET).exe

install:
	@echo "Installing passwdmngr into $(PREFIX)..."

	# Install binary
	mkdir -p $(BINDIR)
	cp -f passwdmngr $(BINDIR)/passwdmngr

	# Install desktop entry
	mkdir -p $(DESKTOPDIR)
	cp -f com.samuelf09.passwdmngr.desktop $(DESKTOPDIR)/com.samuelf09.passwdmngr.desktop

	# Install icon
	mkdir -p $(ICONDIR)
	cp -f resources/icons/hicolor/128x128/apps/logo.png $(ICONDIR)/passwdmngr.png

	@echo "Updating desktop database..."
	-update-desktop-database $(DESKTOPDIR) || true

	@echo "Installation complete."

uninstall:
	@echo "Uninstalling passwdmngr from $(PREFIX)..."

	rm -f $(BINDIR)/passwdmngr
	rm -f $(DESKTOPDIR)/com.samuelf09.passwdmngr.desktop
	rm -f $(ICONDIR)/passwdmngr.png

	@echo "Updating desktop database..."
	-update-desktop-database $(DESKTOPDIR) || true

	@echo "Uninstall complete."

# WINDOWS BUILD (cross-compiled)

ifneq ($(OS),Windows_NT)
ifeq ($(shell uname -s),Linux)

WINCC = x86_64-w64-mingw32-gcc
WIN_TARGET = passwdmngr.exe
WIN_PREFIX = /home/samuelf09/passwdmngr-c/mingw64

WIN_CFLAGS = -Wall -Wextra -O2 \
		-I./include

WIN_LDFLAGS = -lsodium -lcrypto -lssl

WIN_PKG_CFLAGS = $(shell pkg-config --cflags gtk4 json-glib-1.0)

WIN_PKG_LIBS = $(shell pkg-config --libs gtk4 json-glib-1.0)

WIN_PKG_LIBS := $(filter-out -lvulkan,$(WIN_PKG_LIBS))

build/resources-win.o: build/resources.c
	$(WINCC) $(WIN_CFLAGS) $(WIN_PKG_CFLAGS) -c build/resources.c -o build/resources-win.o

build-windows: build/resources.c build/resources-win.o
	$(WINCC) $(WIN_CFLAGS) $(WIN_PKG_CFLAGS) $(SRC) build/resources-win.o -o $(WIN_TARGET) $(WIN_PKG_LIBS) $(WIN_LDFLAGS)
	@echo "Built Windows executable: $(WIN_TARGET)"

endif

else

# WINDOWS BUILD

WINCC = x86_64-w64-mingw32-gcc
WIN_TARGET = passwdmngr.exe

WIN_CFLAGS = -Wall -Wextra -O2 -I./include
WIN_LDFLAGS = -lsodium -lcrypto -lssl

WIN_PKG_CFLAGS = $(shell pkg-config --cflags gtk4 json-glib-1.0)
WIN_PKG_LIBS = $(shell pkg-config --libs gtk4 json-glib-1.0)

build/resources-win.o: build/resources.c
	$(WINCC) $(WIN_CFLAGS) $(WIN_PKG_CFLAGS) -c build/resources.c -o build/resources-win.o

build-windows: build/resources.c build/resources-win.o
	$(WINCC) $(WIN_CFLAGS) $(WIN_PKG_CFLAGS) $(SRC) build/resources-win.o -o $(WIN_TARGET) $(WIN_PKG_LIBS) $(WIN_LDFLAGS)
	@echo "Built Windows executable: $(WIN_TARGET)"

endif

# MAC BUILD

MACCC = clang
MACFLAGS = $(shell pkg-config --cflags gtk4 json-glib-1.0 libsodium openssl)
MACLIBS  = $(shell pkg-config --libs gtk4 json-glib-1.0 libsodium openssl)

ifeq ($(shell uname -s),Darwin)

build/resources-mac.o: build/resources.c
	$(MACCC) $(MACFLAGS) -c build/resources.c -o build/resources-mac.o

build-macos: build/resources-mac.o
	$(MACCC) -I./include $(MACFLAGS) $(SRC) build/resources-mac.o -o passwdmngr $(MACLIBS)

endif