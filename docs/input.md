# Input

How the PC's keyboard, mouse and pads reach the game. Decomp names and retail addresses throughout; the sources are
`src/hand/genkilib_pad_o2.c`, `src/o2/genkilib_0de830.c`, `data/hand/000df370.cpp` (GenkiLib's pads),
`src/hand/mech_part_assets.c` (`sv_control_route`, `actor_triggers_update`), `src/hand/loadout_assets.c` (bindings in
`actor_init`), `src/od/u_07d2b0.c` (`actor_camera_input`, the camera follows) in `../phantom-crash-decomp`.

## The layers

1. Devices to pad records, in `src/input/` (SDL and gubsy). Every device drives one virtual Xbox pad per player, read
   once before every 60 Hz step into a `PadRecord` exactly as GenkiLib's `pads_read` (0xDFEB0) leaves its 0x88-byte
   record: dead zones, the analog buttons' bits, edges, auto-repeat, the sticks' directions.
2. The pad record and the mouse aim are the world's input (`Inputs::pads`, `Inputs::aims`, `src/world/inputs.hpp`).
3. In the world, each human fighter turns its player's pad record into the 20-byte `ControlRecord`
   (`src/world/control_record.hpp`) at the start of its step with `control_route` (`src/world/control_route.cpp`,
   `sv_control_route` 0x7742D). The AI writes the same record; replays store it. Nothing after that knows which device,
   or whether a person, made it.

The route runs inside the world because it reads the fighter: `airborne` and `control_type` pick which stick is which
axis, the stick double flick is a state machine in the Actor (+0x2538, +0x253C, cleared at spawn), the twist axis is
kept from the step before while airborne under Standard, and a fighter in its door, in a scene, at a VS round's end or
dead gets a zero record without touching any of that. Building the record outside the world would mean reading all of
that back out of it.

Files: `world/control_record.hpp` (the record), `world/pad_record.hpp` (the pad record, `MouseAim`),
`world/control_route.{hpp,cpp}` (the route, the button config), `input/pad_read.{hpp,cpp}` (GenkiLib's read, no
SDL), `input/menu_input.{hpp,cpp}` (what screens ask, no SDL), `input/devices.{hpp,cpp}` (SDL and gubsy to pad
records), `input/bindings.{hpp,cpp}` (gubsy schema, profiles, the mouse setting), `input/device_codes.{hpp,cpp}`
(gubsy button codes to SDL codes, for latching).

## The control record

Retail: Actor +0x2F5C `float control_axis[4]`, +0x2F6C `int control_buttons`. Port: four Q16.16 `Fx` and a
`uint32_t`, still 20 bytes.

| field | decomp | meaning (retail, per 30 Hz update) | pad | keyboard and mouse |
|---|---|---|---|---|
| `control_axis[0]` | `axis_forward` | forward/back; walk and run by lean (run past 0.7 held 30 ticks) | left stick y | W / S (full lean, Ctrl held: 0.6) |
| `control_axis[1]` | `axis_leg_turn` | heading += 2 turn_deg pi/180 axis (`actor_turn_by_stick` 0x8E1D7, axis clamped to +-1) | Preferred: left stick x. Standard: right stick x; in the air the left x unless the right x is past 0.001 | Preferred: A / D. Standard: mouse x |
| `control_axis[2]` | `axis_pitch` | camera pitch -= axis x 3 degrees (0.05235988) | right stick y, negated under View Change Normal | mouse y |
| `control_axis[3]` | `axis_twist` | Preferred: aim_yaw += 2 turn_deg pi/180 axis, clamped +-45 degrees. Standard: the strafe, aim_yaw stays 0 | Preferred: right stick x. Standard: left stick x (ground; kept in the air) | Preferred: mouse x. Standard: A / D |
| `control_buttons` | | see below | | |

Every axis is the stick's short (after `pads_read`'s dead zone, below) over 32767, then a dead zone of 0.01 with the
rest rescaled: `(|s| / 32767 - 0.01) / 0.99`. The port does it in integers, `(100 |s| - 32767) * 65536 / (99 *
32767)` rounded to nearest with the sign kept, so the threshold test is exact and left and right are mirror images.
The airborne twist that carries over goes through the same dead zone again every step, as retail's float does
(it decays toward 0; nothing reads it in the air).

Buttons, each from a binding mask over the pad word (`ControlBindings`, built at spawn from the player's button config
`KeyBlock`; held = level, pressed = the read it went down):

