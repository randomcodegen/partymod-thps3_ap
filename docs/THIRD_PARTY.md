# Third-party notices and reproducible source

The combined DLL is built from:

| Component | Source/version | License |
| --- | --- | --- |
| PartyMod | v1.1.6, commit `60664e11aec2ccc60a9abf255ca869dc9587cd47` | MIT (`LICENSE`) |
| APCpp | `projects/APCpp` working-copy snapshot, based on `5eb74d0ee8df27fa1033c2c38d3e03ab1c951241` | LGPL-2.1 (`thirdparty/APCpp/LICENSE`) |
| IXWebSocket | 11.4.5 | BSD-3-Clause (`thirdparty/licenses/ixwebsocket.txt`) |
| JsonCpp | 1.9.6 | Public Domain/MIT (`thirdparty/licenses/jsoncpp.txt`) |
| SDL2 | 2.30.11 | zlib (`thirdparty/licenses/sdl2.txt`) |
| mbedTLS | 3.6.1, via IXWebSocket | Apache-2.0/GPL-2.0-or-later (`thirdparty/licenses/mbedtls.txt`) |
| zlib | 1.3.1, via IXWebSocket | zlib (`thirdparty/licenses/zlib.txt`) |

Console, popup and HUD text loads the combo font directly from the user's installed `Data/fonts/trick.fnt`.  
The original game font is not bundled in this project or the DLL.

APCpp's corresponding source is included in `thirdparty/APCpp`.  

The PartyMod fork contains the complete source and build files needed to modify APCpp and relink the DLL.  
Set `VCPKG_ROOT` and use the commands in `ARCHIPELAGO.md`.  
The vcpkg baseline in `vcpkg.json` pins the package recipes used for the build.

Upstreams:

- PartyMod: https://github.com/PARTYMANX/partymod-thps3
- APCpp: https://github.com/randomcodegen/APCpp

NeverScript commit `77baa233a58c25e271399c534fc6ef89bbb43c06`
was used during development to inspect and compile THPS3 scripts. It is not
included and is not a build or runtime dependency.
