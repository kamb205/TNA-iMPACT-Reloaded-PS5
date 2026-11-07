// TNA iMPACT! PS5 - the hub (see hub_ui.h): the frame, the Home, Settings and
// About pages, and the controls.
//
// Hub based on elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
#include <rex/logging.h>
#include "hub_ui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

#include "hub_style.h"

namespace tnaimpact::hub {

namespace fs = std::filesystem;

static const char* const kTabNames[kTabCount] = {"HOME", "SETTINGS", "ABOUT"};

namespace {

std::string Unquote(std::string v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
  return v;
}

}  // namespace

// ----------------------------------------------------------------- settings

struct Hub::Setting {
  const char* section;
  const char* key;
  const char* label;
  const char* help;
  bool quoted;                      // a TOML string
  std::vector<const char*> values;  // as written to the file
  std::vector<const char*> labels;
  const char* fallback;             // the value when the file has none
};

// The graphics and audio settings, saved in UserData/settings.toml. The keys
// that are real cvars (guest_vblank_hz, anisotropic_override,
// tna_fps_counter) are applied by rex::cvar::LoadConfig when the game starts;
// tna_resolution and tna_aa are the hub's own (one setting maps to several
// cvars, written by ApplyValue), and hub_music_volume is read only by the hub.
static const std::vector<Hub::Setting>& Settings() {
  static const std::vector<Hub::Setting> rows = {
      {"GAME", "tna_game_path", "Game files",
       "Your own TNA iMPACT! disc image (.iso) or extracted game folder with default.xex. The hub looks in "
       "the app folder, /data/homebrew, /data and the USB drives; press Triangle on the Home page to search "
       "again. Left / Right picks one if several are found.",
       true, {}, {}, ""},
      {"GRAPHICS", "tna_resolution", "Resolution",
       "720p is the Xbox 360's own picture. 1440p draws the game at twice that: the sharpest picture that "
       "runs at a full 60 fps. The PS5 always sends 4K to the TV, and the Upscaling setting brings the "
       "picture up to it.",
       true, {"720p", "1080p", "1440p"}, {"720p (Original)", "1080p", "1440p (Recommended)"}, "720p"},
      {"GRAPHICS", "guest_vblank_hz", "Frame Rate",
       "60 matches the Xbox 360's frame clock. 30 halves it: half the motion, but a steady picture on a "
       "busy scene.",
       false, {"30", "60"}, {"30 fps", "60 fps (Original)"}, "60"},
      {"GRAPHICS", "tna_aa", "Anti-Aliasing",
       "Off keeps the game's own picture. FXAA smooths the finished frame. 2x, 4x and 8x use the game's "
       "own multisampling: 2x makes the game's 2x samples native, and its 4x is drawn natively anyway. The "
       "Xbox 360's GPU can't ask for 8x, so 8x is the same as 4x.",
       true, {"off", "fxaa", "2x", "4x", "8x"}, {"Off (Original)", "FXAA", "2x MSAA", "4x MSAA", "8x MSAA"},
       "off"},
      {"GRAPHICS", "present_effect", "Upscaling",
       "How the game's picture is scaled to the TV. FSR 1 rebuilds the edges as it enlarges and sharpens "
       "them: the clearest crowds and text at 720p and 1440p. CAS sharpens the scaled picture without "
       "rebuilding edges. Bilinear is the original soft scaling.",
       true, {"fsr", "cas", "bilinear"},
       {"FSR 1 (Recommended)", "CAS Sharpen", "Bilinear (Original)"}, "fsr"},
      {"GRAPHICS", "anisotropic_override", "Anisotropic Filter",
       "Keeps the mat, the ropes and the crowd sharp when seen at an angle.",
       false, {"0", "2", "3", "4", "5"}, {"Off", "2x", "4x", "8x", "16x (Recommended)"}, "5"},
      {"GRAPHICS", "tna_fps_counter", "FPS Counter",
       "Shows the frame rate in the top-left corner while you play.",
       false, {"true", "false"}, {"On", "Off"}, "false"},
      {"CONTROLLER", "ps5_vibration", "Vibration",
       "How strongly the DualSense vibrates when the game rumbles: Light is gentle, Medium is in between, "
       "Full is the game's own strength. The controller gives a short pulse at the level you pick.",
       false, {"0", "1", "2", "3"}, {"Off", "Light", "Medium", "Full"}, "1"},
      {"CONTROLLER", "tna_lightbar", "Light Bar",
       "The DualSense light bar's colour while you play. System keeps the PS5's own player colour.",
       true, {"gold", "red", "blue", "white", "system"}, {"TNA Gold", "Red", "Blue", "White", "System"},
       "gold"},
      {"AUDIO", "hub_music_volume", "Menu Music",
       "How loud the music is here in the hub. It changes right away.", false,
       {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"},
       {"Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, "50"},
  };
  return rows;
}

// --------------------------------------------------------------------- data

void Hub::Init(const Paths& paths, const Assets& assets, std::string version) {
  paths_ = paths;
  a_ = assets;
  version_ = std::move(version);
  Load();
  // 4K (3x draw scale) was removed: it ran at 30 fps (half speed) on PS5. An
  // older settings file that still asks for it moves to 1440p.
  if (Value("tna_resolution", "") == "4k") {
    for (const Setting& r : Settings())
      if (std::string(r.key) == "tna_resolution") ApplyValue(r, 2);  // (1440p)
  }
  // Settings added after an earlier install (e.g. Upscaling): write the
  // default now, so the file the game loads matches what the hub shows.
  for (const Setting& r : Settings())
    if (r.fallback[0] != '\0' && Value(r.key, "").empty())
      Save(r.key, r.quoted ? std::string("\"") + r.fallback + "\"" : r.fallback);
  ScanGames(false);
}

void Hub::Load() {
  config_.clear();
  std::ifstream in(paths_.config);
  for (std::string line; std::getline(in, line);) {
    const size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos || line[a] == '#') continue;
    if (line[a] == '[') break;  // (top-level keys only)
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(a, eq - a), v = line.substr(eq + 1);
    k.erase(k.find_last_not_of(" \t") + 1);
    v.erase(0, v.find_first_not_of(" \t"));
    if (const size_t hash = v.find(" #"); hash != std::string::npos && v[0] != '"') v.erase(hash);
    v.erase(v.find_last_not_of(" \t\r") + 1);
    config_.emplace_back(k, v);
  }
  // (the game's files come from your disc: the scanner finds them)
  state_ = install::State{};
}

float Hub::MusicVolume() const {
  const int pct = std::atoi(Value("hub_music_volume", "50").c_str());
  return std::clamp(pct, 0, 100) / 100.0f;
}

std::string Hub::Value(const std::string& key, const std::string& fallback) const {
  for (const auto& [k, v] : config_)
    if (k == key) return Unquote(v);
  return fallback;
}

void Hub::Save(const std::string& key, const std::string& value) {
  bool found = false;
  for (auto& [k, v] : config_)
    if (k == key) v = value, found = true;
  if (!found) config_.emplace_back(key, value);
  // The file: that key's line replaced (or added before the first [section]).
  std::vector<std::string> lines;
  {
    std::ifstream in(paths_.config);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
  }
  const std::string entry = key + " = " + value;
  size_t insert_at = lines.size();
  bool done = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& l = lines[i];
    const size_t first = l.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    if (l[first] == '[') {
      insert_at = std::min(insert_at, i);
      break;
    }
    const size_t eq = l.find('=');
    if (eq == std::string::npos || l[first] == '#') continue;
    std::string k = l.substr(first, eq - first);
    k.erase(k.find_last_not_of(" \t") + 1);
    if (k == key) {
      lines[i] = entry;
      done = true;
      break;
    }
  }
  if (!done) {
    // (before the comment lines over the first [section])
    while (insert_at > 0 && insert_at < lines.size() &&
           lines[insert_at - 1].find_first_not_of(" \t") != std::string::npos &&
           lines[insert_at - 1][lines[insert_at - 1].find_first_not_of(" \t")] == '#')
      --insert_at;
    lines.insert(lines.begin() + std::ptrdiff_t(insert_at), entry);
  }
  const fs::path tmp = paths_.config.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    for (const std::string& l : lines) out << l << "\n";
  }
  std::error_code ec;
  fs::create_directories(paths_.config.parent_path(), ec);
  fs::rename(tmp, paths_.config, ec);
}

// The setting's value, and the cvars it stands for (so the game starts with
// them: rex::cvar::LoadConfig reads the file before the runtime is made).
void Hub::ApplyValue(const Setting& r, size_t index) {
  const std::string v = r.values[index];
  if (std::string(r.key) == "tna_resolution") {
    // 720p is the guest's own mode; 1080p makes the guest output 1080p;
    // 1440p and 4K keep the 720p mode and draw at 2x / 3x internally.
    const std::string output = (v == "1440p" || v == "4k") ? "720p" : v;
    const char* scale = v == "1440p" ? "2" : v == "4k" ? "3" : "1";
    Save("resolution", "\"" + output + "\"");
    Save("draw_resolution_scale_x", scale);
    Save("draw_resolution_scale_y", scale);
  } else if (std::string(r.key) == "tna_aa") {
    Save("swap_post_effect", v == "fxaa" ? "\"fxaa\"" : "\"none\"");
    Save("native_2x_msaa", (v == "2x" || v == "4x" || v == "8x") ? "true" : "false");
  } else if (std::string(r.key) == "ps5_vibration") {
    rumble_preview_ = v == "1" ? 40 : v == "2" ? 70 : v == "3" ? 100 : 0;  // (the driver's levels)
  } else if (std::string(r.key) == "tna_lightbar") {
    const char* rgb = v == "red" ? "0xC8102E" : v == "blue" ? "0x1E50C8" : v == "white" ? "0xFFFFFF" : "0xD4A017";
    Save("ps5_lightbar", v == "system" ? "false" : "true");
    Save("ps5_lightbar_rgb", rgb);
  } else if (std::string(r.key) == "guest_vblank_hz") {
    Save("vsync", "true");
  }
  Save(r.key, r.quoted ? "\"" + v + "\"" : v);
}

void Hub::ScanGames(bool announce) {
  const std::string saved = Value("tna_game_path", "");
  games_ = install::Search(paths_.app, saved);
  game_ = games_.empty() ? -1 : 0;
  if (!saved.empty())
    for (int i = 0; i < int(games_.size()); ++i)
      if (games_[size_t(i)].path.string() == saved) game_ = i;
  if (game_ >= 0) {
    const std::string path = games_[size_t(game_)].path.string();
    if (path != saved) Save("tna_game_path", "\"" + path + "\"");
    REXLOG_INFO("PS5 hub: game files: {} found, using {}", games_.size(), path);
  } else {
    REXLOG_WARN("PS5 hub: game files: none found in the app folder, /data or the USB drives");
  }
  UpdateGameState();
  if (announce)
    Toast(game_ >= 0 ? "Game: " + games_[size_t(game_)].label
                     : "No game found: copy your .iso or extracted disc files, then press Triangle again");
}

void Hub::UpdateGameState() {
  state_ = game_ >= 0 ? install::LookAt(games_[size_t(game_)].path) : install::State{};
}

fs::path Hub::GamePath() const { return game_ >= 0 ? games_[size_t(game_)].path : fs::path(); }

void Hub::NowPlaying(const std::string&) {
  // (songs change quietly: no name on the screen)
}

// ------------------------------------------------------------------ drawing

float Hub::Ease(float& v, float target, float speed) {
  v = v < -0.5f ? target : v + (target - v) * (1.0f - std::exp(-dt_ * speed));
  return v;
}

void Hub::Fade(int first_vertex, float alpha) {
  if (alpha >= 0.999f) return;
  for (int i = first_vertex; i < dl_->VtxBuffer.Size; ++i) {
    ImU32& c = dl_->VtxBuffer[i].col;
    c = WithAlpha(c, alpha);
  }
}

void Hub::Text(ImFont* f, float size, float x, float y, ImU32 col, const std::string& t, float align, float spacing) {
  if (!f) f = ImGui::GetFont();
  float px = x - TextWidth(f, size, t, spacing) * align;
  if (spacing <= 0) {
    dl_->AddText(f, size * s_, P(px, y), col, t.c_str());
    return;
  }
  // (letter by letter: a UTF-8 letter's bytes stay together)
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    const char* a = t.c_str() + i;
    const char* b = t.c_str() + std::min(t.size(), i + n);
    dl_->AddText(f, size * s_, P(px, y), col, a, b);
    px += f->CalcTextSizeA(size, FLT_MAX, 0, a, b).x + spacing;
    i += n;
  }
}

