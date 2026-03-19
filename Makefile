MINGW_PREFIX   ?= /usr/x86_64-w64-mingw32
CLANG64_PREFIX   ?= /clangarm64

GIT_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "vUnknown")

CC_LINUX = gcc
CFLAGS     = -Wall -Wextra -std=c99 -O2 -Isrc -Ilib -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-stringop-truncation -Wno-format-truncation -Wno-maybe-uninitialized -DTLESCOPE_VERSION=\"$(GIT_VERSION)\"
CFLAGS_WIN = $(CFLAGS) -DCURL_STATICLIB -static-libgcc -fno-stack-protector

# Sets _WIN variables for each possible architecture
ifeq ($(MSYSTEM),CLANGARM64)
	PKG_CONFIG_WIN ?= pkg-config
	CC_WIN = clang
	LIB_WIN_PATH = -Ilib/raylib_win_arm64/include -Llib/raylib_win_arm64/lib -I$(CLANG64_PREFIX)/include -L$(CLANG64_PREFIX)/lib
	override DIST_WIN = dist/TLEscope-Win-arm64-Portable
else
	PKG_CONFIG_WIN ?= x86_64-w64-mingw32-pkg-config
    CC_WIN = x86_64-w64-mingw32-gcc
	LIB_WIN_PATH = -Ilib/raylib_win/include -Llib/raylib_win/lib -I$(MINGW_PREFIX)/include -L$(MINGW_PREFIX)/lib
endif

ifdef BUILD_FOR_DIST
	CFLAGS += -DBUILD_FOR_DIST
endif

LIB_LIN_PATH = -Ilib/raylib_lin/include -Llib/raylib_lin/lib

SRC       = src/main.c src/astro.c src/config.c src/ui.c src/rotator.c
OBJ       = $(SRC:src/%.c=build/%.o)

LDFLAGS_LIN = $(LIB_LIN_PATH) -lraylib -lcurl -lGL -lm -lpthread -ldl -lrt -lX11
CURL_FIX = $(shell $(PKG_CONFIG_WIN) --libs --static libcurl 2>/dev/null | sed -e 's/-R[^ ]*//g' -e 's/-lzstd//g' || echo "-lcurl -lnghttp2 -lssl -lcrypto -lssh2 -lz -lcrypt32 -lwldap32 -lws2_32")

LDFLAGS_WIN = $(LIB_WIN_PATH) -lraylib -Wl,-Bstatic $(CURL_FIX) -lssp_nonshared -Wl,-Bdynamic -lzstd -lbcrypt -lsecur32 -liphlpapi -lopengl32 -lgdi32 -lwinmm -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,--allow-multiple-definition -mwindows
DIST_LINUX = dist/TLEscope-Linux-Portable
DIST_WIN   = dist/TLEscope-Win-Portable

INSTALL_DIR ?= /opt/TLEscope
LINK_DIR    ?= /usr/local/bin
APP_DIR     ?= /usr/share/applications

# macOS (Apple Silicon / Intel)
CC_MACOS = clang
RAYLIB_CFLAGS = $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS = $(shell pkg-config --libs raylib 2>/dev/null)
LDFLAGS_MACOS = $(RAYLIB_LIBS) -lcurl -framework IOKit -framework Cocoa -framework OpenGL
DIST_MACOS = dist/TLEscope-macOS-Portable

.PHONY: all linux macos windows windows-arm64 win-installer clean build bin install uninstall

all: linux

linux: bin/TLEscope
ifeq ($(BUILD_FOR_DIST),yes)
	@echo "Skipping bundling for distribution build (BUILD_FOR_DIST=yes) the binary can be found in bin."
else
	@mkdir -p $(DIST_LINUX)
	cp bin/TLEscope $(DIST_LINUX)/
	cp -r themes/ $(DIST_LINUX)/
	cp settings.json $(DIST_LINUX)/ 2>/dev/null || true 
	cp data.tle $(DIST_LINUX)/ 2>/dev/null || true
	cp logo*.png $(DIST_LINUX)/ 2>/dev/null || true
	@echo "Linux build bundled in $(DIST_LINUX)/, do not run bin/*"
	@echo "Here's your subshell command to run it! (cd $(DIST_LINUX)/ && ./TLEscope)"
endif

macos: bin/TLEscope-macos
	@mkdir -p $(DIST_MACOS)
	cp bin/TLEscope-macos $(DIST_MACOS)/TLEscope
	cp -r themes/ $(DIST_MACOS)/
	cp settings.json $(DIST_MACOS)/ 2>/dev/null || true
	cp data.tle $(DIST_MACOS)/ 2>/dev/null || true
	cp logo*.png $(DIST_MACOS)/ 2>/dev/null || true
	@echo "macOS build bundled in $(DIST_MACOS)/"
	@echo "Run it with: cd $(DIST_MACOS)/ && ./TLEscope"

