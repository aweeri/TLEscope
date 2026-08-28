<p align="center">
  <a href="https://discord.gg/uDmNsuZR4W"><img src="https://img.shields.io/badge/Discord-%235865F2.svg?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a> <a href="https://ko-fi.com/aweeri"><img src="https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white" alt="Ko-fi"></a> <a href="https://deepwiki.com/aweeri/TLEscope"><img src="https://img.shields.io/badge/DeepWiki-000000?style=for-the-badge&logo=bookstack&logoColor=white" alt="DeepWiki"></a> <a href="https://tlescope.eu"><img src="https://img.shields.io/badge/Website-0F9D58?style=for-the-badge&logo=internetexplorer&logoColor=white" alt="Website"></a>
  <br>
  <a href="https://github.com/aweeri/TLEscope/stargazers"><img src="https://img.shields.io/github/stars/aweeri/TLEscope?style=for-the-badge&color=yellow" alt="Stars"></a> <a href="https://github.com/aweeri/TLEscope/network/members"><img src="https://img.shields.io/github/forks/aweeri/TLEscope?style=for-the-badge&color=lightgrey" alt="Forks"></a> <a href="https://github.com/aweeri/TLEscope/issues"><img src="https://img.shields.io/github/issues/aweeri/TLEscope?style=for-the-badge&color=orange" alt="Issues"></a> <a href="https://github.com/aweeri/TLEscope/graphs/contributors"><img src="https://img.shields.io/github/contributors/aweeri/TLEscope?style=for-the-badge&color=blueviolet" alt="Contributors"></a> <a href="https://github.com/aweeri/TLEscope/pulse"><img src="https://img.shields.io/github/last-commit/aweeri/TLEscope?style=for-the-badge&color=brightgreen" alt="Last Commit"></a>
</p>

# **TLEscope**

TLEscope is a satellite visualization and tracking tool designed to transform orbital data (such as the deprecated Two-Line Element sets or more modern CCSDS Orbit Mean-Elements Messages) into intuitive, interactive data. It provides a streamlined interface for tracking the current and future positions of orbital bodies across both 3D and 2D environments.

### Not interested in the market pitch? [__Click Here__](#download-from-github) to skip directly to downloads.

### **Features**

- **Dual-View Visualization**: Seamlessly toggle between an interactive 3D orbital space and a 2D projection featuring accurate satellite ground tracks.

- **Accurate Terminator Line Simulation**: Easily preview sunlight conditions, in 2D and 3D, with realistic atmospheric scattering and sunset coloring.

- **Coverage Analysis**: Real-time rendering of Line-of-Sight (LOS) coverage areas and comprehensive orbital characteristics.

- **Multi-Format Orbital Data**: Load and parse orbital data in multiple formats:
  - Legacy TLE / 3LE
  - CCSDS OMM JSON
  - CCSDS OMM CSV
  - CCSDS OMM XML
  - CCSDS OMM KVN (Key-Value Notation)