float Hub::TextWidth(ImFont* f, float size, const std::string& t, float spacing) const {
  if (!f) f = ImGui::GetFont();
  if (spacing <= 0) return f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x;
  float w = 0;
  for (size_t i = 0; i < t.size();) {
    size_t n = 1;
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    w += f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str() + i, t.c_str() + std::min(t.size(), i + n)).x + spacing;
    i += n;
  }
  return t.empty() ? 0 : w - spacing;
}

std::string Hub::Fit(ImFont* f, float size, const std::string& t, float width) const {
  if (TextWidth(f, size, t) <= width) return t;
  std::string s = t;
  while (!s.empty() && TextWidth(f, size, s + "...") > width) {
    s.pop_back();
    while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();  // (whole UTF-8 letters)
  }
  return s + "...";
}

// Text wrapped to a width, a line per '\n' at least.
std::vector<std::string> Hub::WrapLines(ImFont* f, float size, float width, const std::string& t) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (;;) {
    const size_t nl = t.find('\n', start);
    std::istringstream words(t.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
    std::string word, cur;
    while (words >> word) {
      const std::string next = cur.empty() ? word : cur + " " + word;
      if (!cur.empty() && TextWidth(f, size, next) > width) {
        lines.push_back(cur);
        cur = word;
      } else {
        cur = next;
      }
    }
    if (!cur.empty()) lines.push_back(Fit(f, size, cur, width));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return lines;
}

float Hub::Wrapped(ImFont* f, float size, float x, float y, float width, ImU32 col, const std::string& t, float line) {
  float yy = y;
  for (const std::string& l : WrapLines(f, size, width, t)) Text(f, size, x, yy, col, l), yy += size * line;
  return yy - y;
}

void Hub::Rect(float x, float y, float w, float h, ImU32 col, float r) {
  dl_->AddRectFilled(P(x, y), P(x + w, y + h), col, r * s_);
}

namespace {
const ImFontGlyph* CapGlyph(ImFont* f) {
  ImFontBaked* baked = f->GetFontBaked(f->LegacySize);
  return baked ? baked->FindGlyphNoFallback('H') : nullptr;
}
}  // namespace

float Hub::CapMid(ImFont* f, float size) const {
  if (!f) f = ImGui::GetFont();
  const ImFontGlyph* g = CapGlyph(f);
  return g ? (g->Y0 + g->Y1) * 0.5f * size / f->LegacySize : size * 0.5f;
}

float Hub::Baseline(ImFont* f, float size) const {
  if (!f) f = ImGui::GetFont();
  const ImFontGlyph* g = CapGlyph(f);
  return g ? g->Y1 * size / f->LegacySize : size * 0.8f;
}

void Hub::TextMid(ImFont* f, float size, float x, float cy, ImU32 col, const std::string& t, float align,
                  float spacing) {
  Text(f, size, x, cy - CapMid(f, size), col, t, align, spacing);
}

// The DualSense's buttons: the pictures in hub/buttons (each drawn centred in
// its square), else drawn in one colour. `size` is the button's longer side: a
// face button's width, the options button's height.
float Hub::GlyphWidth(uint32_t button, float size) const {
  if (auto it = a_.buttons.find(button); it != a_.buttons.end() && it->second)
    return size * it->second.fill_w / std::max(it->second.fill_w, it->second.fill_h);
  switch (button) {
    case kL1:
    case kR1: return size;
    case kOptions: return size * 26 / 22;
    default: return size;
  }
}

float Hub::Glyph(uint32_t button, float cx, float cy, ImU32 col, float size) {
  if (auto it = a_.buttons.find(button); it != a_.buttons.end() && it->second) {
    const Image& img = it->second;
    const float h = size * 0.5f / std::max(img.fill_w, img.fill_h);
    dl_->AddImage(img.id, P(cx - h, cy - h), P(cx + h, cy + h), ImVec2(0, 0), ImVec2(1, 1), col);
    return GlyphWidth(button, size);
  }
  const ImVec2 c = P(cx, cy);
  // (drawn: a face button 30 across, L1 / R1 40, the options button 22 tall)
  const float u = size / (button == kL1 || button == kR1 ? 40 : button == kOptions ? 22 : 30) * s_;
  const float R = 15 * u, k = 6.2f * u, t = 2.2f * u;
  switch (button) {
    case kCross:
    case kCircle:
    case kSquare:
    case kTriangle:
      dl_->AddCircle(c, R, col, 40, 1.6f * u);
      if (button == kCross) {
        dl_->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, t);
        dl_->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, t);
      } else if (button == kCircle) {
        dl_->AddCircle(c, k * 1.1f, col, 28, t);
      } else if (button == kSquare) {
        dl_->AddRect(ImVec2(c.x - k * 0.95f, c.y - k * 0.95f), ImVec2(c.x + k * 0.95f, c.y + k * 0.95f), col, 0, 0, t);
      } else {
        dl_->AddTriangle(ImVec2(c.x, c.y - k * 1.15f), ImVec2(c.x + k * 1.1f, c.y + k * 0.8f),
                         ImVec2(c.x - k * 1.1f, c.y + k * 0.8f), col, t);
      }
      break;
    case kLeft | kRight: {
      const float a = 7 * u, gap = 5 * u;
      dl_->AddTriangleFilled(ImVec2(c.x - gap, c.y - a), ImVec2(c.x - gap, c.y + a), ImVec2(c.x - gap - a * 1.2f, c.y), col);
      dl_->AddTriangleFilled(ImVec2(c.x + gap, c.y - a), ImVec2(c.x + gap, c.y + a), ImVec2(c.x + gap + a * 1.2f, c.y), col);
      break;
    }
    case kOptions:
      dl_->AddRect(ImVec2(c.x - 13 * u, c.y - 11 * u), ImVec2(c.x + 13 * u, c.y + 11 * u), col, 6 * u, 0, 1.6f * u);
      for (int i = -1; i <= 1; ++i)
        dl_->AddLine(ImVec2(c.x - 6 * u, c.y + i * 4.5f * u), ImVec2(c.x + 6 * u, c.y + i * 4.5f * u), col, 1.8f * u);
      break;
    case kR3:
      dl_->AddCircle(c, R, col, 40, 1.6f * u);
      dl_->AddCircleFilled(c, R - 4 * u, col, 40);
      TextMid(a_.body_bold, 13 * u / s_, cx, cy, Black(1), "R3", 0.5f);
      break;
    case kL1:
    case kR1:
      dl_->AddRect(ImVec2(c.x - 20 * u, c.y - 13 * u), ImVec2(c.x + 20 * u, c.y + 13 * u), col, 7 * u, 0, 1.6f * u);
      TextMid(a_.body_bold, 15 * u / s_, cx, cy, col, button == kL1 ? "L1" : "R1", 0.5f);
      break;
  }
  return GlyphWidth(button, size);
}

