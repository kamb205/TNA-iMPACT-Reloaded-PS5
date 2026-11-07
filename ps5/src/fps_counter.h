// TNA iMPACT! PS5 - the in-game FPS counter (the hub's FPS Counter setting:
// tna_fps_counter in UserData/settings.toml).
#pragma once

#include <rex/cvar.h>

namespace rex::ui {
class ImGuiDrawer;
class ImGuiDialog;
}  // namespace rex::ui

// (defined in fps_counter.cpp)
REXCVAR_DECLARE(bool, tna_fps_counter);

// Shows the frame rate in the top-left corner; the dialog lives until the app
// ends (it never closes itself).
rex::ui::ImGuiDialog* CreateFpsCounterDialog(rex::ui::ImGuiDrawer* drawer);
