"""Run the actual permission functions against fake skater memory (requires g++)."""

from pathlib import Path
import subprocess
import tempfile


def test_special_manual_permissions() -> None:
    source = (Path(__file__).parents[1] / "bridge/src/bridge_c_api.cpp").read_text()

    def section(start: str, end: str) -> str:
        return source.split(start, 1)[1].split(end, 1)[0]

    program = r"""
#include <array>
#include <cassert>
#include <cstdint>
#define THPS3AP_TRICK_MANUAL 4
#define THPS3AP_TRICK_SPECIAL 6
std::array<std::uint32_t, 0x5600 / 4> memory{};
bool manuals = false, specials = false;
std::uint8_t* CurrentSkater() {
    return reinterpret_cast<std::uint8_t*>(memory.data());
}
bool THPS3AP_IsTrickCategoryUnlocked(int category) {
    return category == THPS3AP_TRICK_MANUAL ? manuals : specials;
}
"""
    program += "std::array<std::uint32_t, 10> g_saved_manual_tricks" + section(
        "std::array<std::uint32_t, 10> g_saved_manual_tricks",
        "std::uint32_t g_saved_lip_trick_script_pointer",
    )
    program += "struct SavedSpecialQueue" + section(
        "struct SavedSpecialQueue", 'extern "C" void patchDWord',
    )
    program += "void ApplyManualTrickPermission()" + section(
        "void ApplyManualTrickPermission()", "void ApplyLipTrickPermission()",
    )
    program += "void ApplySpecialTrickPermission()" + section(
        "void ApplySpecialTrickPermission()", "struct QbStructHeader",
    )
    program += r"""
void reset() {
    memory.fill(0);
    g_saved_manual_trick_count = 0;
    g_saved_manual_trick_skater = 0;
    g_saved_special_queues = {};
    g_saved_special_queue_skater = 0;
    for (auto offset : {0x5488, 0x54bc, 0x5504}) {
        memory[offset / 4] = 2;
        memory[offset / 4 + 1] = 101;
        memory[offset / 4 + 2] = 102;
    }
    for (auto offset : {0x54b4, 0x54e8, 0x5530})
        memory[offset / 4] = 999;
}
void frame(int permissions) {
    manuals = permissions & 1;
    specials = permissions & 2;
    ApplyManualTrickPermission();
    ApplySpecialTrickPermission();
    assert(memory[0x54bc / 4] == (manuals ? 2u : 0u));
    assert(memory[0x54e8 / 4] == (manuals && specials ? 999u : 0u));
    assert(memory[0x54b4 / 4] == (specials ? 999u : 0u));
    assert(memory[0x5530 / 4] == (specials ? 999u : 0u));
}
int main() {
    // All initial states, unlock orders, repeated frames, and relocks.
    for (int first = 0; first < 4; ++first) {
        for (int second = 0; second < 4; ++second) {
            reset();
            frame(first);
            frame(first);
            frame(second);
            frame(second);
            frame(3);
        }
    }
    // Do not restore an old special after the regular trick mapping changes.
    reset();
    frame(1);
    memory[0x54c0 / 4] = 123;
    specials = true;
    ApplySpecialTrickPermission();
    assert(memory[0x54e8 / 4] == 0);
}
"""
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "special_manuals.cpp"
        executable = Path(directory) / "special_manuals.exe"
        cpp.write_text(program)
        subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    test_special_manual_permissions()
