# Playthrough

The single-player campaign run headless from New Game to the credits and on into the post-game, what each state does
now, and the dev hooks that reach any day or fight quickly when playtesting. Run 2026-11-12 on lavapipe in a private
Xvfb (`scripts/capture.sh`'s setup), `--no-dialogue`, dummy audio.

## How to get anywhere

### Command line

| flag | what |
|---|---|
| `--state HEX` | enter an application state from black on a new game's day 0 (the dev hooks below still apply) |
| `--route FILE` | presses, clicks, shots and fight actions keyed to the front end's states (below) |
| `--battle-script FILE` | seat 0's pad in every fight from a pad script (`src/input/script_input.hpp`), from the fight's first step |
| `--press U:KEYS`, `--click U:X:Y`, `--hover U:X:Y` | a press, click or hover at front end update U |
| `--no-dialogue` | story conversations and battle chatter skipped (kept in the gubsy profile) |
| `--seed N` | the world generator's index at boot |
| `--capture out.png --frames N` | run N frames and write the last; `--route`'s `shot:` and `quit` write others |

### Environment

| variable | what |
|---|---|
| `PHANTOMCRASH_DEV_DAY=N` | day N with its line-up drawn, as a night leaves it (also over `--state`'s day 0) |
| `PHANTOMCRASH_DEV_WON=ids` | those tournaments won; `story` is the 24 class tournaments and the three Area Rankers (the First Ranker is then open on days 0, 15, 30, 45), `classes` the 24 alone |
| `PHANTOMCRASH_DEV_BEATEN=1` | the game beaten: the post-game (Burning Chrome in 49's place, a cycle counted at each 59 -> 0) |
| `PHANTOMCRASH_DEV_BUILDS=row,...` | used Scoobees into garages 0.., garage 0 boarded (row 0 enters every Unlimited class) |
| `PHANTOMCRASH_DEV_MONEY=N` | the money, in units of 10 NY |
| `PHANTOMCRASH_DEV_ENTRY=row` | with `--state 6`: the player's build row in today's fight |
| `PHANTOMCRASH_DEV_FAST=N` | a capture steps N world steps a drawn frame (1..8): a fight at 8 runs several times real time on lavapipe |
| `PHANTOMCRASH_LOAD_SLOT=N` | load save slot N before the first state |
| `PHANTOMCRASH_SAVE_DIR`, `PHANTOMCRASH_GUBSY_ROOT` | where saves and gubsy's files go (keep headless runs out of your own) |
| `PHANTOMCRASH_STORY_DUMP=path` | the story and the whole save image written at shutdown |
| `PHANTOMCRASH_DEV_DURABILITY`, `_PAUSE`, `_TROPHY`, `_CARRY`, `_DROP`, `_SONGS`, `_PAD_LOST`, `_BATTLE_LOOP`, `_SHOTS` | the older capture hooks (docs/screens.md and their code) |
| `PHANTOMCRASH_DEV_ROUTE_SLOTS=1` | `--route`'s `win` prints every fight slot each time it acts |

The save files are text (`<save dir>/slot<N>.sexp`, docs/saves.md): `(story (day N) (money M) (beaten true) ...)`,
`(tournament-won ...)` (56 values, 1 won) and `(events-done ...)` can be edited by hand. The rosters in the file are the
saved day's: after changing `day`, sleep once in the garage (or use `PHANTOMCRASH_DEV_DAY` on a new game) so the line-up
matches.

### Routes

`--route FILE` (`src/input/dev_route.hpp`): one step a line, `STATE WAIT ACTION`, taken in order. A step waits until
the front end is in STATE (hex, or `*` for any) with the fade done, WAIT updates after it entered the state or took the
last step. Actions:

- pad buttons for one update: `a`, `b`, `x`, `y`, `start`, `back`, `up`, `down`, `left`, `right`, `lt`, `rt`, or a hex
  word, joined by `+`; `start` also pauses a fight;
- `click:X:Y` (640x480);
- in a fight, through the world's input (`Inputs::dev`, so a replay repeats them): `kill` (seat 0 kills the fixed
  opponent if it is in, else one not beaten yet, else any), `killall`, `die` (to its nearest opponent), `door` (seat 0
  stands in its nearest gate's entry point: the door scene). Kills are damage queued from seat 0 and settled as any
  hit is: the records, the beaten bytes, the replacements, the boss scenes all follow;
- `win`, `win:N`: every WAIT updates, kill until the tournament's fixed opponent is down (or N kills, 30 by default),
  then the door; seat 0 takes no hits meanwhile. The step holds until the fight's state goes;
- `every:A,B,...`: every WAIT updates the actions A, B (pad words, `kill`), until the state goes: a lesson's lines
  accepted (`every:a`), its drones shot down (`every:a,kill`);
- `shot:NAME` (a bare name lands beside `--capture`'s picture), `pose` (seat 0's place, heading, pitch, twist),
  `quit`, `-` (nothing: waits for the state);
- `include FILE`.

Every route line taken and every state change is printed. Two scripts used below:

    # a new game from the title to the town map (--state 15 --no-dialogue)
    15 40 a          # press start
    15 30 a          # NEW GAME
    19 90 a          # opening.sfd
    19 90 a          # opening02.sfd
    16 40 b          # name entry: to the End page
    16 20 a          # End: the default name
    13 60 b          # help menu: the cursor to End
    13 20 a          # End: the town map

    # a day, from the garage hub after the night (one tournament in area AREA)
    d 100 left       # the hub's wheel: Hangar -> Exit
    d 20 a           # the town map
    29 90 left       # the cursor from the garage to Club Wired
    29 30 a
    8 90 a           # Club Wired: the area select
    3 90 -
    3 15 right       # once per area (0 Shinjuku, 1 Shibuya, 2 Aqualine)
    3 20 a
    3 60 a           # ENTRY (down + a for RENTAL)
    3 60 a           # gate 01
    6 60 win:80
    1e 200 b         # the result: the rolls finished
    1e 30 b          # the cursor to EXIT
    1e 20 a
    18 0 -           # the night; the garage follows

Sleeping instead: `d 100 left`, `d 20 left`, `d 20 left`, `d 20 a` (the hub's Sleep). A fight script that walks away
from the gate after the entrance (`600 0 0 0 0 0`, `180 0 1 0 0 40`, `1 0 0 0 0 0`) keeps the human out of its own
gate's zone, where AIs entering by that gate would wait.

### Jumping to a fight

- Any day's tournament: `PHANTOMCRASH_DEV_BUILDS=0 PHANTOMCRASH_DEV_DAY=13 phantomcrash --state 3` (Shinjuku's ranker on
  day 13 needs `PHANTOMCRASH_DEV_WON=3,4,5,6,7,8,9,10`).
- The First Ranker and the ending: `PHANTOMCRASH_DEV_BUILDS=0 PHANTOMCRASH_DEV_WON=story PHANTOMCRASH_DEV_DAY=0
  phantomcrash --state 3`, then Aqualine.
- The post-game: add `PHANTOMCRASH_DEV_BEATEN=1` (Aqualine's slot on days 0/15/30/45 is Burning Chrome); day 59 and a
  Sleep shows the cycle wrap.
- Free play and the lessons: `--state 13` (the help menu: FreePlay, and About Scoobees' three lessons).
- The test drive: `--state e` with `PHANTOMCRASH_DEV_BUILDS=0`, the garage's menu, Test-drive.

## The route that was run

1. `--state 15`, the prologue above: title, New Game (0x1C -> 9 -> 0x2E), the opening movies (0x19), talk 0x30, name
   entry (0x16), talk 0x31, the help menu (0x13), talk 0x32, the town map (0x29), talk 0x5B (the letter), Club Wired (8),
   the area select (3).
2. 46 days by a greedy plan (a class tournament not yet won each day, an Area Ranker once its area's eight
   are won, the First Ranker once the three are), `PHANTOMCRASH_DEV_BUILDS=0`, `win:80` every 60 updates,
   `PHANTOMCRASH_DEV_FAST=8`: 28 fights (the 24 class tournaments, Rocket Summer on day 13, 12 Door on 33, Breaking
   Strain on 38, Tokyo Snow Day on 45) and 18 sleeps, every fight won, 27 result screens, 46 nights, and the 67 story
   conversations the run's transitions raised (0x30..0x68 and the one-offs 0x82..0x97) resolved and skipped.
3. The First Ranker's bout: her entrance (scene 5) on the main view, her defeat (scene 0x17), the ending's scene (0xA),
   the ending movie (0xB), the credits (0x25: the beaten bit), the secret movies (0xC, the credits' 30 s ran out), the
   night (day 46) and the garage, beaten: 958 s of wall time on lavapipe at `PHANTOMCRASH_DEV_FAST=8`.
4. Separately (each from `--state` with the jumps above): a Free Class rental (1), Master Class (15), Rental Use Only
   (17), Enemy Drones Only (34), Holy Restricted (45) with row 0; No O.C. (50) with row 1 and Photon Restricted (11)
   with row 10 (the Plain Holy of row 0 is refused there, with its camouflage and its body, as it should be); a night
   Free Class (2); Burning Chrome after the game (53 drawn); the 59 -> 0 wrap (cycle 1); a death in a tournament with
   its replay and the result again; the hangar's test drive left by the door; plain free play quit from its pause;
   the three lessons run to their ends ("The lessons" below); a save from the
   system menu and its load; beginner mode's flick turn and pitch spring (`pose`).

Battle memory over the 56 fights of step 2 (28 fights and 28 result-screen demo battles): resident 368, 504, 523 MB
over the first three, then 527..561 MB, flat from the twentieth on.

## What each state does now

| state | what happens | leads to |
|---|---|---|
| 0x9E, 0x12, 0x2D, 2 | logos and the attract movie | 0x28 |
| 0x28, 1 | press start; a minute away runs the attract demo battle | 0x15, or 0x9E |
| 0x15 | title: New Game (runs `game_state_new` again), Load, VS, Options | 0x1C, 0x11, 0x2C, 0x1A |
| 0x1C, 9, 0x2E | the new game's checks (no screens) | 0x19 |
| 0x19 | the two opening movies, each skipped by accept after a second | 0x16 by talk 0x30 |
| 0x16 | name entry; B goes to the End page, A there ends | 0x13 by talk 0x31 |
| 0x13 | the help menu: topics (talks), FreePlay and the three lessons (0x2A); End | 0x29 by talk 0x32 the first time, else Club Wired |
| 0x29 | the town map, the cursor on the spot left last | 0xD, 8, 0xF, 0x1B, 0x14, 0x22 |
| 8 | Club Wired: area select, help, records, schedule, name entry, town map | 3 ... |
| 3 | area page (always Shinjuku first), job page (ENTRY, RENTAL, hangar, characters, info), gate select; refusals in boxes | 6 |
| 6 | the fight: 5 s, the entrance, the fight; the door, the player's death or a ranker's defeat ends it; the First Ranker's into the ending | 0x1E, or 0xA |
| 0x1E | result: rolls, the money, REPLAY / EXIT; the demo battle behind it | 5, or 0x18 |
| 5 | the replay of the last fight (views by left and right, A back) | 0x1E |
| 0x18 | the night: day + 1, the line-up drawn, nothing drawn | 0xD |
| 0xD | the garage hub's wheel (hangar, music, stock, records, schedule, sleep, system, exit) | ... 0x29 |
| 0xE | the hangar; a garage's menu: ride, test-drive, specs, assemble | 0x2F ... |
| 0x2F | the test drive (tournament 47), left by the door or a death | 0xE |
| 0x2A | free play / a lesson; its pause quits, a death, the door or the lesson's end ends it | 0x13 |
| 0x27, 0x1F | system menu, save (three slots, the overwrite question) | |
| 0xA, 0xB, 0x25, 0xC | the ending's scene, the ending movie, the credits (sets beaten), the secret movies | 0x18 |

## The lessons

The help menu's About Scoobees holds the three lessons (rows 14, 15 and 16: `free_play_lesson` 0, 1, 2); each is free
play (0x2A) on its chain of scene states (src/battle/battle_tutorial.cpp) and ends in phase 6, back to the help menu.
Each has a route and a pad script for seat 0 in the main checkout's `captures/lessons/` (`gen.py` writes them):

    PHANTOMCRASH_DEV_FAST=8 phantomcrash --state 13 --no-dialogue \
        --route captures/lessons/lessonN_route.txt --battle-script captures/lessons/lessonN_pad.txt

The route walks the help menu to the lesson (`13 60 down`, `13 10 down`, `13 10 down`, `13 10 a` into About Scoobees,
`13 30 down` plus one `13 10 down` a lesson past the first, `13 10 a`) and then accepts the lines with `2a 60 every:a`
until free play ends (`every:a,kill` for lesson 1). Run 2026-11-12 on lavapipe, each back in the help menu:

| lesson | states | the pad script | ended |
|---|---|---|---|
| 0, the basic moves | 0x18, 0x19, 0x2D, 0x2E, 0x1A..0x1F, 0x22 (the default config is Standard: 0x2D/0x2E strafe in place of 0x20/0x21's twist) | every move cycled: forward and back, strafe each way, a full turn each way (right stick), a run (left thumb), a jump, a held jump, two steps each side (black, white) | update 10984 (11044 since the dodge step counts one a dodge, as retail: 2026-11-12) |
| 1, the attacks | 0x23..0x29: 3 drones with each weapon, then 10 | standing; the route's dev kills | update 5714 |
| 2, the camouflage | 0x2A..0x2C: hidden from the second fighter | X every 130 steps | update 1284 |

Nothing stops a lesson: each step's count passed once its move ran while the step had opened its controls. Lesson 0
takes long only because the pad script cycles every move whatever the step. `lesson0_mid_before.png` and
`lesson0_mid_after.png` are lesson 0's first step before and after the HUD shows in free play's scenes (below).

## Found and fixed on the way

- The lessons' drones never came: free play forced the rumble's entrants to 1 (2 for lesson 2) after the fill, so
  lesson 1 had nothing to shoot and couldn't end. Retail's `free_play_screen_update` sets none: the fill's 6 for the
  lessons' tournament 55 stand and its empty slots draw the drones (`src/screens/title/ending_screens.cpp`).
- A New Game from the title ran `game_state_new` after the boot's dev garages were put in, so
  `PHANTOMCRASH_DEV_BUILDS` and `_MONEY` were lost on the way into a campaign (now applied again after it).
- The shield bubble's texture (`systemdata/muteki01`) wasn't cooked into the battle pack (pack version 8).
- Retail behaviour ported on the way: beginner mode's camera, the lessons' empty lines, the barks' pads and rental
  names, the idle bark's pad, the beam against the supply craft, the rails' prop test (docs/backlog.md).
- The HUD's gate (2026-11-12, `hud_update` 0x4E62E): retail draws none for a paused slot (`battle_slot_bookkeeping &
  0x24`), so the First Ranker's entrance shows no HUD under the letterbox (the port drew it:
  `captures/sp-polish/boss_entrance_before.png`, `_after.png`); and in free play (phase 4) it shows in every scene but
  the door and the intro, so the lessons have it (the port hid it there).

Nothing crashed, hung or went to a wrong state on the route above.

## Captures

In the main checkout's `captures/playthrough/` (not in git): `door_scene.png` (the door scene's shot, `--route`'s
`door`), `boss_entrance_a..c.png` and `boss_sheet.png` (the First Ranker's entrance, scene 5, on the main view),
`ranker_defeat_a..c.png` and `rk_sheet.png` (Rocket Summer's defeat: the rail within 10 and the line waited out),
`ending_scene.png`, `death_c.png` (the killer's moment), `replay_b.png`, `td_b.png` (the test drive),
`after_credits.png` (the garage on day 46, beaten).

## Left for single player

- The headless route's own gaps: the dealer, shops, Sonic Amp and media player were exercised by their own earlier
  captures, not this route.
- ~~The big screens' live picture~~ (2026-11-12): mesh 0x80000 samples the last frame's scene (`texture_scene_last`);
  `captures/sp-polish/bigscreen_before.png` and `_after.png` (`--camera 111.8,16,130,0,0 --frames 3`).
- ~~Shinjuku set 1575's `07.png` reported missing~~ (2026-11-12): it is the gates' bump map (a normal map, A8R8G8B8 on
  the disc), named only by the gate sections' second stage, so the prop set's load skipped it; the gates on the four
  day stages now load theirs (docs/backlog.md "Stages"); `captures/footsteps_shinjuku/gate_before.png` and
  `gate_after.png` (`--camera 111.2,4,138,0,0 --frames 3`).
- docs/backlog.md's checks against the running original wait for emulator runs.
