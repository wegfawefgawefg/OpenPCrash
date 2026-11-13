#pragma once
// Area select and the job menu's pieces shared by its two files (area_select.cpp the steps,
// area_select_draw.cpp the pages).
#include "screens/campaign/campaign.hpp"

// the boarded garage's build as a Scoobee (GameState +0x60C8, the garage's parts by inventory)
RosterBuild garage_build(const Garages& garages, int garage);
// job_entry_allowed (the boarded garage tested first: docs/bugs.md)
bool job_entry_allowed(const FrontEnd& fe, const AreaSelectScreen& a);
// job_menu_enter, job_menu_update, job_gate_select_update
void job_menu_enter(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
void job_menu_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
void gate_select_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);

// area_select_draw.cpp
// stage_entry_transform_count of an area's day stage (job_gate_tab_counts: 3, 2, 2)
int area_gate_count(int area);
std::string area_help_text(Ui& ui, int area);
std::string item_help_text(Ui& ui, int item);
void area_help_init(Ui& ui, AreaSelectScreen& a);
// area_select_draw and job_menu_draw by the page and its slide
void area_select_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
// job_gate_screen_draw
void gate_screen_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);

// job_cards.cpp
// rule_lines_draw (the schedule card's: x 184, y 227 / 262 / 297, layer 0x109) and the
// contest card's: three lines 35 apart, each the rule's subtitle or "No limits." (body class,
// leg classes, the extra)
void rule_lines_draw(FrontEnd& fe, Ui& ui, int tournament, int x, int y, int layer);
// job_characters_update and job_contest_info_update, with their cards
void job_characters_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
void job_contest_step(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
// job_map_fighter_count: roster entries 1.. on the map (player -1)
int job_map_fighter_count(const FrontEnd& fe, const AreaSelectScreen& a);
// job_wanderers.cpp: job_area_open's walkers (job_wanderer_start for entries 1..), their update
// (job_map_fighters_update, ordinary jobs only) and rumble_roster_starts_set
void job_wanderers_start(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
void job_wanderers_update(FrontEnd& fe, Ui& ui, AreaSelectScreen& a);
void job_roster_starts_set(FrontEnd& fe, AreaSelectScreen& a);
// job_map_fighters_draw (selected the CHARACTERS cursor, -1 none)
void job_map_fighters_draw(FrontEnd& fe, Ui& ui, AreaSelectScreen& a, int selected);
