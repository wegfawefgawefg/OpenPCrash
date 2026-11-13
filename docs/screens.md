# Screens

The original's front end as game code, in `src/screens/`, drawn by the sprite layer in `src/render/vk_sprites.*`. The
spec is the decomp's `docs/port/screens.md`, `architecture.md` ("Application states", "The frame") and `audio.md`, with its
C as the ground truth; function names and addresses below are the decomp's.

## The 2D

Every 2D thing is one list, `Draw2D` (`src/talk/draw2d.hpp`): quads in the original's 640x480 pixels with a texture slot,
a texel rectangle and the record's sheet size, ARGB colour, the sprite's z, its layer (`Sprite.blend`), its pivot and
angle, and its flags. Menus, conversations, movies and the fade all write it; `renderer_set_sprites` hands it to the
renderer, which draws it as `pass_draw_overlay` 0xB4FF0 does:

- layers 0..8 in pass 0 under the 3D, 0x1000..0x7FFE pass 14, 0x7FFF (the fade) pass 15, the rest pass 12; ordered by
  pass, layer, then submission (retail's quicksort leaves equal layers in no order). Pass 0 draws into the scene target
  before the meshes, so the pass 11 composite takes it in; 12, 14 and 15 draw onto the final target after the composite
  and before the gamma ramp (docs/shaders.md), which is the identity outside a battle;
- passes 0, 12 and 14 through the 576x448 title-safe viewport at (32, 16), so anything outside it is clipped, as
  retail's viewport clips it; pass 15 through the whole 640x480. The 640x480 screen is fitted 4:3 into the window with
  bars, except a quad flagged `quad_full_window` (port-only, `draw2d.hpp`): placed the same way, then stretched with the
  640x480 over the whole window, unclipped (below, "Widescreen");
- corners at `(x - 32, y - 16)` plus the scaled rectangle, turned about the pivot, through spritemat's
  `(x - (W/2 - 0.5)) / (W/2 - 0.5)` and then half a pixel to Vulkan's pixel centres; positions floored unless flag 0x10;
- point sampling unless flag 0x10, texel coordinates with flag 2, mirrors with 0x400 / 0x800, colour-only (additive)
  with flag 1; `alpha = int(a * z)` (a quad of alpha 0 is dropped); the texel's alpha 0 is killed; premultiplied, blended
  ONE / INV_SRC_ALPHA (`assets/shaders/sprite.*`).

`src/screens/ui.*` is the screens' side of it: `Sprite` (draw.h's) and `sprite_submit`, which does
`render_command_submit_pass` 0xD387D (the blinks 0x2000 / 0x4000 / 0x20 from `menu_input_age`, 0x100 / 0x200, the centring
of flag 8 by half the scaled size, the pivot at the middle), `sprite_rows_draw` over the layout's `SpriteSlot` rows, and
the fade (`screen_fade_*`: sprite 0xE7C at (-32, -32) scaled 21 x 16 at 0x7FFF; the port's 22 x 17 over the whole window). A registry sprite is its own cooked
cut-out (the `screen_X` and `ui_S` packs, docs/assets.md "Packs"), drawn from slot 0x10000 + its id; the original's slots
below that keep what `texture_set_load` binds there for the conversations and the font. A screen's packs are opened when it
first draws from them and closed when it goes; what reached the GPU stays cached. Movies are slot 0x225, drawn 640x480 at
layer 0x104 with flag 2 as `movie_draw_rect` 0xAECD9 does, the picture replaced each frame
(`renderer_update_sprite_texture`).

`screen_text.*`: table strings through the font's glyph order (`sjis_to_glyphs`), `wide_text_draw_small` (proportional
widths, colour codes; the text's layer is 0x101 + its texture argument), the scrolling help line (`text_strip_draw_scroll`
0xBE430: copies of the line side by side across the clip, one pixel a tick), message boxes (`func_000BEB8F`: the box
grows from 0.1 by 0.15 / 0.3 an update, `D_002805D8.f_24` = 1, sound 0xC on its first update; the OK mark and the yes/no
answers), and `number_draw` for the plain right-aligned case. `screen_widgets.*`: the level meter (the sprite player on
`level_meter_a`) and the now-playing strip ("《title　：artist　》" from the Sonic Amp catalogue).

## The application states

`src/screens/front_end.*`: the AppState task. `enter_state` is the state's entry (`menu_request_state`,
`menu_input_mark`, the screen spawned), `update_state` the state's update (the screen's answer to the next state),
`menu_set_state` 0xA1B79 kills the screen, runs `app_transition_resolve` (src/story) and sets the next entry, which runs on
the next update; a screen made during an update first steps on the one after, as a task made during `tasks_run` does. So a
state change leaves two updates with only the fade drawn, as retail. Conversation states (0x30..0x9D) run the talk task
(src/talk) and end by asking for 0x9F; the resolver then plays the next row that holds or goes on to where the change was
going. New Game (0x1C) goes straight on to 9 on the one machine (retail nests a second AppState: docs/bugs.md). Cadence and
music come from `tables/states.sexp`; every state reached so far runs at 60.

Each update: the pads are read (blank while the fade runs, `pads_read_mask` 0), `task_timer` moves, the AppState runs its
entry or update, the screen runs its step, then `fade_frame`. The newest list is drawn at any display rate.