| bit | name | from |
|---|---|---|
| 0x1 | `control_dash` | button1 (left thumb, fixed 0x40) held |
| 0x2 | `control_jump_held` | jump binding held |
| 0x4 | `control_jump_pressed` | jump binding pressed (edge). The AI sends it as a level |
| 0x8 | `control_camouflage` | camo binding pressed (edge) |
| 0x10 | `control_step_right` | dodge_a (RIGHT STEP) held, or a Standard double flick to the right |
| 0x20 | `control_step_left` | dodge_b (LEFT STEP) held, or a Standard double flick to the left |
| 0x40..0x200 | `control_fire_arm_a` .. `control_fire_back_b` | the four fire bindings held (`mount_fire_bits` 0x232550) |
| 0x400 | `control_view_near` | d-pad up pressed (binding_dpad_up, fixed 1) |
| 0x800 | `control_view_far` | d-pad down pressed (fixed 2) |
| 0x1000 | `control_view_right` | d-pad right pressed (pad bit 8). gameplay.md says left; the code tests 8 |
| 0x2000 | `control_view_left` | d-pad left pressed (pad bit 4) |
| 0x4000 | `control_recentre` | right thumb held |

The button config (GameState +0x10FDC per slot) is 8 key indices into `D_001DD470` = A, B, X, Y, left trigger, right
trigger, white, black; the defaults `D_001DD490` = {5, 4, 1, 3, 7, 6, 0, 2}: arm A right trigger, arm B left trigger,
back A on B, back B on Y, right step black (0x1000), left step white (0x2000), jump A, camouflage X. The AI and the
free play lessons other than lesson 3 always get the defaults. The key config screen's rows say so (`key_config_sprites_2`
rows 21 and 22: RIGHT STEP 1631 on `keys[4]`, LEFT STEP 1625 on `keys[5]`, and the help lines), and so does the body:
0x10 subtracts from body x, and body -x is the mech's right (its forward is body -z in the original's frame, +z in
the port's; the mirror keeps x). The decomp's gameplay.md and ai.md call 0x10 the left step; that is a naming slip,
not what retail does. The PC profiles put white, the left step, on the left shoulder and Q, and black on the right
shoulder and E (docs/bugs.md, "Port decisions": until 2026-11-12 the port had the names crossed and Q stepped right).

## The pad record (`pads_read`)

Per read, per player:
- Buttons: the XInput digital byte (d-pad 1 2 4 8, start 0x10, back 0x20, thumbs 0x40 0x80) and the eight analog
  buttons (A B X Y black white LT RT), each 0..255, 30 and under read as 0, and down in the word (bits 8..15) from 63,
  a quarter of the 0xFF limit (0xDECE0). Modern pads' face and shoulder buttons are digital: 255 or 0. Triggers are
  SDL's 0..32767 scaled to 0..255.
- Sticks: through the pad's calibration when one is stored (below), clamped to +-0x7FFF and given a dead zone per
  axis, the rest rescaled to full range (a square dead zone, which keeps forward and turn apart). The original's is
  0x4000, half the travel, on both sticks, and that is the default; each stick has the player's own (PC CONFIG's LEFT
  and RIGHT DEAD ZONE, `phantomcrash.input.left_dead_zone` and `right_dead_zone`, 5..50%, 50 exactly 0x4000), and
  OUTER DEAD ZONE (`outer_dead_zone`, 0..30%, 0 as retail) makes the far end of the travel read as full: what is left
  between the two zones is spread over the whole range. SDL's y is flipped to XInput's up-positive.
- Edges: `rising = (prev ^ cur) & cur`, `falling = (prev ^ cur) & prev` (0xDED50).
- Auto-repeat (0xDEE20): a fresh press repeats at once; a hold repeats after `frame_cadence * 20 / 60` reads (a third
  of a second) and then every 2 reads. Each stick's directions are buttons too (past 0x3F, 0xDEF90) with their own
  edges and a repeat of 1 or 2 reads by how far it leans (`(0x6000 - lean/2) / 12289 + 1`, floored at 1). Retail sums
  the lean in a short that wraps on a hard diagonal and slows it to 3 or 4; the port sums it whole (docs/bugs.md).
- A pad not read (a fade, a lost-pad pause, a player outside the input masks) is cleared to neutral (0xDEC80), so a
  button held through a fade comes back as a new press. Retail does the same.
