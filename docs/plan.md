# Plan

What comes first and what's next. Keep it current; strike what's done.

## First slice

1. ~~Repo skeleton: CMake with the owner's libraries pinned (gubsy, gview, glayout, gfxp, gsexp) and SDL3 the way
   Mans In My House does it; `main.cpp` loop with a 60 Hz fixed step, interpolation alpha and a catch-up cap;
   `State`; a Vulkan renderer bootstrapped on the MIMH renderer's pattern.~~ Done (renderer: `src/render/vk_*`).
2. ~~Disc access~~, changed to the extractor (`docs/assets.md`): `pc_extract` writes `data/` once (PNG, glTF, .sexp);
   the game reads only `data/`. Families so far: textures, models, stages, fighters, skeletons, motion, collision,
   tables, audio, text, scripts.
3. ~~Draw one stage and one mech with a free camera, headless capture to check it looks right.~~ Shinjuku (selector
   0) and build 0 (Plain Holy) posed on the idle clip by the SV graph walk and its IK chains; `scripts/capture.sh`
   (private Xvfb, lavapipe, dummy audio). WASD/QE/shift and right-drag mouse fly the camera.
4. ~~Fixed-point question~~: `docs/numbers.md` (Q16.16 with 64-bit squares and products, 32-bit binary angles,
   `src/fx`).

What the first slice still lacks against the original's look (`docs/port/render.md`):
- ~~the renderer runs its own small mesh shader~~: done, the original's programs from `assets/shaders/original`
  with the binding layout of `docs/shaders.md` and a negative viewport height (`src/render/mesh_draw.cpp`,
  `mesh_constants.cpp`, `vk_*`); per-section material state, all four texture stages by slot, bump, reflect,
  seabump and the point shaders; pass 10's particles are drawn (particle.vert, premultiplied.frag); pass 11's
  cockpitUV draws the cockpit instruments; vision_eff isn't wired yet (nothing queues it);
- ~~one fixed directional light, no stage lights, no fog, no scene tint or gamma ramp, no dof composite~~: done
  (`tables/stage_lighting.sexp`, `src/render/stage_lighting.cpp`, `vk_composite.cpp`; `--day N`); point lights
  are the effects' (`src/effects`); lamps by zone on Shibuya (2026-11-12);
- ~~no stage object animation, no props, no supply craft~~: done in `src/stage` (the world's props, gates, craft
  and crates, the static and shake draws; on screen the animation kinds, the craft, crates, sky craft and moon;
  the ambience; ~~the big screens' scene texture~~ (2026-11-12: mesh 0x80000 samples the last frame's scene,
  `texture_scene_last`, copied by the composite in `vk_backdrop.cpp`)); left: the fighters'
  side (crate pickup, prop touch and shots against props and the craft through the `src/stage/stage_world.hpp`
  calls);
- ~~no stencil shadows~~: done for layer 7 volumes and the fighter's volume batch (slot 9, the remapped chassis
  palette); slots 10 and 11 (mount and back volumes) and the AI fighters' leg blob shadows are not drawn;
- ~~the pass sort is by distance; no per-view culling~~: done: the queues' groups and box culling per view, with
  retail's sort bugs fixed (`docs/bugs.md`);
- ~~the full window instead of the 576x448 safe rectangle~~: `--safe-rect` draws the 3D there, the default fills
  the window; both scale the 640x480 screen to the window;
- ~~zones: only zone 0~~: done (`stage_zone_at`); the lamps' zone ranges too (2026-11-12,
  `mesh_constants.cpp`: Shibuya's by `shibuya_zone_at`, the rest zone 0 as retail);
- the fighter: ~~no sticker model (model 0x592 + slot, pass 2)~~ (2026-11-12, `mech_sticker_items`); ~~no pose blend, IK feet not clamped
  to the ground, the arms' `model_flagged_nodes_set` and the backs' aim and spin left out, the body class 2 default
  backs not drawn~~ (2026-11-11: the pose is in the step, below);
- ~~the cook step (`cooked/`) isn't written~~: done (`src/cook`, `pc_cook`, `docs/assets.md` "Packs"); the game
  loads the stage and the fighter from packs and cooks on first run. Audio still reads `data/audio` directly.