// One hint at the bottom, right-aligned at `right`: the button, then what it
// does, both centred on the hints' line.
float Hub::Hint(float right, uint32_t button, const char* label) {
  constexpr float kLine = 1022, kSize = 20, kButton = 28, kGap = 11, kBetween = 40;
  const float tw = TextWidth(a_.body_bold, kSize, label);
  const float gw = GlyphWidth(button, kButton);
  const float x = right - tw;
  TextMid(a_.body_bold, kSize, x, kLine, White(0.78f), label);
  Glyph(button, x - kGap - gw * 0.5f, kLine, White(0.9f), kButton);
  return x - kGap - gw - kBetween;
}

void Hub::PageTitle(const char* title, const std::string& subtitle) {
  Text(a_.display, 60, 112, 132, kText, title, 0, 1);
  Text(a_.body, 22, 114, 214, White(0.6f), subtitle);
}

void Hub::List(std::vector<Row>& rows, int sel, float rh, const std::function<void(int, float, float)>& draw) {
  const float hh = 50, view = kListView, x = kListX, w = kListW, y0 = kListY;
  float yy = 0;
  int sel_row = -1;
  for (size_t k = 0; k < rows.size(); ++k) {
    Row& r = rows[k];
    if (r.item < 0) {
      yy += k ? 14 : 0;
      r.y = yy;
      yy += hh;
    } else {
      r.y = yy;
      if (r.item == sel) sel_row = int(k);
      yy += rh;
    }
  }
  const float total = yy;
  if (sel_row < 0) return;
  // keep the focused row in view (with its heading, when it's the first under it)
  {
    float top = rows[size_t(sel_row)].y, bottom = top + rh;
    if (sel_row > 0 && rows[size_t(sel_row) - 1].item < 0) top = rows[size_t(sel_row) - 1].y;
    float target = scroll_px_;
    if (top < target) target = top;
    if (bottom > target + view) {
      // (to the first row or heading that keeps it in view: no row cut in half at the top)
      target = bottom - view;
      for (const Row& r : rows)
        if (r.y >= target - 0.5f) {
          target = r.y;
          break;
        }
    }
    scroll_px_ = std::clamp(target, 0.0f, std::max(0.0f, total - view));
  }
  Ease(scroll_f_, scroll_px_, 18);
  Ease(focus_y_, rows[size_t(sel_row)].y, 20);
  dl_->PushClipRect(P(0, y0 - 2), P(kW, y0 + view), true);
  const float push = press_ * 4;
  Rect(x + push, y0 + focus_y_ - scroll_f_ + push * 0.5f, w - push * 2, rh - 6 - push, kText, 10);
  for (size_t k = 0; k < rows.size(); ++k) {
    const Row& row = rows[k];
    const float ry = y0 + row.y - scroll_f_;
    if (ry < y0 - rh - 20 || ry > y0 + view + 20) continue;
    if (row.item < 0) {
      Text(a_.body_bold, 15, x + 24, ry + 20, White(0.45f), row.heading, 0, 3);
      continue;
    }
    const float over = std::clamp(1 - std::fabs(focus_y_ - row.y) / (rh * 0.7f), 0.0f, 1.0f);
    // (a thin line between rows that sit together)
    if (k + 1 < rows.size() && rows[k + 1].item >= 0)
      Rect(x + 24, ry + rh - 3, w - 48, 1, White(0.06f * (1 - over)));
    draw(row.item, ry, over);
  }
  dl_->PopClipRect();
  if (total > view) {  // where the list is
    const float bh = view * view / total;
    const float by = y0 + (view - bh) * scroll_f_ / (total - view);
    Rect(x + w + 16, y0, 3, view - 6, White(0.08f), 1.5f);
    Rect(x + w + 16, by, 3, bh - 6, White(0.5f), 1.5f);
  }
}

