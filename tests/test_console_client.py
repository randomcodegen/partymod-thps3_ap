"""Run the real command handler with a server stub."""

from pathlib import Path
import subprocess
import tempfile


def test_console_client_commands():
    root = Path(__file__).parents[1]
    source = (root / "native_client/main.cpp").read_text()
    handler = source.split("    const auto console_commands = [&] {", 1)[1].split(
        "    std::uint64_t sequence = 0;", 1
    )[0]
    handler = "const auto console_commands = [&] {" + handler
    gap_menu = (
        "Json::Value GapMenu("
        + source.split("Json::Value GapMenu(", 1)[1].split(
            "void RequestStorageIfReady()", 1
        )[0]
    )
    formatter = (
        "constexpr std::string_view ItemColor("
        + source.split("constexpr std::string_view ItemColor(", 1)[1].split(
            "void SendMessages", 1
        )[0]
    )
    trick_permissions = (
        "    const std::pair<const char*, const char*> tricks[] = {"
        + source.split("    const std::pair<const char*, const char*> tricks[] = {", 1)[1].split(
            "    std::map<std::string, std::uint32_t> objectives", 1
        )[0]
    )
    apcpp = (root / "thirdparty/APCpp/Archipelago.cpp").read_text()
    item_names = (
        "std::vector<std::string> AP_GetItemNames() {"
        + apcpp.split("std::vector<std::string> AP_GetItemNames() {", 1)[1].split(
            "void AP_SendBounce", 1
        )[0]
    )
    refresh_names = (
        "void RefreshConsoleNames() {"
        + source.split("void RefreshConsoleNames() {", 1)[1].split(
            "void SaveConsoleConnection", 1
        )[0]
    )
    parser = (
        'for (auto itr : root[i]["data"]) {'
        + apcpp.split('for (auto itr : root[i]["data"]) {', 1)[1].split(
            "                messageQueue.push_back(msg);", 1
        )[0]
    )
    code = (
        r"""
#include "thps3_ap/console.hpp"
#include "Archipelago.h"
#include <cassert>
#include <set>
#include <map>
#include <ranges>
#include <chrono>
#include <charconv>
#include <limits>
namespace Json {
enum { objectValue };
struct Value {
    std::map<std::string,Value> fields; std::vector<Value> entries; std::string text; long long number = 0;
    bool string = false, integer = false;
    Value() {} Value(int n) : number(n), integer(true) {} Value(const char* s) : text(s), string(true) {} Value(std::string s) : text(std::move(s)), string(true) {}
    Value& operator[](const std::string& key) { return fields[key]; }
    const Value& operator[](const std::string& key) const {
        static const Value empty; auto at = fields.find(key); return at == fields.end() ? empty : at->second;
    }
    std::string asString() const { return text; } long long asInt64() const { return number; }
    int asInt() const { return static_cast<int>(number); }
    bool isString() const { return string; } bool isIntegral() const { return integer; } bool isInt() const { return integer; }
    bool isInt64() const { return integer; } bool asBool() const { return number != 0; }
    Value get(const std::string& key, Value fallback) const { auto at = fields.find(key); return at == fields.end() ? fallback : at->second; }
    Value& operator[](std::size_t i) { return entries.at(i); }
    auto begin() const { return entries.begin(); } auto end() const { return entries.end(); }
};
}
struct { std::mutex mutex; Json::Value slot_data; std::set<long long> pending_locations; } g_state;
AP_ConnectionStatus status = AP_ConnectionStatus::Disconnected;
int connections = 0, disconnects = 0;
std::string connected_server, connected_slot, connected_password;
std::vector<std::string> chat;
AP_ConnectionStatus AP_GetConnectionStatus() { return status; }
int AP_GetPlayerID() { return 1; }
void AP_Shutdown() { ++disconnects; status = AP_ConnectionStatus::Disconnected; }
void AP_Say(std::string text) { chat.push_back(std::move(text)); }
std::string_view ConnectionStatusName(AP_ConnectionStatus value) {
    return value == AP_ConnectionStatus::Authenticated ? "authenticated" : "disconnected";
}
namespace thps3_ap { void SetConnectionError(std::string) {} }
void InitializeConnection(const std::string& server, const std::string& slot, const std::string& password) {
    ++connections; connected_server = server; connected_slot = slot; connected_password = password;
    status = AP_ConnectionStatus::Authenticated;
}
std::map<std::string,int> ItemCounts(const std::vector<std::string>& items) {
    std::map<std::string,int> counts; for (const auto& name : items) ++counts[name]; return counts;
}
bool GapInLogic(const Json::Value& gap, const std::map<std::string,int>& items) { return items.contains(gap["need"].asString()); }
int ap_player_id = 1;
std::recursive_mutex state_mutex;
bool shutting_down = false, init = true;
std::string ap_game = "THPS3";
std::map<std::pair<std::string,std::int64_t>,std::string> map_item_id_name;
AP_NetworkPlayer getPlayer(int, int id) { return {0,id,"Player" + std::to_string(id),"Alias" + std::to_string(id),"THPS3"}; }
std::string getItemName(const std::string&, std::int64_t id) { return "Item " + std::to_string(id); }
std::string getLocationName(const std::string&, std::int64_t id) { return "Location " + std::to_string(id); }
std::string AP_GetLocationName(std::int64_t id) { return getLocationName(ap_game, id); }
AP_DataPackageSyncStatus AP_GetDataPackageStatus() { return AP_DataPackageSyncStatus::Synced; }
AP_Message ParseParts(Json::Value root) {
    AP_Message result; auto* msg = &result; std::size_t i = 0;
"""
        + parser
        + r"""
    return result;
}
"""
        + formatter
        + gap_menu
        + item_names
        + refresh_names
        + r"""
Json::Value TrickPermissions(const std::vector<std::string>& items) {
    auto counts = ItemCounts(items);
    Json::Value payload;
"""
        + trick_permissions
        + r"""
    return payload["trick_permissions"];
}
int main() {
    assert(!TrickPermissions({})["wallride"].asBool());
    assert(!TrickPermissions({"Grind Tricks"})["wallride"].asBool());
    assert(!TrickPermissions({"Lip Tricks"})["wallride"].asBool());
    assert(!TrickPermissions({"Grind Tricks", "Lip Tricks"})["wallride"].asBool());
    const auto wallrides = TrickPermissions({"Wallrides"});
    assert(wallrides["wallride"].asBool() && !wallrides["grind"].asBool() && !wallrides["lip"].asBool());
    std::string server = "localhost:1", slot_name = "Player1", password;
    std::string staged_server = server, staged_slot = slot_name, last_connection_error;
    bool profile_bound = false;
    auto next_names_refresh = std::chrono::steady_clock::now();
    """
        + handler
        + r"""
    const auto execute = [&](std::string command) {
        thps3_ap::InGameConsole().commands.push_back(std::move(command)); console_commands();
    };
    execute("/server ws://[::1]:1"); execute("/port 38281");
    execute("/slot Test Player"); execute("/password secret-one"); execute("/connect");
    assert(connections == 1 && connected_server == "ws://[::1]:38281");
    assert(connected_slot == "Test Player" && connected_password == "secret-one");
    execute("/port 0"); execute("/connect");
    assert(connections == 2 && connected_server == "ws://[::1]:38281");
    profile_bound = true;
    execute("/server other:123"); execute("/connect");
    assert(connections == 2 && disconnects == 2 && server == "ws://[::1]:38281");
    execute("/server ws://[::1]:38281"); execute("/slot other"); execute("/connect");
    assert(connections == 2 && slot_name == "Test Player");
    execute("/slot Test Player");
    g_state.pending_locations.insert(42); execute("/disconnect");
    assert(g_state.pending_locations.contains(42) && status == AP_ConnectionStatus::Disconnected);
    execute("!hint Foundry Access"); assert(chat.empty());
    execute("/password secret-two"); execute("/connect");
    assert(connections == 3 && connected_password == "secret-two" && g_state.pending_locations.contains(42));
    execute("hello skaters"); execute("!hint Foundry Access");
    assert((chat == std::vector<std::string>{"hello skaters", "!hint Foundry Access"}));
    execute("/status"); execute("/help"); execute("/unknown");
    for (const auto& line : thps3_ap::InGameConsole().lines)
        assert(line.find("secret-one") == std::string::npos && line.find("secret-two") == std::string::npos);
    Json::Value slot; slot["levels"]["foundry"]["name"] = "Foundry";
    for (int i = 1; i <= 3; ++i) {
        Json::Value gap; gap["level"] = "foundry"; gap["location_id"] = i; gap["checksum"] = i;
        gap["need"] = i == 2 ? "Grind Tricks" : "Flip Tricks";
        gap["name"] = std::string("Gap ") + std::to_string(i); slot["active_gaps"].entries.push_back(gap);
    }
    const auto normal = GapMenu(slot, {"Flip Tricks"}, {1}, "foundry");
    assert(normal["count"].asInt64() == 1 && normal["row_001"].asString() == "Gap 3");
    thps3_ap::InGameConsole().out_of_logic = true;
    const auto outside = GapMenu(slot, {"Flip Tricks"}, {1}, "foundry");
    assert(outside["count"].asInt64() == 2 && outside["row_001"].asString() == "Gap 2");
    assert(outside["title"].asString() == normal["title"].asString());
    map_item_id_name = {{{"THPS3",100},"Foundry Access"}, {{"Other game",101},"Other item"}};
    g_state.slot_data = slot; RefreshConsoleNames();
    assert(thps3_ap::InGameConsole().items == std::vector<std::string>{"Foundry Access"});
    assert(thps3_ap::InGameConsole().locations.size() == 3);
    init = false; assert(AP_GetItemNames().empty()); init = true;
    Json::Value packet;
    for (const auto& [type,text] : std::vector<std::pair<std::string,std::string>>{
        {"player_id","1"}, {"text"," found "}, {"item_id","100"}, {"text"," at "}, {"location_id","200"}}) {
        Json::Value part; part["type"] = type; part["text"] = text; part["flags"] = 1; part["player"] = 1;
        packet["data"].entries.push_back(part);
    }
    Json::Value packets; packets.entries.push_back(packet);
    auto message = ParseParts(packets);
    assert(message.text == "Alias1 found Item 100 at Location 200");
    assert(message.messageParts[0].player == 1 && message.messageParts[2].flags == 1);
    assert(FormatItemMessage(message, 0, "Player1") == "&C7Alias1&C3 found &C1Item 100&C3 at &C6Location 200");
    packets.entries[0]["data"].entries[0]["text"] = "99999999999999999999999";
    packets.entries[0]["data"].entries[2]["text"] = "100junk";
    message = ParseParts(packets);
    assert(message.messageParts[0].type == AP_NormalText && message.messageParts[2].text == "100junk");
}
"""
    )
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "console_client.cpp"
        executable = Path(directory) / "console_client.exe"
        cpp.write_text(code)
        subprocess.run(
            [
                "g++",
                "-std=c++20",
                "-I",
                str(root / "bridge/include"),
                "-I",
                str(root / "thirdparty/APCpp"),
                str(cpp),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)