- ~~Effects~~ (2026-11-11, `src/effects`, decomp `docs/port/effects.md`): the particle pool on the 30 Hz update in
  presentation with its own generators, drawn interpolated through pass 10; every effect set from
  `tables/effects.sexp` (the effects family); sprite effects, scorch marks, shot trails, muzzle flashes, gun smoke,
  wrecks, the craft's jets, fires and crash smoke, burning props' lights, the hit static, the lens flare (a depth
  read a frame late), point lights, a fighter's jump fire, verniers, mufflers, landings, dust, sparks and hover wash.
  Left: contact decals' draw (effopt over the backdrop target, pass 6), the weapon break arcs,
  the lamp trails at night, casings, shot looks and the blast volume (still the weapons' placeholder
  boxes), the shield bubble and camouflage hull. The attachments (0..13), feet (14..19) and muzzles are the live
  pose's (`FighterPoseState`, `weapon_muzzle_at`).

## Player setup

The game ships code only. On first launch it asks for the player's own Phantom Crash (a disc image or an extracted
folder plus the XBE), runs the extractor once into its data folder, then cooks the packs; later launches only
re-cook what changed. The extractor is a module the game links, and `pc_extract` is the same module as a
standalone tool for development and batch runs. This is how Ship of Harkinian and the Mario 64 ports work, and it
means nothing from the disc is ever distributed.

## Then

