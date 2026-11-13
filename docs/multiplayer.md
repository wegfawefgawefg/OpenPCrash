# Multiplayer

What multiplayer the port should have, how it stays in sync, how players find each other, what the campaign does with
two pilots, what has to change in the code now so none of it is blocked later, and the order to build it in. Written
2026-11-11 against fc8c29f. Built since (2026-11-11): local split-screen VS with seats on any device, change-now items 2
and 3, and item 6 settled; then (2026-11-12) the online groundwork: every change-now item but 8 (records per seat, with
their port), the whole step checked deterministic across GCC, Clang and ARM64 (`docs/numbers.md` "Determinism"), and the
measurements below with 6 and 12 fighters. See "Status". The rest is the design.

Short version:

1. Modes: local split-screen VS as the original has it, then the same VS online (local and remote seats mixed, teams
   from the original's team rule), then online co-op in the host's tournaments, each player bringing a machine from their own
   save.
2. Sync: deterministic input sync with rollback, as Mans In My House does it, input delay 2 steps. Each seat's pad
   (the fields the world reads, about 21 bytes a step) and mouse aim go over the wire. The 20-byte control record can't
   go over the wire, because it is built inside the world.
3. Lobby: gubsy-roomd (already running for MIMH) for six-letter room codes and a room list, NAT punch with relay
   fallback. Copy MIMH's `src/net` client and rollback code. No Steam.
4. Campaign: only battles are synced, not menus. The host's save is the campaign. The guest brings a machine from their
   own garage, keeps their own money and records, and gets no story progress.
5. Twelve things to change now, listed below with files. The main ones: per-seat input and config instead of "player 0",
   per-machine options moved out of the world, a field-by-field world hash, and per-fighter lock-on projection.

## What the original has

- **Split-screen VS for 2 to 4 pads.** It is state 0x2C, the join, machine and area pages (decomp `docs/port/screens.md`
  "VS setup"). A battle is state 6 with `vs_mode`, and its end sequence is in `battle_update` (`docs/port/battle.md`
  "Split-screen VS").
  - Each pad is a player. Its `Actor.controller` is the pad, which is also its viewport, HUD and `vs_wins` index.
  - Machines are the three stock builds (PLAIN-AR, -HL, -PH) plus the VS builds that KEEP SV (0x2B) writes as
    separate save games (`VsData.sav`).
  - With two players any area by day or night. With three or four, only the training stage (area 3) or random.
  - VS turns off: replacements, barks, the door scene and gates opening by touch. Sound banks 1 and 2 are muted and every 3D
    sound sits at one point. Only repair and ammo crates drop.
  - A player is out at durability 0, with no weapon left, or through a gate. The round ends when one player or none is
    left. The end menu (rematch, area, machine, quit) runs inside the battle and every pad drives it.
- **Views** come from `viewport_layouts`:
  - two players get top and bottom strips of 576x220;
  - three or four get quarters of 284x220;
  - the HUD has 1, 2 and 4-view tables (`hud.md`).
  - The projection keeps the 52-degree vertical fov and takes the aspect of the rectangle (`render.md`). So in
    two-player VS each player's lock-on test (`hud_senses_update` projects with `pass_projections[controller]`) covers
    about 104 degrees across instead of one view's 64. Two-player VS is a wider lock-on game than single player.
- **Teams.** There is no team mode, but every fighter has `PilotRecord.team`. Two fighters on the same team (not -1)
  don't sense each other, so neither locks on or targets the other, and their shots pass through each other. VS gives each
  player its join order as team. In tournaments the player is -1 and everyone fights them.
- **No online play.** findings.md notes a Winsock error table and "Creating Game %s" strings, left over and unread.
  Nothing reachable uses them.
- **Single-player tournaments only.** Roster entry 0 is the player. The battle ends on the human's door, death or ranker
  kill. `battle_result_build` fills one `GameState.result`. The town map, shops, days and conversations all assume one
  pilot (`progression.md`).

## Modes

### VS, local split screen

Port the original's as it is: state 0x2C from its layout data, 2 to 4 seats, VS builds, split viewports, the 2- and
4-view HUD tables, the end sequence and the muted VS sound rules. Seats come from gubsy lobby players instead of
"connected pads". A seat can be the keyboard and mouse, so two people can play on one pad plus the keyboard. The
lock-on projection stays one view's for every player (owner, 2026-11-11; "Change now", item 6).

- Supported by the original: all of it.
- New: device-to-seat assignment through gubsy, and keyboard and mouse as a VS seat.

### VS online

The same battle with seats on several machines, up to 4 humans in all, any mix (two on one couch plus two remote).

- Each machine draws only its own seats: full screen for one, split for more.
- Sound follows the original's VS rules only on a machine that is split. A machine showing one view plays full 3D sound
  for its seat (presentation only, the world doesn't care).
- The area rule for three or four players exists for split-screen cost, as far as anyone can tell (the decomp doesn't
  say). Online, allow any area for any count. Locally, keep the original's rule.
- Lock-on projection online: one view's (576/448) for every human, split or not. Two-player online VS then plays like
  single player, and nobody gets the wide strip by sitting on a couch.
- Supported by the original: the whole battle, the end menu (it runs in the world on synced pads, so it just works,
  every seat moving the cursor as retail).
- New: the lobby (a GView screen, which AGENTS.md allows for screens the original doesn't have), leaving mid-round
  (the leaver is out, as through a gate), and pause as a local overlay that doesn't stop the battle.

### Team battles

These come from the original's rule with no new combat code. VS seats get a team per side instead of their join order,
so 2v2, or 2v1 with one AI filling. Shots pass through teammates and teammates can't lock on to each other, as retail.

New code: the end check becomes "one team or none left in play" (retail's `vs_nobody_in_play` and the
`vs_in_play` count, by team), and win counts are per seat as now.

AI seats in VS are new too. Tournament 0 has no opponents, so an AI seat is a roster entry from the opponent table or a
stock build, with a level. Keep this optional, one lobby toggle. It is the only VS feature the original can't
already do.

### Co-op in tournaments (online, and local split screen)

Two humans (up to 4 is possible, but 2 is the design) fight in the host's tournament against its drawn opponents. The
two humans share a team id that no pilot record uses, so the original's team rule makes them allies for free. The AI
targets whoever it senses, both of them.

What has to be new:

- **Slots.** Retail has the player plus up to 5 opponents in 6 fighter slots. With a partner, the partner takes one
  opponent slot in the 6-slot tournaments. In the 3-entrant ranker bouts it adds a fourth slot. Replacements keep
  coming from the same queues, so the total opponents in a fight don't change. Their kills are now split two ways.
- **The end.** Retail ends when the human goes through a door, dies or kills a ranker. Co-op ends when every human is
  out, by door or by death. A dead human's view follows the partner after the death scene. A ranker kill by either
  human ends it as retail.
- **Results per seat.** Each human's prize is their own score (halved for a rental), and their own repairs come off it.
  Kills on progression opponents credit the host's pools and `tournament_won` whoever made them. Retail already writes
  progress on "a kill by a human", any human.
- **Entry.** The host pays the fee and picks the tournament. The guest's machine has to pass the rule
  (`job_entry_allowed`) or the guest rents, as retail.

Local split-screen co-op falls out of this plus VS's split views. The second pilot's save is another slot on the same
machine.

Rejected shapes:

- **One shared save for both players.** Then whose garage, whose money, and who owns the pilot name? It needs a second
  economy in the save format. No.
- **Each player's own campaign advancing together.** Two saves on different days have different line-ups, shops and
  progress. Nothing reconciles them. No.

## Sync

### Model: rollback with input delay

Every machine runs `step_world` on the same inputs. The world is already built for this: a copyable value, fixed
point, one RNG, total orders, the AI and the RNG on fixed step parity.

- **Lockstep** waits for every seat's input before each step. At Tokyo to US distances (about 120-150 ms round trip) that
  means 5 to 8 steps of input delay. Every hitch stops everyone.
- **Rollback** runs ahead on predicted remote inputs and re-runs when the real ones arrive. MIMH does exactly this
  (`src/net/rollback.cpp`, `lockstep.cpp`, `timing.cpp`), with a fixed delay of 2 ticks by default (adjustable 1-8, with automatic adaptation), a prediction
  window of 48, and host-relayed authoritative frames.

Use rollback with a 2-step delay (33 ms). Retail read the pad every 33 ms, and its view and aim chase the stick by 30%
an update, so 2 steps is inside what the original already feels like. Remote fighters are the only thing mispredicted:
the AI is deterministic and the local seat is exact.

#### Measured

2026-11-12, Release (GCC 13.3, -O3), Ryzen 9 9950X, with `pc_sim --time` (`src/sim_main.cpp`): the world alone, no
drawing in the process, pinned to one core, three runs each. The machine was busy (load 32-35 on 32 threads, another
agent's lavapipe captures), so these lean high. The fights are the viewer's AI-only demo on Shinjuku, 5400 steps:
`--opponents o19,o40,12,40,5` (6 fighters) and the same plus `o3,o50,o77,22,30,8` in a 12-slot build
(`-DPHANTOMCRASH_FIGHTER_SLOTS=12`, cmake/sim.cmake). The step time is split by how many fighters were still alive after
the step, since a fight thins out.

| what | 6 slots | 12 slots |
|---|---|---|
| `sizeof(World)` | 335,496 bytes: `CombatWorld` 152,768 (the shot pool), fighters 6 x 11,176, `StageWorld` 77,704 (102 props), `RumbleSlot` 4 x 5,444, `Garages` 6,932, `Story` 2,792, AI 6 x 376, HUDs 6 x 252, `Profile` 676 | 417,440 bytes |
| copy a World | 5-8 us median (the `shared_ptr` count only; nothing allocates) | 8-12 us median |
| one step, every fighter alive | mean 160-200 us (6 alive), p99 190-245 us | mean 500-560 us (12 alive), p99 590-710 us |
| one step, fewer alive | 45-55 us with 3 | 205-230 us with 6 |
| re-run 8 steps for a rollback, every fighter alive | about 1.4 ms | about 4.2 ms |
| copy plus 8 re-run steps, over the whole fight | mean 0.64-0.76 ms, p99 1.4-1.8 ms | mean 1.9-2.0 ms, p99 4.7-5.3 ms |
| `world_hash` (every field) | 97-100 us | 124-127 us |

- A 12-fighter step is about 3 times a 6-fighter one, not twice: the senses, separation, threat lists and shot tests
  go over pairs. The brainstorm's guess (0.5-0.7 ms) holds: 0.50-0.56 ms.
- A rollback of 8 steps with 6 fighters is about a twelfth of a 60 Hz frame; with 12, a quarter (4.2 ms of 16.7). A
  window of 15 steps with 12 fighters would be about 8 ms, half a frame: fine on this desktop, tight on a weak laptop.
  The arena cap is the owner's call (`docs/backlog.md`).
- Copying is still not the cost: 16 snapshots are 5.4 MB (6.7 MB with 12 slots). Re-simulating is, and only on a
  misprediction.
- The hash reads every field (half of them the shot pool's), so it costs about a step; every 30 confirmed steps that is
  noise. The earlier 23 us was a hash of raw memory words, which takes in padding that machines can't compare.
- Earlier (2026-11-11 on fc8c29f, a throwaway timing patch in `main.cpp` with lavapipe drawing in the process): 241,048
  bytes with the stage's two vectors, a copy 18-22 us, a step mean 290-355 us.

Allow a window of up to 15 steps (250 ms). Past that, hold the simulation until input arrives, as MIMH does.

### What goes over the wire

Each seat's input for each step: the parts of `PadRecord` the world reads, plus `MouseAim`.

- The control record can't be sent. `control_route` builds it inside the world from the fighter (airborne, control
  type, the double-flick state, the twist kept in the air, blocked in a door or a scene). Under delay and rollback the
  sender doesn't know the fighter's state at the step the input lands on.
- The world reads only `b.cur`, `b.rising`, both sticks' x and y (`control_route.cpp`), and the mouse's twist and
  pitch. Everything else in `PadRecord` (analog levels, falling, repeat, timer, stick directions) is for menus.
- A seat's step input: buttons held (16 bits) and pressed (16), four stick shorts, twist and pitch as raw Q16.16, and a
  flags byte (connected). That is 21 bytes, about the size of a control record. Packets carry every un-acked
  input since the last ack (MIMH's scheme). At 60 packets a second that is a few KB/s a peer before compression.
- The mouse share is already turned into `Fx` axis values by the sender's input layer, using its own fighter's turn rate,
  which is the same build data on every machine. The receiver never sees mouse counts or the sensitivity.
- Prediction of a remote seat: repeat held buttons and sticks, zero the pressed edges, zero the mouse share. A repeated
  mouse share would turn past the sender's target, so it isn't repeated. That is the same rule the port already uses for the
  AI's odd steps: the record held, the edges cleared.

### What every machine must agree on before step 0

The host sends a battle start. Every machine builds the same `World` from it:

- the world's saved part: for VS, `game_state_new` with the VS rosters; for co-op, the host's story, garages and rumble
  slots, about 32 KB raw;
- every seat's roster entry (build, tunes, colour, sticker, team) and seat config (button config, control type, camera
  view);
- the RNG index (retail seeds it from the clock; the host picks it);
- the stage selector, the lobby rules, and the step number.

A handshake before any of that refuses a mismatch in:

- **wire version;**
- **simulation code stamp:** a hash of the world's sources, as MIMH's `cmake/sim_stamp.cmake`;
- **gameplay data hash:** the cooked tables, part records, collision and nav. Every player converts their own disc. A
  different disc release or a different converter version is a desync waiting to happen. Name the files that differ, as
  MIMH's `net/session_files.cpp` does.

### Desyncs

Every 30 confirmed steps each machine hashes the world and sends the hash. The hash is field by field, not raw bytes:
padding, the `BattleData` pointer and `StageWorld::data` must not count. Measured at about 100 us (`world_hash`, every
field; "Measured" above).

On a mismatch:

1. Both sides dump the confirmed world and the inputs since the last match to the run log.
2. The host sends its confirmed world to that client.
3. The client restores it and continues.

This is MIMH's `net/recovery.cpp` minus its section marks at first. A battle snapshot is mostly the shot pool, which is
mostly empty, so it compresses to little. Late join mid-battle isn't needed: VS joins between rounds and co-op joins
between battles, so the restore path only serves recovery.

Crossplay depends on the whole step being deterministic. `docs/numbers.md` "Determinism" checks it: 16 fights hashed
every step matched under GCC, Clang (libstdc++ and libc++) and ARM64, after turning off fused multiply-adds. Add the
Windows compiler when there is a Windows build. On a mismatch, `world_hash_diff` (in one process) or `pc_sim --parts N`
(between machines) names the parts that differ.

### Mouse aim

Mouse aim always turns at the mech's turn rate (owner, 2026-11-11), for everyone in every mode: the mouse moves a target
angle and the mech turns toward it as a full stick would (docs/input.md, "Mouse aim"). There is no faster mouse, no
setting and no lobby rule. `control_route` holds the mouse share and the stick to -1..1 together, so a modified client
can't out-turn a pad either.

### Steps and the clock

`main.cpp` drops backlog past 8 steps a frame ("never slow motion"). Online the net layer owns the step count instead:
it catches up to the host's clock, measuring half the round trip, as MIMH's `server_clock.hpp` does. It never drops
steps, since a dropped step is a desync. The local catch-up cap stays for offline play.

## Lobby

What exists (read 2026-11-11):

- **gubsy, at the pinned c4d5408:**
  - Local lobby players (`GubsyLobbyState::local_players`, device assignment, `gubsy_lobby_add_local_player`,
    `gubsy_lobby_toggle_device`). The primary player keeps the keyboard and the first pad; a hot-plugged pad goes to
    player 1 if it has none.
  - The room client: `RoomServerMatchmaking`, six-letter room codes, `list_rooms`, heartbeats.
  - A `SessionContract`: version, `net_protocol`, content hash, `PlayerHost` or `DedicatedServer`, and connection
    candidates Loopback, LanDirect, PublicDirect, NatPunch, Relay and Steam.
  - The signed punch and relay packet formats (`include/gubsy/realnet/`).
  - Missing: no rollback or lockstep library, and its own UDP transport is a direct-only test transport. The Steam
    backend is a stub; every game builds with it off. The lobby start hook, roster lock and lobby input helpers are
    on gubsy's unpushed master, newer than the pin.
- **gubsy-roomd:** HTTP rooms plus UDP rendezvous and relay, deployed on a dev server (HTTP, rendezvous
  8789, relay 8790).
- **Mans In My House:** 2-8 players, up to 4 per machine.
  - Raw UDP (`net/socket.*`), port 39100 for direct and LAN.
  - gubsy-roomd through libcurl (`net/rooms.cpp`), punch then relay after 3 s (`net/traversal.cpp`).
  - Rollback on every machine with host-formed authoritative frames.
  - A world hash every 30 ticks with checkpoint recovery.
  - A content hash plus sim stamp gate, and a GCC-vs-Clang crossplay check (`scripts/crossplay_check.sh`).
  - All of it is working and played.

Recommendation:

- **Host and join.** One player hosts; the host's machine forms authoritative frames and relays inputs (a star, MIMH's
  shape). With at most 4 humans a mesh would save one hop, but MIMH's star already works and is the one to copy.
- **Codes and a list.** A six-letter room code to share privately, and the room list for "who's up", both from
  gubsy-roomd. LAN direct by address for the couch next door.
- **NAT.** Punch through gubsy-roomd's rendezvous, fall back to its relay after 3 s, as MIMH does. Relay costs one
  extra hop; at 2-step delay plus rollback that is fine.
- **No Steam.** The port needs each player's own disc data, so it can't be a Steam product. Steam's lobbies and relay
  would need an app id it will never have. gubsy keeps a Steam candidate kind if that ever changes.
- **Code.** Copy MIMH's `src/net` (socket, rooms, traversal, timing, rollback, lockstep, recovery) into
  `src/net/`, with the wire payload swapped for the 21-byte seat input and the battle start. That is how gubsy's
  networking docs expect games to do it. If a third game wants the same code, that is when to make it a library (gubsy's
  `docs/ripout/gnetcode.md` sketches one).
- **Local seats.** Use gubsy lobby players for both local VS and online seats. Each machine adds its local players to
  the room and the host numbers the seats. A world seat (`Fighter.controller`, `Inputs::pads[seat]`) is a match seat
  0..3, not a local pad.
- **Screens.** Local VS keeps the original's 0x2C pages. The online room (create, join by code, list, seats, builds,
  rules, ready) is a GView/GLayout screen that hands off to the same machine and area pages where it can.

## The campaign with two players

Only battles are synced. The front end, menus, shops and conversations are not in `step_world` and stay that way. The
only shared moment is the battle start (above) and the battle's end (results per seat).

- **Whose save.** The host's. On every machine, the battle's `World` holds the host's story, garages and rumble slots.
  The guest's machine keeps the guest's own save image aside during the session.
- **Money and parts.** Each player's own. The guest picks a machine from their own garages (or rents). Their prize and
  repairs come from their own seat's result and are applied to their own save image when the battle ends. The host's
  money, parts and garages never change because of the guest.
- **Progress.** The host's only: pools, `tournament_won`, rankers and days advance in the host's save. The guest's own
  story doesn't move. Co-op is hiring a partner, not playing two campaigns.
- **Records.** Per seat. Kills, drones and enemy weapons destroyed count in the fighter during the battle and are
  committed to each seat's own records at the result. Retail writes these to the one profile live; see item 8.
- **Saving.** The host saves as retail does, from the system menu. The guest has no system menu in the host's flow, so
  their save is written at the end of each co-op battle's result, to the slot they joined from. This breaks retail's
  "never saves by itself", for the guest only (question 5).
- **Town map and menus.** The host drives the town map, Club Wired's jobs, the area select and the night. The guest
  has a small partner menu: pick a garage or rental, see their own money. No shopping during the session; the shops are
  the host's day's, and buying from them with the guest's money mixes two economies.
- **Conversations.** They change nothing but their `events_done` bit, which is the host's. They play on the host's
  screen as retail. The guest sees the same conversation and can skip it on their own; it changes nothing there, and the
  next shared screen waits for the host. The skip chord stays as the original has it.
- **Map spin money** (the right-stick circle on the town map) is the host's.

## Change now

These are cheap while the systems are small, and each closes a door if left. Numbered as the backlog refers to them.

1. **A world hash and a whole-step crossplay check.** Done 2026-11-12. `world_hash` covers every field of the `World`
   (`src/world/world_hash.cpp`, `world_hash_save.cpp`, `world_hash_put.hpp`), skipping only the constant tables' pointers
   (`battle`, `StageWorld::data`, `Weapon::record`, the story and roster tables); `world_fight_hash`, the replay's, leaves
   out the save part, the phase, the result and the letterbox's clock, which a replay doesn't step. A size guard beside
   each struct's hash fails the build when a field is added and not hashed. `world_hash_diff` names the parts two worlds
   differ in. `--hash N` prints both hashes every N steps in the game (the viewer and the front end's fights) and in
   `pc_sim`, the world built and stepped headless (`cmake/sim.cmake`, `src/sim_main.cpp`; it builds alone for ARM64). The
   crossplay result is in `docs/numbers.md` "Determinism": every fight matched under GCC, Clang (libstdc++ and libc++) and
   ARM64, after one fix (no fused multiply-adds).
2. **Seats, not "player 0".** Done 2026-11-11 (`src/input/seats.*`, docs/input.md "Seats").
   - `src/world/world.cpp` gives a human whose `controller` is outside 0..3 player 0's pad. Give it a zero pad: a seat
     that isn't there must never borrow someone's input.
   - `src/world/inputs.hpp`: `pads` and `aims` are per match seat, and the comment should say so.
   - `src/main.cpp` fills `read.rates[0]` from `fighters[0]`. Fill each seat's rates from the fighter whose
     `controller` is that seat.
   - `main.cpp`'s ambience listener and `src/view/view.cpp`'s `step_pose` follow fighter 0 or `--follow`. Make that the
     local seat's fighter, one per local view when split screen comes.
3. **Per-seat config at spawn.** Done 2026-11-11: `battle_spawn` takes the seat from the roster entry's `player` (the
   controller), its gate from `player_gate[seat]` and its keys and control type (default 3) from
   `World::player_buttons[seat]`; the starting camera view is 1 until views 2..5 are ported. Was: `src/mech/fighter_spawn.cpp` gives every human the default keys and control type 0
   "until saves come". When saves come, take the bindings, control type and starting camera view from the seat, through
   `SpawnSlot` (`src/battle/battle_spawn.hpp`) or the roster entry, never from `world.profile`. Retail VS keeps a button
   config per player (GameState +0x10FD8/+0x10FDC by slot).
4. **Per-machine options out of the world.** Done 2026-11-12: the volumes, screen shift, vibration (and the unread
   `flag_31`) are `MachineOptions` in `State::machine`; `game_options(fe)` returns them; `SaveImage::machine` writes and
   reads them in the same `(options ...)` form (`docs/saves.md`). `Profile.options` keeps the camera view and the button
   config, player 1's seat config. The title's New Game resets the machine's options too, as retail's `game_state_new`
   does.
5. **The world reads only the input it sends.** Done 2026-11-12 (`src/world/pad_record.hpp`, `control_route.hpp`): the
   world reads `b.cur`, `b.rising`, both sticks' x and y and `MouseAim`, in `control_route` and VS's end menu (which keeps
   its own auto-repeat from them in the world, `VsBattle::menu_buttons`). One more
   world input comes from presentation, `Inputs::scene_text_done` (the lessons' and the ending's lines); it is in
   `docs/backlog.md`.
6. **Lock-on projection.** Settled (owner, 2026-11-11): the lock-on and sense projection stays the one view's (52
   degrees over 576/448) for every fighter in every mode, whatever the viewport's shape, because lock-on range is a big
   part of the game's balance. `src/battle/hud_senses.cpp` and `src/weapons/hud_aim.cpp` keep their one constant
   projection; nothing per fighter. Retail's two-player strip (576x220, about 104 degrees of lock-on across) is listed
   in `docs/bugs.md` as a deliberate deviation. The HUD maps the world's screen units into each view at the one-view
   aspect (`hud_core.cpp`), so in a wide strip the boxes and marks keep one view's width.
7. **Hit static and shake for one seat.** Done in the world 2026-11-12: `StageWorld::hit_seat` records the seat whose hit
   set them (`src/weapons/damage_settle.cpp`); the world's draws are as they were. The effects' 2D goes into each view
   and the static only into that seat's (`effects_screen`, src/effects/screen_effects.cpp, 2026-11-12).
8. **Records and results per seat** when they are ported:
   - Retail writes kills, drones and enemy weapons destroyed to the one profile at the hit, and `battle_result_build`
     fills one `GameState.result`.
   - Count them in `FighterCombat` (it already has `kills` and `score`) and commit them to the seat's records at the
     result. Make the result an array by seat.
   - Single player plays the same.
9. **Flat stage state.** Done 2026-11-12: `StageWorld::props` (102: retail's pool of 100 and the two the training
   stage writes past it) and `events` (64; a fight's busiest step makes 6) are fixed arrays with counts, so a copy
   allocates nothing. They were the world's only containers. `World::battle` stays a `shared_ptr` to the constant
   battle data (a copy bumps its count; the replay's start world keeps the data alive).
10. **Events consumed per step, with an identity.** Done 2026-11-12: `CombatEvent`, `StageEvent` and `BattleEvent`
    carry the step they happened on (and the combat events their slot); presentation reads all three after every step
    (`effects_step` queues them for its 30 Hz update, `hud_step`, `stage_audio_step`), never per frame. Under rollback
    the view keys an event by step, kind and slot to play it once.
11. **A lobby-rules block in the world.** Done 2026-11-12: `LobbyRules` in `World::rules` (`src/world/world.hpp`): `online`,
    `team_end` (VS decided when one pilot team or none is left; `vs_battle_update` reads it) and `ai_seats`. No mouse rule
    (mouse aim is always at the mech's turn rate) and no boss rule (a boss's entrance freezes everyone, however many
    humans). All off is retail; nothing sets them yet.
12. **A gameplay data hash.** Done 2026-11-12: `gameplay_data_hash` (`src/cook/data_hash.*`) over every entry but the
    textures of the core pack and the stage, leg, body, arm and back packs (179 packs, 358 entries, 39 MB, 10 ms at
    start); the game and `pc_sim` print it as `data hash:` in the run log. The lobby handshake will send it.

Not changes, but rules for whoever ports the next systems:

- **Fighter slot 0 is not "the human".** `ai_track_player_hits` (`src/ai/ai_ranker.cpp`) reads slot 0, and that is a
  retail AI number, so keep it; in co-op slot 0 is the host. New code finds humans by `controller >= 0`.
- **The battle's end.** "The human" in the end conditions (door, death, ranker kill, the result) becomes "every human
  out" in co-op. Write it as a loop over humans even while there is one.
- **CRT `rand` stays outside battles.** It is fine in menus (Sonic Amp's stock, the lineup roll's caller), because
  menus aren't synced. Anything a battle reads draws from `world.random`.

## Status

Local split-screen VS, built 2026-11-11:

- Seats (`src/input/seats.*`): any device is any player, in any order; players join on the VS join page by pressing
  accept on their device and leave with cancel to change device (docs/input.md, "Seats").
- VS setup 0x2C (`src/screens/vs_setup.*`, `vs_draw.cpp`, `vs_area.cpp`): the join, machine and area pages from the
  layout data. The machine list has the three stock builds only; KEEP SV's VS builds wait for KEEP SV
  (docs/backlog.md).
- The round (`src/vs_match.*`, `src/battle/vs_battle.*`): retail's viewport layouts (`src/view/split_views.*`), one
  camera and one HUD per view (the 2- and 4-view tables, retail's empty quarter), who is in play, the YOU WIN / LOSE /
  DRAW marks, the end menu and its quit box in the world on every seat's pad, wins and losses by seat, the VS sound
  rules, and the result back to a rematch, the area page, the machine page or the title.
- The renderer draws several 3D viewports a frame (`FrameSetup::views`) and the 2D per view (`Quad2D::view`).
- `--vs N` and the headless pads (docs/input.md, "Headless pads") run it without hardware.

Online groundwork, built 2026-11-12 (every change-now item but 8, above):

- `world_hash` over every field and `world_fight_hash` for the replay, `--hash N`, `world_hash_diff` and
  `pc_sim --parts N` for finding where two worlds part.
- `pc_sim`: the world built and stepped with no window, GPU or sound; the viewer's fights and the front end's tournament
  fight (`--state 6`, with `PHANTOMCRASH_DEV_ENTRY` for a human entry), `--rollback N`, `--replay`, `--time`. It builds alone
  for other targets (`-DPHANTOMCRASH_SIM_ONLY=ON`), the start of a headless server build.
- The fight is built by `src/battle_world.cpp` (the game's battle session and `pc_sim` alike) and the viewer's by
  `src/viewer_world.cpp`.
- `fighter_slots` is one constant (`src/world/fighter_slots.hpp`) every per-slot array follows; a build can set 12.
- Fixed arrays in the stage, event steps, `MachineOptions`, `LobbyRules`, `hit_seat`, the data hash.

## Order of work

1. **Local split-screen VS.**
   - VS setup 0x2C with gubsy lobby seats.
   - KEEP SV and VS builds.
   - Split viewports and the 2- and 4-view HUD tables.
   - The VS battle rules and end sequence, and the VS sound rules.
   - Per-seat config (items 2, 3; 6 settled as one view's projection; 11 is `World::rules` beside `World::vs`).
   - Before it: the world hash and the GCC/Clang check (item 1), so VS is deterministic from its first day. Done.
2. **Online VS.**
   - `src/net` from MIMH: sockets, gubsy-roomd rooms, punch and relay, clock, rollback, host frames.
   - The battle start message and the 21-byte seat input.
   - The handshake (wire version, sim stamp, data hash: item 12).
   - Hash checks and host-resend recovery.
   - The GView room screen, team VS, optional AI seats.
   - Leaving mid-round; pause as an overlay.
   - Needs items 4, 5, 9, 10 done (they are, 2026-11-12).
3. **Co-op tournaments.**
   - The partner seat in the host's roster.
   - The shared human team, the every-human end, and spectating after death.
   - Results, records and saves per seat (item 8).
   - The guest's save image beside the host's world, the partner menu, and conversations on both screens.
   - Local split-screen co-op comes along with this.

## Open questions

1. Co-op as "hire a partner": the host's campaign, the guest bringing their own machine and keeping their own money and
   records, no story progress for the guest. Recommended: yes. Both other shapes need a second economy or two campaigns
   reconciled.
2. The mouse cap online. Settled (owner, 2026-11-11): mouse aim always at the mech's turn rate, in every mode.
3. Lock-on width in two-player split. Settled (owner, 2026-11-11): one view's projection for every player in every
   mode; lock-on range is game balance (item 6).
4. The partner's slot in a tournament. Recommended: the partner replaces one opponent slot in 6-slot tournaments and
   adds a fourth slot in the 3-entrant ranker bouts. Opponent queues are unchanged, so the fight is the same size and
   the money is split by who shoots.
5. The guest's save written automatically after each co-op battle, against retail's "never saves by itself".
   Recommended: yes, for the guest only, to the slot they joined from. The host saves as retail.
