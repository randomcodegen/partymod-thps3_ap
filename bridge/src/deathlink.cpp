#include "thps3_ap/deathlink.hpp"
#include "thps3_ap/console.hpp"
#include "thps3_ap/game_state.hpp"
#include "Archipelago.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <json/json.h>
#include <map>
#include <memory>

namespace {
std::mutex packet_mutex;
std::map<std::string, double> received_times;
}

namespace thps3_ap {

void InitializeDeathLink(std::string slot_name) {
    {
        const std::lock_guard lock(packet_mutex);
        received_times.clear();
    }
    AP_SetDeathLinkSupported(true);
    AP_RegisterBouncedCallback([slot_name = std::move(slot_name)](AP_Bounce bounce) {
        ReceiveDeathLink(bounce, slot_name);
    });
}

bool ReceiveDeathLink(const AP_Bounce& bounce, std::string_view own_slot) {
    if (!bounce.tags || std::find(bounce.tags->begin(), bounce.tags->end(), "DeathLink") == bounce.tags->end())
        return false;
    Json::Value data;
    std::string error;
    const std::unique_ptr<Json::CharReader> reader(Json::CharReaderBuilder{}.newCharReader());
    if (!reader->parse(bounce.data.data(), bounce.data.data() + bounce.data.size(), &data, &error) ||
        !data.isObject() || !data["source"].isString() || data["source"].asString().empty() ||
        !data["time"].isNumeric() || !std::isfinite(data["time"].asDouble()) ||
        data["time"].asDouble() <= 0 || (!data["cause"].isNull() && !data["cause"].isString()))
        return false;
    const auto source = data["source"].asString();
    if (source == own_slot) return false;
    {
        const std::lock_guard lock(packet_mutex);
        auto& last_time = received_times[source];
        if (data["time"].asDouble() <= last_time || !DeathLink().Receive()) return false;
        last_time = data["time"].asDouble();
    }
    const auto cause = data["cause"].asString();
    QueueDisplayMessage(cause.empty() ? "DeathLink received from " + source + '.' : "DeathLink: " + cause);
    return true;
}

void SendDeathLink(std::string_view source) {
    Json::Value data;
    data["time"] = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    data["source"] = std::string(source);
    data["cause"] = std::string(source) + " bailed.";
    std::vector<std::string> tags{"DeathLink"};
    AP_Bounce bounce;
    bounce.tags = &tags;
    bounce.data = Json::writeString(Json::StreamWriterBuilder{}, data);
    AP_SendBounce(bounce);
    QueueDisplayMessage("DeathLink sent: " + std::string(source) + " bailed.");
}

} // namespace thps3_ap
