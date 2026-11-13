#pragma once
// The manifest: what each original asset or table became. Each family collects its own entries and
// writes data/manifest/<family>.sexp; merge_manifests joins every family's file into
// data/manifest.sexp, so families can run alone (--only) without losing the others' entries.
#include <string>
#include <vector>

struct Manifest {
    std::string family;
    std::vector<std::string> entries; // one top-level form each
};

bool write_family_manifest(const std::string& out_dir, const Manifest& manifest);
bool merge_manifests(const std::string& out_dir);
