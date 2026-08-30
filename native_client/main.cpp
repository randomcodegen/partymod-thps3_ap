#include "Archipelago.h"
#include "thps3_ap/bridge_runtime.hpp"
#include <json/json.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <thread>
#include <vector>

namespace {

constexpr char kGame[] = "Tony Hawk's Pro Skater 3";
constexpr wchar_t kDefaultPipe[] = LR"(\\.\pipe\thps3_archipelago)";
constexpr std::size_t kMaxFrame = 1024 * 1024;
std::atomic_bool g_debug{false};

void Debug(std::string_view message) {
    if (g_debug) std::osyncstream(std::cerr) << "[debug] " << message << '\n';
}

Json::StreamWriterBuilder CompactWriter() {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return builder;
}

Json::Value ParseJson(const std::string& text) {
    Json::CharReaderBuilder builder;
    Json::Value value;
    std::string error;
    std::istringstream input(text);
    if (!Json::parseFromStream(builder, input, &value, &error)) {
        throw std::runtime_error("invalid JSON: " + error);
    }
    return value;
}

std::string Frame(const char* type, std::uint64_t sequence, Json::Value payload) {
    Json::Value root;
    root["protocol_version"] = 1;
    root["type"] = type;
    root["sequence"] = Json::UInt64(sequence);
    root["payload"] = std::move(payload);
    std::string frame = Json::writeString(CompactWriter(), root) + '\n';
    if (g_debug) std::osyncstream(std::cerr) << "[debug] pipe -> " << type << " #" << sequence << '\n';
    return frame;
}

struct State {
    std::mutex mutex;
    Json::Value slot_data{Json::objectValue};
    std::vector<std::string> received_items;
    std::set<std::int64_t> checked_locations;
    std::set<std::int64_t> pending_locations;
    Json::Value stat_assignment;
    bool collectible_markers_enabled = false;
    std::atomic_bool dirty{true};
    bool storage_requested = false;
    bool stat_assignment_loaded = false;
    bool goal_sent = false;
    std::string storage_key;
    std::string storage_value;
    AP_GetServerDataRequest storage_request{};
    Json::Value appearance;
    Json::Value pending_appearance;
    bool appearance_loaded = false;
    std::string appearance_storage_key;
    std::string appearance_storage_value;
    AP_GetServerDataRequest appearance_storage_request{};
    std::string marker_storage_key;
    std::string marker_storage_value;
    AP_GetServerDataRequest marker_storage_request{};
    std::uint32_t score_bonus_items_applied = 0;
    bool score_bonus_storage_loaded = false;
    std::string score_bonus_storage_key;
    std::string score_bonus_storage_value;
    AP_GetServerDataRequest score_bonus_storage_request{};
    Json::Value medal_results{Json::objectValue};
    Json::Value pending_medal_results{Json::objectValue};
    bool medal_storage_loaded = false;
    std::string medal_storage_key;
    std::string medal_storage_value;
    AP_GetServerDataRequest medal_storage_request{};
};

State g_state;
std::atomic_bool g_running{true};
std::atomic_bool g_medal_replay_requested{false};
std::atomic_bool g_medal_store_requested{false};

void SetSlotData(const char* key, const std::string& raw) {
    try {
        Json::Value value = ParseJson(raw);
        std::lock_guard lock(g_state.mutex);
        g_state.slot_data[key] = std::move(value);
        if (std::string_view(key) == "collectible_markers_default" &&
            g_state.slot_data[key].isBool()) {
            g_state.collectible_markers_enabled =
                g_state.slot_data[key].asBool();
        }
        g_state.dirty = true;
        Debug(std::string("slot data: ") + key);
    } catch (const std::exception& error) {
        std::cerr << "Rejected slot data " << key << ": " << error.what() << '\n';
    }
}

void ClearItems() {
    std::lock_guard lock(g_state.mutex);
    g_state.received_items.clear();
    g_state.checked_locations.clear();
    g_state.checked_locations.insert(
        g_state.pending_locations.begin(), g_state.pending_locations.end());
    g_state.storage_requested = false;
    g_state.stat_assignment_loaded = false;
    g_state.appearance_loaded = false;
    g_state.score_bonus_storage_loaded = false;
    g_state.medal_storage_loaded = false;
    g_state.medal_results = Json::Value{Json::objectValue};
    g_medal_store_requested = false;
    g_state.pending_appearance = Json::Value{};
    g_state.goal_sent = false;
    g_state.dirty = true;
    Debug("APCpp state cleared for synchronization");
}

void ReceiveItem(std::int64_t item, int, bool) {
    const std::string name = AP_GetItemName(item);
    {
        std::lock_guard lock(g_state.mutex);
        g_state.received_items.push_back(name);
        g_state.dirty = true;
    }
    g_medal_replay_requested = true;
    Debug("item received: " + name);
}

void CheckLocation(std::int64_t location) {
    std::lock_guard lock(g_state.mutex);
    g_state.checked_locations.insert(location);
    g_state.pending_locations.erase(location);
    g_state.dirty = true;
    Debug("location checked: " + std::to_string(location));
}

bool QueueLocation(std::int64_t location) {
    std::lock_guard lock(g_state.mutex);
    if (g_state.checked_locations.contains(location)) return false;
    g_state.checked_locations.insert(location);
    g_state.pending_locations.insert(location);
    g_state.dirty = true;
    Debug("location queued: " + std::to_string(location));
    return true;
}

void SendPendingLocations() {
    std::set<std::int64_t> pending;
    {
        std::lock_guard lock(g_state.mutex);
        pending = g_state.pending_locations;
    }
    if (pending.empty()) return;
    Debug("sending " + std::to_string(pending.size()) + " pending location(s)");
    AP_SendItem(pending);
}

std::map<std::string, int> ItemCounts(const std::vector<std::string>& items) {
    std::map<std::string, int> counts;
    for (const auto& item : items) ++counts[item];
    return counts;
}

bool ValidMedalResults(const Json::Value& results) {
    if (!results.isObject()) return false;
    for (const auto& level : results.getMemberNames())
        if (!results[level].isInt() || results[level].asInt() < 0 ||
            results[level].asInt() > 2) return false;
    return true;
}

std::set<std::int64_t> EarnedAccessibleMedalLocations(
    const Json::Value& slot,
    const std::vector<std::string>& received_items,
    const std::set<std::int64_t>& checked,
    const Json::Value& results) {
    const auto counts = ItemCounts(received_items);
    std::set<std::int64_t> locations;
    constexpr std::array<std::string_view, 3> names{
        "Bronze Medal", "Silver Medal", "Gold Medal"};
    for (const auto& goal : slot["goal_locations"]) {
        const auto level = goal["level"].asString();
        const int id = goal["goal_id"].asInt();
        if (!results.isMember(level) || id < 0 || id >= 3 ||
            goal["name"].asString() != names[static_cast<std::size_t>(id)] ||
            id > results[level].asInt() || goal["precollected"].asBool() ||
            checked.contains(goal["location_id"].asInt64())) continue;
        const auto item = goal["item_name"].asString();
        if (counts.contains(item) && counts.at(item) > 0)
            locations.insert(goal["location_id"].asInt64());
    }
    return locations;
}

void ReplayEarnedMedalsIfRequested() {
    if (!g_medal_replay_requested.exchange(false)) return;
    Json::Value slot, results;
    std::vector<std::string> items;
    std::set<std::int64_t> checked;
    {
        std::lock_guard lock(g_state.mutex);
        if (!g_state.medal_storage_loaded) return;
        slot = g_state.slot_data;
        results = g_state.medal_results;
        items = g_state.received_items;
        checked = g_state.checked_locations;
    }
    std::set<std::int64_t> queued;
    for (const auto location :
            EarnedAccessibleMedalLocations(slot, items, checked, results))
        if (QueueLocation(location)) queued.insert(location);
    if (!queued.empty()) AP_SendItem(queued);
}

bool GapInLogic(const Json::Value& gap, const std::map<std::string, int>& items) {
    bool has_any = gap["any_items"].empty();
    for (const auto& item : gap["any_items"])
        has_any |= items.contains(item.asString());
    if (!has_any) return false;
    for (const auto& item : gap["all_items"])
        if (!items.contains(item.asString())) return false;
    for (const auto& item : gap["goal_items"])
        if (!items.contains(item.asString())) return false;
    return true;
}

void Require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

Json::Value BuildSnapshot(
    const Json::Value& slot,
    const std::vector<std::string>& received_items,
    const std::set<std::int64_t>& checked,
    const Json::Value& stat_assignment,
    const std::string& slot_name,
    const std::string& seed_name,
    bool collectible_markers_enabled,
    const Json::Value& appearance = Json::Value{},
    bool appearance_loaded = false,
    bool stat_assignment_loaded = false,
    std::uint32_t score_bonus_items_applied = 0,
    bool score_bonus_storage_loaded = false) {
    Require(slot["protocol_version"].asInt() == 1, "slot protocol version must be 1");
    Require(slot["selected_skater"].isObject(), "selected_skater is missing");
    Require(slot["levels"].isObject(), "levels is missing");
    Require(slot["goal_locations"].isArray(), "goal_locations is missing");
    Require(slot["goal_unlocks"].isArray(), "goal_unlocks is missing");
    Require(slot["active_gaps"].isArray(), "active_gaps is missing");
    Require(slot["stat_points_are_locations"].isBool(),
        "stat_points_are_locations is missing");
    Require(slot["stat_point_locations"].isArray(), "stat_point_locations is missing");
    Require(slot["hidden_decks_are_locations"].isBool(),
        "hidden_decks_are_locations is missing");
    Require(slot["deck_locations"].isArray(), "deck_locations is missing");

    const auto counts = ItemCounts(received_items);
    Json::Value payload;
    payload["seed_name"] = seed_name;
    payload["slot_name"] = slot_name;
    payload["selected_skater"] = slot["selected_skater"]["key"];
    payload["stat_point_items"] = (std::min)(45, counts.contains("Stat Point") ? counts.at("Stat Point") : 0);
    payload["stat_points_are_locations"] = slot["stat_points_are_locations"];
    payload["hidden_decks_are_locations"] = slot["hidden_decks_are_locations"];
    payload["time_bonus_seconds"] = counts.contains("+1 Second") ? counts.at("+1 Second") : 0;
    payload["score_bonus_items"] = counts.contains("Score Bonus") ? counts.at("Score Bonus") : 0;
    payload["score_bonus_items_applied"] = score_bonus_items_applied;
    payload["score_bonus_storage_loaded"] = score_bonus_storage_loaded;
    payload["stat_assignment"] = stat_assignment;
    payload["stat_assignment_loaded"] = stat_assignment_loaded;
    payload["collectible_markers_enabled"] = collectible_markers_enabled;
    payload["appearance"] = appearance;
    payload["appearance_loaded"] = appearance_loaded;

    Json::Value unlocked(Json::arrayValue);
    for (const auto& level : slot["levels"].getMemberNames()) {
        const std::string access = slot["levels"][level]["name"].asString() + " Access";
        if (counts.contains(access) && counts.at(access) > 0) unlocked.append(level);
    }
    payload["unlocked_levels"] = std::move(unlocked);

    const std::pair<const char*, const char*> tricks[] = {
        {"Flip Tricks", "flip"}, {"Grab Tricks", "grab"},
        {"Grind Tricks", "grind"}, {"Manual Tricks", "manual"},
        {"Lip Tricks", "lip"}, {"Reverts", "revert"},
        {"Special Tricks", "special"},
    };
    for (const auto& [item, key] : tricks)
        payload["trick_permissions"][key] = counts.contains(item) && counts.at(item) > 0;

    std::map<std::string, std::uint32_t> objectives, objective_access,
        objective_location_access, objective_totals, stat_points,
        items_checked, item_totals;
    std::map<std::string, bool> decks;
    struct GapProgress { int checked = 0, in_logic = 0, total = 0; };
    std::map<std::string, GapProgress> gap_progress;
    for (const auto& level : slot["levels"].getMemberNames()) {
        objectives[level] = objective_access[level] =
            objective_location_access[level] = objective_totals[level] =
                stat_points[level] = items_checked[level] =
                item_totals[level] = 0;
        decks[level] = false;
        gap_progress[level] = {};
    }

    for (const auto& goal : slot["goal_locations"]) {
        const auto level = goal["level"].asString();
        const int id = goal["goal_id"].asInt();
        Require(id >= 0 && id < 32, "goal_id is outside the native mask");
        if (goal["precollected"].asBool() ||
            checked.contains(goal["location_id"].asInt64()))
            objectives[level] |= 1u << id;
        if (!goal["precollected"].asBool()) ++objective_totals[level];
        const auto item = goal["item_name"].asString();
        if (counts.contains(item) && counts.at(item) > 0)
            objective_location_access[level] |= 1u << id;
    }
    for (const auto& goal : slot["goal_unlocks"]) {
        const auto level = goal["level"].asString();
        const int id = goal["goal_id"].asInt();
        Require(id >= 0 && id < 32, "goal_id is outside the native mask");
        const auto item = goal["item_name"].asString();
        if (counts.contains(item) && counts.at(item) > 0)
            objective_access[level] |= 1u << id;
    }
    for (const auto& point : slot["stat_point_locations"]) {
        const int id = point["point_id"].asInt();
        Require(id >= 1 && id <= 5, "point_id must be between 1 and 5");
        const auto level = point["level"].asString();
        const bool precollected = point["precollected"].asBool();
        const bool collected = checked.contains(point["location_id"].asInt64());
        if (precollected || collected) stat_points[level] |= 1u << (id - 1);
        if (!precollected) {
            ++item_totals[level];
            if (collected) ++items_checked[level];
        }
    }
    for (const auto& deck : slot["deck_locations"]) {
        const auto level = deck["level"].asString();
        const bool precollected = deck["precollected"].asBool();
        const bool collected = checked.contains(deck["location_id"].asInt64());
        decks[level] = precollected || collected;
        if (!precollected) {
            ++item_totals[level];
            if (collected) ++items_checked[level];
        }
    }

    Json::Value active_gaps(Json::arrayValue);
    for (const auto& gap : slot["active_gaps"]) {
        Json::Value active;
        active["level"] = gap["level"];
        active["checksum"] = gap["checksum"];
        active_gaps.append(std::move(active));
        auto& progress = gap_progress[gap["level"].asString()];
        const bool is_checked = checked.contains(gap["location_id"].asInt64());
        ++progress.total;
        if (is_checked) ++progress.checked;
        if (is_checked || GapInLogic(gap, counts)) ++progress.in_logic;
    }
    payload["active_gaps"] = std::move(active_gaps);

    Json::Value progress(Json::arrayValue), career(Json::arrayValue);
    for (const auto& [level, value] : gap_progress) {
        Json::Value row;
        row["level"] = level;
        row["checked"] = value.checked;
        row["in_logic"] = value.in_logic;
        row["total"] = value.total;
        progress.append(std::move(row));

        Json::Value checks;
        checks["level"] = level;
        checks["objectives"] = objectives[level];
        checks["objective_access"] = objective_access[level];
        checks["objective_location_access"] = objective_location_access[level];
        checks["objective_total"] = objective_totals[level];
        checks["stat_points"] = stat_points[level];
        checks["deck"] = decks[level] ? 1 : 0;
        checks["items_checked"] = items_checked[level];
        checks["item_total"] = item_totals[level];
        career.append(std::move(checks));
    }
    payload["gap_progress"] = std::move(progress);
    payload["career_checks"] = std::move(career);

    Json::Value locations(Json::arrayValue);
    for (auto location : checked) locations.append(Json::Int64(location));
    payload["checked_locations"] = std::move(locations);
    return payload;
}

std::int64_t LocationForEvent(const Json::Value& slot, const Json::Value& event) {
    const auto kind = event["kind"].asString();
    const auto level = event["level"].asString();
    const char* list = kind == "goal" ? "goal_locations" :
        kind == "gap" ? "active_gaps" :
        kind == "stat_point" ? "stat_point_locations" :
        kind == "deck" ? "deck_locations" : "";
    if (!*list) return 0;
    for (const auto& entry : slot[list]) {
        if (entry["precollected"].asBool()) continue;
        if (entry["level"].asString() != level) continue;
        if (kind == "goal" && entry["goal_id"] != event["goal_id"]) continue;
        if (kind == "gap" && entry["checksum"] != event["checksum"]) continue;
        if (kind == "stat_point" && entry["point_id"] != event["point_id"]) continue;
        return entry["location_id"].asInt64();
    }
    return 0;
}

bool IsComplete(const Json::Value& slot, const std::set<std::int64_t>& checked) {
    const auto goal = slot["completion_goal"];
    const auto mode = goal["mode"].asString();
    const int required = goal["required"].asInt();
    const auto is_checked = [&](const Json::Value& entry) {
        return !entry["precollected"].asBool() &&
            checked.contains(entry["location_id"].asInt64());
    };
    const auto count_checked = [&](const Json::Value& entries) {
        int count = 0;
        for (const auto& entry : entries) count += is_checked(entry);
        return count;
    };
    const auto count_goals = [&](const char* field, const std::string& value) {
        int count = 0;
        for (const auto& entry : slot["goal_locations"])
            count += entry[field].asString() == value && is_checked(entry);
        return count;
    };
    if (mode == "objectives") return count_checked(slot["goal_locations"]) >= required;
    if (mode == "cruise_ship") return count_goals("level", "cruise_ship") >= required;
    if (mode == "gold_medals") return count_goals("name", "Gold Medal") >= required;
    if (mode == "goal_type") return count_goals("name", goal["type"].asString()) >= required;
    if (mode == "gap_hunt") return count_checked(slot["active_gaps"]) >= required;
    if (mode == "collectibles")
        return count_checked(slot["stat_point_locations"]) +
            count_checked(slot["deck_locations"]) >= required;
    if (mode == "total_checks")
        return count_checked(slot["goal_locations"]) + count_checked(slot["active_gaps"]) +
            count_checked(slot["stat_point_locations"]) +
            count_checked(slot["deck_locations"]) >= required;
    if (mode != "levels" && mode != "level_tour") return false;

    std::map<std::string, std::pair<int, int>> levels;
    for (const auto& entry : slot["goal_locations"]) {
        auto& [done, total] = levels[entry["level"].asString()];
        done += is_checked(entry);
        ++total;
    }
    int count = 0;
    for (const auto& [_, progress] : levels)
        count += progress.first >=
            (mode == "levels" ? progress.second : std::min(required, progress.second));
    return count >= (mode == "levels" ? required : static_cast<int>(levels.size()));
}

void SendGoalIfComplete() {
    bool send = false;
    {
        std::lock_guard lock(g_state.mutex);
        send = !g_state.goal_sent && g_state.slot_data["completion_goal"].isObject() &&
            IsComplete(g_state.slot_data, g_state.checked_locations);
        if (send) g_state.goal_sent = true;
    }
    if (send) AP_StoryComplete();
    if (send) Debug("completion requirement met; sent StatusUpdate");
}

bool ValidAssignment(const Json::Value& value) {
    if (!value.isArray() || value.size() != 10) return false;
    for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
        if (!value[i].isInt() || value[i].asInt() < 0 ||
            value[i].asInt() > (i == 0 ? 45 : 10)) return false;
    }
    return true;
}

