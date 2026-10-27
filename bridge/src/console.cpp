#include "thps3_ap/console.hpp"
#include "thps3_ap/bridge_c_api.h"
#include "thps3_ap/bridge_runtime.hpp"
#include "thps3_ap/deathlink.hpp"
#include "thps3_ap/game_font.hpp"
#include "thps3_ap/game_state.hpp"
#include <SDL.h>
#include <array>
#include <cstring>
#include <limits>
#include <windows.h>

namespace {
using namespace thps3_ap;
GameFont combo_font;
constexpr COLORREF colors[]{RGB(109, 139, 232), RGB(176, 96, 255),
                            RGB(255, 255, 0),   RGB(255, 255, 255),
                            RGB(0, 238, 238),   RGB(250, 128, 114),
                            RGB(0, 255, 127),   RGB(238, 0, 238)};
ConsoleInput input;
std::vector<std::string> history;
std::size_t history_index = 0, tab_index = 0;
std::string tab_prefix;
bool tab_active = false, release_pending = false, text_input_was_active = false;
std::uint64_t input_revision = 0;

std::wstring Wide(std::string_view text) {
  const auto length = static_cast<int>(text.size());
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
  std::wstring result(size, 0);
  if (size)
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), size);
  return result;
}
std::wstring FallbackWide(std::string_view text) {
  auto value = Wide(text);
  if (combo_font.pixels.empty() || value.empty())
    return value;
  const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
                                 value.data(), static_cast<int>(value.size()),
                                 nullptr, 0, nullptr, nullptr, 0);
  if (!size)
    return value;
  std::wstring upper(size, 0);
  if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(),
                     static_cast<int>(value.size()), upper.data(), size,
                     nullptr, nullptr, 0))
    return value;
  return upper;
}
void ToggleConsole(ConsoleState &state) {
  state.open = !state.open;
  if (state.open) {
    text_input_was_active = SDL_IsTextInputActive() != SDL_FALSE;
    SDL_StartTextInput();
    state.popups.Clear();
  } else {
    if (!text_input_was_active)
      SDL_StopTextInput();
    release_pending = true;
  }
  ++state.revision;
}
void SaveDisplayPreferences() {
  auto &state = InGameConsole();
  std::string path, mode, outside;
  {
    const std::lock_guard lock(state.mutex);
    path = state.config_path;
    mode = std::to_string(static_cast<unsigned>(state.marker_mode));
    outside = state.out_of_logic ? "1" : "0";
  }
  if (path.empty())
    return;
  if (!WritePrivateProfileStringA("Archipelago", "ConsoleGapMarkers",
                                  mode.c_str(), path.c_str()) ||
      !WritePrivateProfileStringA("Archipelago", "ConsoleOutOfLogic",
                                  outside.c_str(), path.c_str()))
    ConsoleLog("Could not save console display preferences in partymod.ini.");
}
void Submit(std::string text) {
  if (text.empty())
    return;
  auto &state = InGameConsole();
  const auto space = text.find(' ');
  const auto command = text.substr(0, space);
  const auto argument = space == std::string::npos
                            ? std::string{}
                            : ConsoleLower(text.substr(space + 1));
  if (command == "/clear") {
    const std::lock_guard lock(state.mutex);
    state.lines.clear();
    state.scroll = 0;
    ++state.revision;
    state.popups.Clear();
  } else if (command == "/quit") {
    SDL_Event event{};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
  } else if (command == "/deathlink_amnesty") {
    auto &deathlink = DeathLink();
    if (!argument.empty() && !deathlink.SetAmnesty(argument)) {
      ConsoleLog("Use /deathlink_amnesty with a positive whole number.");
      return;
    }
    const auto value = std::to_string(deathlink.Amnesty());
    ConsoleLog("DeathLink sends every " + value + " local bail(s).");
    if (!argument.empty()) {
      const std::lock_guard lock(state.mutex);
      if (!state.config_path.empty() &&
          !WritePrivateProfileStringA("Archipelago", "DeathLinkAmnesty",
                                      value.c_str(), state.config_path.c_str()))
        state.Append("Could not save DeathLinkAmnesty in partymod.ini.");
    }
  } else if (command == "/gapmarkers") {
    {
      const std::lock_guard lock(state.mutex);
      if (argument == "bounds")
        state.marker_mode = ConsoleMarkerMode::Bounds;
      else if (argument == "tint")
        state.marker_mode = ConsoleMarkerMode::Tint;
      else if (argument == "off")
        state.marker_mode = ConsoleMarkerMode::Off;
      else if (argument == "both")
        state.marker_mode = ConsoleMarkerMode::Both;
      else if (!argument.empty()) {
        state.Append("Use /gapmarkers bounds, tint, both or off.");
        return;
      }
      constexpr const char *names[]{"both", "bounds", "tint", "off"};
      state.Append(std::string("Gap markers: ") +
                   names[static_cast<unsigned>(state.marker_mode)] + '.');
    }
    if (!argument.empty())
      SaveDisplayPreferences();
  } else if (command == "/outoflogic") {
    {
      const std::lock_guard lock(state.mutex);
      if (argument == "on")
        state.out_of_logic = true;
      else if (argument == "off")
        state.out_of_logic = false;
      else if (argument == "toggle")
        state.out_of_logic = !state.out_of_logic;
      else if (!argument.empty()) {
        state.Append("Use /outoflogic on or off.");
        return;
      }
      state.Append(state.out_of_logic ? "Out-of-logic gap markers: on."
                                      : "Out-of-logic gap markers: off.");
    }
    if (!argument.empty())
      SaveDisplayPreferences();
    THPS3AP_RequestGapMenuRefresh();
  } else if (command == "/markers" || command == "/collectibles" ||
             command == "/geometry") {
    if (argument != "on" && argument != "off" && argument != "toggle" &&
        !argument.empty()) {
      ConsoleLog("Use " + command + " on, off or toggle.");
      return;
    }
    if (command == "/markers") {
      const std::lock_guard lock(state.mutex);
      state.hud = argument == "on" ||
                  ((argument.empty() || argument == "toggle") && !state.hud);
      state.Append(state.hud ? "Markers and HUD: on."
                             : "Markers and HUD: off.");
    } else if (!THPS3AP_HasStateSnapshot())
      ConsoleLog("Connect to a slot first.");
    else if (command == "/collectibles") {
      const bool enabled =
          argument == "on" || ((argument.empty() || argument == "toggle") &&
                               !THPS3AP_CollectibleMarkersEnabled());
      THPS3AP_SetCollectibleMarkersEnabled(enabled);
      thps3_ap::QueueCollectibleMarkersEnabled(enabled);
      ConsoleLog(enabled ? "Collectible markers: on."
                         : "Collectible markers: off.");
    } else {
      const bool enabled =
          argument == "on" || ((argument.empty() || argument == "toggle") &&
                               !THPS3AP_UseIntactLosAngelesGeometry());
      if (enabled != (THPS3AP_UseIntactLosAngelesGeometry() != FALSE))
        THPS3AP_ToggleLosAngelesGeometry();
    }
  } else if (command == "/gaps") {
    if (!THPS3AP_HasStateSnapshot())
      ConsoleLog("Connect to a slot first.");
    else if (argument == "next" || argument.empty())
      THPS3AP_AdvanceGapHighlight();
    else if (argument == "all")
      THPS3AP_ToggleAllGapHighlights();
    else if (argument == "off")
      THPS3AP_ClearGapHighlights();
    else
      ConsoleLog("Use /gaps next, all or off.");
  } else {
    const std::lock_guard lock(state.mutex);
    if (state.commands.size() < 100)
      state.commands.push_back(std::move(text));
    else
      state.Append("Console command queue is full. Try again shortly.");
  }
}
struct Renderer {
  HDC dc = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ old_bitmap = nullptr, old_font = nullptr;
  HFONT font = nullptr;
  void *texture = nullptr;
  void *device = nullptr;
  std::uint32_t *pixels = nullptr;
  int width = 0, height = 0, texture_width = 0, texture_height = 0,
      line_height = 0, font_size = 0, fallback_top = 0;
  std::uint64_t revision = std::numeric_limits<std::uint64_t>::max(),
                edit_revision = 0;
  bool blink = false;
  std::string label_text;
  DWORD label_view_width = 0, label_view_height = 0;
  void Reset() {
    if (texture) {
      auto api = *reinterpret_cast<void ***>(texture);
      reinterpret_cast<ULONG(__stdcall *)(void *)>(api[2])(texture);
    }
    if (dc) {
      SelectObject(dc, old_bitmap);
      SelectObject(dc, old_font);
      DeleteDC(dc);
    }
    if (bitmap)
      DeleteObject(bitmap);
    if (font)
      DeleteObject(font);
    dc = nullptr;
    bitmap = nullptr;
    font = nullptr;
    texture = nullptr;
    label_text.clear();
    label_view_width = label_view_height = 0;
  }
  ~Renderer() { Reset(); }
  bool Ensure(void *next_device, int next_width, int next_height,
              int next_font_size) {
    if (dc && next_device == device && next_width == width &&
        next_height == height && next_font_size == font_size)
      return true;
    Reset();
    device = next_device;
    width = next_width;
    height = next_height;
    font_size = next_font_size;
    texture_width = texture_height = 1;
    while (texture_width < width)
      texture_width *= 2;
    while (texture_height < height)
      texture_height *= 2;
    dc = CreateCompatibleDC(nullptr);
    if (!dc)
      return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = texture_width;
    info.bmiHeader.biHeight = -texture_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                              reinterpret_cast<void **>(&pixels), nullptr, 0);
    const auto make_font = [](int size) {
      return CreateFontW(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH,
                         L"Consolas");
    };
    font = make_font(font_size);
    if (!bitmap || !font) {
      Reset();
      return false;
    }
    old_bitmap = SelectObject(dc, bitmap);
    old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    const auto &reference = combo_font.glyphs['H'];
    for (int attempt = 0; attempt < 2; ++attempt) {
      TEXTMETRICW metrics{};
      GetTextMetricsW(dc, &metrics);
      fallback_top = combo_font.baseline * font_size / combo_font.line_height -
                     metrics.tmAscent;
      MAT2 identity{};
      identity.eM11.value = identity.eM22.value = 1;
      GLYPHMETRICS glyph{};
      if (!reference.height ||
          GetGlyphOutlineW(dc, L'H', GGO_METRICS, &glyph, 0, nullptr,
                           &identity) == GDI_ERROR ||
          !glyph.gmBlackBoxY)
        break;
      const int cap_height =
          (reference.height * font_size + combo_font.line_height / 2) /
          combo_font.line_height;
      const int matched_size =
          (font_size * cap_height + glyph.gmBlackBoxY / 2) / glyph.gmBlackBoxY;
      if (!attempt && matched_size != font_size) {
        const auto matched_font = make_font(matched_size);
        if (!matched_font) {
          Reset();
          return false;
        }
        SelectObject(dc, matched_font);
        DeleteObject(font);
        font = matched_font;
        continue;
      }
      fallback_top = (combo_font.baseline - reference.baseline) * font_size /
                         combo_font.line_height -
                     metrics.tmAscent + glyph.gmptGlyphOrigin.y;
      break;
    }
    line_height = font_size + 4;
    auto api = *reinterpret_cast<void ***>(device);
    using CreateTexture = HRESULT(__stdcall *)(void *, UINT, UINT, UINT, DWORD,
                                               UINT, UINT, void **);
    // managed A8R8G8B8 survives D3D8 device resets, no external font runtime
    if (reinterpret_cast<CreateTexture>(api[20])(
            device, texture_width, texture_height, 1, 0, 21, 1, &texture) < 0) {
      Reset();
      return false;
    }
    revision = std::numeric_limits<std::uint64_t>::max();
    return true;
  }
  const GameFont::Glyph *Glyph(std::string_view character) {
    unsigned code;
    if (character.size() == 1)
      code = static_cast<unsigned char>(character[0]);
    else {
      const auto value = Wide(character);
      if (value.size() != 1)
        return nullptr;
      code = value[0];
    }
    if (code >= combo_font.glyphs.size())
      return nullptr;
    const auto &glyph = combo_font.glyphs[code];
    return glyph.width ? &glyph : nullptr;
  }
  int CharacterWidth(std::string_view text) {
    if (const auto *glyph = Glyph(text))
      return ((glyph->width + 3) * font_size + combo_font.line_height / 2) /
             combo_font.line_height;
    if (text == " " && !combo_font.pixels.empty())
      return ((combo_font.line_height / 3 + 3) * font_size) /
             combo_font.line_height;
    const auto value = FallbackWide(text);
    SIZE size{};
    GetTextExtentPoint32W(dc, value.data(), static_cast<int>(value.size()),
                          &size);
    return size.cx;
  }
  int TextWidth(std::string_view text) {
    int width = 0;
    for (std::size_t i = 0; i < text.size();) {
      const auto next = NextCharacter(text, i);
      width += CharacterWidth(text.substr(i, next - i));
      i = next;
    }
    return width;
  }
  void Text(std::string_view text, int x, int y, COLORREF color) {
    y += 2;
    RECT clip{12, 0, width - 12, height};
    for (std::size_t i = 0; i < text.size();) {
      const auto next = NextCharacter(text, i);
      const auto character = text.substr(i, next - i);
      if (const auto *glyph = Glyph(character)) {
        const int top = combo_font.baseline - glyph->baseline;
        const float scale =
            static_cast<float>(font_size) / combo_font.line_height;
        for (int py =
                 (std::max)(0, y + static_cast<int>(std::floor(top * scale)));
             py < (std::min)(height, y + static_cast<int>(std::ceil(
                                             (top + glyph->height) * scale)));
             ++py)
          for (int px = (std::max)(clip.left, static_cast<LONG>(x));
               px < (std::min)(clip.right, x + static_cast<LONG>(std::ceil(
                                                   glyph->width * scale)));
               ++px) {
            const auto sample =
                combo_font.Sample(*glyph, (px - x + 0.5f) / scale - 0.5f,
                                  (py - y + 0.5f) / scale - top - 0.5f);
            if (sample[3] <= 0)
              continue;
            auto &pixel = pixels[py * texture_width + px];
            const auto red = static_cast<unsigned>(
                sample[0] * GetRValue(color) / 255 +
                (pixel >> 16 & 255) * (1 - sample[3]) + 0.5f);
            const auto green = static_cast<unsigned>(
                sample[1] * GetGValue(color) / 255 +
                (pixel >> 8 & 255) * (1 - sample[3]) + 0.5f);
            const auto blue =
                static_cast<unsigned>(sample[2] * GetBValue(color) / 255 +
                                      (pixel & 255) * (1 - sample[3]) + 0.5f);
            pixel = (red << 16) | (green << 8) | blue;
          }
      } else {
        const auto value = FallbackWide(character);
        SetTextColor(dc, color);
        const int top = y + fallback_top;
        ExtTextOutW(dc, x, top, ETO_CLIPPED, &clip, value.data(),
                    static_cast<UINT>(value.size()), nullptr);
        GdiFlush();
      }
      x += CharacterWidth(character);
      i = next;
    }
  }
  void Fill(int x, int y, int w, int h, COLORREF color) {
    RECT rect{x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    GdiFlush();
  }
  void Paint(const ConsoleState &state, bool next_blink) {
    std::fill(pixels, pixels + texture_width * texture_height, 0u);
    Fill(0, 0, width, height, RGB(12, 16, 24));
    Text("THPS3 ARCHIPELAGO | " + state.status, 12, 8, RGB(120, 200, 255));
    Text("F1/Esc close | Tab complete | Up/Down history", 12, 8 + line_height,
         RGB(160, 170, 185));
    Text("PgUp/PgDn/wheel scroll | /help commands", 12, 8 + 2 * line_height,
         RGB(160, 170, 185));
    const int input_y = height - line_height - 12;
    const int first_y = 12 + 3 * line_height;
    const int row_count = (std::max)(1, (input_y - first_y - 8) / line_height);
    std::deque<std::string> rows;
    const std::size_t end =
        state.lines.size() - (std::min)(state.scroll, state.lines.size());
    for (auto at = end;
         at > 0 && rows.size() < static_cast<std::size_t>(row_count);) {
      auto wrapped = Wrap(state.lines[--at]);
      for (auto it = wrapped.rbegin();
           it != wrapped.rend() &&
           rows.size() < static_cast<std::size_t>(row_count);
           ++it)
        rows.push_front(std::move(*it));
    }
    int y = input_y - 8 - static_cast<int>(rows.size()) * line_height;
    for (const auto &row : rows) {
      ColoredText(row, y);
      y += line_height;
    }
    Fill(8, input_y - 4, width - 16, line_height + 8, RGB(25, 32, 45));
    const bool secret = SecretConsoleInput(input.text);
    if (secret)
      Text("> [password input hidden]", 12, input_y, RGB(235, 235, 235));
    else {
      std::size_t start = 0;
      while (start < input.cursor &&
             TextWidth(std::string_view(input.text)
                           .substr(start, input.cursor - start)) > width - 70)
        start = NextCharacter(input.text, start);
      const int origin = 12 + TextWidth("> ");
      const auto first =
          (std::max)(start, (std::min)(input.cursor, input.anchor));
      const auto last = (std::max)(input.cursor, input.anchor);
      if (last > first) {
        const int left =
            origin +
            TextWidth(
                std::string_view(input.text).substr(start, first - start));
        Fill(left, input_y,
             (std::min)(width - 12 - left,
                        TextWidth(std::string_view(input.text)
                                      .substr(first, last - first))),
             line_height - 2, RGB(60, 85, 125));
      }
      Text("> " + input.text.substr(start), 12, input_y, RGB(235, 235, 235));
      if (!input.text.empty() && !tab_active &&
          input.cursor == input.text.size() && input.cursor == input.anchor) {
        const auto matches =
            CompleteConsole(input.text, state.items, state.locations);
        if (!matches.empty())
          Text(matches.front().substr(input.text.size()),
               origin + TextWidth(input.text.substr(start)), input_y,
               RGB(100, 110, 125));
      }
      if (next_blink)
        Fill(origin + TextWidth(std::string_view(input.text)
                                    .substr(start, input.cursor - start)),
             input_y, 1, line_height - 3, RGB(235, 235, 235));
    }
    GdiFlush();
    for (int row = 0; row < height; ++row)
      for (int x = 0; x < width; ++x)
        pixels[row * texture_width + x] |= 0xf0000000u;
    if (Upload()) {
      revision = state.revision;
      edit_revision = input_revision;
      blink = next_blink;
    }
  }
  std::vector<std::string> Wrap(std::string_view text) {
    std::vector<std::string> rows;
    std::string color = "&C3";
    for (std::size_t start = 0; start < text.size();) {
      auto end = start, space = std::string_view::npos;
      int used = 0;
      std::string active = color, space_color;
      while (end < text.size() && text[end] != '\n') {
        if (end + 2 < text.size() && text[end] == '&' && text[end + 1] == 'C' &&
            text[end + 2] >= '0' && text[end + 2] <= '7') {
          active = text.substr(end, 3);
          end += 3;
          continue;
        }
        const auto next = NextCharacter(text, end);
        const int advance = CharacterWidth(text.substr(end, next - end));
        if (used && used + advance > width - 24)
          break;
        if (text[end] == ' ') {
          space = end;
          space_color = active;
        }
        used += advance;
        end = next;
      }
      if (end < text.size() && text[end] != '\n' && text[end] != ' ' &&
          space != std::string_view::npos && space > start) {
        end = space;
        active = space_color;
      }
      rows.push_back(color + std::string(text.substr(start, end - start)));
      color = active;
      start =
          end + (end < text.size() && (text[end] == '\n' || text[end] == ' ')
                     ? 1
                     : 0);
    }
    return rows;
  }
  void ColoredText(const std::string &text, int y) {
    int x = 12;
    COLORREF color = colors[3];
    for (std::size_t i = 0; i < text.size();) {
      if (i + 2 < text.size() && text[i] == '&' && text[i + 1] == 'C' &&
          text[i + 2] >= '0' && text[i + 2] <= '7') {
        color = colors[text[i + 2] - '0'];
        i += 3;
        continue;
      }
      const auto next = NextCharacter(text, i);
      const auto character = std::string_view(text).substr(i, next - i);
      Text(character, x, y, color);
      x += CharacterWidth(character);
      i = next;
    }
  }
  void PaintPopups(ConsolePopupQueue &popups, std::uint64_t now) {
    popups.Advance(now, [this](auto text) { return Wrap(text); });
    std::fill(pixels, pixels + texture_width * texture_height, 0u);
    int y = 0;
    for (const auto &line : popups.visible) {
      Fill(0, y, width, line_height, RGB(10, 12, 30));
      ColoredText(line.text, y);
      GdiFlush();
      const int alpha = ConsolePopupAlpha(line.until, now);
      for (int row = y; row < y + line_height; ++row)
        for (int col = 0; col < width; ++col) {
          auto &pixel = pixels[row * texture_width + col];
          pixel |= static_cast<std::uint32_t>(
                       pixel == 0x0a0c1eu ? alpha * 180 / 255 : alpha)
                   << 24;
        }
      y += line_height;
    }
    Upload();
  }
  bool Upload() {
    struct LockedRect {
      int pitch;
      void *bits;
    } locked{};
    auto api = *reinterpret_cast<void ***>(texture);
    using Lock =
        HRESULT(__stdcall *)(void *, UINT, LockedRect *, const RECT *, DWORD);
    if (reinterpret_cast<Lock>(api[16])(texture, 0, &locked, nullptr, 0) >= 0) {
      for (int row = 0; row < texture_height; ++row)
        std::memcpy(static_cast<char *>(locked.bits) + row * locked.pitch,
                    pixels + row * texture_width, texture_width * 4);
      reinterpret_cast<HRESULT(__stdcall *)(void *, UINT)>(api[17])(texture, 0);
      return true;
    }
    return false;
  }
  bool PaintLabel(std::string_view text) {
    std::fill(pixels, pixels + texture_width * texture_height, 0u);
    Fill(0, 0, width, height, RGB(10, 12, 30));
    Text(text, 12, 0, colors[0]);
    GdiFlush();
    for (int row = 0; row < height; ++row)
      for (int x = 0; x < width; ++x) {
        auto &pixel = pixels[row * texture_width + x];
        pixel |= pixel == 0x0a0c1eu ? 0xa0000000u : 0xff000000u;
      }
    return Upload();
  }
  void Draw(float left, float top) {
    auto api = *reinterpret_cast<void ***>(device);
    using Scene = HRESULT(__stdcall *)(void *);
    using State = HRESULT(__stdcall *)(void *, DWORD, DWORD);
    using Stage = HRESULT(__stdcall *)(void *, DWORD, DWORD, DWORD);
    using StateBlock = HRESULT(__stdcall *)(void *, DWORD);
    DWORD saved = 0;
    if (reinterpret_cast<Scene>(api[34])(device) < 0)
      return;
    if (reinterpret_cast<HRESULT(__stdcall *)(void *, DWORD, DWORD *)>(api[57])(
            device, 1, &saved) >= 0) {
      const auto set = reinterpret_cast<State>(api[50]);
      set(device, 7, 0);
      set(device, 14, 0);
      set(device, 15, 0);
      set(device, 19, 5);
      set(device, 20, 6);
      set(device, 22, 1);
      set(device, 27, 1);
      set(device, 28, 0);
      set(device, 29, 0);
      set(device, 137, 0);
      set(device, 168, 15);
      set(device, 171, 1);
      reinterpret_cast<HRESULT(__stdcall *)(void *, DWORD, void *)>(api[61])(
          device, 0, texture);
      const auto stage = reinterpret_cast<Stage>(api[63]);
      stage(device, 0, 1, 4);
      stage(device, 0, 2, 2);
      stage(device, 0, 3, 0);
      stage(device, 0, 4, 4);
      stage(device, 0, 5, 2);
      stage(device, 0, 6, 0);
      // Game passes can leave their output in TEMP instead of the visible
      // color.
      stage(device, 0, 28, 1);
      stage(device, 0, 11, 0);
      stage(device, 0, 24, 0);
      stage(device, 0, 13, 3);
      stage(device, 0, 14, 3);
      stage(device, 0, 16, 1);
      stage(device, 0, 17, 1);
      stage(device, 0, 18, 0);
      stage(device, 1, 1, 1);
      reinterpret_cast<StateBlock>(api[88])(device, 0);
      reinterpret_cast<StateBlock>(api[76])(device, 0x144);
      struct Vertex {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
      };
      const float right = left + width, bottom = top + height;
      const float u = static_cast<float>(width) / texture_width,
                  v = static_cast<float>(height) / texture_height;
      const Vertex vertices[]{{left, top, 0, 1, 0xffffffff, 0, 0},
                              {right, top, 0, 1, 0xffffffff, u, 0},
                              {left, bottom, 0, 1, 0xffffffff, 0, v},
                              {left, bottom, 0, 1, 0xffffffff, 0, v},
                              {right, top, 0, 1, 0xffffffff, u, 0},
                              {right, bottom, 0, 1, 0xffffffff, u, v}};
      reinterpret_cast<HRESULT(__stdcall *)(void *, DWORD, UINT, const void *,
                                            UINT)>(api[72])(
          device, 4, 2, vertices, sizeof(Vertex));
      reinterpret_cast<StateBlock>(api[54])(device, saved);
      reinterpret_cast<StateBlock>(api[56])(device, saved);
    }
    reinterpret_cast<Scene>(api[35])(device);
  }
};
Renderer renderer, hud_counter, hud_gap;
struct Viewport {
  DWORD x, y, width, height;
  float min_z, max_z;
};
int FontSize(DWORD height) {
  return ((std::max)(16, (std::min)(32, static_cast<int>(height) / 24)) * 3 +
          2) /
         4;
}
void DrawHudLabel(Renderer &label, void *device, std::string_view text,
                  const Viewport &viewport, bool right) {
  if (text.empty())
    return;
  if (!label.dc || label.device != device || label.label_text != text ||
      label.label_view_width != viewport.width ||
      label.label_view_height != viewport.height) {
    const int available = static_cast<int>(viewport.width) - 24;
    int size = FontSize(viewport.height);
    if (!label.Ensure(device, available, size + 4, size))
      return;
    int width = label.TextWidth(text);
    while (width > available - 24 && size > 1) {
      size =
          (std::max)(1, (std::min)(size - 1, size * (available - 24) / width));
      if (!label.Ensure(device, available, size + 4, size))
        return;
      width = label.TextWidth(text);
    }
    if (!label.Ensure(device, (std::min)(available, width + 24), size + 4,
                      size))
      return;
    if (!label.PaintLabel(text))
      return;
    label.label_text = text;
    label.label_view_width = viewport.width;
    label.label_view_height = viewport.height;
  }
  const float left = viewport.x +
                     (right ? viewport.width - label.width
                            : (viewport.width - label.width) * 0.5f) -
                     0.5f;
  label.Draw(left, viewport.y + 11.5f);
}
} // namespace