void Hub::Panel(const std::function<void(float, float, float)>& draw) {
  const int v0 = dl_->VtxBuffer.Size;
  draw(kPanelX + (1 - Smooth(panel_in_)) * 16, kPanelY, kPanelW);
  Fade(v0, Smooth(panel_in_));
}

float Hub::Facts(float x, float y, float w, const std::vector<std::pair<std::string, std::string>>& facts) {
  for (const auto& [k, v] : facts) {
    Text(a_.body_bold, 15, x, y + 5, White(0.45f), k, 0, 2.5f);
    y += std::max(44.0f, Wrapped(a_.body_bold, 22, x + 160, y, w - 160, White(0.82f), v) + 14);
  }
  return y;
}

void Hub::DrawBackdrop() {
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Rgba(8, 9, 11));
  if (a_.hero) {
    // The picture covers the screen, slowly zooming in and out (a 50 s
    // breath) and drifting sideways with the pages.
    const float breath = 0.5f - 0.5f * std::cos(time_ * 6.2832f / 50.0f);
    const float zoom = 1.04f + 0.06f * breath;
    Ease(hero_x_, (float(tab_) / float(kTabCount - 1) - 0.5f) * 0.024f, 3);
    const float sa = size.x / size.y, ia = a_.hero.width / a_.hero.height;
    float uw = 1, vh = 1;
    if (ia > sa) uw = sa / ia;
    else vh = ia / sa;
    uw /= zoom, vh /= zoom;
    const float room = (1 - uw) * 0.5f;
    const float cx = 0.5f + std::clamp(hero_x_ + 0.004f * std::sin(time_ * 0.07f), -room, room);
    const ImVec2 uv0(cx - uw * 0.5f, 0.42f - vh * 0.42f), uv1(cx + uw * 0.5f, 0.42f + vh * 0.58f);
    dl_->AddImage(a_.hero.id, ImVec2(0, 0), size, uv0, uv1);
  }
  // The pages over it: the picture steps back
  Ease(shade_, tab_ == kHome ? 0.0f : 0.84f, 7);
  if (shade_ > 0.003f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(shade_));
  // (on the home page, the lower left darkens a little under the logo)
  if (shade_ < 0.6f) {
    const float k = 1 - shade_ / 0.6f;
    dl_->AddRectFilledMultiColor(ImVec2(0, P(0, 330).y), ImVec2(P(1200, 0).x, size.y), Black(0), Black(0), Black(0),
                                 Black(0.7f * k));
  }
  // the top bar and the bottom stay readable over a bright picture
  dl_->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(size.x, P(0, 230).y), Black(0.72f), Black(0.72f), Black(0), Black(0));
  const float fade_from = 430 + 330 * std::clamp(shade_ / 0.84f, 0.0f, 1.0f);
  dl_->AddRectFilledMultiColor(ImVec2(0, P(0, fade_from).y), ImVec2(size.x, size.y), Black(0), Black(0), Black(0.94f),
                               Black(0.94f));
}

