CC ?= gcc

CFLAGS = -Wall -Wextra -O2 -I./include
CFLAGS += $(shell pkg-config --cflags gtk4 json-glib-1.0)

LDLIBS := $(shell pkg-config --libs gtk4 json-glib-1.0) -lsodium -lcrypto -lssl

SRC := $(shell find src -name '*.c')
OBJ := $(patsubst src/%.c, build/%.o, $(SRC))

BLUEPRINT ?= blueprint-compiler

BLP := $(shell find resources/ui -name '*.blp')
UI = $(patsubst resources/ui/%.blp, resources/generated/%.ui, $(BLP))
UI += $(shell find resources/ui -name '*.ui')

TARGET ?= passwdmngr

DEBUG ?= 0

ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUGMSG
endif

# Set variables based on platform (mainly for compiling on github runners)
ifeq ($(OS),Windows_NT)
	TARGET := passwdmngr.exe

	WIN_PREFIX := $(shell cygpath -m $(MSYSTEM_PREFIX))

	CFLAGS += -I$(WIN_PREFIX)/include
	LDLIBS += -L$(WIN_PREFIX)/lib
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		CC := clang

		HOMEBREW_PREFIX := $(shell brew --prefix)

    	CFLAGS += -I$(HOMEBREW_PREFIX)/include
    	LDLIBS += -L$(HOMEBREW_PREFIX)/lib
	else ifeq ($(UNAME_S),Linux)
#		linux logic if necessary
	endif
endif

all: $(TARGET)

# Compile c
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Compile blueprint
resources/generated/%.ui: resources/ui/%.blp
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(BLUEPRINT) compile $< > $@

# Compile gresources
build/resources.c: resources/resources.xml $(UI)
	@echo "CC $<"
	@cd resources && glib-compile-resources resources.xml --target=../build/resources.c --generate-source

build/resources.o: build/resources.c
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link
$(TARGET): $(OBJ) build/resources.o
	@echo "Linking object files into executable '$(TARGET)'"
	@$(CC) $(OBJ) build/resources.o -o $(TARGET) $(LDLIBS)
	@echo "Done"

clean:
	@echo "Deleting compiled files"
	@rm -rf build resources/generated $(TARGET)

ifeq ($(UNAME_S),Linux)

PREFIX ?= /usr
BINDIR := $(PREFIX)/bin
SHAREDIR := $(PREFIX)/share
APPDIR := $(SHAREDIR)/passwdmngr
DESKTOPDIR := $(SHAREDIR)/applications
ICONDIR := $(SHAREDIR)/icons/hicolor/128x128/apps

install:
	@echo "Installing passwdmngr into $(PREFIX)..."

#	Install binary
	mkdir -p $(BINDIR)
	cp -f passwdmngr $(BINDIR)/passwdmngr

#	Install desktop entry
	mkdir -p $(DESKTOPDIR)
	cp -f com.samuelf09.passwdmngr.desktop $(DESKTOPDIR)/com.samuelf09.passwdmngr.desktop

#	Install icon
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

# Windows cross-compile: make sure mingw64 folder with all necessary pkgconfig entries and libs is in project root

WIN_CC = x86_64-w64-mingw32-gcc

WIN_TARGET = passwdmngr.exe
WIN_PREFIX = /home/samuelf09/passwdmngr-c/mingw64

WIN_CFLAGS = -Wall -Wextra -O2 -I./include
WIN_CFLAGS += $(shell PKG_CONFIG_PATH=./mingw64/lib/pkgconfig/ pkg-config --cflags gtk4 json-glib-1.0)

WIN_LDLIBS := $(shell PKG_CONFIG_PATH=./mingw64/lib/pkgconfig/ pkg-config --libs gtk4 json-glib-1.0) -lsodium -lcrypto -lssl
WIN_LDLIBS := $(filter-out -lvulkan,$(WIN_LDLIBS))

build/resources-win.o: build/resources.c
	@echo "CC $<"
	@$(WIN_CC) $(CFLAGS) -c build/resources.c -o build/resources-win.o

build-windows: build/resources.c build/resources-win.o $(UI)
	@echo "Compiling app..."
	@$(WIN_CC) $(WIN_CFLAGS) $(SRC) build/resources-win.o -o $(WIN_TARGET) $(WIN_LDLIBS)
	@echo "Built Windows executable: $(WIN_TARGET)"
endif