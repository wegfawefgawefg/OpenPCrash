# Releases

Player builds are made by hand on a Linux machine with `scripts/release.sh`, when the owner wants
one. There is no CI and nothing is uploaded: the script leaves folders and zips in `build-release/`
(git-ignored), and where they go from there is the owner's call. Modelled on Mans In My House's
`scripts/release.sh` and `docs/release.md`, with its pinned toolchains.

## Making one

```sh
scripts/release.sh                     # every system this machine can build (the Mac needs an SDK)
scripts/release.sh --only linux        # one (or --only windows, --only mac)
PHANTOMCRASH_TOOLS_SEED=~/Coding/GameDev/mansinmyhouse/build-release/tools scripts/release.sh
                                       # copy the pinned toolchains MIMH already fetched
scripts/release.sh --mac               # on an Apple Silicon Mac: the Mac build only
```

It leaves, in `build-release/`:

- `OpenPCrash-linux/`, `OpenPCrash-windows/`, `OpenPCrash-mac/`: what a player unpacks, and a zip
  of each beside it, `OpenPCrash-<version>-<system>.zip`, with a copy under a name without the
  stamp, `OpenPCrash-<system>.zip`, the name GitHub Releases' `latest/download/` links (the site's
  download buttons) ask for;
- `symbols/<version>/`: the debug symbols taken out of them (`.debug` files, the Windows `.pdb`,
  the Mac's `.dSYM`s), for making sense of a crash later;
- `linux-build/`, `windows-build/`, `mac-build/`: the build folders, kept so the next release
  builds only what changed; `mac-vulkan/`, the Vulkan loader built for the Mac;
- `tools/`: llvm-mingw, LLVM, osxcross, rcodesign, MoltenVK and the Vulkan loader's sources,
  fetched the first time at pinned versions with pinned sha256s.

Builds run under `nice -n 19` with `PHANTOMCRASH_JOBS` jobs (4 by default). The siblings (Gubsy,
GView, GLayout, gfxp, gsexp) and SDL are always the pins in `cmake/deps.cmake`, never local
checkouts, so a release is what a fresh clone of the commit builds.

## The version

`PHANTOMCRASH_GAME_VERSION` in `cmake/release.cmake`, bumped by hand. A release adds the day and the
commit (`-dirty` if the checkout had changes not committed): `0.1.0-2026-11-12-d1351c1`. It is in
`VERSION.txt`, the zip's name, the first-run screen's corner, the first line of every run log
(`src/build_info.hpp`), the Windows executable's properties and the Mac app's Info.plist.

## What a bundle holds

The game's own files and nothing else:

- the executable (`OpenPCrash`, `OpenPCrash.exe`, `OpenPCrash.app`) and the libraries it loads
  (Linux: SDL and its satellites beside it, rpath `$ORIGIN`; Windows: none, everything is linked
  in; Mac: `Contents/Frameworks`, with MoltenVK and the Vulkan loader);
- `shaders/*.spv`, compiled from the GLSL in `assets/shaders`;
- the port's own `assets/` as git has them (the widescreen anchors, the Jost font for the
  first-run screen), not the shader sources;
