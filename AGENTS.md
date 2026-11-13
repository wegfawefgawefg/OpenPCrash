# Phantom Crash (PC)

A PC reimplementation of Phantom Crash (Genki, Xbox, 2002) on SDL3 and Vulkan. Private repo. It has to play the
same as the original: nothing may feel different except the obvious improvements (resolution, frame rate, load
times, cleaner menus and art). Gameplay is ported, not redesigned.

The spec is the matching decompilation at `../phantom-crash-decomp`. Its `docs/port/` (architecture, data, render,
gameplay) and `docs/findings.md` say what the original does and where; every function there compiles to the
original's bytes, so its C is the ground truth when a doc and the code disagree. Read the decomp, never copy its
matching hacks (volatile stand-ins, identical arms, goto runs): write the plain version of what the code does.

## Style

Follow the C+- style of Mans In My House, Splonks and Teeming (`~/Coding/AGENTS.md`):

- One top-level `State` in `src/state.hpp`. `main.cpp` runs the loop in plain paragraphs with terse lowercase
  comments (`// pump input`, `// run fixed steps`, `// draw frame`). Graphics, audio and the Gubsy runtime are
  locals in main. No `App` struct, no `game.hpp`, no `utils`.
- Plain structs, free functions, explicit dispatch (a switch, not virtual calls). Modest abstraction.
- Files of 300-500 lines in flat domain folders (`world/`, `mech/`, `battle/`, `ai/`, `render/`, `menu/`,
  `data/`, `audio/`...). File names say what they hold.
- Terse "what" comments above code paragraphs. The shared `.clang-format` (LLVM base, 4 spaces, 100 columns,
  left pointers, braces on every if).
- Use the owner's libraries where they fit, pinned like MIMH's `cmake/deps.cmake`: gubsy (SDL host, input,
  bindings, settings, saves), gfxp (fixed point), gsexp (s-expression data files). Local checkouts sit beside
  this repo on disk 2 (`../gubsy`, `../gview`, `../glayout`, `../gfxp`, `../gsexp`).
- The original's screens (title, garage, shop, chips, tournament board, town map, results, name entry...) are
  ported as game code in `screens/`, laid out from the original's UI layout data converted to `.sexp`, so they
  look and behave like the originals and their art can be redrawn later. New screens the original doesn't have
  (PC settings, key bindings, later the online lobby) are built in the Phantom Crash style from the original's
  panels, frames, fonts, cursors and sounds, so they don't look out of place; GLayout may help with layout math, but
  never GView's default look.
- Data the game is tuned by lives in `.sexp` files under `assets/`, converted from the original's tables by
  tools in `tools/`, never typed in by hand from memory.

The code reads like game code, with a trail back to the decomp:

