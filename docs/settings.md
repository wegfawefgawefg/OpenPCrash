# Settings

The PC's own settings: what the Xbox never had to ask (window, resolution, v-sync, frame rate, volumes over the
whole mix, keys and mouse) and a few comforts. The original's own options (vibration, BGM and SE volume, button
config, screen adjust) stay where they are, on the original's screens, saved with the game.

They live on one screen of the port's own, PC CONFIG (state 0xA0), reached from the options screen's PC CONFIG
item, so from the title and from the system menu alike. The list follows Mans In My House's settings
(`src/menu/settings_rows.cpp` there) where they fit a 2002 console port.

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
| FRAME LIMIT | **OFF**, 30, 60, 75, 90, 120, 144, 165, 240 | `video.frame_cap` | main waits out the rest of each frame; the game still steps at 60 | done |
| SHOW FRAME RATE | **OFF**, ON | `video.show_fps` | "60 FPS" in the label font on a dim plate in the top right, over everything but the fade, counted twice a second (`pc_fps_draw`) | done |
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
| DEFAULT | | | | done |

The original's two volumes (BGM 0..255, SE 0..255, `GameOptions`) stay in SOUND CONFIG and in the save.

### Controls (pages INPUT and BINDINGS)

| row | values | where kept | what it does | status |
|---|---|---|---|---|
| MOUSE SPEED | 10..**100**..500, by 10 | input settings profile `mouse_sensitivity` (/100) | the upper body's turn per count (docs/input.md, "Mouse aim") | done |
| MOUSE UP DOWN | **NORMAL**, REVERSE | input settings profile `mouse_invert_y` | | done |
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
| PAUSE IN BACKGROUND | **ON**, OFF | `phantomcrash.game.focus_pause` | a fight opens its pause while the window is away (main.cpp, as Start would); the stage viewer holds its steps | done |
| DEFAULT | | | | done |

### Accessibility

No page of its own (the page column is full); what there is sits with its kind: BRIGHTNESS, FLASHES and CAMERA SHAKE (PICTURE),
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
toggle (no held actions to toggle), field of view (the original's camera is part of the play).

### Left out from Mans In My House

Quality presets and the per-effect detail rows (one look, the original's), language (English), the crosshair, aim
assist, bots, online rows (later, with the lobby), sound direction and path rate (the original's mix is stereo).
Show frame rate is on SCREEN.

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
