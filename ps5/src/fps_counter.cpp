// TNA iMPACT! PS5 - the in-game FPS counter (fps_counter.h).
//
// From elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
#include "fps_counter.h"

#include <chrono>
#include <cstdint>
#include <cstdio>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "hub/ps5_hub.h"

// (the hub's FPS Counter setting writes this)
REXCVAR_DEFINE_BOOL(tna_fps_counter, false, "TNA", "Show the frame rate in the top-left corner while playing");

extern "C" uint64_t tna_guest_swap_count();  // (the SDK's guest swap counter)

namespace {

class FpsCounterDialog final : public rex::ui::ImGuiDialog {
 public:
  explicit FpsCounterDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    // The game's own frame rate (its swaps per second), not the overlay's
    // redraw rate (io.Framerate is always the TV's 60).
    const auto now = std::chrono::steady_clock::now();
    const uint64_t swaps = tna_guest_swap_count();
    const double dt = std::chrono::duration<double>(now - last_).count();
    if (dt >= 0.5) {
      fps_ = double(swaps - last_swaps_) / dt;
      last_ = now, last_swaps_ = swaps;
      if (std::chrono::duration<double>(now - last_log_).count() >= 10.0) {
        last_log_ = now;
        REXLOG_INFO("game fps {:.1f}", fps_);
      }
    }
    if (!REXCVAR_GET(tna_fps_counter)) return;
    ImFont* font = tnaimpact::hub::CounterFont();
    if (!font) return;
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps_);
    // sized for the screen: 30 px at 1080p, 60 px at 4K
    const float s = io.DisplaySize.y / 1080.0f;
    const ImVec2 at(24.0f * s, 16.0f * s);
    auto* dl = ImGui::GetForegroundDrawList();
    dl->AddText(font, 30.0f * s, ImVec2(at.x + 2 * s, at.y + 2 * s), IM_COL32(0, 0, 0, 180), text);
    dl->AddText(font, 30.0f * s, at, IM_COL32(255, 255, 255, 235), text);
  }

 private:
  std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now(), last_log_ = last_;
  uint64_t last_swaps_ = tna_guest_swap_count();
  double fps_ = 0;
};

}  // namespace

rex::ui::ImGuiDialog* CreateFpsCounterDialog(rex::ui::ImGuiDrawer* drawer) { return new FpsCounterDialog(drawer); }
