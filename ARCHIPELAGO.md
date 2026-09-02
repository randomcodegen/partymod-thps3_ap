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

APCpp source is pinned in `thirdparty/APCpp`.
vcpkg supplies SDL2, IXWebSocket, and JsonCpp. 
Dependency and license details are in `docs/THIRD_PARTY.md`.

## Install

Install `thps3.apworld` in Archipelago, then install the US English THPS3 1.01
update on top of your THPS3 installation if you haven't done so yet. 
Extract every file from `partymod-thps3_ap-*.zip` directly into the
THPS3 installation directory beside the unmodified `Skate3.exe`.
Run `partypatcher.exe` once to validate `Skate3.exe` and create `THPS3.exe`. 
Modified, cracked, or unsupported executables are rejected.

Edit the `[Archipelago]` connection settings in `partymod.ini`, optionally use
`partyconfig.exe` for graphics and controls, then launch `THPS3.exe`. See
`docs/SUPPORTED_BUILDS.md` for hashes.

The release DLL is unsigned. Windows may show a download or reputation
warning.

Default connection settings are:

```ini
[Archipelago]
Server=archipelago.gg:3333
Slot=Player1
Password=
```

## License notes

PartyMod is MIT-licensed. APCpp is LGPL-2.1. The bundled Onani HUD glyphs
are permitted for non-commercial projects.
