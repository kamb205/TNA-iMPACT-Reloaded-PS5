// Dev aid: scripted controller input for unattended test runs.
//
// Ported from SvR2008-Recomp-PS5 (src/input_script.cpp), which is
// Copyright (C) 2026 nefariousjosiah and licensed under the GNU GPL v3.0.
//
// Set TNA_INPUT_SCRIPT to the path of a script file to drive player 1's pad
// from the game's first XInput poll. Each entry is
//
//   <seconds>:<buttons>[*<repeat>[@<interval s>]]
//
// with buttons joined by '+' (A B X Y START BACK UP DOWN LEFT RIGHT LB RB
// LT RT) or a left-stick direction (LS_UP LS_DOWN LS_LEFT LS_RIGHT), and
// entries separated by newlines or commas. '#' starts a comment. A press is
// held for 150 ms. Presses are ORed into the real pad state, so the game sees
// ordinary input. Without the variable the hook is installed but only
// forwards to the input system.

#pragma once

namespace tnaimpact {

inline constexpr char kInputScriptEnvVar[] = "TNA_INPUT_SCRIPT";

}  // namespace tnaimpact