void Hub::DrawTopBar() {
  // the pages, between L1 and R1: the buttons centred on the names' capitals,
  // as far from the first and last names
  constexpr float kSize = 26, kSpacing = 2.2f, kGap = 40, kTop = 52, kButton = 42, kButtonGap = 22;
  const float cy = kTop + CapMid(a_.nav, kSize);
  float x = 112;
  const float l1 = GlyphWidth(kL1, kButton);
  Glyph(kL1, x + l1 * 0.5f, cy, White(0.55f), kButton);
  x += l1 + kButtonGap;
  float sel_x = 0, sel_w = 0;
  for (int i = 0; i < kTabCount; ++i) {
    const bool sel = i == tab_;
    const float w = TextWidth(a_.nav, kSize, kTabNames[i], kSpacing);
    Text(a_.nav, kSize, x, kTop, sel ? kText : White(0.5f), kTabNames[i], 0, kSpacing);
    if (sel) sel_x = x, sel_w = w;
    x += w + kGap;
  }
  x -= kGap;
  Glyph(kR1, x + kButtonGap + GlyphWidth(kR1, kButton) * 0.5f, cy, White(0.55f), kButton);
  // the underline, just under the names: it glides to the page, stretching on the way
  const float before = tab_x_;
  Ease(tab_x_, sel_x, 16);
  Ease(tab_w_, sel_w, 22);
  const float travel = std::min(60.0f, std::fabs(sel_x - before) * 0.25f);
  Rect(tab_x_ - travel * 0.5f, kTop + Baseline(a_.nav, kSize) + 4, tab_w_ + travel, 3, kAccent, 1.5f);
}

void Hub::DrawHints() {
  float r = 1808;
  if (confirm_) {
    if (confirm_yes_ != "OK") r = Hint(r, kCircle, confirm_no_label_.empty() ? "Cancel" : confirm_no_label_.c_str());
    r = Hint(r, kCross, confirm_yes_.c_str());
    return;
  }
  if (tab_ != kHome) r = Hint(r, kOptions, "Start Game");
  if (tab_ != kHome) r = Hint(r, kCircle, "Home");
  switch (tab_) {
    case kHome: r = Hint(r, kTriangle, "Search"); r = Hint(r, kCross, "Start Game"); break;
    case kSettingsTab: r = Hint(r, kLeft | kRight, "Change"); break;
    case kAboutTab: break;
  }
  // a message (bottom left, rising in)
  if (toast_time_ > 0) {
    const float a = Smooth(std::min(toast_time_ * 3, (3.0f - toast_time_) * 6));
    const float w = TextWidth(a_.body_bold, 20, toast_) + 44 + (toast_song_ ? 8 : 0);
    const float y = 1002 + (1 - a) * 14;
    Rect(112, y, w, 42, White(0.14f * a), 21);
    if (toast_song_) {  // a note
      const ImU32 c = WithAlpha(kAccent, a);
      dl_->AddCircleFilled(P(130, y + 27), 4.6f * s_, c, 16);
      Rect(133.2f, y + 11, 2.4f, 16, c, 1);
      dl_->AddTriangleFilled(P(133.2f, y + 11), P(141.5f, y + 15.5f), P(133.2f, y + 17.5f), c);
    } else {
      dl_->AddCircleFilled(P(132, y + 21), 4.5f * s_, WithAlpha(kAccent, a), 16);
    }
    TextMid(a_.body_bold, 20, 146 + (toast_song_ ? 8 : 0), y + 21, White(0.92f * a), toast_);
  }
}

// A question over everything (start anyway?), rising in.
void Hub::DrawConfirm() {
  Ease(confirm_in_, confirm_ ? 1.0f : 0.0f, 14);
  if (confirm_in_ < 0.01f) return;
  const float a = Smooth(confirm_in_);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  dl_->AddRectFilled(ImVec2(0, 0), size, Black(0.6f * a));
  const float w = 760, x = (kW - w) * 0.5f;
  const int v0 = dl_->VtxBuffer.Size;
  const float y = 360 + (1 - a) * 30;
  // (its height: the text's lines, wrapped as Wrapped does)
  const int lines = int(WrapLines(a_.body, 22, w - 96, confirm_text_).size());
  const float h = 120 + lines * 22 * 1.4f + 48;
  Rect(x, y, w, h, Rgba(24, 25, 29), 16);
  dl_->AddRectFilled(P(x, y), P(x + 6, y + h), kAccent, 16 * s_, ImDrawFlags_RoundCornersLeft);
  Text(a_.display, 40, x + 48, y + 44, kText, confirm_title_);
  Wrapped(a_.body, 22, x + 48, y + 120, w - 96, White(0.72f), confirm_text_);
  Fade(v0, a);
}