- Every ported function has exactly one `// decomp: name 0xADDR` line directly above its definition (several
  pairs on that line when one port function covers several, `// decomp: a 0x1234, b 0x5678`).
  `tools/decomp_index.py` lists port symbol, decomp name and address from those lines (`--missing` checks them
  against the decomp's symbol tables). Other comments name decomp functions without addresses.
- Comments are terse "what" comments. The 60 Hz conversion comments (Simulation below) and the retail-bug notes
  stay in full.
- No bare hex for meanings: application states (`world/app_states.hpp`, `app_title`), motion states, modes and
  event bits (`mech/fighter_rates.hpp`), scene states (`battle/scene_states.hpp`), bark kinds, flag words
  (`render/mesh_flags.hpp`, `talk/draw2d.hpp`'s `sprite_*`, `garage_*`...), sound banks and cues
  (`audio/sound_cues.hpp`, `ui_sound(ui, bank_sys, se_menu_accept)`). Each definition carries the original
  value in hex and its decomp name, so a search for the hex finds it. Plain numbers (coordinates, colours,
  chances, counts) stay numbers.
- Port names, not the decomp's object names: a fighter's functions are `fighter_*` (the decomp's `actor_*`
  stays in the `decomp:` line), fields get real names (`unused_06` for one nothing reads), never `f_1a4`.
  Converted data keys follow: the converters write real names and keep the original's beside them (the UI
  tables' `(decomp D_...)` next to `(va ...)`, `tools/ui_tables.py` `TABLE_NAMES`).
- `src/screens/` is split by area: `flow/` (the AppState machine and the screens' shared state), `ui/` (the
  drawing and input kit), `title/`, `system/`, `garage/`, `shops/`, `campaign/`, `vs/`, `pc/`.

Do not add tests, test suites or automated gameplay checks (the owner's rule for his games). Builds, headless
captures and play sessions are the check.

## Simulation

- 60 Hz fixed step with render interpolation. The original steps battles at 30 Hz (menus at 60) and counts most
  timers in 60 Hz ticks that step by 2. Every constant changed for the 60 Hz step carries a comment with the
  original value, its address or decomp name, and the conversion (per-step velocities halve, per-step
  accelerations quarter, 60 Hz tick timers count by 1 instead of 2, update-count timers double). When a
  conversion can't give the same result (integration order, collision sweeps, caps applied per step), say so in
  the comment and in `docs/sixty.md`.
- `state.world` is the deterministic simulation for later online play: a copyable value changed only by
  `step_world(World&, const Inputs&)`. No SDL, floats, audio, rendering or wall clock in the world. Fixed point
  (gfxp) and table trig, one random generator owned by the world, total orders for every sort. The original's
  cosmetic code that draws from the gameplay RNG must keep doing so inside the step (decomp
  `docs/port/architecture.md`, "Randomness").
- Presentation (`state.view`) reads world events and interpolation poses and never writes back. No delta time
  in gameplay; overload must never turn into slow motion in the port (run steps to catch up, cap the backlog).
- The original's task order (parent first, siblings by priority, ties by creation) decides gameplay order; keep
  the same order in `step_world`.
- `world_hash` covers every `World` field: a new field goes into `src/world/world_hash*.cpp` (their size guards fail
  the build until it does). `pc_sim` (`cmake/sim.cmake`) steps the world headless for the crossplay hash check, the
  rollback and replay checks and the step timings (`docs/numbers.md` "Determinism").

## Backlog

Full speed, no blockers. Minor issues, open questions, small deviations and polish go into `docs/backlog.md` as
one-line tasks grouped by area, and work carries on; only real trade-offs that change how the game plays go to the
owner.

## Retail bugs

Don't recreate the original's bugs (owner, 2026-11-11). The decomp docs list "retail bugs to keep"; read those
lists as bugs to judge, not to copy. Fix a bug in the port unless it shapes the numbers players know and play by,
such as a gun's damage, a rate of fire, a reach or an AI's habits that set a fight's difficulty; those stay as
retail has them. Either way, every bug goes in `docs/bugs.md`: what retail does (decomp name and address), whether
the port fixes or keeps it, and why. When unsure, keep it and add a line to `docs/backlog.md`.

## Data

- The original disc data is read from an extracted disc, by default
  `../phantom-crash-archive/assets/raw`, and the original XBE at
  `../phantom-crash-decomp/orig/default.xbe`. Never commit game assets, extracted files, converted textures or
  anything taken from the disc or the XBE. Converted caches go in `data/` (gitignored) or the build dir.
- Converters in `tools/` turn the original's tables into `.sexp`; the converted `.sexp` is game data, so it
  stays out of git too unless it's our own content.

## Running

- Never run the game on the owner's display. Headless captures use Xvfb with lavapipe (software Vulkan); a new
  shader is validated on lavapipe before any real-GPU run (a GPU hang froze X on 2026-11-08).
- Kill only by pid. Disk 2 only. `rm` only with literal paths. No time estimates.
- Be kind to the owner's machine; he uses it while agents run (2026-11-12: load 252 on 32 cores from parallel lavapipe
  captures and a load test). At most one game instance per agent at a time, under `nice -n 19` with
  `LP_NUM_THREADS=2`; builds `nice -n 19 ... -j4`; no busy-loop load tests except briefly under `taskset` on 1-2 cores.
- Every headless run sets `XDG_DATA_HOME` to a scratch folder. The game moves an old `PhantomCrash/PhantomCrash`
  user folder to `OpenPCrash/OpenPCrash` on start, and the owner's own folder must only move when he runs the game.
- In a worktree, read the main checkout's `data/` (`--data`) but cook into your own `cooked/` (`--cooked`): worktrees
  with different pack versions sharing one `cooked/` keep re-cooking each other's packs.

## Commits

Commit finished, building chunks often; check `git diff --cached --stat` lists only your files.
