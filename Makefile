CC = gcc
CFLAGS = -Wall -Wextra -g -I./include `pkg-config --cflags gtk4`
LDFLAGS = `pkg-config --libs gtk4` -lsodium

BLUEPRINT = blueprint-compiler

SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c, build/%.o, $(SRC))

BLP = $(wildcard ui/*.blp)
UI = $(patsubst ui/%.blp, data/%.ui, $(BLP))

TARGET = passwdmngr

all: $(TARGET)

build:
	mkdir -p build

# Compile c
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

# Compile blueprint
data/%.ui: ui/%.blp | build
	$(BLUEPRINT) compile $< > $@

# Compile gresources
build/resources.c: data/resources.xml $(UI)
	cd data && glib-compile-resources resources.xml \
		--target=../build/resources.c \
		--generate-source && cd ..

build/resources.o: build/resources.c
	$(CC) $(CFLAGS) -c build/resources.c -o build/resources.o

# Link
$(TARGET): $(OBJ) build/resources.o
	$(CC) $(OBJ) build/resources.o -o $(TARGET) $(LDFLAGS)

clean:
	rm -rf build $(TARGET) $(UI)