class Pipe {
public:
    explicit Pipe(std::wstring path) : path_(std::move(path)) {}
    ~Pipe() { Close(); }

    bool Connect() {
        if (!WaitNamedPipeW(path_.c_str(), 1000)) return false;
        handle_ = CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE) Debug("connected to THPS3 named pipe");
        return handle_ != INVALID_HANDLE_VALUE;
    }

    void Close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            Debug("disconnected from THPS3 named pipe");
        }
        handle_ = INVALID_HANDLE_VALUE;
        pending_.clear();
    }

    bool Write(const std::string& text) {
        std::string_view remaining(text);
        while (!remaining.empty()) {
            DWORD written = 0;
            if (!WriteFile(handle_, remaining.data(), static_cast<DWORD>(remaining.size()), &written, nullptr) || !written)
                return false;
            remaining.remove_prefix(written);
        }
        return true;
    }

    bool ReadFrames(std::vector<std::string>& frames) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available) {
            std::string chunk((std::min)(available, DWORD{65536}), '\0');
            DWORD read = 0;
            if (!ReadFile(handle_, chunk.data(), static_cast<DWORD>(chunk.size()), &read, nullptr)) return false;
            pending_.append(chunk.data(), read);
            if (pending_.size() > kMaxFrame) return false;
        }
        for (std::size_t newline; (newline = pending_.find('\n')) != std::string::npos;) {
            frames.push_back(pending_.substr(0, newline));
            pending_.erase(0, newline + 1);
        }
        return true;
    }

