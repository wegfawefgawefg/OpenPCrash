#pragma once
// The whole extraction as a call: every family (or those named) from an opened disc into
// options.out_dir, then the merged manifest. pc_extract runs it from the command line and the game's
// first-run screen on a worker thread (src/setup), reading the progress and asking it to stop.
#include "disc.hpp"
#include "extract.hpp"

#include <string>
#include <vector>

int extract_family_count();
const char* extract_family_name(int family);
// a player's words for what a family makes ("Textures", "Sound and music")
const char* extract_family_label(int family);
bool extract_family_known(const std::string& name);

// 0..1000 over the whole run, through the family running too
int extract_permille(const ExtractProgress& progress);
// the families in only (every one if empty); false with why, or with progress->cancel set
bool run_extract(const Disc& disc, ExtractOptions options, const std::vector<std::string>& only,
                 ExtractProgress* progress, std::string& error);
