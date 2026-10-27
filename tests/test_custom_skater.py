"""Test appearance storage and initialization with fake native calls (g++)."""

from pathlib import Path
import subprocess
import tempfile


def test_custom_skater() -> None:
    source = (Path(__file__).parents[1] / "bridge/src/bridge_c_api.cpp").read_text()
    code = source.split("struct ProfileField {", 1)[1].split(
        "void SyncStatAssignment(", 1
    )[0]
    code = "struct ProfileField {" + code
    # fake memory uses host-width pointers, QB's stored numeric values remain 32 bit.
    code = code.replace("std::uint32_t value;", "std::uintptr_t value;")
    code = code.replace(
        "const char* name, std::uint32_t value",
        "const char* name, std::uintptr_t value",
    )
    code = code.replace(
        "reinterpret_cast<std::uint32_t>", "reinterpret_cast<std::uintptr_t>"
    )
    code = (
        code.replace("reinterpret_cast<CreateString>(0x004113e0)", "AllocateString")
        .replace("reinterpret_cast<int (__cdecl *)(int)>(0x0040e470)", "RandomChoice")
        .replace(
            "reinterpret_cast<void (__thiscall *)(void*)>(0x004bdbc0)(profile)",
            "NativeRandomize(profile)",
        )
    )
    program = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#define FALSE 0
bool custom = true, active = true, loaded = false;
bool choose_max = true;
int rolls = 0, writes = 0;
std::string saved, queued;
void* g_appearance_profile = nullptr;
bool g_appearance_initialized = false, g_appearance_reload_requested = false;
std::string g_last_appearance, g_server_appearance, g_pending_appearance;
std::chrono::steady_clock::time_point g_appearance_changed_at{};
std::deque<std::string> g_restored_appearance_strings;
bool THPS3AP_IsCustomSkaterSelected() { return custom; }
bool THPS3AP_IsSelectedSkaterActive() { return active; }
namespace thps3_ap {
bool AppearanceStorageLoaded() { return loaded; }
bool GetAppearance(std::string& value) { value = saved; return loaded && !saved.empty(); }
void QueueAppearance(const std::string& value) { queued = value; ++writes; }
}
std::uint32_t QbChecksum(const char* name) {
    static std::unordered_map<std::string, std::uint32_t> keys;
    return keys.try_emplace(name, keys.size() + 1).first->second;
}
int RandomChoice(int size) { assert(size > 0); return choose_max ? size - 1 : 0; }
char* AllocateString(std::uint32_t size) { return new char[size]; }
void NativeRandomize(void*) { ++rolls; }
"""
    program += code + r"""