// ------------------------------------------------------------------- pages

void Hub::DrawHome() {
  // first open: the logo rises in, then the button
  const float in_logo = Smooth((time_ - 0.15f) / 0.8f), in_btn = Smooth((time_ - 0.45f) / 0.6f);
  // (a wide logo: the same area on the screen as the SvR ports')
  const float lx = 104, lw = 700;
  int v0 = dl_->VtxBuffer.Size;
  if (a_.logo) {
    const float lh = lw * a_.logo.height / a_.logo.width;
    const float ly = 862 - 36 - lh + (1 - in_logo) * 40;
    for (int i = 0; i < 8; ++i) {  // a soft shadow under it
      const float a = float(i) * 0.785f, d = 10;
      const float dx = std::cos(a) * d, dy = std::sin(a) * d + 8;
      dl_->AddImage(a_.logo.id, P(lx + dx, ly + dy), P(lx + lw + dx, ly + lh + dy), ImVec2(0, 0), ImVec2(1, 1), Black(0.1f));
    }
    dl_->AddImage(a_.logo.id, P(lx, ly), P(lx + lw, ly + lh));
  } else {
    // (no hub/logo.png: the title in the hub's own font)
    const float ly = 560 + (1 - in_logo) * 40;
    Text(a_.display, 136, lx, ly, kAccent, "TNA", 0, 2);
    Text(a_.display, 96, lx + 300, ly + 30, kText, "iMPACT!", 0, 2);
    Text(a_.body, 24, lx + 4, ly + 220, White(0.55f), "XBOX 360  \xC2\xB7  PLAYSTATION 5", 0, 3);
  }
  Fade(v0, in_logo);

  if (state_.Installed()) {
    const std::string game = "Game: " + GamePath().string();
    Text(a_.body, 18, lx + 4, 828, White(0.45f), Fit(a_.body, 18, game, 760));
  } else {
    // no game files: say where to put them (Triangle searches again)
    const float px = 1010, py = 300, pw = 730;
    Rect(px - 40, py - 34, pw + 80, 300, Black(0.62f), 16);
    dl_->AddRectFilled(P(px - 40, py - 34), P(px - 34, py + 266), kAccent, 8 * s_,
                       ImDrawFlags_RoundCornersLeft);
    Text(a_.display, 34, px, py, kAccent, "GAME FILES NOT FOUND");
    Wrapped(a_.body, 22, px, py + 60, pw, White(0.78f),
            "Your TNA iMPACT! disc image wasn't found. Copy your own .iso (or extracted disc files with "
            "default.xex) to /data/homebrew/PPSA94007/, /data/, or a USB drive, then press Triangle to "
            "search again.");
    TextMid(a_.body_bold, 20, px, py + 236, White(0.55f), "Triangle: search again");
  }

  // START GAME: breathes softly; pressed, it gives a little and flashes
  v0 = dl_->VtxBuffer.Size;
  const float push = press_ * 6;
  const float bx = 112 + push, by = 862 + push + (1 - in_btn) * 24, bw = 420 - push * 2, bh = 84 - push * 2;
  const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.4f);
  const float ring = 5 + 3 * pulse;
  dl_->AddRect(P(bx - ring, by - ring), P(bx + bw + ring, by + bh + ring), White(0.14f + 0.2f * pulse), (12 + ring) * s_, 0,
               2 * s_);
  Rect(bx, by, bw, bh, state_.Installed() ? Mix(kText, kAccent, press_ * 0.9f) : White(0.12f), 12);
  const ImU32 ink = state_.Installed() ? Mix(kInk, kText, press_) : White(0.35f);
  // the arrow and the words as one, centred in the button, the arrow on the words' capitals
  constexpr float kArrowW = 24, kArrowH = 28, kArrowGap = 20, kSize = 40, kSpacing = 2;
  const float cy = by + bh * 0.5f;
  const float tw = TextWidth(a_.display, kSize, "START GAME", kSpacing);
  const float tx = bx + (bw - (kArrowW + kArrowGap + tw)) * 0.5f;
  dl_->AddTriangleFilled(P(tx, cy - kArrowH * 0.5f), P(tx + kArrowW, cy), P(tx, cy + kArrowH * 0.5f), ink);
  TextMid(a_.display, kSize, tx + kArrowW + kArrowGap, cy, ink, "START GAME", 0, kSpacing);
  Fade(v0, in_btn);
}

int Hub::SettingIndex(const Setting& r) const {
  const std::string v = Value(r.key, r.fallback);
  for (size_t k = 0; k < r.values.size(); ++k)
    if (v == r.values[k]) return int(k);
  for (size_t k = 0; k < r.values.size(); ++k)
    if (std::string(r.fallback) == r.values[k]) return int(k);
  return 0;
}

