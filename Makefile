CC_LINUX = gcc
CC_WIN   = x86_64-w64-mingw32-gcc
CFLAGS   = -Wall -Wextra -std=c99 -O2 -Isrc

# Files
SRC       = src/main.c src/astro.c src/config.c
FETCH_SRC = src/fetch_tle.c
OBJ       = $(SRC:src/%.c=build/%.o)
FETCH_OBJ = build/fetch_tle.o

# Library Flags
LDFLAGS_LIN = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
CURL_LIN    = -lcurl
LDFLAGS_WIN = -lraylib -lopengl32 -lgdi32 -lwinmm -mwindows
CURL_WIN    = -lcurl -lws2_32 -lcrypt32
WIN_PATHS   = -Iraylib_win/include -Lraylib_win/lib -I/usr/x86_64-w64-mingw32/include -L/usr/x86_64-w64-mingw32/lib

all: linux

linux: bin/TLEscope bin/fetch_tle

windows: bin/TLEscope.exe bin/fetch_tle.exe

# Linking
bin/TLEscope: $(OBJ) | bin
	$(CC_LINUX) $(CFLAGS) -o $@ $^ $(LDFLAGS_LIN)

bin/fetch_tle: $(FETCH_OBJ) | bin
	$(CC_LINUX) $(CFLAGS) -o $@ $^ $(CURL_LIN)

bin/TLEscope.exe: $(SRC) | bin
	$(CC_WIN) $(CFLAGS) $(WIN_PATHS) -o $@ $^ $(LDFLAGS_WIN)

bin/fetch_tle.exe: $(FETCH_SRC) | bin
	$(CC_WIN) $(CFLAGS) $(WIN_PATHS) -o $@ $^ $(CURL_WIN)

# Compiling (Linux only for object reuse)
build/%.o: src/%.c | build
	$(CC_LINUX) $(CFLAGS) -c $< -o $@

# Directory management
bin build:
	@mkdir -p $@

clean:
	rm -rf bin build

.PHONY: all linux windows clean