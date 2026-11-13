# OpenPCrash

OpenPCrash plays **Phantom Crash** (Genki, Xbox, 2002) on Windows, Mac and Linux. It is a new
program written from scratch that plays the game the way the original does: the same fights, the
garage, the tournaments and the story, at any resolution and frame rate, with keyboard and mouse or
pads, and split-screen VS for two to four players.

OpenPCrash is not affiliated with or endorsed by Genki Co., Ltd. Phantom Crash is a trademark of its
owner.

## You need your own disc

Nothing from the original game is in this repository or in the downloads. OpenPCrash reads your own
copy of Phantom Crash, the North American (USA) release:

- a disc image made from your disc (a plain Xbox image, "XISO", or a full disc image with the video
  partition), or
- the game's folder copied off the disc (the one holding `default.xbe` and `Media`).

The first time it starts, OpenPCrash asks for it: drag the `.iso` or the folder onto its window, or
pick it with Browse. It reads the disc once, on your computer (about a minute, about 4 GB of space),
and keeps what it made in your user folder; later starts go straight to the game. Nothing is
uploaded.

Ordinary PC DVD drives can't read Xbox game discs. The image is made with an original Xbox running a
homebrew dashboard (copy the game's folder off over the network), or with a DVD drive whose firmware
reads Xbox discs and a disc dumping program. An `.iso` you already use in an Xbox emulator works as
it is.

## Playing

Download the zip for your system, unpack it anywhere and run `OpenPCrash` (`OpenPCrash.exe` on
Windows, `OpenPCrash.app` on a Mac). The `README.txt` inside says what each system needs and where
saves go.

Controls, by default:

| | keyboard and mouse | pad |
|---|---|---|
| move | W A S D | left stick |
| aim | mouse | right stick |
| fire the arm weapons | left / right mouse button | right / left trigger |
| fire the back weapons | mouse side buttons, or R / F | B / Y |
| jump | Space | A |
| dash | Shift | left stick click |
| step left / right | Q / E | black / white (shoulders) |
| camouflage | C | X |
| view distance | wheel, or 3 / 4 | d-pad up / down |
| pause | Enter or Escape | Start |
| menus | arrows, Enter or Space, Escape or Backspace | d-pad, A, B |

Every binding can be changed in Options, PC Config. More pads plugged in play split-screen VS.

## Building from source

OpenPCrash is C++20 with CMake, SDL 3 and Vulkan. The libraries it uses are fetched by CMake at
pinned commits (`cmake/deps.cmake`).

**Linux** (Ubuntu 24.04 or similar): a C++20 compiler, CMake 3.20+, Ninja, the Vulkan loader and
headers, and glslang:

```sh
sudo apt install build-essential cmake ninja-build libvulkan-dev glslang-tools
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target phantomcrash
build/phantomcrash
```

A development build keeps the game's data in this checkout (`data/` and `cooked/`, never
committed). With none there yet it opens the same first-run screen; or run the extractor yourself:
`cmake --build build --target pc_extract && build/tools/extract/pc_extract --disc your.iso`.
`--data DIR` and `--cooked DIR` point the game elsewhere.

**Windows**: the releases are cross-compiled from Linux with llvm-mingw:
`scripts/release.sh --only windows` fetches the toolchain (pinned) and builds
`build-release/OpenPCrash-windows/`. Building with MSVC on Windows isn't set up yet.

**Mac**: `scripts/release.sh --mac` on an Apple Silicon Mac with the Xcode command line tools and
`brew install cmake ninja glslang`, or cross-compiled from Linux with osxcross once a macOS SDK is
there (`docs/release.md`).

`scripts/release.sh` makes the player zips for every system it can, by hand; `docs/release.md`
explains what goes in them and why.

## License

OpenPCrash is free software under the GNU General Public License, version 3 (`LICENSE`): anyone may
share and change it, and changes stay open. It contains no game data; you need your own copy of Phantom Crash.

The third-party libraries and the Jost font are under their own licenses
(`scripts/release/THIRD-PARTY.txt`, `assets/fonts/OFL.txt`, `third_party/pl_mpeg/LICENSE`).
