#pragma once
// Where the game's files are. A development build reads them where they were built from, compiled
// in: the checkout's assets/, the build folder's shaders, the checkout's data/ and cooked/. A
// release (PHANTOMCRASH_RELEASE, scripts/release.sh) is unpacked anywhere, so there assets/ and
// shaders/ are folders beside the executable (or in a Mac app's Contents/Resources, which is what
// SDL_GetBasePath gives inside a bundle), and data/ and cooked/, made from the player's own disc
// on first run (src/setup), live in the per-user folder (SDL_GetPrefPath), as the saves and the
// run logs do. --data and --cooked override either.
#include <string>

#ifdef PHANTOMCRASH_RELEASE
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#endif

inline std::string game_path(const char* built) {
#ifdef PHANTOMCRASH_RELEASE
    const char* base = SDL_GetBasePath(); // with its separator on the end
    return std::string(base ? base : "") + built;
#else
    return built;
#endif
}

#ifdef PHANTOMCRASH_RELEASE
// the per-user folder, with its separator on the end
inline std::string pref_path() {
    char* prefs = SDL_GetPrefPath("PhantomCrash", "PhantomCrash");
    const std::string path = prefs ? prefs : "";
    SDL_free(prefs);
    return path;
}
#endif

#ifdef PHANTOMCRASH_ASSET_DIR
inline std::string asset_dir() {
    return game_path(PHANTOMCRASH_ASSET_DIR);
}
#endif

#ifdef PHANTOMCRASH_DATA_DIR
inline std::string default_data_dir() {
#ifdef PHANTOMCRASH_RELEASE
    return pref_path() + "data";
#else
    return PHANTOMCRASH_DATA_DIR;
#endif
}
#endif

#ifdef PHANTOMCRASH_COOKED_DIR
inline std::string default_cooked_dir() {
#ifdef PHANTOMCRASH_RELEASE
    return pref_path() + "cooked";
#else
    return PHANTOMCRASH_COOKED_DIR;
#endif
}
#endif
