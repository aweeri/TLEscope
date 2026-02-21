CC_LINUX = gcc
CC_WIN   = x86_64-w64-mingw32-gcc
CFLAGS   = -Wall -Wextra -std=c99 -O2 -Isrc -Ilib

# [cite_start]Static Library Paths [cite: 1, 3]
LIB_LIN_PATH = -Ilib/raylib_lin/include -Llib/raylib_lin/lib
LIB_WIN_PATH = -Ilib/raylib_win/include -Llib/raylib_win/lib -I/usr/x86_64-w64-mingw32/include -L/usr/x86_64-w64-mingw32/lib

# Files
SRC       = src/main.c src/astro.c src/config.c
FETCH_SRC = src/fetch_tle.c
OBJ       = $(SRC:src/%.c=build/%.o)
FETCH_OBJ = build/fetch_tle.o

# [cite_start]Library Flags [cite: 2]
LDFLAGS_LIN = $(LIB_LIN_PATH) -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
CURL_LIN    = -lcurl
LDFLAGS_WIN = $(LIB_WIN_PATH) -lraylib -lopengl32 -lgdi32 -lwinmm -mwindows
CURL_WIN    = -lcurl -lws2_32 -lcrypt32

all: linux

linux: bin/TLEscope bin/fetch_tle

windows: bin/TLEscope.exe bin/fetch_tle.exe

bin/TLEscope: $(OBJ) | bin
	$(CC_LINUX) $(CFLAGS) -o $@ $^ $(LDFLAGS_LIN)

bin/fetch_tle: $(FETCH_OBJ) | bin
	$(CC_LINUX) $(CFLAGS) -o $@ $^ $(CURL_LIN)

bin/TLEscope.exe: $(SRC) | bin
	$(CC_WIN) $(CFLAGS) -o $@ $^ $(LDFLAGS_WIN)

bin/fetch_tle.exe: $(FETCH_SRC) | bin
	$(CC_WIN) $(CFLAGS) -o $@ $^ $(CURL_WIN)

build/%.o: src/%.c | build
	$(CC_LINUX) $(CFLAGS) $(LIB_LIN_PATH) -c $< -o $@

build:
	mkdir -p build

bin:
	mkdir -p bin

clean:
	rm -rf build bin