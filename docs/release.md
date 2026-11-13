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
  of each beside it, `OpenPCrash-<version>-<system>.zip`;
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
- `README.txt` for players, `THIRD-PARTY.txt`, `VERSION.txt` and `licenses/`: every library's
  license from its fetched sources (SDL 3, SDL_image, SDL_ttf with FreeType, HarfBuzz and PlutoSVG,
  SDL_mixer, Gubsy, GView, GLayout, gfxp, gsexp, cgltf, bc7enc_rdo), pl_mpeg's, Jost's OFL, and on
  the Mac MoltenVK's and the Vulkan loader's.

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
and keeps everything else in the per-user folder (`SDL_GetPrefPath("PhantomCrash", "PhantomCrash")`):
`data/` and `cooked/` made from the player's disc, `saves/`, `gubsy/` (settings and bindings) and
`logs/`. `--data` and `--cooked` still override. A development build keeps reading the checkout's
`data/`, `cooked/` and `assets/` and the build folder's shaders.

## The first run

With no `data/manifest.sexp`, the game opens the setup screen (`src/setup`, docs/assets.md "The
extractor"): drop or browse to the disc image or game folder, and it extracts and cooks on a worker
thread, then starts the game. `pc_extract --disc` reads the same three forms.

## The Linux bundle

Built with the system compiler against the pinned SDL, every library from the build folder copied
beside the executable with an rpath of `$ORIGIN`. It needs from the system glibc (2.38 or newer
when built on Ubuntu 24.04), libstdc++ and the Vulkan loader (`libvulkan.so.1`). SDL finds X11,
Wayland, PipeWire, PulseAudio and ALSA at run time.

## The Windows build

Cross-compiled with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (MIMH's pin,
`cmake/mingw-w64.cmake`), everything static in one `OpenPCrash.exe` with libc++ and libunwind
(`cmake/mingw.cmake`). The Vulkan headers are SDL's Khronos copy and `vulkan-1.dll`'s import
library is written from them; the loader comes with the GPU drivers and is never bundled. The
manifest makes UTF-8 the code page (`src/windows/`), the main thread gets an 8 MB stack, and a
release is a GUI program (`-mwindows`): its output goes to the run log in the per-user folder
(`%APPDATA%\PhantomCrash\PhantomCrash\logs`). MIMH's two fixes to the gubsy and gview pins for
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

## Not yet

- All three build here (2026-11-12: Linux 7.6 MB zip, Windows 5.8 MB, Mac 7.2 MB, the Mac with
  MIMH's macOS 26.5 SDK, signed ad hoc). Only the Linux bundle has run (headless, lavapipe: setup
  from the US ISO into a fresh per-user folder, then the game; the next start skipped setup).
  Nothing has run on Windows or a Mac.
- Follow-ups are in docs/backlog.md, "Release and first run".
- No icon for the executable or the app yet.
- Hosting: the zips aren't uploaded anywhere; the site (`site/`) links placeholder addresses.
