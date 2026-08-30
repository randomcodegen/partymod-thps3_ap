# Native THPS3 bridge

This directory contains the 32-bit Windows bridge used by the combined
PartyMod/Archipelago DLL.

The current bridge:

- Creates `\\.\pipe\thps3_archipelago`.
- Implements the protocol-v1 handshake.
- Applies AP level-access state to the career selector.
- Reports regular-goal and competition-medal location events off the game
  thread.
- Reports stable stat-point location events off the game thread.
- Parses normalized per-level career checks and mirrors checked objective,
  stat-point, and hidden-deck bits into the active profile without calling
  save or reward code.
- Enforces received level, objective, trick-category, skater, stat-point, and
  timer state.
- Reports accepted gaps and renders the gap guide and AP status HUD.
- Displays Archipelago messages through THPS3's five-line chat layout.

The production bridge is compiled into a PartyMod-derived 32-bit DLL so
PartyMod and Archipelago coordinate hook ownership. LevelMod is not required.

The exact PartyMod-patched `THPS3.exe` and original `partymod.dll` hashes are
recorded, and the combined DLL has passed an x86/import/export build audit.
The objective mirror owns career bits 0 through 8 on regular levels and bits 0
through 2 on competitions. The collectible mirror owns the five verified
stat-point bits and the adjacent hidden-deck bit independently, only when each
type is configured as locations; unrelated pickup state remains untouched. Snapshot reconciliation replaces
those owned bits directly and invokes no QScript completion, reward,
progression, or save path. Without stat-point locations, vanilla stat-point
spawning and awards remain active.

The client exports game access and available objective-location masks separately,
so dependency-only goal unlocks activate scripts without inflating menu totals.
The dynamic `GetGoal` hook (checksum
`0xA571BE3E`) treats a locked goal as already handled only while `GameStart`
is registering level content, in collectible setup, and in stock QB progress
guards whose true branch explicitly means "already handled." Every other
query treats it as incomplete so level-status, completion, and career-reward
paths cannot count it. Objective script headers are never rewritten because
THPS3's loader owns their body pointers across level transitions.
The dynamic `JustGotGoal` hook (checksum `0xB12E13D0`) suppresses locked completion
feedback.

The dynamic `SetGoal` hook (checksum `0x36E006E3`) still calls vanilla exactly
once and observes the resulting career-word transition without rereading
QScript parameters. Unlocked transitions queue `location_event` frames and
remain in a process-local pending mask until an authoritative checked-location
snapshot acknowledges them. Locked transitions are immediately removed by the
in-memory mirror. A main-thread word-delta detector applies the same access
test to score goals and medals that are set outside `SetGoal`.

Configure with Visual Studio 2026:

```powershell
cmake --preset windows-x86-debug
cmake --build --preset windows-x86-debug
```

The x86 check is mandatory because THPS3 is a 32-bit process.

## PartyMod-integrated build

The production target is the repository root, which compiles the same bridge
runtime into `partymod.dll`. It requires:

- Visual Studio 2026 with the MSVC x86/x64 toolset.
- A Windows SDK.
- CMake.
- The official SDL 2.32.8 Visual C++ development package under
  `build/deps/SDL2-2.32.8`.

Configure and build:

```powershell
cmake --preset windows-x86-debug
cmake --build --preset windows-x86-release
```

PartyMod requires optimization to remain disabled. The combined target enforces
`/Od` and uses the same dynamic MSVC runtime model as the distributed PartyMod
1.1.6 DLL.
