// Dev aid: scripted controller input for unattended test runs (any renderer).
//
// Ported from SvR2008-Recomp-PS5 (src/input_script.cpp), which is
// Copyright (C) 2026 nefariousjosiah and licensed under the GNU GPL v3.0.
// The original hooks a generated XInputGetState wrapper; here the script is
// applied directly in the XamInputGetState import, which is the single entry
// point every guest XInput poll goes through in this title.
//
//   TNA_INPUT_SCRIPT=/path/to/script.txt
//
// Script file syntax (one entry per line, or comma-separated):
//
//   <seconds since the game first polled the pad>:<buttons>[*<repeat>@<interval s>]
//
// buttons joined with '+' (A B X Y START BACK UP DOWN LEFT RIGHT LB RB LT RT)
// or a left-stick direction (LS_UP LS_DOWN LS_LEFT LS_RIGHT). A press is held
// for 150 ms. Presses are ORed into player 1's state (a missing controller
// reads as connected), so the game's own code sees ordinary input and nothing
// reaches the desktop. Without the variable the hook only forwards to the
// input system.

#include "input_script.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

using rex::X_RESULT;

namespace {

struct Press {
  double at = 0;  // seconds
  uint16_t buttons = 0;
  bool lt = false;
  bool rt = false;
  int16_t lx = 0;
  int16_t ly = 0;
};

constexpr double kHoldSeconds = 0.15;

// XamInputGetState flags (same values the SDK entry uses).
constexpr uint32_t kXInputFlagGamepad = 0x01;
constexpr uint32_t kXInputFlagAnyUser = 1u << 30;

std::vector<Press> g_presses;
bool g_active = false;
bool g_reported = false;
std::once_flag g_parse_once;
std::chrono::steady_clock::time_point g_start;
uint32_t g_packet_bump = 0;
bool g_was_pressed = false;

uint16_t ButtonBit(const std::string& name, bool& lt, bool& rt, int16_t& lx, int16_t& ly) {
  if (name == "UP") return 0x0001;
  if (name == "DOWN") return 0x0002;
  if (name == "LEFT") return 0x0004;
  if (name == "RIGHT") return 0x0008;
  if (name == "START") return 0x0010;
  if (name == "BACK") return 0x0020;
  if (name == "LB") return 0x0100;
  if (name == "RB") return 0x0200;
  if (name == "A") return 0x1000;
  if (name == "B") return 0x2000;
  if (name == "X") return 0x4000;
  if (name == "Y") return 0x8000;
  if (name == "LT") lt = true;
  if (name == "RT") rt = true;
  if (name == "LS_UP") ly = 32767;
  if (name == "LS_DOWN") ly = -32768;
  if (name == "LS_LEFT") lx = -32768;
  if (name == "LS_RIGHT") lx = 32767;
  return 0;
}

void ParseEntry(const std::string& entry) {
  const size_t colon = entry.find(':');
  if (colon == std::string::npos) return;
  double at = std::atof(entry.substr(0, colon).c_str());
  std::string rest = entry.substr(colon + 1);
  int repeat = 1;
  double interval = 1.0;
  if (const size_t star = rest.find('*'); star != std::string::npos) {
    std::string count = rest.substr(star + 1);
    rest = rest.substr(0, star);
    if (const size_t atsign = count.find('@'); atsign != std::string::npos) {
      interval = std::atof(count.substr(atsign + 1).c_str());
      count = count.substr(0, atsign);
    }
    repeat = std::max(1, std::atoi(count.c_str()));
  }
  Press press;
  std::stringstream names(rest);
  std::string name;
  while (std::getline(names, name, '+')) {
    press.buttons |= ButtonBit(name, press.lt, press.rt, press.lx, press.ly);
  }
  for (int i = 0; i < repeat; ++i) {
    press.at = at + i * interval;
    g_presses.push_back(press);
  }
}

void Parse() {
  const char* path = std::getenv(tnaimpact::kInputScriptEnvVar);
  if (!path || !*path) return;

  std::ifstream file(path);
  if (!file) {
    REXLOG_WARN("[input script] cannot open {}", path);
    return;
  }

  std::string contents((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
  // Drop comments, then treat newlines as entry separators too.
  std::string cleaned;
  cleaned.reserve(contents.size());
  bool in_comment = false;
  for (char c : contents) {
    if (in_comment) {
      if (c == '\n') {
        in_comment = false;
        cleaned.push_back('\n');
      }
      continue;
    }
    if (c == '#') {
      in_comment = true;
      continue;
    }
    cleaned.push_back(c);
  }

  std::stringstream entries(cleaned);
  std::string entry;
  while (std::getline(entries, entry, ',')) {
    std::stringstream lines(entry);
    std::string line;
    while (std::getline(lines, line, '\n')) {
      const size_t first = line.find_first_not_of(" \t\r");
      if (first == std::string::npos) continue;
      const size_t last = line.find_last_not_of(" \t\r");
      ParseEntry(line.substr(first, last - first + 1));
    }
  }

  std::sort(g_presses.begin(), g_presses.end(),
            [](const Press& a, const Press& b) { return a.at < b.at; });
  g_active = !g_presses.empty();
  g_start = std::chrono::steady_clock::now();
  REXLOG_INFO("[input script] {} presses from {}", g_presses.size(), path);
}

}  // namespace

// XamInputGetState(user_index, flags, out_state). This is the import every
// guest XInput poll goes through; calling InputSystem directly replaces the
// SDK entry (the original symbol lives in the runtime dylib).
REX_HOOK_RAW(__imp__XamInputGetState) {
  std::call_once(g_parse_once, Parse);

  const uint32_t user_index = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  const uint32_t state_va = ctx.r5.u32;

  if ((flags & 0xFF) && (flags & kXInputFlagGamepad) == 0) {
    ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
    return;
  }

  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF || (flags & kXInputFlagAnyUser)) {
    actual_user_index = 0;
  }

  auto* kernel_state = REX_KERNEL_STATE();
  auto* input_system =
      kernel_state
          ? static_cast<rex::input::InputSystem*>(kernel_state->emulator()->input_system())
          : nullptr;
  if (!input_system) {
    ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
    return;
  }

  auto* state = state_va ? reinterpret_cast<rex::input::X_INPUT_STATE*>(base + state_va) : nullptr;
  const auto result = input_system->GetState(actual_user_index, state);
  ctx.r3.u64 = static_cast<uint32_t>(result);

  if (g_active && actual_user_index < 4) {
    static bool seen_users[4] = {};
    if (!seen_users[actual_user_index]) {
      seen_users[actual_user_index] = true;
      REXLOG_INFO("[input script] GetState user={} flags={:#x} result={:#x}", actual_user_index,
                  flags, uint32_t(result));
    }
  }

  if (!g_active || actual_user_index != 0 || !state) return;
  if (!g_reported) {
    g_reported = true;
    REXLOG_INFO("[input script] active on player 1 ({} presses)", g_presses.size());
  }

  if (result != X_ERROR_SUCCESS) {
    // No controller: present an idle one so the script still drives the game.
    std::memset(state, 0, sizeof(*state));
    ctx.r3.u64 = X_ERROR_SUCCESS;
  }

  const double now =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  uint16_t buttons = 0;
  bool lt = false, rt = false;
  int16_t lx = 0, ly = 0;
  for (const Press& p : g_presses) {
    if (now >= p.at && now < p.at + kHoldSeconds) {
      buttons |= p.buttons;
      lt |= p.lt;
      rt |= p.rt;
      if (p.lx) lx = p.lx;
      if (p.ly) ly = p.ly;
    }
  }
  const bool pressed = buttons || lt || rt || lx || ly;

  if (pressed != g_was_pressed) {
    ++g_packet_bump;
    if (pressed) {
      REXLOG_INFO("[input script] press at {:.1f}s buttons=0x{:04X} lx={} ly={}", now, buttons, lx,
                  ly);
    }
  }
  g_was_pressed = pressed;
  state->packet_number = uint32_t(state->packet_number) + g_packet_bump;
  if (pressed) {
    state->gamepad.buttons = uint16_t(uint16_t(state->gamepad.buttons) | buttons);
    if (lt) state->gamepad.left_trigger = 0xFF;
    if (rt) state->gamepad.right_trigger = 0xFF;
    if (lx) state->gamepad.thumb_lx = lx;
    if (ly) state->gamepad.thumb_ly = ly;
  }
}

// XamInputGetCapabilities(user_index, flags, out_caps). The front end's
// controller-select screen asks whether a pad is connected; with no physical
// controller the input system answers "not connected", so report a scripted
// gamepad for player 1 as well.
REX_HOOK_RAW(__imp__XamInputGetCapabilities) {
  std::call_once(g_parse_once, Parse);

  const uint32_t user_index = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  const uint32_t caps_va = ctx.r5.u32;

  if ((flags & 0xFF) && (flags & kXInputFlagGamepad) == 0) {
    ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
    return;
  }

  uint32_t actual_user_index = user_index;
  if ((actual_user_index & 0xFF) == 0xFF || (flags & kXInputFlagAnyUser)) {
    actual_user_index = 0;
  }

  auto* kernel_state = REX_KERNEL_STATE();
  auto* input_system =
      kernel_state
          ? static_cast<rex::input::InputSystem*>(kernel_state->emulator()->input_system())
          : nullptr;
  auto* caps = caps_va
                   ? reinterpret_cast<rex::input::X_INPUT_CAPABILITIES*>(base + caps_va)
                   : nullptr;
  const auto result =
      input_system ? input_system->GetCapabilities(actual_user_index, flags, caps)
                   : X_ERROR_DEVICE_NOT_CONNECTED;
  ctx.r3.u64 = static_cast<uint32_t>(result);

  if (!g_active || !caps) return;
  static int caps_logs = 0;
  if (caps_logs < 24) {
    ++caps_logs;
    REXLOG_INFO("[input script] capabilities user={} flags={:#x} result={:#x}", user_index, flags,
                uint32_t(result));
  }
  if (actual_user_index != 0) return;
  if (result != X_ERROR_SUCCESS) {
    std::memset(caps, 0, sizeof(*caps));
    caps->type = rex::input::XINPUT_DEVTYPE_GAMEPAD;
    caps->sub_type = 1;  // XINPUT_DEVSUBTYPE_GAMEPAD
    caps->flags = rex::input::X_INPUT_FLAG_GAMEPAD;
    ctx.r3.u64 = X_ERROR_SUCCESS;
  }
}
