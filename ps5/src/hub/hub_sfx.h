// TNA iMPACT! PS5 - the PS5 hub's menu sounds (hub_sfx.cpp): made in code at
// 48 kHz, stereo interleaved. hub/sfx/<name>.wav (or .mp3, .flac, .ogg)
// replaces one (ps5_hub.cpp).
//
// From elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
#pragma once

#include <vector>

#include "hub_ui.h"

namespace tnaimpact::hub {

std::vector<float> SynthSound(Sound sound);
// Its file name in hub/sfx ("move", "select", ...).
const char* SoundName(Sound sound);
// A trophy's chime (the popup in the game); `platinum`: a fuller one.
std::vector<float> TrophyChime(bool platinum);

}  // namespace tnaimpact::hub
