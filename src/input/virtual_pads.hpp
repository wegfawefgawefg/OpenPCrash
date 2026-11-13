#pragma once
// Debug only: pads that aren't there, for headless runs with several players (docs/input.md,
// "Headless pads"), as Mans In My House's MIMH_PADS. PHANTOMCRASH_PADS=N attaches N virtual SDL
// gamepads at start; PHANTOMCRASH_PAD_PRESS="F:P:BUTTON[:N],..." holds virtual pad P's BUTTON from
// main loop frame F for N frames (1 by default). Buttons: a b x y start back up down left right
// lb rb ls rs, the triggers lt rt, and the sticks lx+ lx- ly+ ly- rx+ rx- ry+ ry- (y+ up).
// The pads go through SDL and gubsy as real ones do. PHANTOMCRASH_MOUSE_MOVE="F:DX:DY,..." pushes
// a mouse motion of DX, DY counts at frame F (a flick, for checking mouse aim headless), and
// PHANTOMCRASH_KEY_PRESS="F:KEY,..." taps a key at frame F by its SDL name (Return, Right...).
// PHANTOMCRASH_PAD_UNPLUG="F:P,..." pulls virtual pad P out at frame F (the lost-controller box).
// PHANTOMCRASH_PAD_SPIN="F:P:N[:REACH[:X:Y]]" makes virtual pad P a worn pad whose sticks rest at
// X, Y and reach REACH, turned round their edges from frame F for N frames (virtual_pads.cpp).
#include <vector>

// once after SDL_Init: the pads PHANTOMCRASH_PADS asks for
void virtual_pads_attach();
// once a frame before the events are pumped: PHANTOMCRASH_PAD_PRESS's buttons at this frame
void virtual_pads_frame(int frame);
