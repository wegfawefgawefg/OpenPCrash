# Settings

The PC's own settings: what the Xbox never had to ask (window, resolution, v-sync, frame rate, volumes over the
whole mix, keys and mouse) and a few comforts. The original's own options (vibration, BGM and SE volume, button
config, screen adjust) stay where they are, on the original's screens, saved with the game.

They live on one screen of the port's own, PC CONFIG (state 0xA0), reached from the options screen's PC CONFIG
item, so from the title and from the system menu alike, and from any fight's pause (below, "From the pause"). The
list follows Mans In My House's settings (`src/menu/settings_rows.cpp` there) where they fit a 2002 console port.

## Where they're kept

Gubsy keeps them, under its data root (`<SDL pref path>/gubsy`, or `PHANTOMCRASH_GUBSY_ROOT`):

- the install's, in its top-level settings (`settings_profiles/top_level_game_settings.lisp`), keys
  `phantomcrash.video.*`, `phantomcrash.audio.*`, `phantomcrash.game.*`. Not gubsy's own `gubsy.video.*` and
  `gubsy.audio.*` keys: gubsy's settings catalog writes its defaults for those into the same file the first time
  anything builds it (window mode `fullscreen`, `1280x720`, music `0.8`), which would then read back as the
  player's choice;
- player 1's, in their gubsy settings profile (`phantomcrash.input.*`, `phantomcrash.story.*`), as
  `input/bindings.cpp` registers them;
- the mouse, in player 1's gubsy input settings profile (`mouse_sensitivity`, `mouse_invert_y`), which
  `input/devices.cpp` reads every step;
- the bindings, in player 1's own binding profile (id 1; the premades 100..102 are rebuilt every start).

`src/screens/pc_settings.*` reads them at start (`pc_settings_load`), writes them on every change
(`pc_settings_save`) and applies them (`pc_settings_apply`): the window through SDL, the renderer through
`renderer_set_options` (`RenderOptions`, `src/render/renderer.hpp`), the sound through `set_pc_volumes`
(`src/audio`). Everything applies the moment it changes; there is no OK or Apply step.

## The settings

Status: **done** works now; **part** works but something outside PC CONFIG still has to read it; **to do** not
built. Defaults in bold.

### Display (page SCREEN)