private:
    std::wstring path_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::string pending_;
};

void StoreAssignment(const Json::Value& assignment) {
    std::string value = Json::writeString(CompactWriter(), assignment);
    std::string default_value = "null";
    AP_DataStorageOperation operation{"replace", &value};
    AP_SetServerDataRequest request;
    {
        std::lock_guard lock(g_state.mutex);
        if (g_state.storage_key.empty() || !g_state.stat_assignment_loaded) {
            Debug("ignored stat assignment until slot storage loaded");
            return;
        }
        request.key = g_state.storage_key;
    }
    request.operations.push_back(operation);
    request.default_value = &default_value;
    request.type = AP_DataType::Raw;
    request.want_reply = false;
    AP_SetServerData(&request);
    std::lock_guard lock(g_state.mutex);
    g_state.stat_assignment = assignment;
    g_state.dirty = true;
    Debug("stored stat assignment");
}

void StoreCollectibleMarkersEnabled(bool enabled) {
    std::string value = enabled ? "true" : "false";
    std::string default_value = "false";
    AP_DataStorageOperation operation{"replace", &value};
    AP_SetServerDataRequest request;
    {
        std::lock_guard lock(g_state.mutex);
        if (g_state.marker_storage_key.empty()) return;
        request.key = g_state.marker_storage_key;
    }
    request.operations.push_back(operation);
    request.default_value = &default_value;
    request.type = AP_DataType::Raw;
    request.want_reply = false;
    AP_SetServerData(&request);
    std::lock_guard lock(g_state.mutex);
    g_state.collectible_markers_enabled = enabled;
    g_state.dirty = true;
    Debug("stored collectible marker preference");
}

