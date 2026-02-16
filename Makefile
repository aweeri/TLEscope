CC_LINUX = gcc
CC_WIN = x86_64-w64-mingw32-gcc

CFLAGS = -Wall -Wextra -std=c99 -O2
SRC = src/main.c src/astro.c src/config.c
FETCH_SRC = src/fetch_tle.c

# Linux configuration
LDFLAGS_LINUX = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
CURL_LINUX = -lcurl
TARGET_LINUX = TLEScope
FETCH_LINUX = fetch_tle

# Windows configuration
# Ensure curl is in your mingw lib path
LDFLAGS_WIN = -lraylib -lopengl32 -lgdi32 -lwinmm -mwindows
CURL_WIN = -lcurl -lws2_32 -lcrypt32
INC_WIN = -Iraylib_win/include -I/usr/x86_64-w64-mingw32/include
LIB_WIN = -Lraylib_win/lib -L/usr/x86_64-w64-mingw32/lib
TARGET_WIN = TLEScope.exe
FETCH_WIN = fetch_tle.exe

all: $(TARGET_LINUX) $(FETCH_LINUX)

# Linux Build
$(TARGET_LINUX): $(SRC)
	$(CC_LINUX) $(CFLAGS) -o $(TARGET_LINUX) $(SRC) $(LDFLAGS_LINUX)

$(FETCH_LINUX): $(FETCH_SRC)
	$(CC_LINUX) $(CFLAGS) -o $(FETCH_LINUX) $(FETCH_SRC) $(CURL_LINUX)

# Windows Build
windows: $(TARGET_WIN) $(FETCH_WIN)

$(TARGET_WIN): $(SRC)
	$(CC_WIN) $(CFLAGS) $(INC_WIN) -o $(TARGET_WIN) $(SRC) $(LIB_WIN) $(LDFLAGS_WIN)

$(FETCH_WIN): $(FETCH_SRC)
	$(CC_WIN) $(CFLAGS) $(INC_WIN) -o $(FETCH_WIN) $(FETCH_SRC) $(LIB_WIN) $(CURL_WIN)

clean:
	rm -f $(TARGET_LINUX) $(TARGET_WIN) $(FETCH_LINUX) $(FETCH_WIN)

.PHONY: all windows clean