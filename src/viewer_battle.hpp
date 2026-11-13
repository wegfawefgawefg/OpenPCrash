#pragma once
// The stage viewer's battle setup (--build, --opponents, --demo, --follow) and its AI trace.
#include "state.hpp"

#include <string>

struct Scenery;
struct Renderer;
struct PackLibrary;

// the battle data, the roster and every fighter, slot 0 first; each opponent's build drawn
bool viewer_battle_start(State& state, Scenery& scenery, Renderer& renderer, PackLibrary& packs,
                         std::string& error);
// --trace-ai: one line per AI fighter
void viewer_trace_ai(const World& world);
