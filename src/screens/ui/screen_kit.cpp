// The screens' shared pieces (decomp src/od/u_06c3e0.c slides, src/o1/u_03c51a.c pop timers,
// src/hand/u_0bd620.c pop scales, SpriteRow::draw_scaled and draw_anchored, the
// event scripts over a screen); the numbers, day, money and banners are screen_header.cpp's.
#include "screens/ui/screen_kit.hpp"

#include "audio/sound_cues.hpp"
#include "screens/campaign/campaign.hpp"
#include "screens/flow/front_end.hpp"
#include "screens/ui/screen_text.hpp"
#include "world/world.hpp"

#include <cmath>
#include <cstdio>

// decomp: sprite_draw_at_if_valid 0x17E17
void sprite_draw_at(Ui& ui, int id, float x, float y, int blend, std::uint32_t color,
                    std::uint32_t flags, float z) {
    if (id < 0) {
        return;
    }
    Sprite sprite = sprite_of(ui, id);
    sprite.pos[0] = x;
    sprite.pos[1] = y;
    sprite.blend = blend;
    sprite.color = color;
    sprite.flags |= flags;
    sprite.z = z;
    sprite_submit(ui, sprite);
}

// decomp: pop_scale_peak 0xBE89C
void pop_scale_peak(float out[2], int rise, int end, int t, float peak, float start) {
    if (t <= 0) {
        out[0] = out[1] = 0.0f;
    } else if (t < rise) {
        out[0] = static_cast<float>(t) / static_cast<float>(rise) * peak;
        out[1] = start;
    } else if (t < end) {
        const float span = static_cast<float>(end - rise);
        const float at = static_cast<float>(t - (end - rise));
        out[0] = peak - (peak - 1.0f) / span * at;
        out[1] = (1.0f - start) / span * at + start;
    } else {
        out[0] = out[1] = 1.0f;
    }
}

// decomp: box_pop_scale 0xBE7FC
void box_pop_scale(float out[2], int rise, int end, int t) {
    pop_scale_peak(out, rise, end, t, 1.2f, 0.05f);
}

void pop_timer_init(PopTimer& t, int sprite, int delay, int duration) {
    t.started = -1;
    t.delay = delay;
    t.duration = duration;
    t.sprite = sprite;
}

void pop_timer_start(Ui& ui, PopTimer& t, float c, float d, bool moves, int a, int b) {
    t.started = task_timer_get(ui);
    t.moves = moves;
    t.a = a;
    t.b = b;
    t.c = c;
    t.d = d;
}

bool pop_timer_draw(Ui& ui, const PopTimer& t, int x, int y, int blend) {
    int elapsed = task_timer_get(ui) - t.started;
    bool running = false;
    if (elapsed < t.duration) {
        running = true;
    } else if (elapsed > t.duration) {
        elapsed = t.duration;
    }
    float scale[2];
    pop_scale_peak(scale, t.delay, t.duration, elapsed, 1.0f, 0.05f);
    int px = x, py = y;
    if (t.moves) {
        float f = static_cast<float>(elapsed - t.delay) / static_cast<float>(t.duration - t.delay);
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        const float g = 1.0f - f;
        px = static_cast<int>(static_cast<float>(t.a) * g + static_cast<float>(x) * f);
        py = static_cast<int>(static_cast<float>(t.b) * g + static_cast<float>(y) * f);
    }
    if (t.sprite >= 0) {
        Sprite sprite = sprite_of(ui, t.sprite);
        sprite.flags |= 8;
        sprite.pos[0] = static_cast<float>(px);
        sprite.pos[1] = static_cast<float>(py);
        sprite.blend = blend;
        sprite.scale[0] = t.c * scale[0];
        sprite.scale[1] = t.d * scale[1];
        sprite_submit(ui, sprite);
    }
    return running;
}

void slide_init(Slide& slide, int count, const int* start) {
    slide.started = -1;
    for (int i = 0; i < count && i < 10; ++i) {
        slide.pos[i] = start[i];
    }
}

void slide_draw(Ui& ui, Slide& slide, int count, int frames, int blend, int y, const int* targets,
                const int* ids) {
    if (slide.started == -1) {
        return;
    }
    const int age = task_timer_get(ui) - slide.started;
    int step = 0;
    if (static_cast<unsigned>(frames - age) > 0u) {
        step = (targets[0] - slide.pos[0]) / (frames - age);
        if (age == 1) {
            ui_sound(ui, bank_sys, se_panel_slide_in);
        }
    }
    for (int i = 0; i < count && i < 10; ++i) {
        slide.pos[i] += step;
        if ((step >= 0 && slide.pos[i] > targets[i]) || (step < 0 && slide.pos[i] < targets[i])) {
            slide.pos[i] = targets[i];
        }
        sprite_draw_at(ui, ids[i], static_cast<float>(slide.pos[i]), static_cast<float>(y), blend);
    }
}

