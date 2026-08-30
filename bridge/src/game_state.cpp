#include "thps3_ap/game_state.hpp"

#include <array>
#include <atomic>
#include <json/json.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::atomic_bool g_has_state = false;
std::atomic<std::uint32_t> g_unlocked_level_mask = 0;
std::atomic<std::uint32_t> g_received_stat_point_item_count = 0;
std::atomic_bool g_stat_points_are_locations = true;
std::atomic_bool g_hidden_decks_are_locations = true;
std::atomic<std::uint32_t> g_time_bonus_seconds = 0;
std::atomic<std::uint32_t> g_score_bonus_item_count = 0;
std::atomic<std::uint32_t> g_applied_score_bonus_item_count = 0;
std::atomic_bool g_score_bonus_storage_loaded = false;
std::atomic_bool g_has_stat_assignment = false;
std::atomic_bool g_stat_assignment_storage_loaded = false;
std::array<std::atomic<std::uint32_t>, 10> g_stat_assignment{};
std::atomic_bool g_appearance_storage_loaded = false;
std::mutex g_appearance_mutex;
std::string g_appearance;
std::atomic<std::uint32_t> g_state_revision = 0;
std::atomic<std::uint32_t> g_selected_skater_profile_index =
    std::numeric_limits<std::uint32_t>::max();
std::atomic<std::uint32_t> g_trick_permission_mask = 0;
std::array<std::atomic<std::uint32_t>, 9> g_checked_stat_point_masks{};
std::array<std::atomic_bool, 9> g_checked_decks{};
std::array<std::atomic<std::uint32_t>, 9> g_checked_objective_masks{};
std::array<std::atomic<std::uint32_t>, 9> g_unlocked_objective_masks{};
std::array<std::atomic<std::uint32_t>, 9> g_available_objective_location_masks{};
std::array<std::atomic<std::uint32_t>, 9> g_active_objective_counts{};
std::array<std::atomic<std::uint32_t>, 9> g_checked_item_counts{};
std::array<std::atomic<std::uint32_t>, 9> g_active_item_counts{};
std::array<std::atomic<std::uint32_t>, 9> g_checked_gap_counts{};
std::array<std::atomic<std::uint32_t>, 9> g_in_logic_gap_counts{};
std::array<std::atomic<std::uint32_t>, 9> g_active_gap_counts{};
std::mutex g_display_message_mutex;
std::deque<std::pair<std::string, bool>> g_display_messages;
std::mutex g_connection_error_mutex;
std::string g_connection_error;

constexpr std::size_t kMaxDisplayMessages = 64;
struct LevelEntry {
    std::string_view key;
    int number;
};

constexpr std::array<LevelEntry, 9> kLevels = {{
    {"foundry", 1},
    {"canada", 2},
    {"rio", 3},
    {"suburbia", 4},
    {"airport", 5},
    {"skater_island", 6},
    {"los_angeles", 7},
    {"tokyo", 8},
    {"cruise_ship", 9},
}};

// init_pro_skaters adds these profiles in master_skater_list order. 
// Keep order aligned with worlds/thps3/data.py for direct mapping 
constexpr std::array<std::string_view, 23> kSkaters = {{
    "tony_hawk",
    "steve_caballero",
    "kareem_campbell",
    "rune_glifberg",
    "eric_koston",
    "bucky_lasek",
    "bam_margera",
    "rodney_mullen",
    "chad_muska",
    "andrew_reynolds",
    "geoff_rowley",
    "elissa_steamer",
    "jamie_thomas",
    "darth_maul",
    "wolverine",
    "officer_dick",
    "private_carrera",
    "ollie_the_magic_bum",
    "kelly_slater",
    "demoness",
    "neversoft_eyeball",
    "doom_guy",
    "custom_skater",
}};
constexpr std::array<std::uint32_t, 23> kSkaterNativeChecksums = {{
    0xd32209f9u, 0x1cf2cba7u, 0xf6bf034fu, 0x2480aa16u,
    0x5571b590u, 0x72d8f3f3u, 0x0719ee3fu, 0x49d88425u,
    0xee95de1bu, 0x4d1c1b28u, 0x95c3ee11u, 0x1b661a3au,
    0xe16ce841u,
    0x48ae0eeau, 0x04d68c6cu, 0xb94930acu, 0x30e132cfu,
    0x9b65d7b8u, 0xe9aa6467u, 0xe00192c5u, 0x939470b0u,
    0x00efa334u,
    0x0a7be964u,
}};

