#pragma once
// Stage lighting for the stages family: fog, sun, tint, gamma, shadow colour, dof and heat haze, and
// the stage lamps per stage selector, the text of data/tables/stage_lighting.sexp.
#include <string>

struct Xbe;

std::string stage_lighting_text(const Xbe& xbe);