// decomp: garage_menu_screen_draw 0x1C789, plus_tech_menu_draw 0x2EDC5
void menu_wheel_draw(Ui& ui, const int* ids, int count, int shown, int cursor, int frame) {
    constexpr float quarter = 0.7853982f;
    for (int i = shown - 1; i < shown + 7; ++i) {
        const float a = static_cast<float>(i - shown) * quarter;
        float d = static_cast<float>(shown - cursor) * quarter;
        if (shown == 0 && cursor == count - 1) {
            d = quarter;
        } else if (shown == count - 1 && cursor == 0) {
            d = -quarter;
        }
        const float b = static_cast<float>(frame) * d * 0.125f + a;
        const int k = i < 0 ? count - 1 : i % count;
        Sprite sprite = sprite_of(ui, ids[k]);
        sprite.rot[2] = b;
        sprite.pos[0] = 262.0f;
        sprite.pos[1] = 139.0f;
        sprite.blend = 3;
        sprite.rot[0] = 159.5f;
        sprite.rot[1] = 80.5f;
        sprite_submit(ui, sprite);
    }
}

int menu_wheel_pick(Ui& ui, int item_id) {
    const MenuPointer& p = ui.pointer;
    if (!p.visible || p.y > 304.0f) {
        return menu_wheel_missed;
    }
    constexpr float quarter = 0.7853982f, pi = 3.14159265f;
    // the item at rest and the pivot its quad turns about (place's rot plus half its size)
    const PointerRect r = sprite_rect(ui, item_id, 262.0f, 139.0f);
    const float mx = (r.left + r.right) * 0.5f, my = (r.top + r.bottom) * 0.5f;
    const float px = mx + 159.5f, py = my + 80.5f;
    const float dx = p.x - px, dy = p.y - py;
    // the nearest place by angle, clockwise on screen as the cursor's +1 turns
    float a = std::atan2(dy, dx) - std::atan2(my - py, mx - px);
    a = a < -pi ? a + 2.0f * pi : a > pi ? a - 2.0f * pi : a;
    const int k = static_cast<int>(std::lround(a / quarter));
    // the pointer turned back by that place's angle lands in the item's quad
    const float c = std::cos(-quarter * static_cast<float>(k));
    const float s = std::sin(-quarter * static_cast<float>(k));
    const float x = dx * c - dy * s + px, y = dx * s + dy * c + py;
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom ? k : menu_wheel_missed;
}

int menu_wheel_pointer(Ui& ui, int& cursor, int count, int item_id) {
    int to = 0;
    if (ui.pointer.left) {
        const int pick = menu_wheel_pick(ui, item_id);
        to = pick == menu_wheel_missed || pick == 0 ? 0 : pick > 0 ? 1 : -1;
    }
    if (to == 0 && ui.pointer.wheel != 0) {
        to = ui.pointer.wheel > 0 ? -1 : 1;
    }
    if (to == 0 || count <= 1) {
        return 0;
    }
    cursor = ((cursor + to) % count + count) % count;
    ui.pointer.left = false;
    menu_input_mark(ui);
    return to;
}

void row_scaled(Ui& ui, SpriteSlot row, float sx, float sy) {
    if (row.sprite < 0) {
        return;
    }
    Sprite sprite = sprite_of(ui, row.sprite);
    sprite.pos[0] = row.x;
    sprite.pos[1] = row.y;
    sprite.blend = row.blend;
    sprite.scale[0] = sx;
    sprite.scale[1] = sy;
    sprite.color = row.color;
    sprite.z = row.z;
    sprite.flags |= row.flags;
    sprite_submit(ui, sprite);
}

void row_anchored(Ui& ui, SpriteSlot row, float sx, float sy) {
    if (row.sprite < 0) {
        return;
    }
    // anchor_position kind 0: the scaled sprite keeps its middle
    const float w = static_cast<float>(sprite_width(ui, row.sprite));
    const float h = static_cast<float>(sprite_height(ui, row.sprite));
    row.x += (1.0f - sx) * (w * 0.5f);
    row.y += (1.0f - sy) * (h * 0.5f);
    row_scaled(ui, row, sx, sy);
}

SpriteSlot slot_at(Ui& ui, const char* screen, const char* table, int row) {
    const UiTable* t = ui_table(ui.data, screen, table);
    if (!t || row < 0 || row >= static_cast<int>(t->slots.size())) {
        return SpriteSlot{};
    }
    return t->slots[static_cast<std::size_t>(row)];
}

std::string string_at(Ui& ui, unsigned data_va) {
    return string_at_data(ui, data_va);
}

bool script_start(FrontEnd& fe, Ui& ui, TalkScript& script, TalkInterp& interp,
                  const std::string& name) {
    std::string error;
    if (name.empty() || !load_talk_script(script, ui.data.font, ui.data.data_dir, name, error)) {
        if (!name.empty()) {
            std::fprintf(stderr, "script %s: %s\n", name.c_str(), error.c_str());
        }
        return false;
    }
    // event_interp_start(task, script, 0x80000002, ...): no skip chord, flag 2 to the text
    interp = TalkInterp{};
    talk_interp_start(interp, talk_no_skip | talk_flag_2, ui.task_timer + 1);
    (void)fe;
    return true;
}

void script_step(FrontEnd& fe, Ui& ui, TalkScript& script, TalkInterp& interp) {
    fe.talk_context.input_age = menu_input_age(ui);
    fe.talk_context.frame_cadence = ui.frame_cadence;
    std::vector<int> sounds;
    talk_interp_step(interp, script, fe.talk_context, TalkPads{}, ui.draw, sounds);
    for (const int cue : sounds) {
        ui_sound(ui, bank_sys, cue);
    }
}
