// TNA iMPACT! PS5 - the PS5 hub on the console (ps5_hub.cpp).
//
// Hub based on elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
#pragma once

#if defined(__PROSPERO__)
#include <functional>

#include "hub_ui.h"

struct ImFontAtlas;
struct ImFont;
namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace tnaimpact::hub {
// The hub's fonts (from OnConfigureFonts).
void ConfigureFonts(ImFontAtlas* atlas);
// The hub's fonts, for the port's things in the game.
const Assets& Fonts();
// The in-game FPS counter's font (Oswald, built into the app).
ImFont* AddCounterFont(ImFontAtlas* atlas, float size);
// That font once OnConfigureFonts has made it (null before).
ImFont* CounterFont();
// Shows the hub; `launch` runs (on the UI thread) on START GAME.
void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const Paths& paths,
          std::function<void()> launch);
// The port's version ("Test 7"), as the hub shows it.
const char* Version();
}  // namespace tnaimpact::hub
#endif
