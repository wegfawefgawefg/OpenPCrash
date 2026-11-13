#pragma once
// The models family: every model set (registry kind 0) to glTF 2.0 under data/models/
// (stages/ for the stage environments, mechs/ for fighter parts, misc/ for the rest), one manifest
// entry each.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_models(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