- `README.txt` for players, `THIRD-PARTY.txt`, `VERSION.txt`, `LICENSE.txt` (the game's own,
  GPLv3, the repo's `LICENSE`) and `licenses/`: every library's license from its fetched sources
  (SDL 3, SDL_image, SDL_ttf with FreeType, HarfBuzz and PlutoSVG, SDL_mixer, Gubsy, cgltf,
  bc7enc_rdo), pl_mpeg's, Jost's OFL, and on the Mac MoltenVK's and the Vulkan loader's. GView,
  GLayout, gfxp and gsexp are GPLv3 by the same author, but the pinned commits predate their
  LICENSE files, so each gets Gubsy's copy of the text (not a pin bump, which would bring code).

Never `data/`, `cooked/` or anything from the disc or the XBE. Two checks stop the release:

- `check_bundle`: every file must be one of the above; an asset must be byte for byte the file git
  has; names that look disc-derived (`data/`, `cooked/`, `Media/`, `.xbe`, `.iso`, `.pack`, `.sfd`,
  `.wav`, `.adx`, `.gltf`, `.bin`, `.tga`, `.bmp`, `.png`...) fail outright.
- `check_paths`: `strings` over every binary finds no path of the build machine (the checkout,
  `$HOME`, `/media/`, `/home/`). Release builds compile with `-ffile-prefix-map` (the checkout
  becomes `.`, the build folder and every fetched dependency in it `build`; `cmake/platform.cmake`),
  the Linux rpath is rewritten to `$ORIGIN`, the Windows exe names its PDB by file name only
  (`-pdbaltpath:%_PDB%`), and the Mac's binaries are stripped of their debug map and checked for
  rpaths that point outside the app. (MSVC isn't a release compiler; it would need `/pathmap` or
  clang-cl's `-ffile-prefix-map`.)

## Where a release keeps the player's files

A release build (`-DPHANTOMCRASH_RELEASE=ON`, `src/game_paths.hpp`) reads `assets/` and
`shaders/` beside the executable (`SDL_GetBasePath`, which is `Contents/Resources` in the Mac app)
and keeps everything else in the per-user folder (`SDL_GetPrefPath("OpenPCrash", "OpenPCrash")`):
`data/` and `cooked/` made from the player's disc, `saves/`, `gubsy/` (settings and bindings) and
`logs/`. `--data` and `--cooked` still override. A development build keeps reading the checkout's
`data/`, `cooked/` and `assets/` and the build folder's shaders (its saves, settings and logs are
in the per-user folder too).

The folder was `PhantomCrash/PhantomCrash` before 2026-11-13. The first start of a newer build
moves it over (`user_folder_migrate`, src/user_folder.hpp, run from `run_log_start`): a rename,
or a copy and the old one removed across drives, said in the run log. When both hold files the
old one is left as it is, and the log says so. The window, SDL's app metadata
(`org.openpcrash.OpenPCrash`, the Mac app's bundle id) and Vulkan's application name say
OpenPCrash; the targets, the `PHANTOMCRASH_*` variables and gubsy's settings keys keep their
names.

## The first run

With no `data/manifest.sexp`, the game opens the setup screen (`src/setup`, docs/assets.md "The
extractor"): drop or browse to the disc image or game folder, and it extracts and cooks on a worker
thread, then starts the game. `pc_extract --disc` reads the same three forms. Cancel stops it at
any point, the cook included (`CookOptions::cancel`); a stopped setup leaves no manifest, so the
screen comes back next time. The window asks for the display's own pixels and the text is
rendered at them (`setup_art_fit`), sharp on a Retina or HiDPI display.

## The Linux bundle

Built with the system compiler against the pinned SDL, every library from the build folder copied
beside the executable with an rpath of `$ORIGIN`. It needs from the system glibc (2.38 or newer
when built on Ubuntu 24.04), libstdc++ and the Vulkan loader (`libvulkan.so.1`). SDL finds X11,
Wayland, PipeWire, PulseAudio and ALSA at run time.

## The Windows build

Cross-compiled with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (MIMH's pin,
`cmake/mingw-w64.cmake`), everything static in one `OpenPCrash.exe` with libc++ and libunwind
(`cmake/mingw.cmake`). The Vulkan headers are SDL's Khronos copy and `vulkan-1.dll`'s import
library is written from them; the loader comes with the GPU drivers and is never bundled. It is
delay-loaded (`-Wl,--delayload=vulkan-1.dll`, as MIMH), loaded on the first Vulkan call: main
looks for it first (`vulkan_loader_check`, src/render/vk_device.cpp), so a machine without a
Vulkan driver gets the game's own box saying so and to install the graphics card's driver, not
the system's missing-DLL box; a renderer that fails to start says why in the same box. The
manifest makes UTF-8 the code page (`src/windows/`), the main thread gets an 8 MB stack, and a
release is a GUI program (`-mwindows`): its output goes to the run log in the per-user folder
(`%APPDATA%\OpenPCrash\OpenPCrash\logs`). MIMH's two fixes to the gubsy and gview pins for
Windows are applied as they are fetched (`cmake/patches/`).

## The Mac build

`OpenPCrash.app` for Apple Silicon (arm64), macOS 12 and newer in practice (MoltenVK's release is
built for 12; there is no OpenGL fallback). Made by `scripts/release-mac.sh`, which `release.sh`
sources: on Linux cross-compiled with [osxcross](https://github.com/tpoechtrager/osxcross) and
LLVM 22's Linux release (MIMH's pins), on a Mac with the Xcode command line tools. It bundles the
Vulkan loader, built from Khronos' sources, and MoltenVK's release (arm64 half), found through
`Resources/vulkan/icd.d`.

- **The SDK** can't be downloaded legitimately without a Mac and can't be passed round; it is made
  once on a Mac with osxcross' `tools/gen_sdk_package_tools.sh` and put in
  `build-release/tools/osxcross/tarballs/`. Mans In My House already has one (macOS 26.5, in its
  `build-release/tools/osxcross/tarballs/`, with osxcross built): `PHANTOMCRASH_TOOLS_SEED` pointing
  at MIMH's `build-release/tools` copies it and the built toolchain instead of fetching and building
  them again.
- **Signing:** ad hoc by default, as Apple Silicon runs nothing unsigned. An ad hoc signature
  carries no name, but Gatekeeper stops the first open of a downloaded copy; the README and the site
  say what to do (Open Anyway, right-click Open, or `xattr -dr com.apple.quarantine`).
  `--mac-identity` signs with a real Developer ID instead (on a Mac the keychain identity's name; on
  Linux a `.p12` file with `PHANTOMCRASH_MAC_P12_PASSWORD_FILE`), with the hardened runtime. That
  puts the signer's name on every copy, so it's off unless the owner asks for it. A Developer ID
  build would also want notarizing (`xcrun notarytool submit` on a Mac, or `rcodesign
  notary-submit` with an App Store Connect API key) and stapling; the script doesn't do that yet.
- **Checked as it's made:** every `@rpath` library resolves inside `Frameworks/`, nothing else is
  loaded but `/usr/lib` and `/System/Library`, no rpath points at the build machine, each Mach-O is
  signed.
- **Not yet tried on a Mac.**

## Bug reports

Opt-in crash and bug reports to the owner's server (`src/report/`, `src/screens/pc/report_panel*.cpp`,
`services/openpcrash-reports/`, `tools/reports.py`), after Mans In My House's (its `src/report/`,
`services/mimh-reports/`) and cut down to what OpenPCrash needs. Approved by the owner, 2026-11-13.

**Where they go.** `PHANTOMCRASH_REPORT_URL`, a CMake cache option, empty in the source. With none, everything is
compiled in but the question, PC CONFIG's two rows and the pause's button are hidden, and nothing is sent.
`scripts/release.sh` reads the address from `~/.config/openpcrash/report_url` (one line, the https address) when that
file exists, and says which it did; the address is never in the repository. `PHANTOMCRASH_REPORT_URL` in the
environment overrides the built-in one at run time, for checks.

**Consent.** Off until the player says yes; nothing is sent before that, and nothing silently.

- The first-run screen asks once, when the disc has been read: "Send crash reports to help fix bugs?", YES / NO, with
  one line on what goes. Escape is no.
- An install that never saw the question is asked once over the title, in PC CONFIG's look
  (`src/screens/pc/report_panel.cpp`): "SEND CRASH REPORTS TO HELP FIX BUGS", YES / NO; B is no.
- PC CONFIG, GAME, CRASH REPORTS (SEND / OFF) changes it later. The answer is kept in
  `<per-user folder>/reports/consent.txt`, not in gubsy's settings, so it's known before anything else starts. No
  drops what waits in the outbox.
- A headless capture (`--frames`) sends nothing automatic and never asks (`PHANTOMCRASH_DEV_REPORT_ASK=1` asks, for
  pictures of the question).

**Kinds.**

| kind | what | sent |
|---|---|---|
| `crash` | a signal (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP), an unhandled Windows exception, abort(), an uncaught C++ exception (its `what()`), a bad C runtime parameter, a pure virtual call: the handler writes `reports/crash-<session>.txt` (reason, stack as `module+offset`, context) and in a fight `crash-<session>.fight` (the record), allocating nothing | the next start |
| `crash`, "unclean exit" | a session whose `reports/running-<session>.txt` is still there with no crash file (a fail-fast, killed) | the next start |
| `hang` | no frame for 15 seconds (`report_watchdog.cpp`; a battle's load allows a minute; a machine put to sleep isn't one), with the main thread's stack (Linux and the Mac) and the fight | at once |
| `renderer` | the renderer failing to start, no Vulkan loader, a frame that fails to draw (a lost device) | at once |
| `startup` | the packs, the window, the front end or a battle failing to load | at once |
| `manual` | REPORT A BUG, from the pause (BACK, or its button among the pause's PC items: the cursor and A, or a click) or PC CONFIG, GAME: a note typed or picked a letter at a time with the pad, and from the pause a picture | at once |

Automatic ones: once a kind a session, six at most, ten seconds apart. They go from a worker thread
(`report_send.cpp`) and the game never waits on one; at exit the worker gets two seconds and what's left waits.
A failed send (offline, the server down, 429, 5xx) waits in `reports/outbox/`, ten at most, a month at most, tried
on the next three starts, then dropped.

**What one holds.** `log`: the run log's last 48 KiB with the home folder (and any `/home/<name>`, `/Users/<name>`,
`/media/<name>`, `C:\Users\<name>`) taken out, cut from the front until the JSON is under 240 KiB. `build`: version, commit, day,
release (`src/build_info.hpp`). `system`: the OS and its version, the platform, cores, RAM, and the GPU with its
driver and Vulkan version (`Renderer::device`). `settings`: PC CONFIG's (`pc_settings.cpp report_settings`, never the
display's name). `game`: the AppState, a fight stepping or not. `stack` for a crash or a hang. `replay` from a fight
(below). `shot`, the player's own only: the paused fight at most 512 wide, JPEG at 60. No player names (the record's
pilot, machine, chip and playlist names are put back as a new game has them); the server keeps no addresses.

