#pragma once
// The stage viewer's loop (src/viewer_run.cpp); main runs it instead of the front end for --viewer.
#include "state.hpp"

#include <SDL3/SDL.h>

struct Renderer;
struct PackLibrary;
struct Audio;
struct InputDevices;
struct PcHost;

int run_viewer(State& state, SDL_Window* window, Renderer& renderer, PackLibrary& packs,
               Audio* audio, GubsyRuntime& gubsy, InputDevices& devices, PcHost& pc);
