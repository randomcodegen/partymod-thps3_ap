#include "thps3_ap/bridge_c_api.h"
#include "thps3_ap/bridge_runtime.hpp"
#include "thps3_ap/game_state.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

extern "C" BOOL THPS3AP_LevelMenuRefreshPending() {
    return FALSE;
}

extern "C" void THPS3AP_RequestLevelMenuRefresh() {}
extern "C" void THPS3AP_SetGapMenuData(
    const char*, const char* const*, const std::uint32_t*, std::uint32_t) {}
BOOL g_collectible_markers_enabled = FALSE;
extern "C" void THPS3AP_SetCollectibleMarkersEnabled(BOOL enabled) {
    g_collectible_markers_enabled = enabled;
}
extern "C" BOOL THPS3AP_CollectibleMarkersEnabled() {
    return g_collectible_markers_enabled;
}


namespace {

constexpr wchar_t kTestPipeName[] =
    LR"(\\.\pipe\thps3_archipelago_native_test)";

bool CompleteIo(
    HANDLE pipe,
    BOOL started,
    OVERLAPPED& overlapped,
    DWORD& transferred,
    DWORD timeout_ms) {
    if (started) {
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    if (WaitForSingleObject(overlapped.hEvent, timeout_ms) != WAIT_OBJECT_0) {
        CancelIoEx(pipe, &overlapped);
        WaitForSingleObject(overlapped.hEvent, INFINITE);
        GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
        return false;
    }
    return GetOverlappedResult(
        pipe, &overlapped, &transferred, FALSE) != FALSE;
}

bool WriteFrame(HANDLE pipe, std::string_view frame) {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    OVERLAPPED overlapped{};
    overlapped.hEvent = event;
    DWORD written = 0;
    const BOOL started = WriteFile(
        pipe,
        frame.data(),
        static_cast<DWORD>(frame.size()),
        &written,
        &overlapped);
    const bool succeeded = CompleteIo(
        pipe, started, overlapped, written, 2000) &&
        written == frame.size();
    CloseHandle(event);
    return succeeded;
}

bool ReadFrame(HANDLE pipe, std::string& frame) {
    frame.clear();
    while (frame.find('\n') == std::string::npos) {
        char buffer[4096];
        const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr) {
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD bytes_read = 0;
        const BOOL started = ReadFile(
            pipe, buffer, sizeof(buffer), &bytes_read, &overlapped);
        const bool succeeded = CompleteIo(
            pipe, started, overlapped, bytes_read, 2000);
        CloseHandle(event);
        if (!succeeded || bytes_read == 0) {
            return false;
        }
        frame.append(buffer, bytes_read);
    }
    return true;
}

HANDLE ConnectClient() {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const HANDLE pipe = CreateFileW(
            kTestPipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return INVALID_HANDLE_VALUE;
}

}  // namespace

int main() {
    if (!thps3_ap::StartupConnectionTimedOut(true, 20'000) ||
        thps3_ap::StartupConnectionTimedOut(false, 20'000)) {
        std::cerr << "startup timeout check failed\n";
        return 1;
    }

    std::thread server([] {
        thps3_ap::RunBridgeRuntimeForTest(kTestPipeName);
    });

    const HANDLE pipe = ConnectClient();
    if (pipe == INVALID_HANDLE_VALUE) {
        thps3_ap::RequestBridgeShutdown();
        server.join();
        std::cerr << "failed to connect to bridge runtime\n";
        return 1;
    }

    constexpr std::string_view hello =
        R"({"payload":{},"protocol_version":1,"sequence":1,"type":"hello"})"
        "\n";
    std::string frame;
    std::string appearance;
    std::array<std::uint32_t, 10> stat_assignment{};
    bool passed = WriteFrame(pipe, hello) && ReadFrame(pipe, frame) &&
                  frame.find(R"("type":"hello_ack")") != std::string::npos &&
                  !thps3_ap::StatAssignmentStorageLoaded();

    if (passed) {
        constexpr std::string_view snapshot =
            R"({"payload":{"appearance":"A1;deadbeef000000000100000001;","appearance_loaded":true,"career_checks":[{"level":"canada","objectives":0,"objective_access":1,"objective_location_access":1,"objective_total":9,"stat_points":2,"deck":0,"items_checked":1,"item_total":5},{"level":"foundry","objectives":33,"objective_access":3,"objective_location_access":1,"objective_total":9,"stat_points":5,"deck":1,"items_checked":3,"item_total":6},{"level":"rio","objectives":255,"objective_access":1,"objective_location_access":1,"objective_total":3,"stat_points":0,"deck":0,"items_checked":0,"item_total":0}],"collectible_markers_enabled":true,"gap_progress":[{"level":"canada","checked":0,"in_logic":5,"total":12},{"level":"foundry","checked":7,"in_logic":18,"total":33}],"hidden_decks_are_locations":true,"score_bonus_items":2,"score_bonus_items_applied":1,"score_bonus_storage_loaded":true,"selected_skater":"rodney_mullen","stat_assignment":[3,6,5,7,8,4,6,5,4,7],"stat_assignment_loaded":true,"stat_point_items":12,"stat_points_are_locations":true,"time_bonus_seconds":2,"trick_permissions":{"flip":true,"grab":false,"grind":true,"revert":true,"manual":true,"lip":true,"special":true},"unlocked_levels":["foundry"]},"protocol_version":1,"sequence":2,"type":"state_snapshot"})"
            "\n";
        passed = WriteFrame(pipe, snapshot) && ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"state_ack")") != std::string::npos &&
                 thps3_ap::CheckedStatPointMask(2) == 2 &&
                 thps3_ap::IsDeckChecked(1) &&
                 !thps3_ap::IsDeckChecked(2) &&
                 thps3_ap::CheckedDeckMask() == 2u &&
                 thps3_ap::CheckedObjectiveMask(3) == 7 &&
                 thps3_ap::UnlockedObjectiveMask(1) == 3 &&
                 thps3_ap::AvailableObjectiveLocationMask(1) == 1 &&
                 thps3_ap::UnlockedObjectiveMask(3) == 1 &&
                 thps3_ap::StatPointsAreLocations() &&
                 thps3_ap::HiddenDecksAreLocations() &&
                 thps3_ap::ActiveObjectiveCount(1) == 9 &&
                 thps3_ap::ActiveObjectiveCount(3) == 3 &&
                 thps3_ap::CheckedItemCount(1) == 3 &&
                 thps3_ap::ActiveItemCount(1) == 6 &&
                 thps3_ap::CheckedItemCount(2) == 1 &&
                 thps3_ap::ActiveItemCount(3) == 0 &&
                 thps3_ap::CheckedGapCount(1) == 7 &&
                 thps3_ap::InLogicGapCount(1) == 18 &&
                 thps3_ap::ActiveGapCount(1) == 33 &&
                 thps3_ap::CheckedGapCount(2) == 0 &&
                 thps3_ap::InLogicGapCount(2) == 5 &&
                 thps3_ap::ActiveGapCount(2) == 12 &&
                 thps3_ap::IsObjectiveUnlocked(1, 0) &&
                 !thps3_ap::IsObjectiveUnlocked(1, 2) &&
                 thps3_ap::IsObjectiveUnlocked(1, 5) &&
                 thps3_ap::ObjectiveListPosition(1, 0) == 0 &&
                 thps3_ap::ObjectiveListPosition(1, 1) == 1 &&
                 thps3_ap::ObjectiveListPosition(1, 5) == 2 &&
                 thps3_ap::ObjectiveListPosition(1, 2) == -1 &&
                 thps3_ap::IsObjectiveUnlocked(3, 1) &&
                  thps3_ap::ReceivedStatPointItemCount() == 12 &&
                  thps3_ap::StatPointsAreLocations() &&
                 thps3_ap::TimeBonusSeconds() == 2 &&
                 thps3_ap::TakePendingScoreBonusItems(false) == 0 &&
                 thps3_ap::TakePendingScoreBonusItems(true) == 1 &&
                 thps3_ap::TakePendingScoreBonusItems(true) == 0 &&
                 thps3_ap::GetStatAssignment(stat_assignment) &&
                 thps3_ap::StatAssignmentStorageLoaded() &&
                 stat_assignment[0] == 3 && stat_assignment[4] == 8 &&
                 thps3_ap::AppearanceStorageLoaded() &&
                 thps3_ap::GetAppearance(appearance) &&
                 appearance == "A1;deadbeef000000000100000001;" &&
                 thps3_ap::SelectedSkaterProfileIndex() == 7 &&
                 THPS3AP_CollectibleMarkersEnabled() &&
                 thps3_ap::TrickPermissionMask() == 125 &&
                 thps3_ap::IsTrickCategoryUnlocked(5) &&
                 thps3_ap::IsTrickCategoryUnlocked(6) &&
                 thps3_ap::IsTrickCategoryUnlocked(0) &&
                 !thps3_ap::IsTrickCategoryUnlocked(1) &&
                 thps3_ap::IsTrickCategoryUnlocked(2) &&
                 thps3_ap::IsTrickCategoryUnlocked(3) &&
                 thps3_ap::IsTrickCategoryUnlocked(4) &&
                 thps3_ap::OwnedObjectiveCareerBits(1) == 0x1ffu &&
                 thps3_ap::OwnedObjectiveCareerBits(3) == 0x7u &&
                 thps3_ap::ReplaceObjectiveCareerBits(
                     0x80000200u, 33, 1) == 0x80000221u &&
                 thps3_ap::StatPointMaskToCareerBits(5) == 0x00a00000u &&
                 thps3_ap::ReplaceStatPointCareerBits(
                     0x01401234u, 5) == 0x01a01234u &&
                 thps3_ap::ReplaceDeckCareerBit(
                     0x00a01234u, true) == 0x01a01234u &&
                 thps3_ap::ReplaceDeckCareerBit(
                     0x01a01234u, false) == 0x00a01234u &&
                 thps3_ap::ReplaceDeckInventoryBits(
                     0x80000401u, 2u) == 0x80000403u &&
                 thps3_ap::ReplaceDeckInventoryBits(
                     0x800007ffu, 0u) == 0x80000401u;
    }

    if (passed) {
        // Test-Case 1545223606995394640: 36 received, 18 spent, but storage retained a zero balance.
        std::array<std::uint32_t, 10> assignment{0, 9, 9, 8, 9, 8, 5, 5, 5, 5};
        passed = thps3_ap::AvailableStatPoints(assignment, 36, 0x9b65d7b8u) == 18;
        assignment[0] = 18;
        passed = passed &&
            thps3_ap::AvailableStatPoints(assignment, 36, 0x9b65d7b8u) == 18 &&
            thps3_ap::AvailableStatPoints(assignment, 37, 0x9b65d7b8u) == 19 &&
            thps3_ap::AvailableStatPoints(assignment, 0, 0x9b65d7b8u) == 0;
        ++assignment[1];
        passed = passed &&
            thps3_ap::AvailableStatPoints(assignment, 36, 0x9b65d7b8u) == 17;
        assignment.fill(10);
        passed = passed &&
            thps3_ap::AvailableStatPoints(assignment, 0, 0xe00192c5u) == 0;
        assignment.fill(1);
        passed = passed &&
            thps3_ap::AvailableStatPoints(assignment, 0, 0x939470b0u) == 36 &&
            thps3_ap::AvailableStatPoints(assignment, 45, 0x939470b0u) == 45;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(R"({"stat_point_items":999})");
        passed = thps3_ap::ReceivedStatPointItemCount() == 45;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(R"({"selected_skater":"tony_hawk"})");
        passed = thps3_ap::SelectedSkaterProfileIndex() == 0 &&
                 thps3_ap::SelectedSkaterNativeChecksum() == 0xd32209f9u;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(R"({"selected_skater":"jamie_thomas"})");
        passed = thps3_ap::SelectedSkaterProfileIndex() == 12 &&
                 thps3_ap::SelectedSkaterNativeChecksum() == 0xe16ce841u;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(R"({"selected_skater":"doom_guy"})");
        passed = thps3_ap::SelectedSkaterProfileIndex() == 21 &&
                 thps3_ap::SelectedSkaterNativeChecksum() == 0x00efa334u;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(R"({"selected_skater":"custom_skater"})");
        passed = thps3_ap::SelectedSkaterProfileIndex() == 22 &&
                 thps3_ap::SelectedSkaterNativeChecksum() == 0x0a7be964u &&
                 thps3_ap::SelectedSkaterKey() == "custom_skater";
    }

    if (passed) {
        constexpr std::string_view display_message =
            R"({"payload":{"more":true,"text":"Skater found \"Deck\" \\ Foundry"},"protocol_version":1,"sequence":3,"type":"display_message"})"
            "\n";
        passed = WriteFrame(pipe, display_message);
        std::string text;
        bool more = false;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (passed && !thps3_ap::TakeDisplayMessage(text, more) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        passed = passed && more && text == "Skater found \"Deck\" \\ Foundry";
    }

    if (passed) {
        constexpr std::string_view connection_error =
            R"({"payload":{"message":"invalid seed_name"},"protocol_version":1,"sequence":4,"type":"error"})"
            "\n";
        passed = WriteFrame(pipe, connection_error);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (passed && thps3_ap::ConnectionError().empty() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        passed = passed && thps3_ap::ConnectionError() == "invalid seed_name";
    }

    if (passed) {
        thps3_ap::QueueStatAssignment(
            std::array<std::uint32_t, 10>{{2, 5, 5, 6, 7, 4, 5, 6, 4, 5}});
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"game_state")") != std::string::npos &&
                 frame.find(R"("stat_assignment":[2,5,5,6,7,4,5,6,4,5])") !=
                     std::string::npos;
    }

    if (passed) {
        thps3_ap::QueueAppearance("A1;01234567000000000100000001;");
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"game_state")") != std::string::npos &&
                 frame.find(R"("appearance":"A1;01234567000000000100000001;")") !=
                     std::string::npos;
    }