void Hub::DrawSettings() {
  PageTitle("SETTINGS", "Saved as you change them. They take effect when the game starts.");
  const auto& set = Settings();
  const int sel = std::clamp(item_[tab_], 0, int(set.size()) - 1);
  std::vector<Row> rows;
  for (int i = 0; i < int(set.size()); ++i) {
    if (i == 0 || std::strcmp(set[size_t(i)].section, set[size_t(i) - 1].section)) rows.push_back({-1, set[size_t(i)].section});
    rows.push_back({i});
  }
  const float x = kListX, w = kListW;
  List(rows, sel, 54, [&](int i, float yy, float over) {
    const auto& r = set[size_t(i)];
    const bool game = std::strcmp(r.key, "tna_game_path") == 0;
    const float cy = yy + 24;  // (the row's middle: its highlight is 48 tall)
    TextMid(a_.body_bold, 23, x + 24, cy, Mix(White(0.92f), kInk, over), r.label);
    const std::string v = game ? (games_.empty() ? std::string("Not found") : games_[size_t(game_)].label)
                               : std::string(r.labels[size_t(SettingIndex(r))]);
    if (i == sel) {
      // the value slides in from the side it was changed toward
      const float vr = x + w - 46;
      const float slide = (1 - Smooth(value_in_)) * 18 * value_dir_;
      const int v0 = dl_->VtxBuffer.Size;
      TextMid(a_.body_bold, 23, vr + slide, cy, kInk, Fit(a_.body_bold, 23, v, 420), 1);
      Fade(v0, 0.25f + 0.75f * Smooth(value_in_));
      const float vl = vr - TextWidth(a_.body_bold, 23, v) - 18;
      const float nudge_l = value_dir_ < 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      const float nudge_r = value_dir_ > 0 ? (1 - Smooth(value_in_)) * 6 : 0;
      dl_->AddTriangleFilled(P(vl - nudge_l, cy - 7), P(vl - nudge_l, cy + 7), P(vl - 9 - nudge_l, cy), Black(0.75f * over));
      dl_->AddTriangleFilled(P(vr + 14 + nudge_r, cy - 7), P(vr + 14 + nudge_r, cy + 7), P(vr + 23 + nudge_r, cy),
                             Black(0.75f * over));
    } else {
      TextMid(a_.body, 23, x + w - 24, cy, Mix(White(0.6f), kInk, over), Fit(a_.body, 23, v, 420), 1);
    }
  });
  // the focused setting, explained
  const auto& r = set[size_t(sel)];
  const bool game = std::strcmp(r.key, "tna_game_path") == 0;
  Panel([&](float px, float py, float pw) {
    Text(a_.display, 44, px, py, kText, r.label);
    py += 72;
    py += Wrapped(a_.body, 23, px, py, pw, White(0.68f), r.help);
    py += 34;
    if (game) {
      for (int k = 0; k < int(games_.size()); ++k) {
        const bool c = k == game_;
        if (c) dl_->AddCircleFilled(P(px + 8, py + 14), 6 * s_, kAccent, 20);
        else dl_->AddCircle(P(px + 8, py + 14), 5.5f * s_, White(0.3f), 20, 1.5f * s_);
        TextMid(c ? a_.body_bold : a_.body, 19, px + 30, py + 14, c ? kText : White(0.45f),
                Fit(a_.body, 19, games_[size_t(k)].label, pw - 40));
        py += 38;
      }
      if (games_.empty()) TextMid(a_.body, 20, px + 30, py + 14, White(0.5f), "Not found");
    } else {
      const int cur = SettingIndex(r);
      for (int k = 0; k < int(r.labels.size()); ++k) {
        const bool c = k == cur;
        const float pop = c ? 1 + 0.6f * (1 - Smooth(value_in_)) : 1;
        if (c) dl_->AddCircleFilled(P(px + 8, py + 14), 6 * pop * s_, kAccent, 20);
        else dl_->AddCircle(P(px + 8, py + 14), 5.5f * s_, White(0.3f), 20, 1.5f * s_);
        TextMid(c ? a_.body_bold : a_.body, 22, px + 30, py + 14, c ? kText : White(0.45f), r.labels[size_t(k)]);
        py += 42;
      }
    }
    // the support line, small at the panel's bottom
    Text(a_.body, 18, px, 1010, White(0.4f), "Support the port: buymeacoffee.com/kamb205");
  });
}

void Hub::DrawAbout() {
  PageTitle("ABOUT", "TNA iMPACT! Reloaded for PlayStation 5" +
                         (version_.empty() ? "" : "  \xC2\xB7  Version " + version_));
  struct Credit {
    const char* who;
    const char* what;
  };
  const Credit people[] = {
      {"kamb205", "Recompiled TNA iMPACT! and ported it to PS5. GitHub: github.com/kamb205  \xC2\xB7  "
                  "Discord: kamsb205  \xC2\xB7  Support: buymeacoffee.com/kamb205"},
      {"Midway Studios Los Angeles", "Made TNA iMPACT! (2008), the game this port runs. All names belong to their owners."},
      {"@elripalda", "Brought ReXGlue to PlayStation 5: the PS5 port and the hub this one is based on."},
      {"ReXGlue", "The static recompiler and runtime that turn the Xbox 360 game into a native program."},
      {"Xenia", "The Xbox 360 emulator the runtime's code comes from."},
      {"Dear ImGui", "The interface the hub and the game's overlays are drawn with."},
  };
  float y = 276;
  for (const Credit& c : people) {
    Text(a_.display, 28, 112, y, kText, c.who);
    y += 38;
    y += Wrapped(a_.body, 19, 112, y, 860, White(0.62f), c.what, 1.35f) + 26;
  }
  const float px = kPanelX;
  float py = 286;
  Text(a_.body_bold, 15, px, py, White(0.45f), "SUPPORT THE PORT", 0, 3);
  py += 38;
  Text(a_.body_bold, 22, px, py, kAccent, "buymeacoffee.com/kamb205");
  py += 58;
  Text(a_.body_bold, 15, px, py, White(0.45f), "BUILT WITH", 0, 3);
  py += 40;
  const char* parts[] = {"ReXGlue (Xenia's recompiler and runtime)", "PS5 Payload SDK",
                         "Mesa RADV (Vulkan)", "plume and XenosRecomp", "Dear ImGui",
                         "stb_image, stb_vorbis, dr_mp3 and dr_flac", "Oswald and Inter fonts (SIL Open Font License)",
                         "AMD FidelityFX FSR 1 (MIT)", "UnleashedRecomp-Prospero (DualSense vibration)"};
  for (const char* part : parts) {
    Text(a_.body, 21, px, py, White(0.78f), part);
    py += 34;
  }
  py += 24;
  Text(a_.body_bold, 15, px, py, White(0.45f), "PLEASE NOTE", 0, 3);
  py += 38;
  Wrapped(a_.body, 19, px, py, 680, White(0.55f),
          "An unofficial fan project, not affiliated with or endorsed by TNA Entertainment, Midway, "
          "Microsoft or Sony. TNA iMPACT! and all related names belong to their owners. No game files are "
          "included: you need your own copy of the game.");
}

// --------------------------------------------------------------- behaviour