| state | screen | file | status |
|---|---|---|---|
| 0x9E, 0x2D, 2, 0x19 | Phantagram and Dolby logos, attract movie, opening | `boot_screens.cpp` (src/movie's screens) | ported |
| 0x12 | logo (Genki, then the second logo once a power-on) | `boot_screens.cpp` | ported |
| 1 | attract demo: a minute's AI-only demo battle (main's battle session, as the result screen's) on one of `attract_demo_tournaments` in the last area, 12SENTOU.WAV and the blinking caption; the minute back to the boot, a press to the title return; the next demo in the next area (`D_001D6F34`) | `boot_screens.cpp` | ported; the demo battle puts the save's context back (retail fills it for good) |
| 0x28, 0x15 | press start, title and its New Game / Load menu | `title_screens.cpp` | ported |
| 0x1A, 0x23 | options (vibration and defaults boxes), sound config (live volumes, sound test) | `options_screens.cpp` | ported; day and money not drawn (only reachable from the title here) |
| 0xA0 | PC CONFIG, the port's own (the PC's settings, docs/settings.md); options item 5 goes there, it answers back to 0x1A | `pc_settings_*.cpp` | ported |
| 9, 0x2E, 0x11 | records check, save files check, load | `save_screens.cpp` | on src/save's three slots |
| 0x27, 0x1F | system menu, save menu | `system_screens.cpp`, `save_slots.cpp` | ported on src/save's three slots |
| 0x10, 0 | key config (the original's button config), screen adjust | `key_config_screens.cpp` | ported |
| 0x1D | records: menu, rumble and game pages, tournament pages and grids | `record_screens.cpp`, `record_grids.cpp` | ported, its trophies in 3D (render pass 2, under the 2D, `trophy_models.cpp`) |
| 0x13 | help menu (Pepper's talk) | `help_screens.cpp` | ported; topics return to it; free play (0x2A) is the battle entry's |
| 0x30..0x9D | conversations | `story_screens.cpp` (src/talk) | through the GPU sprite layer |
| 0x16 | name entry: three pages, the cursor and tab rules, per-kind boxes and results | `name_entry.cpp` | ported |
| 0xD | garage hub: the eight-item wheel | `garage_hub.cpp` | ported |
| 4 | assemble, the parts editor: grid, side choice, lists, card and compare, boxes, the turntable | `assemble_screen.cpp`, `assemble_draw.cpp`, `part_card.cpp`, `model_stand.cpp` | ported |
| 0x24, 0x26 | spec page, stock | `spec_screen.cpp`, `stock_screen.cpp` | ported |
| 0x1B | Plus Tech: wheel, module tuning, Chip Uplift | `plus_tech*.cpp`, `chip_carousel.cpp` | ported |
| 0xF | Wild Machines: modules, paint, Chips, used Scoobees | `wild_machines.cpp`, `wild_steps.cpp`, `wild_draw.cpp` | ported |
| 0x29 | town map: the ring, the marker's flight and pop, the place pictures, the stick spin's pay | `town_screens.cpp` | ported |
| 8 | Club Wired: the six-item wheel, the shuffled songs | `club_wired.cpp` (the shuffle is `music_player.cpp`'s) | ported |
| 3 | area select and the job menu: the area page with its TV-static switch, ENTRY, RENTAL, the hangar, CHARACTERS and CONTEST INFO, gate select, the refusal boxes; the job map's overlays, route marks and walking fighters | `area_select.cpp`, `area_select_draw.cpp`, `job_cards.cpp`, `job_wanderers.cpp` | ported |
| 0xE | the hangar: the wheel of ten garages and the exit, the machines over their cards, the garage's menu (Ride, Test-drive, Specs, Assemble, Close), the unlock for 100,000 NY, its boxes; keep SV's mode | `hangar.cpp`, `hangar_draw.cpp` | ported |
| 0x20 | schedule: five days of the three areas from today, a day scrolling past the edges or five with the triggers, the tournament card | `schedule_screen.cpp` | ported |
| 0x2F | test drive: the hangar's boarded machine on tournament 47 (two opponents from the reserve pool), Start pauses (PAUSE), the door or a death ends it back to the hangar | `campaign_dispatch.cpp` | ported |
| 6 | the battle: `battle_enter`'s chain, the letterbox, READY / GO, the pause | `battle_screen.cpp`, `src/battle_run.cpp` | ported, with the effects and the HUD (`battle_run_hud`) |
| 0x2C, 6 in VS | VS setup: join, machine and area pages; state 6 with `vs_mode` is the battle screen with main's battle session running the split-screen round (`src/battle_vs.cpp`): the pause with its quit question, the lost-pad pause | `vs_setup.cpp`, `vs_draw.cpp`, `vs_area.cpp`, `battle_screen.cpp` | ported on seats; the machine list has the stock builds and KEEP SV's kept machines (`vs_machines.cpp`) |
| 0x1E | result: the rows, the rolls, the money, REPLAY / EXIT, the commit | `result_screen.cpp`, `trophy_models.cpp`, `src/battle_run.cpp` | ported, with the demo battle behind the sheet and the trophy |
| 5 | the replay: 16 updates of black, the last tournament fight stepped again from its first world with its recorded Inputs (battle phase 2), the REPLAY frame and the six camera views (left and right, the wheel), A or its end back to 0x1E | `replay_screen.cpp`, `src/battle_replay.cpp` | ported |
| 0xA | the ending's scene: Aqualine by day in battle phase 3, the player through shots 8..0x12 (their curves, fades, lines, the craft's routes, the last shot's signal), then the last frame switched off and two seconds of black | `ending_screens.cpp`, `src/battle/battle_ending.cpp` | ported, with the paper storm (src/effects/stage_effects.cpp), the carried machine, the dissolves and the last shot's cockpit |
| 0x2B | KEEP SV: the kept VS Scoobees (`<save dir>/vs`), SAVE through the hangar's keep SV mode, a kept one's spec page or delete | `keep_sv.cpp`, `src/save/vs_saves.cpp` | ported |
| 0x18 | the night: day + 1, the line-up refilled | `town_screens.cpp` | ported |
| 0x14 | Sonic Amp: today's picks, the three charts, buying, listening with the artist's bio | `sonic_amp.cpp`, `sonic_amp_draw.cpp` | ported |
| 0x21 | media player: choose the battle's list, edit a list (with preview), RANDOM, rename (name entry kind 3) | `media_player.cpp`, `media_edit.cpp`, `media_draw.cpp` | ported |
| 0x22 | dealer: Aeron, Photon, Holy; the day's three machines on the stand, profile, spec page, buying, boarding, the Chip's name (name entry kind 2) | `dealer.cpp`, `dealer_buy.cpp`, `dealer_draw.cpp` | ported |
| 0xB, 0x25, 0xC | the ending movie with its subtitles, the credits (sets the beaten bit), the secret movies | `ending_screens.cpp` (src/movie's screens, `boot_screens.cpp`'s step) | ported |
| 0x2A | free play and the lessons: the setup (`FreePlaySetup`, `free_play_setup`), then the fight through the battle entry (`free_play_spawn`, `free_play_step`); the lessons' tutorial scenes and their lines; the pause menu (continue, quit) | `ending_screens.cpp`, `battle_screen.cpp`, `scene_text.cpp`, `src/battle/battle_tutorial.cpp` | ported; lessons 1 and 2 run to their ends headless (docs/playthrough.md) |
| (any menu state) | the lost-controller box (kind 1) over the screen, which keeps running with no pads | `pad_watch.cpp` | ported; the battle pause draws it with `pad_missing_box_draw(fe, ui, 1)` |
| others | placeholder: names the state; any button goes on to the title return | `story_screens.cpp` | none reachable: 7 (`unused_battle`) has no caller in retail and 0x17 (`net_lobby`, Xbox Live) none in the port |

## The garage and the shops

`part_tables.*` reads the part records for the menus (names as glyphs and Shift-JIS sort keys, prices, the
card stats, the shops' stock rows, the used builds) and evaluates the curves in the original's float
arithmetic, so prices and card numbers come out as retail's. `garage_stats.cpp` is what items, builds and
loadouts weigh and hold, `garage_status`, prices and tune prices; `garage_rules.cpp` the editor's pages,
admission, fitting and taking off (`garage_manual_select`), the socket rule and `used_deliver`. Everything
writes the world's `Garages` and `Story` (money) directly, so a save keeps it. The globals the screens leave
for each other (the garage being edited, the name entry's target, the spec page's build, Wild Machines'
saved menu) are `FrontEnd::shop`.

The model stand (`model_stand.*`) assembles the fighter from a build row appended to its own copy of the
fighter tables, keeps each assembled machine by its classes, paint and sticker, and draws it with the
fighter pass (`draw_fighters`) over the screen's 2D: `Ui::scene` is the frame's 3D, handed to the renderer
in main.cpp. Camera eye (0, 2, -12) to (0, 2, 0), the mode's light, place and turn from `model_viewer.sexp`
(`model_viewer_places`, `model_viewer_cameras`), the mode's point per leg class (garage, dealer, Wild
Machines, spec page), the idle clip. Loads run on a worker one at a time (`stand_loader.cpp`, as retail's
`model_viewer_thread`): the screen asks, the old machine stays up, and the new one swaps in when its read
is done; a screen's destroy waits for the load in flight (`stand_settle`). The dealer and Wild Machines'
used Scoobees keep a line-up: each machine loads in turn, the cursor's slides in to the stand and the
others slide out 8 to the side.

PC: a left drag on the stand turns the machine (only inside the screen's stand area, `stand_area_set`,
chosen clear of its hit boxes and off while a box is up; the press never reaches the screen's own click
handling and the drag doesn't hover its menus), and the bound keys V / B turn it while held
(`stand_turn.cpp`). Its own turn waits while a hand holds it. Retail has no pad control for it.

The mouse: wheels (hub, Plus Tech, Wild Machines) turn with the mouse wheel and confirm on the lit item;
grids, tabs, rows and answers light on hover; the editor's list rows can be dragged: letting go on the
row opens its card, on the stand or the category's cell fits it at once (the list's accept and the
card's accept). Carousels step towards a clicked side card.

The numbers (`number_draw_full`), day, money, the garage banners, icon plates and glyph names are
`screen_header.*`'s, shared by every menu; `screen_kit.*` keeps the garage's pop timers, slides and wheels.

## The day round

The battle and result states run at 30 Hz (`tables/states.sexp`): main runs the front end's update every other world
step there and carries a press made on the skipped step to the next update. State 6 asks main's battle session
(`src/battle_run.*`) for its fight, which loads and builds it as `battle_task_begin` does from today's rumble context
of the area picked, steps it at 60 Hz under the front end's 2D and stops it when the state goes. `--battle-script
FILE` drives player 1 in every fight from a pad script, for headless runs through a tournament; `--seed N` sets the
world generator's index (a capture starts at 0, a normal run from the clock as retail). When a fight stops, the GPU memory it
uploaded (stage, machines, effects: `renderer_set_owner` / `renderer_free_owner`) is freed and its stage and part packs
closed, so a long campaign stays flat; every battle start prints `battle memory:` (handles, MB, packs, resident, and
glibc's heap in use, which leaves out what the allocator and lavapipe keep) and `PHANTOMCRASH_DEV_BATTLE_LOOP=STEPS:COUNT`
restarts a fight COUNT times for the check. Fifteen back-to-back fights on lavapipe (2026-11-12): resident 161, 254,
322 MB over the first three (caches and arenas filling), then 337..349 MB; the heap in use 56, 107, 155, then
137..157 MB, swinging with the fight and not climbing. The rise once seen on the last two of eleven fights is that swing.

## Input

Menus read player 0's pad record (VS setup reads each seat's, `Ui::pads`, docs/input.md "Seats") (`src/input`, docs/input.md) through `menu_accept`, `menu_cancel` and `menu_cursor`
(`pads_cursor_move` with the screen's sound and `menu_input_mark`). The mouse drives the same cursor
(`menu_pointer.*`): hovering an item moves the cursor to it with the d-pad's sound, a left click confirms (on the item when
the screen gives one, anywhere on a box or a "press start"), a right click backs out, the wheel is the d-pad's up and down
and steps a value under the pointer. Hit boxes are the items' sprites at their layout positions. The system cursor shows
once the mouse moves and hides on a key or a pad button. Escape is Back in menus and Start in a fight (devices.cpp,
`ReadOptions::fight`). Alt+Enter and F11 toggle fullscreen; the window's close button quits.

## Running

`scripts/run.sh` boots the game; `--state HEX` enters a state from black, `--press U:KEYS,...` presses pad buttons at
front end update U (names a b x y start back up down left right, or a hex word, joined by +), `--click U:X:Y` clicks, `--hover U:X:Y` moves the pointer there without a click,
`--drag U:X:Y:X2:Y2` presses at X, Y, moves to X2, Y2 over 20 updates and lets go, and the press names `turnl` and
`turnr` hold the stand's turn keys for that update.
`--vs N` enters state 6 with `vs_mode` from black (seat 0 the keyboard, the rest the pads, `--stage` the area) and the
front end goes on from its answer. `--viewer` (or `--stage`, `--build`, `--camera`, `--script`, `--trace`, `--day`, `--safe-rect`) runs the stage viewer
instead: a battle with player 1's fighter driven by the pads (or `--script`) behind the chase camera, or with `--camera`
the free camera alone. `PHANTOMCRASH_SAVE_DIR` and
`PHANTOMCRASH_GUBSY_ROOT` move the saves and gubsy's files for headless runs. Every state change is printed.
`--route FILE` drives a whole run by states (presses, shots, a fight's kills and doors) and `PHANTOMCRASH_DEV_DAY`,
`_WON` and `_BEATEN` jump the story: docs/playthrough.md.

## Widescreen

Genuine widescreen (owner, 2026-11-12: no clean black bars shrinking the screen to 4:3). The 3D fills the window at
any shape, keeping the original's vertical field of view; the original's 640x480 2D keeps its height and spreads over
the width: its middle stays in the centred 4:3 box, its edge pieces move out to the window's sides, and backgrounds
and bars are continued to the window's edges. PC CONFIG's ASPECT 4:3 puts the 3D and all the 2D back in the 4:3 box
with bars, as the original's picture; at 4:3 nothing moves.

### The frame

`render/sprite_frame.hpp` is the arithmetic every part shares: the box (the 640x480 fitted by height), its `spread`
(screen pixels the window reaches past the box on each side), `edge` (how far left- and right-anchored 2D moves out:
the spread times HUD WIDTH) and, in split screen, each view's own spread and the `shift` that centres the view's 2D on
its drawn 3D (retail's view rectangles sit inside the safe area; the port stretches the views' 3D over the window).
The sprite layer (`vk_sprites.cpp`) draws every pass through a viewport reaching past the box to the window's edges
(`sprite_frame_reach`: the spread plus the box's 32 px border, so nothing changes at 4:3) and clips each run by its
quad's anchor (`QuadAnchor`, `talk/draw2d.hpp`): centre to the box's 576x448 title-safe area as before, left and right
to that area moved out by the edge, free to the window.

### Anchors

The front end resolves each quad's anchor in `ui_present` from the screen's rules in `assets/ui/anchors.sexp` (the
port's own data, read at start; `src/screens/ui_anchors.*`, `ui_anchor_shapes.cpp`), keyed by the front end's state;
a state whose pages draw different art has a screen per page, picked each frame by `(shows ID...)`, the registry
sprites that page draws (the hubs' wheel pictures against their framed lists and the used Scoobees).
A rule matches a quad by its registry sprite (ids or ranges, optionally only inside a region), its texture slot (the
conversations' pictures and the font below 0x10000, the movie's 0x225), its size (a screen's frame pieces), or the
region of the 640x480 layout its middle is in (a turned quad's middle where the turn puts it; a split view's layout
fitted back to one view's); groups are shared with `(use NAME)`. The first match decides:

- `centre`: stays in the box, clipped as retail (what matches nothing);
- `left`, `right`: moved out by the frame's edge, clipped with it;
- `free`: stays, clipped to the window: the reticle, lock boxes, marks and tags (world points), the hubs' wheels and
  the hangar's cards, which run on past the box, the conversations and barks;
- `extend`: a background or bar touching the box's safe edges is continued to the window's edges, always at the
  picture's own scale (never stretched): its edge band mirrored back and forth (a band wider than the reach is a
  reflection), tiled as a period, or its edge column repeated (`clamp`, only where the edge is flat across: a bar's
  plain run; an edge with any structure is mirrored, or the sides show lines or streaks the middle doesn't have).
  `(inset 34)` first cuts it to just inside retail's title-safe area, whose border art was never meant to show (often
  unfinished: grey blocks, see-through columns, a frame line at the edge). `(soft R DIM)` blurs the continuation so
  nothing near the picture's edge shows again sharp; `(lines P)` keeps the scanlines drawn into the picture every P
  texel rows: the blur's taps step by whole periods across the rows (`Quad2D::blur_period`, the sprite shader's
  period) and sample as the picture does, so the lines run on sharp, in phase and at their contrast while the picture
  between them softens; `(feather W)` fades the picture's outer W pixels into a soft copy of themselves (`Quad2D::fade`,
  per-vertex alpha), so the soft reflection meets the sharp picture with no seam. `(fill U0 V0 U1 V1)` covers the
  sides with that patch of the picture instead (mirrored across, its rows reflected about their middles so a 2-row line
  stays in step, longer periods repeated whole) under one fading reflection of the edge band: art whose edges hold
  marks that mustn't show mirrored (the town map's places, key config's pad). `(rows ...)` treats bands of texel rows
  apart, each side by the first band naming it, `(side left|right)` limiting a band to one side (a picture whose header
  is a bar on one side and photo on the other); `(split U)` opens a bar at texel column U, its left part moving out with
  the left pieces and its right with the right, the plain column at U repeated across the gap (the header's DAY tab
  and money | its title);
- `blur`: a blurred, dimmed copy covering the window behind the picture (`Quad2D::blur`: the sprite shader's 32-tap
  disc; port-only, never set on an original sprite).

A screen may also lay a flat colour over the whole window under everything (`backdrop`). `PHANTOMCRASH_DEV_ANCHORS=N`
writes the Nth presented list to stderr, a line a quad (sprite, slot, place, layer, middle, the anchor it takes), for
writing the rules; `PHANTOMCRASH_DEV_ANCHOR_FILE=path` reads another rules file, for trying them.

Scrolling text lines (`text_strip_draw_scroll`) that reach the box's safe edge run on to the window's; a line's end
that a `(ticker-end X0 Y0 X1 Y1 left|right)` rule ties to moved pieces moves with them (the hubs' and conversations'
now-playing line runs on to the logo and meter, which the footer's split moved right). The menus' hit
boxes move with their art: `pointer_inside` resolves a rectangle's anchor the same way (`PointerRect::sprite` from
`sprite_rect`, else its middle's region) and moves it by the edge; the pointer maps the window into the 640x480 with x
outside 0..640 beside the box.

### The HUD

Corner gauges and readouts take `left` and `right` by region (the middle's x under 232 or over 408 in the one-view
layout, each split view's fitted back to it); the reticle, tracking and lock boxes, marks, radar and name tags
(0x560..0x573, 0x5B8..0x5BB) are `free`, so a box on a fighter at a wide window's side isn't clipped; READY, GO,
Crasher!, the pause plate and its items stay centred, as the conversations and barks (`slots 0 0xffff free`). In split
screen each view's 2D is centred on its drawn 3D (`HudScreen::sprite_shift`, the HUD's projection of world points
follows) and spreads over its width. The views' 3D is the rectangle they cover together (retail's safe area, which
they stop short of on the right and at the bottom by the gaps between them) stretched over the window
(`split_layout`), so no dead strip is left at the window's edges; the gaps between views stay as dividers. The
durability and O.C gauges' bars stay with their frames (`centre`, by sprite: a 4-way view's bar crosses the
right-anchor line).

The lens flare places the sun over the drawn 3D (`Effects::drawn_3d`, set by the scenery each frame) instead of
retail's 576x448 safe area and draws unclipped, so in a wide window it follows the sun out; retail's projection
without the divide by w and its 32 left, 16 up offset are kept (docs/bugs.md). Its fade and turn still measure the
sun's place in retail's units, so a sun at a wide window's side fades as one at retail's edge.

### Full-window layers

The layers retail draws over its whole picture are flagged `quad_full_window` (`src/talk/draw2d.hpp`) and stretched
with the 640x480 over the whole window, unclipped, so they cover the 3D under them at any width:

- the screen fade (`fade_frame`) and the conversations' fade (`talk_task.cpp`), the black screens between states
  included: 22 x 17 (704 x 544 from -64, -48) where retail's 21 x 16 stops at (608, 464), the edge of its safe-area 3D;
- the letterbox (`letterbox_draw`, `hud_draw_scenes`): retail's heights, the bars at full over y 0..64 and 384..480 of
  the 480, across the whole width;
- the pause's half-black dim, and the result screen's veil over its demo battle;
- the hit static in one view (`effects_screen`): the scratches and flickers from the screen's left edge across its 640
  instead of the safe area's 576, the scratches' band taken from the scene's whole width (split-screen views keep
  theirs, fitted to the view);
- the ending's black masks and switch-off (`black_quad`, `tint_draw`) and the dissolve's captured frame, which is a copy
  of the whole window.

They don't move with the screen adjust (the 3D does). At 4:3 they cover the 640x480's 32 px sides and 16 px
top and bottom too, which the safe-area clip left showing: the port's 3D fills the whole 640x480 where retail's stopped
at the safe area.

### The screens' 3D

The model stands (garage, hangar, dealer, assemble, VS machines) and the records' and result's trophies render over
the whole window with the window's aspect and the original's vertical field of view (`renderer_scene_aspect`), so a
model at a given place keeps its place against the box's 2D and the extra width shows more scene. The hangar draws
the machines of the two garages either side of the cursor's (retail's five), which covers a 21:9 window's neighbours.
Their 3D is scissored to where the 2D may reach (`FrameSetup::clip_to_2d`, set for the menus' frame): retail's
title-safe area in a 4:3 window or with ASPECT 4:3, as the original clipped it (the hangar's next machine no longer
pokes into the black border), the whole window once it is wide.

### Per screen

The rules are `assets/ui/anchors.sexp`'s; "frame" is the shared options-style frame (header opened at column 260,
a plain run between DAY and money and the title; help line across the window; footer to the edges; picture softly
reflected with its scanlines running on). Nearly every menu picture has scanlines drawn in every 6 texel rows
(conversations every 8, the town map's fine ones every 2): `(lines P)` carries them across the sides sharp and in
phase, `(feather 24)` hides the 4:3 edge. Before and after sheets with 1:1 crops across the edge:
`captures/widescreen3/` (not in git).

| screen (state) | background | treatment |
|---|---|---|
| press start, title (0x28, 0x15) | blue streaked picture 2643, scanline rows 2644, black bands 2645 | (a) the picture's 48-texel edge band mirrored out (its streaks run on); the rows sprite (flat across) and the bands repeated to the edges; the extensions end on whole pixels, so no gap line at the box's edge |
| options, sound config, PC CONFIG, system, save, load, key config, name entry, spec, stock, schedule, KEEP SV, media player, VS setup, records (0x1A, 0x23, 0xA0, 0x27, 0x1F, 0x11, 0x10, 0x16, 0x24, 0x26, 0x20, 0x2B, 0x21, 0x2C, 0x1D) | the frame: a 640x480 scanline photo, a 640x96 header, a 640x104 footer (some pictures hold the footer, the records' the whole frame) | bars: cut 34 in (just inside the safe area and its frame line), their flat edge columns repeated, the header split at 260 so DAY and money go left and the title right; photo: rows 96..480 reflected at the safe edge, blurred by whole scanline periods (`lines 6`) so the lines run on, feathered over 24 px; a picture with the footer drawn in keeps that footer's plain bar run on the right from row 376 (its meter never reflected); key config's right side carries on the picture's left half (`fill`), so its pad isn't mirrored |
| records and save files checks (9, 0x2E) | a striped card picture (slot 549) | (a) reflected at its sides |
| screen adjust (0) | a fight's picture with its HUD drawn in | (c) a blurred (10), dimmed (0.5) copy behind |
| parts editor (4) | the frame | frame; HANGAR 01 plate left, BODY SOCKET count right |
| area select, job menu (3) | the frame, the tab bar 857 | frame; the tab bar clamped to the left edge |
| hangar (0xE) | the frame's picture | frame; the wheel's cards and numbers free (the neighbouring garages and their machines show), the shaded ends 1574 and 1575 out to the sides, the plates' bar clamped |
| garage hub, Club Wired, Plus Tech, Wild Machines (0xD, 8, 0x1B, 0xF) | a picture holding the header tab with its motto, a 640x184 footer with the place's logo; their lists and the used Scoobees framed | `(shows ...)`: on the wheel, the black top repeated, the tab carried on from its plain 12-texel edge strip (softly, never the motto), the photo reflected softly with its lines from the tab down on the left and from the top on the right, DAY and money staying on the tab; the footer split at 300 so the logo and the meter go right, the now-playing line running on to them (`ticker-end`); the wheel free. The other pages take the frame |
| help menu (0x13) | as the hubs, a 640x72 header | as the hubs, the header's flat edges repeated; Pepper right |
| town map (0x29) | the map picture 2668 with its place marks, the TOWN MAP logo and the footer drawn in, a 640x72 header | the camouflage carried on from a mark-free strip along the map's top (rows 26..78), mirrored across and reflected about its rows' middles so the fine lines stay in step, blurred by 10 and dimmed to 0.9 (the strip is lighter than the map's edges) under one fading reflection of the map's own edge, feathered over 32 px; below row 356 (left) and 374 (right) the logo's, music bar's and footer's flat bands repeated; header split; the place line along the header free |
| Sonic Amp (0x14) | the frame, the spotlight band 2444, the tab bar 2463 | frame; the band and bar (flat across) repeated to the edges; the banner line along the header runs on with it (the frame's text rules free any line along a 640x72 header: the dealers' help line too) |
| dealer (0x22) | the frame over the scrolling backdrop | frame |
| result (0x1E) | the demo battle (3D, full width) | frame bars over it |
| conversations (0x30..0x9D) | the place's frame picture (talk slot, 640x480) | header rows' flat edges repeated and split at 260, picture rows 72..300 reflected softly with their lines every 8 rows, footer rows split at 300 (the logo right) and the now-playing line running on to it; the portrait (264x336) right, the skip hint right |
| movies (0x9E, 0x2D, 2, 0x19, 0xB, 0x25, 0xC) | the movie frame (slot 0x225) | (c) a blurred (14), dimmed (0.45) copy of the frame behind the 4:3 picture, never stretched |
| logos (0x12), the night (0x18) | black (the original's) | none: the logos sit on the original's black |
| fights, test drive, free play, the replay, the attract demo, the ending's scene (6, 0x2F, 0x2A, 5, 1, 0xA) | the 3D, full width | the HUD group: corners left and right, world marks free, messages centred; the letterbox, pause dim and masks over the whole window |

## Differs from the original

- The load screen shows the port's three slots (left and right pick one) with the pilot, money and day written in the
  font beside the screen's labels; retail showed slot 0's summary with the number fonts and a time and date.
- The load screen's help line (`D_001DDAE4` -> `D_001DDAA0`, "Load play data") is empty: the ui family doesn't extract it.
- The day and money on the options and sound screens aren't drawn yet (`screen_header.*` has `day_draw` and
  `money_draw` now). Retail's `day_draw` and `money_draw` overwrite their position arguments with (0x33, 0x31) and (0xFD,
  0x31), so every screen's day and money land there whatever it passes; the port does the same.
- A load (`save_commit_load`) copies the button config to player 0 (`World::player_buttons`, whose keys the player's
  fighter takes at spawn, `battle_spawn`), applies the volumes and rebuilds the battle playlist (`music_player.*`).
- The load and save screens' slot card draws pilot, garage name, money and day as `save_slot_draw_summary` places them;
  the date, time, chip picture and beaten mark sit at places retail computes at run time (UNKNOWN, not drawn).
- The screen adjust moves the 3D viewports (`Renderer::screen_shift`, set by `ui_present`; `scene_viewport` and the
  depth probe in vk_frame.cpp) and every 2D pass with them but the fade and the full-window layers (`Ui::screen_shift`;
  the effects' 2D in scenery.cpp), at the 2D's scale, so both move together as retail's viewports do. The 2D's box
  clips (centre, left, right: `RendererState::screen_shift` in vk_sprites.cpp) and the menus' 3D clip
  (`scene_clip`) move with them; free and full-window quads keep the window.
- Key config: a mouse click never binds a button (the mouse presses the triggers in menus); a click on rows 0..2 flips
  the value; no cancel, as retail (row 12 leaves).
- The result screen draws its trophy into `Ui::scene` as a late item: render pass 13, between the 2D of pass 12 and
  that of passes 14 and 15 on a cleared depth, as retail (`model_render_submit(.., 0xD)`). The records screen's are pass
  2 (`record_model_draw`'s `model_render_submit(.., 2)`): over the backgrounds, under the cards and grids. The model is found by its render model slot
  through the manifest, loaded the first time and freed when the screen goes (`trophy_models.*`).
- `PHANTOMCRASH_LOAD_SLOT=N` (port-only) loads a slot before the first state, for headless runs with `--state`.
- The help menu's topics go back to the help menu by `menu_screens_next` (the talk states' updates in retail).
- `system_item_sprites` is an IdPair table the ui family doesn't extract; its ten ids are in `system_screens.cpp`.
- Sonic Amp, the media player and the dealer read the mouse as the other menus do: hovering a tab, cell, item,
  list row or button moves the cursor there, a left click confirms on it, a right click is B; the wheel turns chart
  and catalogue pages and the dealer's hangar wheel.
- The media player opens on the playing list (retail opens on playlist 1, docs/bugs.md), and its catalogue has whole
  pages only.
- The dealer's stand loads the machine under the cursor at once (retail loads the three on a thread, the cursor's
  first). Its machine buttons, cards and boxes come from the layout tables; `dealer_index_set(fe, spot)` is the town
  map's call (spot 7 Aeron, 6 Photon, 5 Holy; `MenuGlobals::dealer_index`).
- The lost-controller box (menu states, kind 1) watches player 0's pad as retail does: the one that pressed START on
  the title return (`pad_watch_title_start`; none when the keyboard or a click did). Pulling any other pad does
  nothing. A pad plugged in after the watched one went is taken as it (SDL gives a pad plugged in again a new id), and
  the keyboard's START or accept closes the box as a reconnect would (the keyboard is a controller here), so it never
  traps a player who put the pad away. `PHANTOMCRASH_DEV_PAD_LOST=U` pulls the pad at update U,
  `PHANTOMCRASH_PAD_UNPLUG=F:P` pulls virtual pad P at frame F and `PHANTOMCRASH_DEV_SONGS=N` owns songs 0..N-1, for
  headless captures.
- Free play (0x2A) hands `FrontEnd::extra.free_play` to the battle entry: lesson (from `MenuGlobals::free_play_lesson`),
  phase 4, `rumble[3]` (selector 6), tournament 0 for lessons 0 and 3 or 55 for 1 and 2, build 0x411 for player 0 on
  team 0, lesson 2's second fighter (the same build, AI, team 0xB), the entrants as the fill leaves them (one for tournament 0,
  six for 55, whose empty slots draw the lessons' drones as replacements), the entrance (tutorial scene 0x18, 0x23, 0x2A, or a
  gate for plain free play) and the default buttons for the lessons. The human stands at the origin (but lesson 3), the
  AIs at entry point slot % count, frozen, unable to fire or turn the view, with no immunity; every fighter's empty weapon
  refills each update (phase 4). The pause menu (0xEB1 with continue 0xE8F and quit 0xE90, player 0's pad) ends the
  pause on accept, and quit sets phase 6 under the half-second fade back to 0x13; the door, a death or a lesson's end set
  phase 6 too (decomp "Pause in battle", `free_play_screen_step3/4`). `PHANTOMCRASH_DEV_PAUSE=U` pauses at update U.
- The lessons (`src/battle/battle_tutorial.cpp`, scene states 0x18..0x2E): each step locks the controls, says its line,
  opens the axes and buttons it teaches (`actor_input_restrict`, Fighter::axis_zero / control_allow, free play only) and
  waits for the move, then the closing line. The lines are the battle's scene text: the world asks for the script its
  lesson table points at (`tutorial_say`'s D_00243080 / D_00243138, by control type) and waits; `scene_text.cpp` finds the
  script in `scripts/bark.sexp`'s used-by, plays it on player 0's accept at 30 Hz and answers through
  `Inputs::scene_text_done`, so the world stays a function of its inputs. The counters measure the step's displacement
  (the port's walk has no velocity under root motion).
- The hangar (`hangar.cpp`) runs on the world's garages directly (retail edits a copy and writes it back on leaving),
  its machines on the model stand in mode 6 (camera row 0, light (2, -3, 4)), each 7 units per card from the cursor's,
  the cursor's turning 0.25 degrees an update; loaded at once. The mode it was opened in is kept in
  `Campaign::hangar_mode` across the editor, the spec page and the test drive, as retail's GameState +0x10FBC.
- After the credits there is no New Game+ restart in retail either: the night (0x18) carries on with the beaten bit
  set, `completed_cycles` counts each 59 -> 0 wrap (`day_advance`) and raises the opponents' levels.
- "Press start" takes A as well as Start, so Space confirms there as Enter does (the PC's two confirm keys); the pad
  record can't tell Space from a pad's A, so a pad's A starts there too. Retail takes only Start.
- The sound config's volumes stop at 0 and 255 instead of wrapping on a fresh press (docs/bugs.md).
- The town map's marker spots (`town_map_marker_spots` 0x1FEAB8) aren't written by the ui family yet; their values are
  the decomp's, in the code.
- The hub screens' `day_draw` / `money_draw` place the day and money where retail does, at (0x33, 0x31) and (0xFD, 0x31)
  whatever the caller passes (retail ignores the arguments too).
- The battle's scene cameras (`src/view/scene_camera.*`, curves from the scenes family's `tables/scene_cameras.sexp`):
  the entrance's four shots blended into the chase camera over their last 20 ticks while the fighter walks out of the
  gate on its scene's root motion (the world's, `battle_scenes.cpp`), the door's shot, the death scene's front views of
  the killer and then the wreck, a boss's entrance curves and its defeat's rail camera; each with its fov and roll, cuts
  kept from interpolating. A scene's fov is its view's alone (retail's is global: bugs.md).
- The pause draws the stopped battle under a half-black quad instead of a captured frame at half brightness; it looks the
  same. A tournament's pause is retail's PAUSE with Start the only way out; quitting is the window's close button. VS's
  pause is free play's two items on every entered seat's pad, its quit asking first (`vs_quit_box`); Yes ends the round
  to the title from the front end (retail sets `vs_end_state` 5 in the battle; online it would have to be an input).
- The lost-pad pause (kind 2) in every fight: outside VS when player 0's last gamepad goes, in VS when an entered seat's
  device goes. It pauses once with the reconnect box (the lowest missing seat's number); Start on whatever is left goes on
  and the pulled seat reads a pad at rest. Retail blocks Start until the pad is back.
- The replay is the whole fight's World at its first step and every step's Inputs, stepped again in battle phase 2; the
  live World is put back when it ends, so watching it changes nothing (retail restores the rumble context, the pools and
  the won flags for the same reason). Each step's fight hash (`world_fight_hash`) is checked against the live one's and
  the run log says whether the replay ended on the live fight's hash. The port's loads never run late, so a replay is
  never cut at a replacement (`replay_cut_mark`). Views 0 and 2..5 are presentation's (`scene_camera.cpp`); view 1 is
  the fighter's own chase camera, the only view the port's fighters play in.
- The ending's scene (0xA) freezes the battle on its last frame and masks it to `screen_tint_draw`'s switch-off
  (`scale_alpha_keys`) instead of drawing the captured texture; it looks the same.
- KEEP SV keeps each machine as `<save dir>/vs/<name> for VS.sexp` (the machine's name, its full-width letters as ASCII);
  every file is read on entry, a file that won't read shows as a damaged row, and the drive's block and save-count limits
  can't happen. The kept time is drawn with `date_draw` and `time_hms_draw` in number font 0, as retail. Each file also
  keeps the keeper's button config (`hdr.stats`, `GameState +0x6018`), which the machine's VS player plays with. The
  mouse (the port's): a place takes the list's cursor, SAVE and EXIT the side menu's, a click on a scroll arrow or the
  wheel over the list scrolls it, and each box's answers are hit on their own art.
- The save and load card draws `save_slot_draw_details`: the date and time it was written, the selected garage's Chip
  (`chip_identity_sprite`) and the beaten mark 0x912, at the places four vec2 statics' constructors set at start
  (`D_0096CDF8` (279, 215), `D_0096CE00` (287, 230), `D_00966D00` (287, 139), `D_00966D08` (289, 198)). The port's slot
  number ("2/3") sits under the card, since the Chip covers the card's middle.
- The options, sound config and PC CONFIG draw the day and money unless the options were opened from the title (retail's
  `options_screen_draw`, `sound_config_screen_draw` and `key_config_screen_draw`).
- The result screen's demo battle is main's battle session asked with `Campaign::battle_wanted` 2: the area's context
  refilled and six drawn opponents fought by the AI in phase 1, its sounds muted, the camera on the rails within 12.5 of
  the fighter `demo_battle_enter_next` follows (the world's, on even steps); the context, the line-up and the fight's
  result are put back when it stops. The trophy (`tables/trophies.sexp`) has its moment before the count, its caption
  and the veil. `PHANTOMCRASH_DEV_TROPHY=N` shows trophy N for captures.
- The job map's fighters (`job_wanderers.cpp`) walk the area's day stage route graph (`load_nav_graph`) in float in the
  front end, as retail's menu does, drawing the world's generator; they leave after 120..235 s and a new draw walks in
  from a gate 20..38 s later while more than one is about. Gate select copies each walker's route target into its roster
  entry (`rumble_roster_starts_set`), and the battle starts those opponents there with no entrance (scene 0), the rest
  through gates. In a ranker job only the ranker shows, at its rolled gate.
- CHARACTERS shows a ranker's base level and pay without writing them into its roster entry, as retail does (docs/bugs.md).
- Barks (`src/hud/hud_barks.cpp`): the world asks at retail's call sites (a bit per kind in `Fighter::barks`); the HUD
  applies the speaker rule and chance (the scenes family's `tables/barks.sexp`) and `battle_barks_update`'s pick, and
  plays the Area Ranker's defeat line over any bark. "Battle chatter: Skip" starts no bark (the ranker's line still
  plays: it is the scene's). The boss defeat waits out that line's measured length.
- The options screen has a seventh item, PC CONFIG, before End (its words cut from the original's buttons); the
  items are 43 apart as retail's six (from y 121), the column centred where retail's was, so it starts half a step
  higher (y 99.5) and ends under the footer's rule (captures/platform/options_seven.png).
- In menus the mouse buttons also press the triggers their fight binding names (left: right trigger, right: left
  trigger); nothing ported reads the triggers in menus, but name entry's page tabs will.
- The model stand loads at once instead of on a thread. The shops' stand model 0x4D9 isn't drawn: it sits
  outside retail's 4:3 view and would show at the edge of a wide window.
- The name in name entry is drawn in 18 px cells (`wide_text_draw`), the caret's step.
- The spec page's label panels (`spec_sprites` rows 6 and 10, sprites 2541 and 2542) are drawn at
  (0, 0) as the table has them, which puts them over the top left; worth a look against retail.
- Stock's card has no label plates (its two pop timers' sprites weren't read).
- VS setup (0x2C): a player is a seat on any device, not "pad i" (docs/input.md, "Seats"): any device's accept on
  the join page takes the first free panel and the player's cancel leaves, so the panel's "connected" art shows while
  a device nobody holds is there. A just-joined seat's pad counts once its joining press is let go. The machine list
  has the three stock builds, then KEEP SV's kept machines oldest first, three in view with the 8-frame scroll; a kept
  machine that doesn't read shows the error card as it comes into the list's five slots and can't be locked; a
  locked kept machine fights as a build row after the table (`battle_vs.cpp`, as the campaign's garage machine). A
  kept machine of a body class past 5 shows the unreadable cell (retail stops drawing the list there, read). Random's cycling picture and pick use CRT
  `rand` (retail draws from the gameplay RNG; menus aren't synced). The round fades in over half a second and leaves
  as its fade-out ends (docs/bugs.md, `battle_enter`).
- The join page names each entered panel's device (`seat_device_label`: KEYBOARD AND MOUSE, PAD 2) in PC CONFIG's
  label letters, dark, along the edge of its "You entered." box away from the hint tab.
- VS setup's mouse is the keyboard seat's (`vs_mouse_seat`): while nobody holds the keyboard a left click joins it and
  a right click backs out, as Enter and Escape would; once seated its left click is that player's accept and its
  right click their cancel, hovering a machine cell, an area label, a day or night answer or a yes / no moves that
  player's cursor (the machine list also takes the wheel), and a click accepts on the item under the pointer.
- `PHANTOMCRASH_DEV_BUILDS=row,row` puts those `scoobees.sexp` builds into garages 0.. (and boards
  garage 0) and `PHANTOMCRASH_DEV_MONEY=N` sets the money, for headless captures of the garage screens;
  `PHANTOMCRASH_DEV_DROP=legs,chip` (body, legs, arms, backs, chip) takes those parts off garage 0;
  `PHANTOMCRASH_DEV_CARRY=durability,broken` sets the machine the ending carries (a bit per broken mount).
- `PHANTOMCRASH_DEV_SHOTS=F,F,...` with `--capture out.png --frames N` also writes frames F on the way to
  `out_F.png`, so one run captures several moments (the ending's shots, a dissolve); the viewer too.
