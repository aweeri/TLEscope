<p align="center">
  <a href="https://discord.gg/uDmNsuZR4W"><img src="https://img.shields.io/badge/Discord-%235865F2.svg?style=for-the-badge&logo=discord&logoColor=white" alt="Discord"></a> <a href="https://ko-fi.com/aweeri"><img src="https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white" alt="Ko-fi"></a> <a href="https://deepwiki.com/aweeri/TLEscope"><img src="https://img.shields.io/badge/DeepWiki-000000?style=for-the-badge&logo=bookstack&logoColor=white" alt="DeepWiki"></a>
  <br>
  <a href="https://github.com/aweeri/TLEscope/stargazers"><img src="https://img.shields.io/github/stars/aweeri/TLEscope?style=for-the-badge&color=yellow" alt="Stars"></a> <a href="https://github.com/aweeri/TLEscope/network/members"><img src="https://img.shields.io/github/forks/aweeri/TLEscope?style=for-the-badge&color=lightgrey" alt="Forks"></a> <a href="https://github.com/aweeri/TLEscope/issues"><img src="https://img.shields.io/github/issues/aweeri/TLEscope?style=for-the-badge&color=orange" alt="Issues"></a> <a href="https://github.com/aweeri/TLEscope/graphs/contributors"><img src="https://img.shields.io/github/contributors/aweeri/TLEscope?style=for-the-badge&color=blueviolet" alt="Contributors"></a> <a href="https://github.com/aweeri/TLEscope/pulse"><img src="https://img.shields.io/github/last-commit/aweeri/TLEscope?style=for-the-badge&color=brightgreen" alt="Last Commit"></a>
</p>

# **TLEscope**
​TLEscope is a satellite visualization tool designed to transform Two-Line Element (TLE) sets into intuitive, interactive data. It provides a streamlined interface for tracking the current and future positions of orbital bodies across both 3D and 2D environments.

<img width="2381" height="1235" alt="image" src="https://github.com/user-attachments/assets/a410cb2c-28ef-4e7f-86b5-f8a68ccac478" />


### Not interested in the market pitch? [__Click Here__](#download-from-github) to skip directly to downloads.

### ​**Features**
- **​Dual-View Visualization**: Seamlessly toggle between an interactive 3D orbital space and a 2D projection featuring accurate satellite ground tracks.

- **Accurate Terminator Line Simulation**: Easily preview sunlight conditions, in 2D and 3D.

- **​Coverage Analysis**: Real-time rendering of Line-of-Sight (LOS) coverage areas and comprehensive orbital characteristics.
<img width="45%" alt="image" src="https://github.com/user-attachments/assets/5cdf9629-5ae7-415e-8fa7-5ec3a2c953ef" />
<img width="45%" alt="image" src="https://github.com/user-attachments/assets/5af3467e-abc9-4e57-910f-4bc179fb2808" />


- **​TLE Data Integration**: Efficiently load and parse TLE data for individual satellites or entire constellations.
<img width="963" height="608" alt="image" src="https://github.com/user-attachments/assets/029451e5-0567-4af0-9c91-04d63636e131" />

- **​Customization**: Deeply configurable theming and functional options to suit professional or personal preferences.

- **For nerds, By nerds**:
TLEscope comes equipped with tools designed for RF engineers, satellite operators, and people who *just* want to know when to expect the next sunlit ISS pass.
<img width="769" height="581" alt="image" src="https://github.com/user-attachments/assets/45495333-5f00-4c6c-af66-9933264e5e80" />

- **Minimal Footprint and High Performance**: Developed in pure C utilizing the Raylib framework, TLEscope maintains a minimal footprint. The application provides high-performance rendering that is likely more efficient than your standard system file browser, even with hundreds or thousands of satellites on-screen.

- **​Native OS Support**: Built for Linux and Windows.

### ​**Design Philosophy**
​Most existing orbital tracking software suffers from dated, unintuitive interfaces. TLEscope bridges this gap by prioritizing both visual clarity and ease of use.
​The project is heavily influenced by the Kerbal Space Program map view and Blender-style camera navigation, offering a familiar and fluid control scheme for researchers and enthusiasts alike.

### **Roadmap**
You can find the roadmap here: [ROADMAP.md](https://github.com/aweeri/TLEscope/blob/main/ROADMAP.md)

### **​Development & Contributions**
​TLEscope is an evolving project with a rich roadmap. We welcome bug reports, feature requests, and code contributions via the project's issue tracker.

### **Download from GitHub**
To download TLEscope, grab a portable zip from the [Relases tab](https://github.com/aweeri/TLEscope/releases), then extract it's contents into a directory of choice.
You can choose between nightly and complete relases:
- [**Stable**](https://github.com/aweeri/TLEscope/releases) relases are properly versioned notable milestone builds. They may not have the latest features, but they are a stable and safe choice. 
- [**Nightly**](https://github.com/aweeri/TLEscope/releases/tag/nightly) relases are always up to date with the latest commits, as long as they [compile correctly](https://github.com/aweeri/TLEscope/actions). Do not complain too much if things don't work as expected.

### **Download from a package manager**
TLEscope provides also packages on following systems/distributions:
- Arch Linux:
  - AUR
    * [`tlescope-bin`](https://aur.archlinux.org/packages/tlescope-bin): Downloads and install the latest stable version of TLEscope from [Releases](https://github.com/aweeri/TLEscope/releases).
    * [`tlescope-git`](https://aur.archlinux.org/packages/tlescope-bin): Clones the latest commit of this repository and installs on the system.

### **Building From Source**
TLEscope uses gcc for Linux builds and cross-compiles for Windows using x86_64-w64-mingw32-gcc.

Install the required build tools and libraries, then clone the repository and execute the `make linux` command in the root directory of the project, after which run `./bin/TLEscope` (running from within `bin/` will NOT work). If you want to build a Windows executable, you will also need to install the `mingw-w64` package via your package manager of choice. Steps for typical system configurations can be found below:

**Note**: The Makefile bundles the executable with its required assets (themes, settings, data) into the dist/ directory. For a functional installation, use the contents of dist/TLEscope-Linux or dist/TLEscope-Windows rather than running directly from bin/.

**Debian/Ubuntu-based systems**
```
sudo apt-get update
sudo apt-get install -y gcc make libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev libcurl4-openssl-dev
# If cross-compiling for Windows:
sudo apt-get install -y binutils-mingw-w64-x86-64 gcc-mingw-w64-x86-64

git clone https://github.com/aweeri/TLEscope
cd TLEscope
make linux      # Results in dist/TLEscope-Linux/
```
**Arch-based systems**
```
sudo pacman -Syy
sudo pacman -S --needed base-devel git alsa-lib libx11 libxrandr libxi mesa glu libxcursor libxinerama wayland libxkbcommon curl
# If cross-compiling for Windows:
sudo pacman -S mingw-w64-gcc

git clone https://github.com/aweeri/TLEscope
cd TLEscope
make linux      # Results in dist/TLEscope-Linux/
```
**Windows systems**
Install [MSYS2](https://www.msys2.org/), then run the following in a UCRT64 or MINGW64 terminal:
```
pacman -S mingw-w64-x86_64-gcc make mingw-w64-x86_64-curl
git clone https://github.com/aweeri/TLEscope
cd TLEscope
# Override CC_WIN if using local gcc instead of the cross-compiler
make windows CC_WIN=gcc


```

