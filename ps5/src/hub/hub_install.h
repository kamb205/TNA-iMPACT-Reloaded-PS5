// TNA iMPACT! PS5 - the hub's check of the game's files: the saved path, the
// app folder, or a scan of the PS5's drives (the player brings their own dump).
//
// Hub based on elripalda's SvR2008-Recomp-PS5 (GPL-3.0); see README Credits.
// Plain C++ (std::filesystem), so it also builds on a PC for previews.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tnaimpact::hub::install {

namespace fs = std::filesystem;

// The folder the game's files go in (<app>/Assets).
fs::path GameFolder(const fs::path& app);
// Where the game is in it: Assets itself (the disc's files as they are).
fs::path GameRoot(const fs::path& app);

// What the app folder has. The game starts from Assets/default.xex or, when
// the Assets folder isn't set up, from a disc image in the app folder.
struct State {
  bool xex = false;  // default.xex in the game folder
  bool iso = false;  // a disc image in the app folder (read in place)
  bool Installed() const { return xex || iso; }
};
State Look(const fs::path& app);

// One place the game can be started from: a disc image (.iso, read in place)
// or a folder holding default.xex.
struct Candidate {
  fs::path path;
  std::string label;  // for the hub ("game.iso (disc image)" or the folder)
};

// Every place found, in search order: the saved setting if it still exists,
// <app>/Assets, a disc image in <app>, then /data/homebrew, /data and the USB
// drives (2 levels deep, at most 2 s).
std::vector<Candidate> Search(const fs::path& app, const std::string& saved);
// The path to start the game from: the saved one if it still exists, else the
// first game found, else <app>/Assets (where it isn't: the hub asks for it).
fs::path Resolve(const fs::path& app, const std::string& saved);
// Whether one candidate (a .iso or an extracted game folder) has the game.
State LookAt(const fs::path& path);

// A folder's entries (none if it can't be read; never throws).
std::vector<fs::directory_entry> ListDir(const fs::path& dir);

// "6.6 GB" etc. for messages.
std::string Bytes(uint64_t n);

}  // namespace tnaimpact::hub::install