int Hub::Items() const {
  switch (tab_) {
    case kSettingsTab: return int(Settings().size());
    default: return 0;
  }
}

void Hub::Activate(int dir) {
  const int item = item_[tab_];
  if (tab_ == kSettingsTab) {
    const auto& r = Settings()[size_t(item)];
    if (std::strcmp(r.key, "tna_game_path") == 0) {
      if (games_.empty()) {
        Play(Sound::kError);
        return;
      }
      const int n = int(games_.size());
      game_ = ((game_ < 0 ? 0 : game_) + dir + n) % n;
      Save("tna_game_path", "\"" + games_[size_t(game_)].path.string() + "\"");
      UpdateGameState();
      REXLOG_INFO("PS5 hub: game file chosen: {}", games_[size_t(game_)].path.string());
    } else {
      const int n = int(r.values.size());
      const int idx = (SettingIndex(r) + dir + n) % n;
      ApplyValue(r, size_t(idx));
    }
    value_in_ = 0;
    value_dir_ = float(dir);
    Play(Sound::kSelect);
  }
}

void Hub::Navigate(const Pad& pad) {
  const uint32_t p = pad.pressed;
  if (leaving_) return;
  // R3: the next song, anywhere
  if ((p & kR3) && audio_.next_song) {
    audio_.next_song();  // (quietly: the music just changes)
  }
  if (confirm_) {  // a question: Cross does it, Circle doesn't (or does its own thing)
    if ((p & kCross) && confirm_in_ > 0.6f) {  // (not before it has been seen)
      auto action = std::move(confirm_);
      confirm_ = nullptr;
      action();
    } else if ((p & kCircle) && confirm_in_ > 0.6f) {
      auto no = std::move(confirm_no_);
      confirm_ = nullptr, confirm_no_ = nullptr;
      Play(Sound::kBack);
      if (no) no();
    }
    return;
  }
  // START GAME: once the game is there
  auto launch = [this] { press_ = 1, launch_ = true; };
  auto start = [&] {
    UpdateGameState();
    if (!state_.Installed()) {
      Play(Sound::kError);
      Notice("The game isn't set up",
             "Copy your TNA iMPACT! disc image (.iso) or its extracted files with default.xex to "
             "/data/homebrew/PPSA94007/, /data/ or a USB drive, then press Triangle on the Home page to "
             "search again. No game files are included.");
    } else {
      if (game_ >= 0) Save("tna_game_path", "\"" + games_[size_t(game_)].path.string() + "\"");
      REXLOG_INFO("PS5 hub: START GAME uses {}", GamePath().string());
      launch();
    }
  };
  if (p & kTriangle && tab_ == kHome) {
    ScanGames(true);
    Play(Sound::kSelect);
    return;
  }
  if (p & kOptions) {
    start();
    return;
  }
  if (p & (kL1 | kR1)) {
    GoTo((tab_ + ((p & kR1) ? 1 : kTabCount - 1)) % kTabCount, (p & kR1) ? 1.0f : -1.0f);
    Play(Sound::kPage);
    return;
  }
  if ((p & kCircle) && tab_ != kHome) {
    GoTo(kHome, -1);
    Play(Sound::kBack);
    return;
  }
  if (tab_ == kHome) {
    if (p & kCross) start();
    return;
  }
  const int n = Items();
  if (n == 0) return;
  int& sel = item_[tab_];
  const int before = sel;
  if (p & kUp) sel = (sel + n - 1) % n;
  if (p & kDown) sel = (sel + 1) % n;
  if (sel != before) Play(Sound::kMove);
  if (tab_ == kSettingsTab) {
    if (p & kLeft) Activate(-1);
    if (p & (kRight | kCross)) Activate(1);
    if (p & (kLeft | kRight | kCross)) press_ = 0.6f;
  }
}

void Hub::GoTo(int tab, float dir) {
  if (tab == tab_) return;
  tab_ = tab;
  scroll_f_ = -1;
  scroll_px_ = 0;
  focus_y_ = -1;  // (the highlight starts where the page's row is)
  page_in_ = 0;
  page_dir_ = dir;
  panel_in_ = 1;
  panel_item_ = item_[tab_];
}

bool Hub::Draw(const Pad& pad, float dt) {
  dt_ = std::clamp(dt, 0.0f, 0.1f);
  time_ += dt_;
  toast_time_ = std::max(0.0f, toast_time_ - dt_);
  Navigate(pad);
  if (launch_ && !leaving_) {
    leaving_ = true, press_ = 1;
    Play(Sound::kStart);
  }
  if (item_[tab_] != panel_item_) panel_item_ = item_[tab_], panel_in_ = 0;
  Ease(panel_in_, 1, 10);
  Ease(value_in_, 1, 12);
  Ease(press_, 0, 9);
  const ImVec2 size = ImGui::GetIO().DisplaySize;
  s_ = std::min(size.x / kW, size.y / kH);
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("PS5 hub: layout {}x{} (scale {:.2f})", size.x, size.y, s_);
  }
  ox_ = (size.x - kW * s_) * 0.5f;
  oy_ = (size.y - kH * s_) * 0.5f;
  dl_ = ImGui::GetForegroundDrawList();
  DrawBackdrop();
  DrawTopBar();
  // the page slides in from the side it came from, fading up
  Ease(page_in_, 1, 11);
  const int first = dl_->VtxBuffer.Size;
  const float shift = (1 - page_in_) * 48 * page_dir_;
  ox_ += shift * s_;
  switch (tab_) {
    case kHome: DrawHome(); break;
    case kSettingsTab: DrawSettings(); break;
    case kAboutTab: DrawAbout(); break;
  }
  ox_ -= shift * s_;
  Fade(first, Smooth(page_in_));
  DrawHints();
  DrawConfirm();
  // opening: up from black; START GAME: down to black
  const float open = Smooth(time_ / 0.7f);
  if (leaving_) leave_ = std::min(1.0f, leave_ + dt_ / 0.6f);
  const float black = std::max(1 - open, Smooth(leave_));
  if (black > 0.002f) dl_->AddRectFilled(ImVec2(0, 0), size, Black(black));
  return leaving_ && leave_ >= 1;
}

}  // namespace tnaimpact::hub