**The fight.** The world is a pure function of its inputs, so a fight's first world and every step's `Inputs`
reproduce it exactly, a crash's step included. Every live fight is recorded now (the replay's own record,
`battle_replay.hpp`; only state 6's reaches the result screen's REPLAY, as before; VS isn't yet, docs/backlog.md).
`src/report/report_fight.cpp` makes it bytes: a head once as the fight starts (every `World` field but its pointers,
`FIGHT_FIELDS`; what rebuilds the constant tables: the battle's plan, the lines and lesson scripts presentation
measured into `BattleData`, each weapon's table row; the start's hashes), and a tail when a report is made (the
inputs so far and the one being stepped, every 60th fight hash, the live world's hash). The crash handler writes the
same bytes with `write()` alone. The worker packs them with LZ4 (`third_party/lz4`, the inputs XORed with the step
before and laid out in byte columns first); over 1.5 MiB packed the replay is left out and the message says so.
Recording only reads the world: the nine movement traces print the same step lines as 46d64d2.

`pc_sim --replay FILE` reads a record, rebuilds the first world from this build's packs, checks its hash, steps every
input with `--hash N`'s lines, checks every 60th fight hash and the end's world hash, and exits 0 when all match. It
needs a build of the report's commit (it says so when the world's layout differs).

