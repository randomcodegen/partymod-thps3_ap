#include "thps3_ap/bridge_runtime.hpp"
#include "thps3_ap/bridge_c_api.h"
#include "thps3_ap/game_state.hpp"

#include <json/json.h>

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <sddl.h>

namespace {

std::atomic_bool g_shutdown_requested = false;
std::atomic_bool g_state_snapshot_logged = false;
std::atomic<HANDLE> g_active_pipe = INVALID_HANDLE_VALUE;
std::mutex g_pipe_write_mutex;
std::mutex g_event_mutex;
std::mutex g_startup_log_mutex;

void WriteStartupBreadcrumb(const char* phase) {
    wchar_t path[MAX_PATH];
    const DWORD length = GetTempPathW(MAX_PATH, path);
    if (length == 0 || length + 23 >= MAX_PATH) return;
    wcscat_s(path, L"thps3-ap-startup.log");

    const std::lock_guard lock(g_startup_log_mutex);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetSystemTime(&now);
    char line[256];
    const int size = std::snprintf(line, sizeof(line),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu build=%s %s phase=%s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, GetCurrentProcessId(),
        GetCurrentThreadId(), __DATE__, __TIME__, phase);
    DWORD written = 0;
    if (size > 0) {
        WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr);
        FlushFileBuffers(file);
    }
    CloseHandle(file);
}
std::condition_variable g_event_ready;
std::deque<std::string> g_pending_events;
std::array<std::atomic_uint32_t, 9> g_reported_goals{};
std::array<std::atomic_uint32_t, 9> g_reported_medal_results{};
std::atomic<std::uint64_t> g_reported_stat_points = 0;
std::atomic_uint32_t g_reported_decks = 0;
std::atomic_uint32_t g_event_sequence = 2;

constexpr std::array<std::string_view, 9> kLevelKeys = {
    "foundry",
    "canada",
    "rio",
    "suburbia",
    "airport",
    "skater_island",
    "los_angeles",
    "tokyo",
    "cruise_ship",
};

constexpr std::string_view SkaterStateName(std::uint32_t state) {
    constexpr std::array<std::string_view, 6> names = {
        "ground", "air", "wall", "lip", "rail", "wallplant",
    };
    return state < names.size() ? names[state] : "unknown";
}

constexpr std::string_view TriggerTypeName(std::uint32_t type) {
    constexpr std::array<std::string_view, 10> names = {
        "none", "skate_off_edge", "jump_off", "land_on", "skate_off",
        "skate_onto", "bonk", "lip_on", "lip_off", "lip_jump",
    };
    return type < names.size() ? names[type] : "unknown";
}

constexpr std::string_view kHelloAck =
    R"({"payload":{"bridge_version":"1.0.0","game_hooks_ready":true},"protocol_version":1,"sequence":0,"type":"hello_ack"})"
    "\n";
constexpr std::string_view kStateAck =
    R"({"payload":{},"protocol_version":1,"sequence":1,"type":"state_ack"})"
    "\n";
bool WriteAll(HANDLE pipe, std::string_view data) {
    const std::lock_guard write_lock(g_pipe_write_mutex);
    const HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io_event == nullptr) {
        return false;
    }

    bool succeeded = true;
    while (!data.empty()) {
        ResetEvent(io_event);
        OVERLAPPED overlapped{};
        overlapped.hEvent = io_event;
        DWORD written = 0;
        if (!WriteFile(
                pipe,
                data.data(),
                static_cast<DWORD>(data.size()),
                &written,
                &overlapped)) {
            if (GetLastError() != ERROR_IO_PENDING ||
                WaitForSingleObject(io_event, INFINITE) != WAIT_OBJECT_0 ||
                !GetOverlappedResult(
                    pipe, &overlapped, &written, FALSE)) {
                succeeded = false;
                break;
            }
        }
        if (written == 0) {
            succeeded = false;
            break;
        }
        data.remove_prefix(written);
    }
    CloseHandle(io_event);
    return succeeded;
}

