#!/usr/bin/env bash
# Build and play on your own display. Everything after the options goes to the game.
#
#   scripts/run.sh [--lavapipe] [game arguments...]
#     --lavapipe   software Vulkan instead of the GPU (slow, but safe for a shader nobody has run yet)
#
#   scripts/run.sh                       the game from its boot (logos, attract, title)
#   scripts/run.sh --state 15            straight to the title (any state number, hex)
#   scripts/run.sh --viewer              the stage viewer: drive build 0 on Shinjuku, chase camera
#   scripts/run.sh --stage 2 --build 5   the viewer on another stage and build
#   scripts/run.sh --camera x,y,z,yaw,pitch   the viewer with the free camera, no fighter
#   scripts/run.sh --talk talk_5b        a conversation
#   scripts/run.sh --no-dialogue         story conversations and battle chatter skipped, and kept
#                                        that way in your gubsy profile for later runs
#   scripts/run.sh --dialogue            both shown again (the default), also kept
#   PHANTOMCRASH_NO_DIALOGUE=1 scripts/run.sh   the same as --no-dialogue
#   In a conversation: Escape or a pad's Back (or a right click) skips it; held, or with Shift,
#   it skips every conversation chained after it too.
#
# Each run writes a log (everything said with SDL_Log: the sound device's story, the health line
# every 10 s) to ~/.local/share/OpenPCrash/OpenPCrash/logs/run-YYYYMMDD-HHMMSS.log, the newest
# ten kept. Send the latest with a report of sound going away (docs/audio.md "The run log").
#   PHANTOMCRASH_SOUND_TRACE=1 scripts/run.sh   every sound started, cut, refused or stopped,
#                                               with its step, into the same log
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
lavapipe=0
if [[ "${1:-}" == "--lavapipe" ]]; then
    lavapipe=1
    shift
fi

# build
if [[ ! -f "$repo_root/build/CMakeCache.txt" ]]; then
    cmake -S "$repo_root" -B "$repo_root/build" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$repo_root/build" -j8 --target phantomcrash

# extract the disc data once
if [[ ! -f "$repo_root/data/manifest.sexp" ]]; then
    cmake --build "$repo_root/build" -j8 --target pc_extract
    (cd "$repo_root" && build/tools/extract/pc_extract)
fi

# play
cd "$repo_root"
if [[ "${PHANTOMCRASH_NO_DIALOGUE:-}" == 1 ]]; then
    set -- --no-dialogue "$@"
fi
if [[ $lavapipe -eq 1 ]]; then
    exec env VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json build/phantomcrash "$@"
fi
exec build/phantomcrash "$@"
