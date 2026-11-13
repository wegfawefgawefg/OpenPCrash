#pragma once
// The scenes family: what the battle's scenes, cameras and barks read from the XBE's .data, to
// data/tables/ (docs/tables.md):
//   scene_cameras.sexp  scene_camera_rows 0x258E68: per scene state the camera curves
//                       (scene_camera_sample 0xADF9D), the root motion the fighter walks
//                       (scene_root_delta 0xAE1E7) and whether both are relative to the scene's
//                       focus and heading
//   camera_rails.sexp   camera_rails 0x2329F8 and camera_rail_zone_ranges 0x232AD8 (view 5)
//   trophies.sexp       battle_tournament_trophy_map 0x249D28 (the result screen's trophy)
//   barks.sexp          bark_speaker_rule 0x203768, the three bark_chance tables, and
//                       ranker_defeat_scripts 0x2430CC (actor_bark 0x493F0,
//                       scene_boss_defeat_enter 0x897E1)
// Curves are written as the motion family writes them (motion_curve_eval 0xAB02C's modes, times in
// ticks), with the readers' flips already in the port frame (z negated).
struct Disc;
struct ExtractOptions;
struct Manifest;

bool extract_scenes(const Disc& disc, const ExtractOptions& options, Manifest& manifest);
