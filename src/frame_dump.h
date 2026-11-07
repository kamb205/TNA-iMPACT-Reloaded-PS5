// Developer tool: write presented guest frames to disk for offline lighting
// analysis (T014). Set
//
//   TNA_DUMP_FRAME=<dir>   directory to write raw R8G8B8X8 frames into
//   TNA_DUMP_EVERY=<secs>  seconds between captures (default 1.0)
//
// Frames are written as frame_<n>.rgba (width*height*4 bytes, R8G8B8X8) with a
// one-line size.txt ("width height stride"). This reuses the presenter's public
// CaptureGuestOutput (the same readback a screenshot feature would use), so no
// SDK change is needed. Off unless TNA_DUMP_FRAME is set.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>

namespace tnaimpact {

inline std::atomic<bool>& FrameDumpStopFlag() {
  static std::atomic<bool> stop{false};
  return stop;
}

inline void StopFrameDump() { FrameDumpStopFlag().store(true); }

inline void StartFrameDumpIfRequested(rex::Runtime* runtime) {
  const char* dir = std::getenv("TNA_DUMP_FRAME");
  if (!dir || !*dir || !runtime) return;
  const char* every = std::getenv("TNA_DUMP_EVERY");
  double interval = every && *every ? std::atof(every) : 1.0;
  if (interval <= 0.0) interval = 1.0;
  const std::string dump_dir = dir;

  rex::ui::Presenter* presenter =
      runtime->graphics_system() ? runtime->graphics_system()->presenter() : nullptr;
  if (!presenter) {
    REXLOG_WARN("[tna] frame dump: no presenter available");
    return;
  }

  std::thread([presenter, dump_dir, interval]() {
    uint64_t index = 0;
    while (!FrameDumpStopFlag().load()) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(static_cast<int64_t>(interval * 1000.0)));
      if (FrameDumpStopFlag().load()) break;
      rex::ui::RawImage image;
      if (!presenter->CaptureGuestOutput(image)) continue;
      if (image.width == 0 || image.height == 0 || image.data.empty()) continue;
      const std::string path = dump_dir + "/frame_" + std::to_string(index) + ".rgba";
      FILE* file = std::fopen(path.c_str(), "wb");
      if (file) {
        std::fwrite(image.data.data(), 1, image.data.size(), file);
        std::fclose(file);
        REXLOG_INFO("[tna] frame dump: {}x{} stride={} bytes={} -> {}", image.width,
                    image.height, image.stride, image.data.size(), path);
        const std::string size_path = dump_dir + "/size.txt";
        FILE* size_file = std::fopen(size_path.c_str(), "wb");
        if (size_file) {
          std::fprintf(size_file, "%u %u %zu\n", image.width, image.height, image.stride);
          std::fclose(size_file);
        }
      } else {
        REXLOG_WARN("[tna] frame dump: cannot open {}", path);
      }
      ++index;
    }
  }).detach();

  REXLOG_INFO("[tna] frame dump: enabled, every {:.2f}s to {}", interval, dump_dir);
}

}  // namespace tnaimpact