void StoreScoreBonusItemsApplied(std::uint32_t count) {
    std::string value = std::to_string(count);
    std::string default_value = "0";
    AP_DataStorageOperation operation{"replace", &value};
    AP_SetServerDataRequest request;
    {
        std::lock_guard lock(g_state.mutex);
        if (!g_state.score_bonus_storage_loaded ||
            count <= g_state.score_bonus_items_applied) return;
        const auto totals = ItemCounts(g_state.received_items);
        if (count > (totals.contains("Score Bonus")
                ? static_cast<std::uint32_t>(totals.at("Score Bonus")) : 0)) {
            Debug("ignored invalid consumed score bonus count");
            return;
        }
        request.key = g_state.score_bonus_storage_key;
    }
    request.operations.push_back(operation);
    request.default_value = &default_value;
    request.type = AP_DataType::Raw;
    request.want_reply = false;
    AP_SetServerData(&request);
    std::lock_guard lock(g_state.mutex);
    g_state.score_bonus_items_applied = count;
    g_state.dirty = true;
    Debug("stored consumed score bonus count");
}

void SendAppearanceToServer(
    const Json::Value& appearance,
    const std::string& storage_key) {
    std::string value = Json::writeString(CompactWriter(), appearance);
    std::string default_value = "null";
    AP_DataStorageOperation operation{"replace", &value};
    AP_SetServerDataRequest request;
    request.key = storage_key;
    request.operations.push_back(operation);
    request.default_value = &default_value;
    request.type = AP_DataType::Raw;
    request.want_reply = false;
    AP_SetServerData(&request);
}

void StoreAppearance(const Json::Value& appearance) {
    std::string storage_key;
    {
        std::lock_guard lock(g_state.mutex);
        if (g_state.appearance_storage_key.empty()) return;
        if (!g_state.appearance_loaded) {
            g_state.pending_appearance = appearance;
            return;
        }
        storage_key = g_state.appearance_storage_key;
    }
    SendAppearanceToServer(appearance, storage_key);
    std::lock_guard lock(g_state.mutex);
    g_state.appearance = appearance;
    g_state.dirty = true;
    Debug("stored skater appearance");
}

