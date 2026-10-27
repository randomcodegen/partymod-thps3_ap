#include "thps3_ap/bridge_c_api.h"
#include "thps3_ap/console.hpp"
#include "thps3_ap/deathlink.hpp"
#include "thps3_ap/game_font.hpp"
#include <SDL.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>

int gaps = 0;
BOOL collectibles = FALSE, geometry = FALSE;
extern "C" BOOL THPS3AP_HasStateSnapshot() { return TRUE; }
extern "C" void THPS3AP_ClearGapHighlights() { gaps = 0; }
extern "C" void THPS3AP_ToggleAllGapHighlights() { gaps = 2; }
extern "C" void THPS3AP_AdvanceGapHighlight() { ++gaps; }
extern "C" BOOL THPS3AP_CollectibleMarkersEnabled() { return collectibles; }
extern "C" void THPS3AP_SetCollectibleMarkersEnabled(BOOL value) {
  collectibles = value;
}
extern "C" BOOL THPS3AP_UseIntactLosAngelesGeometry() { return geometry; }
extern "C" void THPS3AP_ToggleLosAngelesGeometry() { geometry = !geometry; }
extern "C" void THPS3AP_RequestGapMenuRefresh() {}
namespace thps3_ap {
void QueueCollectibleMarkersEnabled(bool value) {
  assert(value == (collectibles != FALSE));
}
} // namespace thps3_ap

namespace {
void Key(SDL_Keycode key, Uint16 mod = 0) {
  SDL_Event event{};
  event.type = SDL_KEYDOWN;
  event.key.keysym.sym = key;
  event.key.keysym.mod = mod;
  assert(THPS3AP_ProcessConsoleEvent(&event));
}
void Text(const char *value) {
  SDL_Event event{};
  event.type = SDL_TEXTINPUT;
  SDL_strlcpy(event.text.text, value, sizeof(event.text.text));
  assert(THPS3AP_ProcessConsoleEvent(&event));
}
void Submit(const char *value) {
  Text(value);
  Key(SDLK_RETURN);
}

void *texture_api[19]{};
struct Texture {
  void **api = texture_api;
} texture;
std::vector<DWORD> pixels;
int texture_width = 0, texture_height = 0, creates = 0, draws = 0, uploads = 0;
float drawn_left = 0, drawn_top = 0, drawn_right = 0;
bool fail_upload = false;
ULONG __stdcall Release(void *) { return 0; }
struct LockedRect {
  int pitch;
  void *bits;
};
HRESULT __stdcall Lock(void *, UINT, LockedRect *rect, const RECT *, DWORD) {
  if (fail_upload) {
    fail_upload = false;
    return E_FAIL;
  }
  rect->pitch = texture_width * 4;
  rect->bits = pixels.data();
  ++uploads;
  return 0;
}
HRESULT __stdcall Unlock(void *, UINT) { return 0; }
HRESULT __stdcall CreateTexture(void *, UINT width, UINT height, UINT levels,
                                DWORD usage, UINT format, UINT pool,
                                void **result) {
  assert(levels == 1 && usage == 0 && format == 21 && pool == 1);
  texture_width = width;
  texture_height = height;
  pixels.resize(width * height);
  *result = &texture;
  ++creates;
  return 0;
}
struct Viewport {
  DWORD x, y, width, height;
  float min_z, max_z;
} viewport{0, 0, 960, 540, 0, 1};
HRESULT __stdcall GetViewport(void *, Viewport *out) {
  *out = viewport;
  return 0;
}
HRESULT __stdcall Scene(void *) { return 0; }
DWORD texture_result = 5, saved_texture_result = 0;
bool specular = true, saved_specular = false;
HRESULT __stdcall State(void *, DWORD state, DWORD value) {
  if (state == 29)
    specular = value != 0;
  return 0;
}
HRESULT __stdcall Stage(void *, DWORD stage, DWORD state, DWORD value) {
  if (stage == 0 && state == 28)
    texture_result = value;
  return 0;
}
HRESULT __stdcall Shader(void *, DWORD) { return 0; }
HRESULT __stdcall Restore(void *, DWORD) {
  texture_result = saved_texture_result;
  specular = saved_specular;
  return 0;
}
HRESULT __stdcall SetTexture(void *, DWORD, void *) { return 0; }
HRESULT __stdcall Block(void *, DWORD, DWORD *result) {
  saved_texture_result = texture_result;
  saved_specular = specular;
  *result = 1;
  return 0;
}
HRESULT __stdcall Draw(void *, DWORD type, UINT count, const void *vertices,
                       UINT stride) {
  assert(type == 4 && count == 2 && stride == 28);
  assert(texture_result == 1 && !specular);
  const auto *first = static_cast<const float *>(vertices);
  drawn_left = first[0];
  drawn_top = first[1];
  drawn_right = first[7];
  ++draws;
  return 0;
}
void SavePreview(const char *name = "console-preview.bmp", int width = 936,
                 int height = 516) {
  BITMAPFILEHEADER file{};
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(info);
  info.biWidth = width;
  info.biHeight = -height;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biSizeImage = width * height * 4;
  file.bfType = 0x4d42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + info.biSizeImage;
  std::ofstream out(name, std::ios::binary);
  out.write(reinterpret_cast<const char *>(&file), sizeof(file));
  out.write(reinterpret_cast<const char *>(&info), sizeof(info));
  for (int row = 0; row < height; ++row)
    out.write(
        reinterpret_cast<const char *>(pixels.data() + row * texture_width),
        width * 4);
}
} // namespace

