// TNA iMPACT! - PS5 port: the shared app (src/tnaimpact_app.h, unchanged) with
// the PS5's content paths, the hub (ps5/src/hub) and crash reports. The game's
// files live in the app folder, which the PS5 mounts at /app0:
//
//   /data/homebrew/PPSA94007/Assets/       (or an .iso in the app folder)
//   /data/homebrew/PPSA94007/UserData/     (settings.toml, logs, saves, crashes)
//
// The hub (based on elripalda's SvR2008-Recomp-PS5, GPL-3.0) is shown from
// OnFinalizePaths, before the runtime is made; START GAME loads settings.toml
// as cvars and resumes the start.
#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/flags.h>
#include <rex/logging.h>
#include <rex/ui/flags.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "crash_report.h"
#include "fps_counter.h"
#include "hub/hub_install.h"
#include "hub/ps5_hub.h"
#include "tnaimpact_app.h"

// (graphics_system.cpp; the hub's Frame Rate setting)
REXCVAR_DECLARE(double, guest_vblank_hz);

class TnaimpactPs5App : public TnaimpactApp {
 public:
  using TnaimpactApp::TnaimpactApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<TnaimpactPs5App>(
        new TnaimpactPs5App(ctx, "tnaimpact", PPCImageConfig));
  }

  // The Xenos GPU plugin draws the game (Vulkan on RADV). There is no command line on the PS5,
  // so set it here: without it the runtime starts with no graphics at all (no window, no hub).
  void OnPreSetup(rex::RuntimeConfig& config) override {
    TnaimpactApp::OnPreSetup(config);
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    REXLOG_INFO("PS5: GPU plugin {}", config.gpu_plugin);
    // SDL's dummy video driver (PS5) is 1024x768: a fullscreen window takes that 4:3 size and the
    // hub and overlays are laid out for it, then stretched over the 16:9 TV. A plain 3840x2160
    // window matches the display surface the presenter picks.
    rex::cvar::SetFlagByName("fullscreen", "false");
    rex::cvar::SetFlagByName("window_width", "3840");
    rex::cvar::SetFlagByName("window_height", "2160");
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const std::filesystem::path app = rex::filesystem::GetExecutableFolder();
    if (paths.game_data_root.empty()) paths.game_data_root = FindGame(app);
    paths.update_data_root.clear();
    paths.user_data_root = app / "UserData";
    paths.cache_root = app / "UserData" / "cache";
    tnaimpact::InstallCrashReporter(app / "UserData" / "crashes");
    // The hub's settings, written once with the defaults the hub expects.
    std::error_code ec;
    std::filesystem::create_directories(paths.user_data_root, ec);
    const std::filesystem::path settings = SettingsPath(app);
    if (!std::filesystem::exists(settings, ec)) {
      std::ofstream(settings, std::ios::trunc) << kDefaultSettings;
    }
  }

  // Shown before the runtime exists (the game hasn't started): the hub's
  // START GAME applies settings.toml and lets ReXApp continue the start.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig& defaults,
                                                 std::function<void(rex::PathConfig)> resume) override {
    const std::filesystem::path app = rex::filesystem::GetExecutableFolder();
    const std::filesystem::path settings = SettingsPath(app);
    if (!imgui_drawer()) {  // (no UI: start without the hub, but with its settings)
      rex::cvar::LoadConfig(settings);
      return defaults;
    }
    tnaimpact::hub::Paths paths{app, settings, app / "UserData"};
    tnaimpact::hub::Show(imgui_drawer(), immediate_drawer(), paths, [this, defaults, resume, app, settings] {
      // The hub's changes apply to this start: the runtime and the GPU are
      // made after `resume`, so draw_resolution_scale_* and the guest video
      // mode are read with the values just saved.
      rex::cvar::LoadConfig(settings);
      REXLOG_INFO("PS5 hub: settings: resolution {}", REXCVAR_GET(resolution));
      REXLOG_INFO("PS5 hub: settings: draw scale {}x{}, {} fps, anti-aliasing {}, anisotropic {}, upscaling {}, FPS counter {}",
                  REXCVAR_GET(draw_resolution_scale_x), REXCVAR_GET(draw_resolution_scale_y),
                  int(REXCVAR_GET(guest_vblank_hz)), REXCVAR_GET(swap_post_effect),
                  REXCVAR_GET(anisotropic_override), REXCVAR_GET(present_effect),
                  REXCVAR_GET(tna_fps_counter) ? "on" : "off");
      rex::PathConfig start = defaults;
      start.game_data_root = FindGame(app);
      REXLOG_INFO("PS5 hub: START GAME (game {})", start.game_data_root.string());
      // (deferred, not from inside the dialog's draw: the start makes the
      // runtime, so let the current ImGui frame finish and the dialog go first)
      app_context().CallInUIThreadDeferred([start, resume] { resume(start); });
    });
    return std::nullopt;
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    tnaimpact::hub::ConfigureFonts(atlas);
    tnaimpact::hub::AddCounterFont(atlas, 30.0f);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override { drawer_ = drawer; }

  void OnPreLaunchModule() override {  // (tna_fps_counter, the hub's setting)
    // (always made: it logs the game fps; it draws only with tna_fps_counter on)
    if (drawer_) CreateFpsCounterDialog(drawer_);
  }

  void OnPostLoadXexImage() override {
    TnaimpactApp::OnPostLoadXexImage();  // TNA_DUMP_IMAGE, when set
    REXLOG_INFO("==== TNA iMPACT! Reloaded PS5 port, version {} ====", TNAIMPACT_PS5_VERSION);
    REXLOG_INFO("game: {}", game_data_root().string());
  }

 private:
  static std::filesystem::path SettingsPath(const std::filesystem::path& app) {
    return app / "UserData" / "settings.toml";
  }

  // The hub's defaults: Resolution 720p, 60 fps, anti-aliasing off (the
  // game's own), FSR 1 upscaling, anisotropic 16x, FPS counter off.
  static constexpr const char* kDefaultSettings =
      "# TNA iMPACT! - settings (the hub rewrites this file as you change it)\n"
      "# Resolution: 720p, 1080p or 1440p (1440p draws at 2x: draw_resolution_scale_* = 2)\n"
      "tna_resolution = \"720p\"\n"
      "resolution = \"720p\"\n"
      "draw_resolution_scale_x = 1\n"
      "draw_resolution_scale_y = 1\n"
      "# Frame rate: 30 or 60 (guest_vblank_hz; vsync on)\n"
      "guest_vblank_hz = 60\n"
      "vsync = true\n"
      "# Anti-aliasing: off, fxaa, 2x, 4x or 8x\n"
      "tna_aa = \"off\"\n"
      "swap_post_effect = \"none\"\n"
      "native_2x_msaa = false\n"
      "# Upscaling: FSR 1 (recommended), CAS sharpen, or the original bilinear\n"
      "present_effect = \"fsr\"\n"
      "# Anisotropic filtering: 0 (off), 2, 3 (4x), 4 (8x) or 5 (16x)\n"
      "anisotropic_override = 5\n"
      "tna_fps_counter = false\n"
      "# Controller: vibration 0 (off), 1 (light), 2 (medium) or 3 (full); light bar colour\n"
      "ps5_vibration = 1\n"
      "tna_lightbar = \"gold\"\n"
      "ps5_lightbar = true\n"
      "ps5_lightbar_rgb = 0xD4A017\n"
      "hub_music_volume = 50\n";

  // The game's files: the saved path (tna_game_path in settings.toml), else
  // <app>/Assets, an .iso in the app folder, or the hub's scan of the drives.
  static std::filesystem::path FindGame(const std::filesystem::path& app) {
    return tnaimpact::hub::install::Resolve(app, SavedGamePath(app));
  }

  // tna_game_path from settings.toml (the hub writes it; "" if not set).
  static std::string SavedGamePath(const std::filesystem::path& app) {
    std::ifstream in(SettingsPath(app));
    for (std::string line; std::getline(in, line);) {
      const size_t first = line.find_first_not_of(" \t");
      if (first == std::string::npos || line[first] == '#') continue;
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string key = line.substr(first, eq - first);
      key.erase(key.find_last_not_of(" \t") + 1);
      if (key != "tna_game_path") continue;
      std::string value = line.substr(eq + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t\r") + 1);
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
      return value;
    }
    return {};
  }

  rex::ui::ImGuiDrawer* drawer_ = nullptr;
};