void SendMedalResultsToServer(
    const Json::Value& results,
    const std::string& storage_key) {
    std::string value = Json::writeString(CompactWriter(), results);
    std::string default_value = "{}";
    AP_DataStorageOperation operation{"replace", &value};
    AP_SetServerDataRequest request;
    request.key = storage_key;
    request.operations.push_back(operation);
    request.default_value = &default_value;
    request.type = AP_DataType::Raw;
    request.want_reply = false;
    AP_SetServerData(&request);
}

void StoreMedalResultsIfRequested() {
    if (!g_medal_store_requested.exchange(false)) return;
    Json::Value results;
    std::string storage_key;
    {
        std::lock_guard lock(g_state.mutex);
        if (!g_state.medal_storage_loaded) return;
        results = g_state.medal_results;
        storage_key = g_state.medal_storage_key;
    }
    SendMedalResultsToServer(results, storage_key);
}

void RecordMedalResult(const std::string& level, int medal_id) {
    if (level.empty() || medal_id < 0 || medal_id > 2) return;
    Json::Value results;
    std::string storage_key;
    {
        std::lock_guard lock(g_state.mutex);
        Json::Value& target = g_state.medal_storage_loaded
            ? g_state.medal_results : g_state.pending_medal_results;
        if (target.isMember(level) && target[level].asInt() >= medal_id) return;
        target[level] = medal_id;
        if (!g_state.medal_storage_loaded) return;
        results = g_state.medal_results;
        storage_key = g_state.medal_storage_key;
    }
    SendMedalResultsToServer(results, storage_key);
    g_medal_replay_requested = true;
    Debug("stored medal result for " + level);
}

void ReceiveStorageReply(AP_SetReply reply) {
    std::lock_guard lock(g_state.mutex);
    if (!reply.value) return;
    try {
        const auto value = ParseJson(*static_cast<std::string*>(reply.value));
        if (reply.key == g_state.storage_key) {
            if (!value.isNull() && !ValidAssignment(value))
                throw std::runtime_error("invalid stat assignment");
            g_state.stat_assignment = value;
            g_state.stat_assignment_loaded = true;
        } else if (reply.key == g_state.appearance_storage_key) {
            if (!value.isNull() && !value.isString())
                throw std::runtime_error("invalid skater appearance");
            g_state.appearance = value;
            g_state.appearance_loaded = true;
        } else if (reply.key == g_state.marker_storage_key) {
            if (!value.isBool()) throw std::runtime_error("invalid collectible marker preference");
            g_state.collectible_markers_enabled = value.asBool();
        } else if (reply.key == g_state.score_bonus_storage_key) {
            if (!value.isUInt()) throw std::runtime_error("invalid consumed score bonus count");
            g_state.score_bonus_items_applied = value.asUInt();
            g_state.score_bonus_storage_loaded = true;
        } else if (reply.key == g_state.medal_storage_key) {
            if (!ValidMedalResults(value))
                throw std::runtime_error("invalid medal results");
            g_state.medal_results = value;
            bool merged = false;
            for (const auto& level : g_state.pending_medal_results.getMemberNames())
                if (!g_state.medal_results.isMember(level) ||
                    g_state.medal_results[level].asInt() <
                        g_state.pending_medal_results[level].asInt()) {
                    g_state.medal_results[level] =
                        g_state.pending_medal_results[level];
                    merged = true;
                }
            g_state.pending_medal_results = Json::Value{Json::objectValue};
            g_state.medal_storage_loaded = true;
            if (merged) g_medal_store_requested = true;
            g_medal_replay_requested = true;
        } else {
            return;
        }
        g_state.dirty = true;
        Debug("received slot storage update");
    } catch (const std::exception& error) {
        std::cerr << "Rejected slot storage update: " << error.what() << '\n';
    }
}

Json::Value GapMenu(
    const Json::Value& slot,
    const std::vector<std::string>& received_items,
    const std::set<std::int64_t>& checked,
    const std::string& level) {
    Json::Value payload;
    int done = 0, total = 0, row = 1;
    const auto items = ItemCounts(received_items);
    std::vector<const Json::Value*> visible;
    for (const auto& gap : slot["active_gaps"]) {
        if (gap["level"].asString() != level) continue;
        ++total;
        if (checked.contains(gap["location_id"].asInt64())) { ++done; continue; }
        if (!GapInLogic(gap, items)) continue;
        visible.push_back(&gap);
    }
    std::ranges::sort(visible, {}, [](const Json::Value* gap) {
        return (*gap)["name"].asString();
    });
    for (const Json::Value* gap : visible) {
        char name[20];
        std::snprintf(name, sizeof(name), "row_%03d", row);
        payload[name] = (*gap)["name"];
        std::snprintf(name, sizeof(name), "checksum_%03d", row++);
        payload[name] = (*gap)["checksum"];
    }
    if (row == 1) payload["row_001"] = done == total
        ? "All active gaps complete"
        : "No gaps currently in logic";
    payload["count"] = row == 1 ? 1 : row - 1;
    const auto level_name = slot["levels"][level]["name"].asString();
    payload["title"] = level_name + " Gaps (" + std::to_string(done) + "/" +
        std::to_string(done + visible.size()) + " (" + std::to_string(total) + "))";
    return payload;
}