int main(int argc, char **argv) {
  using namespace thps3_ap;
  std::vector<std::uint8_t> font_bytes(1078);
  const auto put16 = [&](std::size_t at, unsigned value) {
    font_bytes[at] = value & 255;
    font_bytes[at + 1] = value >> 8;
  };
  const auto put32 = [&](std::size_t at, unsigned value) {
    put16(at, value & 65535);
    put16(at + 2, value >> 16);
  };
  put32(0, font_bytes.size());
  put32(4, 1);
  put32(8, 3);
  put32(12, 2);
  put16(16, 1);
  put16(18, 'A');
  put32(20, font_bytes.size() - 20);
  put16(24, 3);
  put16(26, 2);
  put16(28, 8);
  font_bytes[37] = font_bytes[38] = font_bytes[40] = 1;
  font_bytes[41] = 2;
  put32(46, 0xffffffff);
  put32(50, 0xff808080);
  put32(1066, 1);
  put16(1070, 1);
  put16(1074, 2);
  put16(1076, 2);
  GameFont font;
  assert(font.Parse(font_bytes));
  assert(font.width == 3 && font.line_height == 3 && font.baseline == 2);
  assert(font.glyphs['A'].x == 1 && font.glyphs['A'].baseline == 1);
  assert(font.pixels[1] == 0xffffffff && font.pixels[5] == 0xff808080);
  assert(font.glyphs['B'].width == 0);
  const auto smooth = font.Sample(font.glyphs['A'], 0.5f, 0.5f);
  assert(smooth[0] > 223 && smooth[0] < 224 && smooth[3] == 1);
  const auto edge = font.Sample(font.glyphs['A'], -0.5f, 0);
  assert(edge[0] == 127.5f && edge[3] == 0.5f);
  const auto outside = font.Sample(font.glyphs['A'], -1, 0);
  assert(outside[0] == 0 && outside[3] == 0);
  for (std::size_t length : {0, 15, 20, 36, 1066, 1077})
    assert(!font.Parse(std::span(font_bytes).first(length)) &&
           font.pixels.empty());
  for (std::size_t at : {0, 4, 8, 12, 16, 20, 24, 28, 1066, 1070, 1074}) {
    const auto saved = font_bytes[at];
    font_bytes[at] = 255;
    assert(!font.Parse(font_bytes) && font.pixels.empty());
    font_bytes[at] = saved;
  }
  if (argc > 1) {
    std::ifstream file(argv[1], std::ios::binary);
    assert(file);
    font_bytes.assign(std::istreambuf_iterator<char>(file), {});
    assert(font.Parse(font_bytes));
    assert(font.line_height == 15 && font.glyphs['A'].width == 9);
  }
  ConsolePopupQueue queue;
  const auto wrap = [](auto text) {
    return std::vector<std::string>{std::string(text)};
  };
  for (int i = 0; i < 6; ++i)
    queue.Add(std::to_string(i));
  queue.Advance(100, wrap);
  assert(queue.visible.size() == 4 && queue.waiting.size() == 2);
  assert(ConsolePopupAlpha(3850, 3100) == 255);
  assert(ConsolePopupAlpha(3850, 3475) == 127);
  assert(ConsolePopupAlpha(3850, 3850) == 0);
  queue.Advance(3850, wrap);
  assert(queue.visible.size() == 2 && queue.visible.front().text == "4");
  assert(queue.visible.front().until == 7600);
  queue.Advance(7600, wrap);
  assert(queue.Empty());
  ConsoleInput edit;
  edit.Set("a\xc3\xa9\xf0\x9f\x98\x80z");
  edit.Move(-1, false);
  edit.Move(-1, true);
  assert(edit.Selected() == "\xf0\x9f\x98\x80");
  edit.Erase(true);
  edit.Erase(true);
  assert(edit.text == "az");
  edit.Set(std::string(999, 'a'));
  edit.Insert("\xc3\xa9");
  assert(edit.text.size() == 999);
  edit.Insert("b");
  edit.Insert("c");
  assert(edit.text.size() == 1000);
  edit.anchor = 0;
  edit.Insert("one\r\ntwo\t\x01");
  assert(edit.text == "one  two ");
  for (const auto *bad :
       {"", "ws://", "host:0", "host:65536", "host:wat", "host/path",
        "host name:123", "::1:123", "user@host:123"})
    assert(!ValidConsoleServer(bad));
  assert(ValidConsoleServer("localhost:38281") &&
         ValidConsoleServer("ws://[::1]:38281"));
  assert(ConsoleServerWithPort("wss://[::1]:1", "123") == "wss://[::1]:123");
  assert(ConsoleServerWithPort("ws://[::1]", "123") == "ws://[::1]:123");
  assert(SecretConsoleInput("/password secret") &&
         SecretConsoleInput("!admin login secret"));
  assert(CompleteConsole("/con", {}, {}) ==
         std::vector<std::string>{"/connect"});
  assert(CompleteConsole("!HiNt fo", {"Foundry Access"}, {}) ==
         std::vector<std::string>{"!hint Foundry Access"});
  assert(CompleteConsole("!hint_location fo", {}, {"Foundry - Sick Score"}) ==
         std::vector<std::string>{"!hint_location Foundry - Sick Score"});
  assert(CompleteConsole("/password secret", {}, {}).empty());
  auto &state = InGameConsole();
  {
    const auto lines = state.lines.size(),
               waiting = state.popups.waiting.size();
    const auto revision = state.revision;
    std::ofstream debug_log("console-debug-test.log");
    state.log.swap(debug_log);
    ConsoleDebug("[debug] item received: Airport Access");
    state.log.swap(debug_log);
    debug_log.close();
    std::ifstream file("console-debug-test.log");
    std::string line;
    std::getline(file, line);
    assert(line == "[debug] item received: Airport Access");
    assert(state.lines.size() == lines &&
           state.popups.waiting.size() == waiting &&
           state.revision == revision);
  }
  ConsoleLog("Before the main menu");
  assert(state.lines.back() == "Before the main menu" && state.popups.Empty());
  state.popups.Add("Stale startup popup");
  state.popups.visible.push_back({"Stale visible startup popup", 100});
  THPS3AP_ConsoleMainMenuReady();
  assert(state.popups_ready && state.popups.Empty());
  ConsoleLog("After the main menu");
  THPS3AP_ConsoleMainMenuReady();
  assert(state.popups.waiting.size() == 1);
  state.popups.Clear();
  const auto font_test_root = std::filesystem::absolute("console-font-test");
  const auto font_path = font_test_root / "Data" / "fonts" / "trick.fnt";
  std::filesystem::create_directories(font_path.parent_path());
  {
    std::ofstream file(font_path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(font_bytes.data()),
               font_bytes.size());
  }
  const auto ini = (font_test_root / "console-settings-test.ini").string();
  {
    std::ofstream file(ini);
    file << "[Archipelago]\nConsoleGapMarkers=99\nConsoleOutOfLogic=1\n";
  }
  THPS3AP_InitializeConsole(ini.c_str());
  assert(DeathLink().Amnesty() == 1);
  assert(state.marker_mode == ConsoleMarkerMode::Both && state.out_of_logic);
  for (int i = 0; i < 4608; ++i)
    ConsoleLog(std::to_string(i));
  assert(state.lines.size() == 4096 && state.lines.front() == "512");
  state.scroll = 8;
  const auto old_line = state.lines[state.lines.size() - state.scroll - 1];
  ConsoleLog("new");
  assert(state.scroll == 9 &&
         state.lines[state.lines.size() - state.scroll - 1] == old_line);
  const auto before = state.lines.size();
  ConsoleLog(std::string(4095, 'x') + "\xc3\xa9");
  assert(state.lines.size() == before && state.lines.back() == "\xc3\xa9");
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  assert(SDL_Init(SDL_INIT_VIDEO) == 0);
  SDL_Window *window =
      SDL_CreateWindow("Console test", 0, 0, 960, 540, SDL_WINDOW_HIDDEN);
  assert(window);
  Key(SDLK_F1);
  assert(THPS3AP_ConsoleCapturesInput() && state.popups.Empty());
  state.scroll = 0;
  Key(SDLK_PAGEUP);
  assert(state.scroll == 8);
  ConsoleLog("while reading");
  assert(state.scroll == 9);
  Key(SDLK_PAGEDOWN);
  assert(state.scroll == 1);
  SDL_Event wheel{};
  wheel.type = SDL_MOUSEWHEEL;
  wheel.wheel.y = -1;
  assert(THPS3AP_ProcessConsoleEvent(&wheel));
  assert(state.scroll == 0);
  Submit("/status");
  auto commands = TakeConsoleCommands();
  assert(commands.size() == 1 && commands.front() == "/status");
  Submit("/password super-secret");
  commands = TakeConsoleCommands();
  assert(commands.front() == "/password super-secret");
  Key(SDLK_UP);
  Key(SDLK_RETURN);
  commands = TakeConsoleCommands();
  assert(commands.front() == "/status");
  Text("!hint f");
  state.items = {"Foundry Access", "Flip Tricks"};
  Key(SDLK_TAB);
  Key(SDLK_TAB);
  Key(SDLK_RETURN);
  commands = TakeConsoleCommands();
  assert(commands.front() == "!hint Foundry Access");
  Text("abcd");
  Key(SDLK_LEFT);
  Key(SDLK_LEFT, KMOD_SHIFT);
  Key(SDLK_LEFT, KMOD_SHIFT);
  Text("XY");
  Key(SDLK_RETURN);
  commands = TakeConsoleCommands();
  assert(commands.front() == "aXYd");
  assert(SDL_SetClipboardText("one\r\ntwo") == 0);
  Key(SDLK_c, KMOD_CTRL);
  Key(SDLK_v, KMOD_CTRL);
  assert(TakeConsoleCommands().empty());
  Key(SDLK_RETURN);
  commands = TakeConsoleCommands();
  assert(commands.front() == "one  two");
  Submit("/collectibles on");
  Submit("/deathlink_amnesty 3");
  assert(DeathLink().Amnesty() == 3);
  assert(GetPrivateProfileIntA("Archipelago", "DeathLinkAmnesty", 0,
                               ini.c_str()) == 3);
  Submit("/deathlink_amnesty 0");
  Submit("/deathlink_amnesty -1");
  Submit("/deathlink_amnesty 3x");
  assert(DeathLink().Amnesty() == 3);
  assert(TakeConsoleCommands().empty());
  DeathLink().SetAmnesty("1");
  THPS3AP_InitializeConsole(ini.c_str());
  assert(DeathLink().Amnesty() == 3);
  assert(collectibles);
  Submit("/gaps all");
  assert(gaps == 2);
  Submit("/gaps off");
  assert(gaps == 0);
  Submit("/gapmarkers tint");
  assert(state.marker_mode == ConsoleMarkerMode::Tint);
  Submit("/gapmarkers bounds");
  assert(state.marker_mode == ConsoleMarkerMode::Bounds);
  Submit("/outoflogic on");
  assert(state.out_of_logic);
  Submit("/outoflogic off");
  assert(!state.out_of_logic);
  assert(GetPrivateProfileIntA("Archipelago", "ConsoleGapMarkers", 99,
                               ini.c_str()) == 1);
  assert(GetPrivateProfileIntA("Archipelago", "ConsoleOutOfLogic", 99,
                               ini.c_str()) == 0);
  Submit("/markers off");
  assert(!state.hud);
  Submit("/markers on");
  assert(state.hud);
  Submit("/clear");
  assert(state.lines.empty() && state.scroll == 0 && state.popups.Empty());
  ConsoleLog("&C7Player1&C3 found &C1Foundry Access&C3 at &C6Foundry - Secret "
             "Tape&C3.");
  ConsoleLog("&C2Skater&C3: Unicode chat and editing: caf\xc3\xa9.");
  ConsoleLog("Use /help for commands. Chat and !commands go to the AP server.");
  state.status = "authenticated | Player1";
  Text("!hint fo");
  texture_api[2] = reinterpret_cast<void *>(Release);
  texture_api[16] = reinterpret_cast<void *>(Lock);
  texture_api[17] = reinterpret_cast<void *>(Unlock);
  void *api[89]{};
  struct {
    void **api;
  } device{api};
  api[20] = reinterpret_cast<void *>(CreateTexture);
  api[41] = reinterpret_cast<void *>(GetViewport);
  api[34] = api[35] = reinterpret_cast<void *>(Scene);
  api[50] = reinterpret_cast<void *>(State);
  api[63] = reinterpret_cast<void *>(Stage);
  api[61] = reinterpret_cast<void *>(SetTexture);
  api[57] = reinterpret_cast<void *>(Block);
  for (int index : {54, 56, 76, 88})
    api[index] = reinterpret_cast<void *>(Shader);
  api[54] = reinterpret_cast<void *>(Restore);
  api[72] = reinterpret_cast<void *>(Draw);
  THPS3AP_DrawConsole(&device);
  assert(draws == 1 && uploads == 1 && creates == 1);
  assert(texture_result == 5 && specular);
  SavePreview();
  assert(std::any_of(pixels.begin(), pixels.end(),
                     [](DWORD pixel) { return pixel == 0xf0b060ffu; }));
  THPS3AP_DrawConsole(&device);
  assert(creates == 1 && draws == 2); // reuse the texture
  Key(SDLK_a, KMOD_CTRL);
  Text("/password hidden");
  THPS3AP_DrawConsole(&device);
  Key(SDLK_ESCAPE);
  assert(!state.open);
  assert(!THPS3AP_ConsoleCapturesInput());
  const auto history_size = state.lines.size();
  ConsoleLog("&C7Player1&C3 found &C1Wallrides&C3 at &C6Airport - Secret "
             "Tape&C3. caf\xc3\xa9. "
             "A long message keeps its colors and waits for room instead of "
             "losing its ending. "
             "More text to fill the backlog and verify that opening F1 "
             "dismisses every row. " +
             std::string(600, 'x'));
  ConsoleDebug("[debug] log file only");
  THPS3AP_DrawConsole(&device);
  assert(state.lines.size() == history_size + 1);
  assert(!THPS3AP_ConsoleCapturesInput());
  assert(drawn_left > 0 && drawn_right > viewport.width * 0.95f &&
         drawn_right < viewport.width);
  const int popup_font_size = (viewport.height / 24 * 3 + 2) / 4;
  assert(drawn_top > viewport.height * 0.15f &&
         drawn_top + 4 * (popup_font_size + 4) < viewport.height / 2);
  assert(state.popups.visible.size() == 4 && !state.popups.waiting.empty());
  assert(state.popups.visible.front().text.find("Airport") !=
         std::string::npos);
  assert(std::all_of(
      state.popups.visible.begin(), state.popups.visible.end(),
      [](const auto &line) { return line.text.starts_with("&C"); }));
  assert(std::any_of(pixels.begin(), pixels.end(),
                     [](DWORD pixel) { return pixel == 0xffb060ffu; }));
  assert(pixels[0] == 0xb40a0c1eu);
  SavePreview("console-popup-preview.bmp", viewport.width * 96 / 100,
              4 * (popup_font_size + 4));
  for (auto &line : state.popups.visible)
    line.until = GetTickCount64() + 375;
  THPS3AP_DrawConsole(&device);
  assert((pixels[0] >> 24) > 60 && (pixels[0] >> 24) <= 90);
  for (auto &line : state.popups.visible)
    line.until = 0;
  THPS3AP_DrawConsole(&device);
  assert(!state.popups.visible.empty() &&
         state.popups.visible.front().until != 0);
  THPS3AP_OpenConsole();
  assert(state.open && state.popups.Empty());
  THPS3AP_DrawConsole(&device);
  Key(SDLK_ESCAPE);
  const auto previous_draws = draws;
  THPS3AP_DrawConsole(&device);
  assert(draws == previous_draws);
  if (argc > 1) {
    const std::string unicode = "H\xce\xb7\xc3\xa9H";
    ConsoleLog(unicode);
    THPS3AP_DrawConsole(&device);
    std::vector<std::pair<int, int>> ink;
    bool in_letter = false;
    for (int x = 12; x < 150; ++x) {
      int top = 100, bottom = -1;
      for (int y = 0; y < popup_font_size + 4; ++y)
        if ((pixels[y * texture_width + x] >> 16 & 255) > 70) {
          top = (std::min)(top, y);
          bottom = y;
        }
      if (bottom < 0) {
        in_letter = false;
        continue;
      }
      if (!in_letter)
        ink.emplace_back(top, bottom);
      else {
        ink.back().first = (std::min)(ink.back().first, top);
        ink.back().second = (std::max)(ink.back().second, bottom);
      }
      in_letter = true;
    }
    assert(ink.size() == 4);
    const int native_height = ink[0].second - ink[0].first + 1;
    const int fallback_height = ink[1].second - ink[1].first + 1;
    const int accented_height = ink[2].second - ink[2].first + 1;
    assert(fallback_height >= native_height - 2 &&
           fallback_height <= native_height + 2);
    assert(accented_height >= native_height &&
           accented_height <= native_height + 7);
    assert(ink[2].first < ink[0].first && ink[2].second >= ink[0].second - 2 &&
           ink[2].second <= ink[0].second + 2);
    assert(state.lines.back() == unicode);
  }
  const auto hud_draws = draws;
  DrawHudText(&device, "GAPS: 1/2 (3)", "");
  assert(draws == hud_draws + 1 && drawn_right > viewport.width - 2 &&
         drawn_right < viewport.width && drawn_top >= 0);
  assert(std::any_of(pixels.begin(), pixels.end(),
                     [](DWORD pixel) { return pixel == 0xff6d8be8u; }));
  SavePreview("hud-counter-preview.bmp",
              static_cast<int>(drawn_right - drawn_left), popup_font_size + 4);
  const auto hud_uploads = uploads, hud_creates = creates;
  DrawHudText(&device, "GAPS: 1/2 (3)", "");
  assert(draws == hud_draws + 2 && uploads == hud_uploads &&
         creates == hud_creates);
  fail_upload = true;
  const auto failed_hud_draws = draws;
  DrawHudText(&device, "GAPS: 2/2 (3)", "");
  assert(draws == failed_hud_draws);
  DrawHudText(&device, "GAPS: 2/2 (3)", "");
  assert(draws == failed_hud_draws + 1);
  DrawHudText(&device, "GAPS: 1/2 (3)", "Active gap: caf\xc3\xa9");
  assert(drawn_left > viewport.width / 4 &&
         drawn_right < viewport.width * 3 / 4);
  SavePreview("hud-gap-preview.bmp", static_cast<int>(drawn_right - drawn_left),
              popup_font_size + 4);
  DrawHudText(&device, "GAPS: 1/2 (3)", std::string(800, 'W'));
  assert(drawn_left >= 0 && drawn_right <= viewport.width);
  const auto hidden_draws = draws;
  state.hud = false;
  DrawHudText(&device, "GAPS: 1/2 (3)", "Active gap");
  assert(draws == hidden_draws);
  state.hud = true;
  const auto fallback_draws = draws;
  THPS3AP_InitializeConsole(
      (font_test_root / "missing" / "partymod.ini").string().c_str());
  DrawHudText(&device, "GAPS: 1/2 (3)", "");
  assert(draws == fallback_draws + 1);
  THPS3AP_OpenConsole();
  THPS3AP_DrawConsole(&device);
  assert(draws == fallback_draws + 2);
  DrawHudText(&device, "GAPS: 1/2 (3)", "Active gap");
  assert(draws == fallback_draws + 2);
  SDL_DestroyWindow(window);
  SDL_Quit();
  std::cout << "Console editing, history, completion, input capture, commands "
               "and rendering passed.\n";
}
