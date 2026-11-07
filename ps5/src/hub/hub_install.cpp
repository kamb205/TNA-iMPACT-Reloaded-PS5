// TNA iMPACT! PS5 - the PS5 hub's check of the game's files (see
// hub_install.h).
//
// Hub based on elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
#include "hub_install.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>

#if __has_include(<rex/logging.h>)
#include <rex/logging.h>
#else  // (a preview build on a PC)
#include <fmt/format.h>
#define REXLOG_INFO(...) std::fputs((fmt::format(__VA_ARGS__) + "\n").c_str(), stdout)
#define REXLOG_WARN(...) std::fputs((fmt::format(__VA_ARGS__) + "\n").c_str(), stdout)
#define REXLOG_ERROR(...) std::fputs((fmt::format(__VA_ARGS__) + "\n").c_str(), stdout)
#endif

namespace tnaimpact::hub::install {

namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// The game's executable in the game folder (default.xex, any letter case).
fs::path FindXex(const fs::path& root) {
  std::error_code ec;
  for (const char* name : {"default.xex", "DEFAULT.XEX", "Default.xex"}) {
    const fs::path p = root / name;
    if (fs::is_regular_file(p, ec)) return p;
  }
  return {};
}

// A disc image in the app folder (the game also starts from one).
bool HasDiscImage(const fs::path& app) {
  for (const auto& e : ListDir(app)) {
    std::error_code ec;
    const std::string ext = Lower(e.path().extension().string());
    if (e.is_regular_file(ec) && (ext == ".iso" || ext == ".xiso")) return true;
  }
  return false;
}

// The folder with the game's default.xex in it: `p` itself, or its Assets.
fs::path XexFolder(const fs::path& p) {
  std::error_code ec;
  if (!fs::is_directory(p, ec)) return {};
  if (!FindXex(p).empty()) return p;
  for (const char* assets : {"Assets", "assets"})
    if (!FindXex(p / assets).empty()) return p / assets;
  return {};
}

bool IsDiscImage(const fs::path& p, uint64_t min_size) {
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) return false;
  const std::string ext = Lower(p.extension().string());
  if (ext != ".iso" && ext != ".xiso") return false;
  if (min_size == 0) return true;
  return fs::file_size(p, ec) > min_size && !ec;
}

std::string LabelFor(const fs::path& p) {
  std::error_code ec;
  if (fs::is_regular_file(p, ec)) return p.filename().string() + " (disc image)";
  return p.string();
}

void Add(std::vector<Candidate>& out, const fs::path& p) {
  if (p.empty()) return;
  std::error_code ec;
  std::error_code ec2;
  const bool ok = (fs::is_regular_file(p, ec) && IsDiscImage(p, 0)) || fs::is_directory(p, ec2);
  if (!ok) return;
  const fs::path norm = fs::weakly_canonical(p, ec);
  const fs::path key = ec ? p : norm;
  for (const Candidate& c : out) {
    std::error_code cec;
    const fs::path ck = fs::weakly_canonical(c.path, cec);
    if ((cec ? c.path : ck) == key) return;
  }
  out.push_back({p, LabelFor(p)});
}

// A folder listing, but never an enormous one (the scan must stay short).
std::vector<fs::directory_entry> CappedList(const fs::path& dir, size_t cap) {
  std::vector<fs::directory_entry> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end && out.size() < cap; it.increment(ec))
    out.push_back(*it);
  return out;
}

void ScanRoot(std::vector<Candidate>& out, const fs::path& root,
              std::chrono::steady_clock::time_point deadline) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  const fs::path game = XexFolder(root);
  if (!game.empty()) Add(out, game);
  for (const auto& e : CappedList(root, 4000)) {
    if (std::chrono::steady_clock::now() > deadline) return;
    const fs::path p = e.path();
    std::error_code fec;
    if (e.is_regular_file(fec)) {
      if (IsDiscImage(p, 1ull << 30)) Add(out, p);
      continue;
    }
    if (!e.is_directory(fec)) continue;
    const fs::path sub_game = XexFolder(p);
    if (!sub_game.empty()) {
      Add(out, sub_game);
      continue;
    }
    // (the second level: a USB drive's subfolders)
    const auto sub = CappedList(p, 4000);
    if (sub.size() >= 4000) continue;  // (too big to be a game folder)
    for (const auto& s : sub) {
      if (std::chrono::steady_clock::now() > deadline) return;
      const fs::path q = s.path();
      std::error_code sec;
      if (s.is_regular_file(sec)) {
        if (IsDiscImage(q, 1ull << 30)) Add(out, q);
      } else if (s.is_directory(sec)) {
        const fs::path g = XexFolder(q);
        if (!g.empty()) Add(out, g);
      }
    }
  }
}

}  // namespace

