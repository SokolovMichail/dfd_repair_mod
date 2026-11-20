// spares_repair.dll (defiance.cpp): the startup check for table patches that nobody merges.
//
// DefianceLoader's defiance.table-merge plugin merges each enabled mod's <table>.<mod folder>.patch.xml (or .txt)
// into the game's data tables. It is off by default, and a mod that ships only such patches (the table-merge
// package of HEMTT Spares) then adds nothing to the game, without a word. So at startup, when the merge is off or
// not installed, the plugin looks through the enabled mods and names each one whose patch tables are ignored.
//
// Paths are wide strings and the file system is read with plain Win32 calls: std::filesystem's code conversions
// do not link in the plugin's static UCRT build.

#pragma once

#include <string>
#include <vector>

namespace spares_supply {

// The folders of the enabled mods in the game's mods.json (a JSON array of {"enabled": ..., "folder": ...}), in
// file order, as UTF-8. Anything that is not such an object is skipped.
std::vector<std::string> enabledMods(const std::string& json);

// A mod's table patches: files under modFolder named <...>.<folder in lower case>.patch.xml / .patch.txt, as the
// table merge looks for them. Paths relative to modFolder, '/'-separated, UTF-8, sorted.
std::vector<std::string> tablePatches(const std::wstring& modFolder, const std::string& folder);

// tableMerge - the table merge's "enabled" setting as the loader gives it (null - the plugin is not installed).
// When it is not "true", one log line for every enabled mod (mods.json) under modsDir that ships table patches.
// Returns how many mods were named.
int warnIgnoredTablePatches(const char* tableMerge, const std::wstring& modsJson, const std::wstring& modsDir);

}  // namespace spares_supply
