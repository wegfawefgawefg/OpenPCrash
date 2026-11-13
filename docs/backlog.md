# Backlog

Minor issues, open questions, small deviations and polish, one task a line with where to look, grouped by area. Work
carries on past them (AGENTS.md, "Backlog"); only real trade-offs that change how the game plays go to the owner.
Bigger work goes in `docs/plan.md`.

## Checks against the original

- [x] No emulator runs for now (owner, 2026-11-11). The rows marked UNKNOWN in docs/bugs.md are settled from the decomp's code and data instead (owner, 2026-11-12: follow the decomp where it reads clearly, else decide and log it), each row with its evidence; none is left.
- [x] Any positive camouflage options total gives at least 80 ticks hidden (the Pocket Watch cloak; `camo_fade_ticks` + `camo_hold_ticks`, src/mech/fighter_camo.hpp): settled from `camo_update` 0x74E00, which runs the 60-tick fade and the 20-tick hold before state 4 first compares the time left with 60 (docs/bugs.md).
- [x] The camouflage shader's k a human sees is 0.25 whenever an AI fighter runs after them, not their chip's `camo_distortion` (`camo_frame_k`, src/render/camo_draw.cpp): settled, every running fighter task sets it before its step and only the draw reads it (docs/bugs.md).
- [x] Firing doesn't end camouflage: settled from the decomp. State 7 is entered only by `camo_enter_state_7` 0x753C4, whose two callers are `actor_settle_damage`'s damage and death paths (src/hand/mech_part_assets.c); the other writers are `camo_update` itself (X while cloaked gives state 5, not 7) and the death scene's fade of the killer's cloak (4 to 5). Nothing on the fire path touches `camo_state`, so the runtime note's 7 can only have been a hit landing in the same moment. The port's camouflage is as read (src/mech/fighter_camo.cpp).
- [x] Corpses take hits until their slot retires (`corpses_take_hits`, src/weapons/shot_hits.cpp): settled, `shot_hit_fighters` 0x9C942 has no test of `alive` (docs/bugs.md).

## Input