bool ReadPipe(HANDLE pipe, void* buffer, DWORD size, DWORD& bytes_read) {
    const HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io_event == nullptr) {
        return false;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = io_event;
    bytes_read = 0;
    bool succeeded = ReadFile(
        pipe, buffer, size, &bytes_read, &overlapped) != FALSE;
    if (!succeeded && GetLastError() == ERROR_IO_PENDING &&
        WaitForSingleObject(io_event, INFINITE) == WAIT_OBJECT_0) {
        succeeded = GetOverlappedResult(
            pipe, &overlapped, &bytes_read, FALSE) != FALSE;
    }

    CloseHandle(io_event);
    return succeeded;
}

bool ConnectPipe(HANDLE pipe) {
    const HANDLE io_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (io_event == nullptr) {
        return false;
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = io_event;
    bool connected = ConnectNamedPipe(pipe, &overlapped) != FALSE;
    if (!connected) {
        const DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED) {
            connected = true;
        } else if (error == ERROR_IO_PENDING &&
                   WaitForSingleObject(io_event, INFINITE) == WAIT_OBJECT_0) {
            DWORD transferred = 0;
            connected = GetOverlappedResult(
                pipe, &overlapped, &transferred, FALSE) != FALSE;
        }
    }

    CloseHandle(io_event);
    return connected;
}

void WritePendingEvents(
    HANDLE pipe,
    std::atomic_bool& stop_requested,
    std::atomic_bool& writer_enabled) {
    while (!g_shutdown_requested.load() && !stop_requested.load()) {
        std::string event;
        {
            std::unique_lock event_lock(g_event_mutex);
            g_event_ready.wait(event_lock, [&stop_requested, &writer_enabled] {
                return g_shutdown_requested.load() || stop_requested.load() ||
                       (writer_enabled.load() && !g_pending_events.empty());
            });
            if (g_shutdown_requested.load() || stop_requested.load()) {
                return;
            }
            event = std::move(g_pending_events.front());
            g_pending_events.pop_front();
        }

        if (!WriteAll(pipe, event)) {
            {
                const std::lock_guard event_lock(g_event_mutex);
                g_pending_events.push_front(std::move(event));
            }
            return;
        }
    }
}

bool HandleFrame(
    HANDLE pipe,
    std::string_view frame,
    std::atomic_bool& writer_enabled) {
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(
            frame.data(), frame.data() + frame.size(), &root, &errors) ||
        !root["protocol_version"].isInt() ||
        root["protocol_version"].asInt() != thps3_ap::kProtocolVersion ||
        !root["type"].isString()) {
        return true;
    }

    const std::string type = root["type"].asString();
    const Json::Value& payload = root["payload"];
    if (type == "disconnect") {
        return false;
    }
    if (type == "hello") {
        if (WriteAll(pipe, kHelloAck)) {
            writer_enabled.store(true);
            g_event_ready.notify_all();
        }
    } else if (type == "state_snapshot") {
        thps3_ap::ApplyStateSnapshot(frame);
        if (!g_state_snapshot_logged.exchange(true)) {
            thps3_ap::StartupBreadcrumb("first_state_snapshot");
        }
        THPS3AP_SetCollectibleMarkersEnabled(
            payload["collectible_markers_enabled"].isBool() &&
            payload["collectible_markers_enabled"].asBool());
        THPS3AP_RequestLevelMenuRefresh();
        WriteAll(pipe, kStateAck);
    } else if (type == "display_message" && payload["text"].isString()) {
        thps3_ap::QueueDisplayMessage(
            payload["text"].asString(),
            payload["more"].isBool() && payload["more"].asBool());
    } else if (type == "error" && payload["message"].isString()) {
        thps3_ap::SetConnectionError(payload["message"].asString());
    } else if (type == "gap_menu" && payload["title"].isString()) {
        std::array<std::string, 106> rows;
        std::array<const char*, 106> row_pointers{};
        std::array<std::uint32_t, 106> checksums{};
        std::size_t row_count = 0;
        for (; row_count < rows.size(); ++row_count) {
            char key[16];
            std::snprintf(
                key, sizeof(key), "row_%03u",
                static_cast<unsigned>(row_count + 1));
            if (!payload[key].isString()) {
                break;
            }
            rows[row_count] = payload[key].asString();
            row_pointers[row_count] = rows[row_count].c_str();
            std::snprintf(
                key, sizeof(key), "checksum_%03u",
                static_cast<unsigned>(row_count + 1));
            checksums[row_count] =
                payload[key].isUInt() ? payload[key].asUInt() : 0;
        }
        THPS3AP_SetGapMenuData(
            payload["title"].asCString(), row_pointers.data(),
            checksums.data(), static_cast<std::uint32_t>(row_count));
    }
    return true;
}
void ServeClient(HANDLE pipe) {
    std::atomic_bool stop_writer = false;
    std::atomic_bool writer_enabled = false;
    std::thread writer(
        WritePendingEvents,
        pipe,
        std::ref(stop_writer),
        std::ref(writer_enabled));
    std::string pending;
    pending.reserve(4096);
    char buffer[4096];

    while (!g_shutdown_requested.load()) {
        DWORD bytes_read = 0;
        if (!ReadPipe(pipe, buffer, sizeof(buffer), bytes_read) ||
            bytes_read == 0) {
            break;
        }

        pending.append(buffer, bytes_read);
        while (true) {
            const std::size_t newline = pending.find('\n');
            if (newline == std::string::npos) {
                break;
            }
            if (!HandleFrame(
                    pipe,
                    std::string_view(pending).substr(0, newline),
                    writer_enabled)) {
                stop_writer.store(true);
                g_event_ready.notify_all();
                writer.join();
                return;
            }
            pending.erase(0, newline + 1);
        }

        if (pending.size() > 1024 * 1024) {
            break;
        }
    }

    stop_writer.store(true);
    g_event_ready.notify_all();
    writer.join();
}

}  // namespace