// goal_scripts.qb defines GOAL_STAT_POINT1..5 as career flags 55..51.
// Per-level pickup storage is the second 32-bit flag word, so these become
// bits 23..19. GOAL_DECK is flag 56 (bit 24) and is mirrored separately.
constexpr std::array<std::uint32_t, 5> kStatPointCareerBits = {{
    0x00800000u,
    0x00400000u,
    0x00200000u,
    0x00100000u,
    0x00080000u,
}};
constexpr std::uint32_t kOwnedStatPointCareerBits = 0x00f80000u;
constexpr std::uint32_t kDeckCareerBit = 0x01000000u;
constexpr std::uint32_t kOwnedDeckInventoryBits = 0x000003feu;
constexpr std::uint32_t kRegularObjectiveCareerBits = 0x000001ffu;
constexpr std::uint32_t kCompetitionObjectiveCareerBits = 0x00000007u;

bool IsCompetitionLevel(int level_number) {
    return level_number == 3 || level_number == 6 || level_number == 8;
}

std::uint32_t Unsigned(const Json::Value& object, const char* key) {
    const Json::Value& value = object[key];
    return value.isUInt() ? value.asUInt() : 0;
}

Json::Value ParsePayload(std::string_view frame) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(
            frame.data(), frame.data() + frame.size(), &root, &errors)) {
        return {};
    }
    return root.isMember("payload") ? root["payload"] : root;
}

int LevelNumberForKey(std::string_view key) {
    for (const LevelEntry& level : kLevels) {
        if (level.key == key) {
            return level.number;
        }
    }
    return 0;
}