- [x] The 0x4000 stick dead zone is half of every axis: LEFT and RIGHT DEAD ZONE (PC CONFIG, INPUT, default the original's 50) make it smaller for modern pads, OUTER DEAD ZONE reads a worn stick's end as full, STICK CALIBRATION rescales a pad that rests off centre or falls short (docs/input.md).
- [x] The turn clips' names are crossed like the steps' were: `slot_turn_left` (8) is the right turn (the idle's `right ? slot_turn_left`; `actor_strafe_turn_start` 0x8DBEA starts 8 for a strafe right). Names only: the clips play the right way (taste pass: renamed, same numbers)

## Stages

- [x] The day stages' prop sets lost the pictures only a section's second stage names (texture_set_load loads a set whole; the port loaded each section's first picture): the gates' bump maps on Shinjuku (set 1575's `07.png`, slot 328), Shibuya (1577's `04.png`), Aqualine (1579's `07.png`) and the test arena (1581's `02.png`) drew with white. `load_set` now loads the rest of the set (src/stage/stage_view.cpp).

## Effects

- [x] The paper storm (sets 0x41..0x4E): src/effects/stage_effects.cpp starts it on the ending's craft jets and follows `ending_paper(world)`'s anchors (points D_0098CA14 and the two after it), each group shown by its flag.
- [x] The paper storm starts on a CraftJets event at route segment 0 (the install; retail's craft state 8): settled from the decomp, the same. `supply_craft_update` 0x670FC starts the storm only from state 8, which only `stage_route_install` 0x67967 sets; the other jet starts are `supply_craft_resume` (after a pause: boss scenes, none in the ending) and the release's end (`segment >= route_release_end`). Only a `route_release_end` of 0 or a resume at segment 0 would tell them apart; an event of its own if the craft's step ever changes.
- [x] Lamp trails on Shibuya by day (rumble 2) in its zones 2..4, let go elsewhere there (`actor_effects_update`'s `shibuya_zone_at` test at the fighter's position, src/effects/lamp_trails.cpp).
- [x] Lamp trails in a replay: a human's are let go in replay view 0 (`actor_decals_step`, `replay_view`: `Effects::replay_view` from the battle's cameras), beside the live fight's cockpit view (`own_lamps_hidden`).
- [x] The lamps' zone ranges (`multi_matrix_pl`, `lamp_ranges[zone]`): on Shibuya (selectors 2, 3, `game_get_tournament_day_mode`) the zone is `shibuya_zone_at`'s at the batch's first bone, else 0 (src/render/mesh_constants.cpp).
- [x] A fighter's contact decals (`sv_contact_route` 0x7C740 on surface type 8, bit 0x2000): settled from the decomp, retail puts the ring on the face the sphere met (`collision_face_test`'s matrix: rows v2 - v0, the normal, v1 - v0 at the sphere's centre, through `matrix_axes_normalize`, dropped 2 along the normal). Type 8 is the stages' edge walls, so the ring stands on the wall; the port had it 2 under the fighter's feet, under the floor. Now the face is found again from the sphere where the step left it (`collision_surface_face`, src/effects/contact_decals.cpp; `captures/batch_fx_sound/edge_decal*.png`). Retail's matrix can be stale (an edge or corner push sets 0x2000 without writing it) or the upper sphere's; the port takes a type 8 face either way.
- [x] The muzzle flash 0x3F3 (muzzle kind 2) is always additive, as retail's are from the first flame shot of a battle on (`model_slot_flags_or(0x3F3, 0x8000)`; docs/bugs.md).
- [x] The shield bubble shows on everyone outside a scene while immune, the player too during the entrance's 300 ticks of immunity after the intro: settled from `actor_draw` 0x82DE0's test (`controller == -1 && !actor_is_fixed_opponent_role(roster) || !scene.state`, then `immune_frames > 10 && durability > 0`), which `bubble_shown` is.
- [x] The effopt and rainbow animation (glow, scroll, band phase): settled from the decomp, retail steps them per draw call (`effopt_glow_time` 0x71955 per decal drawn, `shader_constants_rainbow` 0xC432E per mesh drawn in every view), so they ran faster with more on screen and in split screen; docs/bugs.md fixes them to once an update. The bubble's spin and the rainbow's two phases had still stepped once per bubble: now once an update (src/effects/shield_bubble.cpp).
- [x] Settled from the decomp, as read (`actor_land` 0x90AFD sets phase and phase_prev alike and `motion_changed`; the pose build's streams need `motion_phase != motion_phase_prev`): A plain jump's landing now starts no foot landing set, as read: `actor_land` sets `motion_phase_prev` to the phase, so its motion change meets the `motion_phase != motion_phase_prev` test, and the recovery starts at 105 or later (`motion_end - (motion_end - 105) x bounce`), past the Plain Holy fall clip's touchdown key 97 and ending before its 134. The falling row of `foot_landing_sets` (smoke 0x1B) then shows only where a fall clip's keys lie past the recovery's start. Check against a retail capture of a landing.
- [x] The beam is turned to each view's eye; retail turns it to view 0's in every view (`weapon_laser_controller` 0x9B97E): settled, a retail bug the port fixes (docs/bugs.md, "In split screen every view shares one fov...").
- [x] Validation (lavapipe): the overlay's load pass was begun on the final target's framebuffer made for the screen pass, with other subpass dependencies (VUID-00904, 02684); every pass now takes `vk_pass_dependencies` (src/render/vk_target.cpp).
- [x] The body sticker model (0x592 + slot, `body_sticker_model_assets`) with the build's sticker (texture set 1349 + sticker) at the body matrix: pass 2 with flags 0x400, cloaked pass 8 with alpha timer / 60 (states 1, 2), (60 - timer) / 60 (5, 6), 0 in 3, 4, 7, 8 (`mech_sticker_items`, src/render/scenery.cpp; the stands draw it too).
- [x] The sticker, the body and its shadow are left out in the cockpit view, the weapons drawn (`actor_draw`'s `flag_3`: `own_body_hidden`, src/view/cockpit_view.cpp, `FighterPose::body_hidden`).
- [x] The camouflage's reveal sphere sits on the aim point before the step's settle, as retail's `camo_update` (before `actor_settle_damage` in `actor_step` 0x756F3) reads it (`take_step_poses`, src/view/view.cpp).
- [x] A muzzle flash is placed on the barrel's current pose at the 30 Hz update; one mount can't fire twice between two updates (the quickest refire is 2 steps), but a pull of several shots steps the barrel before each flash (`weapon_muzzle_matrix` with `flag2`), and every flash had taken the pull's last barrel: the muzzle event carries its barrel now (`CombatEvent::effect`, `live_muzzle`, src/effects/combat_effects.cpp).
- [x] The lens flare places the sun over the drawn 3D (`Effects::drawn_3d`, set by the scenery) instead of retail's 576 x 448 safe area, unclipped (`anchor_free`); the fade and turn measure in retail's units (docs/screens.md "Widescreen").

## Weapons

- [x] The weapon clip's opening sound: `event_weapon_open` from the fighter step (src/world/world.cpp) when a human's locked weapon leaves rest, played by the HUD as bank 3 cue 0x15 at the fighter.
- [x] An AI's lockable weapons (pods with open and close frames) stay shut: settled from `weapon_anim_update` 0x9B08B (only `controller != -1 && anim_next == 1` leaves state 0, and nothing else writes the model's nodes but `weapon_anim_step`), so an AI fires from the model file's rest pose (docs/bugs.md).
- [x] The port's untouched weapon pose is the file's rest pose, which an AI's shots leave from: checked from the data (2026-11-12). The extractor writes each node's translation and Euler angles from the model file (`model_decode.cpp` +0x20 and +0x2C, mirrored in z, the angles in `extras` with `%.9g`, so a float comes back exact), `read_gltf` reads them back, `load_model_nodes` takes them into `RigModelNode` and the pose starts from them; only `weapon_anim_step` writes the nodes after that, as retail's `model_node_write_transform` only runs from it (`anim_player_start` in `weapon_anim_init` 0x9AEF5 samples nothing).
- [x] The weapon clips' scale channels (0x2000..0x800) aren't applied: settled, moot. `model_node_write_transform` would write them into the node scale that `weapon_attach_matrix` 0x9A954 multiplies into the muzzle chain, but no retail clip has one.
- [x] The weapon models' nodes' static scale: checked from the data (2026-11-12), every node of objects[0] of the 141 weapon model assets (arm-a, arm-b, back-a and back-b models; 877 nodes) is (1, 1, 1) but ten that are one float step off it (0.99999994 or 1.00000012 on one axis): the Tolomeo arms (assets 144, 146, nodes 0, 1, 3, 4) and the Slime arms (154, 156, node 0). Through `weapon_attach_matrix` they move a muzzle by about 1e-7 of its offset, under the world's Q16.16 step, so `fighter_muzzle` taking the node unscaled gives retail's points; nothing to carry.
- [x] The model stands draw from `build_mech_pose` (float), not the world; the world's step already has battle_phase 5's skip (`stand`: no clips, no back aim, src/mech/pose_weapons.cpp) for a stand run on it. Nothing to do: the stands are front-end presentation and stay float.

## Garage and shops

- [x] Assemble: the BODY SOCKET HOLE NUMBER counts ("2", "5") drew over the machine, left of their panel, in a wide window: the assemble screen's anchors took only the digit 0 (sprite 3693) right with the panel. The counts are the number font's digits 3693..3702, and the rule now takes those inside the counts' region (520, 120)..(600, 150) (assets/ui/anchors.sexp), so they sit either side of the "/" as at 4:3 (retail's 0x224 and 0x23E, y 0x84). captures/screens_left/assemble_{before,after}_wide.png.

- [x] An incomplete build on a stand is laid out as the parts it has (`scoobee_parts_draw_spread`, src/screens/garage/stand_parts.cpp): the garage's and the spec page's view 0 places, the hangar's view 6.
- [x] The model stand loads on a worker as retail's thread does and swaps when done (src/screens/garage/stand_loader.cpp): `load_mech` is three steps (src/mech/mech_load.cpp), the packs opened on the main thread, the models, motions and texture blocks read on the worker, the uploads and closes back on the main thread; one load at a time, only the latest ask waits, and every screen's destroy waits for it (`stand_settle`) so nothing closes its packs under it.
- [x] The stand's places (`model_viewer_places`, `model_viewer_cameras`' lights, D_00241BB8, D_002417C8, D_00242038, D_00242648, D_002426BC..D_002427C0) come from the ui family's `model_viewer.sexp` (tools/ui_tables.py reads sv_attach.c); data extracted before this needs `pc_extract --only ui`, or the stands stay off. The per-class points D_00242648 names are code (func_0011DC90 and three more write them at boot), so they stay in `model_stand.cpp`.
- [x] The spread's scale-in: retail draws the parts at 0 while `viewer.grow` is above 0, counting it down, but `model_viewer_build_slot` zeroes grow on every load and only `model_viewer_draw_grow` (stock, Plus Tech, Wild Machines' parts) raises it, so on the garage and the spec page the spread is at full size from its first frame, as the port draws it.
- [x] A weapon fitted alone on a shop's stand takes leg class 0's colour 0 paint (docs/bugs.md); retail leaves texture slot player + 0x20A as the last load left it (UNKNOWN).
- [x] Plus Tech (mode 4), Wild Machines (mode 3, tabs 0..3) and stock (mode 1) show the part under the list's cursor growing in on the stand (`model_viewer_fit_part`, `model_viewer_draw_part_effect`).
- [x] The shops' stand model 0x4D9 (`shop_stand_model_draw`) isn't drawn: point_mdl's first object is a unit shape at x 10, 12 in front of the menu camera (52 degrees): outside retail's 4:3 view, so retail never shows it; a wide window would.
- [x] Wild Machines' used Scoobees and the dealer's three stand on a line-up (`stand_line_*`): each loads in turn (the cursor's first), the cursor's slides in to its leg class's point 5% a frame and the others out 8 to the side, turning; leaving the used list hides them (`_used_shown_machine_update`, `_used_model_slot_pick`, `dealer_shown_machine_update`, `dealer_model_slot_pick`, `model_viewer_actor_update`).
- [x] PC: the machine on any stand (garage, spec page, stock, Plus Tech, Wild Machines, the dealer, the hangar's cursor) turns by a left drag inside the screen's stand area, kept clear of its menus' hit boxes, and by V / B held (TURN STAND LEFT / RIGHT on PC CONFIG's BINDINGS); its own turn waits meanwhile. Retail has no stand control on the pad, no tilt and no zoom, so the pad gets none.
- [x] A player's own binding profile with both TURN STAND rows cleared kept getting V / B back at every start: the migration for profiles from before the rows runs once now (`phantomcrash.input.stand_keys_given` in the install's settings, src/input/bindings.cpp).
- [x] The stands' grab areas are rectangles per screen and step (`stand_area_set`) in the 640x480 layout. Checked 2026-11-12: a wide window widens the 3D's horizontal field at the same vertical one, so a stand's machine lands at the same layout point at 4:3 and wide, and the areas cover it in both; the menus beside them are centred or move outward, never into an area. The assemble stand's machine lands at layout x 388..597 at both 640x480 and 960x540 inside its area (340, 96)..(640, 330), and a drag there turns it at both (captures/screens_left/stand_*_area.png, the area outlined).
- [x] Name entry from the new game's opening (0x30) and from Club Wired (8) plays the shuffled Sonic Amp pool until the name is accepted (`music_shuffle_task_start`); Club Wired itself already pumped it.
- [x] The spec page's label panels (`spec_sprites` rows 6 and 10, sprites 2541 and 2542) and its frame (row 3, 2544) drew at (0, 0), the XBE table's words. Three boot constructors write them (decomp src/o1/u_11cf00.c: `func_0011CF00` row 3 at the header's origin D_0019D5A8 (51, 104), `func_0011CF3A` row 6 and `func_0011CF15` row 10 at the origin plus (25, 80)); src/screens/garage/spec_screen.cpp places them so. captures/screens_left/spec_*.png.
- [x] Stock's card pops its two label plates (0xA29 / 0xA2A, options 0xA2C / 0xA2D) before the card, with the kind's picture and the Chip, colour and sticker corner mark (`stock_option_card_draw` 0x3D4A7); the rows wait for the list's frame but the card doesn't (`stock_list_draw`).
- [x] The load screen's help line (`D_001DDAE4` -> `D_001DDAA0`, "Load play data"): the ui family extracts it (`save_load_help_lines`; a fresh `pc_extract --only ui` writes the same `save.sexp` as main's `data/`) and the load screen scrolls it.

## Menus

- [x] The day and money on the options, sound config and PC CONFIG screens, unless opened from the title.
- [x] The save and load slot card's date, time, Chip picture and beaten mark, at the places their vec2 statics' constructors set (`src/od/u_11d960.c`..`u_11d9c0.c`); the port's slot number moved under the card.
- [x] The screen adjust moves the 3D viewports with the 2D (render.md "viewports": retail moves every viewport by `screen_shift_x/y` before SetViewport): `Renderer::screen_shift` from `ui_present`, `scene_viewport` and the depth probe in src/render/vk_frame.cpp, the effects' 2D in scenery.cpp. Pass 15 (the fade) and the full-window layers stay on the glass. captures/screens_left/adjust_*.png.
- [x] Two sprite-at helpers did one job: `sprite_at` is gone, every caller draws with `sprite_draw_at` (screen_kit).

## Widescreen (assets/ui/anchors.sexp, docs/screens.md "Widescreen")

- [x] Soft reflections leave a visible seam at the 4:3 edge (town map, hubs); the town map's blurred sides show mirrored place markers: `(feather 24)` fades the picture into its soft copy, `(lines P)` keeps the scanlines drawn into the art running on sharp and in phase (the blur steps by whole periods), the town map's sides carry on a mark-free strip of its camo (`fill`).
- [x] Hub and conversation footers: the now-playing line stops at its old right edge, leaving an empty strip before the moved logo and meter: `(ticker-end ...)` moves the line's clip end with the footer's right part.
- [x] Sonic Amp: the header line starts at its old left edge, under the moved diagonal: the headers open at column 260, a plain run (288 cut into the 640x72 headers' diagonal), and the banner line runs on with the header (free).
- [x] Town map: the clamped logo rows give horizontal camo streaks beside the logo: the camo stops at row 356 (left) / 374 (right), below that only the flat bands repeat.
- [x] Hubs: the header extension's top-right corner is dark; DAY/money/motto stay centred because the motto is baked into the picture: the right side is the photo from the black top down (reflected softly), the tab carried on from its plain edge strip; the hubs' framed pages get the frame's header and footer (`shows`).
- [x] Title at 21:9: a thin vertical line in the top and bottom black bands near the left edge: an extension starting between pixels was floored short of the picture; ends now land on whole pixels.
- [x] Hangar at 4:3: the next machine pokes into the black border on the right (as before the box_43 change); decide whether to clip it: the menus' 3D is scissored to the 2D's reach (`FrameSetup::clip_to_2d`), retail's safe area at 4:3.
- [x] Not yet captured wide: 4-way split, the spec screen, a lettered fight scene (`captures/widescreen3/`).
- [ ] Town map: the sides are a blurred continuation of a mark-free strip of the camo (rows 26..78), slightly lighter than the map's darker edges and softly mirrored every ~55 rows; a sharp continuation would need camo art wider than the original's.
- [ ] Hubs: the header tab is carried on from its 12-texel plain edge strip, softened; its glints repeat faintly every 24 texels at 21:9.
- [ ] Split views keep retail's 4 px gaps between views as dark dividers, darkened a little more by the composite's soft focus near each view's edge.

## Scenes and cameras (src/view/scene_camera.cpp, src/battle/battle_scenes.cpp)

- [x] The door scene, a boss's entrance (scene 5) and an Area Ranker's defeat captured headless (`--route`'s `door`, `kill`
  and `shot:`; `captures/playthrough/door_scene.png`, `boss_entrance_*.png`, `ranker_defeat_*.png`, docs/playthrough.md).
- [x] The First Ranker's entrance (scene 5, everyone else paused): retail hides the player's HUD, `hud_update` 0x4E62E stops on
  a slot with `battle_slot_bookkeeping & 0x24`, which the boss entrance sets on everyone else; the port's `hud_shows` checks
  `parked` the same way. The same gate shows the HUD in free play's scenes but the door and the intro (the lessons).
- [x] The rail cameras test the props too (`prop_ray_test` mode 0 in `camera_view5_rails`, `rail_camera`).
- [x] Scene curves are sampled at display rate: each step's pose keeps its row and frame (`SceneCurveAt`), and a drawn frame between two steps samples the curves at its own fractional frame, the entrance's blend into the fighter's camera with it (`scene_curve_pose`, src/view/scene_camera.cpp, `interpolate_camera_pair`). Presentation only (decomp cameras.md: `scene_camera_sample` changes nothing).
- [x] Some entrances (rows 1..4) end low: settled from the decomp, the port's camera is retail's (`scene_camera_sample` 0xADF9D, `motion_curve_eval` 0xAB02C, the blend in `camera_scene_update` 0x80068, the focus from `scene_gate_entry_start` 0x824D2). At frame 520 (the blend's start) rows 1..4 look at the walk's end point 2.2..3.1 up, pitched down 9 to 25 degrees, not the ground; a picture that shows the ground would come from the root walk or the focus, not the camera.

- [x] The ending (0xA): the carried durability and weapons (`Story::ending_carry`, kept by `boss_exit` for 0x17; the durability and breaks at the ending's spawn, the rounds and fitted flags in every shot's lead-in).
- [x] The carried breaks come back at weapon_state 4 (the arcs): settled, the same as retail's carried `EndingWeaponParts` (`weapon_state`, `weapon_break_timer`, copied at the end of scene 0x17 and restored in `fighter_spawn`). A state under 4 needs a break in the last ~16 updates before the 240-tick defeat scene ends, which no fight reaches.
- [x] The ending: the dissolves between shots 0xC..0x10 (`TerminalRow.f_0c` capture 30, `frame_to_texture_for`): the last frame of the shot (the renderer's capture 0x225) fades out over the next for 30 updates in pass 14 (`frame_capture_fade_draw`).
- [x] The ending: the last shot's cockpit view (`scene_ending_enter` sets view 0, `hud_update` shows the HUD in scene 0x12, `actor_draw` hides the player) with its instruments.
- [x] The ending: the signal's 3D sound (bank 3 0x27) at the shot's focus (`battle_events_play`).
- [x] The ending's table: main's data/ has `tables/scene_ending.sexp`; the campaign run played the shots with their lines.
- [x] The ending's clip advance: settled from the decomp, the port was wrong. `actor_leg_walk_speed` 0x76289 is the leg class's root motion walk column (`Fighter::walk_scale`, units a clip tick), not the walk rate; the port had divided by `walk_rate` for legs without root motion and stepped root-motion legs 1 a step (their feet skated). The gate walk (`scene_intro_update` 0x8920C) and the door's (`scene_door_update` 0x8A0E1) had the same divisor. All three take `walk_scale` now, with retail's fallbacks (src/battle/battle_ending.cpp, battle_scenes.cpp; docs/sixty.md).
- [x] Replay views go through the world's view functions (`fighter_view_of`, the chase from the replay's own `eye_replay`), retail's `camera_view_blend(replay_view, replay_view)` in `actor_camera_update` 0x7DCAA. The replay's view 0: settled from the decomp, retail doesn't tilt its up (a human outside state 7 takes `camera_scene_update`'s plain-up branch) but `camera_view0_cockpit` 0x7E881 adds the hit jitter to its target; now the port does too, by CAMERA SHAKE (`replay_shake_apply`, src/view/cockpit_view.cpp). Its lamp trails are let go as retail's (`actor_decals_step`).
- [x] A dying human's own camera (scene 7, step 0, up to 330 ticks) is drawn from its `view_matrix` as it stands (`camera_scene_update` 0x80068), so in view 0 retail shows `camera_shake_up`'s tilt for up to 15 updates after the killing hit. The tilt is the world's (`FighterCamera::up`); the drawn camera now rolls by the angle that turns the plain look-at's up into the tilted one's, times CAMERA SHAKE (`dying_tilt_apply`, src/view/cockpit_view.cpp), live and in a replay (whose look-at takes the tilt by the fighter's own view 0 in any replay view). Outside that step retail draws a plain up, as before.
- [x] The replay records the whole `Inputs` a step (36,000 at most, a few MB): decided to keep it. A few MB is nothing on a PC, the record is what the replay check compares, and trimming it would need a second input type to keep in step with `Inputs`.

## Barks (src/hud/hud_barks.cpp, src/battle/battle_barks.cpp)

- [x] The beam's sweep against the supply craft and crates (`supply_craft_beam_hit_test`, src/weapons/shot_pass.cpp): the craft down for +100 with bark 0x16.
- [x] The idle bark's clock reads the fighter's own pad (`Fighter::pad_held`, `func_000DF1C0`); an AI's `PADREC[-1]` is taken as nothing held (docs/bugs.md).
- [x] A rental's bark names its chip (`rental_chip_name_get`: the chip's identity name, else the chip's own).
- [x] The bark interpreter reads player 0's pad: accept finishes a typed line, the skip chord ends waits and delays (script_flags 1).
- [x] A bark's line never finishes on A: settled from the decomp. `event_cmd_text` 0xC5AFD takes accept (A or START, `input_accept_pressed`) only for a typed line without the hold bit, and none of the 929 bark scripts types (all speed 0, flags 0x80000000) or waits for a page; `event_cmd_delay` 0xC696F ends only on the 0xC0C0 chord. So in a bark only the chord counts; A finishing a line is the scenes' text (ranker, lessons, ending). src/talk/talk_interp.cpp matches.
- [x] Barks longer than a boss defeat's 180 ticks: settled from the scripts, many are (550 of the 929 run past 90 updates; the longest 345, ENTER_NEW_BOSS and ENTER), so retail's defeat runs on while one plays when a beaten-before ranker or another rule leaves it running (docs/bugs.md). The port's fix stands.

## Result and records 3D

- [x] The result screen's demo battle: retail's `demo_battle_enter` mutes banks 1..3, and the stage ambience is bank 1's
  cues 0x14..0x1D, so only the result's music plays under the sheet, as the port does (decomp audio.md).
- [x] Records: the trophies (the First Ranker's over its card, the grids') were pass 13 over the 2D, as the result's. `record_model_draw` 0x3347C submits them with `model_render_submit(.., 2)`: render pass 2, under the cards' and grids' 2D of pass 12 (the result's is `.., 0xD`). `trophy_draw` takes the pass (src/screens/system/trophy_models.cpp). The First Ranker's now turns behind the card's dark glass as a shadow, its portraits and won marks over it (captures/screens_left/records_{before,after}.png).

