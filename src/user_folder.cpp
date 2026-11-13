#include "user_folder.hpp"

#include <SDL3/SDL.h>
#include <filesystem>

namespace fs = std::filesystem;

std::string user_folder_migrate() {
    // the new folder (SDL_GetPrefPath makes it, so a new one is there and empty) and the old one
    // beside it under the same base
    char* prefs = SDL_GetPrefPath(user_folder_org, user_folder_app);
    if (!prefs) {
        return "";
    }
    const fs::path now = fs::path(prefs).parent_path(); // without the separator on the end
    SDL_free(prefs);
    const fs::path old = now.parent_path().parent_path() / "PhantomCrash" / "PhantomCrash";
    std::error_code error;
    if (!fs::is_directory(old, error)) {
        return "";
    }
    if (!fs::is_empty(now, error)) {
        return "user folder: " + old.string() + " and " + now.string() +
               " both hold files; the old one is left as it is";
    }

    // a rename on the same drive, else a copy and the old one removed
    fs::remove(now, error);
    error.clear();
    fs::rename(old, now, error);
    if (error) {
        const std::string why = error.message();
        error.clear();
        fs::copy(old, now, fs::copy_options::recursive, error);
        if (error) {
            const std::string copy_why = error.message();
            fs::remove_all(now, error);
            fs::create_directories(now, error);
            return "user folder: " + old.string() + " couldn't be moved to " + now.string() +
                   " (" + why + "; copy: " + copy_why + "); the old one is left as it is";
        }
        fs::remove_all(old, error);
    }
    // the old org folder, if nothing else is in it
    fs::remove(old.parent_path(), error);
    return "user folder: moved " + old.string() + " to " + now.string();
}
