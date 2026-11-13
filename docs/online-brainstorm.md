# Online play: brainstorm

Ideas to react to, not a spec. Written 2026-11-11 against 44517a7, after the owner turned down `docs/multiplayer.md`'s
"hire a partner" co-op as too limited and asked for co-op campaign, dropping into each other's fights, persistent arenas,
and an honest look at whether rollback is the right netcode for all of it.

What still stands in `docs/multiplayer.md`: local VS, online VS, team battles from the team rule, the 21-byte seat input,
the handshake, the hash checks, the lobby on gubsy-roomd, and the twelve "change now" items. What this reopens: "The
campaign with two players", open question 1, and "late join mid-battle isn't needed" (drop-in and arenas need it).

Short version:

1. The original has no player level and no difficulty setting. A pilot's strength is their machine (parts and tunes), and
   a fight's strength is which tournament it is. So "level normalization" means machines and tournaments, not levels.
2. A retail tournament battle is already a drop-in arena. It has no timer, opponents keep walking in through gates as
   others die, and the player cashes out by walking back into a gate. Drop-in co-op and persistent arenas both grow out
   of that loop with few new rules.
3. Co-op campaign: a **party career** with one shared day, shared tournament progress, and a wallet, garages and records
   per member. Each member fights once a day, together or apart, and anyone can drop into a party member's fight.
4. Drop-in outside a party: join a friend's fight in their world, with a plain warning screen and a rental option. You
   keep your money and records. Their progress stays theirs. Optionally join as a rival.
5. Arenas: many small rooms, not one big one. The stages hold about 8 to 12 fighters, so an arena is 8 humans at most,
   with AI filling up to 12. Bracketed by machine (tune clamp and machine value), not by campaign progress.
