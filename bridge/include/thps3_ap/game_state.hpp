#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace thps3_ap {

void ApplyStateSnapshot(std::string_view frame);
void QueueDisplayMessage(std::string message, bool more = false);
bool TakeDisplayMessage(std::string& message, bool& more);
void SetConnectionError(std::string message);
std::string ConnectionError();
bool HasStateSnapshot();
bool StartupConnectionTimedOut(
    bool main_menu_loaded,
    std::uint64_t elapsed_milliseconds);
std::uint32_t UnlockedLevelMask();
std::uint32_t ReceivedStatPointItemCount();
bool StatPointsAreLocations();
bool HiddenDecksAreLocations();
std::uint32_t TimeBonusSeconds();
std::uint32_t TakePendingScoreBonusItems(bool active_combo);
std::uint32_t ReceivedScoreBonusItemCount();
bool GetStatAssignment(std::array<std::uint32_t, 10>& assignment);
std::uint32_t AvailableStatPoints(
    const std::array<std::uint32_t, 10>& assignment,
    std::uint32_t received_points,
    std::uint32_t skater_checksum);
bool StatAssignmentStorageLoaded();
bool AppearanceStorageLoaded();
bool GetAppearance(std::string& appearance);
std::uint32_t StateRevision();
std::uint32_t SelectedSkaterProfileIndex();
std::string_view SelectedSkaterKey();
std::uint32_t SelectedSkaterNativeChecksum();
std::uint32_t TrickPermissionMask();
bool IsTrickCategoryUnlocked(int category);
bool IsLevelUnlocked(int level_number);
std::uint32_t CheckedStatPointMask(int level_number);
bool IsDeckChecked(int level_number);
std::uint32_t CheckedDeckMask();
std::uint32_t CheckedObjectiveMask(int level_number);
std::uint32_t UnlockedObjectiveMask(int level_number);
std::uint32_t AvailableObjectiveLocationMask(int level_number);
std::uint32_t ActiveObjectiveCount(int level_number);
std::uint32_t CheckedItemCount(int level_number);
std::uint32_t ActiveItemCount(int level_number);
std::uint32_t CheckedGapCount(int level_number);
std::uint32_t InLogicGapCount(int level_number);
std::uint32_t ActiveGapCount(int level_number);
bool IsObjectiveUnlocked(int level_number, int goal_id);
int ObjectiveListPosition(int level_number, int goal_id);
std::uint32_t OwnedObjectiveCareerBits(int level_number);
std::uint32_t ReplaceObjectiveCareerBits(
    std::uint32_t existing_career_bits,
    std::uint32_t objective_mask,
    int level_number);
std::uint32_t StatPointMaskToCareerBits(std::uint32_t stat_point_mask);
std::uint32_t ReplaceStatPointCareerBits(
    std::uint32_t existing_career_bits,
    std::uint32_t stat_point_mask);
std::uint32_t ReplaceDeckCareerBit(
    std::uint32_t existing_career_bits,
    bool checked);
std::uint32_t ReplaceDeckInventoryBits(
    std::uint32_t existing_inventory_bits,
    std::uint32_t checked_deck_mask);

}  // namespace thps3_ap
