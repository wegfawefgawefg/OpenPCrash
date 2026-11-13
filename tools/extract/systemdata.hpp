#pragma once
// The systemdata family: the loose pictures in Media/systemdata the renderer loads by file name
// (bump_light.tga, the water's yurayuraTest.bmp and wave02.bmp...) to data/textures/systemdata/.
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_systemdata(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