namespace thps3_ap {

void StartupBreadcrumb(const char* phase) {
    WriteStartupBreadcrumb(phase);
}

DWORD RunBridgeRuntimeForTest(const wchar_t* pipe_name) {
    g_shutdown_requested.store(false);

    PSECURITY_DESCRIPTOR pipe_descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;GA;;;SY)(A;;GA;;;OW)S:(ML;;NW;;;ME)",
            SDDL_REVISION_1,
            &pipe_descriptor,
            nullptr)) {
        return GetLastError();
    }
    SECURITY_ATTRIBUTES pipe_security{
        sizeof(SECURITY_ATTRIBUTES), pipe_descriptor, FALSE};

    while (!g_shutdown_requested.load()) {
        HANDLE pipe = CreateNamedPipeW(
            pipe_name,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            64 * 1024,
            64 * 1024,
            0,
            &pipe_security
        );
        if (pipe == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            LocalFree(pipe_descriptor);
            return error;
        }
        StartupBreadcrumb("pipe_ready");

        g_active_pipe.store(pipe);
        const bool connected = ConnectPipe(pipe);
        if (connected && !g_shutdown_requested.load()) {
            StartupBreadcrumb("pipe_client_connected");
            ServeClient(pipe);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        g_active_pipe.store(INVALID_HANDLE_VALUE);
    }
    LocalFree(pipe_descriptor);
    return 0;
}

DWORD WINAPI RunBridgeRuntime(void*) {
    StartupBreadcrumb("bridge_thread_enter");
    return RunBridgeRuntimeForTest(kPipeName);
}

void RequestBridgeShutdown() {
    g_shutdown_requested.store(true);
    g_event_ready.notify_all();
    const HANDLE pipe = g_active_pipe.load();
    if (pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipe, nullptr);
    }
}

void QueueStatAssignment(const std::array<std::uint32_t, 10>& assignment) {
    const std::uint32_t sequence = g_event_sequence.fetch_add(1);
    std::string values = "[";
    for (const std::uint32_t value : assignment) {
        if (values.size() > 1) {
            values += ',';
        }
        values += std::to_string(value);
    }
    values += ']';
    const std::string event =
        R"({"payload":{"stat_assignment":)" + values +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(sequence) + R"(,"type":"game_state"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_all();
}

void QueueScoreBonusItemsApplied(std::uint32_t count) {
    const std::string event =
        R"({"payload":{"score_bonus_items_applied":)" + std::to_string(count) +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"game_state"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_all();
}

