### **TLEscope**
​TLEscope is a satellite visualization tool designed to transform Two-Line Element (TLE) sets into intuitive, interactive data. It provides a streamlined interface for tracking the current and future positions of orbital bodies across both 3D and 2D environments.

<img width="35%" alt="image" src="https://github.com/user-attachments/assets/392efef1-6531-43cd-8d3e-649d0ecd37c8" />
<img width="45%" alt="image" src="https://github.com/user-attachments/assets/2e01038f-3c29-49f0-b474-2dc558634d5d" />


### ​**Features**
- **​Dual-View Visualization**: Seamlessly toggle between an interactive 3D orbital space and a 2D projection featuring accurate satellite ground tracks.

- **Accurate Terminator Line Simulation**: Easily preview sunlight conditions, in 2D and 3D.
<img width="50%" alt="image" src="https://github.com/user-attachments/assets/0a490c57-5627-4d5f-a495-fac82730f663" />


- **​Coverage Analysis**: Real-time rendering of Line-of-Sight (LOS) coverage areas and comprehensive orbital characteristics.
<img width="45%" alt="image" src="https://github.com/user-attachments/assets/5cdf9629-5ae7-415e-8fa7-5ec3a2c953ef" />
<img width="45%" alt="image" src="https://github.com/user-attachments/assets/d13e7e8d-61ab-408d-b802-3fe6b8c56259" />

- **​TLE Data Integration**: Efficiently load and parse TLE data for individual satellites or entire constellations.
<img width="100%" alt="image" src="https://github.com/user-attachments/assets/74b80b20-6c57-4fa9-a426-4469445156a4" />

- **​Customization**: Deeply configurable theming and functional options to suit professional or personal preferences.

- **Minimal Footprint and High Performance**: Developed in pure C utilizing the Raylib framework, TLEscope maintains a minimal footprint. The application provides high-performance rendering that is likely more efficient than your standard system file browser, even with hundreds or thousands of satellites on-screen.


- **​Native OS Support**: Built for Linux and Windows.

### ​**Design Philosophy**
​Most existing orbital tracking software suffers from dated, unintuitive interfaces. TLEscope bridges this gap by prioritizing both visual clarity and ease of use.
​The project is heavily influenced by the Kerbal Space Program map view and Blender-style camera navigation, offering a familiar and fluid control scheme for researchers and enthusiasts alike.

### **​Development & Contributions**
​TLEscope is an evolving project with a rich roadmap. While TLE data importing is currently a very manual process, full integration is planned for future releases. We welcome bug reports, feature requests, and code contributions via the project's issue tracker.

### **Download**
To download TLEscope, grab a portable zip from the [Relases tab](https://github.com/aweeri/TLEscope/releases), then extract it's contents into a directory of choice.
You can choose between nightly and complete relases:
- **Stable** relases are properly versioned notable milestone builds. They may not have the latest features, but they are a stable and safe choice. 
- **Nightly** relases are always up to date with the latest commits, as long as they [compile correctly](https://github.com/aweeri/TLEscope/actions). Do not complain too much if things don't work as expected.