std::uint32_t SkaterProfileIndexForKey(std::string_view key) {
    for (std::size_t index = 0; index < kSkaters.size(); ++index) {
        if (kSkaters[index] == key) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

void ParseCareerChecks(const Json::Value& payload) {
    std::array<std::uint32_t, kLevels.size()> stat_point_masks{};
    std::array<bool, kLevels.size()> checked_decks{};
    std::array<std::uint32_t, kLevels.size()> objective_masks{};
    std::array<std::uint32_t, kLevels.size()> unlocked_objective_masks{};
    std::array<std::uint32_t, kLevels.size()> available_objective_location_masks{};
    std::array<std::uint32_t, kLevels.size()> active_objective_counts{};
    std::array<std::uint32_t, kLevels.size()> checked_item_counts{};
    std::array<std::uint32_t, kLevels.size()> active_item_counts{};

    if (payload["career_checks"].isArray()) {
        for (const Json::Value& entry : payload["career_checks"]) {
            const int level_number = entry["level"].isString()
                ? LevelNumberForKey(entry["level"].asString())
                : 0;
            if (level_number == 0) {
                continue;
            }
            const std::size_t index =
                static_cast<std::size_t>(level_number - 1);
            const std::uint32_t owned_objectives =
                IsCompetitionLevel(level_number)
                    ? kCompetitionObjectiveCareerBits
                    : kRegularObjectiveCareerBits;
            stat_point_masks[index] = Unsigned(entry, "stat_points") & 0x1fu;
            objective_masks[index] =
                Unsigned(entry, "objectives") & owned_objectives;
            unlocked_objective_masks[index] =
                Unsigned(entry, "objective_access") & owned_objectives;
            available_objective_location_masks[index] =
                Unsigned(entry, "objective_location_access") & owned_objectives;
            active_objective_counts[index] = Unsigned(entry, "objective_total");
            checked_item_counts[index] = Unsigned(entry, "items_checked");
            active_item_counts[index] = Unsigned(entry, "item_total");
            checked_decks[index] = Unsigned(entry, "deck") != 0;
        }
    }

    for (std::size_t index = 0; index < kLevels.size(); ++index) {
        g_checked_stat_point_masks[index].store(
            stat_point_masks[index], std::memory_order_release);
        g_checked_decks[index].store(
            checked_decks[index], std::memory_order_release);
        g_checked_objective_masks[index].store(
            objective_masks[index], std::memory_order_release);
        g_unlocked_objective_masks[index].store(
            unlocked_objective_masks[index], std::memory_order_release);
        g_available_objective_location_masks[index].store(
            available_objective_location_masks[index], std::memory_order_release);
        g_active_objective_counts[index].store(
            active_objective_counts[index], std::memory_order_release);
        g_checked_item_counts[index].store(
            checked_item_counts[index], std::memory_order_release);
        g_active_item_counts[index].store(
            active_item_counts[index], std::memory_order_release);
    }
}

void ParseGapProgress(const Json::Value& payload) {
    std::array<std::uint32_t, kLevels.size()> checked_counts{};
    std::array<std::uint32_t, kLevels.size()> in_logic_counts{};
    std::array<std::uint32_t, kLevels.size()> total_counts{};

    if (payload["gap_progress"].isArray()) {
        for (const Json::Value& entry : payload["gap_progress"]) {
            const int level_number = entry["level"].isString()
                ? LevelNumberForKey(entry["level"].asString())
                : 0;
            if (level_number == 0) {
                continue;
            }
            const std::size_t index =
                static_cast<std::size_t>(level_number - 1);
            checked_counts[index] = Unsigned(entry, "checked");
            in_logic_counts[index] = Unsigned(entry, "in_logic");
            total_counts[index] = Unsigned(entry, "total");
        }
    }

    for (std::size_t index = 0; index < kLevels.size(); ++index) {
        g_checked_gap_counts[index].store(
            checked_counts[index], std::memory_order_release);
        g_in_logic_gap_counts[index].store(
            in_logic_counts[index], std::memory_order_release);
        g_active_gap_counts[index].store(
            total_counts[index], std::memory_order_release);
    }
}

void ParseStatAssignment(const Json::Value& payload) {
    const bool storage_loaded =
        payload["stat_assignment_loaded"].isBool() &&
        payload["stat_assignment_loaded"].asBool();
    g_stat_assignment_storage_loaded.store(
        storage_loaded, std::memory_order_release);
    g_has_stat_assignment.store(false, std::memory_order_release);

    const Json::Value& input = payload["stat_assignment"];
    bool valid = input.isArray() && input.size() == g_stat_assignment.size();
    for (Json::ArrayIndex index = 0; valid && index < input.size(); ++index) {
        valid = input[index].isUInt() &&
            input[index].asUInt() <= (index == 0 ? 45u : 10u);
    }
    if (valid) {
        for (Json::ArrayIndex index = 0; index < input.size(); ++index) {
            g_stat_assignment[index].store(
                input[index].asUInt(), std::memory_order_release);
        }
    }
    g_has_stat_assignment.store(valid, std::memory_order_release);
}
}  // namespace

namespace thps3_ap {

void QueueDisplayMessage(std::string message, bool more) {
    if (message.empty()) {
        return;
    }
    const std::lock_guard lock(g_display_message_mutex);
    if (g_display_messages.size() == kMaxDisplayMessages) {
        g_display_messages.pop_front();
    }
    g_display_messages.emplace_back(std::move(message), more);
}

bool TakeDisplayMessage(std::string& message, bool& more) {
    const std::lock_guard lock(g_display_message_mutex);
    if (g_display_messages.empty()) {
        return false;
    }
    message = std::move(g_display_messages.front().first);
    more = g_display_messages.front().second;
    g_display_messages.pop_front();
    return true;
}

void SetConnectionError(std::string message) {
    const std::lock_guard lock(g_connection_error_mutex);
    g_connection_error = std::move(message);
}

std::string ConnectionError() {
    const std::lock_guard lock(g_connection_error_mutex);
    return g_connection_error;
}

void ApplyStateSnapshot(std::string_view frame) {
    const Json::Value payload = ParsePayload(frame);
    if (!payload.isObject()) {
        return;
    }

    std::uint32_t mask = 0;
    if (payload["unlocked_levels"].isArray()) {
        for (const Json::Value& key : payload["unlocked_levels"]) {
            const int level_number =
                key.isString() ? LevelNumberForKey(key.asString()) : 0;
            if (level_number != 0) {
                mask |= 1u << (level_number - 1);
            }
        }
    }

    ParseCareerChecks(payload);
    ParseGapProgress(payload);
    ParseStatAssignment(payload);

    const bool appearance_loaded =
        payload["appearance_loaded"].isBool() &&
        payload["appearance_loaded"].asBool();
    {
        const std::lock_guard lock(g_appearance_mutex);
        g_appearance = appearance_loaded && payload["appearance"].isString()
            ? payload["appearance"].asString()
            : std::string{};
    }
    g_appearance_storage_loaded.store(
        appearance_loaded, std::memory_order_release);
    g_selected_skater_profile_index.store(
        payload["selected_skater"].isString()
            ? SkaterProfileIndexForKey(payload["selected_skater"].asString())
            : std::numeric_limits<std::uint32_t>::max(),
        std::memory_order_release);

    constexpr std::array<const char*, 7> permissions = {
        "flip", "grab", "grind", "revert", "manual", "lip", "special",
    };
    std::uint32_t trick_permission_mask = 0;
    for (std::size_t index = 0; index < permissions.size(); ++index) {
        if (payload["trick_permissions"][permissions[index]].isBool() &&
            payload["trick_permissions"][permissions[index]].asBool()) {
            trick_permission_mask |= 1u << index;
        }
    }
    g_trick_permission_mask.store(
        trick_permission_mask, std::memory_order_release);

    const std::uint32_t stat_point_items =
        (std::min)(Unsigned(payload, "stat_point_items"), 45u);
    g_received_stat_point_item_count.store(
        stat_point_items, std::memory_order_release);
    g_stat_points_are_locations.store(
        payload["stat_points_are_locations"].asBool(),
        std::memory_order_release);
    g_hidden_decks_are_locations.store(
        payload["hidden_decks_are_locations"].asBool(),
        std::memory_order_release);
    g_time_bonus_seconds.store(
        Unsigned(payload, "time_bonus_seconds"), std::memory_order_release);
    g_score_bonus_item_count.store(
        Unsigned(payload, "score_bonus_items"), std::memory_order_release);

    const bool score_bonus_storage_loaded =
        payload["score_bonus_storage_loaded"].isBool() &&
        payload["score_bonus_storage_loaded"].asBool();
    g_score_bonus_storage_loaded.store(
        score_bonus_storage_loaded, std::memory_order_release);
    if (score_bonus_storage_loaded) {
        const std::uint32_t stored =
            Unsigned(payload, "score_bonus_items_applied");
        std::uint32_t applied =
            g_applied_score_bonus_item_count.load(std::memory_order_acquire);
        while (stored > applied &&
               !g_applied_score_bonus_item_count.compare_exchange_weak(
                   applied, stored, std::memory_order_acq_rel)) {}
    }

    g_unlocked_level_mask.store(mask, std::memory_order_release);
    g_has_state.store(true, std::memory_order_release);
    g_state_revision.fetch_add(1, std::memory_order_acq_rel);
}
bool HasStateSnapshot() {
    return g_has_state.load(std::memory_order_acquire);
}

bool StartupConnectionTimedOut(
    bool main_menu_loaded,
    std::uint64_t elapsed_milliseconds) {
    return !HasStateSnapshot() && main_menu_loaded &&
        elapsed_milliseconds >= 20'000;
}

std::uint32_t UnlockedLevelMask() {
    return g_unlocked_level_mask.load(std::memory_order_acquire);
}

std::uint32_t ReceivedStatPointItemCount() {
    return g_received_stat_point_item_count.load(std::memory_order_acquire);
}

bool StatPointsAreLocations() {
    return g_stat_points_are_locations.load(std::memory_order_acquire);
}

bool HiddenDecksAreLocations() {
    return g_hidden_decks_are_locations.load(std::memory_order_acquire);
}

std::uint32_t TimeBonusSeconds() {
    return g_time_bonus_seconds.load(std::memory_order_acquire);
}

std::uint32_t ReceivedScoreBonusItemCount() {
    return g_score_bonus_item_count.load(std::memory_order_acquire);
}

std::uint32_t TakePendingScoreBonusItems(bool active_combo) {
    if (!active_combo ||
        !g_score_bonus_storage_loaded.load(std::memory_order_acquire)) {
        return 0;
    }
    const std::uint32_t desired =
        g_score_bonus_item_count.load(std::memory_order_acquire);
    const std::uint32_t applied =
        g_applied_score_bonus_item_count.load(std::memory_order_acquire);
    if (desired <= applied) {
        return 0;
    }
    g_applied_score_bonus_item_count.store(desired, std::memory_order_release);
    return desired - applied;
}

bool GetStatAssignment(std::array<std::uint32_t, 10>& assignment) {
    if (!g_has_stat_assignment.load(std::memory_order_acquire)) {
        return false;
    }
    for (std::size_t index = 0; index < assignment.size(); ++index) {
        assignment[index] =
            g_stat_assignment[index].load(std::memory_order_acquire);
    }
    return true;
}

bool StatAssignmentStorageLoaded() {
    return g_stat_assignment_storage_loaded.load(std::memory_order_acquire);
}

bool AppearanceStorageLoaded() {
    return g_appearance_storage_loaded.load(std::memory_order_acquire);
}

bool GetAppearance(std::string& appearance) {
    if (!AppearanceStorageLoaded()) {
        return false;
    }
    const std::lock_guard lock(g_appearance_mutex);
    appearance = g_appearance;
    return !appearance.empty();
}

std::uint32_t StateRevision() {
    return g_state_revision.load(std::memory_order_acquire);
}

std::uint32_t SelectedSkaterProfileIndex() {
    return g_selected_skater_profile_index.load(std::memory_order_acquire);
}

std::string_view SelectedSkaterKey() {
    const std::uint32_t index = SelectedSkaterProfileIndex();
    return index < kSkaters.size() ? kSkaters[index] : std::string_view{};
}

std::uint32_t SelectedSkaterNativeChecksum() {
    const std::uint32_t index = SelectedSkaterProfileIndex();
    return index < kSkaterNativeChecksums.size()
        ? kSkaterNativeChecksums[index]
        : 0;
}

std::uint32_t TrickPermissionMask() {
    return g_trick_permission_mask.load(std::memory_order_acquire);
}

bool IsTrickCategoryUnlocked(int category) {
    if (!HasStateSnapshot()) {
        return true;
    }
    return category >= 0 && category < 7 &&
        (TrickPermissionMask() & (1u << category)) != 0;
}

bool IsLevelUnlocked(int level_number) {
    if (!HasStateSnapshot()) {
        return true;
    }
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return true;
    }
    const std::uint32_t mask = UnlockedLevelMask();
    return (mask & (1u << (level_number - 1))) != 0;
}

std::uint32_t CheckedStatPointMask(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_checked_stat_point_masks[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

bool IsDeckChecked(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return false;
    }
    return g_checked_decks[static_cast<std::size_t>(level_number - 1)].load(
        std::memory_order_acquire);
}

std::uint32_t CheckedDeckMask() {
    std::uint32_t mask = 0;
    for (int level_number = 1;
         level_number <= static_cast<int>(kLevels.size());
        ++level_number) {
        if (IsDeckChecked(level_number)) {
            mask |= 1u << level_number;
        }
    }
    return mask;
}

std::uint32_t CheckedObjectiveMask(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_checked_objective_masks[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t UnlockedObjectiveMask(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_unlocked_objective_masks[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t AvailableObjectiveLocationMask(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_available_objective_location_masks[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t ActiveObjectiveCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_active_objective_counts[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t CheckedItemCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_checked_item_counts[static_cast<std::size_t>(level_number - 1)]
        .load(std::memory_order_acquire);
}

std::uint32_t ActiveItemCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_active_item_counts[static_cast<std::size_t>(level_number - 1)]
        .load(std::memory_order_acquire);
}

std::uint32_t CheckedGapCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_checked_gap_counts[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t InLogicGapCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_in_logic_gap_counts[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

std::uint32_t ActiveGapCount(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return g_active_gap_counts[
        static_cast<std::size_t>(level_number - 1)].load(
            std::memory_order_acquire);
}

bool IsObjectiveUnlocked(int level_number, int goal_id) {
    if (!HasStateSnapshot()) {
        return true;
    }
    const std::uint32_t owned_bits = OwnedObjectiveCareerBits(level_number);
    if (goal_id < 0 || goal_id >= 32 ||
        (owned_bits & (1u << goal_id)) == 0) {
        return true;
    }
    const std::uint32_t accessible =
        UnlockedObjectiveMask(level_number) |
        CheckedObjectiveMask(level_number);
    return (accessible & (1u << goal_id)) != 0;
}

int ObjectiveListPosition(int level_number, int goal_id) {
    constexpr std::array<int, 9> kDisplayOrder{{0, 1, 2, 3, 5, 4, 8, 6, 7}};
    int position = 0;
    for (const int displayed_goal : kDisplayOrder) {
        if (displayed_goal == goal_id) {
            return IsObjectiveUnlocked(level_number, goal_id) ? position : -1;
        }
        position += IsObjectiveUnlocked(level_number, displayed_goal) ? 1 : 0;
    }
    return -1;
}

std::uint32_t OwnedObjectiveCareerBits(int level_number) {
    if (level_number < 1 || level_number > static_cast<int>(kLevels.size())) {
        return 0;
    }
    return IsCompetitionLevel(level_number)
        ? kCompetitionObjectiveCareerBits
        : kRegularObjectiveCareerBits;
}

std::uint32_t ReplaceObjectiveCareerBits(
    std::uint32_t existing_career_bits,
    std::uint32_t objective_mask,
    int level_number) {
    const std::uint32_t owned_bits = OwnedObjectiveCareerBits(level_number);
    return (existing_career_bits & ~owned_bits) |
        (objective_mask & owned_bits);
}

std::uint32_t StatPointMaskToCareerBits(std::uint32_t stat_point_mask) {
    std::uint32_t career_bits = 0;
    for (std::size_t index = 0; index < kStatPointCareerBits.size(); ++index) {
        if ((stat_point_mask & (1u << index)) != 0) {
            career_bits |= kStatPointCareerBits[index];
        }
    }
    return career_bits;
}

std::uint32_t ReplaceStatPointCareerBits(
    std::uint32_t existing_career_bits,
    std::uint32_t stat_point_mask) {
    return (existing_career_bits & ~kOwnedStatPointCareerBits) |
        StatPointMaskToCareerBits(stat_point_mask);
}

std::uint32_t ReplaceDeckCareerBit(
    std::uint32_t existing_career_bits,
    bool checked) {
    return checked
        ? existing_career_bits | kDeckCareerBit
        : existing_career_bits & ~kDeckCareerBit;
}

std::uint32_t ReplaceDeckInventoryBits(
    std::uint32_t existing_inventory_bits,
    std::uint32_t checked_deck_mask) {
    return (existing_inventory_bits & ~kOwnedDeckInventoryBits) |
        (checked_deck_mask & kOwnedDeckInventoryBits);
}

}  // namespace thps3_ap