void RequestStorageIfReady() {
    if (AP_GetConnectionStatus() != AP_ConnectionStatus::Authenticated) return;
    std::lock_guard lock(g_state.mutex);
    if (g_state.storage_requested) return;
    const auto storage_prefix = AP_GetPrivateServerDataPrefix() + "thps3_";
    g_state.storage_key = storage_prefix + "stat_assignment";
    g_state.storage_request.key = g_state.storage_key;
    g_state.storage_request.value = &g_state.storage_value;
    g_state.storage_request.type = AP_DataType::Raw;
    g_state.appearance_storage_key = storage_prefix + "appearance";
    g_state.appearance_storage_request.key = g_state.appearance_storage_key;
    g_state.appearance_storage_request.value = &g_state.appearance_storage_value;
    g_state.appearance_storage_request.type = AP_DataType::Raw;
    g_state.marker_storage_key = storage_prefix + "collectible_markers";
    g_state.marker_storage_request.key = g_state.marker_storage_key;
    g_state.marker_storage_request.value = &g_state.marker_storage_value;
    g_state.marker_storage_request.type = AP_DataType::Raw;
    g_state.score_bonus_storage_key = storage_prefix + "score_bonus";
    g_state.score_bonus_storage_request.key = g_state.score_bonus_storage_key;
    g_state.score_bonus_storage_request.value = &g_state.score_bonus_storage_value;
    g_state.score_bonus_storage_request.type = AP_DataType::Raw;
    g_state.medal_storage_key = storage_prefix + "medal_results";
    g_state.medal_storage_request.key = g_state.medal_storage_key;
    g_state.medal_storage_request.value = &g_state.medal_storage_value;
    g_state.medal_storage_request.type = AP_DataType::Raw;
    AP_SetNotify(g_state.storage_key, AP_DataType::Raw);
    AP_SetNotify(g_state.appearance_storage_key, AP_DataType::Raw);
    AP_SetNotify(g_state.marker_storage_key, AP_DataType::Raw);
    AP_SetNotify(g_state.score_bonus_storage_key, AP_DataType::Raw);
    AP_SetNotify(g_state.medal_storage_key, AP_DataType::Raw);
    AP_GetServerData(&g_state.storage_request);
    AP_GetServerData(&g_state.appearance_storage_request);
    AP_GetServerData(&g_state.marker_storage_request);
    AP_GetServerData(&g_state.score_bonus_storage_request);
    AP_GetServerData(&g_state.medal_storage_request);
    g_state.storage_requested = true;
    Debug("requested slot storage keys");
}

void UpdateStorageResult() {
    std::lock_guard lock(g_state.mutex);
    if (!g_state.storage_requested) return;
    if (g_state.storage_request.status == AP_RequestStatus::Done) {
        g_state.stat_assignment_loaded = true;
        try {
            const auto value = ParseJson(g_state.storage_value);
            if (!value.isNull() && !ValidAssignment(value))
                throw std::runtime_error("invalid stat assignment");
            if (g_state.stat_assignment != value) {
                g_state.stat_assignment = value;
                g_state.dirty = true;
            }
        } catch (const std::exception& error) {
            std::cerr << "Rejected stored stat assignment: " << error.what() << '\n';
        }
        g_state.dirty = true;
        g_state.storage_request.status = AP_RequestStatus::Error;
    }
    if (g_state.appearance_storage_request.status == AP_RequestStatus::Done) {
        g_state.appearance_loaded = true;
        try {
            const auto value = ParseJson(g_state.appearance_storage_value);
            if (!value.isNull() && !value.isString())
                throw std::runtime_error("invalid stored skater appearance");
            Json::Value selected = value;
            if (selected.isNull() && g_state.pending_appearance.isString()) {
                selected = g_state.pending_appearance;
                SendAppearanceToServer(
                    selected, g_state.appearance_storage_key);
            }
            g_state.pending_appearance = Json::Value{};
            if (g_state.appearance != selected) {
                g_state.appearance = selected;
                g_state.dirty = true;
            }
        } catch (const std::exception& error) {
            std::cerr << "Rejected stored skater appearance: "
                      << error.what() << '\n';
        }
        g_state.dirty = true;
        g_state.appearance_storage_request.status = AP_RequestStatus::Error;
    }
    if (g_state.marker_storage_request.status == AP_RequestStatus::Done) {
        try {
            const auto value = ParseJson(g_state.marker_storage_value);
            if (!value.isNull() && !value.isBool())
                throw std::runtime_error("invalid collectible marker preference");
            const bool enabled = value.isBool() && value.asBool();
            if (g_state.collectible_markers_enabled != enabled) {
                g_state.collectible_markers_enabled = enabled;
                g_state.dirty = true;
            }
        } catch (const std::exception& error) {
            std::cerr << "Rejected stored marker preference: " << error.what() << '\n';
        }
        g_state.marker_storage_request.status = AP_RequestStatus::Error;
    }
    if (g_state.score_bonus_storage_request.status == AP_RequestStatus::Done) {
        try {
            const auto value = ParseJson(g_state.score_bonus_storage_value);
            if (!value.isNull() && !value.isUInt())
                throw std::runtime_error("invalid stored consumed score bonus count");
            g_state.score_bonus_items_applied = value.isUInt() ? value.asUInt() : 0;
            g_state.score_bonus_storage_loaded = true;
            g_state.dirty = true;
        } catch (const std::exception& error) {
            std::cerr << "Rejected stored consumed score bonus count: "
                      << error.what() << '\n';
        }
        g_state.score_bonus_storage_request.status = AP_RequestStatus::Error;
    }
    if (g_state.medal_storage_request.status == AP_RequestStatus::Done) {
        try {
            Json::Value value = ParseJson(g_state.medal_storage_value);
            if (value.isNull()) value = Json::Value{Json::objectValue};
            if (!ValidMedalResults(value))
                throw std::runtime_error("invalid stored medal results");
            g_state.medal_results = std::move(value);
            bool merged = false;
            for (const auto& level : g_state.pending_medal_results.getMemberNames())
                if (!g_state.medal_results.isMember(level) ||
                    g_state.medal_results[level].asInt() <
                        g_state.pending_medal_results[level].asInt()) {
                    g_state.medal_results[level] =
                        g_state.pending_medal_results[level];
                    merged = true;
                }
            g_state.pending_medal_results = Json::Value{Json::objectValue};
            g_state.medal_storage_loaded = true;
            if (merged) g_medal_store_requested = true;
            g_medal_replay_requested = true;
        } catch (const std::exception& error) {
            std::cerr << "Rejected stored medal results: "
                      << error.what() << '\n';
        }
        g_state.medal_storage_request.status = AP_RequestStatus::Error;
    }
}

constexpr bool IsServerItemMessage(
    AP_MessageType type, std::string_view sender) {
    return type == AP_MessageType::ItemRecv && sender == "Archipelago";
}

static_assert(IsServerItemMessage(AP_MessageType::ItemRecv, "Archipelago"));
static_assert(!IsServerItemMessage(AP_MessageType::ItemRecv, "Skater"));

constexpr std::string_view ItemColor(int flags) {
    return flags & 0b001 ? "&C1" : flags & 0b010 ? "&C0" :
        flags & 0b100 ? "&C5" : "&C4";
}

