#!/usr/bin/env bash
# A headless capture: the game in a private Xvfb (never the owner's display) on lavapipe (software
# Vulkan), killed by pid afterwards, with SDL's dummy audio driver so nothing reaches the speakers. Everything after the options goes to the game.
#
#   scripts/capture.sh [--gpu] out.png [game arguments...]
#     --gpu   the real GPU instead of lavapipe (only after a shader is validated on lavapipe)
set -uo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
gpu=0
if [[ "${1:-}" == "--gpu" ]]; then
    gpu=1
    shift
fi
out="$1"
shift
display=":$((90 + RANDOM % 400))"
Xvfb "$display" -screen 0 1600x1200x24 -nolisten tcp >/dev/null 2>&1 &
xvfb_pid=$!
export SDL_AUDIO_DRIVER=dummy
sleep 1
if [[ $gpu -eq 1 ]]; then
    DISPLAY="$display" "$repo_root/build/phantomcrash" --capture "$out" "$@"
else
    DISPLAY="$display" VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
        "$repo_root/build/phantomcrash" --capture "$out" "$@"
fi
status=$?
kill "$xvfb_pid"
wait "$xvfb_pid" 2>/dev/null
exit $status