6. Netcode: deterministic input sync everywhere, with two kinds of host. A player's machine hosts casual VS, co-op and
   drop-in (MIMH's rollback, as `multiplayer.md` has it). The VPS hosts arenas and ranked, running the same `step_world`
   headless and ordering everyone's inputs. Server-authoritative state sync, which most action games use, buys little
   here and costs a lot; reasons below.
7. Local saves are editable text, so nothing competitive may trust them. Fairness comes from rules checked on the build
   itself, and rankings from the server re-running the inputs.

## What the original gives us to build on

These facts decide most of the design. Sources are the decomp's `docs/port/progression.md`, `battle.md`, `ai.md`,
`parts.md`, and this repo's `docs/numbers.md`.

**No levels, no difficulty.**
- There is no player level or experience (progression.md "Rank"). Class A to D are entry fees (5000, 1000, 500, 0).
- A machine's strength is its parts at their tunes (-99..99) through the stat curves. A positive tune adds durability,
  weight and damage and costs speed. Some weapons have a cliff at 99: the Pistol/HM15 does 900 at tune 98 and 15000 at
  99 (parts.md).
- An opponent's strength is its build row's tunes. Its level barely matters in quest fights: the level bug gives every
  quest AI level 0's cone, fire pacing and dodge bands. Demo battles get level 99 everywhere and are "far sharper".
- New Game+ already has an opponent scaling knob. `opponent_level_bonus` adds `completed_cycles` to every opponent's
  level and part tunes, capped at 99. Pay is worked out before the bonus, so harder opponents pay the same.

**Days and progress.**
- A day is one tournament out of the day's three (one per area). Night advances the day. The 60-day schedule loops
  forever with no deadline, and every tournament comes round again.
- The only locks are the ranker gates: all eight class wins of an area open its ranker, and all three rankers open the
  First Ranker. Shops, dealers and side tournaments are open from day 0. Shop stock is a function of `day % 60` alone.
- Progress is written on "a kill by a human": the victim's ticket byte (a global opponent row) and, for a fixed opponent,
  `tournament_won[t]`. Opponent rows are the same in every save, so another pilot's save could take the same credit.

**The battle loop.**
- Retail battles have no time limit. Opponents enter through gates from the queues and the reserve pool as others are
  retired, 150 updates after a draw, one at a time. The human ends it by walking back into a gate (cash out), by dying,
  or by killing a ranker.
- The prize is the score: destruction values with streak doubling, plus crates. It is paid on death too, and halved for a
  rental. Dying costs body and leg repairs on top of broken weapons.
- Entrants are at most 6 (`fighter_slots`). Gates per stage: Shinjuku 3, Shibuya 2, Aqualine 2, training stage 4.
- Teams: two fighters on the same team (not -1) can't lock on to each other and their shots pass through. Most pilot
  records have a team, so AIs from one team never fight each other.
- Rentals are allowed under rules 0, 4 and 25. Rule 25 is "Rental Use Only". Rules 19 to 21 have weight limits and are
  never scheduled. Rule 27 is No O.C. (no camouflage).

**Single-human assumptions that break with several humans** (all read in battle.md):
- After a human is killed, its killer is frozen and intangible for 840 ticks while the death camera shows it. With
  human killers that takes the killer out for 14 seconds.
- A boss entrance parks every other fighter. A boss defeat parks everyone for at least 180 ticks.
- Replacements don't come while any human is in a scene (intro, door, death). With eight humans entering and dying, that
  can hold replacements back forever.
- Pause stops the battle; the letterbox and intro cameras assume one viewer.

**Stages** (collision bounds, numbers.md):

| stage | size | gates | retail fighters |
|---|---|---|---|
| Shibuya | 182 x 189 | 2 | 6 |
| training stage (test arena) | 213 x 213 | 4 | 6 (VS 2..4) |
| Shinjuku | 243 x 244 | 3 | 6 |
| Umihotaru / Aqualine | 529 x 170 | 2 | 6 |

AI sight is 100 units and the melee lock range is 25. On Aqualine fighters can lose each other along the long axis.

**Saves** are `.sexp` text on each player's disk (`docs/saves.md`). Anything in them can be edited by hand.

## Modes

### Co-op campaign: the party career

**How it plays.** Two to four friends start a party career. It has one day, one story and one set of tournament
progress, and each member has their own wallet, garages, inventory and records. On a day, each member does what retail
lets one pilot do: shop, tune, then fight one tournament or sleep. Members in the same tournament fight it together.
Members in different tournaments fight separately, and anyone done for the day can drop into a party member's fight
(next section). Night comes when every member has fought or slept, and everyone sees it together.

**What it builds on.**
- Menus aren't synced (multiplayer.md), so each member walks the town map, shops and garage on their own screen. Only the
  day changing and the battles are shared.
- Joint battles use the team rule. Humans share a team id, so there is no friendly fire and no lock-on between them.
- Opponent slots work as in multiplayer.md's co-op: a partner takes an opponent slot in 6-slot fights.
- Pool and win progress is written on any human's kill, as retail already does.

**Progress and economy.**
- Shared: `day`, pools, `tournament_won`, the ranker progress, the day's line-up and drawn rosters, the beaten bit and
  cycles. When the eighth Shinjuku class win comes, the ranker opens for everyone.
- Per member: money, garages, inventory, colours and stickers, records, the music player, and `events_done`.
  Conversations only set their own done bit (story.md), so per-member done bits let every member see every
  conversation at their own next transition.
- Shops: everyone sees the day's stock and buys with their own money. Sonic Amp's nightly roll is party-wide.
- Map-spin money: per member.
- Entry fees: whoever enters pays. In a joint fight each member pays for themselves, and a member can rent if their
  machine fails the rule.
- Prize: each member's own score, minus their own repairs, as multiplayer.md "Results per seat".

**Open problems.**
- **Where the party save lives.** On the host's machine means the host has to be online for anyone to progress. On the
  VPS means anyone can play the party's day at any time, but that needs a server store and makes the VPS part of the
  campaign. A middle way: the host's machine is the truth, and every member gets a copy at each night, so any member can
  take over hosting.
- **Absent members.** Does the day wait for them? Probably a "sleep for them" vote, or the day advances when present
  members are done, and an absent member just loses that day's fight. Retail has no deadline, so losing a day costs
  little.
- **Splitting a party.** Leaving takes a copy of your wallet and garages into a personal save, with the party's story at
  that point. A member can't merge back without a rule for whose story wins.
- **One fight a day is retail's pacing.** A party member who already fought and drops into someone else's fight: does
  that use anything up? Lean: no. Helping costs nothing and pays the helper's own score.
- **Save format.** The party save needs a new kind: one `story` block plus N member blocks. `docs/saves.md` gets a
  second reader, and single-player saves stay as they are.

### Drop-in to a friend's fight

**How it plays.** From the town map (or a friends list), a friend's battle shows as joinable. You pick a machine and join.
You enter through a gate as retail's replacements do, with the 9-second intro and immunity, so a mid-fight entrance
already looks right. You leave as anyone does: through a gate, or dead. The fight is the owner's: their tournament,
their opponents, their world.

Inside a party this is just the party mode. Outside a party it is a visit, and the warning screen matters:

```
Rin is fighting FAHRENHEIT 451, Shinjuku, Class A, day 23.
4 opponents left in the queue. Opponents' machines: about 310,000 NY, tunes up to +40.
Rin's machine: 412,000 NY.   Yours (HOLY-02): 38,000 NY.
Your machine passes the rule.               [ Join ]  [ Join with a rental ]  [ Watch ]  [ Back ]
```

The numbers on that screen are what the game actually has:
- the day and tournament;
- the rule, and whether your machine passes it (`job_entry_allowed`);
- what's left in the queues;
- machine values: every part priced at its tune through the price curve, as the shops already do (see "Fairness" for
  whether price is a good measure);
- the opponents' tunes from their build rows, plus the NG+ bonus.

There is no single "level" to show, so the screen shows the fight and the two machines, and lets you judge.

**Join as a rival (optional).** You take an opponent slot on the AI's side instead (team -1, everyone fights you). It is
an invasion into a friend's tournament. Your kills of the owner are scored like any kill, and the owner's kill of you
pays your destruction value as if you were an opponent. Friends only, and the owner can turn it off.

**What the joiner earns.**
- Money and records: their own score and repairs, into their own save. Retail pays prize on death too, so a weak joiner
  carried through a Class A fight gets rich fast. Against friends, accept it. If it matters, cap the joiner's prize at
  the destruction values of the tier they could enter themselves, or force rental pay (halved).
- Opponent credit: the opponents they shoot down could be marked beaten in their own save, since opponent rows are
  global. Then pools fill in both saves.
- Tournament wins: not credited. You win your own tournaments. Without this, a joiner's ranker gates could open from
  a friend's fight.
- The owner's progress is untouched by the joiner, except that the joiner's kills count for the owner's pools, as in
  any co-op fight.

**Normalization options, weakest to strongest:**
1. Warn only (the screen above). Lean: this is enough between friends.
2. Rental: join on the tier's rental build, with prize halved. It already exists for rules 0, 4 and 25; extend it to
   any visit.
3. Scale the opponents: a "party bonus" through the NG+ mechanism (tunes plus N, capped at 99) when the strongest human
   machine is far above the tournament. Pay is unchanged, as NG+. This is the original's own lever, so the numbers stay
   the game's.
4. Scale the joiner: clamp the joiner's tunes to a band around the tournament's opponents (see Fairness). It works
   through the same curve readers, but can push a build over capacity (the overweight cliff), so the screen has to show
   the clamped machine first.

**Open problems.**
- **Late join.** It needs the confirmed world sent to the joiner (about 241 KB raw, mostly an empty shot pool). MIMH
  does this with chunked checkpoints and a future join tick.
- **The fighter slot.** In a full 6-slot fight, the joiner waits for an opponent to be retired and takes the slot
  instead of the next replacement.
- **Ending the battle.** The owner leaving through a gate ends the battle for everyone, as now. The owner dying while a
  visitor lives: the battle could carry on until every human is out (multiplayer.md's co-op end). The owner's result is
  settled at their death either way.
- The single-human assumptions above: the killer freeze, boss parking, and replacements held during scenes.

### Persistent arenas

**How it plays.** A list of arenas, always open. You pick one and a machine from your garages (filtered by the arena's
bracket), and you join the gate queue. You walk in with the retail intro and immunity, and fight AIs and maybe other
humans. You leave through any gate to bank your score, or you die and pay repairs. Then you rejoin with any machine.
No rounds and no timer, which is how a retail tournament already plays.

**Kinds**, each with its own rule set:

| kind | humans | AI | built from |
|---|---|---|---|
| PvE | one shared team | endless, from a tournament's pools | any tournament's pools and reserve, so a "Class D arena", a "Master Class arena", a "Burning Chrome arena" |
| Free-for-all | everyone team -1 | filler, from a tournament's pools | VS rules, minus the round end |
| Teams | two human teams | optional filler | the team rule |

The tier of a PvE arena is just which tournament's pools it draws from. That is the original's difficulty, as data.
Ranker fights can be timed events: the tournament's fixed opponent (Rocket Summer's Roy, Breaking Strain, the Burning
Chrome three) comes in on a clock, with a bonus for the kill.

**Size.** Retail packs 6 fighters into Shibuya. A rough density guide from that, to be tried in play, not trusted:

| stage | fighters | humans at most |
|---|---|---|
| Shibuya | 8 | 6 |
| training stage | 10 | 8 |
| Shinjuku | 12 | 8 |
| Aqualine | 12 | 8 |

That means raising `fighter_slots` from 6 to 12. The `World` grows by about 6 x 10 KB, and the shot pool may need to grow
too. Gates are the throughput limit. A gate takes a few seconds to cycle, so 2 gates let in a few entrants a minute,
which is fine at these sizes. More entry points would be new data per stage.

"Many players" means many arena rooms, not hundreds in one. The stages are small and the original's fights are tuned for
about six.

**Respawn and scoring.**
- No respawn in place. Death ends your entry; you re-enter through the queue, as a new entrant. The 9-second intro with
  300 ticks of immunity is the spawn protection, already built.
- Score is retail's: destruction value, streak doubling, crates. The arena board keeps the best single entry per arena
  per week, plus kills.
- Leaving by a gate keeps the retail door scene (240 ticks, immune and intangible). It is a safe exit, which is fine
  because the score is paid on death too.

**Rewards back into the campaign.** Options:
- Pay arena money into your personal save, as a battle would. A save is yours and editable anyway, so farming only
  cheats you. Lean: yes, at retail pay. Halve it only if arenas turn out to out-earn tournaments in play.
- Records into your save, and the arena board on the server.
- No parts, unlocks or story progress. Retail never hands out parts in battle either.

**Rules that change for multi-human battles** (the single-human list above):
- The killer freeze: off when the killer is a human, or cut to the 180-tick camera without freezing anyone.
- Boss entrance and defeat parking: stays as retail, everyone freezes (owner, 2026-11-11: "it's cute").
- Replacements held during scenes: hold only for the scene that needs it (a boss entrance), never for intros, doors or
  deaths.
- Pause: an overlay that doesn't stop the battle, as online VS.

**Open problems.**
- An empty arena isn't stepped. "Persistent" means always available, and the server tears the world down when the last
  human leaves.
- Which machines are allowed: see Fairness.
- PvE arenas with AIs from one team: same-team AIs ignore each other, so the arena should draw from several teams to keep
  AIs fighting each other too, or not, as a rule.
- Score farming of weak AIs by maxed machines in a PvE arena: the bracket handles it (a maxed machine plays the Master
  Class arena).

### VS and teams

As `multiplayer.md` has it: local split screen exactly as retail, online VS for up to 4 humans, team VS from the team
rule, and optional AI seats. Nothing here changes it. Two additions that fall out of the arena work:
- VS rule sets (below) as lobby choices: "any build", "stock tune", "rental only", "machine value under X".
- Build codes: a VS build as a short text code (the build record and tunes as an s-expression) to paste into a VS lobby,
  where the rule set allows builds the player doesn't own.

### Ranked duels

1v1 (and maybe 2v2) on the VPS, with a fixed rule set: stock tune or a value cap, and an optional time
limit, since retail VS has none and two camouflaged machines can stall. Ratings are kept on the server.

The determinism pays here. The server has every input and runs the same `step_world`, so it computes the result itself
and doesn't have to trust either player's report. A 3-minute duel is 10,800 steps, about 3 to 4 seconds of CPU at the
measured 0.3 ms a step. Every ranked match is also a replay, for free.

### Tournaments run by players

A host sets a bracket of VS duels or a rumble with house rules. The rule fields are the original's own: body class, leg
classes, weight limits (rules 19 to 21 have them and were never used), No O.C., rental only, plus the online rule sets.
Spectators get confirmed inputs a few seconds late, which costs nothing under input sync. Each match keeps its replay.

### Garages, gifts, trading

- Between friends: gift a part or money from your save to theirs. With local saves this runs on trust (the sender's
  save drops the item, the receiver's gains it). Lean: yes, friends only.
- A market with strangers only makes sense with a server-owned economy (progression model C below). Lean: no.
- A party shares no garage. Each member keeps their own, but can gift within the party.

## Fairness

Where humans meet, what stops a maxed machine from a long career from wrecking a new player:

1. **Brackets by machine, not by progress.** Progress is in an editable save and says little anyway, since there is no
   level. The machine is sent at battle start, so the server can check it. Two measures:
   - **Machine value**: every fitted part priced at its tune through `shop_item_price`, which rises with tune either way.
     It measures investment, and is probably a fair proxy for power. Check it against the tune-99 cliff weapons first:
     if their price doesn't jump the way their damage does, value brackets need those parts listed separately.
   - **Tune band**: every tune clamped to a band (0, or plus or minus 30) at spawn, read through the same curves. The
     numbers stay retail's; only the tune input changes. "Stock tune" (all 0) is the strongest leveller and kills the
     99 cliffs. A clamped build can come out over capacity, so the entry screen has to show the clamped machine's stats
     and weight before you join.
2. **Separate rule sets per mode.** Co-op and visits: anything goes, warn only. PvE arenas: bracket by tier (tune band and
   value per tier). FFA, teams and ranked: stock tune or a value cap. Rental Use Only rooms already exist as a retail rule.
3. **Separate the humans.** PvE arenas put all humans on one team, so a maxed machine can only steal kills, not grief.
   PvP is opt-in by arena kind; there is no PvP flag inside a PvE arena.
4. **Mouse aim always at the mech's turn rate** (owner, 2026-11-11): in every mode, for everyone; no setting and no
   lobby rule (docs/input.md, "Mouse aim").
5. **No stat scaling of human machines beyond the tune clamp.** Scaling durability or damage per player would be a
   mechanic the original doesn't have. The tune clamp is the original's own variable.
6. **Opponent scaling, only in co-op, only upward**: the NG+ bonus, when the party's machines outclass the tournament.

## Netcode

### The four models

Numbers are from multiplayer.md's measurement or rough extrapolation, marked as such. Measured since (2026-11-12,
multiplayer.md "Measured", the world alone): a step with 6 fighters alive 0.16-0.2 ms, with 12 alive 0.5-0.56 ms; a
world copy 5-12 us. Japan to the US west coast is about 110 to 130 ms round trip; to the east coast, about 150
to 180 ms.

| | P2P rollback (player host, MIMH) | lockstep | server state sync, predict own, interpolate others | hybrid: server runs the deterministic world, clients roll back |
|---|---|---|---|---|
| humans per battle | 2-4 comfortable, 6-8 possible | 2-8 | 8-32+; here capped by the stages near 12 | 2-8 comfortable, 12 maybe (measure) |
| AIs | free: every peer runs them | free | each one replicated as state | free |
| own mech, Japan-US | 2-step delay (33 ms), exact | 5-10 steps of delay (80-170 ms); any hitch stalls everyone | instant, but corrected when hit, rammed, knocked back or grabbed | 2-step delay, exact; corrected only if your input reached the server late |
| others, Japan-US | live, snapping back 3-6 steps on a mispredict | live, never wrong | 100-200 ms in the past, smooth | as P2P, but the latency is both legs through the server |
| hits | exact everywhere | exact | projectiles fine; beams and melee need lag compensation ("hit behind cover") | exact |
| cheating | every peer holds the whole world; state hacks desync and get overwritten; the host can tamper with others' inputs | as rollback | server owns truth; aimbots still work | as rollback, with a neutral host; the server can re-run inputs to verify results |
| server cost | rooms and relay only, a few KB/s a player | same | one step per step per room (0.5-0.56 ms at 12 fighters, measured) plus about 30-60 KB/s down per player (guessed: 12 fighters x ~80 bytes x 30 Hz) | one step per step per room, plus inputs (21 bytes x humans x 60 Hz) and checkpoints on join |
| reuse of the world | all of it, plus MIMH's `src/net` | all of it | the server reuses all of it; clients need new snapshot apply, own-fighter prediction, interpolation and lag compensation | all of it, plus a headless server build |

### Notes on the honest comparison

- **Rollback's cost grows with humans.** Every remote human's input change is a possible re-simulation. At 8 humans
  someone changes input almost every step, so each frame re-runs the window. Measured (2026-11-12): a 12-fighter step
  is 0.50-0.56 ms, so a 6-8 step window is 3-4.5 ms a frame, out of 16.6. That's fine on a desktop, tight on a weak
  laptop.
- **Rollback is the fairer PvP model at equal latency.** Both sides see their own inputs exactly and the other's late.
  State sync with lag compensation favours the shooter: you get hit after reaching cover. Phantom Crash's damage is
  mostly projectiles and homing missiles, which state sync handles well. But beams are instant and melee lunges home
  within 25 units, and those are where it would feel wrong.
- **State sync's usual anti-cheat gain mostly doesn't apply.** Its big advantage, not sending what you can't see, is
  weak here. The stages are open arenas, and a camouflaged fighter is still drawn (the camouflage pass refracts the
  backdrop at its body), so every client needs its position anyway. Under input sync a hacked client can draw
  camouflaged enemies plainly; under state sync it nearly can too.
- **AIs are free under input sync and cost bandwidth under state sync.** PvE arenas are mostly AIs.
- **Lockstep is only a fallback.** MIMH already drops to waiting when the rollback window overflows. Nothing here wants
  lockstep as the main model.
- **gubsy's own co-op policy** (`docs/cooperative_multiplayer_policy.md`) says host-authoritative snapshot sync with
  prediction, not rollback. That is right for a loose co-op shooter. It is the wrong trade for a port whose
  fights must play the same as retail, whose world is already deterministic, and which has no measured need for more
  than about 8 humans in a fight.
- **The server needs the game data.** A headless server running `step_world` needs the converted tables, part records,
  collision, nav and rig data from a disc. On the owner's private VPS that's the owner's own disc. It is a reason this
  can't become a public service run by strangers.
- **Where the VPS sits matters more than the model.** Japan to a US-west server is about 120 ms. A US-west server is
  the fair middle for Japan and the US; a Tokyo server favours Japan. Check where the server sits, and whether a
  second region is worth it.

### Per mode

| mode | netcode | host | why |
|---|---|---|---|
| local VS | none | | split screen |
| online VS, teams, casual | P2P rollback | a player (star, MIMH) | at most 4 humans; feels local; no server cost |
| co-op party, drop-in visits | P2P rollback with late join | the battle's owner | the owner's world is the battle; at most 4 humans; MIMH's checkpoints do the join |
| ranked duels | hybrid | VPS | neutral host, results verified by re-running the inputs, replays for free |
| arenas | hybrid | VPS | always available without a player hosting; 8 humans plus AIs; one netcode family |

So the answer differs by host, not by model. It is the same rollback on the same 21-byte inputs everywhere; only who
orders the inputs changes. Switch arenas to state sync only if one of these turns out true: more than 8 humans in one
arena are wanted, re-simulation is too slow on players' machines, or the cross-ocean snapping looks bad with many
humans. None of these is known now. Each can be measured before committing.

## The progression problem

The original is one pilot's career in one save. Every number that matters (day, pools, wins, money, parts) is in that
save, it advances one tournament a day, and the save is a text file the player can edit. Every online mode has to answer
two questions: which save a battle reads, and which saves it writes. And since nothing in a local save can be trusted,
what can a stranger rely on?

### A. Personal careers with drop-in

Everyone keeps their own save. Battles run in someone's world, and visitors earn money, records and maybe opponent
credit in their own saves.
- For: no new save kind, play whenever, works with any friend at any point.
- Against: no shared story; "co-op" is visiting, which is what the owner called too limited; a visitor's own campaign
  moves only a little.

### B. Party career with a shared day

The co-op mode above: a party save with shared progress and per-member wallets and garages. Fights together or apart,
drop-in within the party.
- For: a real co-op campaign; drop-in falls out of it; retail's day and tournament structure is unchanged; joint fights
  are just co-op battles.
- Against: a new save kind and some party menus; members have to be online for the party to advance (or the save lives
  on the VPS); leaving and merging parties is awkward.

### C. Separate online career on the server

A server-owned pilot: money and garages earned only online, one server-wide day (for example, a day every few real
hours, from the same 60-day schedule), the same line-up and shops for everyone, and arenas and tournaments as the way to
earn.
- For: nobody can edit it, so ranked, boards and trading mean something; a shared persistent world.
- Against: the biggest build (accounts, a database, a live economy to tune); the VPS becomes a service to keep up; the
  single-player campaign doesn't connect to it; and it is a new game's economy, not the port's.

### D. Online modes don't touch the career

VS, arenas and ranked use rule-set machines (stock builds, VS builds, build codes, rentals) and give only records, rank
and boards.
- For: simplest and fairest; zero economy design.
- Against: nothing to take home; arenas get stale without a reason to play.

### Lean

B for friends' co-op, with A's visits for friends outside a party. Arenas pay money and records into your own save
(they're yours to cheat), with brackets checked on the build. Ranked and boards are server-side and verified by re-running
the inputs. Skip C unless arenas turn out to be what people play most. Use D's rule sets inside every PvP mode regardless.

## What this adds to `docs/multiplayer.md`'s "change now"

- **Late join** is needed (drop-in, arenas, rejoin). Copy MIMH's checkpoint transfer along with the rest of `src/net`.
- **`fighter_slots` as a parameter.** Keep 6 for retail modes. Let the world carry up to 12 and make the per-slot arrays
  sized by it (fighters, AI, HUDs, lock warnings). Measure the 12-fighter step before deciding the arena cap.
- **Multi-human battle rules** in the lobby-rules block (item 11): the killer freeze, replacements held
  during scenes, the every-human end.
- **A headless server build**: `step_world` plus the cooked data, with no SDL window, Vulkan or audio. The world already
  has no SDL in it.
- **Machine value and tune clamp** as pure functions over a build and the part tables, used by the entry screens, the
  lobby and the server alike.

## Decisions for the owner

1. **Progress model.** Lean: a party career (B) for co-op, visits (A) for friends outside a party, arenas paying into your
   own save, and no server-owned career for now.
2. **Party pacing.** One fight per member per day, with night when everyone has fought or slept. Lean: yes, and helping
   in another member's fight uses nothing up.
3. **Where a party save lives.** Lean: the host's machine, copied to every member at each night so anyone can take over.
   The VPS only if parties want to play without the host.
4. **What a visitor earns.** Lean: their own money and records, and opponent credit in their own save; no tournament
   wins; no cap on prize between friends.
5. **Normalization in co-op and visits.** Lean: a warning screen plus a rental option, and the NG+ bonus as an opt-in
   "party bonus". No clamping of a friend's machine.
6. **Invasions** (joining a friend's fight on the AI's side). Lean: yes, friends only, and the owner can turn it off.
7. **Arena brackets.** Lean: tiers by tune band and machine value, with PvE tiers named by the tournament they draw from;
   stock tune for FFA, teams and ranked.
8. **Arena size.** Lean: `fighter_slots` up to 12, at most 8 humans, per-stage caps found in play.
9. **Netcode.** Lean: input sync with rollback everywhere; the player hosts casual VS and co-op, the VPS hosts arenas and
   ranked. Revisit state sync only on measured need.
10. **Game data on the VPS** for the headless server. Lean: yes, on the owner's private box only.
11. **Multi-human rule changes** (killer freeze, replacement hold, pause). Lean: change them whenever more than one
    human is in a battle; single player and local VS keep retail's. Boss parking is settled: it stays for everyone
    (owner, 2026-11-11). Netcode ideas (9) liked by the owner, 2026-11-11.
12. **Ranked.** Lean: later, after online VS. Fixed rules: stock tune, an optional time limit, results
    verified by the server.
13. **Gifts and trading.** Lean: gifts between friends, no market.
