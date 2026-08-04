CC = gcc
CFLAGS = -Wall -Wextra -g -I./include `pkg-config --cflags gtk4 json-glib-1.0`
LDFLAGS = `pkg-config --libs gtk4 json-glib-1.0` -lsodium -lcrypto -lssl

BLUEPRINT = blueprint-compiler

SRC = $(shell find src -name '*.c')
OBJ = $(patsubst src/%.c, build/%.o, $(SRC))

BLP = $(wildcard resources/ui/*.blp)
UI = $(patsubst resources/ui/%.blp, resources/generated/%.ui, $(BLP))

TARGET = passwdmngr

all: $(TARGET)

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
	rm -rf build resources/generated $(TARGET)
