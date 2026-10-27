#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace thps3_ap {

void DrawHudText(void *device, std::string_view counter, std::string_view gap);

inline std::size_t PreviousCharacter(const std::string &text, std::size_t at) {
  if (at)
    --at;
  while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
    --at;
  return at;
}
inline std::size_t NextCharacter(std::string_view text, std::size_t at) {
  if (at < text.size())
    ++at;
  while (at < text.size() &&
         (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
    ++at;
  return at;
}
struct ConsoleInput {
  std::string text;
  std::size_t cursor = 0, anchor = 0;
  void Set(std::string value) {
    text.clear();
    cursor = anchor = 0;
    Insert(std::move(value));
  }
  void Move(int direction, bool selecting) {
    if (!selecting && cursor != anchor)
      cursor = direction < 0 ? (std::min)(cursor, anchor)
                             : (std::max)(cursor, anchor);
    else
      cursor = direction < 0 ? PreviousCharacter(text, cursor)
                             : NextCharacter(text, cursor);
    if (!selecting)
      anchor = cursor;
  }
  void Edge(bool end, bool selecting) {
    cursor = end ? text.size() : 0;
    if (!selecting)
      anchor = cursor;
  }
  std::string Selected() const {
    return text.substr((std::min)(cursor, anchor),
                       (std::max)(cursor, anchor) - (std::min)(cursor, anchor));
  }
  void Insert(std::string value) {
    for (auto &c : value)
      if (c == '\r' || c == '\n' || c == '\t')
        c = ' ';
    value.erase(
        std::remove_if(value.begin(), value.end(),
                       [](unsigned char c) { return c < 32 || c == 127; }),
        value.end());
    if (value.empty())
      return;
    const auto first = (std::min)(cursor, anchor),
               last = (std::max)(cursor, anchor);
    auto size = (std::min)(value.size(), 1000 - (text.size() - (last - first)));
    while (size < value.size() && size &&
           (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80)
      --size;
    text.replace(first, last - first, value, 0, size);
    cursor = anchor = first + size;
  }
  void Erase(bool backwards) {
    auto first = (std::min)(cursor, anchor), last = (std::max)(cursor, anchor);
    if (first == last) {
      if (backwards)
        first = PreviousCharacter(text, first);
      else
        last = NextCharacter(text, last);
    }
    text.erase(first, last - first);
    cursor = anchor = first;
  }
};
inline std::string ConsoleLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}
inline bool SecretConsoleInput(std::string_view text) {
  return text.starts_with("/password") || text.starts_with("!admin login");
}
inline const std::vector<std::string> &ConsoleCommands() {
  static const std::vector<std::string> commands{"/help",
                                                 "/server",
                                                 "/port",
                                                 "/slot",
                                                 "/password",
                                                 "/connect",
                                                 "/disconnect",
                                                 "/status",
                                                 "/quit",
                                                 "/markers",
                                                 "/gapmarkers",
                                                 "/outoflogic",
                                                 "/collectibles",
                                                 "/gaps",
                                                 "/geometry",
                                                 "/clear",
                                                 "/deathlink_amnesty",
                                                 "!help",
                                                 "!license",
                                                 "!options",
                                                 "!admin",
                                                 "!players",
                                                 "!getitem",
                                                 "!hint",
                                                 "!hint_location",
                                                 "!remaining",
                                                 "!missing",
                                                 "!checked",
                                                 "!status",
                                                 "!release",
                                                 "!collect",
                                                 "!countdown",
                                                 "!alias"};
  return commands;
}
inline std::vector<std::string>
CompleteConsole(const std::string &input, const std::vector<std::string> &items,
                const std::vector<std::string> &locations) {
  static const std::vector<std::string> switches{"off", "on", "toggle"},
      gaps{"all", "next", "off"};
  static const std::vector<std::string> modes{"bounds", "tint", "off", "both"};
  std::string prefix;
  const auto *names = &ConsoleCommands();
  const auto lower = ConsoleLower(input);
  if (lower.starts_with("!hint_location ")) {
    prefix = "!hint_location ";
    names = &locations;
  } else if (lower.starts_with("!hint ")) {
    prefix = "!hint ";
    names = &items;
  } else if (lower.starts_with("/gaps ")) {
    prefix = "/gaps ";
    names = &gaps;
  } else if (lower.starts_with("/gapmarkers ")) {
    prefix = "/gapmarkers ";
    names = &modes;
  } else if (lower.starts_with("/outoflogic ")) {
    prefix = "/outoflogic ";
    names = &switches;
  } else {
    for (const auto *command : {"/markers ", "/collectibles ", "/geometry "})
      if (lower.starts_with(command)) {
        prefix = command;
        names = &switches;
        break;
      }
    if (prefix.empty() && input.find(' ') != std::string::npos)
      return {};
  }
  const auto needle = lower.substr(prefix.size());
  std::vector<std::string> result;
  for (const auto &name : *names)
    if (ConsoleLower(name).starts_with(needle))
      result.push_back(prefix + name);
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}
inline bool ValidConsoleServer(std::string server) {
  if (server.starts_with("wss://"))
    server.erase(0, 6);
  else if (server.starts_with("ws://"))
    server.erase(0, 5);
  if (server.empty() || server.size() > 240 ||
      server.find_first_of("/\\ \t\r\n@?#") != std::string::npos)
    return false;
  if (std::any_of(server.begin(), server.end(),
                  [](unsigned char c) { return c < 32 || c == 127; }))
    return false;
  const auto colon = server.rfind(':');
  if (colon == std::string::npos)
    return server.find_first_of("[]") == std::string::npos;
  if (!colon || colon + 1 == server.size())
    return false;
  const auto host = server.substr(0, colon), port = server.substr(colon + 1);
  if (host.find(':') != std::string::npos &&
      (host.front() != '[' || host.back() != ']'))
    return false;
  if (port.size() > 5 ||
      !std::all_of(port.begin(), port.end(),
                   [](unsigned char c) { return std::isdigit(c); }))
    return false;
  const auto value = std::stoul(port);
  return value > 0 && value <= 65535;
}
inline std::string ConsoleServerWithPort(std::string server,
                                         std::string_view port) {
  const auto scheme = server.find("://");
  const auto start = scheme == std::string::npos ? 0 : scheme + 3;
  const auto close = server.find(']', start);
  const auto colon = server.rfind(':');
  if (colon != std::string::npos && colon >= start &&
      (close == std::string::npos || colon > close))
    server.erase(colon);
  return server + ':' + std::string(port);
}
enum class ConsoleMarkerMode { Both, Bounds, Tint, Off };
inline int ConsolePopupAlpha(std::uint64_t until, std::uint64_t now) {
  return now >= until
             ? 0
             : static_cast<int>(
                   255 * (std::min)(std::uint64_t{750}, until - now) / 750);
}
struct ConsolePopupQueue {
  struct Line {
    std::string text;
    std::uint64_t until;
  };
  std::deque<std::pair<std::string, bool>> waiting;
  std::deque<Line> visible;
  bool Empty() const { return waiting.empty() && visible.empty(); }
  void Clear() {
    waiting.clear();
    visible.clear();
  }
  void Add(std::string_view text, bool more = false) {
    if (!text.empty() && waiting.size() < 4096)
      waiting.emplace_back(text, more);
  }
  bool Take(std::string &text, bool &more) {
    if (waiting.empty())
      return false;
    text = std::move(waiting.front().first);
    more = waiting.front().second;
    waiting.pop_front();
    return true;
  }
  void Advance(
      std::uint64_t now,
      const std::function<std::vector<std::string>(std::string_view)> &wrap) {
    while (!visible.empty() && visible.front().until <= now)
      visible.pop_front();
    while (visible.size() < 4 && !waiting.empty()) {
      auto rows = wrap(waiting.front().first);
      waiting.pop_front();
      for (auto i = rows.size(); i > 1; --i)
        waiting.emplace_front(std::move(rows[i - 1]), false);
      if (!rows.empty())
        visible.push_back({std::move(rows.front()), now + 3750});
    }
  }
};
struct ConsoleState {
  std::mutex mutex;
  std::deque<std::string> lines, commands;
  ConsolePopupQueue popups;
  std::vector<std::string> items, locations;
  std::string status = "connecting";
  std::string config_path;
  std::size_t scroll = 0;
  std::uint64_t revision = 0;
  bool open = false, hud = true, popups_ready = false;
  bool out_of_logic = false;
  ConsoleMarkerMode marker_mode = ConsoleMarkerMode::Both;
  std::ofstream log;
  ConsoleState() {
    try {
      const auto time =
          std::chrono::system_clock::now().time_since_epoch().count();
      log.open(std::filesystem::temp_directory_path() /
               ("thps3-ap-console-" + std::to_string(time) + ".log"));
    } catch (const std::exception &) { /* logging must not prevent startup. */
    }
  }
  void WriteLog(std::string_view text) {
    if (log) {
      log << text << '\n';
      log.flush();
    }
  }
  void Append(std::string_view text, bool notify = true) {
    WriteLog(text);
    if (notify && !open && popups_ready)
      popups.Add(text);
    for (std::size_t start = 0; start < text.size();) {
      auto end = text.find('\n', start);
      if (end == std::string_view::npos)
        end = text.size();
      end = (std::min)(end, start + 4096);
      while (end < text.size() && end > start &&
             (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
      if (end == start)
        ++end;
      lines.emplace_back(text.substr(start, end - start));
      if (lines.size() > 4096)
        lines.pop_front();
      if (scroll)
        scroll = (std::min)(scroll + 1, lines.size() - 1);
      start = end + (end < text.size() && text[end] == '\n' ? 1 : 0);
    }
    ++revision;
  }
};
inline ConsoleState &InGameConsole() {
  static ConsoleState state;
  return state;
}
inline void ConsoleMainMenuReady() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  if (!state.popups_ready) {
    state.popups.Clear();
    state.popups_ready = true;
  }
}
inline void ConsoleLog(std::string_view text, bool notify = true) {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  state.Append(text, notify);
}
inline void ConsoleDebug(std::string_view text) {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  state.WriteLog(text);
}
inline bool ConsoleIsOpen() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  return state.open;
}
inline bool ConsoleHudVisible() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  return state.hud && !state.open;
}
inline ConsoleMarkerMode ConsoleGapMarkerMode() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  return state.marker_mode;
}
inline bool ConsoleOutOfLogic() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  return state.out_of_logic;
}
inline std::deque<std::string> TakeConsoleCommands() {
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  std::deque<std::string> result;
  result.swap(state.commands);
  return result;
}

} // namespace thps3_ap