    if (passed) {
        thps3_ap::QueueCollectibleMarkersEnabled(false);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"game_state")") != std::string::npos &&
                 frame.find(R"("collectible_markers_enabled":false)") !=
                     std::string::npos;
    }

    if (passed) {
        THPS3AP_QueueStatPointEvent(1, 4);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"location_event")") !=
                     std::string::npos &&
                 frame.find(R"("level":"foundry")") != std::string::npos &&
                 frame.find(R"("point_id":4)") != std::string::npos;
    }

    if (passed) {
        THPS3AP_QueueGoalEvent(3, 2);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"location_event")") != std::string::npos &&
                 frame.find(R"("kind":"goal")") != std::string::npos &&
                 frame.find(R"("level":"rio")") != std::string::npos &&
                 frame.find(R"("goal_id":2)") != std::string::npos;
    }

    if (passed) {
        THPS3AP_RecordMedalResult(3, 2);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("kind":"medal_result")") != std::string::npos &&
                 frame.find(R"("level":"rio")") != std::string::npos &&
                 frame.find(R"("medal_id":2)") != std::string::npos;
    }

    if (passed) {
        THPS3AP_QueueDeckEvent(2);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"location_event")") !=
                     std::string::npos &&
                 frame.find(R"("kind":"deck")") != std::string::npos &&
                 frame.find(R"("level":"canada")") != std::string::npos;
    }

    if (passed) {
        THPS3AP_QueueGapEvent(1, 0xC4745838u, 4, 3);
        passed = ReadFrame(pipe, frame) &&
                 frame.find(R"("type":"location_event")") !=
                     std::string::npos &&
                 frame.find(R"("kind":"gap")") != std::string::npos &&
                 frame.find(R"("level":"foundry")") != std::string::npos &&
                 frame.find(R"("checksum":3295959096)") != std::string::npos &&
                 frame.find(R"("skater_state_name":"rail")") !=
                     std::string::npos &&
                 frame.find(R"("trigger_type_name":"land_on")") !=
                     std::string::npos;
    }

    if (passed) {
        thps3_ap::ApplyStateSnapshot(
            R"({"stat_points_are_locations":false,"hidden_decks_are_locations":false})");
        passed = !thps3_ap::StatPointsAreLocations() &&
                 !thps3_ap::HiddenDecksAreLocations();
    }

    constexpr std::string_view disconnect =
        R"({"payload":{},"protocol_version":1,"sequence":3,"type":"disconnect"})"
        "\n";
    WriteFrame(pipe, disconnect);
    CloseHandle(pipe);
    thps3_ap::RequestBridgeShutdown();
    server.join();

    if (!passed) {
        std::cerr << "bridge runtime integration check failed\n";
        return 1;
    }
    return 0;
}