## Sound (docs/audio.md)

- [x] The weapons' sounds: fire by muzzle callback, casings, the laser's beam, the melee swing and hit, a shot's hit, an AI rocket's flight, the wreck (src/view/combat_sounds.cpp).
- [x] The laser's hit, 1/0x12: `event_beam_hit` from `shot_hit_fighters`' beam and `impact_volume_scan`, on even steps (src/view/combat_sounds.cpp).
- [x] The legs on footsteps, jumps, landings, dodges, contacts, turns, the dive and falls (src/view/fighter_sounds.cpp; the footsteps are `motion_select_descriptor`'s key crossings, `fighter_footsteps` in src/view/footsteps.cpp).
- [x] The footsteps' `motion_changed` is read off the step as a new clip slot, a new motion state or a landing (src/view/footsteps.cpp). A `motion_set_animation` of the same slot and state also sets `motion_phase_prev` to the phase it gives, so unless the phase moves on after it in the same step, the step reads as a phase that didn't go forward and the cursors start again as retail's changed motion makes them; the one handler that moves the phase on after a set (idle to skid or turn) changes the state, which is seen (read, not run, the 44 calls): no world flag needed.
- [x] Retail's footsteps start the foot landing sets (`actor_effects_update`'s `0x400 << foot`, the only writer of those bits): `fighter_footsteps` gives a bit per foot, numbered in `sv_pose_walk`'s order as attachments 14 on are, gathered each step by `effects_step` and read at the 30 Hz update. The port's stand-in (every foot on any landing) is gone: retail has no other writer of those bits.
- [x] `actor_sounds_update`'s hum (1/0xC every 5 s), the broken arm's crackle (3/0x25, read as "overheat" before: `weapon_state` 4 is a broken arm's arcs done), low armour (3/0x27) and score (1/0x13) sounds (fighter_sounds.cpp).
- [x] A shot that hits on the step it was fired, or on its last tick of life, made no hit sound: `event_shot_hit` from every pass that meets something (`battle_shots_update`'s `hit_slot`), blades and beams included as retail.
- [x] The shared cue voices: settled from the matching `sound_start` 0xDE830, no emulator needed. Each bank's cue is one `CueVoice` for the whole game, its `next` moving round its `count` buffers for every caller, so six fighters with the same gun share bank 1's two voices of its fire cue and one's shot cuts another's, in retail as in the port (`play_sound`, src/audio/audio_sounds.cpp). Kept: it is the original's voice budget, and taking the least heard voice instead changed 88 cuts to 86 in a measured fight (docs/audio.md).
- [x] Mix lock: every effect call held `mix_lock`, so a main thread put off the CPU while holding it held the device's ask up with it (the case held: the main thread is the busiest and runs at the ordinary priority). Now a command ring (retail's own design, `sound_command_push`): the main thread answers from its views of the voices and sends commands, the mix runs them at the top of its ask and answers through atomics; music reaches the mix through a ring the reader fills, converted on the reader's thread. The device's thread waits on nothing the main thread holds (docs/audio.md, "Threads"; the health line's `ring-full=`).

## Battle HUD

- [x] The cockpit instruments (view 0, `hud_draw_meters`): models 0x4DD..0x4F1 in pass 11 through cockpitUV with their gauges, cells and colours, the O.C gauge 0x4E7 and the cloak glow 0x4DD (src/hud/hud_meters.cpp, src/effects/cockpit_draw.cpp). Reached with the view switch (d-pad up; 3 or the wheel on the keyboard), and in the ending's last shot.
- [x] In view 0 the weapon readout is retail's in-world panels: each row's cut-out on the stage view's unit quad in pass 11, angled in at the sides (`hud_draw_readout(in_world)`, `HudElem::draw_either`, `uv_override_submit`; src/hud/hud_readout.cpp, src/effects/cockpit_draw.cpp). Check their size and tilt against a retail capture.
- [x] `hud.bob` follows the body node's height between updates (`actor_frame_reset`'s `body_point`): settled from the decomp, the arithmetic is retail's; `body_point` is taken every update whether the HUD shows or not, and the port took it only while shown, so the first update after the entrance or a scene jolted by the whole change since. Now taken every update and from the spawn (`hud_bob_step`, src/hud/hud_meters.cpp).

- [x] Popup points are stored and placed but not drawn: settled, retail never draws them (`hud_draw_readout` 0x4ACBB's places are dead stores; `shown[].value` is copied and reset, read nowhere). Kept undrawn.
- [x] The lock alarm stops and restarts every 11 updates while a lock stays on: settled from `actor_sounds_update` 0x935B7's code (docs/bugs.md). Heard in a headless run (`captures/batch_fx_sound/new_wall_lock.txt`: started on steps 654, 676, 698..., stopped 20 steps after each); it had been played 2D and refused as a 3D cue until 2026-11-12.

## Campaign

- [x] The job map's walkers run in float in the front end and only their route targets reach the world (RosterEntry.start, converted exactly): decided to keep (docs/bugs.md "Port decisions"); the job map is the campaign's, which is one machine's, and online play is VS, which has none.
- [x] The job map's icon heading: settled from `job_wanderer_draw` 0x43FFA (`-angle - pi/2 - atan2_f(dir.z, dir.x)` with the direction at +0xC), which src/screens/campaign/job_cards.cpp computes the same.
- [x] The hangar's incomplete machines lay their parts out over their cards (`scoobee_parts_draw_spread` view 6).
- [x] The hangar's destroy copies the boarded machine into GameState +0x327C (`rumble[0..3].roster[0]`, the player's entry in each rumble, not VS) and zeroes its start: settled, nothing sees it. Every way into a fight (a job's entry 0x169E5, the test drive, free play, VS setup, the attract demo) writes `roster[0]` first, and the job map draws from index 1; only the saved GameState holds the copy.
- [x] The hangar's mouse: name plates hover and click, the mouse wheel turns the wheel; the card's menu rows hover. The plates' boxes are their slanted art's middle 32 columns, tiling at their pitch (they overlapped by 8).

## Free play and the lessons

- [x] Beginner mode's pitch spring and quarter-turn flick (`beginner_camera_input`, src/battle/battle_camera.cpp): checked headless, a flick turns the legs 90 degrees and back, the pitch springs to level after the stick lets go.
- [x] The lessons' lines: a table entry with no script starts nothing, as retail (`BattleData::scene_text_scripts`).
- [x] The dodge lesson (0x1F) counted steps a dodge clip is on. Retail (`scene_tutorial_1f_update` 0x8C041) counts updates whose `motion_slot_prev` is a dodge, and `actor_frame_reset` 0x88A5C clears it every update before `motion_set_animation` 0x8DD32 writes it, so a count is a clip change out of a dodge: one a dodge, two dodges each way. The port does the same (`Fighter::motion_slot_prev`, hashed); lesson 0 run to its end headless again.
- [x] Headless runs pause a fight with `--route`'s `start` (PHANTOMCRASH_DEV_PAUSE=U still works).
- [x] The lessons' scene cameras: retail has no camera rows for 0x18..0x2E; the enters of 0x18, 0x23, 0x2A and 0x2C cut to view 1 (`FighterCamera::view`).
- [x] The lessons' drones: free play kept the fill's entrants (6 for tournament 55), so lesson 1's drones come in (they never did).
- [x] Every lesson run to its end headless (2026-11-12): lesson 0 by a pad script cycling every move, 1 by dev kills, 2 by X; routes and pad scripts in `captures/lessons/` (docs/playthrough.md "The lessons", `--route`'s `every:`).

## Multiplayer groundwork (docs/multiplayer.md, "Change now")

- [ ] VS's pause quit ends the round from the front end (`vs_pause_step`); online it must reach the world as an input (`vs_end_state` 5).
- [ ] `Inputs::scene_text_done` (the lessons' and the ending's lines) is one machine's presentation answering the world; online it must be a seat's input or the line's length measured into the world, as the rankers' are.
- [ ] `BattleData::ranker_line_ticks` is measured from the scripts by the HUD's tables in `battle_run_start`; measure it from the data at load so every machine (and pc_sim, which has 0) gets the same.
- [ ] The profile's Sonic Amp stock is rolled with CRT `rand` at boot (`music_state_new`), so two machines' `world_hash` differ there from the start; online, the battle start sends the host's save part, so compare `world_fight_hash` until then.
- [ ] `LobbyRules::online` and `ai_seats` are set by nothing yet; the online lobby and VS's AI seats set them at battle start (`team_end` is read by `vs_battle_update`).
- [x] Records and results per seat, local VS: the results are per seat (`VsBattle::wins`, `losses` by seat across rounds, the VS marks), kills and score per fighter (`FighterCombat`); a human's kill adds to the loaded profile's lifetime records as retail's `actor_settle_damage` does (VS saves nothing, so they never reach a file). Per-seat records for online co-op stay with item 8 of docs/multiplayer.md.
- [ ] `Prop::tris` (576 bytes a prop, 59 KB of the 102-prop pool) never change after `stage_world_init`; keep them beside the world, constant for the battle, to shrink snapshots (src/stage/stage_world.hpp).
- [ ] The shot pool is 150 KB of the 335 KB world and about half the words the hash reads (100 us a hash); hash and snapshot only live shots once shot allocation is shown to set every field.
- [ ] pc_sim's source list (cmake/sim.cmake) repeats the world's sources from the game's; fold both into a `phantomcrash_world` library when the CMake lists are next reorganised.
- [ ] The crossplay check runs ARM64 on packs cooked on x86-64; a real ARM64 machine cooks its own (the cook's floats now unfused too), which the data hash compares. Run it on real ARM64 hardware when there is one, and add MSVC (/fp:precise) with the Windows build.
- [ ] Decide the arena cap from the 12-fighter numbers (docs/multiplayer.md "Measured": a 12-fighter step is about 3x a 6-fighter one, a rollback of 8 about 2.7 ms).

## Local VS (docs/multiplayer.md "Status")

- [x] KEEP SV's kept machines in VS setup's machine list (`src/screens/vs/vs_machines.cpp`): the scroll, the read-failure card, the locked picture and name; a round spawns a kept machine as a build row after the table (`battle_vs.cpp`).
- [x] A kept machine plays with the config kept with it: done in 2207e36 (KEEP SV keeps `hdr.stats`, the keeper's button config, as `(buttons ...)`, and `battle_vs_start` gives it to the machine's seat, as retail's `player_config_store` of `vs_entrants[n].save`).
- [x] A rematch reloads the stage, the machines and the effects every round, as retail's battle_enter does; since 0cdad66 a VS round runs through `battle_run`, and the battle screen's destroy stops it with `battle_run_stop`, which frees everything the battle uploaded (`renderer_free_owner(render_owner_battle)`), so rounds don't pile up. Keeping the scenery across same-area rounds isn't worth a second path.
- [x] Split screen: the effects' 2D goes into each view (`effects_screen` with a `ScreenView`): the hit static fitted to the view and only in `StageWorld::hit_seat`'s, its scratches taken from that view's 3D; the lens flare stays single view. The scratches' scene texture (texture_scene) now reaches the sprite layer, which skipped every negative handle before, so the static's bands draw in one view too.
- [x] The hit static is set only for a human in view 0 (`damage_settle.cpp`, as retail), which the view switch reaches; `PHANTOMCRASH_HURT=STEP:SLOT:DAMAGE` hits a fighter for captures (the viewer and the front end's fights).
- [x] Split screen: the sprite effects' billboards and quads, the scorch marks, the combat effects and the beam are queued per view with that view's matrices and eye (`effects_queue` from `view_draws`, the billboards into the frame's own storage), so each faces its view's camera. The trails didn't: `trail_draw` rewrites a mesh's vertices for the eye, and every upload of a frame lands before any view draws, so all views drew the last view's. Each view has its own trail meshes now (`EffectAssets::trail_mesh[4][50]`, src/effects/trails.cpp).
- [x] VS setup: the machine names roll with their panel, glyphs fill x 18 high (`vs_panel_machine_draw` 0x3F559 and `vs_panel_name_draw` 0x3F871's `wide_text_draw_small(..., 0x12, alpha * 18, ...)`; src/screens/vs/vs_machines.cpp).
- [x] VS setup and area select: the area plate's key tracks and Random's pictures come from the data now. The extractor already wrote the words they sit in (area_select.sexp's D_001DA200..D_001DACCC, split at every name); `area_plate_play` (src/screens/campaign/area_plate.cpp, shared by both screens) reads that file back as memory, follows the tracks' pointers and stops each key list at its negative frame. The label slide's tables (`D_0019EAC8`, `D_0019EAE0`, `D_0019EB04`, in .rdata) are extracted into vs_mode.sexp (`CODE_INT_ARRAYS`, tools/ui_tables.py). Data extracted before this needs `pc_extract --only ui` (it changes vs_mode.sexp only), or the labels slide from and to 0.
- [x] VS end menu: its cursor repeats while held as retail's `pads_cursor_move(..., 0x16)` (the quit box's 0x17), wrapping only on a fresh press: the world keeps each seat's buttons and left stick directions with their auto-repeat from `b.cur`, `b.rising` and the stick (`VsBattle::menu_buttons`, `menu_stick`, src/battle/vs_battle.cpp, at the battle's 30 Hz read rate), so it still reads no more of the pad than it sends (multiplayer.md item 5), and the first seat whose pad moves it wins, as retail's loop over the mask.
- [x] The join page names each entered panel's device ("KEYBOARD AND MOUSE", "PAD 2") in PC CONFIG's label letters along its "You entered." box (`device_draw`, src/screens/vs/vs_draw.cpp).
- [x] The VS screens take the mouse as the keyboard seat's: clicks join, accept and cancel, hovering moves its cursor (docs/screens.md, `vs_mouse_seat`).
- [x] Gubsy's hot-plug gave a new pad to its primary player until the next `seats_sync`: `seats_sync` runs on its `SDL_EVENT_GAMEPAD_ADDED` now, in the same pump (main.cpp, viewer_run.cpp). Before it, every read already came after a `seats_sync` and the presses are kept by device id, so the case never reached seat 0.
- [x] VS sound: decided, retail's rules stay (banks 1 and 2 muted, every 3D sound at (0, 0, 2), the listener pinned; docs/bugs.md "Audio"). `battle_enter` 0x4A23E mutes the two banks on purpose, the way `demo_battle_enter` mutes three, and one listener can't place sounds for two to four views; every player still hears each fighter's own fire, legs and warnings.

## Sonic Amp (0x14)

- [x] The artist strip retail builds under the RANK ticker and never draws: decided not to draw it, as retail shows none (docs/bugs.md).
- [x] The tab plates, the arrows 0x97D / 0x989 and the zoom panel's trail, checked line by line against `sonic_amp_tabs_draw` 0x2B42A, `sonic_amp_arrows_draw` 0x2A19B and `sonic_amp_panel_zoom_draw` 0x29A0A: the same places, timings, blends and alphas; the trail runs while `leaving` (+0x4C, the tab cursor on leave). One difference fixed: off the tab cursor retail draws the picked tab as a plain sprite, steady, where the port kept the row's pulse flag 0x2000 (captures/screens_left/sonic_amp_{before,after}.png).
- [x] The grid's, the menu's and the tabs' mouse hit boxes are their sprites' drawn art (the cells' box sat 4 up and left and took the gaps).

## Media player (0x21)

- [x] The "new" mark (1694, blinking with 0x4000): Sonic Amp's buy sets `music_owned` to 1, `media_page_draw` 0x37142 marks the cells whose `music_owned_state` is 1, and `media_playlist_screen_destroy` turns every 1 into 2; the port does the same (sonic_amp.cpp, media_draw.cpp, media_player.cpp): two bought songs show NEW, a heard one its number (captures/screens_left/media_new_marks.png).
- [x] The catalogue's mouse hit boxes are the cells' art widened to their 47 x 30 pitch (they sat 8 left: a click on a cell's right picked the next) and the exit plate's art.
- [x] A left click anywhere in the catalogue added the song under the cursor (and on the exit row left the screen); a click counts only on the cursor's cell or the exit plate now (src/screens/shops/media_edit.cpp; docs/bugs.md "Port decisions"; captures/screens_left/media_click_{before,after}.png: a click on the jacket picture added 003 before, nothing after).

## Dealer (0x22)

- [x] The stand's mode 2 place: the dealer's machines stand at D_0098C618's point for their leg class and slide in from 8 to the side (`model_viewer_offset`, `_shifted`), not at `model_viewer_places` row 2, which only places mode 2's incomplete builds (none). The spec page (mode 5) stands at D_0098C518's too.
- [x] The Chip's name entry gets no second name: retail's `name_entry_target_set` 0x2C559 stores it in `name_entry_second_name` 0x415DEC, which no function reads, so nothing on screen depends on it.

## Lost controller

- [x] The watch follows the pad that pressed START on the title return (`pad_watch.cpp`); a pad plugged in after it went is taken as it.

## Code

- [ ] Taste pass leftovers (2026-11-12): six files sit just over 500 lines (vk_frame, ui_anchors, hud_core, effects, record_screens, wild_machines); screen coordinates, sprite and model ids, layers (0x101..) and colours are still hex literals (the ids could become per-domain tables like audio/sound_cues.hpp); effect set ids (`effect_set_play(fx.pool, 0x23, ...)`) want names from a checked table, the extractor's EFFECT_ names don't line up with them; conversations keep their state numbers (talk_NN); the legs' sound cues are indices shared by banks 1 and 3; the AI's roll chances stay numbers.
- [x] Taste cleanup pass (owner, 2026-11-12: "sanity taste cleanup"): done 2026-11-12 apart from the leftovers above (AGENTS.md "Style" has the rules), after the widescreen-polish, garage-stand and settings branches merge; behaviour identical, proven by the movement traces and world_hash:
  name the front-end states (10 screens files `case 0xe:`), flag bits, motion modes, sound and texture ids; `actor_*` functions to `fighter_*`; the 71 `"D_..."` UI data keys and `f_1a4`-style members to real names (in the converter); split `screens/` (119 files) into sub-areas; fold `d3dx/` into `fx/` or `render/`; split the four files over 500 lines; trim address comments to one decomp-name note per function.
  Keep the trail to the decomp: every ported function keeps one greppable `// decomp: name 0xADDR` line; each named constant's definition carries the original value and decomp name; the converted UI data keeps the `D_` address beside its new key; gameplay function boundaries and call order stay as they are (names change, not shapes); the 60 Hz conversion and retail-bug comments stay; a tool builds an index of port symbol to decomp name and address from the `decomp:` lines.

- [x] SDL_Log lines from the audio thread (the 10 s `audio: health` line) landed in the middle of `--trace` lines: stdout is line buffered and SDL_Log's lines go out holding stdout's lock (src/run_log.cpp).
- [x] `extra_dispatch.cpp` folded into `menu_dispatch.cpp`; the lost controller is `pad_watch.cpp`, the shared row and script pieces `screen_kit.cpp`.
- [x] Three gameplay files over 500 lines split by moving whole functions, behaviour identical (the nine movement traces): `src/story/rumble.cpp` (rosters to `rumble_roster.cpp`), `src/stage/supply_craft.cpp` (the hit test and the nearest crate to `supply_crates.cpp`), `src/mech/fighter_motion.cpp` (`motion_root_delta` to `fighter_root.cpp`).
- [x] PHANTOMCRASH_VK_VALIDATION=1's VUID-00904 / 02684 every battle frame: fixed with the line under Effects; the capture set (title to ending) reports nothing now.

## KEEP SV (0x2B)

- [x] The kept time is drawn with `date_draw` / `time_hms_draw` (number font 0) at (0xBD, 0x176) and (0x116, 0x176).
- [x] The mouse: the places, SAVE, EXIT, the scroll arrows and each box's answers have hit boxes on their art; the wheel over the list scrolls it.
- [x] The header's records block (`hdr.stats`, GameState +0x6018) is the keeper's button config: kept as `(buttons ...)` in the file, and the machine's VS player plays with it (`player_config_store`).
- [x] Two machines of the same name share a file, as retail's saved-game names do (the overwrite question); a name of only characters a file name can't hold is all '_': decided to keep (docs/bugs.md "Port decisions").

## Settings (PC CONFIG, docs/settings.md)

- [x] The PC CONFIG letter scan runs in the cook (`src/cook/cook_pc_art.cpp`, `pc_art.sexp` in core); the game reads no PNGs for it.
- [x] BORDERLESS, FULLSCREEN and REFRESH RATE rebuilt on SDL3's documented order (src/screens/pc/pc_display.cpp: out of fullscreen and maximized before a size, modes only from the display's list, `SDL_SyncWindow` after each step, exact rates, high-density modes in pixels, the display remembered by name and place, fallbacks to the nearest mode, the window's display, the primary, the desktop) and checked under a private i3 on a private Xvfb: every mode, F11 both ways, the fallbacks (docs/settings.md, captures/platform/display_*).
- [x] REFRESH RATE (SCREEN page): fullscreen's rates at its size (RESOLUTION still picks a size at its fastest).
- [x] The movies' sound follows MUSIC VOLUME, as retail plays them at its music volume (decomp docs/port/audio.md); kept.
- [x] BINDINGS rebinds the axes: MOVE STICK, AIM STICK, MOUSE AIM, LEFT and RIGHT TRIGGER, WHEEL (`pc_settings_axes.cpp`).
- [x] BINDINGS clears a row's pad buttons with Shift+Delete or a pad's Y (Delete the keys).
- [x] Key names with signs the label font lacks are spelled out (L BRACKET, SEMICOLON, BACKSLASH...).
- [x] BINDINGS has a PLAYER picker (profiles 1..4); each VS seat plays with its own once it has one.
- [x] Per-seat binds with real pads: nothing in the path knows a pad's kind. `seats_sync` gives lobby player i its own profile (id 1 + i) once it exists, `read_pads` reads each player's profile for the devices it holds, and SDL's virtual pads come through SDL and gubsy with ids as real ones do, so what the headless runs show holds for any pad.
- [x] SHOW FRAME RATE (SCREEN page): the rate on a dim plate, top right (`pc_fps_draw`).
- [x] ANTI-ALIASING (PICTURE): FXAA in the port's post pass between the composite and the 2D (`post.frag`, validated on lavapipe); MSAA not offered (the dof samples the depth, which MSAA would need resolved).
- [x] BRIGHTNESS curves only the 3D now: the same post pass, the composite and the cockpit's meshes; the menus, the HUD and the fades keep the original's brightness.
- [x] BRIGHTNESS and FXAA leave pass 13's late meshes (the results' trophies over the 2D of pass 12) as they are and lift overlay pass 0 (the 2D under a menu's 3D stand, in the scene) with the scene: kept so, each with what it is drawn among (the trophies with the menu round them, a backdrop with the machine standing in front of it).
- [x] FXAA and BRIGHTNESS on a real GPU: `post.frag` uses core GLSL 450 only (no extensions beyond the include directive, no subgroups), samples an R8G8B8A8_UNORM target whose linear filtering and colour attachment Vulkan requires of every device, and passed lavapipe with the validation layers; nothing in it differs by device.
- [x] FLASHES (PICTURE page): the effects' lights, muzzle flashes, lens flare and hit flicker dimmed in presentation.
- [x] CAMERA SHAKE (PICTURE, 0..100): the hit shake on the drawn cockpit view and the HUD's screen points, a presentation scale on `StageWorld::shake_*` (src/view/cockpit_view.cpp).
- [x] Retail's hit shake reaches the fighter's own look-at, so for its 15 updates it also moves the tracking point (`reticle_track_world`), the lock boxes and the senses: in the world now (`FighterCamera::shake_shift`, `up`; src/battle/camera_views.cpp), AIs' look-ats too as retail; CAMERA SHAKE scales only the drawn camera and the HUD's points with it (src/view/cockpit_view.cpp).
- [x] Menu size: not offered; the menus are the original's 640x480 layouts scaled to the window's height, already as large as it allows, and their pieces are laid out against each other (docs/settings.md, "Accessibility").
- [x] HUD SIZE (SCREEN, 70..130): the fight's HUD corner pieces scaled about their corners (`hud_scale`, src/screens/ui/ui_anchors.cpp).
- [x] HUD SIZE in split-screen VS stays at 100: each view lays out the original's own smaller tables in its quarter or half, and scaling them about the view's corners would push them into the next view; HUD SIZE is the one-view HUD's (docs/settings.md).
- [x] STICK CALIBRATION is kept per pad now (its serial where SDL reads one, else its path and its kind), so two pads of one kind keep their own (docs/input.md, "Calibration"). The maths reads SDL's raw axes, the same for a real pad and `PHANTOMCRASH_PAD_SPIN`'s virtual worn one.
- [x] Pad rumble: `event_hit_rumble` from `actor_settle_damage` plays the hit's table on the seat's own pads (src/input/pad_rumble.cpp); OPTION's VIBRATION turns it off, VIBRATION STRENGTH (INPUT) is gubsy's input profile's `vibration_strength`.
- [x] The pads' rumble halts as retail's `rumble_halt`: at the pause and each paused update, a fighter parked by a scene or retired, the fight's end; a pad with trigger motors also gets the hit in its triggers (src/input/pad_rumble.cpp, docs/input.md "Pad rumble").
- [x] PC CONFIG's help lines are padded as the original's are: 34 ideographic spaces before (the sound config's table, whose frame PC CONFIG is), OPTION's PC CONFIG line 30 (the options' table), 4 after.
- [x] A window resized by hand keeps its size (`pc_window_event`, written once the drag stops) and RESOLUTION offers it beside the fixed list.

## Screens

- [x] OPTION's seven items are 43 apart as retail's six, the column centred where retail's was (y 99.5 on), clear of the header's and footer's rules (captures/platform/options_seven.png).

## Release and first run (docs/release.md, src/setup)

- [ ] The Windows and Mac bundles build but have never run: try each on real hardware (setup screen, drop, Browse, extraction, the game).
- [ ] Windows extraction converts Shift-JIS through code page 932 (tools/extract/sjis.cpp), Linux through iconv: check a Windows data/text and data/ui come out byte-identical to Linux's.
- [ ] vulkan-1.dll is imported directly: on a Windows machine without Vulkan the system's missing-DLL box shows instead of a plain message (MIMH delay-loads it for its GLES fallback; the port has no fallback).
- [ ] The setup screen draws at 1280x960 scaled: blurry text on a HiDPI or Retina window (render text at the window's pixel density).
- [ ] No icon for OpenPCrash.exe or OpenPCrash.app yet.
- [ ] The cook has no cancel: Cancel is hidden for its few seconds at the end of setup.
- [ ] GView, GLayout, gfxp and gsexp have no LICENSE file in their repos, so the bundles' licenses/ has none for them.
- [x] Done in the taste pass (6b4b98d): tools/extract/audio_codec.cpp decode_adx rewritten from the public ADX format description, output byte-identical. Was: the coefficient lines followed CRI closely; rewrite them from the public ADX description (vgmstream, Wikipedia) with neutral names and drop the func_000EB160/EB210/EED30 references.
- [ ] The window title and the per-user folder still say "Phantom Crash"/"PhantomCrash"; decide whether they become OpenPCrash (moving existing saves).