windows: bin/TLEscope.exe
	@mkdir -p $(DIST_WIN)
	cp bin/TLEscope.exe $(DIST_WIN)/
	cp $(MINGW_PREFIX)/bin/libzstd*.dll $(DIST_WIN)/ 2>/dev/null || true
	cp -r themes/ $(DIST_WIN)/
	cp settings.json $(DIST_WIN)/ 2>/dev/null || true
	cp data.tle $(DIST_WIN)/ 2>/dev/null || true
	cp logo*.png $(DIST_WIN)/ 2>/dev/null || true
	cp $(MINGW_PREFIX)/bin/libssp*.dll $(DIST_WIN)/ 2>/dev/null || true
	@echo "Windows build bundled in $(DIST_WIN)/, run it from there!"

windows-arm64: bin/TLEscope.exe
	@mkdir -p $(DIST_WIN)
	cp bin/TLEscope.exe $(DIST_WIN)/
	cp $(CLANG64_PREFIX)/bin/libzstd*.dll $(DIST_WIN)/ 2>/dev/null || true
	cp -r themes/ $(DIST_WIN)/
	cp settings.json $(DIST_WIN)/ 2>/dev/null || true
	cp data.tle $(DIST_WIN)/ 2>/dev/null || true
	cp logo*.png $(DIST_WIN)/ 2>/dev/null || true
	cp $(CLANG64_PREFIX)/bin/libssp*.dll $(DIST_WIN)/ 2>/dev/null || true
	@echo "Windows ARM64 build bundled in $(DIST_WIN)/, run it from there!"

win-installer: windows
	@echo "Building Windows installer..."
	convert logo.png $(DIST_WIN)/logo.ico 2>/dev/null || echo "Warning: ImageMagick not installed, skipping .ico generation"
	makensis installer.nsi
	@echo "Installer built at dist/TLEscope-Installer.exe"

# yes makefile this data copied juuuuuuuust fine and is safe and sound don't worry about it :3 
# microsoft, and I mean this sincerely, please keep bloating windows so that people stop using it and annoying me about it thanks bye.

bin/TLEscope: $(OBJ) | bin
	$(CC_LINUX) $(CFLAGS) -o $@ $^ $(LDFLAGS_LIN)

bin/TLEscope-macos: $(SRC) | bin
	@if ! pkg-config --exists raylib 2>/dev/null; then echo "Error: raylib not found. Install with: brew install raylib"; exit 1; fi
	$(CC_MACOS) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $^ $(LDFLAGS_MACOS)

bin/TLEscope.exe: $(SRC) | bin
	$(CC_WIN) $(CFLAGS_WIN) -o $@ $^ $(LDFLAGS_WIN)

bin/TLEscope-arm64.exe: $(SRC) | bin
	$(CC_WIN) $(CFLAGS_WIN) -o $@ $^ $(LDFLAGS_WIN)

build/%.o: src/%.c | build
	$(CC_LINUX) $(CFLAGS) $(LIB_LIN_PATH) -c $< -o $@

build:
	mkdir -p build

bin:
	mkdir -p bin

clean:
	rm -rf build bin dist

install: linux
	@echo "Installing to $(DESTDIR)$(INSTALL_DIR)..."
	install -d $(DESTDIR)$(INSTALL_DIR)
	cp -r $(DIST_LINUX)/* $(DESTDIR)$(INSTALL_DIR)/
	chmod 755 $(DESTDIR)$(INSTALL_DIR)/TLEscope
	install -d $(DESTDIR)$(LINK_DIR)
	@echo '#!/bin/sh' > $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'USER_DIR="$${XDG_CONFIG_HOME:-$$HOME/.config}/TLEscope"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'mkdir -p "$$USER_DIR"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'ln -sfn "$(INSTALL_DIR)/themes" "$$USER_DIR/themes"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'ln -sfn "$(INSTALL_DIR)/logo.png" "$$USER_DIR/logo.png"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'if [ ! -f "$$USER_DIR/settings.json" ] && [ -f "$(INSTALL_DIR)/settings.json" ]; then cp "$(INSTALL_DIR)/settings.json" "$$USER_DIR/settings.json"; fi' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'if [ ! -f "$$USER_DIR/data.tle" ] && [ -f "$(INSTALL_DIR)/data.tle" ]; then cp "$(INSTALL_DIR)/data.tle" "$$USER_DIR/data.tle"; fi' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'cd "$$USER_DIR" && exec "$(INSTALL_DIR)/TLEscope" "$$@"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	chmod 755 $(DESTDIR)$(LINK_DIR)/TLEscope
	install -d $(DESTDIR)$(APP_DIR)
	@echo '[Desktop Entry]' > $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Type=Application' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Name=TLEscope' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Exec=TLEscope' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Icon=$(INSTALL_DIR)/logo.png' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Terminal=false' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Categories=Utility;Science;' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo "Install complete. You can now execute 'TLEscope' from anywhere."

uninstall:
	@echo "Uninstalling..."
	rm -f $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	rm -f $(DESTDIR)$(LINK_DIR)/TLEscope
	rm -rf $(DESTDIR)$(INSTALL_DIR)
	@echo "Uninstall complete."