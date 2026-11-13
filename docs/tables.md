# Tables

`pc_extract --only tables` (`tools/extract/tables*.cpp`) reads the game's static tables out of the retail XBE and
writes them to `data/tables/*.sexp`, one form per row, plus `data/manifest/tables.sexp`. Field names are the decomp's
(`../phantom-crash-decomp/include/tables.h`, `include/event.h`); a field the decomp hasn't read keeps its offset name
(`f_04`) and the file's header comment says what the data shows. Every row carries its row number as `(id n)`: ids
are what saves, builds, shop stock and code use, so rows are never reordered or dropped.

Conventions:

- Table strings are Shift-JIS in the XBE. They become UTF-8 through the font's own order tables (`font_sjis_order`
  0x27DE38, glyph i is pair i; `font_unicode_order` 0x27F030, the code point of glyphs 0..2221). Every table
  string maps except cp932 0xFA56 (full-width apostrophe, in two tournament names), which the font lacks and the
  extractor maps to U+FF07. Full-width Latin stays full-width ("ＨＬＢｏｄｙ"): it is what the game draws. Apart
  from the file names (movies, music files) the only half-width text is identity "Crackin", the team -1 name
  "None" and one space in a lyricist's name, so folding to ASCII would be lossy only there. A null string pointer
  is `nil`.
- Enums carry their number and a name: `(behavior 5 laser)`, `(geometry 3 hl_4roller)`, `(src 25 opening_movie)`.
  The number is the data; the name is a label.
- Floats are written with the fewest digits that read back to the same bits (`0.3`, not `0.300000012`).
- Pointers to code or to event scripts are XBE addresses in hex (`0x303210`); the event text family keys scripts
  by the same address.
- Trailing `; ...` comments on opponents, pilots and identities are names looked up for reading, not data.
- Units as the original: money in units of 10 New Yen, weights in hundredths of a ton, timers in 60 Hz frames.

## Stat curves

A tunable stat is `(curve base at_mid at_max mid negative)` (`CurveF`, `CurveU`, `CurveS`). `at_mid` and `at_max`
are added to `base`; they are not values themselves. With `a = |tune|` (tune -99..99) and `s = negative` when the
tune is below zero, else 1 (`Curve::value_at` 0x920E0, `rumble_curve_value` 0x92291, `float_curve_value` 0x9247D):

- `mid` 0: `base + a / 99 * at_max * s`
- `mid` 99: `base + a / 99 * at_mid * s`
- `a < mid`: `base + a / mid * at_mid * s`
- otherwise: `base + (at_mid + (a - mid) / (99 - mid) * (at_max - at_mid)) * s`

The result is a float clamped to 0..1e8. `CurveU` converts its words as unsigned (so a decreasing integer curve
would wrap); none of the 1419 integer curves in retail decreases, so this only matters for edited data.

## Files