static_assert(ItemColor(0) == "&C4");
static_assert(ItemColor(0b001) == "&C1");
static_assert(ItemColor(0b010) == "&C0");
static_assert(ItemColor(0b100) == "&C5");

std::string FormatItemMessage(
    const AP_Message& message, int flags, std::string_view slot_name) {
    std::string text;
    for (const auto& part : message.messageParts) {
        text += part.type == AP_ItemText ? ItemColor(flags) :
            part.type == AP_PlayerText
                ? (part.text == slot_name ? "&C7" : "&C2") :
            part.type == AP_LocationText ? "&C6" : "&C3";
        text += part.text;
    }
    return text;
}

std::vector<std::string> SplitDisplayMessage(std::string_view text) {
    constexpr std::size_t kMaxBytes = 80;
    std::vector<std::string> chunks;
    std::string active_color;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::size_t start = offset;
        const std::string prefix = chunks.empty() ? "" : active_color;
        std::string chunk = prefix;
        std::size_t word_break = std::string_view::npos;
        std::string color_at_word_break = active_color;
        while (offset < text.size()) {
            const bool color = offset + 2 < text.size() &&
                text[offset] == '&' && text[offset + 1] == 'C';
            const std::size_t token_size = color ? 3 : 1;
            if (chunk.size() + token_size > kMaxBytes) break;
            chunk.append(text.substr(offset, token_size));
            if (color) active_color.assign(text.substr(offset, token_size));
            offset += token_size;
            if (!color && text[offset - 1] == ' ') {
                word_break = offset;
                color_at_word_break = active_color;
            }
        }
        if (offset < text.size() && word_break != std::string_view::npos &&
            word_break > start) {
            chunk = prefix + std::string(text.substr(
                start, word_break - start - 1));
            offset = word_break;
            active_color = std::move(color_at_word_break);
        }
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

void SendMessages(
    Pipe& pipe, std::uint64_t& sequence, std::string_view slot_name) {
    while (AP_Message* message = AP_PopLatestMessage()) {
        auto* received_item = message->type == AP_MessageType::ItemRecv
            ? static_cast<AP_ItemRecvMessage*>(message) : nullptr;
        auto* sent_item = message->type == AP_MessageType::ItemSend
            ? static_cast<AP_ItemSendMessage*>(message) : nullptr;
        const bool hide = IsServerItemMessage(
            message->type,
            received_item ? received_item->sendPlayer : "");
        std::string text = received_item
            ? FormatItemMessage(*received_item, received_item->flags, slot_name)
            : sent_item
                ? FormatItemMessage(*sent_item, sent_item->flags, slot_name)
            : message->text;
        AP_FreeMessage(message);
        if (hide) continue;
        for (char& character : text)
            if (static_cast<unsigned char>(character) >= 0x80) character = '?';
        Debug("display source: " + text);
        auto chunks = SplitDisplayMessage(text);
        for (std::size_t index = 0; index < chunks.size(); ++index) {
            Debug("display chunk " + std::to_string(index + 1) + "/" +
                std::to_string(chunks.size()) + ": " + chunks[index]);
            Json::Value payload;
            payload["text"] = std::move(chunks[index]);
            payload["more"] = index + 1 < chunks.size();
            pipe.Write(Frame("display_message", sequence++, std::move(payload)));
        }
    }
}

std::string_view ConnectionStatusName(AP_ConnectionStatus status) {
    switch (status) {
        case AP_ConnectionStatus::Disconnected: return "disconnected";
        case AP_ConnectionStatus::Connected: return "connected";
        case AP_ConnectionStatus::Authenticated: return "authenticated";
        case AP_ConnectionStatus::ConnectionRefused: return "refused";
    }
    return "unknown";
}

std::string ConnectionFailureMessage(std::string_view reason) {
    return "Archipelago connection failed: " + std::string(reason);
}

void Run(const std::string& slot_name) {
    Pipe pipe(kDefaultPipe);
    std::uint64_t sequence = 0;
    auto last_status = AP_ConnectionStatus::Disconnected;
    auto next_pending_send = std::chrono::steady_clock::now();
    std::string last_connection_error;
    while (g_running) {
        RequestStorageIfReady();
        UpdateStorageResult();
        StoreMedalResultsIfRequested();
        ReplayEarnedMedalsIfRequested();
        if (!pipe.Connect()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        Json::Value hello;
        hello["client"] = "thps3-apclient-native";
        hello["client_version"] = "1.0.0";
        hello["game"] = kGame;
        if (!pipe.Write(Frame("hello", sequence++, std::move(hello)))) { pipe.Close(); continue; }

        while (g_running) {
            RequestStorageIfReady();
            UpdateStorageResult();
            StoreMedalResultsIfRequested();
            ReplayEarnedMedalsIfRequested();
            AP_RoomInfo room;
            const auto status = AP_GetConnectionStatus();
            if (status != last_status) {
                Debug("APCpp connection: " + std::string(ConnectionStatusName(status)));
                if (status == AP_ConnectionStatus::Authenticated)
                    thps3_ap::StartupBreadcrumb("apcpp_authenticated");
                if (status == AP_ConnectionStatus::Authenticated)
                    next_pending_send = std::chrono::steady_clock::now();
                last_status = status;
            }
            const std::string connection_error = AP_GetConnectionError();
            if (!connection_error.empty() && connection_error != last_connection_error) {
                Json::Value payload;
                payload["message"] = ConnectionFailureMessage(connection_error);
                if (!pipe.Write(Frame("error", sequence++, std::move(payload)))) break;
                last_connection_error = connection_error;
            }
            const bool authenticated = status == AP_ConnectionStatus::Authenticated;
            const bool have_room = AP_GetRoomInfo(&room) == 0;
            const auto now = std::chrono::steady_clock::now();
            if (authenticated && now >= next_pending_send) {
                SendPendingLocations();
                next_pending_send = now + std::chrono::seconds(1);
            }
            if (authenticated) SendGoalIfComplete();
            if (authenticated && have_room && g_state.dirty.exchange(false)) {
                try {
                    Json::Value slot, assignment, appearance;
                    bool appearance_loaded = false;
                    bool stat_assignment_loaded = false;
                    bool collectible_markers_enabled = false;
                    bool score_bonus_storage_loaded = false;
                    std::uint32_t score_bonus_items_applied = 0;
                    std::vector<std::string> items;
                    std::set<std::int64_t> checked;
                    {
                        std::lock_guard lock(g_state.mutex);
                        slot = g_state.slot_data;
                        assignment = g_state.stat_assignment;
                        stat_assignment_loaded = g_state.stat_assignment_loaded;
                        appearance = g_state.appearance;
                        appearance_loaded = g_state.appearance_loaded;
                        collectible_markers_enabled =
                            g_state.collectible_markers_enabled;
                        score_bonus_storage_loaded = g_state.score_bonus_storage_loaded;
                        score_bonus_items_applied = g_state.score_bonus_items_applied;
                        items = g_state.received_items;
                        checked = g_state.checked_locations;
                    }
                    if (!pipe.Write(Frame("state_snapshot", sequence++, BuildSnapshot(
                            slot, items, checked, assignment, slot_name, room.seed_name,
                            collectible_markers_enabled, appearance,
                            appearance_loaded, stat_assignment_loaded,
                            score_bonus_items_applied,
                            score_bonus_storage_loaded)))) break;
                } catch (const std::exception& error) {
                    Json::Value payload;
                    payload["message"] = error.what();
                    if (!pipe.Write(Frame("error", sequence++, std::move(payload)))) break;
                }
            }
            SendMessages(pipe, sequence, slot_name);

            std::vector<std::string> frames;
            if (!pipe.ReadFrames(frames)) break;
            for (const auto& raw : frames) {
                try {
                    const auto message = ParseJson(raw);
                    if (message["protocol_version"].asInt() != 1) continue;
                    const auto type = message["type"].asString();
                    if (g_debug) std::osyncstream(std::cerr) << "[debug] pipe <- " << type
                        << " #" << message["sequence"].asUInt64() << '\n';
                    const auto payload = message["payload"];
                    if (type == "location_event" && payload["kind"].asString() == "gap_list") {
                        Json::Value slot;
                        std::vector<std::string> items;
                        std::set<std::int64_t> checked;
                        { std::lock_guard lock(g_state.mutex); slot = g_state.slot_data; items = g_state.received_items; checked = g_state.checked_locations; }
                        pipe.Write(Frame("gap_menu", sequence++, GapMenu(slot, items, checked, payload["level"].asString())));
                    } else if (type == "location_event" &&
                               payload["kind"].asString() == "medal_result") {
                        RecordMedalResult(
                            payload["level"].asString(),
                            payload["medal_id"].asInt());
                    } else if (type == "location_event") {
                        Json::Value slot;
                        { std::lock_guard lock(g_state.mutex); slot = g_state.slot_data; }
                        const auto location = LocationForEvent(slot, payload);
                        if (location && QueueLocation(location)) {
                            Debug("sending location " + std::to_string(location));
                            AP_SendItem(location);
                        } else if (!location) {
                            Debug("ignored unmapped " + payload["kind"].asString() + " event");
                        }
                    } else if (type == "game_state") {
                        if (ValidAssignment(payload["stat_assignment"])) StoreAssignment(payload["stat_assignment"]);
                        if (payload["appearance"].isString())
                            StoreAppearance(payload["appearance"]);
                        if (payload["score_bonus_items_applied"].isUInt())
                            StoreScoreBonusItemsApplied(
                                payload["score_bonus_items_applied"].asUInt());
                        if (payload["collectible_markers_enabled"].isBool())
                            StoreCollectibleMarkersEnabled(
                                payload["collectible_markers_enabled"].asBool());
                    } else if (type == "game_complete") {
                        SendGoalIfComplete();
                    }
                } catch (const std::exception& error) {
                    std::cerr << "Rejected bridge frame: " << error.what() << '\n';
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        pipe.Close();
    }
}

void RunClient(
    const std::string& server,
    const std::string& slot,
    const std::string& password) {
    thps3_ap::StartupBreadcrumb("apcpp_thread_enter");
    g_running = true;
    {
        std::lock_guard lock(g_state.mutex);
        g_state.pending_locations.clear();
    }
    AP_NetworkVersion version{0, 6, 7};
    AP_SetClientVersion(&version);
    AP_Init(server.c_str(), kGame, slot.c_str(), password.c_str());
    AP_SetItemClearCallback(ClearItems);
    AP_SetItemRecvCallback(ReceiveItem);
    AP_SetLocationCheckedCallback(CheckLocation);
    AP_RegisterSetReplyCallback(ReceiveStorageReply);
    const char* raw_keys[] = {"selected_skater", "levels", "goal_locations", "goal_unlocks",
        "active_gaps", "stat_point_locations", "stat_points_are_locations",
        "hidden_decks_are_locations", "deck_locations", "completion_goal",
        "collectible_markers_default"};
    AP_RegisterSlotDataIntCallback("protocol_version", [](int value) {
        std::lock_guard lock(g_state.mutex);
        g_state.slot_data["protocol_version"] = value;
        g_state.dirty = true;
    });
    for (const char* key : raw_keys)
        AP_RegisterSlotDataRawCallback(key, [key](std::string value) { SetSlotData(key, value); });
    Debug("starting APCpp client");
    thps3_ap::StartupBreadcrumb("apcpp_start");
    AP_Start();
    Run(slot);
    AP_Shutdown();
}

std::atomic_bool g_embedded_started{false};
std::string g_embedded_server;
std::string g_embedded_slot;
std::string g_embedded_password;

DWORD WINAPI RunEmbeddedClient(void*) {
    RunClient(g_embedded_server, g_embedded_slot, g_embedded_password);
    g_embedded_started = false;
    return 0;
}

}  // namespace

extern "C" void THPS3AP_StartEmbeddedClient(
    const char* server,
    const char* slot,
    const char* password,
    BOOL debug) {
    if (!server || !*server || !slot || !*slot || g_embedded_started.exchange(true)) return;
    g_debug = debug != FALSE;
    g_embedded_server = server;
    g_embedded_slot = slot;
    g_embedded_password = password ? password : "";
    HANDLE thread = CreateThread(nullptr, 0, RunEmbeddedClient, nullptr, 0, nullptr);
    if (!thread) {
        g_embedded_started = false;
        return;
    }
    CloseHandle(thread);
}