int main() {
    alignas(void*) std::array<std::uint8_t, 0x700> profile{};
    ProfileField body_value{13, QbChecksum("value"), QbChecksum("male"), nullptr};
    ProfileField* body_children = &body_value;
    ProfileField colors[] = {
        {1, QbChecksum("h"), 0, nullptr}, {1, QbChecksum("s"), 50, nullptr},
        {1, QbChecksum("v"), 100, nullptr}, {1, QbChecksum("use_default_hsv"), 1, nullptr}
    };
    for (int i = 0; i < 3; ++i) colors[i].next = &colors[i + 1];
    ProfileField* color_children = colors;
    ProfileField skin_colors[] = {
        {1, QbChecksum("h"), 300, nullptr}, {1, QbChecksum("s"), 88, nullptr},
        {1, QbChecksum("v"), 0, nullptr}, {1, QbChecksum("use_default_hsv"), 0, nullptr}
    };
    ProfileField head_colors[] = {
        {1, QbChecksum("h"), 300, nullptr}, {1, QbChecksum("s"), 88, nullptr},
        {1, QbChecksum("v"), 0, nullptr}, {1, QbChecksum("use_default_hsv"), 0, nullptr}
    };
    for (int i = 0; i < 3; ++i) {
        skin_colors[i].next = &skin_colors[i + 1];
        head_colors[i].next = &head_colors[i + 1];
    }
    ProfileField* skin_children = skin_colors;
    ProfileField* head_children = head_colors;
    ProfileField skin{10, QbChecksum("skin"), reinterpret_cast<std::uintptr_t>(&skin_children), nullptr};
    ProfileField head{10, QbChecksum("head"), reinterpret_cast<std::uintptr_t>(&head_children), &skin};
    ProfileField torso{10, QbChecksum("torso"), reinterpret_cast<std::uintptr_t>(&color_children), &head};
    ProfileField body{10, QbChecksum("body_type"), reinterpret_cast<std::uintptr_t>(&body_children), &torso};
    *reinterpret_cast<ProfileField**>(profile.data() + 0x130) = &body;
    ProfileField info[] = {
        {4, QbChecksum("display_name"), reinterpret_cast<std::uintptr_t>("Custom Skater"), nullptr},
        {3, QbChecksum("hometown"), reinterpret_cast<std::uintptr_t>("Los Angeles, CA"), nullptr},
        {1, QbChecksum("age"), 16, nullptr}, {13, QbChecksum("stance"), 0x2bfc8eb5u, nullptr},
        {13, QbChecksum("trickstyle"), QbChecksum("street"), nullptr},
        {13, QbChecksum("pushstyle"), QbChecksum("never_mongo"), nullptr},
        {13, QbChecksum("default_trick_mapping"), QbChecksum("CustomTricks"), nullptr},
        {1, QbChecksum("speed"), 5, nullptr}
    };
    for (int i = 0; i < 7; ++i) info[i].next = &info[i + 1];
    *reinterpret_cast<ProfileField**>(profile.data() + 0x694) = info;
    SyncAppearance(profile.data());
    assert(rolls == 0 && writes == 0); // Storage still loading.
    loaded = true;
    active = false;
    SyncAppearance(profile.data());
    assert(rolls == 0 && writes == 0); // Selected skater still loading.
    active = true;
    SyncAppearance(profile.data());
    assert(rolls == 1 && writes == 1 && g_appearance_reload_requested);
    assert(body_value.value == QbChecksum("female"));
    assert(colors[0].value == 355 && colors[1].value == 100 && colors[2].value == 100);
    assert(colors[3].value == 0 && info[2].value == 80 && info[7].value == 5);
    assert(std::string(reinterpret_cast<const char*>(info[0].value)) == "Taylor Clark");
    assert(info[6].value == QbChecksum("ThomasTricks_default"));
    assert(info[4].value == QbChecksum("street")); // Keep AP's generated logic valid.
    assert(skin_colors[0].value == 180 && skin_colors[1].value == 100);
    assert(skin_colors[2].value == 100 && skin_colors[3].value == 1);
    assert(head_colors[0].value == 0 && head_colors[1].value == 50);
    assert(head_colors[2].value == 100 && head_colors[3].value == 1);
    saved = queued;
    // Create New restores the slot preset, including details/tricks, without
    // resetting career stats or drawing another random skater.
    colors[0].value = 10;
    info[2].value = 12;
    info[6].value = QbChecksum("CustomTricks");
    info[7].value = 9;
    SyncAppearance(profile.data()); // An unsaved edit is pending.
    saved.clear(); // The initial server reply may still be pending.
    assert(RestoreCustomSkaterPreset(profile.data()));
    assert(colors[0].value == 355 && info[2].value == 80);
    assert(info[6].value == QbChecksum("ThomasTricks_default"));
    assert(info[7].value == 9 && rolls == 1 && writes == 1);
    assert(g_pending_appearance.empty());
    saved = queued;
    active = false;
    assert(!RestoreCustomSkaterPreset(profile.data()));
    active = true;
    loaded = false;
    assert(!RestoreCustomSkaterPreset(profile.data()));
    loaded = true;
    assert(!RestoreCustomSkaterPreset(nullptr));
    colors[0].value = 0;
    info[2].value = 16;
    g_appearance_profile = nullptr; // Restart: restore both separate structures.
    assert(RestoreCustomSkaterPreset(profile.data()));
    assert(rolls == 1 && writes == 1 && colors[0].value == 355 && info[2].value == 80);
    g_appearance_reload_requested = false;
    SyncAppearance(profile.data());
    assert(rolls == 1 && writes == 1 && !g_appearance_reload_requested);
    // Repair a saved black tint without rerolling the skater's other choices.
    skin_colors[2].value = 0;
    skin_colors[3].value = 0;
    head_colors[2].value = 0;
    head_colors[3].value = 0;
    saved = CaptureAppearance(profile.data());
    SyncAppearance(profile.data());
    assert(skin_colors[2].value == 100 && skin_colors[3].value == 1);
    assert(head_colors[2].value == 100 && head_colors[3].value == 1);
    assert(writes == 2 && rolls == 1 && queued != saved);
    saved = "A1;"; // Older/incomplete records must not cause a reload every frame.
    SyncAppearance(profile.data());
    g_appearance_reload_requested = false;
    SyncAppearance(profile.data());
    assert(!g_appearance_reload_requested && rolls == 1);
    const int before_edit = writes;
    // Local edits can be stored without repeatedly restoring the previous reply.
    colors[0].value = 5;
    SyncAppearance(profile.data());
    g_appearance_changed_at -= std::chrono::seconds(1);
    SyncAppearance(profile.data());
    assert(writes == before_edit + 1);
    SyncAppearance(profile.data());
    assert(colors[0].value == 5 && !g_appearance_reload_requested);
    custom = false;
    assert(!RestoreCustomSkaterPreset(profile.data()));
    saved.clear();
    g_appearance_profile = nullptr;
    SyncAppearance(profile.data());
    assert(rolls == 1); // Professional skaters never get randomized.
    custom = true;
    choose_max = false; // Worst case: every random brightness draw is zero.
    RandomizeCustomSkater(profile.data());
    assert(skin_colors[2].value == 100 && skin_colors[3].value == 1);
    assert(head_colors[2].value == 100 && head_colors[3].value == 1);
}
"""
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "custom_skater.cpp"
        executable = Path(directory) / "custom_skater.exe"
        cpp.write_text(program)
        subprocess.run(
            ["g++", "-std=c++20", str(cpp), "-o", str(executable)], check=True
        )
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    test_custom_skater()
