# 🎵 Simple Music Visualizer

A real-time audio visualizer built with **SDL2**, **SDL2_gfx**, and **Windows Core Audio API**. Captures system audio and displays frequency bars and beat-reactive visuals.

---

## 🧰 Features
- mono version
- Realtime system audio capture
- Spectrum-based bar animation
- Beat & amplitude detection
- GPU-accelerated rendering via SDL2
- Simulation mode fallback if audio capture fails

## 🛠 Prerequisites & Installation

Before building and running the project, make sure you have the following installed and configured:

---
Running the Visualizer
Run the compiled executable (F5 or Ctrl + F5 in Visual Studio).
The visualizer uses Windows Core Audio APIs for system audio capture. Make sure your default playback device supports loopback recording.

If audio capture is unavailable or unsupported, the program falls back to simulation mode to display a dummy animation.

Tested on Windows 10 and 11 with Visual Studio 2022.

Play some audio on your system (music, YouTube, etc.).

The visualizer will display frequency bars reacting to system audio.

Use keyboard controls:

SPACE: Print debug audio info to console

ESC: Quit the application


### 1. Visual Studio 2022

- Download and install [Visual Studio 2022 Community Edition](https://visualstudio.microsoft.com/vs/)
- During installation, select the **Desktop development with C++** workload to get the required C++ toolchain and Windows SDK.

 
![Untitled](https://github.com/user-attachments/assets/fc4b78d9-5fa8-456c-9f23-167a186404b8)


### 2. vcpkg (C++ Package Manager)

We use **vcpkg** to manage SDL2 and SDL2_gfx dependencies easily.

#### a. Clone and bootstrap vcpkg

Open **PowerShell** or **Command Prompt** and run:

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install sdl2 sdl2-gfx
.\vcpkg integrate install


