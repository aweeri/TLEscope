GIT_VERSION     := $(shell scripts/version.sh describe 2>/dev/null || echo "vUnknown")
GIT_VERSION_NUM := $(shell scripts/version.sh num 2>/dev/null || echo "0.0.0.0")

CC_LINUX = g++
CXXFLAGS   = -Wall -Wextra -std=c++20 -O2 -Isrc -Ilib -Ilib/nlohmann/single_include -Ilib/imgui -Ilib/rlImGui -Ilib/rlImGui/extras -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable -Wno-sign-compare -Wno-stringop-truncation -Wno-format-truncation -Wno-maybe-uninitialized -Wno-narrowing -Wno-missing-field-initializers -DTLESCOPE_VERSION=\"$(GIT_VERSION)\" -DImDrawIdx='unsigned int'

# raylib is built from the git submodule (lib/raylib)
RAYLIB_SRC   = lib/raylib/src
RAYLIB_LIB   = $(RAYLIB_SRC)/libraylib.a
RAYLIB_CFLAGS = -I$(RAYLIB_SRC)
# Extra args passed to raylib's own Makefile (for example CC=... PLATFORM_OS=WINDOWS for cross-compiles)
RAYLIB_MAKE_ARGS ?=

CXXFLAGS_LIN = $(CXXFLAGS) $(RAYLIB_CFLAGS) -MMD -MP
CXXFLAGS_WIN = $(CXXFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -DCURL_STATICLIB -static-libgcc -fno-stack-protector

LDFLAGS_WIN_EXTRA =

# Sets _WIN variables for each possible architecture
ifeq ($(MSYSTEM),CLANGARM64)
	PKG_CONFIG_WIN ?= pkg-config
	CC_WIN = clang++
	DIST_WIN_ARM64 = dist/TLEscope-Win-arm64-Portable

	LDFLAGS_WIN_EXTRA =
else ifeq ($(MSYSTEM),UCRT64)
	PKG_CONFIG_WIN ?= pkg-config
	CC_WIN = g++
else ifeq ($(MSYSTEM),MINGW64)
	PKG_CONFIG_WIN ?= pkg-config
	CC_WIN = g++
else
	PKG_CONFIG_WIN ?= x86_64-w64-mingw32-pkg-config
    CC_WIN = x86_64-w64-mingw32-g++
endif

# Windows resource compiler (for the VERSIONINFO + ICON resources embedded in the exe)
ifeq ($(MSYSTEM),CLANGARM64)
	RC_WIN ?= llvm-windres
else
	RC_WIN ?= windres
endif

# Host compiler used to build the build-time icon generator. It must produce a
# binary that runs on the build machine, so this is deliberately not CC_WIN
# (which may be a cross-compiler when building from Linux).
HOST_CXX ?= g++
EXE_EXT  := $(if $(filter Windows_NT,$(OS)),.exe,)
ICON_GEN := build/gen_icon$(EXE_EXT)
ICON_OUT := build/logo.ico

CURL_FIX_RAW := $(shell $(PKG_CONFIG_WIN) --libs --static libcurl 2>/dev/null)
ifeq ($(strip $(CURL_FIX_RAW)),)
    CURL_FIX = -lcurl -lngtcp2_crypto_ossl -lngtcp2 -lnghttp3 -lnghttp2 -lssl -lcrypto -lssh2 -lbrotlidec -lbrotlicommon -lz -lpsl -lidn2 -lunistring -liconv -lcrypt32 -lwldap32 -lws2_32 -lnormaliz -lgdi32 -ladvapi32
else
    CURL_FIX = $(shell echo "$(CURL_FIX_RAW)" | sed -e 's/-R[^ ]*//g' -e 's/-lzstd//g')
endif

# Link against the static raylib built from the submodule, plus per-OS system libs
LDFLAGS_LIN = $(RAYLIB_LIB) -lcurl -lGL -lX11 -lm -lpthread -ldl -lrt
LDFLAGS_WIN = $(RAYLIB_LIB) -Wl,-Bstatic $(CURL_FIX) -lssp_nonshared -Wl,-Bdynamic -lzstd -lbcrypt -lsecur32 -liphlpapi -Wl,-Bstatic -lwinpthread -Wl,--allow-multiple-definition -lopengl32 -lgdi32 -lwinmm -mwindows $(LDFLAGS_WIN_EXTRA)
DIST_LINUX = dist/TLEscope-Linux-Portable
DIST_WIN   = dist/TLEscope-Win-Portable

INSTALL_DIR ?= /opt/TLEscope
LINK_DIR    ?= /usr/local/bin
APP_DIR     ?= /usr/share/applications

# macOS (Apple Silicon / Intel)
CC_MACOS = clang++
LDFLAGS_MACOS = $(RAYLIB_LIB) -lcurl -framework CFNetwork -framework CoreFoundation -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo -framework QuartzCore
DIST_MACOS = dist/TLEscope-macOS-Portable

SRC          = src/main.cpp src/core/astro.cpp src/core/propagator.cpp src/core/config.cpp src/core/theme.cpp src/core/location.cpp src/data/storage.cpp src/data/provider.cpp src/data/cache.cpp src/data/omm_parser.cpp src/data/async_fetch.cpp src/ui/ui.cpp src/ui/ui_input.cpp src/ui/ui_layout.cpp src/ui/labels.cpp src/ui/imgui_theme.cpp src/ui/logo.cpp src/ui/notifications.cpp src/ui/touch_gesture.cpp src/ui/hints.cpp src/ui/tutorial.cpp src/io/rotator.cpp src/util/c23_compat.cpp src/util/log.cpp src/render/coverage_mesh.cpp src/expo/expo_mode.cpp src/ui/tools/tools_registry.cpp src/ui/tools/tools_common.cpp src/ui/tools/tools_settings.cpp src/ui/tools/tools_scene.cpp src/ui/tools/tools_plot.cpp $(wildcard src/ui/tools/tool_*.cpp)
IMGUI_SRC    = lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_tables.cpp lib/imgui/imgui_widgets.cpp
RLIMGUI_SRC  = lib/rlImGui/rlImGui.cpp
OBJ          = $(SRC:src/%.cpp=build/%.o) $(IMGUI_SRC:lib/imgui/%.cpp=build/%.o) $(RLIMGUI_SRC:lib/rlImGui/%.cpp=build/%.o)
OBJ_WIN      = $(SRC:src/%.cpp=build_win/%.o) $(IMGUI_SRC:lib/imgui/%.cpp=build_win/%.o) $(RLIMGUI_SRC:lib/rlImGui/%.cpp=build_win/%.o)

# a progress bar!
TOTAL_OBJ := $(words $(OBJ))
$(shell echo "$(TOTAL_OBJ)" > /tmp/tlescope_build_total; echo "0" > /tmp/tlescope_build_counter)
TOTAL_WIN_OBJ := $(words $(OBJ_WIN))
$(shell echo "$(TOTAL_WIN_OBJ)" > /tmp/tlescope_build_total_win; echo "0" > /tmp/tlescope_build_counter_win)

# auto-generated header dependencies (-MMD): editing a header rebuilds every
# object that includes it, so a struct change can't leave stale objects behind
-include $(OBJ:.o=.d) $(OBJ_WIN:.o=.d)

.PHONY: all raylib clean-libs linux macos windows windows-arm64 win-installer clean build bin install uninstall test

all: linux

build/main.o: src/render/map_view.h src/expo/expo_mode.h src/ui/logo.h

# Build raylib from the git submodule into a static library (lib/raylib/src/libraylib.a)
# Extra args can be passed via RAYLIB_MAKE_ARGS, e.g. for cross-compiling:
#   make raylib RAYLIB_MAKE_ARGS="CC=aarch64-linux-gnu-gcc PLATFORM_OS=LINUX"
raylib: $(RAYLIB_LIB)

$(RAYLIB_LIB):
	@if [ ! -f "$(RAYLIB_SRC)/raylib.h" ]; then echo "Error: raylib submodule not initialized. Run: git submodule update --init --recursive"; exit 1; fi
	@printf "\033[1;35mBuilding raylib (static)...\033[0m\n"
	$(MAKE) -C $(RAYLIB_SRC) $(RAYLIB_MAKE_ARGS)
	@printf "\033[1;32mraylib built: $(RAYLIB_LIB)\033[0m\n"

# Clean bundled library build artifacts (raylib). Use when switching
# platforms/toolchains so a stale libraylib.a isn't reused.
clean-libs:
	rm -f $(RAYLIB_SRC)/*.o
	rm -f $(RAYLIB_LIB) $(RAYLIB_SRC)/libraylib.web.a $(RAYLIB_SRC)/libraylib.so*
	rm -f $(RAYLIB_SRC)/raygui.c $(RAYLIB_SRC)/*-protocol.h $(RAYLIB_SRC)/*-protocol-code.h $(RAYLIB_SRC)/*-protocol-code.c

linux: $(ICON_OUT) raylib bin/TLEscope
	rm -rf $(DIST_LINUX)
	@mkdir -p $(DIST_LINUX)
	cp bin/TLEscope $(DIST_LINUX)/TLEscope.tmp && mv -f $(DIST_LINUX)/TLEscope.tmp $(DIST_LINUX)/TLEscope
	cp -r themes $(DIST_LINUX)/
	cp $(ICON_OUT) $(DIST_LINUX)/
	@echo "Linux build bundled in $(DIST_LINUX)/"
	@echo "Run it with: cd $(DIST_LINUX)/ && ./TLEscope"

macos: $(ICON_OUT) raylib bin/TLEscope-macos
	rm -rf $(DIST_MACOS)
	@mkdir -p $(DIST_MACOS)
	cp bin/TLEscope-macos $(DIST_MACOS)/TLEscope
	cp -r themes $(DIST_MACOS)/
	cp $(ICON_OUT) $(DIST_MACOS)/
	@echo "macOS build bundled in $(DIST_MACOS)/"
	@echo "Run it with: cd $(DIST_MACOS)/ && ./TLEscope"
	@echo "To make a MacOS bundle: ./macos_bundle.sh"

windows: raylib bin/TLEscope.exe
	rm -rf $(DIST_WIN)
	@mkdir -p $(DIST_WIN)
	cp bin/TLEscope.exe $(DIST_WIN)/
	cp $(MINGW_PREFIX)/bin/libzstd*.dll $(DIST_WIN)/ 2>/dev/null || true
	cp -r themes $(DIST_WIN)/
	cp $(MINGW_PREFIX)/bin/libssp*.dll $(DIST_WIN)/ 2>/dev/null || true
	@echo "Windows build bundled in $(DIST_WIN)/, run it from there!"

windows-arm64: raylib bin/TLEscope-arm64.exe
	rm -rf $(DIST_WIN_ARM64)
	@mkdir -p $(DIST_WIN_ARM64)
	cp bin/TLEscope-arm64.exe $(DIST_WIN_ARM64)/TLEscope.exe
	cp $(CLANG64_PREFIX)/bin/libzstd*.dll $(DIST_WIN_ARM64)/ 2>/dev/null || true
	cp -r themes $(DIST_WIN_ARM64)/
	cp $(CLANG64_PREFIX)/bin/libssp*.dll $(DIST_WIN_ARM64)/ 2>/dev/null || true
	@echo "Windows ARM64 build bundled in $(DIST_WIN_ARM64)/, run it from there!"

win-installer: windows $(ICON_OUT)
	@echo "Building Windows installer..."
	makensis -DVERSION_STR="$(GIT_VERSION)" -DVERSION_NUM="$(GIT_VERSION_NUM)" installer.nsi
	@echo "Installer built at dist/TLEscope-Installer.exe"

# yes makefile this data copied juuuuuuuust fine and is safe and sound don't worry about it :3
# microsoft, and I mean this sincerely, please keep bloating windows so that people stop using it and annoying me about it thanks bye.

# depends on the raylib archive so a parallel make cannot link before it exists
bin/TLEscope: $(OBJ) $(RAYLIB_LIB) | bin
	@printf "\033[1;35mLinking...\033[0m\n"
	$(CC_LINUX) $(CXXFLAGS_LIN) -o $@ $(OBJ) $(LDFLAGS_LIN)
	@printf "\033[1;32mBuild complete! \033[0m\033[0;36mTLEscope v$(GIT_VERSION)\033[0m\n"

bin/TLEscope-macos: raylib $(SRC) src/util/window_macos.mm src/util/proxy_macos.cpp $(IMGUI_SRC) $(RLIMGUI_SRC) | bin
	$(CC_MACOS) $(CXXFLAGS) $(RAYLIB_CFLAGS) -o $@ $(filter-out raylib,$^) $(LDFLAGS_MACOS)

bin/TLEscope.exe: $(OBJ_WIN) build_win/versioninfo.o $(RAYLIB_LIB) | bin
	@printf "\033[1;35mLinking...\033[0m\n"
	$(CC_WIN) $(CXXFLAGS_WIN) -o $@ $(OBJ_WIN) build_win/versioninfo.o $(LDFLAGS_WIN)
	@printf "\033[1;32mBuild complete! \033[0m\033[0;36mTLEscope v$(GIT_VERSION)\033[0m\n"

bin/TLEscope-arm64.exe: $(OBJ_WIN) build_win/versioninfo.o | bin
	@printf "\033[1;35mLinking...\033[0m\n"
	$(CC_WIN) $(CXXFLAGS_WIN) -o $@ $^ $(LDFLAGS_WIN)
	@printf "\033[1;32mBuild complete! \033[0m\033[0;36mTLEscope v$(GIT_VERSION)\033[0m\n"

build/%.o: src/%.cpp | build
	@mkdir -p $(@D)
	@scripts/progress.sh $(CC_LINUX) $(CXXFLAGS_LIN) -c $< -o $@

build/%.o: lib/imgui/%.cpp | build
	@mkdir -p $(@D)
	@scripts/progress.sh $(CC_LINUX) $(CXXFLAGS_LIN) -c $< -o $@

build/%.o: lib/rlImGui/%.cpp | build
	@mkdir -p $(@D)
	@scripts/progress.sh $(CC_LINUX) $(CXXFLAGS_LIN) -c $< -o $@

build_win/%.o: src/%.cpp | build_win
	@mkdir -p $(@D)
	@COUNTER_FILE=/tmp/tlescope_build_counter_win TOTAL_FILE=/tmp/tlescope_build_total_win scripts/progress.sh $(CC_WIN) $(CXXFLAGS_WIN) -c $< -o $@

build_win/%.o: lib/imgui/%.cpp | build_win
	@mkdir -p $(@D)
	@COUNTER_FILE=/tmp/tlescope_build_counter_win TOTAL_FILE=/tmp/tlescope_build_total_win scripts/progress.sh $(CC_WIN) $(CXXFLAGS_WIN) -c $< -o $@

build_win/%.o: lib/rlImGui/%.cpp | build_win
	@mkdir -p $(@D)
	@COUNTER_FILE=/tmp/tlescope_build_counter_win TOTAL_FILE=/tmp/tlescope_build_total_win scripts/progress.sh $(CC_WIN) $(CXXFLAGS_WIN) -c $< -o $@

# The .rc embeds both the VERSIONINFO and the application ICON, so the icon
# must exist before windres runs (and therefore before the exe is linked).
build_win/versioninfo.o: src/versioninfo.rc.in scripts/gen_versioninfo.sh scripts/version.sh $(ICON_OUT) | build_win
	@mkdir -p $(@D)
	@scripts/gen_versioninfo.sh "$(GIT_VERSION_NUM)" "$(GIT_VERSION)" build_win/versioninfo.rc "$(ICON_OUT)"
	@$(RC_WIN) -O coff -i build_win/versioninfo.rc -o $@

# Rasterize the hardcoded logo into a multi-size .ico (build artifact, never
# committed). Reuses src/ui/logo_raster.h, the same code the app renders with.
$(ICON_OUT): scripts/gen_icon.cpp src/ui/logo_raster.h | build
	@mkdir -p $(@D)
	@$(HOST_CXX) -std=c++17 -O2 -Isrc -o $(ICON_GEN) scripts/gen_icon.cpp
	@$(ICON_GEN) $(ICON_OUT)

build:
	mkdir -p build

build_win:
	mkdir -p build_win

bin:
	mkdir -p bin

clean:
	rm -rf build build_win bin dist

install:
	@echo "Installing to $(DESTDIR)$(INSTALL_DIR)..."
	install -d $(DESTDIR)$(INSTALL_DIR)
	cp -r $(DIST_LINUX)/* $(DESTDIR)$(INSTALL_DIR)/
	chmod 755 $(DESTDIR)$(INSTALL_DIR)/TLEscope
	install -d $(DESTDIR)$(LINK_DIR)
	@echo '#!/bin/sh' > $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'USER_DIR="$${XDG_CONFIG_HOME:-$$HOME/.config}/TLEscope"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'mkdir -p "$$USER_DIR"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'ln -sfn "$(INSTALL_DIR)/themes" "$$USER_DIR/themes"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'if [ ! -f "$$USER_DIR/settings.json" ] && [ -f "$(INSTALL_DIR)/settings.json" ]; then cp "$(INSTALL_DIR)/settings.json" "$$USER_DIR/settings.json"; fi' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'if [ ! -f "$$USER_DIR/data.tle" ] && [ -f "$(INSTALL_DIR)/data.tle" ]; then cp "$(INSTALL_DIR)/data.tle" "$$USER_DIR/data.tle"; fi' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	@echo 'cd "$$USER_DIR" && exec "$(INSTALL_DIR)/TLEscope" "$$@"' >> $(DESTDIR)$(LINK_DIR)/TLEscope
	chmod 755 $(DESTDIR)$(LINK_DIR)/TLEscope
	install -d $(DESTDIR)$(APP_DIR)
	@echo '[Desktop Entry]' > $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Type=Application' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Name=TLEscope' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Exec=TLEscope' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Icon=$(DESTDIR)$(INSTALL_DIR)/logo' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Terminal=false' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo 'Categories=Utility;Science;' >> $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	@echo "Install complete. You can now execute 'TLEscope' from anywhere."

uninstall:
	@echo "Uninstalling..."
	rm -f $(DESTDIR)$(APP_DIR)/TLEscope.desktop
	rm -f $(DESTDIR)$(LINK_DIR)/TLEscope
	rm -rf $(DESTDIR)$(INSTALL_DIR)
	@echo "Uninstall complete."