- ~~Static tables to `.sexp`~~ (the tables family, `docs/tables.md`).
- ~~Mech movement on a stage at 60 Hz, camera, input~~ (2026-11-11): a fighter spawns from a build into `state.world`
  (the performance pass in Q16.16, `src/mech/fighter_spawn.cpp`) and runs the motion states 0..6 and 11..14 for every
  leg class (`fighter_motion.cpp`, `fighter_air.cpp`), `sv_contact_route` against the stage grid
  (`fighter_contact.cpp`, `src/battle/stage_collision.cpp`) and the chase camera (`src/battle/battle_camera.cpp`), with
  every conversion of `docs/sixty.md`; main wires the input layer and draws the fighter from the world interpolated.
  The game boots the front end; the battle is the stage viewer (`--viewer`, or `--stage`, `--build`, `--script`...)
  until the front end starts one.
  Standing jumps, skids and dodges measured against the read code (`docs/sixty.md`, "Measured in the port"). Left:
  - ~~the pose walk in fixed point in the step~~ (2026-11-11, `src/mech/pose_walk.cpp`, `pose_weapons.cpp`,
    `docs/numbers.md` "The pose in the step"): the SV walk, IK with the feet on the stage, the pose blend, the arms'
    and backs' models, the attachment points and the wheels in Q16.16; the view draws every slot from it. Left: the
    footstep events (the 0x3800 key crossings), the contact sphere sets (kind 5 assets, not extracted yet) and with
    them the contact-set push of `fighters_separate`, the weapons' own clips (`weapon_anim_update`, resource 1 of the
    weapon blocks). ~~The arms' raise and the backs' elevation~~ (2026-11-11, with the weapons merged: the pose reads
    the triggers' `raise`, `mount_free_aim`, the HUD's `reticle_track_world` and `Weapon.elevation`, and writes
    `launch_pitch`);
  - ~~fighters against each other (`fighters_separate`)~~ (the sphere push; the contact-set push waits for the pose
    walk), props (gates, cars) in the contact route and the camera, the contact decals, `actor_fall_reset` (its thresholds `D_00232658` aren't extracted);
  - the stagger, melee and dead states (7, 9, 10) with damage and weapons; the other camera views, the view switch
    and the cockpit; the beginner mode's camera;
  - ~~more than one fighter~~ (2026-11-11: `--opponents` spawns AIs with retail's gate rolls, `src/battle/battle_spawn.cpp`);
  - a comparison against xemu recordings of the scenes in `docs/sixty.md`, "Checking against the original".
- ~~Weapons, shots, damage~~ (2026-11-11, `src/weapons`, docs/sixty.md "Weapons as the port runs them"): triggers,
  the fire, laser and melee controllers, every shot mover, the shot pass and hit order, damage and the settle,
  lock-on, the stagger, swing and dead states. ~~Muzzles from the pose walk and the weapon blocks' attach points~~
  (2026-11-11: `fighter_muzzle` is the block's muzzle point under the pose's weapon node, barrels cycled; the
  flash, casing and spare streams 1..4 aren't read yet); the beam test against the supply craft and
  crates (`supply_craft_beam_hit_test`); camouflage; the killer's scene 7 and 840 ticks; free play's refill; the
  weapon break timer and the gauge roll (HUD); sounds from `World.combat.events`. ~~AI~~; battle rules;
  the rest of `docs/port/gameplay.md`.
- AI (2026-11-11, `src/ai`): all of decomp `docs/port/ai.md` runs in `step_world` on even steps as retail's 30 Hz code:
  `ai_enter` with the level bug, targeting, the states 0..6, aim, routes over the ENPT graphs (`data/nav`, cooked into
  the stage packs), ram and crates, the fire decisions, dodging, camouflage, rankers; the HUD senses it reads
  (`src/battle/hud_senses.cpp`) and the AI's cockpit camera. Its reads of damage, weapons, shots, camouflage and the
  lock-on HUD go through `src/ai/ai_links.cpp`, which answers for an undamaged, unarmed world until those systems
  merge. Left: wire the links (weapon reach and resources, `DamageState.pending`, shot threats, `triggers[].pressed`,
  the camouflage state, the lock HUD's `locked_on`/`locking_me`/`melee_locked` and reticle box, the tournament rule
  tier); the gate scenes (an AI's intro walk and freeze, `door_wait`, `stage_door_available` on busy gates); the ranker
  bout's rolled gate and map starts; `actor_door_check` (an out-of-ammo AI reaching a door leaves); the replacement
  newcomers.
- The original's screens ported as game code from its layout data (GView only for PC-only screens). ~~The front end~~
  (docs/screens.md): the application states with the original's numbers, the sprite layer (`src/render/vk_sprites`),
  the boot (movies, logo, attract), title return, title, options, sound config, the load screen and the records check
  on the save slots, New Game through the opening and its conversations to the town map. Next: name entry (0x16),
  the help menu (0x13), the town map (0x29), the system menu (0x27) with the save screen (0x1F), key config (0x10),
  screen adjust (0), VS setup (0x2C), the attract demo (1) once there is a battle; then garage, shops, tournament flow.
  Saves: the format, load and write, and the Xbox import are in `src/save` (docs/saves.md).
- Audio: music, SE banks (Xbox ADPCM), ADX; movies (Sofdec is MPEG-1). Extracted to data/audio/ (docs/audio-assets.md);
  next the player side: streams, cue playback with voices and 3D, I3DL2 reverb, the movie player. The movie player and
  its screens are in `src/movie` (docs/movies.md), played by the front end's movie states through the sprite layer.
- Rendering parity with `docs/port/render.md` (the original's programs and fixed-function paths are written as GLSL in
  `assets/shaders/original/`, index and binding layout in `docs/shaders.md`), then improvements (resolution, 4K, menu art redrawn as vector
  recreations of the original's prerendered assets).
- Online play later: the world is deterministic fixed point from the start.
- Mouse and keyboard are first-class (owner, 2026-11-11). Battle: WASD legs, mouse upper-body twist and pitch, weapons on the mouse buttons, everything mid-fight under the left hand; all devices write the same ControlRecord. Mouse aim always turns at the mech's turn rate, toward a target the mouse moves (owner, 2026-11-11; docs/input.md). Menus keep the original's cursor logic; the mouse drives that cursor (hover selects, click confirms, right click backs out, wheel scrolls), with hit boxes from the layout rects.

## Open

- How to check "plays the same" at 60 Hz without a test suite: no emulator runs for now (owner, 2026-11-11);
  rows read but not run are marked UNKNOWN in bugs.md and listed in docs/backlog.md.
- (Superseded 2026-11-11: there is no uncapped mouse aim.) A harder mode for uncapped mouse aim, later (owner, 2026-11-11): more aggressive AI behaviour is the likely direction. Story opponents spawn at AI level 0 while the demo battles use 99 (decomp `docs/port/ai.md`); that may be a deliberate cut rather than a bug.