| File | Rows | Decomp table | XBE | Source |
| --- | --- | --- | --- | --- |
| `bodies.sexp` | 16 | `sv_body_table` (`BodyRecord`) | 0x1B5B88 | `parts.c` |
| `legs.sexp` | 43 | `sv_leg_table` (`LegRecord`) | 0x1B5F88 | `parts.c` |
| `chips.sexp` | 96 | `sv_chip_table` (`ChipRecord`) | 0x1B7FC8 | `parts.c` |
| `arm_weapons.sexp` | 76 | `sv_arm_table` (`WeaponRecord`) | 0x1BAE48 | `parts.c` |
| `back_weapons.sexp` | 69 | `sv_back_table` (`WeaponRecord`) | 0x1BE9A8 | `parts.c` |
| `options.sexp` | 107 | `sv_option_table` (`OptionRecord`) | 0x1C1F90 | `parts.c` |
| `colors.sexp` | 57 | `sv_color_table` (`PaintRecord`) | 0x1C31F8 | `parts.c` |
| `stickers.sexp` | 206 | `sv_sticker_table` (`PaintRecord`) | 0x1C34A8 | `parts.c` |
| `scoobees.sexp` | 1190 | `rumble_scoobee_loadouts` (`BuildRecord`) | 0x19F920 | `roster.c` |
| `opponents.sexp` | 1187 | `rumble_opponent_entries` (`OpponentEntry`) | 0x1B1008 | `roster.c` |
| `reaction_profiles.sexp` | 67 | `rumble_reaction_profiles` (`ReactionProfile`) | 0x1B3520 | `roster.c` |
| `teams.sexp` | 84 + none | `rumble_team_names`, `D_00206E20` for -1 | 0x20C1F0 | `roster.c` |
| `ai.sexp` | 382 forms | `ai_transition_probs_normal` .. `ai_search_move_weights` | 0x205DA8 | `roster.c` |
| `identities.sexp` | 990 | `rumble_identities` (`IdentityRecord`) | 0x1C5AF8 | `identities.c` |
| `identity_kinds.sexp` | 17 | `D_001C98D8` | 0x1C98D8 | `identities.c` |
| `pilots.sexp` | 1060 | `rumble_pilot_records` (`PilotRecord`) | 0x1C9920 | `identities.c` |
| `tournaments.sexp` | 56 | `tournaments` (`Tournament`) | 0x24B2A0 | `tournaments.c` |
| `tournament_rules.sexp` | 28 | `tournament_rules` (`TournamentRule`) | 0x24D020 | `tournaments.c` |
| `opponent_pools.sexp` | 272 | `opponent_pools` (`OpponentPool`) | 0x24D4F0 | `tournaments.c` |
| `day_schedule.sexp` | 60 | `day_schedule` (`DaySchedule`) | 0x249EC0 | `tournaments.c` |
| `shop_stock.sexp` | 65 + 24 + 135 + 24 | `dealer_stock`, `chip_stock`, `parts_stock`, `scoobee_stock` | 0x24FB30.. | `tournaments.c` |
| `tournament_groups.sexp` | 3 + 1 + 1 | `class_tournaments`, `area_ranker_tournaments`, `first_ranker_tournaments` | 0x253280 | `tournaments.c` |
| `music.sexp` | 81 | `music_sonic_amp_catalogue` (`MusicRecord`) | 0x1EFCC8 | `music.c` |
| `music_files.sexp` | 202 | `music_files` (track number to WAV name; `on_disc` 89 of them) | 0x254398 | `misc_b.c` |
| `states.sexp` | 160 + boot | `frame_cadence_table`, `state_pause_allowed`, `app_state_restart_table`, `state_music`, `state_pad_pause`, `app_state_entry_table`, `app_state_boot` | 0x2559C8.. | `state_tables.c` |
| `event_triggers.sexp` | 110 | `event_policy_table` (`EventTrigger`), predicates named from `event_predicates` | 0x1D0E00 | `event_triggers.c`, `misc_k.c` |
| `movies.sexp` | 11 | `movie_files` | 0x25943C | `movies.c` |
| `ending_subtitles.sexp` | 13 | `ending_subtitles` (`Subtitle`), to the -1 row | 0x17CF88 | `src/od/u_01c2f0.c` |
| `pepper_talk.sexp` | 35 | `pepper_talk_items` (`PepperTalkItem`), the help menu | 0x1DE000 | `pepper_talk.c` |
| `impact_fx.sexp` | 11 | `impact_fx.rows` (`ImpactFx`) | 0x253320 | `misc_b.c` |

The scenes family (`pc_extract --only scenes`, `tools/extract/scenes.cpp`) writes four more:

| File | Rows | Decomp table | XBE | Read by |
| --- | --- | --- | --- | --- |
| `scene_cameras.sexp` | 17 states | `scene_camera_rows` (camera curves: roll, fov, eye, target; the root motion track; relative) | 0x258E68 | `scene_camera_sample` 0xADF9D, `scene_root_delta` 0xAE1E7 |
| `camera_rails.sexp` | 8 day modes | `camera_rails`, `camera_rail_zone_ranges` | 0x2329F8, 0x232AD8 | `camera_view5_rails` 0x7F5C1 |
| `barks.sexp` | 35 kinds | `bark_speaker_rule`, `bark_chance_human`, `bark_chance_fixed_opponent`, `bark_chance_ai`, `ranker_defeat_scripts` | 0x203768.., 0x2430CC | `actor_bark` 0x493F0, `scene_boss_defeat_enter` 0x897E1 |
| `trophies.sexp` | 50 pairs | `battle_tournament_trophy_map` (whole; the ui family has it cut in three) | 0x249D28 | `result_screen_setup` 0x94F6E |

Curves are written as the motion family writes them; camera z curves are already the port frame's, the root track
has the motion family's signs, rail points have z negated.

Named states are the decomp's `state_*_enter` names; 0x30..0x9D are `talk_30`..`talk_9d`, 159 is `none`. Each parts
stock row's note says which part kind the day schedule uses it for.

## Overlap and what is elsewhere

- The first-slice extractor writes `fighter_assets.sexp` (model, colour, graph and motion asset ids, motion timing,
  Scoobee loadouts and the part records' class fields; since 2026-11-11 also `leg-radius` D_0022F338,
  `leg-root-motion` `sv_geometry_response_table` 0x232470 and `leg-attachments`, the fixed attachment vectors of
  `sv_leg_attachment_table` 0x259270), `sv_graphs.sexp` and `stage_scene.sexp`. `scoobees.sexp`
  and the parts' `geometry`/`body_class` fields repeat some of that under the same decomp names.
- The effects family (`pc_extract --only effects`, `tools/extract/effects.cpp`) writes `effects.sexp`: the
  particle records with their curves, `effect_lists`, `effect_sets`, the sprite effect kinds, the lens flare's
  cells and pieces, all 13 `impact_fx` rows and `foot_landing_sets`.
- Not written here: stage tables (routes, props, lights, the ending's shot table), SV tables, the render
  registry, portraits, UI layouts and message boxes, the shot look table 0x2533C0,
  fonts and number fonts, event scripts, and loose UI strings (`shop_strings.c`, `shop_numbers.c`,
  `save_dialogs.c`, `net_strings.c`).
