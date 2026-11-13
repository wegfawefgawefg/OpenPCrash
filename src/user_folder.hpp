#pragma once
// The per-user folder (SDL_GetPrefPath): the saves, gubsy's settings and bindings, the run logs,
// and in a release data/ and cooked/ (src/game_paths.hpp). OpenPCrash/OpenPCrash
// (~/.local/share/OpenPCrash/OpenPCrash/ on Linux, %APPDATA%\OpenPCrash\OpenPCrash\ on Windows,
// ~/Library/Application Support/OpenPCrash/OpenPCrash/ on a Mac). Builds before 2026-11-13 kept it
// in PhantomCrash/PhantomCrash, which the first start of a newer one moves over.
#include <string>

inline constexpr const char* user_folder_org = "OpenPCrash";
inline constexpr const char* user_folder_app = "OpenPCrash";

// the old PhantomCrash/PhantomCrash folder moved to the new one when the new one is empty (renamed,
// or copied and removed across drives); left alone when both hold something. What it did, for the
// run log ("" when there was no old folder)
std::string user_folder_migrate();
