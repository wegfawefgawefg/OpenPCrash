#pragma once
// Where the tables the extractor reads sit in the retail XBE (virtual addresses), with the decomp's
// names (../phantom-crash-decomp/data/symbols.txt, src/data/*.c). Every one is proven against
// retail by the decomp's check.py.
#include <cstdint>

namespace xbe_at {

// asset registry: 1927 AssetRecord rows of 0x18 (docs/port/data.md section 4)
constexpr std::uint32_t asset_table = 0x285D58;
constexpr int asset_count = 1927;
// CabinetName {main, alt} per cabinet, 16 of them (D_001C3E98)
constexpr std::uint32_t cabinet_names = 0x1C3E98;
constexpr int cabinet_count = 16;

// stages, by selector stage * 2 + night (src/data/stage_scene.c)
constexpr std::uint32_t stage_model_assets = 0x2211C4;     // int[8]
constexpr std::uint32_t stage_collision_assets = 0x2211E4; // int[8]
constexpr std::uint32_t stage_objects = 0x221204;          // StageObject *[8], ended by model -1
constexpr std::uint32_t stage_object_lods = 0x221224;      // StageObjectLod *[8]
constexpr std::uint32_t stage_object_zones = 0x221244;     // u8 (*)[5] per object row, by zone
constexpr std::uint32_t stage_lod_mid_distance = 0x2211BC; // float, D_002211BC
constexpr std::uint32_t stage_lod_far_distance = 0x2211C0; // float, D_002211C0
constexpr std::uint32_t stage_entry_points = 0x2014B8;     // stage_entry_points
// stage lighting (battle_lighting_enter 0x44A98, src/data/ui_b2.c, ui_c.c, misc_e.c)
constexpr std::uint32_t stage_lighting_records = 0x2013E8; // D_002013E8: 0x54-byte record *[8]
constexpr std::uint32_t stage_lighting_variant = 0x4168BC; // D_004168BC, .bss, never written
constexpr std::uint32_t stage_shadow_colors = 0x201408;    // D_002013E4.f_24: ARGB[8]
constexpr std::uint32_t stage_light_directions = 0x201518; // float[8][3]
constexpr std::uint32_t stage_dof_levels = 0x203740;       // D_00203740 float[8]
constexpr std::uint32_t stage_heat_wave = 0x203760;        // D_00203760 u8[8]
constexpr std::uint32_t stage_lights = 0x281648;           // StageLights[8], 0x18
constexpr std::uint32_t point_light_default = 0x280CE8;    // BumpLight: pos, colour, range
constexpr std::uint32_t point_height_scale = 0x280604;     // D_00280604 float (100)
constexpr std::uint32_t lamp_default_position = 0x280CFC;  // float, then D_00280D00, D_00280D04

// fighters (src/data/misc_c.c, misc_d.c, sv_graphs.c, sv_motion.c)
constexpr std::uint32_t body_model_assets = 0x25AB88;         // int[7] by body class
constexpr std::uint32_t body_sticker_model_assets = 0x25ABA4; // int[7]
constexpr std::uint32_t leg_model_assets = 0x25ABC0;          // int[21] by leg class
constexpr std::uint32_t part_color_assets =
    0x25AAE0; // PartColorAssets {int *ids; int count}[21] by leg class
constexpr std::uint32_t arm_b_model_assets =
    0x25AC18; // D_0025AC18 int[34] by arm class (weapon[1])
constexpr std::uint32_t arm_a_model_assets = 0x25ACA0;  // D_0025ACA0 int[34] (weapon[0])
constexpr std::uint32_t arm_a_block_assets = 0x25AD28;  // D_0025AD28 weapon blocks (weapon[0])
constexpr std::uint32_t arm_b_block_assets = 0x25ADB0;  // D_0025ADB0 (weapon[1])
constexpr std::uint32_t back_a_block_assets = 0x25AF68; // back_wfb_assets_mount2, by back class
constexpr std::uint32_t back_b_block_assets = 0x25B000; // back_wfb_assets_mount3
constexpr std::uint32_t back_a_model_assets =
    0x25AE38; // D_0025AE38 by back class (weapon[2], back A)
constexpr std::uint32_t back_b_model_assets = 0x25AED0; // D_0025AED0 (weapon[3], back B)
constexpr std::uint32_t leg_graph_roots = 0x2415D0;     // D_002415D0 SvNode *[21]
constexpr std::uint32_t arm_b_graph_roots = 0x241628;   // D_00241628 SvNode *[34] (weapon[1])
constexpr std::uint32_t arm_a_graph_roots = 0x2416B0;   // D_002416B0 SvNode *[34] (weapon[0])
constexpr std::uint32_t arm_a_motion_assets = 0x2590A8; // D_002590A8 int[21][2] (weapon[0])
constexpr std::uint32_t arm_b_motion_assets = 0x259150; // D_00259150 int[21][2] (weapon[1])
constexpr std::uint32_t body_motion_assets = 0x2591F8;  // int[7], 7-slot sets
constexpr std::uint32_t leg_motion_assets = 0x259218;   // sv_leg_root_curve_table int[21], 37 slots
constexpr std::uint32_t arm_motion_class = 0x22F2B0;    // sv_arm_motion_class_table int[34]
constexpr std::uint32_t motion_timing = 0x22F3E0;       // sv_motion_timing_records [21][37]
constexpr std::uint32_t leg_radius = 0x22F338;          // D_0022F338 float[21][2]
constexpr std::uint32_t geometry_response = 0x232470;   // sv_geometry_response_table float[21][2]
constexpr std::uint32_t leg_attachment_assets = 0x259270; // sv_leg_attachment_table int[21]
constexpr std::uint32_t scoobee_loadouts = 0x19F920;      // rumble_scoobee_loadouts, 1190 x 0x3C
constexpr int scoobee_loadout_count = 1190;
constexpr std::uint32_t body_records = 0x1B5B88; // sv_body_table, 16 x 0x40
constexpr std::uint32_t leg_records = 0x1B5F88;  // sv_leg_table, 43 x 0xC0
constexpr std::uint32_t arm_records = 0x1BAE48;  // sv_arm_table, 76 x 0xC8
constexpr std::uint32_t back_records = 0x1BE9A8; // sv_back_table, 69 x 0xC8
constexpr int body_record_count = 16;
constexpr int leg_record_count = 43;
constexpr int arm_record_count = 76;
constexpr int back_record_count = 69;

// the tables family (tools/extract/tables*.cpp); rows and strides are in the writers
// parts (src/data/parts.c)
constexpr std::uint32_t chip_records = 0x1B7FC8;    // sv_chip_table, 96 x 0x7C
constexpr std::uint32_t option_records = 0x1C1F90;  // sv_option_table, 107 x 0x2C
constexpr std::uint32_t color_records = 0x1C31F8;   // sv_color_table, 57 x 0xC
constexpr std::uint32_t sticker_records = 0x1C34A8; // sv_sticker_table, 206 x 0xC
// roster (src/data/roster.c, identities.c)
constexpr std::uint32_t opponent_entries = 0x1B1008;  // rumble_opponent_entries, 1187 x 8
constexpr std::uint32_t reaction_profiles = 0x1B3520; // rumble_reaction_profiles, 67 x 0x8C
constexpr std::uint32_t identities = 0x1C5AF8;        // rumble_identities, 990 x 0x10
constexpr std::uint32_t identity_kinds = 0x1C98D8;    // D_001C98D8, 17 names
constexpr std::uint32_t pilot_records = 0x1C9920;     // rumble_pilot_records, 1060 x 0x1C
constexpr std::uint32_t team_names = 0x20C1F0;        // rumble_team_names, 84
constexpr std::uint32_t team_none_name = 0x206E20;    // D_00206E20, "None" for team -1
// AI tuning (src/data/roster.c)
constexpr std::uint32_t ai_transition_probs_normal = 0x205DA8;    // u8 [42][5]
constexpr std::uint32_t ai_transition_probs_alternate = 0x205E80; // u8 [42][5]
constexpr std::uint32_t ai_selection_profiles = 0x205F58;         // u8 [12][25]
constexpr std::uint32_t ai_stat_bands = 0x206088;                 // i16 [20][3]
constexpr std::uint32_t ai_d_0020611c = 0x20611C;                 // D_0020611C int[12]
constexpr std::uint32_t ai_nav_resource_selectors = 0x20614C;     // int[8]
constexpr std::uint32_t ai_d_00206170 = 0x206170;                 // D_00206170 float[5][4]
constexpr std::uint32_t ai_transition_result_map = 0x2061C0;      // int[5]
constexpr std::uint32_t ai_d_002061d4 = 0x2061D4;                 // D_002061D4 int[12]
constexpr std::uint32_t ai_level_timing_profiles = 0x206208;      // i16 [256][6]
constexpr std::uint32_t ai_search_move_weights = 0x206E08;        // i16 [8]
// tournaments, days and shops (src/data/tournaments.c)
constexpr std::uint32_t day_schedule = 0x249EC0;             // 60 x 0x3C
constexpr std::uint32_t tournaments = 0x24B2A0;              // 56 x 0x6C
constexpr std::uint32_t tournament_rules = 0x24D020;         // 28 x 0x2C
constexpr std::uint32_t opponent_pools = 0x24D4F0;           // 272 x 0x24
constexpr std::uint32_t dealer_stock = 0x24FB30;             // 65 x 0x18
constexpr std::uint32_t chip_stock = 0x250148;               // 24 x 0x48
constexpr std::uint32_t parts_stock = 0x250808;              // 135 x 0x48
constexpr std::uint32_t scoobee_stock = 0x252E00;            // 24 x 0x30
constexpr std::uint32_t class_tournaments = 0x253280;        // int[3][8]
constexpr std::uint32_t area_ranker_tournaments = 0x2532E0;  // int[3]
constexpr std::uint32_t first_ranker_tournaments = 0x2532EC; // int[1]
// effects (src/data/misc_b.c)
constexpr std::uint32_t impact_fx_rows = 0x253320; // impact_fx.rows, 11 x 0xC
// music (src/data/music.c, misc_b.c)
constexpr std::uint32_t music_catalogue = 0x1EFCC8; // music_sonic_amp_catalogue, 81 x 0x1AC
constexpr std::uint32_t music_files = 0x254398;     // char *[202]
// application states (src/data/state_tables.c)
constexpr std::uint32_t frame_cadence_table = 0x2559C8;     // int[160]
constexpr std::uint32_t state_pause_allowed = 0x255C48;     // int[160]
constexpr std::uint32_t app_state_restart_table = 0x255EC8; // int[160]
constexpr std::uint32_t state_music = 0x256148;             // int[160]
constexpr std::uint32_t state_pad_pause = 0x2563C8;         // int[159]
constexpr std::uint32_t app_state_boot = 0x256644;          // int
constexpr std::uint32_t app_state_entry_table = 0x256650;   // code pointers [159]
// conversations and screens (src/data/event_triggers.c, misc_k.c, movies.c, pepper_talk.c)
constexpr std::uint32_t event_policy_table = 0x1D0E00; // EventTrigger, 110 x 0x24
constexpr std::uint32_t event_predicates = 0x40E7A0;   // code pointers [75]
constexpr std::uint32_t movie_files = 0x25943C;        // char *[11]
constexpr std::uint32_t ending_subtitles = 0x17CF88;   // Subtitle {i16, i16, char *}, ended by -1
constexpr std::uint32_t pepper_talk_items = 0x1DE000;  // PepperTalkItem, 35 x 0xC
// the UI font's glyph order (src/data/misc_e.c)
constexpr std::uint32_t font_sjis_order = 0x27DE38;    // 2297 Shift-JIS pairs
constexpr std::uint32_t font_unicode_order = 0x27F030; // UTF-16 for glyphs 0..2221, 0-ended
constexpr int font_glyph_count = 2297;
constexpr int font_unicode_count = 2222;
// sound (src/data/misc_b.c, music.c, movies.c, stage_props.c, state_tables.c)
constexpr int music_file_count = 202;
constexpr std::uint32_t music_sonic_amp_catalogue = 0x1EFCC8; // MusicRecord[81], 0x1AC, wave +0
constexpr int music_catalogue_count = 81;
constexpr int music_record_stride = 0x1AC;
constexpr int state_music_count = 160;
constexpr int movie_count = 11;
constexpr std::uint32_t reverb_city = 0x22E8A0;   // ReverbPreset (DSI3DL2LISTENER), 0x30
constexpr std::uint32_t reverb_room = 0x22E8D0;   // ReverbPreset
constexpr std::uint32_t reverb_room_2 = 0x22E900; // ReverbPreset, the same values as reverb_room
constexpr std::uint32_t stage_ambience_enters = 0x22E840; // void (*)()[8] by stage selector
constexpr std::uint32_t volume_millibels = 0x410318;      // short[256], volume_millibels 0xDD2D0
constexpr int volume_level_count = 256;
// text and the font (docs/port/data.md "Text and the font", section 14; src/data/misc_e.c)
constexpr std::uint32_t glyph_widths = 0x1C3FB0; // u16 per glyph below 0x184: left, width
constexpr int glyph_width_count = 0x184;
constexpr int font_atlas_asset = 1684; // Media/scroll/ascii_font_sys_tex.bin, 18x18 cells
constexpr int font_cell = 18;
constexpr int font_cells_per_row = 28; // wide_glyph_rect 0xBE9AC: a 512-texel sheet / 18

// event scripts: EventCommand arrays (include/event.h). EVENTSEG is walked whole; the .data
// arrays sit in three runs (src/data/script_bios.c, script_barks.c, script_maker_*.c)
constexpr std::uint32_t bios_begin =
    0x1DE230; // Sonic Amp artist notes, pool and commands alternate
constexpr std::uint32_t bios_end = 0x1EFC9C;
constexpr std::uint32_t barks_begin = 0x291260; // battle barks and the tutorial
constexpr std::uint32_t barks_end = 0x400024;
constexpr std::uint32_t makers_begin = 0x40E180; // the dealer's maker talks
constexpr std::uint32_t makers_end = 0x40E68C;
constexpr int event_trigger_count = 110;
constexpr int reaction_profile_count = 67;
constexpr std::uint32_t music_records = 0x1EFCC8; // MusicRecord[81] of 0x1AC, bio at +0x1A0
constexpr int music_record_count = 81;
constexpr std::uint32_t dealer_screen_rows = 0x1FDC8C; // DealerScreenRows, talks[3] at +0x1C

constexpr std::uint32_t portrait_actors =
    0x1C42E8; // PortraitActor[28] of 0x28, texture asset first
constexpr int portrait_actor_count = 28;
constexpr std::uint32_t talk_backgrounds = 0x1D0DA0; // TextureLoad rows {asset, slot} by background
constexpr int talk_background_count = 6;
// 2D (src/data/render_registry.c, portraits.c, misc_f.c; docs/port/data.md sections 13 and 16)
constexpr std::uint32_t render_registry = 0x276598; // RenderEntry {float *rect; int handle}
constexpr int render_registry_count = 3763;
constexpr std::uint32_t portrait_actor_table = 0x1C42E8; // PortraitActor[28], 0x28 each
constexpr std::uint32_t portrait_sprite_table = 0x1C4748; // PortraitSprite[180], 0x1C each
constexpr int portrait_sprite_count = 180;
constexpr int portrait_slot = 534; // the handle portrait sets load at (534 + 2 * loaded slot)
constexpr std::uint32_t level_meter_a = 0x404350;       // AnimResource
constexpr std::uint32_t level_meter_a_parts = 0x400048; // AnimPart[7]
constexpr std::uint32_t level_meter_b = 0x408670;
constexpr std::uint32_t level_meter_b_parts = 0x404368;

} // namespace xbe_at