- Several devices on one player merge into one pad: buttons ORed, analog levels by the strongest, each stick from the
  first device whose stick is off centre (as `pads_stick_read` 0xDEA30 takes the first pad in the mask). Retail keeps
  one record per pad and tests the pads in a player's mask in turn; the only difference is two devices pressing the
  same button within one read.

## Calibration

PC CONFIG's STICK CALIBRATION (INPUT page; `src/screens/pc/pc_settings_calibrate.cpp`, `src/input/pad_calibration.*`)
measures one pad: its sticks let go (each axis's rest, the average of the last 16 reads, and each trigger's rest, the
highest of them), then turned round their edges and the triggers pulled (each axis's furthest reach either way, each
trigger's furthest pull). A press of A keeps it once every stick axis has reached 0x3000 from its rest both ways; a
trigger that wasn't pulled 0x3000 past its rest keeps the plain range. The kept reach is 97% of what was reached (and a
trigger's rest moved 3% in), so the edge reads as full without straining for it.

It is kept per pad in the install's gubsy settings, `phantomcrash.input.calibration.<key>`: sixteen numbers, each
stick axis's rest, low and high, then each trigger's rest and full pull, SDL's raw units. The key is the pad's kind (its
SDL GUID) and its serial number where SDL reads one (`<GUID>.serial.<serial>`: Xbox, PlayStation and Switch pads over
SDL's HIDAPI drivers, most pads on Windows), so two pads of one kind keep their own. A pad with no serial is kept under
its path (`<GUID>.path.<path>`) and under its kind alone (`<GUID>`), since a path changes when it is plugged in
elsewhere; it reads its path's first, then its kind's. Letters, digits, dots, dashes and underscores are kept in the
key, the rest written `_`. Y on the panel (or Delete) takes out every key the pad reads: it reads as it comes again.

The reads (`devices.cpp`) map each raw axis from its rest to 0 and its reach either way to +-32767, and each trigger
from its rest to 0 and its pull to 32767, before the dead zones and the trigger's own 30 of 255. With nothing stored
the raw values go through untouched. Like the dead zones it happens before the pad record is built: it changes what a
player sends, never how the world steps.

The panel draws each stick's travel as the radar's ring (0x570), the dead zone as the tracking box's four corners
(0x561, mirrored and turned as `hud_draw_tracking_box` sets them), the stick as the lock marks' four corners (0x56E) 9 px
round the tracking dot (0x56b): green from its rest while measuring, with the reach traced in 32 directions in small
dots as it turns; once kept, yellow through the calibration with the raw reading a grey dot, which should touch the
ring at a full lean. The triggers fill the
sound config's level bars. `PHANTOMCRASH_PAD_SPIN` (below) drives it headless.

## The upper body from the stick (retail)

All in `actor_camera_input` 0x7D2B0, per 30 Hz update, not in beginner mode. Both are rate axes: the stick sets a
speed, never an angle. Linear in the axis after the two dead zones above (0x4000 per stick axis in `pads_read`, then
0.01 in `sv_control_route`); no acceleration, no curve, no smoothing of the axis itself.
- Twist, Preferred only (control type bit 0 clear): `aim_yaw += (video_rate_nominal / frame_cadence) * turn_deg *
  pi/180 * control_axis[3]`, then clamped to +-0.7853982 (45 degrees). In battle the factor is 2: up to 2 turn_deg
  degrees an update at full stick, the same rate the legs turn at (`actor_turn_by_stick` 0x8E1D7 uses the same
  expression with `control_axis[1]`, which it clamps to +-1; the twist axis is not clamped, only the angle).
  `turn_deg` is the fighter's turn rate (gameplay.md: 0.1..90 degrees a tick). Under Standard `aim_yaw` stays 0 and
  the right stick turns the legs instead.
- Pitch: `camera.pitch -= control_axis[2] * 0.05235988` (3 degrees an update at full, a constant, not scaled by
  turn_deg). The limit moves with the stick: `pitch_stick = 0.1 * |axis| + 0.9 * pitch_stick`, limit `=
  0.3926991 * pitch_stick + 0.7853982`, so 45 degrees at rest growing toward 67.5 while the stick is held, and the
  pitch is clamped to it every update (it sinks back to 45 when the stick is let go).
- The view follows both: `camera_yaw_follow` 0x7FBDE closes 30% of the way to heading + aim_yaw each update;
  `camera_pitch_follow` 0x7FCF2 closes 30% of the way to the pitch clamped to +-45 degrees. The reticle is screen
  centre, so the aim is the view, a few updates behind the stick.
- Recentre (right thumb, 0x4000): pitch and aim_yaw ease to 0 over 8 updates and the legs take the twist.
- Beginner mode is separate: the pitch springs back, and the right stick past 0.5 snap-turns the legs 90 degrees at
  0.8 of the turn rate.

## Mouse aim

Mouse aim always turns at the mech's turn rate (owner, 2026-11-11), in every mode; there is no faster setting. The
mouse aims the upper body: its x drives the axis the right stick's x drives (twist under Preferred, leg turn under
Standard), its y the pitch, up as up whatever the View Change setting (gubsy's invert y flips it).

- Mouse counts move a target angle at once (0.000873 rad, about 0.05 degrees, a count at sensitivity 1, gubsy's "Mouse
  Sensitivity"). The target is kept per seat (`InputDevices::owed_twist`, `owed_pitch`: what is left to turn) until it
  is reached.
- Each step the input layer turns what is left into this step's axis values at the fighter's own rates (`AimRates`,
  filled from the seat's fighter by `seat_aim_rates`: twist or leg turn `turn_deg * pi/180` a step, pitch `0.05235988 /
  2` a step), held to -1..1, so the mech turns exactly as a full stick would and stops exactly on the target: the last
  step's axis is the fraction left. What goes to the world is the axis in Q16.16, and the target keeps what that axis
  doesn't turn.
- `control_route` adds the mouse share to the stick axes and holds the sum to -1..1 (`MouseAim` has no flag: there is no
  uncapped path). The legs' turn (`actor_turn_by_stick`) clamps to 1 as retail.
- Under Preferred the target stays within 45 degrees of the legs, retail's twist clamp, so it never asks for an aim the
  mech can't take. The pitch target isn't held to the pitch limit; past it the target is used up while the pitch stays
  clamped.
- The camera stays with the mech as in the original: it follows the aim, not the mouse.
- The HUD draws where the mouse asked to aim: a dim copy of the tracking dot (0x56B at alpha 0x50) at the reticle plus
  the angle left, through the world's projection; hidden once the target is reached, so the real reticle sweeps onto
  it (`hud_reticle.cpp`).

Checked headless (2026-11-11, `PHANTOMCRASH_MOUSE_MOVE=30:2000:0`, build 0, turn 1.971 degrees a step, Standard): the
heading turned 1.97 degrees every step for 50 steps and the 51st landed on 71.85, the target to the hundredth (171.89 less
2000 counts x 0.000873 rad).

The world uses the same per-step pitch constant (0.05235988 / 2, `src/battle/battle_camera.cpp`) and twist rate
(the fighter's `turn_step`), so the mouse lands exactly.

## Seats

Who plays at this machine and on what (`src/input/seats.{hpp,cpp}`, after Mans In My House's `local_players.cpp`).
A seat is a match seat 0..3: gubsy's lobby player of the same number, `Inputs::pads[seat]` and `Inputs::aims[seat]`, the
fighter whose `controller` is that seat, and in VS its panel, view, HUD and win count.

- Alone (the front end, the campaign, the viewer): seat 0 plays on the keyboard, the mouse and every pad at once
  (`seats_alone`).
- Split (VS): each seat plays on what it joined with, the keyboard and mouse together or one pad, in any order: a pad as
  P1 and the keyboard as P2, the keyboard as P1 and a pad as P2, two to four pads. On the VS join page an accept (Enter,
  Space, a pad's A or Start) from a device nobody holds takes the first free panel (`seat_join`); that player's cancel
  (Escape, Backspace, B, Back) leaves (`seat_leave`), freeing the panel and the device, so they can come back on another
  device. A pad pulled on the join page drops its player, as retail. The press a device joins with is ignored until it
  is let go (`VsPanel::wait_release`), so joining never also starts the match.
- `seats_sync` runs every update before the pads are read (`main.cpp`, both loops and the VS round): four lobby players
  while split, so lobby player i is seat i, one alone; each player's device assignments toggled to match the seats (a
  hot-plugged pad that gubsy hands to its primary player is taken back on its `SDL_EVENT_GAMEPAD_ADDED`, in the same
  pump, so no read ever sees it on player 1; the reads came after a `seats_sync` before this too, and the pads' presses
  are kept by device id, so a press in that update never reached seat 0); a new player takes player 1's
  settings profile and its own binds profile (id 1 + seat) once PC CONFIG's BINDINGS has made one, else player 1's
  ("Keyboard, mouse and pad" binds both kinds of device).
- The mouse belongs to the seat holding the keyboard (`mouse_seat`); `read_pads` aims only for a player with the mouse
  device, so no other seat reads it. Relative mouse mode is on through a battle (the cursor is captured) and off in menus.
- `seat_aim_rates` fills each seat's `AimRates` from the fighter whose `controller` is that seat (zero for a seat with
  none). A fighter whose controller is outside 0..3 reads a pad at rest (`step_world`), never another seat's.
- At spawn (`battle_spawn`) a human's seat is its roster entry's `player`: that is its `controller`, its gate is
  `BattleState::player_gate[seat]`, and its keys and control type come from `World::player_buttons[seat]` (the seat's
  button config; player 1's is the save's). VS writes each entrant's (a stock machine plays with the default config,
  `player_config_reset`, a kept one with the config KEEP SV kept with it, `player_config_store` of `hdr.stats`); VS
  fighters start in view 1 (`camera_init`; outside VS a human starts in the view it left its last fight in, "Views").
- The main view, the HUD's viewer and the stage ambience follow seat 0's fighter (or `--follow`); split screen draws each
  seat's own camera (`interpolate_seat_camera`).

## Control type

A human's control type is its button config's (`button_control_type`, `player_config_flags_update` 0x27CDA), as retail:
the default config gives 3, Standard and View Change Normal. Until 2026-11-11 every human ran 0 (Preferred, Reverse).

What Standard means with keyboard and mouse: W / S move, A / D strafe (they are the left stick's x, Standard's strafe),
and the mouse's x turns the whole mech, the legs, at the mech's own turn rate, exactly as the right stick's x does
under Standard; the mouse's y pitches, up as up. There is no twist under Standard (`aim_yaw` stays 0), so the reticle
is always straight ahead of the legs. Under Preferred the mouse's x twists the upper body within 45 degrees and A / D
turn the legs. The original's BUTTON CONFIG screen (key config, state 0x10) switches it.

Checked 2026-11-11: `docs/sixty.md`'s movement scripts (`captures/movement/legs.txt`, `jump_held.txt`,
`walk_run_jump.txt` with builds 0, 37 and 72 on Shinjuku, and the held jump on the test arena) with `--trace 1`, before
and after: `jump_held`, `walk_run_jump` and the arena jump print the same trace line for line. `legs.txt` turns with the
left stick's x (0.3), which under Standard strafes instead, so its trace differs from step 91 (the heading stays 171.89
and the mech slides sideways); the same script with the turn moved to the right stick, Standard's turn, prints the old
Preferred trace line for line for all three builds. Nothing else changes.

## Views

The d-pad changes the camera (`actor_camera_input` 0x7D2B0, decomp docs/port/cameras.md "Input"; the world's
`camera_view_input`, src/battle/camera_views.cpp), from the control record's view bits, so replays and every seat get
it the same way:

| control bit | pad | keyboard and mouse | what it does |
|---|---|---|---|
| 0x400 `control_view_near` | d-pad up | 3, wheel away | to view 0, the cockpit, blended over 20 updates (40 steps); one view only (not split VS) |
| 0x800 `control_view_far` | d-pad down | 4, wheel toward | to view 1, the chase view, blended the same way |
| 0x1000 `control_view_right` | d-pad right | 2, Tab | a cut to the next outside view: from 0 or 1 to 2, then 2, 3, 4, 5, 2 |
| 0x2000 `control_view_left` | d-pad left | 1 | a cut the other way: from 0 or 1 to 5, then 5, 4, 3, 2, 5 |

Views: 0 cockpit, 1 chase (the default), 2 side, 3 side high, 4 front, 5 the stage's rails. Left and right do nothing
in beginner mode or while a near/far blend runs; up and down do nothing while one runs. The view is the aim's camera:
the HUD's screen points, the lock boxes and the tracking point are taken through it, so in views 2..5 (no reticle,
radar or lock box drawn, as retail) the locks behave as retail's do there. A human outside VS starts each fight in
the view it left the last one in (`camera_view_save` 0x7E837 into `GameOptions::camera_view`, saved); VS starts
everyone in view 1; the lessons cut back to view 1 at their scenes (`camera_view_cut`).

In the cockpit (src/view/cockpit_view.hpp) the own body, its sticker, shadow and lamp trails are left out (the
weapons stay), the HUD swaps the gauge set for the instruments and the readout's panels, and a hit shakes the view.
The shake is the fighter's own look-at in the world, as retail's, so it jitters the tracking point, the lock boxes and
the senses too; PC CONFIG's CAMERA SHAKE scales only the drawn camera and the HUD's points with it, never the aim. A hit on a human in a fight also rumbles that seat's own pads (below).

## Pad rumble

`actor_settle_damage` 0x766DE starts a table on the hit human's pad (`rumble_start`); the port's world raises
`event_hit_rumble` (slot, damage) in the step's events, and presentation plays it (src/input/pad_rumble.cpp) on the
pads of the seat that fighter plays, through `SDL_RumbleGamepad`: three rows of 20, 10 and 5 updates at (65000, 45000),
(50000, 35000), (30000, 25000) on the low and high motors, each level times the damage clamped 300..1000 over 1000,
the durations halved under 500 damage, one update off between rows (retail plays only the first row: docs/bugs.md).
Only in a fight (battle phase 0: not free play, demos, replays or the ending). OPTION's VIBRATION turns it off (its
NO is `rumble_start`'s flags 1); PC CONFIG's VIBRATION STRENGTH scales it per seat (gubsy's input profile).

It is sent as Mans In My House sends it (`rumble_seat` in its local_players.cpp): `SDL_RumbleGamepad` on every pad the
seat's gubsy player holds. A row's levels are sent every step for 100 ms; SDL passes only a change of level on to the pad
and otherwise just moves the end, so a pad gets one report a row and a game that stops stepping goes quiet by itself. A
pad with trigger motors (SDL's `SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN`: Xbox One and Series pads) also gets the hit
in its triggers through `SDL_RumbleGamepadTriggers`, the low and high motors' levels at half (a port addition; retail's
Duke has no trigger motors). Retail's `rumble_halt` 0xDEB20 ends a fighter's table and stops its motors on the spot;
the port halts at the same points: the pause and every paused update (`pause_enter` 0xB10C0, `pause_menu_update`), a
fighter parked by a scene (`battle_pause_others` 0x91D44) or retired, and the fight's end (`fighter_task_destroy`
0x80CB4); the stage viewer halts while it holds still with the window away. SDL stops a pad's motors when it is
closed, so quitting leaves nothing running. `PHANTOMCRASH_RUMBLE_TRACE=1` prints every send; with
`PHANTOMCRASH_PADS=1` and `PHANTOMCRASH_HURT=STEP:SLOT:DAMAGE` (src/view/combat_viewer.hpp) a headless run shows the
levels (the virtual pads answer "not supported").

## Headless pads

For runs with several players without the hardware (`src/input/virtual_pads.{hpp,cpp}`, debug only, as MIMH's
`MIMH_PADS`):

- `PHANTOMCRASH_PADS=N` attaches N virtual SDL gamepads (up to 4) at start; they go through SDL and gubsy as real ones.
- `PHANTOMCRASH_PAD_PRESS="F:P:BUTTON[:N],..."` holds virtual pad P's BUTTON from main loop frame F for N frames (1 by
  default): `a b x y start back up down left right lb rb ls rs`, the triggers `lt rt`, the sticks `lx+ lx- ly+ ly- rx+
  rx- ry+ ry-` (y+ up).
- `PHANTOMCRASH_KEY_PRESS="F:KEY,..."` taps a key at frame F by its SDL name (`Return`, `Escape`, `Right`); the input
  layer latches it as one read held.
- `PHANTOMCRASH_MOUSE_MOVE="F:DX:DY,..."` pushes a mouse motion of DX, DY counts at frame F.
- `PHANTOMCRASH_PAD_SPIN="F:P:N[:REACH[:X:Y]]"` makes virtual pad P a worn pad: both sticks rest at X, Y of the travel
  and reach only REACH of it; from frame F for N frames they turn twice round their edge and the triggers pull to REACH,
  then for N/2 frames they lean to the edge up and right (STICK CALIBRATION's captures).
- `--vs N` starts a VS round at once on `--stage`: seat 0 the keyboard and mouse, seats 1.. the pads in turn, the stock
  builds in turn. `PHANTOMCRASH_VS_KO="STEP:SLOT"` knocks a slot out at a world step, to reach the round's end.

Frames count one a step in a capture (`scripts/capture.sh`, `--frames`), so a frame is a front end update or a battle
step. Example, a pad as P1 and the keyboard as P2 through the VS screens:
`PHANTOMCRASH_PADS=1 PHANTOMCRASH_PAD_PRESS=140:0:a:3,... PHANTOMCRASH_KEY_PRESS=150:Return,... scripts/capture.sh
out.png --state 2c --frames 720`.

## Defaults

The player starts on their own profile (id 1, a copy of the first premade); premades are rebuilt every start:
"Keyboard, mouse and pad" (100), "Xbox pad" (101), "Keyboard and mouse" (102). Alone, seat 0 holds the keyboard,
the mouse and every pad as they connect ("Seats").

Xbox pad (each button where the Duke had it): A B X Y as themselves, left shoulder white (left step), right shoulder
black (right step), Start, Back, both stick clicks, d-pad, both sticks, both triggers as levels.

Keyboard and mouse, every fight control in the left hand's reach or on the mouse:

| key | pad | in a fight (default button config) |
|---|---|---|
| W A S D | left stick | move; A / D turn under Preferred, strafe under Standard |
| Ctrl | left stick at 0.6 | walk |
| Shift | left thumb | dash |
| Space | A | jump (menus: accept) |
| Q / E | white / black | step left / right |
| C | X | camouflage |
| left / right mouse | right / left trigger | fire arm A / arm B |
| mouse side buttons (forward / back), R / F | B / Y | fire back A / back B |
| mouse | (aim) | upper body and pitch |
| middle mouse, Z | right thumb | recentre |
| wheel, 3 / 4 | d-pad up / down | view nearer / farther |
| 1 / 2, Tab | d-pad left / right | cycle the outside views |
| X | thumbs and triggers | race through conversation text (the original's chord) |
| Enter | Start | pause (menus: accept) |
| Escape | Back; Start in a fight | menus: cancel; in a fight pause (`ReadOptions::fight`, devices.cpp); in a conversation skip it, held or with Shift skip its chain |
| Backspace | Back | menus: cancel |
| arrows, Page Up / Down | d-pad, triggers | menus, page tabs |
| V / B | (PC: TURN STAND LEFT / RIGHT) | menus: turn the machine on a garage stand; a left drag on the stand does too |

The default control type is Standard (3, "Control type"): the mouse turns the mech and A / D strafe, as the right and
left sticks do there. Under Preferred the mouse twists the upper body and A / D turn the legs. It is the original's
BUTTON CONFIG setting.

## Menus

`input/menu_input.hpp` over a `PadRecord` (player 0's `Ui::pad`, or in VS each seat's own, `Ui::pads[seat]`):
- `menu_accept_pressed` (A or START, 0x110), `menu_cancel_pressed` (B or BACK, 0x220), `menu_start_pressed`,
  `menu_back_pressed`: new presses only.
- `text_skip_held`: the conversations' chord, both thumbs and both triggers held (0xC0C0, every bit), unless the script's
  flags bit 31 turns it off.
- PC only, conversations (`talk/talk_skip.hpp`, docs/story.md "Skipping"): a new press of Back (Escape, Backspace, a pad's Back)
  or a right click skips the conversation; the same button still held 24 updates later (for a right click, the left trigger its
  binding holds), or Back pressed with the left thumb held (Shift), skips the rest of its chain too. Two gubsy settings in
  Gameplay, profile scope, "show" or "skip": "Story conversations" (`phantomcrash.story.conversations`, `story_talk_skipped`, read
  by main every front end update into `FrontEnd::story_talk_skipped`) and "Battle chatter" (`phantomcrash.story.battle_chatter`,
  `battle_chatter_skipped`, for the battle's bark start). Like the other gubsy settings they have no in-game screen yet; the
  original's options screen has no PC row to put them beside (its items are the original's button art). `--no-dialogue` sets
  both to skip in player 1's profile and saves it (kept for later runs), `--dialogue` back to show; `scripts/run.sh` turns
  `PHANTOMCRASH_NO_DIALOGUE=1` into `--no-dialogue`.
- `menu_cursor_move(pad, value, lo, count, flags)`: `pad_cursor_move` 0xDF370. Flags 1 horizontal, 2 move on
  auto-repeat, 4 wrap on a fresh press, 8 always wrap, 0x10 / 0x20 the left / right stick too, 0x40 no d-pad; the usual
  0x16, 0x17, 0x12, 0x13, 0x1F are named. Returns the step. Retail plays the screen's cursor sound and stores
  `menu_input_mark` only when the step isn't 0 and the screen gave a sound; the screen does both. `menu_accept_pressed`
  and friends don't mark (the per-pad `pad_accept_pressed` does; no screen uses it).
- `menu_list_move`: the scrolling list (0xDF630).

## 60 Hz

The port reads the pads before every 60 Hz step and builds the control record every step (docs/sixty.md, "How the
port is laid out for this"). Retail read once per update: every 33 ms in battle.

- Presses are never lost: every key, mouse button and pad button that goes down is latched from its SDL event, and the
  next read counts it as held even if it is already up. So a tap shorter than a step is held for exactly one read:
  one rising edge then one falling edge. Retail loses a press shorter than its 33 ms read.
- Presses are never doubled: edges compare against the previous read, so a held button gives one rising edge, on the
  first step it is seen. When a frame runs two steps back to back, the second read sees no new events: the tap was the
  first step's.
- Analog values (sticks, triggers) are sampled at the read, not averaged. Mouse motion is summed since the last read
  and moves the target at once; the turn toward it is per step whatever the frame rate.
- What edge-detects per read in retail, now per step: `control_buttons` 4 (jump press), 8 (camouflage), 0x400..0x2000
  (d-pad); `actor_triggers_update` 0x7645E sets a mount's trigger to 3 on the update its fire bit appears and 2 while
  it stays; menus' accept, cancel and cursor moves. Each still happens once per press.
- What counts reads or updates and was converted: the stick double flick window, 5 updates, is 10 steps (R12 D);
  auto-repeat counts steps, scaled by the cadence the screen ran at in retail (`ReadOptions::cadence`: 20 steps then
  every 2 at 60, 20 then every 4 at 30), so it keeps retail's wall-clock timing. The arm raise latch (5 updates, 10
  steps) is the weapons code's.
- Fades and lost-pad pauses: `ReadOptions::blank` clears every record (`pads_read_mask` 0); latches are dropped.

## Wiring

Wired in `main.cpp` (2026-11-11) with these lines (MIMH's pattern); the fighter step (`fighter_step`,
`src/mech/fighter_spawn.cpp`) calls `control_route` for a human fighter. `--script` replaces player 1's pad record
with one read from a file for headless runs (`src/input/script_input.hpp`).

```cpp
#include "input/bindings.hpp"
#include "input/devices.hpp"
#include <gubsy/runtime.hpp>

// init input (SDL_Init needs SDL_INIT_GAMEPAD as well)
GubsyRuntime gubsy;
GubsyAppConfig gubsy_config;
gubsy_config.window_title = "Phantom Crash";
gubsy_config.data_root = <SDL_GetPrefPath("PhantomCrash", "PhantomCrash")> + "gubsy";
init_gubsy_runtime(gubsy, gubsy_config);
register_input(gubsy);
InputDevices devices;

// pump input: every event (this forwards to gubsy; don't forward it twice)
input_event(devices, gubsy, event);

// run fixed steps: before each step_world
ReadOptions read;               // cadence, blank during fades, masks, mouse_captured
seats_sync(state.seats, gubsy); // gubsy's lobby players follow the seats
seat_aim_rates(state.world, read); // each seat's rates from its fighter
read_pads(devices, gubsy, read);
inputs.pads = devices.pads;
inputs.aims = devices.aims;

// shutdown
cleanup_gubsy_runtime(gubsy);
```

In the fighter step, for a human: `control_route(actor.control, inputs.pads[seat], inputs.aims[seat],
actor.bindings, {actor.control_type, actor.airborne, blocked}, actor.flick)`, with `actor.bindings =
control_bindings_from_keys(seat_keys)` and `actor.control_type = button_control_type(seat_buttons)` at spawn
(`default_key_block` and 0 for the AI and the lessons); `blocked` once a VS round is decided.

Escape is the pad's Back (menu cancel) in menus and Start in a fight (`ReadOptions::fight`, set by the stage viewer's
loop in `main.cpp`); the window's close button quits. Relative mouse mode (`SDL_SetWindowRelativeMouseMode`) is on in a fight and off in menus;
`mouse_captured` follows it.

## Open

- The 0x4000 stick dead zone is half of every axis. It is the original's and the default; LEFT and RIGHT DEAD ZONE
  (PC CONFIG, INPUT) make it smaller for a modern pad.
- The pitch sign: `actor_camera_input` lowers `camera.pitch` for a positive axis, read here as looking down, so the
  mouse's up is subtracted. Check in play.
- Gubsy's 2D bindings with a device-specific encoding (one pad's stick only) are skipped; the premades don't use them.
