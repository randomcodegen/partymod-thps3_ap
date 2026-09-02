# Supported game build

Only the unmodified US English THPS3 1.01 executable is accepted as patch input.

| Role | File | SHA-256 | Size |
| --- | --- | --- | ---: |
| US English 1.01 patch input | `Skate3.exe` | `b895a02ba2928516f0fcc165f6a02df374cb66a942d07ef3f7036eb5540d5c98` | 1,908,736 |
| PartyMod-patched runtime | `THPS3.exe` | `1b67409414fc37a406d288098232b9947b21cdc16c80e014e580aa386bd57fef` | 1,912,832 |
| Stock PartyMod 1.1.6 | `partymod.dll` | `846f2d5abe82c9781e6c2d5cd6cbe12b930286ee0912dd01b25ad5f017848f5a` | 37,888 |


## Manual installation

1. Install the THPS3 and the 1.01 update and close the game.
2. Extract every file from `partymod-thps3_ap-*.zip` directly into the game
   directory beside the unmodified `Skate3.exe`.
3. Run `partypatcher.exe` from the game directory. It validates `Skate3.exe`
   before creating `THPS3.exe`.
4. Edit the `[Archipelago]` settings in `partymod.ini`, optionally configure
   graphics and controls with `partyconfig.exe`, and launch `THPS3.exe`.

Modified or cracked executables are rejected before `THPS3.exe` is written.