- **Online Data Fetching**: Fetch orbital data directly from:
  - [CelesTrak](https://celestrak.org/) — built-in source groups (stations, visual, weather, etc.)
  - [ReTLEctor](https://github.com/MrTalon63/retlector) — CelesTrak mirror for frequent queries
  - Custom data sources with configurable URLs and preferred formats

- **Dear ImGui Interface**: Modern, dockable-window UI built with [Dear ImGui](https://github.com/ocornut/imgui) via [rlImGui](https://github.com/raylib-extras/rlImGui), including:

- **Customization**: Deeply configurable theming and functional options to suit professional or personal preferences. Settings are persisted to [`settings.json`](settings.json).

- **For nerds, By nerds**:
TLEscope comes equipped with tools designed for RF engineers, satellite operators, and people who *just* want to know when to expect the next sunlit ISS pass.

- **Native OS Support**: Built for Linux (x86_64 and ARM64), macOS (Apple Silicon and Intel), and Windows (x86_64 and ARM64).

### **Design Philosophy**

Most existing orbital tracking software suffers from dated, unintuitive interfaces. TLEscope bridges this gap by prioritizing both visual clarity and ease of use.

The project is heavily influenced by the Kerbal Space Program map view and Blender-style camera navigation, offering a familiar and fluid control scheme for researchers and enthusiasts alike.

### **Development & Contributions**

TLEscope is an evolving project with a rich roadmap. We welcome bug reports, feature requests, and code contributions via the project's issue tracker. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for guidelines.

### **Download from GitHub**

To download TLEscope, grab a portable zip from the [Releases tab](https://github.com/aweeri/TLEscope/releases), then extract its contents into a directory of choice.
You can choose between nightly and complete releases:
- [**Stable**](https://github.com/aweeri/TLEscope/releases) releases are properly versioned notable milestone builds. They may not have the latest features, but they are a stable and safe choice.
- [**Nightly**](https://github.com/aweeri/TLEscope/releases/tag/nightly) releases are always up to date with the latest commits, as long as they [compile correctly](https://github.com/aweeri/TLEscope/actions). Do not complain too much if things don't work as expected.

### **Download from a package manager**

TLEscope provides also packages on following systems/distributions:
- Arch Linux:
  - AUR
    * [`tlescope-bin`](https://aur.archlinux.org/packages/tlescope-bin): Downloads and installs the latest stable version of TLEscope from [Releases](https://github.com/aweeri/TLEscope/releases) as AUR package.
    * [`tlescope-git`](https://aur.archlinux.org/packages/tlescope-git): Git clones the latest commit of this repository and installs on the system as AUR package.

### **Building From Source**

TLEscope uses GCC for Linux builds, Clang for macOS, and cross-compiles for Windows using `x86_64-w64-mingw32-g++` (or `clang++` on ARM64).

The project is written in **C++20** and uses the **raylib** framework with **Dear ImGui** (via rlImGui) for the user interface. Orbital propagation uses the **SGP4** algorithm, and HTTP fetching uses **libcurl**.

**raylib is vendored as a git submodule** and built from source by the Makefile, so there is no need to install raylib via a system package manager. When cloning, use `--recurse-submodules` (or run `git submodule update --init --recursive` after a plain clone) to fetch it.

Install the required build tools and libraries, then clone the repository and execute the appropriate `make` command in the root directory of the project. Steps for typical system configurations can be found below:

> [!NOTE]
> The Makefile bundles the executable with its required assets (themes, settings, data) into the `dist/` directory. For a functional installation, use the contents of `dist/TLEscope-Linux-Portable` or `dist/TLEscope-Win-Portable` rather than running directly from `bin/`.

**Debian/Ubuntu-based systems**
```
sudo apt-get update
sudo apt-get install -y g++ make libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev libcurl4-openssl-dev
# Additionally needed if cross-compiling for Windows:
sudo apt-get install -y binutils-mingw-w64-x86-64 g++-mingw-w64-x86-64

git clone --recurse-submodules https://github.com/aweeri/TLEscope
cd TLEscope
make linux      # Results in dist/TLEscope-Linux-Portable/
```
**Arch-based systems**
```
sudo pacman -S --needed base-devel git alsa-lib libx11 libxrandr libxi mesa glu libxcursor libxinerama wayland libxkbcommon curl
# If cross-compiling for Windows:
sudo pacman -S mingw-w64-gcc

git clone --recurse-submodules https://github.com/aweeri/TLEscope
cd TLEscope
make linux      # Results in dist/TLEscope-Linux-Portable/
```
**macOS (Apple Silicon / Intel)**
```
git clone --recurse-submodules https://github.com/aweeri/TLEscope
cd TLEscope
make macos      # Results in dist/TLEscope-macOS-Portable/
```
**Windows systems (MSYS2 UCRT64 / MINGW64)**
Install [MSYS2](https://www.msys2.org/), then run the following in a **UCRT64** terminal:
```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-pkgconf make git
git clone --recurse-submodules https://github.com/aweeri/TLEscope
cd TLEscope
make windows
```
*(Or in a **MINGW64** terminal using `mingw-w64-x86_64-` packages)*

**Windows ARM64 (MSYS2 CLANGARM64)**
Install [MSYS2](https://www.msys2.org/), then run the following in a **CLANGARM64** terminal:
```
pacman -S mingw-w64-clang-aarch64-clang mingw-w64-clang-aarch64-curl mingw-w64-clang-aarch64-zstd mingw-w64-clang-aarch64-pkgconf make git
git clone --recurse-submodules https://github.com/aweeri/TLEscope
cd TLEscope
make windows-arm64
```

### **Make Targets**

| Target | Description |
|--------|-------------|
| `make raylib` | Build the raylib submodule into a static library (done automatically by the other targets) |
| `make linux` | Build for Linux (x86_64 or ARM64) |
| `make macos` | Build for macOS (Apple Silicon / Intel) |
| `make windows` | Build for Windows x86_64 (cross-compile or native) |
| `make windows-arm64` | Build for Windows ARM64 (MSYS2 CLANGARM64) |
| `make win-installer` | Build Windows executable + NSIS installer |
| `make install` | System-wide install to `/opt/TLEscope` |
| `make uninstall` | Remove system-wide installation |
| `make clean` | Remove build artifacts |

### **System-wide Installation**

```sh
make linux
sudo make install          # Installs to /opt/TLEscope
# Now you can run 'TLEscope' from anywhere
sudo make uninstall        # Removes the installation
```


### **Credits**

See [`CREDITS.md`](CREDITS.md) for contributors and attributions.
