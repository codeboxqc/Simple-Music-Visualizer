🎵 Simple Music Visualizer
A real-time audio visualizer built with SDL2, SDL2_gfx, and Windows Core Audio API. Captures system audio and displays frequency bars and beat-reactive visuals with a dynamic plasma color palette for vibrant, music-synchronized animations.
🧰 Features

Mono Audio Capture: Real-time system audio capture using Windows Core Audio APIs.
Spectrum-Based Bar Animation: Displays frequency bars that react to audio input, enhanced with a plasma color palette ("Ice and Frost" or "Neon Dreams") for visually striking effects.
Beat & Amplitude Detection: Visuals pulse with the music’s beat and amplitude.
GPU-Accelerated Rendering: Leverages SDL2 for smooth, hardware-accelerated graphics.
Simulation Mode: Fallback mode with dummy animations if audio capture is unavailable.
Customizable Visuals: Supports keyboard controls and potential web-based control via an HTML interface.

🛠 Prerequisites & Installation
Before building and running the project, ensure the following are installed and configured:
1. Visual Studio 2022

Install Visual Studio 2022 with the Desktop development with C++ workload.
Ensure CMake support is included for project configuration.

2. vcpkg (C++ Package Manager)
We use vcpkg to manage SDL2 and SDL2_gfx dependencies easily.
a. Clone and Bootstrap vcpkg
Open PowerShell or Command Prompt and run:

git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install sdl2:x64-windows sdl2-gfx:x64-windows
.\vcpkg integrate install


 

 
![Untitled](https://github.com/user-attachments/assets/fc4b78d9-5fa8-456c-9f23-167a186404b8)


🚀 Running the Visualizer

Run the compiled executable (e.g., build\Release\visualizer.exe or press F5 in Visual Studio).
Play audio on your system (e.g., music, YouTube, Spotify).
The visualizer displays frequency bars that react to the audio, using a plasma color palette that alternates between "Ice and Frost" (cool blues) and "Neon Dreams" (vibrant greens/pinks) for dynamic effects.
If audio capture fails, the program falls back to simulation mode, showing animated bars with dummy data.

Keyboard Controls

SPACE: Print debug audio info (e.g., amplitude, beat intensity) to the console.
C: Cycle through visualization curve types.
ESC: Quit the application.

Visualizer Features

Plasma Color Palette: The spectrum bars use a modified plasma palette, randomly selecting between "Ice and Frost" (index 0) and "Neon Dreams" (index 5) for smooth color transitions. The bars’ hue, saturation, and brightness adjust dynamically based on audio input and beat detection.
Audio-Reactive Animation: Bar heights and colors respond to audio frequencies and amplitude, enhanced by beat detection for rhythmic pulsing.


