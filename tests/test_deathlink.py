"""Exercise the native bail integration with fake game memory and engine calls."""

from pathlib import Path
import subprocess
import tempfile


def test_deathlink_bail_integration():
    root = Path(__file__).parents[1]
    source = (root / "bridge/src/bridge_c_api.cpp").read_text()
    function = (
        "void ApplyDeathLinkBail() {"
        + source.split("void ApplyDeathLinkBail() {", 1)[1].split(
            "void ApplyManualTrickPermission()", 1
        )[0]
    )
    function = function.replace(
        "reinterpret_cast<SwitchScript>(0x004c2780)", "SwitchScriptStub"
    )
    code = r"""
#include "thps3_ap/deathlink.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#define __thiscall
alignas(void*) std::array<std::uint8_t, 0x9400> memory{};
bool present = true, paused = false, selected = true;
int forced = 0;
std::atomic_uint32_t g_current_career_level{1};
std::uint8_t* CurrentSkater() { return present ? memory.data() : nullptr; }
bool IsGamePaused() { return paused; }
bool THPS3AP_IsSelectedSkaterActive() { return selected; }
std::uint32_t QbChecksum(const char* name) {
    assert(std::strcmp(name, "NoseManualBail") == 0);
    return 0x918d4817;
}
void SwitchScriptStub(void* skater, std::uint32_t script, void* params) {
    assert(skater == memory.data() && script == 0x918d4817 && params == nullptr);
    memory[0x83b0] = 1;
    ++forced;
}
"""
    code += function + r"""
int main() {
    auto& state = thps3_ap::DeathLink();
    state.SetEnabled(true);
    state.SetConnected(true);
    *reinterpret_cast<void**>(memory.data() + 0x9c) = memory.data();
    memory[0x84cc] = 1;
    memory[0x83b0] = 1;
    ApplyDeathLinkBail(); assert(state.TakeSend());
    ApplyDeathLinkBail(); assert(!state.TakeSend());
    memory[0x83b0] = 0; ApplyDeathLinkBail();
    assert(state.Receive());
    for (int condition = 0; condition < 6; ++condition) {
        paused = condition == 0;
        present = condition != 1;
        selected = condition != 2;
        g_current_career_level = condition == 3 ? 0 : 1;
        memory[0x84cc] = condition == 4 ? 0 : 1;
        *reinterpret_cast<void**>(memory.data() + 0x9c) = condition == 5 ? nullptr : memory.data();
        ApplyDeathLinkBail(); assert(forced == 0);
    }
    *reinterpret_cast<void**>(memory.data() + 0x9c) = memory.data();
    ApplyDeathLinkBail(); assert(forced == 1);
    ApplyDeathLinkBail(); assert(!state.TakeSend());
    ApplyDeathLinkBail(); assert(forced == 1);
    assert(state.Receive());
    ApplyDeathLinkBail(); assert(forced == 1);
    memory[0x83b0] = 0; ApplyDeathLinkBail();
    memory[0x83b0] = 1; ApplyDeathLinkBail(); assert(state.TakeSend());
}
"""
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "deathlink.cpp"
        executable = Path(directory) / "deathlink.exe"
        cpp.write_text(code)
        subprocess.run(
            [
                "g++",
                "-std=c++20",
                "-I",
                str(root / "bridge/include"),
                str(cpp),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