| row | values | key | what it does | status |
|---|---|---|---|---|
| DISPLAY MODE | **WINDOW**, BORDERLESS, FULLSCREEN | `video.window_mode` | windowed at its size; borderless is SDL fullscreen with no mode (over the desktop); fullscreen sets the exclusive mode below. On the display the window was last on (`video.display`), else the window's, else the primary (`src/screens/pc/pc_display.cpp`) | done |
| RESOLUTION | window sizes that fit the display (4:3, 16:9, 16:10, 21:9; **the command line's, 1280x960**), no larger than its room for windows, or the display's exclusive modes in fullscreen, in pixels (a high-density mode's points times its density); the desktop's, fixed, in borderless | `video.window_size` `"1280x960"`, `video.fullscreen_mode` `"WxH@Hz"` or `desktop` | the window's size, or the screen mode in fullscreen (a size once, at its fastest refresh; a mode the display lacks gives the nearest it has, a display with none fullscreen over the desktop); a window resized by hand keeps its size (written half a second after the drag stops; not while maximized) and the list offers it | done |
| REFRESH RATE | the rates the display's exclusive modes have at fullscreen's size, slowest first, to two places where not whole (59.94); dim with the desktop's (or DESKTOP) in a window or borderless | `video.fullscreen_mode`'s `@Hz` | the exclusive mode's refresh (`pc_refresh_step`), SDL's exact rate; RESOLUTION picks a size at its fastest | done |
| VSYNC | **ON**, OFF | `video.vsync` | FIFO; off is mailbox where the surface has it, else immediate | done |
| FRAME LIMIT | **MAX**, 30, 60, 75, 90, 120, 144, 165, 240 | `video.frame_cap` (MAX is 0; it read OFF before) | the most frames a second; the loop waits out the rest of each frame (`pc_frame_end`). MAX is a ceiling, not no limit: a fight, which draws between its steps with an interpolation alpha, stops at 500 frames a second (`frame_ceiling`, src/screens/pc/pc_settings.hpp) with v-sync off, and v-sync on holds it to the display (nothing passes 500 even where a v-sync present doesn't wait). The menus, the movies and a paused fight draw at the display's refresh rate (src/front_end_run.cpp): with v-sync on FIFO paces them, with it off each frame waits out the rest of one refresh (the window's display's rate from SDL, 60 when SDL doesn't know it); a limit under the refresh holds them to it. Their picture changes only on a 60 Hz step; the frames between show it again. The stage viewer (`--viewer`) draws like a fight. The game steps at 60 whatever it draws | done |
| SHOW FRAME RATE | **OFF**, ON | `video.show_fps` | "60 FPS" in the label font on a dim plate in the top right, over everything but the fade, the frames presented, counted twice a second (`pc_frame_end`, drawn by `pc_fps_draw`) | done |
| HUD WIDTH | 0..**100**, by 10 | `video.hud_width` | how far the HUD's corner gauges and readouts and the menus' edge pieces (DAY and money, the screens' titles, the hubs' logos) move out of the 4:3 middle toward a wide window's sides: 0 keeps them in the 4:3 box, 100 puts them at the window's safe edges. Backgrounds and bars reach the window's edges at any value; the reticle, boxes and messages stay where they are (docs/screens.md "Widescreen"). Nothing changes at 4:3 or with ASPECT 4:3 | done |
| HUD SIZE | 70..**100**..130, by 10 | `video.hud_size` | the fight's HUD scaled piece by piece about its place on the title-safe area's edge: the gauges, readouts and their numbers (the HUD's own layers, hud.md: 0x101..0x103, 0x108) that the anchors put left or right, about their side (32 or 608) and, down, the top (16) or bottom (464) for the top and bottom fifths, else the middle (240), so the weapon readouts stacked down the sides spread or close up evenly; the durability and overcharge gauges at the bottom's middle about (320, 464) (`hud_piece`, `hud_scale` in src/screens/ui/ui_anchors.cpp, on screens that take in the anchors' `hud` group). The reticle, boxes, marks, radar and name tags stay where the world puts them; the VS marks, the barks, READY and GO and the pause as they are; one view only (split screens keep the original's smaller tables). The stage viewer (`--viewer`) sets no anchor screen, so neither HUD WIDTH nor HUD SIZE reach it | done |
| DEFAULT | | | the page back | done |

F11 and Alt+Enter flip DISPLAY MODE between WINDOW and BORDERLESS (FULLSCREEN goes back to WINDOW), saved like a change on the page.

The window follows SDL3's documented order (`pc_display_apply`): out of fullscreen and maximized before a size or a place is asked (SDL ignores both there), an exclusive mode only one `SDL_GetFullscreenDisplayModes` lists for the display (the mode's display is where the window goes), `SDL_SyncWindow` after each step that may be asynchronous. The display the window moves to (dragged there, or one unplugged) is kept as its name and where it sits (`"NAME@X,Y"`, two of one name told apart by place); a display unplugged puts a fullscreen window back in its mode on what is left. Each apply says what came of it in the run log (`settings: window MODE on "DISPLAY" WxH (pixels) [fullscreen], mode`). Checked on a private Xvfb with a private i3 (2026-11-12): fullscreen and borderless fill the 1600x1200 screen, WINDOW is 1280x960, F11 flips each way and FULLSCREEN back to WINDOW at run time, a kept display that isn't there and a mode the display lacks fall back, a window size larger than the display is held to it (captures/platform/display_*).

### Graphics (page PICTURE)

| row | values | key | what it does | status |
|---|---|---|---|---|
| RENDER SCALE | 50, 75, **100**, 125, 150, 175, 200 | `video.render_scale` | every target at this percent of the window, the output scaled to the window (nearest at 100, linear otherwise). Over 100 is supersampling | done |
| ANTI-ALIASING | **OFF**, FXAA | `video.antialias` `"off"`/`"fxaa"` | FXAA over the 3D in the port's post pass, after the composite (dof, dofblur or the tinted copy) and the cockpit's meshes, before the 2D, so the menus, the HUD and text stay sharp (`assets/shaders/original/post.frag`, `vk_record_post`). Validated on lavapipe. MSAA would need a resolve of the depth the dof reads; not offered | done |
| ASPECT | **WIDE**, 4:3 | `video.fit_43` | wide: the 3D fills the window, the 2D spreads over it (docs/screens.md "Widescreen"); 4:3: the 3D and the 2D in the 4:3 box with bars, as the original's picture (`renderer_scene_aspect` gives the projection) | done |
| SAFE FRAME | **OFF**, ON | `video.safe_area` | the 3D in the original's 576x448 title-safe rectangle with the black border, as the Xbox showed it (`--safe-rect`); the stage viewer, the campaign's fights, VS (each split view) and the replays | done |
| BRIGHTNESS | 50..**100**..150, by 5 | `video.brightness` | a power curve (`x^(100/b)`) over the 3D alone, in the same post pass as FXAA (the composite and the cockpit's meshes); the menus, the HUD and the fades keep the original's brightness. Frames without 3D (the 2D menus) never take the pass. The 2D drawn under the 3D (overlay pass 0) is in the scene, so it is lifted with it; pass 13's late meshes, drawn over the 2D, are not | done |
| FLASHES | 0..**100**, by 10 | `video.flashes` | how bright the flashes are: the effects' point lights (blasts and shots lighting the scene), the muzzle flashes, the lens flare and the hit static's flicker, scaled in presentation (`Effects::flash_scale`, `RenderOptions::flashes`); the world never sees it | done |
| CAMERA SHAKE | 0..**100**, by 10 | `video.camera_shake` | how hard the cockpit view shakes when hit: retail's only shake (`hit_shake_jitter`). The world shakes the fighter's look-at as retail does (the aim, boxes and senses with it); this scales only the drawn camera and the HUD's points with it (src/view/cockpit_view.cpp, `RenderOptions::camera_shake`); 0 a still view, the aim as retail's either way | done |
| FIELD OF VIEW | **82**..130, by 2 | `video.fov` | how wide the fight's camera sees, in degrees across a 16:9 view as PC games give it: 82 is retail's 52 degrees vertical (2 atan(tan 26 x 16/9) = 81.9; 82 draws exactly 52), 100 is 67.7 vertical, 120 is 88.5, 130 is 100.7. The game keeps the vertical angle that stands for, so a 4:3 window sees less across and a 21:9 one more (Hor+, as at 52). It widens the fighter's own views only: the chase views, the cockpit, each split-screen view in VS and a replay's views, and an entrance's camera as it hands over to the fighter's (by the fighter's share of the blend); the scene cameras (entrances, doors, deaths, bosses, the demo battle's rails) and the garage stands keep their own (`fight_fov_apply`, src/view/scene_camera.cpp). Presentation only: the world's aim and lock-on keep retail's 52-degree projection (src/weapons/hud_aim.cpp), so the HUD's world points (the reticle's centre, the tracking box and dot, the lock box, the missile and melee marks, the mouse's target dot, the radar ring and its off-screen arrows, the name tags and the shake's offset) are moved from the world's screen to the drawn one, about the view's centre by tan 26 / tan(vertical / 2) (`HudFrame::world`, src/hud), and the cockpit's instruments and the weapon readout's panels (pass 11) keep the view's own 52. The lens flare and every 3D effect go through the drawn camera's own matrices, so they follow it. The world's "on screen" stays retail's: someone at a wide view's edge, outside retail's picture, gets an off-screen arrow and no name tag. Checked headless at 82, 100, 120 and 130 with a dummy locked, chase and cockpit, 4:3 and 16:9, and a VS split (captures/fov): the boxes, marks, dot and tags sit on the target at each, 82 draws byte-identical to before, the world hashes match at every value. A split-screen strip keeps the vertical too, so it is very wide across at 120 and over | done |
| TEXTURE FILTER | **TRILINEAR**, ANISOTROPIC 2, 4, 8, 16 | `video.anisotropy` | anisotropic filtering on the meshes' filtered samplers where the device has it (samplers and texture sets made again on a change) | done |
| SOFT FOCUS | **ON**, OFF | `video.soft_focus` | the battle composite's blur, dof and heat haze, the original's soft look (docs/bugs.md keeps it); off draws the tinted copy instead | done |
| DEFAULT | | | the page back (v-sync kept) | done |

### Audio (page SOUND)

| row | values | key | what it does | status |
|---|---|---|---|---|
| MASTER VOLUME | 0..**100**, by 5 | `audio.master` | the whole mix, after the original's levels | done |
| MUSIC VOLUME | 0..**100** | `audio.music` | the streams (music and the movies' sound) over the original's BGM volume. The movies stay with the music: retail plays them at its music volume (decomp docs/port/audio.md, Sofdec's volume from `music_volume`) | done |
| EFFECTS VOLUME | 0..**100** | `audio.effects` | every sound effect and its reverb over the original's SE volume | done |
| MUTE IN BACKGROUND | **ON**, OFF | `audio.mute_unfocused` | master to 0 while the window hasn't the keyboard | done |
| MUSIC | OPEN | | opens the MUSIC panel (below, "Music") in PC CONFIG's place; its EXIT comes back to this row | done |
| DEFAULT | | | | done |

The original's two volumes (BGM 0..255, SE 0..255, `GameOptions`) stay in SOUND CONFIG and in the save.

### Controls (pages INPUT and BINDINGS)

| row | values | where kept | what it does | status |
|---|---|---|---|---|
| MOUSE SPEED | 10..**100**..500, by 10 | input settings profile `mouse_sensitivity` (/100) | the upper body's turn per count (docs/input.md, "Mouse aim") | done |
| MOUSE UP DOWN | **NORMAL**, REVERSE | input settings profile `mouse_invert_y` | | done |
| AIR TURN | **MOUSE**, KEYS | game setting `phantomcrash.input.air_turn_mouse` | MOUSE: in the air under Preferred, while the mouse aims, the keys' left stick across (A and D) is dropped, so only the mouse turns the machine (owner's playtest, 2026-11-13); a pad's stick is never touched. Read in `devices.cpp` from the fighter's `airborne` as the step's input is built, so the world gets plain input | done |
| DOUBLE TAP STEP | **ON**, OFF | game setting `phantomcrash.input.double_tap_step` (1 on, 0 off) | under Standard the left stick's double flick (out, back, out again within 10 steps) steps, as retail; on the keys that is A or D tapped twice. OFF: only the step buttons (Q and E, white and black) step (owner's playtest, 2026-11-13). Not a pad hack: the seats' settings are put in the world as the fight's setup (`World::player_controls`, `pc_controls_commit` before the spawns), and the spawn sets the fighter's `control_type_no_flick` bit beside `button_control_type`, which `control_route` reads; hashed and in the bug reports' fight record. Each VS seat reads its own settings profile (this row edits player 1's). Over a paused fight it says NEXT FIGHT | done |
| LEFT DEAD ZONE | 5..**50**, by 5 | `phantomcrash.input.left_dead_zone` | the left stick's dead zone per axis, percent of the travel; 50 is exactly the original's 0x4000 and the default, less suits a modern pad (docs/input.md, "The pad record") | done |
| RIGHT DEAD ZONE | 5..**50**, by 5 | `phantomcrash.input.right_dead_zone` | the right stick's, the same | done |
| OUTER DEAD ZONE | **0**..30, by 5 | `phantomcrash.input.outer_dead_zone` | the far end of each stick's travel that already reads as full, for a stick that no longer reaches its edge; 0 as retail | done |
| STICK CALIBRATION | PAD 1..4 (the pads plugged in), NO PAD | install settings `phantomcrash.input.calibration.<GUID>.serial.<serial>` (or `.path.<path>` and `<GUID>`) | accept opens the calibration panel on the pad in place of the rows: let go and press A (the rests), turn the sticks round their edges and pull the triggers and press A (the reach): kept for that pad (by its serial where SDL reads one, so two of one kind keep their own), then shown through it. B back, Y or Delete puts the pad back as it came. The reads rescale each axis from its rest and reach before the dead zones (docs/input.md, "Calibration") | done |
| VIBRATION STRENGTH | 0..**100**, by 10 | input settings profile `vibration_strength` (/100) | how hard the pads rumble when hit (src/input/pad_rumble.cpp); each VS seat reads its own profile's. OPTION's VIBRATION (the original's) turns it on and off | done |
| BINDINGS | PLAYER 1..4; a row per pad button and PC action: the keys and mouse buttons, the pad buttons; the axes: MOVE STICK and AIM STICK (LS, RS, NONE), MOUSE AIM (ON, OFF), LEFT and RIGHT TRIGGER (LT, RT, NONE), WHEEL (VIEW, OFF) | binding profiles 1..4 | PLAYER picks whose profile the page edits: players 2..4 play with player 1's until something is changed for them, which makes their own as a copy (`seats_sync` hands each VS seat its own once it exists). Accept on a button row listens (6 s): the next key or mouse button replaces the row's keys, the next pad button its pad buttons; Escape stops; Delete clears the keys (an axis row: nothing drives it), Shift+Delete or a pad's Y the pad buttons. Left and right step PLAYER and the axes through what the input layer reads. DEFAULT copies the keyboard, mouse and pad premade as the picked player's. Key names with signs the label font lacks are spelled out ([ is L BRACKET, ; SEMICOLON). The help line says what else the control presses | done |

The eight fight buttons are labelled by what the original's BUTTON CONFIG makes them do now (`ButtonConfig::keys`),
with the pad's own icons; the rest by their fixed meaning.

### Gameplay (page GAME)

| row | values | key | what it does | status |
|---|---|---|---|---|
| STORY TALK | **SHOW**, SKIP | `phantomcrash.story.conversations` | trigger conversations run unseen (docs/story.md, "Skipping"; read every update, `story_talk_skipped`) | done |
| BATTLE CHATTER | **SHOW**, SKIP | `phantomcrash.story.battle_chatter` | the pilots' lines in fights never show (`battle_chatter_skipped` into the barks) | done |
| PAUSE IN BACKGROUND | **ON**, OFF | `phantomcrash.game.focus_pause` | a fight opens its pause while the window is away (`src/front_end_steps.cpp`, as Start would); the stage viewer holds its steps | done |
| CRASH REPORTS | **OFF** until asked, SEND | `reports/consent.txt` (not gubsy's) | the bug reports' consent (docs/release.md, "Bug reports"); shown only in a build with a report address | done |
| REPORT A BUG | | | accept opens the report panel (a note, no picture); shown only in a build with a report address | done |
| CHEATS | OPEN | the game's save | accept opens the CHEATS page (below); dim from the title (IN A GAME) and over a fight's pause (NOT IN A FIGHT) | done |
| DEFAULT | | | (not the CHEATS) | done |

### CHEATS (from GAME's CHEATS row)

Playtest 2026-11-13: the owner, starved of money, and Plus Tech's tune prices brutal past the knee at 98. A page of the
rows' kind under GAME (the column has no room for an eighth button above the footer, and the buttons' letters have no
H), so the column keeps GAME lit; back returns to GAME's CHEATS row. `src/screens/pc/pc_cheats.cpp`.

They are game state, not settings: `Story::cheats` (`CheatRules`, `src/story/cheat_rules.hpp`) in the world, hashed with
the story, saved in the save's `(pc-rules ...)` form (docs/saves.md), so a save remembers how it was played. A new game
starts retail (`story_boot` is untouched: `Story{}`'s defaults); a save from before the form loads as retail. Changed only
here, from the front end between fights (the row is dim over the pause); the fight reads MONEY WON once, when it builds
its result. Any cheat used or rule off RETAIL marks the game CHEATED for good (`CheatRules::cheated`), and bug reports
carry the rules in `context.game.rules` ("retail", or "cheated, infinite money, tune 1/100, shop RETAIL, money won x1").

| row | values | what it does |
|---|---|---|
| INFINITE MONEY | **OFF**, ON | the money held at its cap, 99,999,999 units (999,999,990 NY), each front end update outside a played fight (`cheat_money_hold`, `src/front_end_steps.cpp`) and the moment it is switched on; shops still take their price, the next update puts it back |
| ADD MONEY | +1,000,000 NY | accept adds 100,000 units through `money_add` (held to the cap); repeat as often as wanted; dim at the cap or with INFINITE MONEY |
| UNLOCK ALL MUSIC | owned/78 | songs 0..77 not owned become bought (`music_owned` 1, not heard) |
| PAINTS AND STICKERS | UNLOCK, ALL OWNED | every colour and sticker the part tables have gets its owned bit |
| ALL GARAGES | open/10 | `Garages::unlocked` all ten bits |
| SKIP DAY | DAY N | the night's work (`night_step`): Sonic Amp's stock, `day_advance`, the line-up refilled with drawn opponents |
| TUNE PRICES | **RETAIL**, 1/10, 1/20, 1/100, 1/200, FREE | Plus Tech's tune price (modules' `part_tune_price`, `\|price(to) - price(from)\|` with both legs across 0, and a Chip's difference) divided and rounded to whole units of 10 NY, at least 1 unit; FREE is 0. The sign is kept, so a Chip tuned down refunds the divided price (`cheat_tune_price`) |
| SHOP PRICES | **RETAIL**, 1/2, 1/4 | Wild Machines' modules, paints, options, Chips and used Scoobees, the dealers' machines and Sonic Amp's songs, bought and sold alike (selling stays half of the divided price, so nothing pays) (`cheat_shop_price`) |
| MONEY WON | **x1**, x2, x5, x10 | a fight's prize multiplied in `battle_result_build`, held to the cap; repairs stay retail |
| THIS GAME | RETAIL, CHEATED | shown dim: whether this game has used any of the above |

The price rows step with left and right (the wheel, a click on a half); the cheats are buttons (accept or a click on
the row, as many times as wanted). Not here yet: WIN THE CURRENT TOURNAMENT (docs/backlog.md), the selling rule and the
balance-shaping switches (the RETAIL BUGS page).

Prices with TUNE PRICES (the Pistol/HM15, price curve 1500 + 21000 at 98, + 700000 at 99, in units of 10 NY):

| tune | RETAIL | 1/10 | 1/20 | 1/100 | 1/200 | FREE |
|---|---|---|---|---|---|---|
| 0 to Heavy 98 | 210,000 NY | 21,000 | 10,500 | 2,100 | 1,050 | 0 |
| 0 to Heavy 99 | 7,000,000 NY | 700,000 | 350,000 | 70,000 | 35,000 | 0 |
| Heavy 98 to 99 | 6,790,000 NY | 679,000 | 339,500 | 67,900 | 33,950 | 0 |
| Heavy 99 to Light 99 | 14,000,000 NY | 1,400,000 | 700,000 | 140,000 | 70,000 | 0 |

### Accessibility

No page of its own (the page column is full); what there is sits with its kind: BRIGHTNESS, FLASHES, CAMERA SHAKE and FIELD OF VIEW (PICTURE),
SOFT FOCUS off for anyone the haze bothers (PICTURE), HUD SIZE (SCREEN), MOUSE UP DOWN, the dead zones and STICK
CALIBRATION (INPUT), every control and axis
rebindable per player (BINDINGS), VIBRATION STRENGTH (INPUT), and the conversations' skip (GAME, and the X chord in a
conversation).

Menu size is not offered. The menus are the original's 640x480 layouts scaled to fill the window's height at any
window size, so they are already as large as the window allows: a larger menu would push the layout's top and bottom
off the screen, and a smaller one only shrinks it into the middle. Their pieces are laid out against each other (the
frames, the bars, the help line's clip, the cursors, the mouse's hit boxes), so scaling pieces apart, as HUD SIZE does
the HUD's corners, would pull them off their frames. HUD WIDTH (SCREEN) pulls their edge pieces in toward the middle.

Left out: captions (the original's text is all on screen), hold or
toggle (no held actions to toggle). FIELD OF VIEW (PICTURE) widens the fight's camera for anyone its 52 degrees make sick;
the aim and lock-on keep retail's.

### Left out from Mans In My House

Quality presets and the per-effect detail rows (one look, the original's), language (English), the crosshair, aim
assist, bots, online rows (later, with the lobby), sound direction and path rate (the original's mix is stereo).
Show frame rate is on SCREEN.

## From the pause

Every fight's pause (a tournament's, the test drive's, free play's and the lessons', VS's) has the port's buttons
under it in PC CONFIG's look: OPTIONS, PC CONFIG, MUSIC and REPORT A BUG (in a build with a report address)
(`src/screens/pc/pause_settings.*`). Free play's and VS's menus run their cursor on from Continue and Quit through
them; a tournament's pause, which has no menu in retail, gets a cursor of its own over them, two a row so all four
clear the entrance's letterbox (A or a click opens one, Start still ends the pause). BACK still opens REPORT A BUG at
once. MUSIC opens the MUSIC panel (below, "Music") the same way.

OPTIONS and PC CONFIG open PC CONFIG's pages over the paused fight: the same column, rows, header and help line, the
fight dimmed behind instead of the backdrop and no footer, and OPTIONS first in the column. EXIT, or back on it,
returns to the pause on the item it was opened from. Everything applies at once as on the screen and is kept the
same way; SAFE FRAME, which a fight takes at its start, says NEXT FIGHT there. OPTIONS holds the original's options
a fight can take, kept in the machine's options and saved with the game as the options screen's are:

| row | values | what it does |
|---|---|---|
| VIBRATION | **ON**, OFF | the options' vibration box (`MachineOptions::vibration`, 0 on) |
| BGM VOLUME | 0..**255**, by 15 | the sound config's BGM volume, applied at once (`set_music_level`) |
| SE VOLUME | 0..**225**..255, by 15 | the sound config's SE volume (`set_sound_level`) |
| SCREEN LEFT RIGHT | -32..**0**..30 | the screen adjust across, live as on the adjust screen |
| SCREEN UP DOWN | -16..**0**..14 | the screen adjust down |
| BUTTON CONFIG | NOT IN A FIGHT | shown dim: the buttons are the world's (`profile.options.buttons`), part of the fight's record, so they change only from the options outside a fight |

The fight stands still under it: the world doesn't step while paused and nothing here writes it, so the fight's
inputs, its replay, a bug report's record and `world_hash` are as they would be without the panel (checked with
`--hash 1`, docs/screens.md). While the panel is up the pause reads no pad or pointer, Start can't end the pause and
Escape is Back as in the menus (it stays Back until let go after the panel closes, so it can't come back as Start);
the entrance's letterbox and a missing pad's box wait under it until it closes. In VS any entered seat's pad drives
it, as the pause's menu.

## Music

The MUSIC panel (`src/screens/pc/music_panel*.cpp`, the fights' side in `src/screens/shops/fight_music.cpp`, docs/audio.md
"Fight music") is PC CONFIG's frame with a column of its own: MUSIC, SONGS (the playlist editor; the option buttons'
letters have no L or Y, so it can't say PLAYLIST) and EXIT. It opens from the pause's MUSIC item over any paused fight
(EXIT goes back to the pause) and in the menus from SOUND's MUSIC row (EXIT goes back to the row), so the playlists can
be built from anywhere PC CONFIG opens, not only the media player's shop.

MUSIC's rows:

| row | values | key | what it does |
|---|---|---|---|
| NOW PLAYING | the song's title | | left is BACK (the song before this one again, this one from its start when there is none), right or A is NEXT (the mode's next song at once); the gap after it as after any song. Dim outside a fight and in a fight with a track of its own (below) |
| PICK A SONG | every owned song | | left and right go through the owned songs, A (or a click on the label) plays it: in a fight at once, and ONE SONG REPEAT's song from then; in the menus as the media player's preview, looped, the screen's music back on leaving the panel |
| MODE | PLAYLIST, ALL SONGS SHUFFLE, ONE SONG REPEAT | `audio.fight_music` | how a fight chooses its songs (docs/audio.md "Fight music"). Until it is set (`default` in the file) it is **ALL SONGS SHUFFLE once more than 12 songs are owned, else PLAYLIST**: a new game owns one song and plays retail's list; a player who has bought more than a playlist holds hears them all without building one |
| EDIT PLAYLIST | OPEN | | the SONGS page |

SONGS edits the media player's playlists in place (retail's state 0x21 screen keeps working as it did):

| row | values | what it does |
|---|---|---|
| PLAYLIST | LIST 1..8, or its name | the playlist the fights play (`playlist_selected`) |
| ORDER | IN ORDER, RANDOM | `playlist_random` |
| SONG 1..12 | EMPTY, every owned song | the playlist's songs from the top, as the media player's editor keeps them (it adds at the end and takes off the last): EMPTY takes a song off and the ones under it move up, the first empty slot adds one, the rest are dim. A plays the song as PICK A SONG does |

Every change makes the battle's copy again at once (`music_playlist_compact`), where retail made it only when the
media screen closed or a save loaded. Over a fight the playlists edited are a copy the fight plays from, written to
the world when the fight ends (`fight_lists_commit` in `battle_screen_destroy`): the world's playlists are part of
`world_hash`, and the world changes only by its step during a fight. MODE and the songs touch nothing the world holds
either, so a fight with the panel used plays, records and hashes as one without it (checked with `--hash 1`).

The song controls work in a fight that plays the list: a tournament's and VS. The four tournaments with a theme of
their own (14, 31, 48 and 49, `battle_music_update`'s, docs/audio.md) keep it, and so do the test drive
and free play with their fixed `12SENTOU.WAV`; their NOW PLAYING and PICK A SONG are dim and say why. MODE and SONGS
still change what the next fight plays.

## The look

PC CONFIG is built only from the original's art, cut so new words read as the original's (the owner's direction:
new menus mustn't look out of place). `src/screens/pc_settings_art.*`:

- The frame is the sound config's: backdrop 2514, header 2519 and footer 2520 (`sound_config_sprites`' rows and
  layers), the level meter and the now-playing strip, the help line at (0, 0x48) scrolling in the system font, the
  half-second fades, the options' music.
- The title: 2519's SOUND covered with its band's own blue (texels 604..620) and PC set before CONFIG, the P cut
  from the options frame's OPTION (2106) and the C from CONFIG, each in bands that follow the italic slant so no
  neighbour's edge comes along.
- The page column: 192x40 option buttons. The words are cut letter by letter from the original's buttons (SOUND
  CONFIG, SCREEN ADJUST, EXIT, BGM TEST, BGM VOLUME, KEEP SV, SE TEST, SE VOLUME, DEFAULT and their lit twins): the
  letters' column runs are found by scanning the button PNGs for the yellow text, baselines matched, and each
  button is laid out in columns that never overlap (caps, filler, letters) so the lit one pulses as one piece.
  Where a button's letters touch (DEFAULT, VIBRATION, BUTTON CONFIG, OPTION, LOAD) it can't be cut and is
  skipped, which leaves twenty letters and no H, L, Q, W, Y or Z: hence SCREEN, PICTURE and INPUT rather than
  DISPLAY, GRAPHICS and CONTROLS. The options screen's
  PC CONFIG item is the same.
- The rows: the key config's bars (1597 tinted grey, 1598 lit and pulsing on the current row) 3-sliced to width, its
  arrows (1588..1591), its label font (the white condensed labels of `key_config_tex`, cut the same way, white text
  found by brightness) for labels and words, the number font the sound config uses (font 3) for digits and signs,
  the multiplication sign cut from SHOOTDOWN×, the sound config's volume bar (2509) and fill (0x9DB). The label
  font has no Q or Z: Q is O with R's leg, Z is T's bar, N's diagonal mirrored and L's foot.
- Sounds: the cursor's (0, 3), accept (0, 5), the box sound (0, 0xC) when BINDINGS starts listening.

A new row is a line in `src/screens/pc/pc_settings_rows.cpp` (its label and help, its value and its step); keep labels
in capitals and to the letters there are.

The cook finds the glyphs: it scans the label, number and button PNGs under `data/ui/images` for the text's column
runs and writes each letter's cut to `pc_art.sexp` in the core pack (`src/cook/cook_pc_art.cpp`, the PNGs its
sources); the game reads the cuts and draws them from the cooked sprites.

## Input on the screen

Pad, keyboard and mouse as on the original's screens (docs/screens.md, "Input"): up and down move in the column or
the rows, accept or right goes into a page's rows, left and right step a value (held, they repeat), back returns
to the column and, there, moves to EXIT. The mouse: hovering a button or a row moves the cursor there, a click on a
button opens its page, a click on a row's value steps it (left half down, right half up), the wheel over the
current row steps it, a right click is back. In BINDINGS the keys and buttons go to PC CONFIG first while it
listens (`front_end_event`, called before the pads in main).