void QueueAppearance(std::string appearance) {
    const std::string event =
        R"({"payload":{"appearance":")" + appearance +
        R"("},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"game_state"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_all();
}

void QueueCollectibleMarkersEnabled(bool enabled) {
    const std::string event =
        R"({"payload":{"collectible_markers_enabled":)" +
        std::string(enabled ? "true" : "false") +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"game_state"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_all();
}

}  // namespace thps3_ap

extern "C" void THPS3AP_QueueStatPointEvent(int level_number, int point_id) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size()) ||
        point_id < 1 || point_id > 5) {
        return;
    }

    const unsigned bit_index =
        static_cast<unsigned>((level_number - 1) * 5 + (point_id - 1));
    const std::uint64_t bit = std::uint64_t{1} << bit_index;
    const std::uint64_t previous = g_reported_stat_points.fetch_or(bit);
    if ((previous & bit) != 0) {
        return;
    }

    const std::string event =
        R"({"payload":{"kind":"stat_point","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"(","point_id":)" + std::to_string(point_id) +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}

extern "C" void THPS3AP_QueueGoalEvent(int level_number, int goal_id) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size()) ||
        goal_id < 0 || goal_id >= 32 ||
        (thps3_ap::OwnedObjectiveCareerBits(level_number) &
            (1u << goal_id)) == 0) {
        return;
    }

    auto& reported = g_reported_goals[
        static_cast<std::size_t>(level_number - 1)];
    const std::uint32_t bit = 1u << goal_id;
    const std::uint32_t previous = reported.fetch_or(
        bit, std::memory_order_acq_rel);
    if ((previous & bit) != 0) {
        return;
    }

    const std::string event =
        R"({"payload":{"kind":"goal","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"(","goal_id":)" + std::to_string(goal_id) +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}

extern "C" void THPS3AP_RecordMedalResult(int level_number, int medal_id) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size()) ||
        medal_id < 0 || medal_id > 2 ||
        thps3_ap::OwnedObjectiveCareerBits(level_number) != 0x7u) {
        return;
    }
    const std::uint32_t bit = 1u << medal_id;
    if ((g_reported_medal_results[
            static_cast<std::size_t>(level_number - 1)].fetch_or(bit) & bit) != 0) {
        return;
    }
    const std::string event =
        R"({"payload":{"kind":"medal_result","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"(","medal_id":)" + std::to_string(medal_id) +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}

extern "C" void THPS3AP_QueueDeckEvent(int level_number) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size())) {
        return;
    }

    const std::uint32_t bit = 1u << (level_number - 1);
    const std::uint32_t previous = g_reported_decks.fetch_or(
        bit, std::memory_order_acq_rel);
    if ((previous & bit) != 0) {
        return;
    }

    const std::string event =
        R"({"payload":{"kind":"deck","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"("},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}

extern "C" void THPS3AP_QueueGapListRequest(int level_number) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size())) {
        return;
    }

    const std::string event =
        R"({"payload":{"kind":"gap_list","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"("},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}

extern "C" void THPS3AP_QueueGapEvent(
    int level_number,
    std::uint32_t checksum,
    std::uint32_t skater_state,
    std::uint32_t trigger_type) {
    if (level_number < 1 ||
        level_number > static_cast<int>(kLevelKeys.size())) {
        return;
    }

    const std::string event =
        R"({"payload":{"kind":"gap","level":")" +
        std::string(kLevelKeys[static_cast<std::size_t>(level_number - 1)]) +
        R"(","checksum":)" + std::to_string(checksum) +
        R"(,"skater_state":)" + std::to_string(skater_state) +
        R"(,"skater_state_name":")" +
        std::string(SkaterStateName(skater_state)) +
        R"(","trigger_type":)" + std::to_string(trigger_type) +
        R"(,"trigger_type_name":")" +
        std::string(TriggerTypeName(trigger_type)) +
        R"(")" +
        R"(},"protocol_version":1,"sequence":)" +
        std::to_string(g_event_sequence.fetch_add(1)) +
        R"(,"type":"location_event"})" + "\n";
    {
        const std::lock_guard event_lock(g_event_mutex);
        g_pending_events.push_back(event);
    }
    g_event_ready.notify_one();
}
