#include "../native_client/gap_catalog.hpp"

#include <cassert>
#include <string_view>

int main() {
  using namespace thps3_ap;
  Json::Value selection;
  selection["catalog"] = kGapCatalogFingerprint;
  selection["ids"] = Json::Value(Json::arrayValue);
  assert(ExpandGapSelection(selection).empty());

  selection["ids"].append(30320001);
  selection["ids"].append(30320000);
  const auto gaps = ExpandGapSelection(selection);
  assert(gaps.size() == 2);
  assert(gaps[0]["name"].asString() == "Over the Pipe!");
  assert(gaps[0]["any_items"].empty());
  assert(gaps[1]["level"].asString() == "foundry");
  assert(gaps[1]["location_id"].asInt() == 30320000);
  assert(gaps[1]["checksum"].asUInt() == 0xD4A8BD34u);
  assert(gaps[1]["any_items"][0].asString() == "Grind Tricks");

  selection["ids"].clear();
  selection["ids"].append(30320600);
  selection["ids"].append(30320751);
  selection["ids"].append(30320029);
  const auto routes = ExpandGapSelection(selection);
  assert(routes[0]["name"].asString() == "Passin' Gas!");
  assert(routes[0]["any_items"].size() == 1);
  assert(routes[0]["any_items"][0].asString() == "Wallrides");
  assert(routes[1]["all_items"].size() == 2);
  assert(routes[1]["all_items"][0].asString() == "Grind Tricks");
  assert(routes[1]["all_items"][1].asString() == "Wallrides");
  assert(routes[2]["goal_items"][0].asString() ==
         "Unlock Foundry - Un-Jam 5 Valves");

  const auto rejects = [](const Json::Value &value, std::string_view message) {
    try {
      ExpandGapSelection(value);
    } catch (const std::runtime_error &error) {
      return std::string_view(error.what()).starts_with(message);
    }
    return false;
  };
  assert(rejects(Json::Value{}, "Invalid THPS3 gap selection"));
  selection["ids"][2] = 30320600;
  assert(rejects(selection, "Duplicate location ID"));
  selection["ids"][2] = 30329999;
  assert(rejects(selection, "Unknown location ID"));
  selection["ids"][2] = 30320000.0;
  assert(rejects(selection, "Invalid location ID"));
  selection["catalog"] = "different";
  assert(rejects(selection, "THPS3 gap catalogue mismatch"));
}
