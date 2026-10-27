# THPS3 Archipelago Client

This fork embeds the Archipelago client and THPS3 bridge in PartyMod 1.1.6's
`partymod.dll`.

## Build

Requirements: Visual Studio 2022 with C++ support, CMake, Git, and vcpkg.
Set `VCPKG_ROOT` to the vcpkg checkout, then run:

```powershell
cmake --preset windows-x86-debug
cmake --build --preset windows-x86-release
ctest --test-dir build/partymod-x86 -C Release --output-on-failure
```

APCpp source is bundled in `thirdparty/APCpp`.  
vcpkg supplies SDL2, IXWebSocket, and JsonCpp.  
Dependency and license details are in `docs/THIRD_PARTY.md`.

## Install

Install `thps3.apworld` in Archipelago, then install the US English THPS3 1.01 update on top of your THPS3 installation.  
The client requires the [Microsoft Visual C++ v14 x86 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).  
Extract every file from `partymod-thps3_ap-*.zip` directly into the THPS3 installation  directory beside the unmodified `Skate3.exe`.  
Run `partypatcher.exe` once to validate `Skate3.exe` and create `THPS3.exe`.  
Modified, cracked, or unsupported executables are rejected.

Edit the `[Archipelago]` connection settings in `partymod.ini`, optionally use `partyconfig.exe` for graphics and controls, then launch `THPS3.exe`.  
See `docs/SUPPORTED_BUILDS.md` for hashes.

The release DLL is unsigned. Windows may show a download or reputation warning.

Default connection settings are:

```ini
[Archipelago]
Server=archipelago.gg:3333
Slot=Player1
Password=
```

When Archipelago selects Custom Skater, a slot with no saved skater gets a fully random outfit and the default regular-trick preset.

## Gameplay shortcuts

| Action | Keyboard | Controller |
| --- | --- | --- |
| Highlight the next remaining gap | Tap **F8** | Tap **LB + RB** (**L1 + R1**) together |
| Toggle all gap highlights | Hold **F8** for **1.5 seconds** | Hold **LB + RB** (**L1 + R1**) for **1.5 seconds** |
| Change normal/intact Los Angeles geometry after the next level reload | Press **F7** | Click both sticks (**L3 + R3**) |

## In-game console

Press **F1** to open or close the console. **Esc** also closes it.

- **Enter:** send chat or an Archipelago `!command`.
- **Tab:** complete commands, `!hint` item names and `!hint_location` checks.
- **Left/Right, Home/End:** move the cursor. Hold **Shift** to select.
- **Backspace/Delete:** delete text or the selection.
- **Ctrl+A/C/V:** select all, copy and paste. Pasted newlines become spaces.
- **Up/Down:** recall the last 100 commands.
- **Page Up/Down or mouse wheel:** scroll message history.

`/help` lists commands. `/server host:port`, `/port number`, `/slot name` and `/password value` stage connection settings.  
`/connect` applies them, and `/disconnect` disconnects.

Console commands:

- `/markers on/off/toggle`: show or hide markers and the AP HUD.
- `/gapmarkers bounds/tint/both/off`: choose gap marker rendering.
- `/outoflogic on/off`: include out-of-logic gaps in the gap menu and marker selection.
- `/collectibles on/off/toggle`: control saved collectible markers.
- `/gaps next/all/off`: select the next gap, toggle all highlights or clear them.
- `/geometry on/off/toggle`: use intact Los Angeles geometry after reloading.
- `/status`: show the connection status and queued check count.
- `/quit`: exit the game.

Gap marker style and out-of-logic preferences are saved in `partymod.ini`.

## DeathLink

Enable `death_link: true` in your THPS3 YAML before generating a seed to share bails with other DeathLink players.  
Incoming DeathLinks make your player bail and wait through menus and pauses until gameplay resumes.

`/deathlink_amnesty X` sends one DeathLink every **X local bails**, defaulting to 1.