void thps3_ap::DrawHudText(void *device, std::string_view counter,
                           std::string_view gap) {
  if (!device)
    return;
  auto &state = InGameConsole();
  const std::lock_guard lock(state.mutex);
  if (!state.hud || state.open)
    return;
  Viewport viewport{};
  auto api = *reinterpret_cast<void ***>(device);
  if (reinterpret_cast<HRESULT(__stdcall *)(void *, Viewport *)>(api[41])(
          device, &viewport) < 0 ||
      viewport.width < 120 || viewport.height < 120)
    return;
  DrawHudLabel(hud_counter, device, counter, viewport, true);
  DrawHudLabel(hud_gap, device, gap, viewport, false);
}

extern "C" BOOL THPS3AP_ConsoleCapturesInput() {
  if (thps3_ap::ConsoleIsOpen())
    return TRUE;
  if (release_pending) {
    int count = 0;
    const auto *keys = SDL_GetKeyboardState(&count);
    if (std::any_of(keys, keys + count, [](Uint8 key) { return key != 0; }))
      return TRUE;
    release_pending = false;
  }
  return FALSE;
}
extern "C" void THPS3AP_InitializeConsole(const char *path) {
  if (!path || !*path)
    return;
  auto &state = thps3_ap::InGameConsole();
  const std::lock_guard lock(state.mutex);
  state.config_path = path;
  combo_font = {};
  const auto font_path = std::filesystem::path(path).parent_path() / "Data" /
                         "fonts" / "trick.fnt";
  std::ifstream font_file(font_path, std::ios::binary | std::ios::ate);
  const auto font_bytes = font_file.tellg();
  bool loaded = false;
  if (font_file && font_bytes > 0 && font_bytes <= 1024 * 1024) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(font_bytes));
    font_file.seekg(0);
    if (font_file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()))
      loaded = combo_font.Parse(bytes);
  }
  if (!loaded)
    state.Append(
        "Could not load Data/fonts/trick.fnt. Using the console fallback font.",
        false);
  renderer.Reset();
  hud_counter.Reset();
  hud_gap.Reset();
  ++state.revision;
  char amnesty[32];
  GetPrivateProfileStringA("Archipelago", "DeathLinkAmnesty", "1", amnesty,
                           sizeof(amnesty), path);
  if (!thps3_ap::DeathLink().SetAmnesty(amnesty))
    thps3_ap::DeathLink().SetAmnesty("1");
  const auto mode =
      GetPrivateProfileIntA("Archipelago", "ConsoleGapMarkers", 0, path);
  state.marker_mode = mode <= 3 ? static_cast<thps3_ap::ConsoleMarkerMode>(mode)
                                : thps3_ap::ConsoleMarkerMode::Both;
  state.out_of_logic =
      GetPrivateProfileIntA("Archipelago", "ConsoleOutOfLogic", 0, path) != 0;
}
extern "C" BOOL THPS3AP_ConsoleHudVisible() {
  return thps3_ap::ConsoleHudVisible() ? TRUE : FALSE;
}
extern "C" void THPS3AP_ConsoleMainMenuReady() {
  thps3_ap::ConsoleMainMenuReady();
}
extern "C" void THPS3AP_OpenConsole() {
  auto &state = thps3_ap::InGameConsole();
  const std::lock_guard lock(state.mutex);
  if (!state.open)
    ToggleConsole(state);
}
extern "C" BOOL THPS3AP_ProcessConsoleEvent(const void *raw) {
  const auto &event = *static_cast<const SDL_Event *>(raw);
  auto &state = thps3_ap::InGameConsole();
  std::unique_lock lock(state.mutex);
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F1) {
    if (!event.key.repeat)
      ToggleConsole(state);
    return TRUE;
  }
  if (!state.open)
    return FALSE;
  if (event.type == SDL_WINDOWEVENT &&
      event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
    ToggleConsole(state);
    return FALSE;
  }
  if (event.type == SDL_TEXTINPUT) {
    input.Insert(event.text.text);
    tab_active = false;
    ++input_revision;
    return TRUE;
  }
  if (event.type == SDL_MOUSEWHEEL) {
    const int direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                              ? -event.wheel.y
                              : event.wheel.y;
    if (direction > 0)
      state.scroll =
          (std::min)(state.scroll + 3u,
                     state.lines.empty() ? 0u : state.lines.size() - 1);
    else if (direction < 0)
      state.scroll = state.scroll > 3 ? state.scroll - 3 : 0;
    ++state.revision;
    return TRUE;
  }
  if (event.type == SDL_KEYDOWN) {
    const auto key = event.key.keysym.sym;
    const bool ctrl = (event.key.keysym.mod & KMOD_CTRL) != 0,
               shift = (event.key.keysym.mod & KMOD_SHIFT) != 0;
    if (key == SDLK_ESCAPE) {
      ToggleConsole(state);
      return TRUE;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      const auto text = input.text;
      if (!text.empty() && !thps3_ap::SecretConsoleInput(text)) {
        history.push_back(text);
        if (history.size() > 100)
          history.erase(history.begin());
      }
      input.Set("");
      tab_active = false;
      history_index = history.size();
      state.scroll = 0;
      ++input_revision;
      lock.unlock();
      Submit(text);
      return TRUE;
    }
    if (ctrl && key == SDLK_c) {
      const auto selected = input.Selected();
      if (!selected.empty())
        SDL_SetClipboardText(selected.c_str());
    } else if (ctrl && key == SDLK_v) {
      char *text = SDL_GetClipboardText();
      if (text) {
        input.Insert(text);
        SDL_free(text);
      }
    } else if (ctrl && key == SDLK_a) {
      input.anchor = 0;
      input.cursor = input.text.size();
    } else if (key == SDLK_LEFT || key == SDLK_RIGHT)
      input.Move(key == SDLK_LEFT ? -1 : 1, shift);
    else if (key == SDLK_HOME || key == SDLK_END)
      input.Edge(key == SDLK_END, shift);
    else if (key == SDLK_BACKSPACE || key == SDLK_DELETE)
      input.Erase(key == SDLK_BACKSPACE);
    else if (key == SDLK_UP) {
      if (history_index)
        input.Set(history[--history_index]);
    } else if (key == SDLK_DOWN) {
      if (history_index < history.size())
        ++history_index;
      input.Set(history_index < history.size() ? history[history_index] : "");
    } else if (key == SDLK_PAGEUP)
      state.scroll =
          (std::min)(state.scroll + 8u,
                     state.lines.empty() ? 0u : state.lines.size() - 1);
    else if (key == SDLK_PAGEDOWN)
      state.scroll = state.scroll > 8 ? state.scroll - 8 : 0;
    else if (key == SDLK_TAB) {
      if (!tab_active) {
        tab_prefix = input.text;
        tab_index = 0;
        tab_active = true;
      }
      const auto matches =
          thps3_ap::CompleteConsole(tab_prefix, state.items, state.locations);
      if (!matches.empty())
        input.Set(matches[tab_index++ % matches.size()]);
      ++input_revision;
      return TRUE;
    }
    tab_active = false;
    ++input_revision;
    ++state.revision;
    return TRUE;
  }
  return event.type == SDL_KEYUP || event.type == SDL_TEXTEDITING ||
         (event.type >= SDL_MOUSEMOTION && event.type <= SDL_MOUSEWHEEL) ||
         event.type == SDL_CONTROLLERBUTTONDOWN ||
         event.type == SDL_CONTROLLERBUTTONUP ||
         event.type == SDL_CONTROLLERAXISMOTION;
}
extern "C" void THPS3AP_DrawConsole(void *device) {
  if (!device)
    return;
  auto &state = thps3_ap::InGameConsole();
  const std::lock_guard lock(state.mutex);
  if (!state.open && (!state.popups_ready || state.popups.Empty()))
    return;
  Viewport viewport{};
  auto api = *reinterpret_cast<void ***>(device);
  if (reinterpret_cast<HRESULT(__stdcall *)(void *, Viewport *)>(api[41])(
          device, &viewport) < 0 ||
      viewport.width < 120 || viewport.height < 120)
    return;
  const int font_size = FontSize(viewport.height);
  const int width =
      state.open ? (std::min)(2048, static_cast<int>(viewport.width) - 24)
                 : static_cast<int>(viewport.width * 96 / 100);
  const int height =
      state.open ? (std::min)(1024, static_cast<int>(viewport.height) - 24)
                 : 4 * (font_size + 4);
  if (!renderer.Ensure(device, width, height, font_size))
    return;
  const bool blink = (GetTickCount64() / 500) % 2 != 0;
  if (!state.open) {
    renderer.PaintPopups(state.popups, GetTickCount64());
    if (state.popups.Empty())
      return;
  } else if (renderer.revision != state.revision ||
             renderer.edit_revision != input_revision ||
             renderer.blink != blink)
    renderer.Paint(state, blink);
  const float left =
      viewport.x + (state.open ? 12.0f : viewport.width * 0.02f) - 0.5f;
  const float top =
      viewport.y + (state.open ? 12.0f : viewport.height * 0.21f) - 0.5f;
  renderer.Draw(left, top);
}
