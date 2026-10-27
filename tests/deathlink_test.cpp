#include "Archipelago.h"
#include "thps3_ap/deathlink.hpp"
#include "thps3_ap/game_state.hpp"
#include "thps3_ap/console.hpp"
#include <cassert>
#include <json/json.h>
#include <limits>

extern bool enable_deathlink;
extern std::string ap_player_name;
extern bool parse_response(std::string, std::string &);

int main() {
  thps3_ap::QueueDisplayMessage("Startup message");
  assert(thps3_ap::InGameConsole().popups.Empty());
  assert(thps3_ap::InGameConsole().lines.back() == "Startup message");
  thps3_ap::ConsoleMainMenuReady();
  thps3_ap::DeathLinkState state;
  auto bail = [&] {
    state.ObserveBail(false, true);
    state.ObserveBail(true, true);
  };
  bail();
  assert(!state.TakeSend() && !state.Receive());
  state.SetEnabled(true);
  bail();
  assert(!state.TakeSend() && !state.Receive());
  state.SetConnected(true);
  bail();
  assert(state.TakeSend());
  for (int i = 0; i < 100; ++i)
    state.ObserveBail(true, true);
  assert(!state.TakeSend());
  bail();
  assert(state.SetAmnesty("3"));
  assert(state.TakeSend());
  for (const auto *invalid : {"", "0", "-1", "1.5", "3x", " 3", "4294967296"}) {
    assert(!state.SetAmnesty(invalid) && state.Amnesty() == 3);
  }
  for (int round = 0; round < 2; ++round) {
    bail();
    assert(!state.TakeSend());
    bail();
    assert(!state.TakeSend());
    bail();
    assert(state.TakeSend() && !state.TakeSend());
  }
  bail();
  bail();
  assert(state.Receive());
  state.ObserveBail(false, false);
  assert(state.BeginReceivedBail(false));
  state.ObserveBail(false, true);
  state.ObserveBail(true, true);
  assert(!state.TakeSend());
  assert(state.Receive() && !state.BeginReceivedBail(true));
  bail();
  assert(state.TakeSend());
  assert(state.Receive());
  state.SetConnected(false);
  assert(!state.BeginReceivedBail(false) && !state.TakeSend());
  state.SetConnected(true);
  bail();
  assert(!state.TakeSend());
  assert(state.SetAmnesty("1"));
  bail();
  assert(state.TakeSend());
  assert(state.Receive());
  state.SetEnabled(false);
  assert(!state.BeginReceivedBail(false) && !state.TakeSend());

  enable_deathlink = true;
  ap_player_name = "THPS3";
  thps3_ap::DeathLink().SetEnabled(true);
  thps3_ap::DeathLink().SetConnected(true);
  thps3_ap::InitializeDeathLink("THPS3");
  int received = 0;
  AP_RegisterBouncedCallback([&](AP_Bounce bounce) {
    if (thps3_ap::ReceiveDeathLink(bounce, "THPS3"))
      ++received;
  });
  Json::Value packet;
  packet[0]["cmd"] = "Bounced";
  packet[0]["tags"][0] = "DeathLink";
  auto &data = packet[0]["data"];
  data["source"] = "Other";
  data["cause"] = "Other fell.";
  data["time"] = 1000.25;
  const auto deliver = [&] {
    std::string response;
    parse_response(Json::writeString(Json::StreamWriterBuilder{}, packet),
                   response);
    assert(!AP_DeathLinkPending());
  };
  deliver();
  assert(received == 1);
  deliver();
  assert(received == 1);
  data["time"] = 999.0;
  deliver();
  assert(received == 1);
  data["time"] = 1000.5;
  deliver();
  assert(received == 2);
  data["source"] = "THPS3";
  deliver();
  assert(received == 2);
  data["source"] = 1;
  deliver();
  assert(received == 2);
  data["source"] = "Other";
  for (const auto &invalid :
       {Json::Value{}, Json::Value("invalid"), Json::Value(-1), Json::Value(0),
        Json::Value(std::numeric_limits<double>::infinity())}) {
    data["time"] = invalid;
    deliver();
    assert(received == 2);
  }
  data["time"] = 2000.0;
  data["cause"] = 1;
  deliver();
  assert(received == 2);
  data["cause"] = "Other fell.";
  packet[0]["tags"] = "DeathLink";
  deliver();
  assert(received == 2);
  packet[0]["tags"] = Json::arrayValue;
  packet[0]["tags"][0] = 123;
  deliver();
  assert(received == 2);
  packet[0]["tags"][0] = "DeathLink";
  enable_deathlink = false;
  thps3_ap::DeathLink().SetEnabled(false);
  deliver();
  assert(received == 2);
  enable_deathlink = true;
  thps3_ap::DeathLink().SetEnabled(true);
  deliver();
  assert(received == 3);
  std::string message;
  bool more = false;
  for (int i = 0; i < 3; ++i) {
    assert(thps3_ap::TakeDisplayMessage(message, more));
    assert(message == "DeathLink: Other fell." && !more);
  }
  assert(!thps3_ap::TakeDisplayMessage(message, more));
  thps3_ap::SendDeathLink("THPS3");
  assert(thps3_ap::TakeDisplayMessage(message, more));
  assert(message == "DeathLink sent: THPS3 bailed." && !more);
  assert(thps3_ap::InGameConsole().lines.back() == message);
  thps3_ap::InGameConsole().open = true;
  thps3_ap::SendDeathLink("THPS3");
  assert(!thps3_ap::TakeDisplayMessage(message, more));
  assert(thps3_ap::InGameConsole().lines.back() == "DeathLink sent: THPS3 bailed.");
}