std::vector<fs::directory_entry> ListDir(const fs::path& dir) {
  std::vector<fs::directory_entry> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec))
    out.push_back(*it);
  return out;
}

std::string Bytes(uint64_t n) {
  char buf[32];
  if (n >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.1f GB", double(n) / double(1ull << 30));
  else if (n >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.0f MB", double(n) / double(1ull << 20));
  else std::snprintf(buf, sizeof(buf), "%.0f KB", double(n) / 1024.0);
  return buf;
}

fs::path GameFolder(const fs::path& app) {
  std::error_code ec;
  if (fs::is_directory(app / "Assets", ec)) return app / "Assets";
  if (fs::is_directory(app / "assets", ec)) return app / "assets";
  return app / "Assets";
}

fs::path GameRoot(const fs::path& app) { return GameFolder(app); }

State Look(const fs::path& app) {
  State s;
  s.xex = !FindXex(GameRoot(app)).empty();
  if (!s.xex) s.iso = HasDiscImage(app);
  if (s.Installed()) {
    REXLOG_INFO("PS5 hub: game found ({})", s.xex ? "Assets/default.xex" : "a disc image in the app folder");
  } else {
    REXLOG_WARN("PS5 hub: no game yet (Assets/default.xex or an .iso in {})", app.string());
  }
  return s;
}

State LookAt(const fs::path& path) {
  State s;
  std::error_code ec;
  if (path.empty()) return s;
  if (fs::is_directory(path, ec)) {
    s.xex = !FindXex(path).empty();
  } else if (fs::is_regular_file(path, ec)) {
    s.iso = IsDiscImage(path, 0);
  }
  return s;
}

std::vector<Candidate> Search(const fs::path& app, const std::string& saved) {
  std::vector<Candidate> out;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  // 1. the saved setting, if it is still there
  if (!saved.empty()) {
    const fs::path p(saved);
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      const fs::path g = XexFolder(p);
      if (!g.empty()) Add(out, g);
    } else if (IsDiscImage(p, 0)) {
      Add(out, p);
    }
  }
  // 2. the app folder: Assets/default.xex, then any disc image
  std::error_code ec;
  for (const char* d : {"Assets", "assets"}) {
    const fs::path g = XexFolder(app / d);
    if (!g.empty()) Add(out, g);
  }
  for (const auto& e : CappedList(app, 4000)) {
    std::error_code fec;
    if (e.is_regular_file(fec) && IsDiscImage(e.path(), 0)) Add(out, e.path());
  }
  // 3. the PS5's drives (2 levels deep, capped at 2 s)
  for (const char* root : {"/data/homebrew", "/data", "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
                           "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7", "/mnt/ext0", "/mnt/ext1"}) {
    if (std::chrono::steady_clock::now() > deadline) break;
    ScanRoot(out, root, deadline);
  }
  return out;
}

fs::path Resolve(const fs::path& app, const std::string& saved) {
  std::error_code ec;
  if (!saved.empty()) {
    const fs::path p(saved);
    if (fs::is_directory(p, ec)) {
      const fs::path g = XexFolder(p);
      if (!g.empty()) return g;
    } else if (IsDiscImage(p, 0)) {
      return p;
    }
  }
  for (const char* d : {"Assets", "assets"}) {
    const fs::path g = XexFolder(app / d);
    if (!g.empty()) return g;
  }
  for (const auto& e : CappedList(app, 4000))
    if (IsDiscImage(e.path(), 0)) return e.path();
  const std::vector<Candidate> found = Search(app, saved);
  if (!found.empty()) return found.front().path;
  return app / "Assets";
}

}  // namespace tnaimpact::hub::install
