#include "manifest.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

bool write_family_manifest(const std::string& out_dir, const Manifest& manifest) {
    const fs::path dir = fs::path(out_dir) / "manifest";
    std::error_code error;
    fs::create_directories(dir, error);
    std::ofstream out(dir / (manifest.family + ".sexp"), std::ios::binary);
    if (!out) {
        return false;
    }
    out << ";; " << manifest.family
        << ": written by pc_extract (tools/extract), see docs/assets.md\n";
    for (const std::string& entry : manifest.entries) {
        out << entry << '\n';
    }
    return static_cast<bool>(out);
}

bool merge_manifests(const std::string& out_dir) {
    const fs::path dir = fs::path(out_dir) / "manifest";
    std::vector<fs::path> files;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, error)) {
        if (entry.path().extension() == ".sexp") {
            files.push_back(entry.path());
        }
    }
    // families in name order, so the merged file comes out the same every run
    std::sort(files.begin(), files.end());
    std::ofstream out(fs::path(out_dir) / "manifest.sexp", std::ios::binary);
    if (!out) {
        return false;
    }
    out << ";; every family's manifest/<family>.sexp joined (pc_extract, docs/assets.md)\n";
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        out << text.str();
    }
    return static_cast<bool>(out);
}
