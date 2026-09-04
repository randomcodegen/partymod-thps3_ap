#include "thps3_ap/bridge_c_api.h"
#include "thps3_ap/game_state.hpp"
#include "thps3_ap/bridge_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

#include "onani_hud_font.inc"

std::atomic_bool g_bridge_started = false;
std::atomic_bool g_collectible_markers_enabled = false;
std::atomic_bool g_use_intact_los_angeles_geometry = false;
std::atomic_bool g_level_menu_refresh_requested = false;
std::atomic_bool g_level_transition = false;
std::atomic_uint32_t g_current_career_level = 0;
std::mutex g_debug_log_mutex;
std::once_flag g_debug_log_init;
char g_debug_log_path[MAX_PATH]{};

constexpr std::uint32_t CareerBaseSeconds(std::uint32_t level) {
    return level == 3 || level == 6 || level == 8 ? 60u : 120u;
}

static_assert(CareerBaseSeconds(3) == 60u && CareerBaseSeconds(1) == 120u);
constexpr std::array<std::array<std::uint32_t, 3>, 9> kCareerScoreGoals{{
    {{10'000u, 30'000u, 60'000u}},
    {{35'000u, 70'000u, 120'000u}},
    {{0u, 0u, 0u}},
    {{55'000u, 110'000u, 200'000u}},
    {{75'000u, 150'000u, 300'000u}},
    {{0u, 0u, 0u}},
    {{100'000u, 190'000u, 400'000u}},
    {{0u, 0u, 0u}},
    {{150'000u, 225'000u, 500'000u}},
}};
static_assert(kCareerScoreGoals[6][2] == 400'000u);
constexpr std::size_t kGapMenuRowCount = 106;
constexpr std::uint32_t kGapHighlightTint = 0x4040b8ffu;
constexpr std::uint32_t kGapHighlightColor = 0xff40b8ffu;
constexpr std::uint32_t kStatPointHighlightColor = 0xffff8000u;
constexpr std::uint32_t kDeckHighlightColor = 0xff00ff00u;
std::mutex g_gap_menu_mutex;
std::string g_gap_menu_title;
std::array<std::string, kGapMenuRowCount> g_gap_menu_rows;
std::array<std::uint32_t, kGapMenuRowCount> g_gap_menu_checksums{};
std::size_t g_gap_menu_row_count = 0;
std::atomic_bool g_gap_menu_pending = false;
std::atomic_bool g_gap_highlight_validation_pending = false;
std::atomic_uint32_t g_gap_highlight_pending_row = 0xffffffffu;
std::atomic_bool g_gap_highlight_pending_auto = false;
std::atomic_bool g_gap_advance_requested = false;
std::atomic_bool g_all_gap_highlights_toggle_requested = false;
std::atomic_bool g_all_gap_highlights_refresh_pending = false;
bool g_all_gap_highlights_enabled = false;
std::uint32_t g_gap_highlighted_checksum = 0;
std::size_t g_gap_highlighted_row = kGapMenuRowCount;
std::size_t g_gap_menu_preferred_focus_row = kGapMenuRowCount;
std::array<std::uint32_t, kGapMenuRowCount> g_gap_auto_order{};
std::size_t g_gap_auto_order_count = 0;
std::size_t g_gap_auto_cursor = kGapMenuRowCount;
bool g_gap_auto_enabled = false;
std::string g_active_gap_hud_text;

constexpr std::size_t NeighborGapMenuRow(
    std::size_t removed_row, std::size_t remaining_count) {
    return removed_row == kGapMenuRowCount || remaining_count == 0
        ? kGapMenuRowCount
        : (std::min)(removed_row, remaining_count - 1);
}

static_assert(NeighborGapMenuRow(2, 4) == 2);
static_assert(NeighborGapMenuRow(3, 3) == 2);
static_assert(NeighborGapMenuRow(0, 0) == kGapMenuRowCount);
std::atomic_uint32_t g_highlighted_cassette_level = 0;
std::array<std::atomic_uint32_t, 9> g_pending_goal_masks{};
std::array<std::atomic_bool, 9> g_pending_decks{};
void* g_stat_assignment_profile = nullptr;
std::array<std::uint32_t, 10> g_last_stat_assignment{};
void* g_appearance_profile = nullptr;
bool g_appearance_initialized = false;
std::string g_last_appearance;
std::string g_pending_appearance;
std::chrono::steady_clock::time_point g_appearance_changed_at{};
std::deque<std::string> g_restored_appearance_strings;
std::array<std::uint32_t, 10> g_saved_manual_tricks{};
std::uint32_t g_saved_manual_trick_count = 0;
std::uintptr_t g_saved_manual_trick_skater = 0;
std::uint32_t g_saved_lip_trick_script_pointer = 0;
std::uint32_t g_saved_lip_trick_script_header = 0;

constexpr std::uint32_t kScorePerBonusItem = 100;

struct SavedSpecialQueue {
    std::uint32_t special = 0;
    std::uint32_t regular_count = 0;
    std::array<std::uint32_t, 10> regular{};
};

std::array<SavedSpecialQueue, 3> g_saved_special_queues{};
std::uintptr_t g_saved_special_queue_skater = 0;

extern "C" void patchDWord(void* address, std::uint32_t value);
extern "C" std::uint32_t THPS3AP_MainMenuScriptLoadCount();

// A valid THPS3 LipTrick script that restores Air state, moves the skater off
// the coping, reinstalls airborne exceptions, and resumes Airborne. A no-op
// return is unsafe here because native lip detection has already suspended the
// coping transition before this script starts.
std::array<std::uint8_t, 71> g_disabled_lip_trick_script{{
    0x23, 0x16, 0x96, 0xcf, 0x47, 0x16, 0x01, 0x16, 0x96, 0xbf, 0x8e, 0x94,
    0x16, 0x04, 0x47, 0x9f, 0x43, 0x01, 0x16, 0x87, 0xc8, 0xc1, 0x10, 0x16,
    0x50, 0x88, 0x2d, 0x9d, 0x07, 0x17, 0x01, 0x00, 0x00, 0x00, 0x01, 0x16,
    0x87, 0xc8, 0xc1, 0x10, 0x16, 0xea, 0xd9, 0x24, 0x04, 0x07, 0x17, 0x06,
    0x00, 0x00, 0x00, 0x01, 0x16, 0x5b, 0xb9, 0x0e, 0x50, 0x01, 0x16, 0x7f,
    0xa3, 0x9f, 0xad, 0x16, 0xf7, 0x89, 0x3c, 0xcf, 0x01, 0x24, 0x01,
}};

// GOAL_STAT_POINT1..5 own bits 23..19 in each level's pickup word.
// GOAL_DECK is the adjacent bit 24 and is mirrored independently.
constexpr std::uint32_t kOwnedStatPointCareerBits = 0x00f80000u;
constexpr std::uint32_t kDeckCareerBit = 0x01000000u;
constexpr std::uint32_t kOwnedDeckInventoryBits = 0x000003feu;
constexpr std::array<const char*, 10> kStatNames{{
    "points_available",
    "air",
    "hangtime",
    "ollie",
    "speed",
    "spin",
    "switch",
    "rail_balance",
    "lip_balance",
    "manual_balance",
}};

std::uint32_t QbChecksum(const char* name) {
    using Checksum = std::uint32_t (__cdecl *)(const char*);
    return reinterpret_cast<Checksum>(0x004265F0)(name);
}

std::uint32_t* FindProfileInteger(void* profile, std::uint32_t checksum) {
    auto* component = *reinterpret_cast<std::uint8_t**>(
        static_cast<std::uint8_t*>(profile) + 0x694);
    while (component != nullptr) {
        if (component[0] == 1 &&
            *reinterpret_cast<std::uint32_t*>(component + 4) == checksum) {
            return reinterpret_cast<std::uint32_t*>(component + 8);
        }
        component = *reinterpret_cast<std::uint8_t**>(component + 12);
    }
    return nullptr;
}

struct ProfileField {
    std::uint32_t type;
    std::uint32_t key;
    std::uint32_t value;
    ProfileField* next;
};

ProfileField* FindProfileField(ProfileField* field, std::uint32_t key) {
    while (field != nullptr && field->key != key) {
        field = field->next;
    }
    return field;
}

bool IsAppearanceRoot(std::uint32_t key) {
    static const auto keys = [] {
        constexpr std::array names{
            "body_type", "head", "hat", "hair", "jaw", "shoes",
            "torso", "legs", "kneepads", "elbowpads", "backpack",
            "glasses", "boardup", "boarddown", "helmet", "hat_logo",
            "helmet_logo", "front_logo", "back_logo", "accessories",
            "chest_tattoo", "back_tattoo", "left_arm_tattoo",
            "right_arm_tattoo", "socks", "left_leg_tattoo",
            "right_leg_tattoo", "special_item", "special_item_2", "skin",
            "height_scale", "weight_scale", "scaling_mode",
        };
        std::array<std::uint32_t, names.size()> result{};
        for (std::size_t index = 0; index < names.size(); ++index) {
            result[index] = QbChecksum(names[index]);
        }
        return result;
    }();
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool IsAppearanceScalar(std::uint32_t key) {
    static const auto keys = [] {
        constexpr std::array names{
            "display_name", "hometown", "age", "height", "weight",
            "trickstyle", "stance", "pushstyle",
        };
        std::array<std::uint32_t, names.size()> result{};
        for (std::size_t index = 0; index < names.size(); ++index) {
            result[index] = QbChecksum(names[index]);
        }
        return result;
    }();
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

bool IsStoredProfileType(std::uint32_t type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 13;
}

void AppendHex(std::string& output, std::uint32_t value, int digits) {
    constexpr char hex[] = "0123456789abcdef";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4) {
        output.push_back(hex[(value >> shift) & 0xf]);
    }
}

void AppendAppearanceField(
    std::string& output,
    std::uint32_t root,
    std::uint32_t child,
    const ProfileField& field) {
    if (!IsStoredProfileType(field.type)) {
        return;
    }
    const char* const string_value = field.type == 3 || field.type == 4
        ? reinterpret_cast<const char*>(field.value)
        : nullptr;
    if ((field.type == 3 || field.type == 4) && string_value == nullptr) {
        return;
    }
    AppendHex(output, root, 8);
    AppendHex(output, child, 8);
    AppendHex(output, field.type, 2);
    if (field.type == 3 || field.type == 4) {
        for (std::size_t index = 0;
             string_value[index] != '\0' && index < 255;
             ++index) {
            AppendHex(
                output, static_cast<unsigned char>(string_value[index]), 2);
        }
    } else {
        AppendHex(output, field.value, 8);
    }
    output.push_back(';');
}

std::string CaptureAppearance(void* profile) {
    std::string output = "A1;";
    ProfileField* field = *reinterpret_cast<ProfileField**>(
        static_cast<std::uint8_t*>(profile) + 0x694);
    for (; field != nullptr && output.size() < 32768; field = field->next) {
        if (IsAppearanceScalar(field->key)) {
            AppendAppearanceField(output, field->key, 0, *field);
        } else if (IsAppearanceRoot(field->key) &&
                   (field->type == 10 || field->type == 11) &&
                   field->value != 0) {
            auto** const nested = reinterpret_cast<ProfileField**>(field->value);
            for (ProfileField* child = *nested;
                 child != nullptr && output.size() < 32768;
                 child = child->next) {
                AppendAppearanceField(output, field->key, child->key, *child);
            }
        }
    }
    return output;
}

bool ParseHex(std::string_view text, std::uint32_t& value) {
    value = 0;
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

void ApplyAppearance(void* profile, std::string_view appearance) {
    if (!appearance.starts_with("A1;") || appearance.size() > 32768) {
        return;
    }
    ProfileField* const fields = *reinterpret_cast<ProfileField**>(
        static_cast<std::uint8_t*>(profile) + 0x694);
    std::size_t position = 3;
    while (position < appearance.size()) {
        const std::size_t end = appearance.find(';', position);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view record = appearance.substr(position, end - position);
        position = end + 1;
        if (record.size() < 18) {
            continue;
        }
        std::uint32_t root_key = 0;
        std::uint32_t child_key = 0;
        std::uint32_t stored_type = 0;
        if (!ParseHex(record.substr(0, 8), root_key) ||
            !ParseHex(record.substr(8, 8), child_key) ||
            !ParseHex(record.substr(16, 2), stored_type) ||
            !(IsAppearanceScalar(root_key) || IsAppearanceRoot(root_key))) {
            continue;
        }
        ProfileField* field = FindProfileField(fields, root_key);
        if (field == nullptr) {
            continue;
        }
        if (child_key != 0) {
            if ((field->type != 10 && field->type != 11) || field->value == 0) {
                continue;
            }
            field = FindProfileField(
                *reinterpret_cast<ProfileField**>(field->value), child_key);
        }
        if (field == nullptr || !IsStoredProfileType(field->type)) {
            continue;
        }
        const std::string_view encoded = record.substr(18);
        if ((stored_type == 3 || stored_type == 4) &&
            (field->type == 3 || field->type == 4) &&
            encoded.size() % 2 == 0 && encoded.size() <= 510) {
            std::string decoded;
            decoded.reserve(encoded.size() / 2);
            bool valid = true;
            for (std::size_t index = 0; index < encoded.size(); index += 2) {
                std::uint32_t byte = 0;
                valid &= ParseHex(encoded.substr(index, 2), byte);
                decoded.push_back(static_cast<char>(byte));
            }
            if (!valid) {
                continue;
            }
            if (field->type == 4) {
                using CreateString = char* (__cdecl *)(std::uint32_t);
                char* const value = reinterpret_cast<CreateString>(0x00428c4e)(
                    static_cast<std::uint32_t>(decoded.size() + 1));
                std::memcpy(value, decoded.c_str(), decoded.size() + 1);
                field->value = reinterpret_cast<std::uint32_t>(value);
            } else {
                g_restored_appearance_strings.push_back(std::move(decoded));
                field->value = reinterpret_cast<std::uint32_t>(
                    g_restored_appearance_strings.back().c_str());
            }
        } else if (stored_type == field->type && encoded.size() == 8) {
            std::uint32_t value = 0;
            if (ParseHex(encoded, value)) {
                field->value = value;
            }
        }
    }
}

void SyncAppearance(void* profile) {
    if (profile != g_appearance_profile) {
        g_appearance_profile = profile;
        g_appearance_initialized = false;
        g_last_appearance.clear();
        g_pending_appearance.clear();
    }
    if (!g_appearance_initialized) {
        std::string saved;
        if (thps3_ap::AppearanceStorageLoaded() &&
            thps3_ap::GetAppearance(saved)) {
            ApplyAppearance(profile, saved);
            g_last_appearance = CaptureAppearance(profile);
        } else {
            g_last_appearance = CaptureAppearance(profile);
            thps3_ap::QueueAppearance(g_last_appearance);
        }
        g_appearance_initialized = true;
        return;
    }
    std::string saved;
    if (thps3_ap::GetAppearance(saved) &&
        saved != g_last_appearance && saved != g_pending_appearance) {
        ApplyAppearance(profile, saved);
        g_last_appearance = CaptureAppearance(profile);
        g_pending_appearance.clear();
        return;
    }
    if (!thps3_ap::AppearanceStorageLoaded()) {
        return;
    }
    const std::string current = CaptureAppearance(profile);
    if (current == g_last_appearance) {
        g_pending_appearance.clear();
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (current != g_pending_appearance) {
        g_pending_appearance = current;
        g_appearance_changed_at = now;
        return;
    }
    if (now - g_appearance_changed_at >= std::chrono::milliseconds(750)) {
        g_last_appearance = current;
        g_pending_appearance.clear();
        thps3_ap::QueueAppearance(current);
    }
}

void SyncStatAssignment(void* profile, std::uint32_t received_points) {
    if (!thps3_ap::StatAssignmentStorageLoaded()) {
        return;
    }
    static const std::array<std::uint32_t, kStatNames.size()> stat_checksums = [] {
        std::array<std::uint32_t, kStatNames.size()> checksums{};
        for (std::size_t index = 0; index < checksums.size(); ++index) {
            checksums[index] = QbChecksum(kStatNames[index]);
        }
        return checksums;
    }();

    std::array<std::uint32_t*, 10> fields{};
    for (std::size_t index = 0; index < fields.size(); ++index) {
        fields[index] = FindProfileInteger(profile, stat_checksums[index]);
        if (fields[index] == nullptr) {
            return;
        }
    }

    if (profile != g_stat_assignment_profile) {
        g_stat_assignment_profile = profile;
        g_last_stat_assignment.fill(0xffffffffu);
        std::array<std::uint32_t, 10> saved{};
        if (thps3_ap::GetStatAssignment(saved)) {
            for (std::size_t index = 0; index < saved.size(); ++index) {
                *fields[index] = saved[index];
            }
        }
    }

    std::array<std::uint32_t, 10> assignment{};
    for (std::size_t index = 0; index < assignment.size(); ++index) {
        assignment[index] = *fields[index];
    }
    // Restore stats first, then recover the balance from the full item history.
    assignment[0] = thps3_ap::AvailableStatPoints(
        assignment, received_points, thps3_ap::SelectedSkaterNativeChecksum());
    *fields[0] = assignment[0];
    if (assignment == g_last_stat_assignment) {
        return;
    }
    g_last_stat_assignment = assignment;
    thps3_ap::QueueStatAssignment(assignment);
}

bool IsGamePaused() {
    using AcquireGameFlow = std::uint8_t* (__cdecl *)(int);
    using ReleaseGameFlow = void (__cdecl *)();
    std::uint8_t* const game_flow =
        reinterpret_cast<AcquireGameFlow>(0x004492B0)(0);
    const bool paused = game_flow != nullptr && game_flow[0x49] != 0;
    reinterpret_cast<ReleaseGameFlow>(0x00449300)();
    return paused;
}

std::uint8_t* CurrentSkater() {
    std::uintptr_t address =
        *reinterpret_cast<std::uintptr_t*>(0x005d06c0);
    if (address == 0) {
        return nullptr;
    }
    constexpr std::array<std::uintptr_t, 4> offsets{{
        0x580, 0x4, 0x2c, 0x48,
    }};
    for (const std::uintptr_t offset : offsets) {
        address = *reinterpret_cast<std::uintptr_t*>(address + offset);
        if (address == 0 || address == 0xffffffffu) {
            return nullptr;
        }
    }
    return reinterpret_cast<std::uint8_t*>(address);
}

void ApplyManualTrickPermission() {
    std::uint8_t* const skater = CurrentSkater();
    if (skater == nullptr) {
        return;
    }

    auto* const count = reinterpret_cast<std::uint32_t*>(skater + 0x54bc);
    auto* const tricks = reinterpret_cast<std::uint32_t*>(skater + 0x54c0);
    if (!THPS3AP_IsTrickCategoryUnlocked(THPS3AP_TRICK_MANUAL)) {
        if (*count > 0 && *count <= g_saved_manual_tricks.size()) {
            g_saved_manual_trick_count = *count;
            g_saved_manual_trick_skater =
                reinterpret_cast<std::uintptr_t>(skater);
            for (std::uint32_t i = 0; i < *count; ++i) {
                g_saved_manual_tricks[i] = tricks[i];
            }
        }
        if (*count != 0) {
            *count = 0;
        }
    } else if (g_saved_manual_trick_skater ==
                   reinterpret_cast<std::uintptr_t>(skater) &&
               g_saved_manual_trick_count > 0) {
        if (*count == 0) {
            for (std::uint32_t i = 0; i < g_saved_manual_trick_count; ++i) {
                tricks[i] = g_saved_manual_tricks[i];
            }
            *count = g_saved_manual_trick_count;
        }
        g_saved_manual_trick_count = 0;
        g_saved_manual_trick_skater = 0;
    }
}

void ApplyLipTrickPermission() {
    using ResolveQbKey = void* (__cdecl *)(std::uint32_t);
    const auto resolve_qb_key = reinterpret_cast<ResolveQbKey>(0x00426340);
    auto* const header = static_cast<std::uint8_t*>(
        resolve_qb_key(0x1647cf96u)); // LipTrick
    if (header == nullptr || *reinterpret_cast<std::uint32_t*>(header + 4) != 7) {
        return;
    }

    auto* const script_pointer = reinterpret_cast<std::uint32_t*>(header + 12);
    const auto header_address = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(header));
    const auto disabled_pointer = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(g_disabled_lip_trick_script.data()));
    const std::uint32_t current_pointer = *script_pointer;

    if (!THPS3AP_IsTrickCategoryUnlocked(THPS3AP_TRICK_LIP)) {
        if (current_pointer != disabled_pointer) {
            g_saved_lip_trick_script_pointer = current_pointer;
            g_saved_lip_trick_script_header = header_address;
            patchDWord(script_pointer, disabled_pointer);
        }
    } else if (header_address == g_saved_lip_trick_script_header &&
               current_pointer == disabled_pointer &&
               g_saved_lip_trick_script_pointer != 0) {
        patchDWord(script_pointer, g_saved_lip_trick_script_pointer);
        g_saved_lip_trick_script_pointer = 0;
        g_saved_lip_trick_script_header = 0;
    }
}

void ApplySpecialTrickPermission() {
    std::uint8_t* const skater = CurrentSkater();
    if (skater == nullptr) {
        return;
    }

    constexpr std::array<std::uint32_t, 3> special_offsets{{
        0x54b4, 0x54e8, 0x5530,
    }};
    constexpr std::array<std::uint32_t, 3> count_offsets{{
        0x5488, 0x54bc, 0x5504,
    }};
    constexpr std::array<std::uint32_t, 3> list_offsets{{
        0x548c, 0x54c0, 0x5508,
    }};
    const auto skater_address = reinterpret_cast<std::uintptr_t>(skater);
    if (g_saved_special_queue_skater != skater_address) {
        g_saved_special_queues = {};
        g_saved_special_queue_skater = skater_address;
    }

    const bool unlocked = THPS3AP_IsTrickCategoryUnlocked(
        THPS3AP_TRICK_SPECIAL);
    for (std::size_t queue = 0; queue < special_offsets.size(); ++queue) {
        auto* const special = reinterpret_cast<std::uint32_t*>(
            skater + special_offsets[queue]);
        auto* const count = reinterpret_cast<std::uint32_t*>(
            skater + count_offsets[queue]);
        auto* const regular = reinterpret_cast<std::uint32_t*>(
            skater + list_offsets[queue]);
        SavedSpecialQueue& saved = g_saved_special_queues[queue];

        if (!unlocked) {
            if (*special != 0 && *count <= saved.regular.size()) {
                saved.special = *special;
                saved.regular_count = *count;
                for (std::uint32_t i = 0; i < *count; ++i) {
                    saved.regular[i] = regular[i];
                }
            }
            if (*special != 0) {
                *special = 0;
            }
            continue;
        }

        bool same_regular_queue = *count == saved.regular_count;
        for (std::uint32_t i = 0;
             same_regular_queue && i < saved.regular_count; ++i) {
            same_regular_queue = regular[i] == saved.regular[i];
        }
        if (*special == 0 && saved.special != 0 && same_regular_queue) {
            *special = saved.special;
        }
        saved = {};
    }
}

struct QbStructHeader {
    std::uint32_t type;
    std::uint32_t key;
    union {
        const char* text;
        std::uint32_t raw;
    } value;
    QbStructHeader* next;
};

struct QbStruct {
    QbStructHeader* head;
    QbStructHeader* tail;
};

struct QbArray {
    QbStruct** structures;
    std::uint16_t type;
    std::uint16_t size;
};

static_assert(sizeof(QbStructHeader) == 16);
static_assert(sizeof(QbStruct) == 8);
static_assert(sizeof(QbArray) == 8);

std::uint32_t QbChecksum(std::string_view value);
void ValidateGapHighlight();
void ApplyGapAdvanceRequest();
void ApplyPendingGapHighlight();
void ApplyAllGapHighlightsRequest();

struct GapHighlightEndpoint {
    std::uint32_t gap;
    std::uint32_t node;
    std::uint32_t cluster;
    std::uint32_t node_class;
    std::uint32_t role;
    float x;
    float y;
    float z;
    std::uint32_t level;
};

struct GapRailSegment {
    std::uint32_t gap;
    float start_x;
    float start_y;
    float start_z;
    float end_x;
    float end_y;
    float end_z;
    std::uint32_t level;
};

#include "gap_highlights.inc"
#include "gap_fallback_markers.inc"

std::vector<std::uint32_t> g_gap_tint_sectors;
std::vector<std::uint32_t> g_gap_logged_draw_sectors;
std::vector<GapHighlightEndpoint> g_gap_markers;
std::vector<GapRailSegment> g_gap_rail_lines;
void* g_stage_state_device = nullptr;
std::uint32_t g_stage_state_block = 0;

void GapHighlightLog(const char* format, ...) {
    char directory[MAX_PATH]{};
    if (GetTempPathA(MAX_PATH, directory) == 0) {
        return;
    }
    const std::string path = std::string(directory) +
        "thps3-ap-gap-highlight.log";
    FILE* const file = std::fopen(path.c_str(), "a");
    if (file == nullptr) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fclose(file);
}

std::uint8_t* FindSuperSector(std::uint32_t checksum) {
    auto** const table_pointer = reinterpret_cast<std::uint32_t**>(0x0085a4b8);
    if (*table_pointer == nullptr) {
        return nullptr;
    }
    auto* entry = *table_pointer + (checksum & 0x00000fffu) * 3u;
    while (entry != nullptr) {
        if (entry[0] == checksum) {
            return reinterpret_cast<std::uint8_t*>(entry[1]);
        }
        entry = reinterpret_cast<std::uint32_t*>(entry[2]);
    }
    return nullptr;
}

bool ColorGapSector(std::uint32_t checksum) {
    std::uint8_t* const sector = FindSuperSector(checksum);
    if (sector == nullptr) {
        GapHighlightLog("MEMBER crc=%08x sector=null", checksum);
        return false;
    }
    if (std::any_of(
            g_gap_tint_sectors.begin(), g_gap_tint_sectors.end(),
            [checksum](std::uint32_t saved) { return saved == checksum; })) {
        return true;
    }
    const std::uint16_t count = *reinterpret_cast<std::uint16_t*>(sector + 0x5e);
    g_gap_tint_sectors.push_back(checksum);
    GapHighlightLog(
        "MEMBER crc=%08x sector=%p vertices=%u", checksum, sector, count);
    return true;
}

bool ColorGapCluster(std::uint32_t checksum) {
    if (checksum == 0) {
        return false;
    }
    auto* const game = *reinterpret_cast<std::uint8_t**>(0x008e1e90);
    if (game == nullptr) {
        return false;
    }
    using GetCluster = std::uint8_t* (__thiscall *)(void*, std::uint32_t);
    auto* const cluster = reinterpret_cast<GetCluster>(0x004bf530)(
        game + 0x118, checksum);
    if (cluster == nullptr) {
        GapHighlightLog("CLUSTER crc=%08x ptr=null", checksum);
        return false;
    }

    bool colored = false;
    unsigned member_count = 0;
    auto* const sentinel = cluster + 0x24;
    auto* object = *reinterpret_cast<std::uint8_t**>(sentinel + 0x0c);
    while (object != nullptr && object != sentinel) {
        ++member_count;
        colored |= ColorGapSector(
            *reinterpret_cast<std::uint32_t*>(object + 0x14));
        object = *reinterpret_cast<std::uint8_t**>(object + 0x0c);
    }
    GapHighlightLog(
        "CLUSTER crc=%08x ptr=%p members=%u targets=%u",
        checksum, cluster, member_count,
        static_cast<unsigned>(g_gap_tint_sectors.size()));
    return colored;
}

struct LiveObjectLookup {
    std::uint32_t node_index;
    std::uint8_t* object;
};

void __cdecl FindLiveObject(void* object, void* context) {
    auto& lookup = *static_cast<LiveObjectLookup*>(context);
    auto* const bytes = static_cast<std::uint8_t*>(object);
    if (bytes != nullptr
        && *reinterpret_cast<std::uint32_t*>(bytes + 0x38)
            == lookup.node_index) {
        lookup.object = bytes;
    }
}

bool LiveObjectPosition(
    std::uint32_t checksum, float& x, float& y, float& z) {
    using IterateObjects = void (__thiscall *)(
        void*, void (__cdecl *)(void*, void*), void*);
    auto* const game = *reinterpret_cast<std::uint8_t**>(0x008e1e90);
    if (game == nullptr) {
        return false;
    }
    void* const manager = game + 0x78;
    using GetNodeIndex = std::uint32_t (__cdecl *)(std::uint32_t);
    LiveObjectLookup lookup{
        reinterpret_cast<GetNodeIndex>(0x0042b2b0)(checksum), nullptr};
    reinterpret_cast<IterateObjects>(0x004c2d20)(
        manager, FindLiveObject, &lookup);
    if (lookup.object == nullptr) {
        return false;
    }
    const auto* const position = reinterpret_cast<const float*>(
        lookup.object + 0x18);
    if (!std::isfinite(position[0]) || !std::isfinite(position[1])
        || !std::isfinite(position[2])) {
        return false;
    }
    x = position[0];
    y = position[1];
    z = position[2];
    return true;
}

void RestoreGapSectors() {
    GapHighlightLog(
        "CLEAR gap=%08x targets=%u", g_gap_highlighted_checksum,
        static_cast<unsigned>(g_gap_tint_sectors.size()));
    g_gap_tint_sectors.clear();
    g_gap_logged_draw_sectors.clear();
    g_gap_markers.clear();
    g_gap_rail_lines.clear();
}

constexpr std::uint32_t kPropertiesChecksum = 0x783cce38u;
constexpr std::uint32_t kIdChecksum = 0x40c698afu;
constexpr std::array<std::uint32_t, 9> kChatPropertyChecksums{{
    0x9702d6e6u,  // chat1props
    0x1196a448u,  // chat2props
    0xdaca77edu,  // chat3props
    0xc7cf4755u,  // chat4props
    0x0c9394f0u,  // chat5props
    0x8a07e65eu,  // chat6props
    0x415b35fbu,  // chat7props
    0xb00d872eu,  // chat8props
    0x7b51548bu,  // chat9props
}};
constexpr std::array<std::uint32_t, 9> kChatLineIds{{
    0x641864b7u,  // AP_Chat_Line_1
    0xfd11350du,  // AP_Chat_Line_2
    0x8a16059bu,  // AP_Chat_Line_3
    0x14729038u,  // AP_Chat_Line_4
    0x6375a0aeu,  // AP_Chat_Line_5
    0xfa7cf114u,  // AP_Chat_Line_6
    0x8d7bc182u,  // AP_Chat_Line_7
    0x1dc4dc13u,  // AP_Chat_Line_8
    0x6ac3ec85u,  // AP_Chat_Line_9
}};
constexpr std::array<std::size_t, 5> kChatRows{{0, 1, 2, 3, 4}};
static_assert(kChatRows.size() == 5 && kChatRows.back() == 4);
bool g_ap_chat_palette_active = false;
std::uint32_t g_ap_chat_active_id = 0;
std::uint32_t g_ap_chat_active_properties = 0;
constexpr std::array<std::array<std::uint8_t, 5>, 8> kApChatPalette{{
    {{109, 139, 232, 128, 15}},  // useful: slateblue
    {{176, 96, 255, 128, 15}},   // progression: purple
    {{255, 255, 0, 128, 15}},    // player: ANSI yellow
    {{255, 255, 255, 128, 15}},  // base/reset: white
    {{0, 238, 238, 128, 15}},    // normal item: cyan
    {{250, 128, 114, 128, 15}},  // trap: salmon
    {{0, 255, 127, 128, 15}},    // location: green
    {{238, 0, 238, 128, 15}},    // own player: magenta
}};

void LogLiveChatPalette(std::uint32_t source_count) {
    static bool logged = false;
    if (logged) {
        return;
    }
    std::printf(
        "[debug] live chat layout palette: source count=%u -> AP count=%u\n",
        static_cast<unsigned>(source_count),
        static_cast<unsigned>(kApChatPalette.size()));
    for (std::size_t index = 0; index < kApChatPalette.size(); ++index) {
        std::printf(
            "[debug] live chat layout C%u: %u,%u,%u\n",
            static_cast<unsigned>(index), kApChatPalette[index][0],
            kApChatPalette[index][1], kApChatPalette[index][2]);
    }
    logged = true;
}

void* FindPanelElement(std::uint32_t id_checksum) {
    using GetMenuManager = void* (__cdecl *)(int);
    using FreeMenuManager = void (__cdecl *)();
    using GetMenuElement = void* (__thiscall *)(void*, std::uint32_t, int);
    void* const manager = reinterpret_cast<GetMenuManager>(0x004D12A0)(0);
    void* const element = manager != nullptr
        ? reinterpret_cast<GetMenuElement>(0x004D23B0)(
            manager, id_checksum, 1)
        : nullptr;
    if (manager != nullptr) {
        reinterpret_cast<FreeMenuManager>(0x004D12F0)();
    }
    return element;
}

bool DestroyPanelElement(std::uint32_t id_checksum) {
    using ResolveQbKey = void* (__cdecl *)(std::uint32_t);
    using DestroyElement = bool (__cdecl *)(void*, void*);
    auto* const qb_header = static_cast<std::uint8_t*>(
        reinterpret_cast<ResolveQbKey>(0x00426340)(0xab938d1eu));
    if (qb_header == nullptr ||
        *reinterpret_cast<std::uint32_t*>(qb_header + 4) != 8) {
        return false;
    }
    const auto destroy_element = reinterpret_cast<DestroyElement>(
        *reinterpret_cast<std::uint32_t*>(qb_header + 12));
    if (destroy_element == nullptr) {
        return false;
    }
    QbStructHeader id{};
    id.type = 13;
    id.key = kIdChecksum;
    id.value.raw = id_checksum;
    QbStruct params{&id, &id};
    alignas(8) std::byte script[0x398]{};
    return destroy_element(&params, script);
}

bool LaunchPanelText(
    const char* message,
    std::uint32_t id_checksum,
    std::uint32_t properties_checksum,
    bool use_ap_palette = true) {
    using ResolveQbKey = void* (__cdecl *)(std::uint32_t);
    using LaunchPanelMessage = bool (__cdecl *)(void*, void*);
    const auto resolve_qb_key =
        reinterpret_cast<ResolveQbKey>(0x00426340);
    auto* const qb_header = static_cast<std::uint8_t*>(
        resolve_qb_key(0xf9f4ef2cu));  // LaunchPanelMessage
    if (qb_header == nullptr ||
        *reinterpret_cast<std::uint32_t*>(qb_header + 4) != 8) {
        return false;
    }
    const auto launch_panel_message = reinterpret_cast<LaunchPanelMessage>(
        *reinterpret_cast<std::uint32_t*>(qb_header + 12));
    if (launch_panel_message == nullptr) {
        return false;
    }
    QbStructHeader id{};
    QbStructHeader properties{};
    QbStructHeader text{};
    text.type = 3;
    text.value.text = message;
    text.next = &id;
    id.type = 13;
    id.key = kIdChecksum;
    id.value.raw = id_checksum;
    id.next = &properties;
    properties.type = 13;
    properties.key = kPropertiesChecksum;
    properties.value.raw = properties_checksum;
    QbStruct params{&text, &properties};
    alignas(8) std::byte script[0x398]{};
    void* const element_before = FindPanelElement(id_checksum);
    const bool destroyed = DestroyPanelElement(id_checksum);
    g_ap_chat_active_id = id_checksum;
    g_ap_chat_active_properties = properties_checksum;
    g_ap_chat_palette_active = use_ap_palette;
    const bool launched = launch_panel_message(&params, script);
    g_ap_chat_palette_active = false;
    g_ap_chat_active_id = 0;
    g_ap_chat_active_properties = 0;
    void* const element_after = FindPanelElement(id_checksum);
    std::printf(
        "[debug] panel element id=%08x props=%08x before=%p destroyed=%u "
        "launched=%u after=%p\n",
        id_checksum, properties_checksum, element_before,
        destroyed ? 1u : 0u, launched ? 1u : 0u, element_after);
    return launched;
}

constexpr std::chrono::milliseconds DisplayMessageDelay(bool more) {
    return std::chrono::milliseconds(more ? 100 : 1000);
}

static_assert(DisplayMessageDelay(true).count() == 100);
static_assert(DisplayMessageDelay(false).count() == 1000);

constexpr bool StartsMultilineMessage(bool more, bool continuing) {
    return more && !continuing;
}

static_assert(StartsMultilineMessage(true, false));
static_assert(!StartsMultilineMessage(true, true));
static_assert(!StartsMultilineMessage(false, false));

void DisplayNextMessage() {
    using Clock = std::chrono::steady_clock;
    static auto next_message_time = Clock::time_point{};
    static auto last_message_time = Clock::time_point{};
    static std::size_t next_chat_row = 0;
    static bool continuing = false;

    const auto now = Clock::now();
    if (now < next_message_time) {
        return;
    }
    if (last_message_time != Clock::time_point{} &&
        now - last_message_time >= std::chrono::milliseconds(7500)) {
        next_chat_row = 0;
    }

    std::string message;
    bool more = false;
    if (!thps3_ap::TakeDisplayMessage(message, more)) {
        return;
    }
    if (StartsMultilineMessage(more, continuing)) {
        next_chat_row = 0;
    }

    const std::size_t row = kChatRows[next_chat_row];
    const bool displayed = LaunchPanelText(
        message.c_str(),
        kChatLineIds[row],
        kChatPropertyChecksums[row]);
    std::printf(
        "[debug] display HUD row=%u props=%08x continuing=%u more=%u %s: %s\n",
        static_cast<unsigned>(next_chat_row + 1),
        kChatPropertyChecksums[row],
        continuing ? 1u : 0u,
        more ? 1u : 0u,
        displayed ? "accepted" : "rejected",
        message.c_str());
    next_chat_row = (next_chat_row + 1) % kChatRows.size();
    continuing = more;
    last_message_time = now;
    next_message_time = now + DisplayMessageDelay(more);
}

void UpdateCassetteProgressText() {
    const std::uint32_t level =
        g_highlighted_cassette_level.load(std::memory_order_acquire);
    if (level < 1 || level > 9) {
        return;
    }

    const std::uint32_t checked_objectives =
        thps3_ap::CheckedObjectiveMask(static_cast<int>(level));
    const std::uint32_t unlocked_objectives =
        thps3_ap::AvailableObjectiveLocationMask(static_cast<int>(level)) |
        checked_objectives;

    char objective_text[40];
    char gap_text[40];
    char item_text[24];
    std::snprintf(
        objective_text,
        sizeof(objective_text),
        "Goals: %d/%d (%u)",
        std::popcount(checked_objectives),
        std::popcount(unlocked_objectives),
        thps3_ap::ActiveObjectiveCount(static_cast<int>(level)));
    std::snprintf(
        gap_text,
        sizeof(gap_text),
        "Gaps: %u/%u (%u)",
        thps3_ap::CheckedGapCount(static_cast<int>(level)),
        thps3_ap::InLogicGapCount(static_cast<int>(level)),
        thps3_ap::ActiveGapCount(static_cast<int>(level)));
    std::snprintf(
        item_text,
        sizeof(item_text),
        "Items: %u/%u",
        thps3_ap::CheckedItemCount(static_cast<int>(level)),
        thps3_ap::ActiveItemCount(static_cast<int>(level)));

    using SetElementText = void (__cdecl *)(std::uint32_t, const char*);
    const auto set_element_text =
        reinterpret_cast<SetElementText>(0x004CE940);
    set_element_text(QbChecksum("cassette_menu_line_1"), objective_text);
    set_element_text(QbChecksum("cassette_menu_line_2"), gap_text);
    set_element_text(QbChecksum("AP_CassetteGoals"), objective_text);
    set_element_text(QbChecksum("AP_CassetteGaps"), gap_text);
    set_element_text(QbChecksum("AP_CassetteItems"), item_text);

    using SetElementVisibility = void (__cdecl *)(const char*);
    const auto set_element_visibility = reinterpret_cast<SetElementVisibility>(
        level == 3 || level == 6 || level == 8 ? 0x00443D70 : 0x00443E60);
    set_element_visibility("AP_CassetteGoals");
    set_element_visibility("AP_CassetteGaps");
    reinterpret_cast<SetElementVisibility>(0x00443D70)("AP_CassetteItems");
}

std::uint32_t QbChecksum(std::string_view value) {
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char byte : value) {
        if (byte == '/') {
            byte = '\\';
        } else if (byte >= 'A' && byte <= 'Z') {
            byte = static_cast<unsigned char>(byte + ('a' - 'A'));
        }
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

void ApplyGapMenuData() {
    if (!g_gap_menu_pending.load(std::memory_order_acquire)) {
        return;
    }
    using GetMenuManager = void* (__cdecl *)(int);
    using FreeMenuManager = void (__cdecl *)();
    using GetMenuElement = void* (__thiscall *)(void*, std::uint32_t, int);
    void* const manager = reinterpret_cast<GetMenuManager>(0x004D12A0)(0);
    void* const title_element = manager != nullptr
        ? reinterpret_cast<GetMenuElement>(0x004D23B0)(
            manager, QbChecksum("APGapMenuTitle"), 1)
        : nullptr;
    if (manager != nullptr) {
        reinterpret_cast<FreeMenuManager>(0x004D12F0)();
    }
    if (title_element == nullptr ||
        !g_gap_menu_pending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    std::string title;
    std::array<std::string, kGapMenuRowCount> rows;
    std::array<std::uint32_t, kGapMenuRowCount> checksums;
    std::size_t row_count = 0;
    std::size_t preferred_focus_row = kGapMenuRowCount;
    {
        const std::lock_guard lock(g_gap_menu_mutex);
        title = g_gap_menu_title;
        rows = g_gap_menu_rows;
        checksums = g_gap_menu_checksums;
        row_count = g_gap_menu_row_count;
        preferred_focus_row = g_gap_menu_preferred_focus_row;
        g_gap_menu_preferred_focus_row = kGapMenuRowCount;
    }

    using SetElementText = void (__cdecl *)(const char*, const char*);
    const auto set_element_text = reinterpret_cast<SetElementText>(0x004CE920);
    set_element_text("APGapMenuTitle", title.c_str());

    using SetMenuElementStatic = void (__thiscall *)(void*, bool);
    using AddMenuChild = void (__thiscall *)(void*, void*);
    using RemoveMenuChild = void (__thiscall *)(void*, void*);
    using ResetScrollingMenu = void (__thiscall *)(void*);
    using FocusMenuElement = bool (__thiscall *)(void*, std::uint32_t);
    const auto set_menu_element_static =
        reinterpret_cast<SetMenuElementStatic>(0x004CDA00);
    const auto add_menu_child = reinterpret_cast<AddMenuChild>(0x004CD300);
    const auto remove_menu_child =
        reinterpret_cast<RemoveMenuChild>(0x004CD330);
    static void* cached_menu = nullptr;
    static void* cached_title = nullptr;
    static std::array<void*, kGapMenuRowCount> cached_rows{};
    void* const row_manager = reinterpret_cast<GetMenuManager>(0x004D12A0)(0);
    if (row_manager != nullptr) {
        void* const menu = reinterpret_cast<GetMenuElement>(0x004D23B0)(
            row_manager, QbChecksum("APGapMenu"), 1);
        void* const back = reinterpret_cast<GetMenuElement>(0x004D23B0)(
            row_manager, QbChecksum("APGapBack"), 1);
        if (menu != cached_menu || title_element != cached_title) {
            std::array<void*, kGapMenuRowCount> rows{};
            for (std::size_t index = 0; index < rows.size(); ++index) {
                char name[20];
                std::snprintf(
                    name, sizeof(name), "APGapRow%03u",
                    static_cast<unsigned>(index + 1));
                rows[index] = reinterpret_cast<GetMenuElement>(0x004D23B0)(
                    row_manager, QbChecksum(name), 1);
            }
            if (std::all_of(rows.begin(), rows.end(),
                    [](void* row) { return row != nullptr; })) {
                cached_menu = menu;
                cached_title = title_element;
                cached_rows = rows;
            }
        }
        const auto& row_elements = cached_rows;
        for (std::size_t index = 0; index < row_elements.size(); ++index) {
            if (row_elements[index] != nullptr) {
                set_menu_element_static(row_elements[index], index >= row_count);
            }
        }
        if (menu != nullptr && back != nullptr &&
            menu == cached_menu && title_element == cached_title &&
            std::all_of(row_elements.begin(), row_elements.end(),
                [](void* row) { return row != nullptr; })) {
            const std::uint32_t previous_focus =
                *reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(menu) + 0x1a4);
            const auto parent = [](void* element) {
                return *reinterpret_cast<void**>(
                    static_cast<std::uint8_t*>(element) + 4);
            };
            if (parent(back) == menu) {
                remove_menu_child(menu, back);
            }
            for (std::size_t index = 0; index < row_elements.size(); ++index) {
                const bool attached = parent(row_elements[index]) == menu;
                if (index < row_count && !attached) {
                    add_menu_child(menu, row_elements[index]);
                } else if (index >= row_count && attached) {
                    remove_menu_child(menu, row_elements[index]);
                }
            }
            add_menu_child(menu, back);
            *reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(menu) + 0x1c0) =
                static_cast<std::uint32_t>((std::min)(
                    std::size_t{12}, row_count + 2));
            reinterpret_cast<ResetScrollingMenu>(0x004D8370)(menu);
            const auto focus_menu_element =
                reinterpret_cast<FocusMenuElement>(0x004D8330);
            std::uint32_t preferred_focus = previous_focus;
            const auto active = std::find(
                checksums.begin(), checksums.begin() + row_count,
                g_gap_highlighted_checksum);
            if (g_gap_highlighted_checksum != 0 &&
                active != checksums.begin() + row_count) {
                char name[20];
                std::snprintf(
                    name, sizeof(name), "APGapRow%03u",
                    static_cast<unsigned>(active - checksums.begin() + 1));
                preferred_focus = QbChecksum(name);
            } else if (preferred_focus_row < row_count) {
                char name[20];
                std::snprintf(
                    name, sizeof(name), "APGapRow%03u",
                    static_cast<unsigned>(preferred_focus_row + 1));
                preferred_focus = QbChecksum(name);
            }
            if (!focus_menu_element(menu, preferred_focus)) {
                focus_menu_element(
                    menu,
                    QbChecksum(row_count == 0
                        ? "APGapBack"
                        : "APGapRow001"));
            }
        }
        reinterpret_cast<FreeMenuManager>(0x004D12F0)();
    }
    // Detached rows ignore text changes. Reattach the current rows first,
    // then populate them so a larger menu renders fully on its first refresh.
    for (std::size_t index = 0; index < row_count; ++index) {
        char name[20];
        std::snprintf(name, sizeof(name), "APGapRow%03u", static_cast<unsigned>(index + 1));
        const std::string text = g_gap_highlighted_checksum != 0 &&
                checksums[index] == g_gap_highlighted_checksum
            ? "ACTIVE: " + rows[index]
            : rows[index];
        set_element_text(name, text.c_str());
    }
    for (std::size_t index = row_count; index < kGapMenuRowCount; ++index) {
        char name[20];
        std::snprintf(
            name, sizeof(name), "APGapRow%03u",
            static_cast<unsigned>(index + 1));
        set_element_text(name, "");
    }
}

void* g_last_objective_profile = nullptr;
std::uint32_t g_last_objective_state_revision = 0;

struct LevelMenuEntry {
    std::uint32_t cassette_id_checksum;
    int level_number;
    std::uint32_t career_flag;
};

constexpr std::array<LevelMenuEntry, 9> kLevelMenuEntries{{
    {0x726335c5, 1, 10},  // cassette_foundry
    {0x8355b910, 2, 11},  // cassette_canada
    {0x7288edfb, 3, 12},  // cassette_rio
    {0x0c49d5d9, 4, 13},  // cassette_suburbia
    {0x119f87b6, 5, 14},  // cassette_airport
    {0x71540250, 6, 15},  // cassette_skater_island
    {0x2e752d63, 7, 16},  // cassette_los_angeles
    {0xa72168ed, 8, 17},  // cassette_tokyo
    {0x953c7891, 9, 18},  // cassette_ship
}};

bool IsValidGoalId(int level_number, int goal_id) {
    const std::uint32_t owned_bits =
        thps3_ap::OwnedObjectiveCareerBits(level_number);
    return goal_id >= 0 && goal_id < 32 &&
        (owned_bits & (1u << goal_id)) != 0;
}

void RefreshUnlockedScoreGoals(void* profile_manager, int level_number) {
    if (profile_manager == nullptr || level_number < 1 || level_number > 9) {
        return;
    }
    constexpr std::array<const char*, 3> scripts{{
        "Got_HighScore", "Got_ProScore", "Got_SickScore",
    }};
    const auto& scores = kCareerScoreGoals[
        static_cast<std::size_t>(level_number - 1)];
    const std::uint32_t completed =
        thps3_ap::CheckedObjectiveMask(level_number) |
        g_pending_goal_masks[static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
    auto* const slots = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uint8_t*>(profile_manager) + 0x15c);
    for (int goal_id = 0; goal_id < 3; ++goal_id) {
        auto* const slot = slots + goal_id * 3;
        const std::uint32_t bit = 1u << goal_id;
        if (scores[goal_id] != 0 && slot[0] == 0 &&
            (completed & bit) == 0 &&
            thps3_ap::IsObjectiveUnlocked(level_number, goal_id)) {
            slot[0] = scores[goal_id];
            slot[1] = QbChecksum(scripts[goal_id]);
            slot[2] = static_cast<std::uint32_t>(goal_id);
        }
    }
}

std::uint32_t ReconcileObjectiveCareerBits(void* profile, bool detect_new_goals) {
    auto* const profile_bytes = static_cast<std::uint8_t*>(profile);
    std::uint32_t write_count = 0;
    for (int level_number = 1; level_number <= 9; ++level_number) {
        auto* const goals = reinterpret_cast<std::uint32_t*>(
            profile_bytes + 0x564 + (level_number - 1) * 8);
        const std::uint32_t before = *goals;
        const std::uint32_t owned_bits =
            thps3_ap::OwnedObjectiveCareerBits(level_number);
        const std::uint32_t checked =
            thps3_ap::CheckedObjectiveMask(level_number) & owned_bits;
        auto& pending_goal_mask = g_pending_goal_masks[
            static_cast<std::size_t>(level_number - 1)];
        std::uint32_t pending =
            pending_goal_mask.load(std::memory_order_acquire) & ~checked;
        pending_goal_mask.store(pending, std::memory_order_release);

        if (detect_new_goals) {
            const std::uint32_t newly_completed =
                before & owned_bits & ~(checked | pending);
            if (owned_bits == 0x7u) {
                for (int medal_id = 2; medal_id >= 0; --medal_id) {
                    if ((newly_completed & (1u << medal_id)) != 0) {
                        THPS3AP_RecordMedalResult(level_number, medal_id);
                        break;
                    }
                }
            }
            for (int goal_id = 0; goal_id < 9; ++goal_id) {
                if ((newly_completed & (1u << goal_id)) != 0) {
                    THPS3AP_RecordGoalCompletion(level_number, goal_id);
                }
            }
            pending = pending_goal_mask.load(std::memory_order_acquire) &
                ~checked;
        }

        const std::uint32_t desired = checked | pending;
        const std::uint32_t after = thps3_ap::ReplaceObjectiveCareerBits(
            before, desired, level_number);
        if (after != before) {
            *goals = after;
            ++write_count;
        }
    }
    return write_count;
}

std::uint32_t ReconcileStatCareerBits(void* profile) {
    auto* const profile_bytes = static_cast<std::uint8_t*>(profile);
    std::uint32_t write_count = 0;
    for (int level_number = 1; level_number <= 9; ++level_number) {
        auto* const pickups = reinterpret_cast<std::uint32_t*>(
            profile_bytes + 0x5e8 + (level_number - 1) * 8);
        const std::uint32_t before = *pickups;
        const std::uint32_t checked_mask =
            thps3_ap::CheckedStatPointMask(level_number);
        std::uint32_t after = thps3_ap::StatPointsAreLocations()
            ? thps3_ap::ReplaceStatPointCareerBits(before, checked_mask)
            : before;
        if (thps3_ap::HiddenDecksAreLocations()) {
            const bool deck_checked = thps3_ap::IsDeckChecked(level_number);
            auto& pending_deck = g_pending_decks[
                static_cast<std::size_t>(level_number - 1)];
            if (deck_checked) {
                pending_deck.store(false, std::memory_order_release);
            }
            const bool deck_pending =
                pending_deck.load(std::memory_order_acquire);
            after = thps3_ap::ReplaceDeckCareerBit(
                after, deck_checked || deck_pending);
        }
        if (after != before) {
            *pickups = after;
            ++write_count;
        }
    }
    return write_count;
}

std::uint32_t ReconcileDeckInventoryBits(void* profile) {
    if (!thps3_ap::HiddenDecksAreLocations()) {
        return 0;
    }
    using GetSkaterChecksum = std::uint32_t (__thiscall *)(void*);
    using GetDeckInventory = std::uint32_t (__cdecl *)(std::uint32_t);
    using SetDeckInventory = void (__cdecl *)(std::uint32_t, std::uint32_t);
    const auto get_skater_checksum =
        reinterpret_cast<GetSkaterChecksum>(0x004BD900);
    const auto get_deck_inventory =
        reinterpret_cast<GetDeckInventory>(0x00446130);
    const auto set_deck_inventory =
        reinterpret_cast<SetDeckInventory>(0x00446160);

    const std::uint32_t skater_checksum = get_skater_checksum(profile);
    const std::uint32_t before = get_deck_inventory(skater_checksum);
    std::uint32_t desired = thps3_ap::CheckedDeckMask();
    for (std::size_t index = 0; index < g_pending_decks.size(); ++index) {
        if (g_pending_decks[index].load(std::memory_order_acquire)) {
            desired |= 1u << (index + 1);
        }
    }
    const std::uint32_t after = thps3_ap::ReplaceDeckInventoryBits(
        before, desired & kOwnedDeckInventoryBits);

    if (after == before) {
        return 0;
    }

    // This updates the same process-local table read by CAssetteMenuElement's
    // "Deck: Yes/No" display. It does not invoke a deck reward, QScript, or
    // save path, and it preserves every non-career bit in the table entry.
    set_deck_inventory(skater_checksum, after);
    return 1;
}

bool ReadCurrentSkaterChecksum(std::uint32_t& checksum) {
    using AcquireProfileManager = void* (__cdecl *)(int);
    using ReleaseProfileManager = void (__cdecl *)();
    using GetCurrentProfile = void* (__thiscall *)(void*, void*);
    using GetSkaterChecksum = std::uint32_t (__thiscall *)(void*);
    void* const manager = reinterpret_cast<AcquireProfileManager>(0x004367E0)(0);
    void* const profile = manager != nullptr
        ? reinterpret_cast<GetCurrentProfile>(0x00438B10)(manager, nullptr)
        : nullptr;
    if (profile != nullptr) {
        checksum = reinterpret_cast<GetSkaterChecksum>(0x004BD900)(profile);
    }
    reinterpret_cast<ReleaseProfileManager>(0x00436830)();
    return profile != nullptr;
}

}  // namespace

extern "C" void __fastcall THPS3AP_TextLayoutHook(
    void* text_layout,
    void*,
    std::uintptr_t arg1,
    std::uintptr_t arg2,
    std::uintptr_t arg3,
    std::uintptr_t arg4,
    std::uintptr_t arg5,
    std::uintptr_t arg6,
    std::uintptr_t arg7,
    std::uintptr_t arg8) {
    if (g_ap_chat_palette_active) {
        LogLiveChatPalette(arg6);
        std::printf(
            "[debug] text constructor id=%08x props=%08x layout=%p "
            "source_colors=%p source_count=%u\n",
            g_ap_chat_active_id, g_ap_chat_active_properties, text_layout,
            reinterpret_cast<void*>(arg5), static_cast<unsigned>(arg6));
        arg5 = reinterpret_cast<std::uintptr_t>(kApChatPalette.data());
        arg6 = kApChatPalette.size();
    }
    using Layout = void (__thiscall *)(
        void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
        std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
        std::uintptr_t);
    reinterpret_cast<Layout>(0x004C8270)(
        text_layout, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}

extern "C" void THPS3AP_DebugLog(const char* format, ...) {
    std::call_once(g_debug_log_init, [] {
        if (GetTempPathA(MAX_PATH, g_debug_log_path) == 0 ||
            strcat_s(
                g_debug_log_path,
                sizeof(g_debug_log_path),
                "thps3-ap-qb-trace.log") != 0) {
            g_debug_log_path[0] = '\0';
            return;
        }
        if (std::FILE* const file = std::fopen(g_debug_log_path, "w")) {
            std::fprintf(
                file,
                "%lu pid=%lu trace-start\n",
                static_cast<unsigned long>(GetTickCount()),
                static_cast<unsigned long>(GetCurrentProcessId()));
            std::fflush(file);
            std::fclose(file);
        }
    });
    if (g_debug_log_path[0] == '\0') {
        return;
    }

    std::lock_guard lock(g_debug_log_mutex);
    std::FILE* const file = std::fopen(g_debug_log_path, "a");
    if (file == nullptr) {
        return;
    }
    std::fprintf(
        file,
        "%lu tid=%lu ",
        static_cast<unsigned long>(GetTickCount()),
        static_cast<unsigned long>(GetCurrentThreadId()));
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

extern "C" void THPS3AP_StartBridge(HINSTANCE module_handle) {
    if (g_bridge_started.exchange(true)) {
        return;
    }

    HANDLE thread = CreateThread(
        nullptr,
        0,
        thps3_ap::RunBridgeRuntime,
        module_handle,
        0,
        nullptr
    );
    if (thread == nullptr) {
        g_bridge_started.store(false);
        return;
    }
    CloseHandle(thread);
}

extern "C" BOOL THPS3AP_HasStateSnapshot() {
    return thps3_ap::HasStateSnapshot() ? TRUE : FALSE;
}

extern "C" BOOL THPS3AP_StatPointsAreLocations() {
    return thps3_ap::StatPointsAreLocations() ? TRUE : FALSE;
}

extern "C" BOOL THPS3AP_HiddenDecksAreLocations() {
    return thps3_ap::HiddenDecksAreLocations() ? TRUE : FALSE;
}

extern "C" void THPS3AP_BeginLevelTransition() {
    THPS3AP_DebugLog(
        "transition-begin current=%u",
        g_current_career_level.load(std::memory_order_acquire));
    g_level_transition.store(true, std::memory_order_release);
}

extern "C" void THPS3AP_EndLevelTransition(int level_number) {
    THPS3AP_DebugLog(
        "transition-end old=%u new=%d",
        g_current_career_level.load(std::memory_order_acquire), level_number);
    g_current_career_level.store(
        static_cast<std::uint32_t>(level_number), std::memory_order_release);
    g_level_transition.store(false, std::memory_order_release);
}

extern "C" BOOL THPS3AP_IsLevelUnlocked(int level_number) {
    return thps3_ap::IsLevelUnlocked(level_number) ? TRUE : FALSE;
}

extern "C" BOOL THPS3AP_IsObjectiveUnlocked(
    int level_number,
    int goal_id) {
    return thps3_ap::IsObjectiveUnlocked(level_number, goal_id) ? TRUE : FALSE;
}

extern "C" BOOL THPS3AP_IsObjectiveChecked(
    int level_number,
    int goal_id) {
    return IsValidGoalId(level_number, goal_id) &&
        (thps3_ap::CheckedObjectiveMask(level_number) & (1u << goal_id)) != 0
        ? TRUE
        : FALSE;
}

extern "C" int THPS3AP_ObjectiveListPosition(
    int level_number,
    int goal_id) {
    return thps3_ap::ObjectiveListPosition(level_number, goal_id);
}

extern "C" BOOL THPS3AP_IsTrickCategoryUnlocked(int category) {
    return thps3_ap::IsTrickCategoryUnlocked(category) ? TRUE : FALSE;
}

extern "C" std::uint32_t THPS3AP_ReconcileStatCareerBits() {
    using AcquireProfileManager = void* (__cdecl *)(int);
    using ReleaseProfileManager = void (__cdecl *)();
    const auto acquire_profile_manager =
        reinterpret_cast<AcquireProfileManager>(0x004367E0);
    const auto release_profile_manager =
        reinterpret_cast<ReleaseProfileManager>(0x00436830);

    void* const manager = acquire_profile_manager(0);
    void* selector = nullptr;
    void* profile = nullptr;
    if (manager != nullptr) {
        selector = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(manager) + 0x134);
    }
    if (selector != nullptr) {
        const auto count = *static_cast<std::uint32_t*>(selector);
        const auto current = *reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(selector) + 0x1c);
        if (current < count) {
            profile = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(selector) +
                0x14 + current * sizeof(void*));
        }
    }
    const std::uint32_t writes =
        profile != nullptr ? ReconcileStatCareerBits(profile) : 0;
    release_profile_manager();
    return writes;
}

extern "C" std::uint32_t THPS3AP_ReconcileCareerBits() {
    using AcquireProfileManager = void* (__cdecl *)(int);
    using ReleaseProfileManager = void (__cdecl *)();
    const auto acquire_profile_manager =
        reinterpret_cast<AcquireProfileManager>(0x004367E0);
    const auto release_profile_manager =
        reinterpret_cast<ReleaseProfileManager>(0x00436830);

    void* const manager = acquire_profile_manager(0);
    void* selector = nullptr;
    void* profile = nullptr;
    if (manager != nullptr) {
        selector = *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(manager) + 0x134);
    }
    if (selector != nullptr) {
        const auto count = *static_cast<std::uint32_t*>(selector);
        const auto current = *reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(selector) + 0x1c);
        if (current < count) {
            profile = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(selector) +
                0x14 + current * sizeof(void*));
        }
    }
    std::uint32_t writes = 0;
    if (profile != nullptr) {
        writes += ReconcileObjectiveCareerBits(profile, false);
        writes += ReconcileStatCareerBits(profile);
        writes += ReconcileDeckInventoryBits(profile);
    }
    release_profile_manager();
    return writes;
}

extern "C" void THPS3AP_RecordGoalCompletion(int level_number, int goal_id) {
    if (!IsValidGoalId(level_number, goal_id) ||
        !thps3_ap::IsObjectiveUnlocked(level_number, goal_id)) {
        return;
    }
    const std::uint32_t bit = 1u << goal_id;
    if ((thps3_ap::CheckedObjectiveMask(level_number) & bit) != 0) {
        return;
    }
    auto& pending = g_pending_goal_masks[
        static_cast<std::size_t>(level_number - 1)];
    const std::uint32_t previous = pending.fetch_or(
        bit, std::memory_order_acq_rel);
    if ((previous & bit) != 0) {
        return;
    }

    THPS3AP_QueueGoalEvent(level_number, goal_id);
}

extern "C" void THPS3AP_RecordDeckCollection(int level_number) {
    if (!thps3_ap::HiddenDecksAreLocations() ||
        level_number < 1 || level_number > 9 ||
        thps3_ap::IsDeckChecked(level_number)) {
        return;
    }
    auto& pending = g_pending_decks[
        static_cast<std::size_t>(level_number - 1)];
    if (pending.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    THPS3AP_QueueDeckEvent(level_number);
}

extern "C" void THPS3AP_RequestLevelMenuRefresh() {
    g_level_menu_refresh_requested.store(true, std::memory_order_release);
}

extern "C" void THPS3AP_ApplyCareerTimerBonus() {
    struct QbHeader {
        std::uint32_t key_part;
        std::uint32_t type;
        std::uint32_t id;
        void* value;
        QbHeader* next;
    };
    struct StructHeader {
        std::uint32_t type;
        std::uint32_t key;
        std::uint32_t value;
        StructHeader* next;
    };
    using ResolveQbKey = QbHeader* (__cdecl *)(std::uint32_t);
    QbHeader* const mode = reinterpret_cast<ResolveQbKey>(0x00426340)(
        0x6054e0dfu);  // mode_career
    if (mode == nullptr || mode->type != 10 || mode->value == nullptr) {
        return;
    }
    auto* field = *static_cast<StructHeader**>(mode->value);
    while (field != nullptr && field->key != 0x8d38a280u) {  // default_time_limit
        field = field->next;
    }
    if (field != nullptr && field->type == 1) {
        const std::uint32_t base_seconds = CareerBaseSeconds(
            g_current_career_level.load(std::memory_order_acquire));
        const std::uint32_t time_limit = base_seconds + std::min(
            thps3_ap::TimeBonusSeconds(),
            std::numeric_limits<std::uint32_t>::max() - base_seconds);
        field->value = time_limit;

        using IsCareerMode = int (__cdecl *)();
        if (reinterpret_cast<IsCareerMode>(0x00421540)() != 0) {
            using AcquireProfileManager = void* (__cdecl *)(int);
            using ReleaseProfileManager = void (__cdecl *)();
            using SetTimeLimit = void (__thiscall *)(void*, std::uint32_t);
            void* const manager =
                reinterpret_cast<AcquireProfileManager>(0x004367E0)(0);
            if (manager != nullptr) {
                reinterpret_cast<SetTimeLimit>(0x00437E40)(
                    manager, time_limit);
            }
            reinterpret_cast<ReleaseProfileManager>(0x00436830)();
        }
    }
}

extern "C" void THPS3AP_RecordHighlightedCassetteLevel(int level_number) {
    if (level_number >= 1 && level_number <= 9) {
        g_highlighted_cassette_level.store(
            static_cast<std::uint32_t>(level_number),
            std::memory_order_release);
    }
}

extern "C" void __fastcall THPS3AP_RefreshCassetteMenu(
    void* cassette_menu,
    void*) {
    using RefreshCassetteMenu = void (__thiscall *)(void*);
    reinterpret_cast<RefreshCassetteMenu>(0x00443C30)(cassette_menu);
    UpdateCassetteProgressText();
}

extern "C" void THPS3AP_PumpMainThread() {
    static const bool startup_logged = [] {
        thps3_ap::StartupBreadcrumb("first_main_thread_pump");
        return true;
    }();
    (void)startup_logged;
    static ULONGLONG connection_wait_started = 0;
    static bool connection_timeout_shown = false;
    static bool seeded_skater_selection_attempted = false;
    static bool default_music_shuffle_applied = false;
    const bool connected = THPS3AP_HasStateSnapshot() != FALSE;
    if (!connected) {
        seeded_skater_selection_attempted = false;
        if (THPS3AP_MainMenuScriptLoadCount() != 0) {
            const ULONGLONG now = GetTickCount64();
            if (connection_wait_started == 0) {
                connection_wait_started = now;
            }
            if (!connection_timeout_shown && thps3_ap::StartupConnectionTimedOut(
                    true, now - connection_wait_started)) {
                connection_timeout_shown = true;
                std::string error = thps3_ap::ConnectionError();
                if (error.empty()) {
                    error = "THPS3 could not connect to Archipelago.\n\n"
                            "Check Server, Slot, and Password in partymod.ini, "
                            "then start THPS3 again.";
                }
                const HWND game_window = GetActiveWindow();
                MessageBoxA(
                    game_window,
                    error.c_str(),
                    "Archipelago connection failed",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
                PostMessageA(game_window, WM_CLOSE, 0, 0);
            }
        }
        return;
    }
    if (THPS3AP_MainMenuScriptLoadCount() == 0) {
        return;
    }
    connection_wait_started = 0;
    connection_timeout_shown = false;

    if (!default_music_shuffle_applied &&
        THPS3AP_MainMenuScriptLoadCount() != 0) {
        reinterpret_cast<void (__cdecl *)(int)>(0x004c6ad0)(1);
        default_music_shuffle_applied = true;
    }

    std::uint32_t current_skater = 0;
    if (!seeded_skater_selection_attempted &&
        ReadCurrentSkaterChecksum(current_skater)) {
        seeded_skater_selection_attempted = true;
        const std::uint32_t selected =
            thps3_ap::SelectedSkaterNativeChecksum();
        if (selected != 0 && current_skater != selected) {
            QbStructHeader name{};
            name.type = 13;
            name.key = QbChecksum("name");
            name.value.raw = selected;
            QbStruct params{&name, &name};
            using RunScript = void (__cdecl *)(const char*, void*, int, int);
            reinterpret_cast<RunScript>(0x00428240)(
                reinterpret_cast<const char*>(0x005b6f34), &params, 0, 0);
        }
    }

    THPS3AP_ApplyCareerTimerBonus();
    ValidateGapHighlight();
    ApplyGapMenuData();
    ApplyGapAdvanceRequest();
    ApplyAllGapHighlightsRequest();
    ApplyPendingGapHighlight();
    DisplayNextMessage();
    ApplyManualTrickPermission();
    ApplyLipTrickPermission();
    ApplySpecialTrickPermission();

    using GetScoreObject = std::uint8_t* (__cdecl *)();
    std::uint8_t* const score =
        reinterpret_cast<GetScoreObject>(0x0041C590)();
    if (score != nullptr) {
        auto* const base_score = reinterpret_cast<std::uint32_t*>(score + 0x08);
        const std::uint32_t multiplier =
            *reinterpret_cast<const std::uint32_t*>(score + 0x38);
        const std::uint32_t items = thps3_ap::TakePendingScoreBonusItems(
            !IsGamePaused() && *base_score != 0 && multiplier != 0);
        if (items != 0) {
            const std::uint32_t room =
                std::numeric_limits<std::uint32_t>::max() - *base_score;
            *base_score += (std::min)(items, room / kScorePerBonusItem) *
                kScorePerBonusItem;
            thps3_ap::QueueScoreBonusItemsApplied(
                thps3_ap::ReceivedScoreBonusItemCount());
        }
    }

    using IsCareerMode = int (__cdecl *)();
    const auto is_career_mode = reinterpret_cast<IsCareerMode>(0x00421540);

    // The career selector is THPS3's cassettemenu, not its text level menu.
    // Cassette elements read numeric career flags (10-18) directly from the
    // profile flag bank. Keep that bank synchronized every frame so vanilla
    // career grants cannot make the menu go out of sync.
    using AcquireProfileManager = void* (__cdecl *)(int);
    using ReleaseProfileManager = void (__cdecl *)();
    using SetCareerFlag = void (__thiscall *)(void*, std::uint32_t);
    using GetCareerFlag = bool (__thiscall *)(void*, std::uint32_t);
    const auto acquire_profile_manager =
        reinterpret_cast<AcquireProfileManager>(0x004367E0);
    const auto release_profile_manager =
        reinterpret_cast<ReleaseProfileManager>(0x00436830);
    const auto set_career_flag =
        reinterpret_cast<SetCareerFlag>(0x004BB1D0);
    const auto unset_career_flag =
        reinterpret_cast<SetCareerFlag>(0x004BB200);
    const auto get_career_flag =
        reinterpret_cast<GetCareerFlag>(0x004BB230);

    // Flags below 100 belong to the current career profile.
    void* const profile_manager = acquire_profile_manager(0);
    void* profile = nullptr;
    if (profile_manager != nullptr) {
        using GetCurrentProfile = void* (__thiscall *)(void*, void*);
        profile = reinterpret_cast<GetCurrentProfile>(0x00438B10)(
            profile_manager, nullptr);
    }
    if (profile == nullptr) {
        release_profile_manager();
        return;
    }

    auto* const profile_bytes = static_cast<std::uint8_t*>(profile);
    const std::uint32_t current_level =
        *reinterpret_cast<const std::uint32_t*>(profile_bytes + 0x690);
    g_current_career_level.store(current_level, std::memory_order_release);
    RefreshUnlockedScoreGoals(
        profile_manager, static_cast<int>(current_level));
    if (thps3_ap::StatPointsAreLocations()) {
        SyncStatAssignment(profile, thps3_ap::ReceivedStatPointItemCount());
    }
    SyncAppearance(profile);

    const std::uint32_t state_revision = thps3_ap::StateRevision();
    const bool detect_new_goals =
        profile == g_last_objective_profile &&
        state_revision == g_last_objective_state_revision;
    ReconcileObjectiveCareerBits(profile, detect_new_goals);
    g_last_objective_profile = profile;
    g_last_objective_state_revision = state_revision;

    // Rebuild only AP-owned stat collectible bits for every level. This is a
    // pure in-memory mirror: it calls no save, award, completion, or QScript path. 
    ReconcileStatCareerBits(profile);
    ReconcileDeckInventoryBits(profile);

    void* const flag_bank =
        static_cast<std::uint8_t*>(profile) + 0x558;
    if (current_level == 3 || current_level == 6 || current_level == 8) {
        for (int medal_id = 2; medal_id >= 0; --medal_id) {
            if (get_career_flag(flag_bank, 50u + medal_id)) {
                THPS3AP_RecordMedalResult(
                    static_cast<int>(current_level), medal_id);
                break;
            }
        }
    }
    for (const auto& entry : kLevelMenuEntries) {
        const bool unlocked =
            THPS3AP_IsLevelUnlocked(entry.level_number) != FALSE;
        (unlocked ? set_career_flag : unset_career_flag)(
            flag_bank, entry.career_flag);
    }
    // Keep Cruise Ship visible as a stable AP location; flag 197 still
    // independently controls whether its cassette can be selected.
    set_career_flag(flag_bank, 159);  // SPECIAL_HAS_SEEN_SHIP
    std::uint32_t readback_mask = 0;
    for (const auto& entry : kLevelMenuEntries) {
        if (get_career_flag(flag_bank, entry.career_flag)) {
            readback_mask |= 1u << (entry.level_number - 1);
        }
    }
    release_profile_manager();

    // Cassette objects cache their unlocked state at +0x1d4 when the selector
    // is constructed. Update that cache continuously so a snapshot received
    // while the main menu is open takes effect without rebuilding the menu.
    // Looking up all nine every frame also catches objects created after the
    // most recent bridge snapshot.
    using GetMenuManager = void* (__cdecl *)(int);
    using FreeMenuManager = void (__cdecl *)();
    using GetMenuElement = void* (__thiscall *)(void*, std::uint32_t, int);
    const auto get_menu_manager =
        reinterpret_cast<GetMenuManager>(0x004D12A0);
    const auto free_menu_manager =
        reinterpret_cast<FreeMenuManager>(0x004D12F0);
    const auto get_menu_element =
        reinterpret_cast<GetMenuElement>(0x004D23B0);

    void* const manager = get_menu_manager(0);
    std::uint32_t found_mask = 0;
    std::uint32_t updated_mask = 0;
    std::uint32_t cached_before_mask = 0;
    std::array<void*, kLevelMenuEntries.size()> cassette_elements{};
    void* cassette_menu = nullptr;
    if (manager != nullptr) {
        // QB checksum of the cassettemenu container created by BuildCassetteMenu.
        cassette_menu = get_menu_element(manager, 0x1209a926, 1);
        for (std::size_t index = 0; index < kLevelMenuEntries.size(); ++index) {
            const auto& entry = kLevelMenuEntries[index];
            void* const element =
                get_menu_element(manager, entry.cassette_id_checksum, 1);
            if (element == nullptr) {
                continue;
            }
            cassette_elements[index] = element;
            const std::uint32_t bit = 1u << (entry.level_number - 1);
            found_mask |= bit;
            auto* const cached_unlocked = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(element) + 0x1d4);
            if (*cached_unlocked != 0) {
                cached_before_mask |= bit;
            }
            *cached_unlocked = THPS3AP_IsLevelUnlocked(entry.level_number) != FALSE
                ? 1u
                : 0u;
            updated_mask |= bit;
        }
        if (get_menu_element(manager, QbChecksum("career_level_gaps"), 1) !=
            nullptr) {
            using SetElementText = void (__cdecl *)(std::uint32_t, const char*);
            const auto set_element_text =
                reinterpret_cast<SetElementText>(0x004CE940);
            for (const auto& entry : kLevelMenuEntries) {
                const std::uint32_t checked_objectives =
                    thps3_ap::CheckedObjectiveMask(entry.level_number);
                const std::uint32_t unlocked_objectives =
                    thps3_ap::AvailableObjectiveLocationMask(entry.level_number) |
                    checked_objectives;
                char goals[16];
                std::snprintf(
                    goals, sizeof(goals), "%d/%d (%u)",
                    std::popcount(checked_objectives),
                    std::popcount(unlocked_objectives),
                    thps3_ap::ActiveObjectiveCount(entry.level_number));
                char goal_id[16];
                std::snprintf(
                    goal_id, sizeof(goal_id), "level%d", entry.level_number);
                set_element_text(QbChecksum(goal_id), goals);

                char text[24];
                std::snprintf(
                    text,
                    sizeof(text),
                    "%u/%u (%u)",
                    thps3_ap::CheckedGapCount(entry.level_number),
                    thps3_ap::InLogicGapCount(entry.level_number),
                    thps3_ap::ActiveGapCount(entry.level_number));
                char id[24];
                std::snprintf(
                    id, sizeof(id), "AP_LevelGap%d", entry.level_number);
                set_element_text(QbChecksum(id), text);

                std::snprintf(
                    text,
                    sizeof(text),
                    "%u/%u",
                    thps3_ap::CheckedItemCount(entry.level_number),
                    thps3_ap::ActiveItemCount(entry.level_number));
                std::snprintf(
                    id, sizeof(id), "AP_LevelItem%d", entry.level_number);
                set_element_text(QbChecksum(id), text);
            }
        }
        if (get_menu_element(
                manager, QbChecksum("AP_ToggleCollectibleMarkers"), 1) !=
            nullptr) {
            using SetElementText = void (__cdecl *)(const char*, const char*);
            reinterpret_cast<SetElementText>(0x004ce920)(
                "AP_ToggleCollectibleMarkers",
                g_collectible_markers_enabled.load(std::memory_order_acquire)
                    ? "Item Markers: On"
                    : "Item Markers: Off");
        }
        free_menu_manager();
    }

    if (cassette_menu != nullptr) {
        if (g_level_menu_refresh_requested.exchange(
                false, std::memory_order_acq_rel)) {
            // Refresh the currently highlighted cassette's shared detail
            // panel immediately. PartyMod's AP hook at the two +0x1d4 reads
            // makes this native routine consume AP state instead of vanilla
            // career progression. This runs once per changed snapshot, not
            // once per frame.
            THPS3AP_RefreshCassetteMenu(cassette_menu, nullptr);
        }
    }

    std::uint32_t cached_after_mask = 0;
    for (std::size_t index = 0; index < cassette_elements.size(); ++index) {
        if (cassette_elements[index] == nullptr) {
            continue;
        }
        if (*reinterpret_cast<std::uint32_t*>(
                static_cast<std::uint8_t*>(cassette_elements[index]) + 0x1d4) !=
            0) {
            cached_after_mask |= 1u <<
                (kLevelMenuEntries[index].level_number - 1);
        }
    }

}

extern "C" void THPS3AP_SetGapMenuData(
    const char* title,
    const char* const* rows,
    const std::uint32_t* checksums,
    std::uint32_t row_count) {
    const std::lock_guard lock(g_gap_menu_mutex);
    g_gap_menu_title = title != nullptr ? title : "Remaining Gaps";
    g_gap_menu_row_count = (std::min)(
        static_cast<std::size_t>(row_count), kGapMenuRowCount);
    for (std::size_t index = 0; index < g_gap_menu_row_count; ++index) {
        g_gap_menu_rows[index] = rows[index] != nullptr ? rows[index] : "";
        g_gap_menu_checksums[index] = checksums != nullptr ? checksums[index] : 0;
    }
    g_gap_highlight_validation_pending.store(true, std::memory_order_release);
    if (g_all_gap_highlights_enabled) {
        g_all_gap_highlights_refresh_pending.store(true, std::memory_order_release);
    }
    g_gap_menu_pending.store(true, std::memory_order_release);
}

namespace {

constexpr std::uint32_t ToggleGapSelection(
    std::uint32_t current,
    std::uint32_t requested) {
    return current == requested ? 0 : requested;
}

static_assert(ToggleGapSelection(7, 7) == 0);
static_assert(ToggleGapSelection(3, 7) == 7);

constexpr std::size_t NextGapOrderIndex(
    const std::uint32_t* order,
    std::size_t order_count,
    std::size_t start,
    const std::uint32_t* remaining,
    std::size_t remaining_count) {
    for (std::size_t index = start; index < order_count; ++index) {
        for (std::size_t row = 0; row < remaining_count; ++row) {
            if (order[index] == remaining[row]) {
                return index;
            }
        }
    }
    return order_count;
}

constexpr std::uint32_t kGapOrderCheck[] = {10, 20, 30};
constexpr std::uint32_t kRemainingGapCheck[] = {10, 30};
static_assert(NextGapOrderIndex(
    kGapOrderCheck, 3, 1, kRemainingGapCheck, 2) == 2);
constexpr std::uint32_t kExhaustedGapOrderCheck[] = {10, 20, 30};
constexpr std::uint32_t kWrappedGapCheck[] = {10};
static_assert(NextGapOrderIndex(
    kExhaustedGapOrderCheck, 3, 3, kWrappedGapCheck, 1) == 3);
static_assert(NextGapOrderIndex(
    kWrappedGapCheck, 1, 0, kWrappedGapCheck, 1) == 0);

void RememberGapOrder(std::size_t cursor) {
    g_gap_auto_order = g_gap_menu_checksums;
    g_gap_auto_order_count = g_gap_menu_row_count;
    g_gap_auto_cursor = cursor;
}

bool QueueNextGap(std::size_t start) {
    const std::size_t index = NextGapOrderIndex(
        g_gap_auto_order.data(), g_gap_auto_order_count, start,
        g_gap_menu_checksums.data(), g_gap_menu_row_count);
    if (index == g_gap_auto_order_count) {
        return false;
    }
    const auto row = std::find(
        g_gap_menu_checksums.begin(),
        g_gap_menu_checksums.begin() + g_gap_menu_row_count,
        g_gap_auto_order[index]);
    g_gap_auto_cursor = index;
    g_gap_highlight_pending_auto.store(true, std::memory_order_release);
    g_gap_highlight_pending_row.store(
        static_cast<std::uint32_t>(row - g_gap_menu_checksums.begin()),
        std::memory_order_release);
    return true;
}

bool QueueNextGapOrRefresh(std::size_t start) {
    if (QueueNextGap(start)) {
        return true;
    }
    RememberGapOrder(kGapMenuRowCount);
    return QueueNextGap(0);
}

void ApplyGapAdvanceRequest() {
    if (!g_gap_advance_requested.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const std::lock_guard lock(g_gap_menu_mutex);
    if (g_gap_menu_row_count == 0) {
        g_gap_advance_requested.store(true, std::memory_order_release);
        return;
    }
    if (g_gap_auto_order_count == 0) {
        const auto selected = std::find(
            g_gap_menu_checksums.begin(),
            g_gap_menu_checksums.begin() + g_gap_menu_row_count,
            g_gap_highlighted_checksum);
        const std::size_t cursor = selected ==
                g_gap_menu_checksums.begin() + g_gap_menu_row_count
            ? kGapMenuRowCount
            : static_cast<std::size_t>(selected - g_gap_menu_checksums.begin());
        RememberGapOrder(cursor);
    }
    const std::size_t start = g_gap_auto_cursor < g_gap_auto_order_count
        ? g_gap_auto_cursor + 1
        : 0;
    QueueNextGapOrRefresh(start);
}

void ValidateGapHighlight() {
    if (!g_gap_highlight_validation_pending.exchange(
            false, std::memory_order_acq_rel) ||
        g_gap_highlighted_checksum == 0) {
        return;
    }
    bool still_listed = false;
    {
        const std::lock_guard lock(g_gap_menu_mutex);
        const auto selected = std::find(
            g_gap_menu_checksums.begin(),
            g_gap_menu_checksums.begin() + g_gap_menu_row_count,
            g_gap_highlighted_checksum);
        still_listed = selected !=
            g_gap_menu_checksums.begin() + g_gap_menu_row_count;
        if (still_listed) {
            g_gap_highlighted_row = static_cast<std::size_t>(
                selected - g_gap_menu_checksums.begin());
            g_gap_menu_preferred_focus_row = kGapMenuRowCount;
        } else {
            g_gap_menu_preferred_focus_row = NeighborGapMenuRow(
                g_gap_highlighted_row, g_gap_menu_row_count);
        }
    }
    if (!still_listed) {
        RestoreGapSectors();
        g_gap_highlighted_checksum = 0;
        g_gap_highlighted_row = kGapMenuRowCount;
        {
            const std::lock_guard lock(g_gap_menu_mutex);
            g_active_gap_hud_text.clear();
        }
        g_gap_menu_pending.store(true, std::memory_order_release);
        if (g_gap_auto_enabled) {
            const std::lock_guard lock(g_gap_menu_mutex);
            if (!QueueNextGapOrRefresh(g_gap_auto_cursor + 1)) {
                g_gap_auto_enabled = false;
                g_active_gap_hud_text.clear();
            }
        }
    }
}

void AddGapHighlight(
    std::uint32_t gap_checksum, std::uint32_t current_level) {
    constexpr std::uint32_t kRailNode = 0x8e6b02adu;
    constexpr std::uint32_t kEnvironmentObject = 0xdabd3086u;
    const std::size_t rail_line_count_before = g_gap_rail_lines.size();
    const std::size_t marker_count_before = g_gap_markers.size();
    bool has_rail_tint_target = false;
    bool rail_tint_complete = true;
    std::vector<std::uint32_t> tinted_rail_clusters;
    for (const GapHighlightEndpoint& endpoint : kGapHighlightEndpoints) {
        if (endpoint.gap != gap_checksum
            || (endpoint.level != 0u && endpoint.level != current_level)) {
            continue;
        }
        if (endpoint.role == 3u) {
            continue;
        }
        if (endpoint.role == 2u || endpoint.role == 4u) {
            has_rail_tint_target = true;
            const bool tinted = ColorGapSector(endpoint.node);
            rail_tint_complete &= tinted;
            if (tinted && endpoint.cluster != 0u) {
                tinted_rail_clusters.push_back(endpoint.cluster);
            }
            continue;
        }
        if (endpoint.node_class == kRailNode) {
            continue;
        }
        if (endpoint.node_class == kEnvironmentObject) {
            ColorGapSector(endpoint.node);
        } else if (!ColorGapSector(endpoint.node)) {
            ColorGapCluster(endpoint.cluster);
        }
    }
    rail_tint_complete &= has_rail_tint_target;
    for (const GapHighlightEndpoint& endpoint : kGapHighlightEndpoints) {
        if (endpoint.gap == gap_checksum
            && (endpoint.level == 0u || endpoint.level == current_level)
            && endpoint.node_class == kRailNode
            && endpoint.cluster != 0u
            && std::find(
                tinted_rail_clusters.begin(), tinted_rail_clusters.end(),
                endpoint.cluster) == tinted_rail_clusters.end()) {
            rail_tint_complete = false;
            break;
        }
    }
    for (const GapRailSegment& segment : kGapRailSegments) {
        if (segment.gap == gap_checksum
            && (segment.level == 0u || segment.level == current_level)
            && !rail_tint_complete) {
            g_gap_rail_lines.push_back(segment);
        }
    }
    const bool rail_highlighted = rail_tint_complete
        || g_gap_rail_lines.size() != rail_line_count_before;

    const bool has_reviewed_markers = std::any_of(
        kGapHighlightEndpoints.begin(), kGapHighlightEndpoints.end(),
        [gap_checksum, current_level](const GapHighlightEndpoint& endpoint) {
            return endpoint.gap == gap_checksum
                && (endpoint.level == 0u || endpoint.level == current_level)
                && endpoint.role == 3u;
        });
    const bool suppress_markers = std::any_of(
        kGapHighlightEndpoints.begin(), kGapHighlightEndpoints.end(),
        [gap_checksum, current_level](const GapHighlightEndpoint& endpoint) {
            return endpoint.gap == gap_checksum
                && (endpoint.level == 0u || endpoint.level == current_level)
                && endpoint.role == 4u;
        });
    bool rail_marker_added = false;
    for (const GapHighlightEndpoint& endpoint : kGapHighlightEndpoints) {
        if (endpoint.gap != gap_checksum
            || (endpoint.level != 0u && endpoint.level != current_level)
            || endpoint.role == 2u || endpoint.role == 4u
            || has_reviewed_markers != (endpoint.role == 3u)) {
            continue;
        }
        if (!has_reviewed_markers
            && rail_highlighted && endpoint.node_class == kRailNode) {
            if (rail_marker_added) {
                continue;
            }
            rail_marker_added = true;
        }
        g_gap_markers.push_back(endpoint);
    }
    if (g_gap_markers.size() == marker_count_before && !suppress_markers) {
        const auto marker = std::find_if(
            kGapFallbackMarkers.begin(), kGapFallbackMarkers.end(),
            [gap_checksum, current_level](const GapHighlightEndpoint& endpoint) {
                return endpoint.gap == gap_checksum
                    && (endpoint.level == 0u || endpoint.level == current_level);
            });
        if (marker != kGapFallbackMarkers.end()) {
            g_gap_markers.push_back(*marker);
        }
    }
}

void ApplyPendingGapHighlight() {
    const std::uint32_t row_index = g_gap_highlight_pending_row.exchange(
        0xffffffffu, std::memory_order_acq_rel);
    if (row_index == 0xffffffffu) {
        return;
    }
    const bool automatic = g_gap_highlight_pending_auto.exchange(
        false, std::memory_order_acq_rel);
    std::uint32_t checksum = 0;
    std::string name;
    {
        const std::lock_guard lock(g_gap_menu_mutex);
        if (row_index >= g_gap_menu_row_count) {
            return;
        }
        checksum = g_gap_menu_checksums[row_index];
        name = g_gap_menu_rows[row_index];
        if (!automatic) {
            RememberGapOrder(row_index);
        }
    }
    if (checksum == 0) {
        {
            const std::lock_guard lock(g_gap_menu_mutex);
            g_active_gap_hud_text.clear();
        }
        if (automatic) {
            g_gap_auto_enabled = false;
        }
        return;
    }

    g_all_gap_highlights_enabled = false;
    g_all_gap_highlights_refresh_pending.store(false, std::memory_order_release);
    RestoreGapSectors();
    g_gap_auto_enabled = automatic;
    g_gap_highlighted_checksum = automatic
        ? checksum
        : ToggleGapSelection(g_gap_highlighted_checksum, checksum);
    g_gap_highlighted_row = g_gap_highlighted_checksum != 0
        ? row_index
        : kGapMenuRowCount;
    {
        const std::lock_guard lock(g_gap_menu_mutex);
        g_active_gap_hud_text = g_gap_highlighted_checksum != 0
            ? "Active Gap: " + name
            : "";
    }
    GapHighlightLog(
        "SELECT row=%u requested=%08x active=%08x",
        row_index, checksum, g_gap_highlighted_checksum);
    g_gap_menu_pending.store(true, std::memory_order_release);
    if (g_gap_highlighted_checksum == 0) {
        return;
    }

    const std::uint32_t current_level =
        g_current_career_level.load(std::memory_order_acquire);
    AddGapHighlight(g_gap_highlighted_checksum, current_level);
}

void ApplyAllGapHighlightsRequest() {
    const bool toggle = g_all_gap_highlights_toggle_requested.exchange(
        false, std::memory_order_acq_rel);
    const bool refresh = g_all_gap_highlights_refresh_pending.exchange(
        false, std::memory_order_acq_rel);
    if (!toggle && !refresh) {
        return;
    }
    if (toggle) {
        g_all_gap_highlights_enabled = !g_all_gap_highlights_enabled;
        if (g_all_gap_highlights_enabled) {
            g_gap_auto_enabled = false;
            g_gap_highlighted_checksum = 0;
            g_gap_highlighted_row = kGapMenuRowCount;
            g_gap_highlight_pending_auto.store(false, std::memory_order_release);
            g_gap_highlight_pending_row.store(
                0xffffffffu, std::memory_order_release);
            g_gap_menu_pending.store(true, std::memory_order_release);
        }
    }
    RestoreGapSectors();
    const std::uint32_t current_level =
        g_current_career_level.load(std::memory_order_acquire);
    if (g_all_gap_highlights_enabled) {
        std::array<std::uint32_t, kGapMenuRowCount> checksums;
        std::size_t row_count;
        {
            const std::lock_guard lock(g_gap_menu_mutex);
            checksums = g_gap_menu_checksums;
            row_count = g_gap_menu_row_count;
            g_active_gap_hud_text = "Active gap: ALL";
        }
        for (std::size_t index = 0; index < row_count; ++index) {
            if (checksums[index] != 0) {
                AddGapHighlight(checksums[index], current_level);
            }
        }
        GapHighlightLog(
            "ALL enabled gaps=%u targets=%u",
            static_cast<unsigned>(row_count),
            static_cast<unsigned>(g_gap_tint_sectors.size()));
    } else if (g_gap_highlighted_checksum != 0) {
        {
            const std::lock_guard lock(g_gap_menu_mutex);
            const auto selected = std::find(
                g_gap_menu_checksums.begin(),
                g_gap_menu_checksums.begin() + g_gap_menu_row_count,
                g_gap_highlighted_checksum);
            g_active_gap_hud_text = selected !=
                    g_gap_menu_checksums.begin() + g_gap_menu_row_count
                ? "Active Gap: " + g_gap_menu_rows[
                    selected - g_gap_menu_checksums.begin()]
                : "";
        }
        AddGapHighlight(g_gap_highlighted_checksum, current_level);
        GapHighlightLog("ALL disabled");
    } else {
        const std::lock_guard lock(g_gap_menu_mutex);
        g_active_gap_hud_text.clear();
    }
}

}  // namespace

extern "C" void THPS3AP_SelectGapMenuRow(std::uint32_t row_index) {
    g_gap_auto_enabled = false;
    g_gap_highlight_pending_auto.store(false, std::memory_order_release);
    g_gap_highlight_pending_row.store(row_index, std::memory_order_release);
}

extern "C" void THPS3AP_AdvanceGapHighlight() {
    g_gap_advance_requested.store(true, std::memory_order_release);
    THPS3AP_QueueGapListRequest(static_cast<int>(
        g_current_career_level.load(std::memory_order_acquire)));
}

extern "C" void THPS3AP_ToggleAllGapHighlights() {
    g_all_gap_highlights_toggle_requested.store(true, std::memory_order_release);
    THPS3AP_QueueGapListRequest(static_cast<int>(
        g_current_career_level.load(std::memory_order_acquire)));
}

extern "C" BOOL THPS3AP_HasActiveStageOverlay() {
    return (!g_gap_tint_sectors.empty() || !g_gap_markers.empty() ||
            !g_gap_rail_lines.empty() ||
            g_collectible_markers_enabled.load(std::memory_order_acquire))
        ? TRUE : FALSE;
}

extern "C" void THPS3AP_CaptureStageRenderState(void* device) {
    if (device == nullptr) {
        return;
    }
    void** const api = *reinterpret_cast<void***>(device);
    using CreateStateBlock = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t*);
    using DeleteStateBlock = long (__stdcall *)(void*, std::uint32_t);
    std::uint32_t state_block = 0;
    if (reinterpret_cast<CreateStateBlock>(api[57])(
            device, 1, &state_block) < 0) {
        return;
    }
    if (g_stage_state_block != 0 && g_stage_state_device == device) {
        reinterpret_cast<DeleteStateBlock>(api[56])(
            device, g_stage_state_block);
    }
    g_stage_state_device = device;
    g_stage_state_block = state_block;
}

extern "C" void THPS3AP_DrawCapturedStageOverlays(void* device) {
    if (device == nullptr || g_stage_state_device != device ||
        g_stage_state_block == 0) {
        return;
    }
    void** const api = *reinterpret_cast<void***>(device);
    using CreateStateBlock = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t*);
    using ApplyStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DeleteStateBlock = long (__stdcall *)(void*, std::uint32_t);
    if (g_gap_tint_sectors.empty() && g_gap_markers.empty() &&
        g_gap_rail_lines.empty() &&
        !g_collectible_markers_enabled.load(std::memory_order_acquire)) {
        const std::uint32_t unused_state = g_stage_state_block;
        g_stage_state_block = 0;
        reinterpret_cast<DeleteStateBlock>(api[56])(device, unused_state);
        static bool skip_logged = false;
        if (!skip_logged) {
            GapHighlightLog("STAGE_SKIP no overlays");
            skip_logged = true;
        }
        return;
    }
    std::uint32_t current_state = 0;
    if (reinterpret_cast<CreateStateBlock>(api[57])(
            device, 1, &current_state) < 0) {
        return;
    }
    const std::uint32_t stage_state = g_stage_state_block;
    g_stage_state_block = 0;
    reinterpret_cast<ApplyStateBlock>(api[54])(device, stage_state);
    THPS3AP_DrawGapTintOverlay(device);
    THPS3AP_DrawGapMarkers(device);
    reinterpret_cast<ApplyStateBlock>(api[54])(device, current_state);
    reinterpret_cast<DeleteStateBlock>(api[56])(device, current_state);
    reinterpret_cast<DeleteStateBlock>(api[56])(device, stage_state);
}

extern "C" void THPS3AP_DrawGapTintOverlay(void* device) {
    if (device == nullptr || g_gap_tint_sectors.empty()) {
        return;
    }

    struct MeshRecord {
        MeshRecord* next;
        std::uint32_t payload;
        std::uint8_t* sector;
    };
    auto tint_checksum = [](std::uint8_t* sector) -> std::uint32_t {
        for (std::uint32_t checksum : g_gap_tint_sectors) {
            if (FindSuperSector(checksum) == sector) {
                return checksum;
            }
        }
        return 0;
    };

    void** const api = *reinterpret_cast<void***>(device);
    using SetState = long (__stdcall *)(void*, std::uint32_t, std::uint32_t);
    using SetStageState = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, std::uint32_t);
    using CreateStateBlock = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t*);
    using ApplyStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DeleteStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using SetTexture = long (__stdcall *)(void*, std::uint32_t, void*);
    using SetShader = long (__stdcall *)(void*, std::uint32_t);
    using SetStreamSource = long (__stdcall *)(
        void*, std::uint32_t, void*, std::uint32_t);
    using SetIndices = long (__stdcall *)(void*, void*, std::uint32_t);
    using DrawIndexedPrimitive = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t, std::uint32_t);

    std::uint32_t saved_state = 0;
    if (reinterpret_cast<CreateStateBlock>(api[57])(
            device, 1, &saved_state) < 0) {  // D3DSBT_ALL
        return;
    }
    const int mesh_count = *reinterpret_cast<int*>(0x005c94c8);
    auto* mesh = reinterpret_cast<std::int32_t*>(0x00901758);
    for (int mesh_index = 0; mesh_index < mesh_count;
         ++mesh_index, mesh += 5) {
        if (mesh[2] == 0) {
            continue;
        }
        auto* record = reinterpret_cast<MeshRecord*>(mesh[3]);
        while (record != nullptr) {
            MeshRecord* const next = record->next;
            const std::uint32_t checksum = tint_checksum(record->sector);
            if (checksum == 0) {
                record = next;
                continue;
            }
            reinterpret_cast<SetState>(api[50])(
                device, 60, kGapHighlightTint);
            reinterpret_cast<SetState>(api[50])(
                device, 14, 0);  // ZWRITEENABLE = false
            reinterpret_cast<SetState>(api[50])(
                device, 15, 0);  // ALPHATESTENABLE = false
            reinterpret_cast<SetState>(api[50])(
                device, 19, 5);  // SRCBLEND = SRCALPHA
            reinterpret_cast<SetState>(api[50])(
                device, 20, 6);  // DESTBLEND = INVSRCALPHA
            reinterpret_cast<SetState>(api[50])(
                device, 27, 1);  // ALPHABLENDENABLE = true
            reinterpret_cast<SetState>(api[50])(
                device, 171, 1);  // BLENDOP = ADD
            reinterpret_cast<SetStageState>(api[63])(
                device, 0, 1, 2);  // COLOROP = SELECTARG1
            reinterpret_cast<SetStageState>(api[63])(
                device, 0, 2, 3);  // COLORARG1 = TFACTOR
            reinterpret_cast<SetStageState>(api[63])(
                device, 0, 4, 2);  // ALPHAOP = SELECTARG1
            reinterpret_cast<SetStageState>(api[63])(
                device, 0, 5, 3);  // ALPHAARG1 = TFACTOR
            if (std::find(
                    g_gap_logged_draw_sectors.begin(),
                    g_gap_logged_draw_sectors.end(), checksum) ==
                g_gap_logged_draw_sectors.end()) {
                g_gap_logged_draw_sectors.push_back(checksum);
                GapHighlightLog(
                    "DRAW crc=%08x record=%p sector=%p",
                    checksum, record, record->sector);
            }
            const auto* const payload =
                reinterpret_cast<const std::uint8_t*>(record->payload);
            if (payload == nullptr) {
                record = next;
                continue;
            }
            const auto field = [payload](std::size_t offset) {
                return *reinterpret_cast<const std::uint32_t*>(payload + offset);
            };
            const std::uint32_t primitive_type = field(0x18);
            const std::uint32_t index_count = field(0x0c);
            const std::uint32_t vertex_buffer = field(0x20);
            const std::uint32_t index_buffer = field(0x1c);
            const std::uint32_t base_vertex_index = field(0x24);
            std::uint32_t primitive_count = 0;
            switch (primitive_type) {
            case 2: primitive_count = index_count / 2; break;
            case 3: primitive_count = index_count > 0 ? index_count - 1 : 0; break;
            case 4: primitive_count = index_count / 3; break;
            case 5:
            case 6: primitive_count = index_count > 2 ? index_count - 2 : 0; break;
            default: break;
            }
            if (primitive_count == 0 || vertex_buffer == 0 || index_buffer == 0) {
                record = next;
                continue;
            }
            reinterpret_cast<SetTexture>(api[61])(device, 0, nullptr);
            reinterpret_cast<SetShader>(api[88])(device, 0);
            reinterpret_cast<SetShader>(api[76])(device, field(0x14));
            reinterpret_cast<SetStreamSource>(api[83])(
                device, 0, reinterpret_cast<void*>(vertex_buffer), field(0x04));
            reinterpret_cast<SetIndices>(api[85])(
                device, reinterpret_cast<void*>(index_buffer), base_vertex_index);
            reinterpret_cast<DrawIndexedPrimitive>(api[71])(
                device, primitive_type, 0, field(0x08), 0, primitive_count);
            record = next;
        }
    }
    reinterpret_cast<ApplyStateBlock>(api[54])(device, saved_state);
    reinterpret_cast<DeleteStateBlock>(api[56])(device, saved_state);
}

extern "C" void THPS3AP_DrawGapMarkers(void* device) {
    if (device == nullptr) {
        return;
    }
    struct Vertex {
        float x;
        float y;
        float z;
        std::uint32_t color;
    };
    std::vector<Vertex> vertices;
    vertices.reserve(g_gap_markers.size() * 6 + g_gap_rail_lines.size() * 2 + 36);
    const auto add_cross = [&vertices](
            float x, float y, float z, std::uint32_t color) {
        constexpr float collectible_radius = 24.0f;
        vertices.insert(vertices.end(), {
            {x - collectible_radius, y, z, color},
            {x + collectible_radius, y, z, color},
            {x, y - collectible_radius, z, color},
            {x, y + collectible_radius, z, color},
            {x, y, z - collectible_radius, color},
            {x, y, z + collectible_radius, color},
        });
    };
    for (const GapHighlightEndpoint& marker : g_gap_markers) {
        if (marker.role == 3u) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (LiveObjectPosition(marker.node, x, y, z)) {
                add_cross(x, y + 0.2f, z, kGapHighlightColor);
                continue;
            }
        }
        const float y = marker.y + 0.2f;
        const float z = -marker.z;
        add_cross(marker.x, y, z, kGapHighlightColor);
    }
    for (const GapRailSegment& rail : g_gap_rail_lines) {
        vertices.push_back({
            rail.start_x, rail.start_y + 0.2f, -rail.start_z, kGapHighlightColor});
        vertices.push_back({
            rail.end_x, rail.end_y + 0.2f, -rail.end_z, kGapHighlightColor});
    }

    constexpr std::array<unsigned, 23> stat_layouts{
        1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 1,
        2, 1, 3, 2, 4, 3, 4, 1, 1, 4};
    constexpr std::array<unsigned, 23> deck_layouts{
        3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 3, 1,
        1, 2, 1, 3, 2, 3, 3, 3, 3, 1};
    static_assert(stat_layouts[7] == 4 && deck_layouts[7] == 2);
    const std::uint32_t level = g_current_career_level.load(std::memory_order_acquire);
    const std::uint32_t skater = thps3_ap::SelectedSkaterProfileIndex();
    if (g_collectible_markers_enabled.load(std::memory_order_acquire) &&
        level >= 1 && level <= 9 && skater < stat_layouts.size()) {
        using GetArray = QbArray* (__cdecl *)(std::uint32_t, std::uint32_t);
        QbArray* const nodes = reinterpret_cast<GetArray>(0x00426590)(
            0xc472ecc5u, 1);  // NodeArray
        struct Target {
            std::uint32_t name;
            std::uint32_t color;
        };
        std::array<Target, 6> targets{};
        std::array<bool, 6> marker_drawn{};
        std::size_t target_count = 0;
        char name[64]{};
        const std::uint32_t checked_stats =
            thps3_ap::CheckedStatPointMask(static_cast<int>(level));
        if (thps3_ap::StatPointsAreLocations()) {
            for (unsigned point = 1; point <= 5; ++point) {
                if ((checked_stats & (1u << (point - 1))) == 0) {
                    std::snprintf(name, sizeof(name), "TRG_Stat_Point_%c%02u",
                        'A' + stat_layouts[skater] - 1, point);
                    targets[target_count++] = {
                        QbChecksum(name), kStatPointHighlightColor};
                }
            }
        }
        if (thps3_ap::HiddenDecksAreLocations() &&
            !thps3_ap::IsDeckChecked(static_cast<int>(level))) {
            std::snprintf(name, sizeof(name), "TRG_Goal_Deck_%u",
                deck_layouts[skater]);
            targets[target_count++] = {QbChecksum(name), kDeckHighlightColor};
        }
        if (nodes != nullptr && nodes->structures != nullptr &&
            (nodes->type == 0x0a || nodes->type == 0x0b)) {
            for (std::uint16_t index = 0; index < nodes->size; ++index) {
                QbStruct* const node = nodes->structures[index];
                std::uint32_t node_name = 0;
                const float* position = nullptr;
                unsigned field_count = 0;
                for (QbStructHeader* field = node != nullptr ? node->head : nullptr;
                     field != nullptr && field_count++ < 64;
                     field = field->next) {
                    if (field->key == QbChecksum("Name")) {
                        node_name = field->value.raw;
                    } else if (field->key == QbChecksum("Position") &&
                               field->type == 6) {
                        position = reinterpret_cast<const float*>(
                            field->value.text);
                    }
                }
                if (node_name == 0 || position == nullptr ||
                    IsBadReadPtr(position, sizeof(float) * 3)) {
                    continue;
                }
                const auto target = std::find_if(
                    targets.begin(), targets.begin() + target_count,
                    [node_name](const Target& item) {
                        return item.name == node_name;
                    });
                if (target == targets.begin() + target_count) {
                    continue;
                }
                const auto target_index = static_cast<std::size_t>(
                    target - targets.begin());
                if (marker_drawn[target_index]) {
                    continue;
                }
                marker_drawn[target_index] = true;
                add_cross(
                    position[0], position[1] + 0.2f, -position[2],
                    target->color);
            }
        }
    }
    if (vertices.empty()) {
        return;
    }

    void** const api = *reinterpret_cast<void***>(device);
    using SetState = long (__stdcall *)(void*, std::uint32_t, std::uint32_t);
    using SetTransform = long (__stdcall *)(void*, std::uint32_t, const float*);
    using SetStageState = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, std::uint32_t);
    using SetTexture = long (__stdcall *)(void*, std::uint32_t, void*);
    using SetShader = long (__stdcall *)(void*, std::uint32_t);
    using CreateStateBlock = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t*);
    using ApplyStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DeleteStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DrawPrimitiveUp = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, const void*, std::uint32_t);

    std::uint32_t state_block = 0;
    if (reinterpret_cast<CreateStateBlock>(api[57])(
            device, 1, &state_block) < 0) {
        return;
    }
    constexpr float identity[16]{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    reinterpret_cast<SetState>(api[50])(device, 137, 0);
    reinterpret_cast<SetState>(api[50])(device, 7, 0);
    reinterpret_cast<SetState>(api[50])(device, 14, 0);
    reinterpret_cast<SetState>(api[50])(device, 28, 0);
    reinterpret_cast<SetTransform>(api[37])(device, 256, identity);
    reinterpret_cast<SetTexture>(api[61])(device, 0, nullptr);
    reinterpret_cast<SetStageState>(api[63])(device, 0, 1, 2);
    reinterpret_cast<SetStageState>(api[63])(device, 0, 2, 0);
    reinterpret_cast<SetStageState>(api[63])(device, 0, 4, 2);
    reinterpret_cast<SetStageState>(api[63])(device, 0, 5, 0);
    reinterpret_cast<SetShader>(api[76])(device, 0x42);
    reinterpret_cast<DrawPrimitiveUp>(api[72])(
        device, 2, static_cast<std::uint32_t>(vertices.size() / 2),
        vertices.data(), sizeof(Vertex));

    reinterpret_cast<ApplyStateBlock>(api[54])(device, state_block);
    reinterpret_cast<DeleteStateBlock>(api[56])(device, state_block);
}

extern "C" void THPS3AP_ToggleCollectibleMarkers() {
    const bool enabled = !g_collectible_markers_enabled.load(
        std::memory_order_acquire);
    g_collectible_markers_enabled.store(enabled, std::memory_order_release);
    thps3_ap::QueueCollectibleMarkersEnabled(enabled);
    using SetElementText = void (__cdecl *)(const char*, const char*);
    reinterpret_cast<SetElementText>(0x004ce920)(
        "AP_ToggleCollectibleMarkers",
        enabled ? "Item Markers: On" : "Item Markers: Off");
}

extern "C" void THPS3AP_ToggleLosAngelesGeometry() {
    const bool intact = !g_use_intact_los_angeles_geometry.load(
        std::memory_order_acquire);
    g_use_intact_los_angeles_geometry.store(intact, std::memory_order_release);
    thps3_ap::QueueDisplayMessage(intact
        ? "LA GEOMETRY: INTACT AFTER RELOAD"
        : "LA GEOMETRY: NORMAL AFTER RELOAD");
}

extern "C" BOOL THPS3AP_UseIntactLosAngelesGeometry() {
    return g_use_intact_los_angeles_geometry.load(std::memory_order_acquire)
        ? TRUE
        : FALSE;
}

extern "C" void THPS3AP_SetCollectibleMarkersEnabled(BOOL enabled) {
    g_collectible_markers_enabled.store(enabled != FALSE, std::memory_order_release);
}

extern "C" BOOL THPS3AP_CollectibleMarkersEnabled() {
    return g_collectible_markers_enabled.load(std::memory_order_acquire)
        ? TRUE : FALSE;
}

extern "C" void THPS3AP_DrawHudOverlay(void* device) {
    const std::uint32_t level =
        g_current_career_level.load(std::memory_order_acquire);
    if (device == nullptr || !THPS3AP_HasStateSnapshot() ||
        level < 1 || level > 9) {
        return;
    }
    struct Rect {
        long x1;
        long y1;
        long x2;
        long y2;
    };
    struct Viewport {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t width;
        std::uint32_t height;
        float min_z;
        float max_z;
    } viewport{};
    void** const api = *reinterpret_cast<void***>(device);
    using GetViewport = long (__stdcall *)(void*, Viewport*);
    const long viewport_result =
        reinterpret_cast<GetViewport>(api[41])(device, &viewport);
    if (viewport_result < 0) {
        return;
    }
    static bool viewport_logged = false;
    if (!viewport_logged) {
        GapHighlightLog(
            "HUD_VIEWPORT x=%u y=%u width=%u height=%u result=%ld",
            viewport.x, viewport.y, viewport.width, viewport.height,
            viewport_result);
        viewport_logged = true;
    }
    char counter[40];
    std::snprintf(
        counter, sizeof(counter), "GAPS: %u/%u (%u)",
        thps3_ap::CheckedGapCount(static_cast<int>(level)),
        thps3_ap::InLogicGapCount(static_cast<int>(level)),
        thps3_ap::ActiveGapCount(static_cast<int>(level)));
    std::string active_gap;
    {
        const std::lock_guard lock(g_gap_menu_mutex);
        active_gap = g_active_gap_hud_text;
    }
    const auto glyph_for = [](char character) -> const HudFontGlyph& {
        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A';
        }
        if (character < 32 || character > 126) {
            character = '?';
        }
        return kOnaniHudGlyphs[character - 32];
    };
    const auto text_width = [&glyph_for](std::string_view text) {
        float width = 0.0f;
        for (char character : text) {
            width += glyph_for(character).advance;
        }
        return width;
    };
    std::vector<Rect> rectangles;
    rectangles.reserve(2048);
    std::vector<Rect> background_rectangles;
    background_rectangles.reserve(2);
    const auto append_background = [&](float left, float top,
                                       float width, float height) {
        const long x1 = static_cast<long>(std::floor(left)) - 4;
        const long y1 = static_cast<long>(std::floor(top)) - 4;
        const long x2 = static_cast<long>(std::ceil(left + width)) + 4;
        const long y2 = static_cast<long>(std::ceil(top + height)) + 4;
        background_rectangles.push_back({x1, y1, x2, y2});
    };
    const auto append_text = [&](std::string_view text, float start_x,
                                 float start_y, float scale) {
        float cursor_x = start_x;
        for (char character : text) {
            const HudFontGlyph& glyph = glyph_for(character);
            for (std::size_t row = 0; row < glyph.rows.size(); ++row) {
                std::uint32_t bit = 0;
                while (bit < 32) {
                    while (bit < 32 &&
                           (glyph.rows[row] & (1u << bit)) == 0) {
                        ++bit;
                    }
                    const std::uint32_t run_start = bit;
                    while (bit < 32 &&
                           (glyph.rows[row] & (1u << bit)) != 0) {
                        ++bit;
                    }
                    if (run_start == bit) {
                        continue;
                    }
                    const float left = cursor_x + run_start * scale;
                    const float top = start_y + row * scale;
                    const float right = cursor_x + bit * scale;
                    const float bottom = top + scale;
                    rectangles.push_back({
                        static_cast<long>(std::floor(left)),
                        static_cast<long>(std::floor(top)),
                        static_cast<long>(std::ceil(right)),
                        static_cast<long>(std::ceil(bottom))});
                }
            }
            cursor_x += glyph.advance * scale;
        }
    };
    constexpr float counter_scale = 1.5f;
    const float counter_width = text_width(counter) * counter_scale;
    const float hud_y = static_cast<float>(viewport.y) + 12.0f;
    const float counter_x =
        static_cast<float>(viewport.x + viewport.width) -
        counter_width - 12.0f;
    append_background(
        counter_x, hud_y, counter_width,
        kOnaniHudFontHeight * counter_scale);
    append_text(
        counter, counter_x, hud_y, counter_scale);
    if (!active_gap.empty()) {
        const float available = static_cast<float>(viewport.width) - 24.0f;
        const float unscaled_width = text_width(active_gap);
        const float active_scale = (std::min)(
            1.5f, available / unscaled_width);
        const float active_width = unscaled_width * active_scale;
        const float active_x = static_cast<float>(viewport.x) +
            (static_cast<float>(viewport.width) - active_width) * 0.5f;
        append_background(
            active_x, hud_y, active_width,
            kOnaniHudFontHeight * active_scale);
        append_text(
            active_gap, active_x, hud_y, active_scale);
    }

    using Clear = long (__stdcall *)(
        void*, std::uint32_t, const Rect*, std::uint32_t,
        std::uint32_t, float, std::uint32_t);
    struct HudVertex {
        float x;
        float y;
        float z;
        float rhw;
        std::uint32_t color;
    };
    std::vector<HudVertex> background_vertices;
    background_vertices.reserve(background_rectangles.size() * 6);
    constexpr std::uint32_t background_color = 0xa0000000u;
    for (const Rect& rect : background_rectangles) {
        const float left = static_cast<float>(rect.x1) - 0.5f;
        const float top = static_cast<float>(rect.y1) - 0.5f;
        const float right = static_cast<float>(rect.x2) - 0.5f;
        const float bottom = static_cast<float>(rect.y2) - 0.5f;
        background_vertices.insert(background_vertices.end(), {
            {left, top, 0.0f, 1.0f, background_color},
            {right, top, 0.0f, 1.0f, background_color},
            {left, bottom, 0.0f, 1.0f, background_color},
            {left, bottom, 0.0f, 1.0f, background_color},
            {right, top, 0.0f, 1.0f, background_color},
            {right, bottom, 0.0f, 1.0f, background_color},
        });
    }
    using Scene = long (__stdcall *)(void*);
    using SetState = long (__stdcall *)(void*, std::uint32_t, std::uint32_t);
    using SetStageState = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, std::uint32_t);
    using SetTexture = long (__stdcall *)(void*, std::uint32_t, void*);
    using SetShader = long (__stdcall *)(void*, std::uint32_t);
    using CreateStateBlock = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t*);
    using ApplyStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DeleteStateBlock = long (__stdcall *)(void*, std::uint32_t);
    using DrawPrimitiveUp = long (__stdcall *)(
        void*, std::uint32_t, std::uint32_t, const void*, std::uint32_t);
    const long begin_result = reinterpret_cast<Scene>(api[34])(device);
    long draw_result = -1;
    long end_result = -1;
    if (begin_result >= 0) {
        std::uint32_t state_block = 0;
        if (reinterpret_cast<CreateStateBlock>(api[57])(
                device, 1, &state_block) >= 0) {
            reinterpret_cast<SetState>(api[50])(device, 7, 0);
            reinterpret_cast<SetState>(api[50])(device, 14, 0);
            reinterpret_cast<SetState>(api[50])(device, 15, 0);
            reinterpret_cast<SetState>(api[50])(device, 19, 5);
            reinterpret_cast<SetState>(api[50])(device, 20, 6);
            reinterpret_cast<SetState>(api[50])(device, 22, 1);
            reinterpret_cast<SetState>(api[50])(device, 27, 1);
            reinterpret_cast<SetState>(api[50])(device, 28, 0);
            reinterpret_cast<SetState>(api[50])(device, 137, 0);
            reinterpret_cast<SetState>(api[50])(device, 168, 0x0f);
            reinterpret_cast<SetState>(api[50])(device, 171, 1);
            reinterpret_cast<SetTexture>(api[61])(device, 0, nullptr);
            reinterpret_cast<SetStageState>(api[63])(device, 0, 1, 2);
            reinterpret_cast<SetStageState>(api[63])(device, 0, 2, 0);
            reinterpret_cast<SetStageState>(api[63])(device, 0, 4, 2);
            reinterpret_cast<SetStageState>(api[63])(device, 0, 5, 0);
            reinterpret_cast<SetStageState>(api[63])(device, 1, 1, 1);
            reinterpret_cast<SetShader>(api[88])(device, 0);
            reinterpret_cast<SetShader>(api[76])(device, 0x44);
            draw_result = reinterpret_cast<DrawPrimitiveUp>(api[72])(
                device, 4,
                static_cast<std::uint32_t>(background_vertices.size() / 3),
                background_vertices.data(), sizeof(HudVertex));
            reinterpret_cast<ApplyStateBlock>(api[54])(device, state_block);
            reinterpret_cast<DeleteStateBlock>(api[56])(device, state_block);
        }
        end_result = reinterpret_cast<Scene>(api[35])(device);
    }
    constexpr std::uint32_t color = 0xff6d8be8u;
    const long result = reinterpret_cast<Clear>(api[36])(
        device, static_cast<std::uint32_t>(rectangles.size()),
        rectangles.data(), 1, color, 1.0f, 0);  // D3DCLEAR_TARGET
    static bool clear_logged = false;
    if (!clear_logged) {
        GapHighlightLog(
            "HUD_BACKGROUND begin=%ld draw=%ld end=%ld quads=%u; "
            "HUD_CLEAR rects=%u result=%ld",
            begin_result, draw_result, end_result,
            static_cast<unsigned>(background_rectangles.size()),
            static_cast<unsigned>(rectangles.size()), result);
        clear_logged = true;
    }
}

extern "C" BOOL THPS3AP_IsSelectedSkaterActive() {
    const std::uint32_t selected = thps3_ap::SelectedSkaterNativeChecksum();
    if (selected == 0) {
        return FALSE;
    }
    std::uint32_t current = 0;
    return ReadCurrentSkaterChecksum(current) && current == selected
        ? TRUE
        : FALSE;
}

extern "C" BOOL THPS3AP_IsCustomSkaterSelected() {
    return thps3_ap::SelectedSkaterProfileIndex() == 22 ? TRUE : FALSE;
}
