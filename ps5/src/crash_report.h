// TNA iMPACT! PS5 - crash reports (from the SvR 2008/SvR 2011 PS5 ports).
//
// On abort(), std::terminate (uncaught C++ exception) or an unhandled
// structured exception, writes <dir>/crash_<time>.txt (reason + symbolized
// stack of the crashing thread) and the same lines to the log, then lets the
// process die as it would have. POSIX: fatal signals only (no minidump).

#pragma once

#include <filesystem>

namespace tnaimpact {

void InstallCrashReporter(const std::filesystem::path& dir);

// Called (once) when a crash is reported, before the log is flushed: lets a
// subsystem log what it knows.
void SetCrashHook(void (*hook)());

}  // namespace tnaimpact
