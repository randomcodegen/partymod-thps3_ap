# Supported game build

Only English THPS3 1.01 patched by PartyMod v1.1.6 is supported.

| Role | File | SHA-256 | Size |
| --- | --- | --- | ---: |
| Vanilla 1.01 baseline | `Skate3 - 1.0.1.exe` | `b895a02ba2928516f0fcc165f6a02df374cb66a942d07ef3f7036eb5540d5c98` | 1,908,736 |
| PartyMod-patched runtime | `THPS3.exe` | `1b67409414fc37a406d288098232b9947b21cdc16c80e014e580aa386bd57fef` | 1,912,832 |
| Stock PartyMod 1.1.6 | `partymod.dll` | `846f2d5abe82c9781e6c2d5cd6cbe12b930286ee0912dd01b25ad5f017848f5a` | 37,888 |

All three are PE32 x86. The PartyMod readme has a stale 1.1.3 heading; use the
hash above to identify v1.1.6.

## Manual installation

1. Close THPS3.
2. Back up the stock `partymod.dll` from the game directory.
3. Copy the AP Release DLL into that directory as `partymod.dll`.
4. Copy the supplied `partymod.ini` beside it and edit the connection details.

To uninstall, close the game and restore the backed-up DLL. The bridge checks
the executable identity and refuses to install hooks on an unknown build.

