THPS3 ARCHIPELAGO CLIENT (BASED ON PARTYMOD 1.1.6)

This is a patch for THPS3 1.01 to improve its input handling as well as smooth out a few other parts of the PC port.
The patch is designed to keep the game as original as possible, and leave its files unmodified.

Features and Fixes:
- Replaced input system entirely with new, modern system using the SDL2 library
- Movement stick now controls menus
- Improved cursor handling, no longer moving the cursor and only showing it when relevant
- Improved window handling allowing for custom resolutions and configurable windowing
- Fixed aspect ratio to be based on window dimensions (previously it was based on the PS2 framebuffer at 10:7/640x448)
- The game no longer opens the game's launcher when run directly
- Replaced configuration files with new INI-based system (see partymod.ini)
- Custom configurator program to handle new configuration files
- Fixed ledge warp bugs where the skater is teleported down farther than intended
- Fixed visually missing geometry in various areas (notably, the airport entrance and destructable wall in Skater Island)
- Fixes music randomization having the same sequence between sessions
- Fixes the skater's shadow not appearing transparent
- Fixes missing sounds after several retries
- Connects to alternative online services (defaults to OpenSpy)
- Fixes network interface binding issues (hosting servers works now!  remember to forward ports 5150-5151 (as usual) as well as 6500)
- Optionally adds a single level practice mode for speedrunning
- Optionally removes the trick cap for combo multipliers

INSTALLATION:
1. Install the US English THPS3 1.01 update. The game directory must contain the unmodified Skate3.exe; cracked executables are not supported. Remove the widescreen mod if installed by deleting dinput8.dll.
2. Extract EVERY FILE from partymod-thps3_ap-*.zip directly into the THPS3 installation directory, beside Skate3.exe.
3. From the THPS3 installation directory, run partypatcher.exe once. It validates Skate3.exe and creates THPS3.exe. If validation fails, restore the original US English 1.01 executable.
4. Edit the [Archipelago] connection settings in partymod.ini. Optionally run partyconfig.exe to configure graphics and controls.
5. Launch the game with the newly created THPS3.exe, not Skate3.exe.

NOTE: if the game is installed into the "Program Files" directory, you may need to run each program as administrator. 
Also, if the game is installed into the "Program Files" directory, save files will be saved in the C:\Users\<name>\AppData\Local\VirtualStore directory.  
For more information, see here: https://answers.microsoft.com/en-us/windows/forum/all/please-explain-virtualstore-for-non-experts/d8912f80-b275-48d7-9ff3-9e9878954227
