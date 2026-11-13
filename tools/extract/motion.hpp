#pragma once
// The motion family: the curve sets (registry kind 4 that aren't the 392-byte attachment tables;
// docs/port/data.md section 7) to data/motion/<id>_<cabinet>.sexp. Curve modes are kept as the
// game evaluates them (motion_curve_eval 0xAB02C: step, linear, ease and the Hermite variants),
// times in 60 Hz ticks. The readers' flips are baked into the port frame: rotation curves are read
// as (-x, y, -z) and translation as (x, -y, z) (sv_articulation_sample 0xAE640), then the port
// frame negates z, so a file's rotate is (x, -y, -z) of the disc's and its translate (x, -y, -z).
// The 0x3800 group (footstep markers, motion_select_descriptor 0xAD6B7) is kept as stored.
#include <cstdint>
#include <string>
#include <vector>

struct Disc;
struct ExtractOptions;
struct Manifest;

// one curve at byte offsets keys and values of bytes (keys 0: a null track, "(none)"), as the
// motion sets write it, every value times sign; both offsets moved past it (the weapon blocks'
// clips use the same encoding, docs/port/data.md section 10)
std::string motion_curve_text(const std::vector<std::uint8_t>& bytes, std::uint32_t& keys,
                              std::uint32_t& values, float sign, bool& ok);

bool extract_motion(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
