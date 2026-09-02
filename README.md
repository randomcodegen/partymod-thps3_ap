# PARTYMOD for THPS3
This is a patch for THPS3 1.01 to improve its input handling as well as smooth out a few other parts of the PC port.
The patch is designed to keep the game as original as possible, and leave its files unmodified.

## 🎉 PARTYMOD 🎉
PARTYMOD is a series of patches that provide various fixes and modernizations for the THPS series and other games on their engines.
[Other PARTYMOD Releases Available Here](https://partymod.newnet.city/)

## Features and Fixes
* Replaced input system entirely with new, modern system using the SDL2 library
* Movement stick now controls menus
* Improved cursor handling, no longer moving the cursor and only showing it when relevant
* Improved window handling allowing for custom resolutions and configurable windowing
* Fixed aspect ratio to be based on window dimensions (previously it was based on the PS2 framebuffer at 10:7/640x448)
* The game no longer opens the game's launcher when run directly
* Replaced configuration files with new INI-based system (see partymod.ini)
* Custom configurator program to handle new configuration files
* Fixed ledge warp bugs where the skater is teleported down farther than intended
* Fixed visually missing geometry in various areas (notably, the airport entrance and destructable wall in Skater Island)
* Fixes the skater's shadow not appearing transparent
* Fixes missing sounds after several retries
* Connects to alternative online services (defaults to OpenSpy) 
* Fixes network interface binding issues (hosting servers works now!  remember to forward ports 5150-5151 (as usual) as well as 6500)
* Optionally adds a single level practice mode for speedrunning
* Optionally removes the trick cap for combo multipliers

## Installation
1. Install THPS3 and the 1.01 patch. 
The game directory must contain the unmodified `Skate3.exe`; cracked executables are not supported. Remove the widescreen mod if installed by deleting `dinput8.dll`.
1. Extract **every file** from `partymod-thps3_ap-*.zip` directly into the THPS3 installation directory, beside `Skate3.exe`.
2. From that directory, run `partypatcher.exe` once. 
It validates `Skate3.exe` and creates the supported `THPS3.exe`. 
If validation fails, restore the original US English 1.01 executable.
3. Edit the `[Archipelago]` connection settings in `partymod.ini`. 
Optionally run `partyconfig.exe` to configure graphics and controls.
4. Launch the game with the newly created `THPS3.exe`, not `Skate3.exe`.

NOTE: if the game is installed into the "Program Files" directory, you may need to run each program as administrator. 
Also, if the game is installed into the "Program Files" directory, save files will be saved in the C:\Users\<name>\AppData\Local\VirtualStore directory.  
For more information, see here: https://answers.microsoft.com/en-us/windows/forum/all/please-explain-virtualstore-for-non-experts/d8912f80-b275-48d7-9ff3-9e9878954227

## Building
The build requires CMake and SDL2 (I install it via vspkg).  Create the project file like so from the partymod-thps3/build directory:
```
cmake .. -A win32 -DCMAKE_TOOLCHAIN_FILE=C:/[vcpkg directory]/scripts/buildsystems/vcpkg.cmake
```

Set the optimization optimization for the partymod dll to O0 (disable optimization) because MSVC seems to break certain functions when optimization is enabled.
Additionally, set the SubSystem to "Windows (/SUBSYSTEM:WINDOWS)" in the partyconfig project.