**The server.** `services/openpcrash-reports/` (its README): aiohttp behind nginx's `location = /openpcrash-reports`
on 127.0.0.1:8796, `/var/lib/openpcrash-reports`, a token, a fixed list of fields and kinds, size and rate limits,
no addresses kept, no access log. Deployed 2026-11-13 to the owner's dev box beside mimh-reports.
`tools/reports.py pull | list | show ID | replay ID [--hash 1]` on the owner's machine (rsync over ssh into
`bug-reports/`, git-ignored; the destination is in `~/.config/openpcrash/report_ssh`, not the repository).

**Headless checks.** `PHANTOMCRASH_DEV_REPORT=U` sends a manual report ("DEV CHECK", with the fight) at front end
update U, `PHANTOMCRASH_DEV_REPORT_OPEN=U` opens REPORT A BUG, `PHANTOMCRASH_DEV_CRASH=U` crashes on purpose; the
consent is the scratch per-user folder's `reports/consent.txt`. `PHANTOMCRASH_REPORTS=1` lets a capture send automatic
ones and last time's crash. `tools/reports.py show ID --exe build/phantomcrash` names the game's stack frames with
addr2line from a build of the report's commit (a release's `symbols/` `.debug` file does too).

Checked 2026-11-13, headless on lavapipe against the deployed server (`captures/reports/`):

- a tournament fight (`--state 6`, seed 4, build row 37, `walk_run_jump.txt`, `--hash 1`) sent a manual report at
  update 700 (36 KiB, the record 22 KiB packed); `tools/reports.py pull` and `replay` stepped it in `pc_sim`: the start
  world matched, 12 of 12 fight hashes, the end's world hash, and all 698 `hash STEP WORLD FIGHT` lines were identical
  to the game's own run;
- a fight crashed on purpose (SIGSEGV at update 500, seed 11, `legs.txt`): the handler wrote the crash file and the
  record (470 KiB raw), the next start sent them (36 KiB), the stack named `report_crash_on_purpose`,
  `report_panel_frame`, `front_end_update`, and the replay matched all 498 steps' hash lines;
- the nine movement traces print the same step lines as 46d64d2.

## Site visits

The site counts its own visits, self-hosted on the dev server beside the bug reports: no third-party tracker, no
cookies, nothing kept in the browser, nothing a cookie banner would be needed for. Asked for by the owner,
2026-11-13.

**The page.** A short script at the end of `site/index.html` sends one request (`navigator.sendBeacon`, a
`text/plain` JSON body) for the page view and one for each click on the three download links, the big download
button (it carries the visitor's system) and the footer's source link: the path, the referring site's host only,
`?ref=` or `?utm_source=` if the address has one, a screen-width bucket (phone, tablet, laptop, desktop, wide) and
the browser's language. Nothing is sent when the browser has Do Not Track or Global Privacy Control on. The
address it posts to is the `COUNT_URL` const at the top of that script, the only place it appears; moving to a
domain is that one edit. To tag a link you post somewhere, add `?ref=NAME` to the site's address.

**The server.** `POST /openpcrash-reports/visit` in `services/openpcrash-reports/visits.py` (its README has the
fields, limits and the GeoIP database): it takes only the GitHub Pages origin, drops bots, and keeps per hit the
time, kind, the page's fields, the country and city (DB-IP City Lite, looked up locally), the browser and OS family,
and a visitor hash that is salted per UTC day with a salt held only in memory (Plausible's method), so a day's
visitors can be counted and nothing else. No IP address is written anywhere and nginx logs nothing for the route.
DB-IP's licence (CC BY 4.0) needs the credit in the site's footer. Deployed 2026-11-13.

**Reading it.** `tools/site_stats.py [--days 30] [--top 12]` on the owner's machine reads the month files over ssh
(the same `~/.config/openpcrash/report_ssh` as `tools/reports.py`) and prints, for the window: views, visitors and
clicks a day, then countries, cities, referring sites, `?ref=` sources, browsers, systems, screens and languages
(views and visitors each), and the download and source clicks. Under that, GitHub's own numbers through `gh api`:
each release's download count per zip (exact, all time), and the repo page's views and referrers for GitHub's last
14 days. A visitor is one browser on one day, so a window's visitors are the sum of its days'. `--from DIR` reads a
local folder of `visits-YYYY-MM.jsonl`; `--no-github` skips GitHub.

Checked 2026-11-13 against the deployed server (`captures/site_stats/`, the test hits deleted from the server after):
headless Chrome on a local copy of `site/` (the server let the local origin in for the test only) sent the view with
the referring host and `?ref=`, and the five clicks (the big button, three links, the source link); with Do Not
Track on it sent nothing; with Chrome's own headless User-Agent the server dropped the hits. curl through nginx with
the Pages origin: views and downloads from Safari on macOS and Firefox on Windows kept with country and city; a bot
User-Agent dropped; no or another origin 403; an unknown kind 400; 5 KB 413. nginx's access and error logs held no
line for the route.

## Not yet

- All three build here (2026-11-12: Linux 7.6 MB zip, Windows 5.8 MB, Mac 7.2 MB, the Mac with
  MIMH's macOS 26.5 SDK, signed ad hoc). Only the Linux bundle has run (headless, lavapipe: setup
  from the US ISO into a fresh per-user folder, then the game; the next start skipped setup).
  Nothing has run on Windows or a Mac.
- Follow-ups are in docs/backlog.md, "Release and first run".
- No icon for the executable or the app yet.
- Hosting: the zips aren't uploaded anywhere yet; the site's buttons point at the public repo's
  GitHub Releases (`releases/latest/download/OpenPCrash-<system>.zip`).

## The public repo

https://github.com/wegfawefgawefg/OpenPCrash (GPLv3, public since 2026-11-13) is a separate history: snapshots of
this private repo, never its commits (they carry session links and work notes). To publish a new snapshot: clone the
public repo into a scratch folder, replace its tree with `git archive HEAD` of this repo (keep its `.git`), check the
tree has no local paths, private names or disc files (`grep -r -E '/media/|/home/'`, no `data/` or `cooked/`), commit
under the usual name with a plain message, and push. Only when the owner asks.

The site is on GitHub Pages (https://wegfawefgawefg.github.io/OpenPCrash/), served from the public repo's `gh-pages`
branch: a copy of `site/` pushed by hand. While the newest release is a pre-release, the deployed copy's download links
name its tag (`releases/download/v0.1.0/...`), because `releases/latest` skips pre-releases; once a release is a full
one, deploy `site/` as it is (`releases/latest/download/...`). `site/shots/` holds headless 1920x1080 captures of the
port (WebP, up to 200 KB, with a 640-wide `-t` thumbnail each); the page's drawings are its own inline SVG and CSS.
The hero and some gallery shots are posed scenes: the stage viewer's `--pose FILE` (`src/viewer_pose.hpp`) places the
machines, drives them with pad lines and puts the camera, and the fight then runs as any other; the other shots are
fights caught as played, through the game's own views.
