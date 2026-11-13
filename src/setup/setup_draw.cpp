#include "setup/setup_draw.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct FontFile {
    const char* file;
    float size;
};
// by SetupFont
constexpr FontFile font_files[] = {
    {"Jost-ExtraBoldItalic.ttf", 86}, {"Jost-Bold.ttf", 40}, {"Jost-Medium.ttf", 29},
    {"Jost-Bold.ttf", 32},            {"Jost-Medium.ttf", 21},
};

// the 4:3 frame the original's menus fill: 576x448 of 640x480, doubled
constexpr float left = 64, right = 1216, top = 32, bottom = 928;

SDL_FColor fcolor(SetupColor c) {
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

// a filled convex polygon around its first point
void fill_fan(SetupArt& art, const std::vector<SDL_FPoint>& points, SetupColor color) {
    if (points.size() < 3) {
        return;
    }
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    for (const SDL_FPoint& p : points) {
        vertices.push_back({p, fcolor(color), {0, 0}});
    }
    for (int i = 1; i + 1 < static_cast<int>(points.size()); ++i) {
        indices.insert(indices.end(), {0, i, i + 1});
    }
    SDL_RenderGeometry(art.renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

// the rounded rectangle's outline, clockwise
std::vector<SDL_FPoint> round_rect_points(SDL_FRect r, float radius) {
    radius = std::min({radius, r.w / 2, r.h / 2});
    const float cx[4] = {r.x + r.w - radius, r.x + r.w - radius, r.x + radius, r.x + radius};
    const float cy[4] = {r.y + radius, r.y + r.h - radius, r.y + r.h - radius, r.y + radius};
    std::vector<SDL_FPoint> points;
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= 6; ++step) {
            const float a = (-90.0f + 90.0f * corner + 15.0f * step) * 3.14159265f / 180.0f;
            points.push_back({cx[corner] + radius * std::cos(a), cy[corner] + radius * std::sin(a)});
        }
    }
    return points;
}

// a line of width two from a to b
void thick_line(SetupArt& art, SDL_FPoint a, SDL_FPoint b, SetupColor color) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
    const float nx = -dy / length * 1.25f, ny = dx / length * 1.25f;
    fill_fan(art, {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny},
                   {a.x - nx, a.y - ny}},
             color);
}

// a smooth step from (x0, y0) to (x1, y1)
std::vector<SDL_FPoint> swoop(float x0, float y0, float x1, float y1) {
    std::vector<SDL_FPoint> points;
    for (int i = 0; i <= 16; ++i) {
        const float t = static_cast<float>(i) / 16.0f;
        const float s = t * t * (3 - 2 * t);
        points.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * s});
    }
    return points;
}

std::string text_key(SetupFont font, const std::string& text, SetupColor color, int align,
                     float wrap) {
    char head[64];
    std::snprintf(head, sizeof head, "%d/%02x%02x%02x%02x/%d/%d/", static_cast<int>(font), color.r,
                  color.g, color.b, color.a, align, static_cast<int>(wrap));
    return head + text;
}

} // namespace

bool setup_art_open(SetupArt& art, SDL_Renderer* renderer, const std::string& font_dir,
                    std::string& error) {
    art.renderer = renderer;
    if (!TTF_Init()) {
        error = std::string("fonts: ") + SDL_GetError();
        return false;
    }
    for (int i = 0; i < static_cast<int>(SetupFont::count); ++i) {
        const std::string path = font_dir + "/" + font_files[i].file;
        art.fonts[i] = TTF_OpenFont(path.c_str(), font_files[i].size);
        if (!art.fonts[i]) {
            error = "can't open the font " + path + ": " + SDL_GetError();
            return false;
        }
    }
    return true;
}

void setup_art_fit(SetupArt& art) {
    // the letterbox in the output's pixels (the window's size times its pixel density)
    SDL_FRect box{};
    if (!SDL_GetRenderLogicalPresentationRect(art.renderer, &box) || box.w <= 0) {
        return;
    }
    art.origin = {box.x, box.y};
    const float scale = std::clamp(box.w / setup_w, 0.25f, 8.0f);
    if (std::fabs(scale - art.scale) < 0.001f) {
        return;
    }
    art.scale = scale;
    for (int i = 0; i < static_cast<int>(SetupFont::count); ++i) {
        TTF_SetFontSize(art.fonts[i], font_files[i].size * scale);
    }
    for (auto& [key, texture] : art.texts) {
        SDL_DestroyTexture(texture);
    }
    art.texts.clear();
}

void setup_art_close(SetupArt& art) {
    for (auto& [key, texture] : art.texts) {
        SDL_DestroyTexture(texture);
    }
    art.texts.clear();
    for (TTF_Font*& font : art.fonts) {
        if (font) {
            TTF_CloseFont(font);
            font = nullptr;
        }
    }
    TTF_Quit();
}

void fill_rect(SetupArt& art, SDL_FRect rect, SetupColor color) {
    SDL_SetRenderDrawColor(art.renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(art.renderer, &rect);
}

void fill_round_rect(SetupArt& art, SDL_FRect rect, float radius, SetupColor color) {
    std::vector<SDL_FPoint> points = round_rect_points(rect, radius);
    points.insert(points.begin(), {rect.x + rect.w / 2, rect.y + rect.h / 2});
    points.push_back(points[1]);
    fill_fan(art, points, color);
}

void framed_round_rect(SetupArt& art, SDL_FRect rect, float radius, float thickness,
                       SetupColor frame, SetupColor fill) {
    fill_round_rect(art, rect, radius, frame);
    fill_round_rect(art,
                    {rect.x + thickness, rect.y + thickness, rect.w - 2 * thickness,
                     rect.h - 2 * thickness},
                    std::max(0.0f, radius - thickness), fill);
}

void draw_setup_frame(SetupArt& art, const char* title, const std::string& footer_left,
                      const std::string& footer_right) {
    SDL_Renderer* r = art.renderer;
    // the backdrop: dark blue with the original's scan lines
    fill_rect(art, {0, 0, setup_w, setup_h}, setup_black);
    fill_rect(art, {left, 190, right - left, 640}, setup_deep);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    std::vector<SDL_FRect> lines;
    for (float y = 190; y < 830; y += 4) {
        lines.push_back({left, y, right - left, 1.5f});
    }
    SDL_SetRenderDrawColor(r, 0, 0, 0, 90);
    SDL_RenderFillRects(r, lines.data(), static_cast<int>(lines.size()));

    // the top band: blue, the black wedge on the left under its swoop, the white edges
    fill_rect(art, {left, top, right - left, 104}, setup_blue);
    fill_rect(art, {left, top, right - left, 3}, {60, 104, 220, 255});
    const std::vector<SDL_FPoint> top_curve = swoop(190, 82, 300, 136);
    std::vector<SDL_FPoint> wedge = {{left, 136}, {left, 82}};
    wedge.insert(wedge.end(), top_curve.begin(), top_curve.end());
    fill_fan(art, wedge, setup_black);
    thick_line(art, {left, 82}, {190, 82}, setup_white);
    for (std::size_t i = 1; i < top_curve.size(); ++i) {
        thick_line(art, top_curve[i - 1], top_curve[i], setup_white);
    }
    fill_rect(art, {300, 135, right - 300, 2.5f}, setup_white);
    fill_rect(art, {left, 137, right - left, 52}, setup_black);
    fill_rect(art, {left, 189, right - left, 2.5f}, setup_white);
    draw_text(art, SetupFont::title, title, right - 26, 32, setup_white, 1);

    // the bottom band: a black strip rising into the blue on the right, the blue below
    fill_rect(art, {left, 828, right - left, bottom - 828}, setup_blue);
    const std::vector<SDL_FPoint> bottom_curve = swoop(800, 830, 860, 776);
    // (concave: the strip, its slanted end and the rise apart)
    fill_rect(art, {left, 830, 800 - left, 54}, setup_black);
    fill_fan(art, {{800, 884}, {800, 830}, {880, 830}, {840, 884}}, setup_black);
    std::vector<SDL_FPoint> rise = bottom_curve;
    rise.push_back({right, 776});
    rise.push_back({right, 830});
    rise.push_back({800, 830});
    fill_fan(art, rise, setup_black);
    thick_line(art, {left, 830}, {800, 830}, setup_white);
    for (std::size_t i = 1; i < bottom_curve.size(); ++i) {
        thick_line(art, bottom_curve[i - 1], bottom_curve[i], setup_white);
    }
    thick_line(art, {860, 776}, {right, 776}, setup_white);
    thick_line(art, {left, 884}, {840, 884}, setup_white);
    thick_line(art, {840, 884}, {880, 830}, setup_white);
    thick_line(art, {880, 830}, {right, 830}, setup_white);
    draw_text(art, SetupFont::small, footer_left, left + 24, 845, setup_grey, -1);
    draw_text(art, SetupFont::small, footer_right, right - 24, 791, setup_grey, 1);
}

float draw_text(SetupArt& art, SetupFont font, const std::string& text, float x, float y,
                SetupColor color, int align, float wrap) {
    if (text.empty()) {
        return 0;
    }
    const std::string key = text_key(font, text, color, align, wrap);
    SDL_Texture* texture = nullptr;
    const auto found = art.texts.find(key);
    if (found != art.texts.end()) {
        texture = found->second;
    } else {
        TTF_Font* face = art.fonts[static_cast<int>(font)];
        const SDL_Color c{color.r, color.g, color.b, color.a};
        SDL_Surface* surface = nullptr;
        if (wrap > 0) {
            TTF_SetFontWrapAlignment(face, align < 0   ? TTF_HORIZONTAL_ALIGN_LEFT
                                           : align > 0 ? TTF_HORIZONTAL_ALIGN_RIGHT
                                                       : TTF_HORIZONTAL_ALIGN_CENTER);
            surface = TTF_RenderText_Blended_Wrapped(face, text.c_str(), 0, c,
                                                     static_cast<int>(wrap * art.scale));
        } else {
            surface = TTF_RenderText_Blended(face, text.c_str(), 0, c);
        }
        if (surface) {
            texture = SDL_CreateTextureFromSurface(art.renderer, surface);
            SDL_DestroySurface(surface);
        }
        art.texts[key] = texture;
    }
    if (!texture) {
        return 0;
    }
    // the texture is in the window's pixels: placed and sized back in layout units, its corner on
    // a whole pixel so each texel lands on one
    float w = 0, h = 0;
    SDL_GetTextureSize(texture, &w, &h);
    w /= art.scale;
    h /= art.scale;
    const float at = align < 0 ? x : align > 0 ? x - w : x - w / 2;
    const auto on_pixel = [&](float v, float origin) {
        return (std::round(origin + v * art.scale) - origin) / art.scale;
    };
    const SDL_FRect to{on_pixel(at, art.origin.x), on_pixel(y, art.origin.y), w, h};
    SDL_RenderTexture(art.renderer, texture, nullptr, &to);
    return h;
}

void draw_button(SetupArt& art, SDL_FRect rect, const std::string& label, bool lit) {
    if (lit) {
        framed_round_rect(art, rect, 14, 4, {40, 30, 0, 255}, setup_yellow);
    } else {
        framed_round_rect(art, rect, 14, 4, setup_white, {14, 14, 18, 255});
    }
    const float h =
        static_cast<float>(TTF_GetFontHeight(art.fonts[static_cast<int>(SetupFont::button)])) /
        art.scale;
    draw_text(art, SetupFont::button, label, rect.x + rect.w / 2, rect.y + (rect.h - h) / 2,
              lit ? setup_black : setup_yellow);
}

void draw_bar(SetupArt& art, SDL_FRect rect, int permille) {
    framed_round_rect(art, rect, 8, 3, setup_white, {4, 10, 34, 255});
    const float inner = rect.w - 12;
    const float filled = inner * static_cast<float>(std::clamp(permille, 0, 1000)) / 1000.0f;
    if (filled > 2) {
        fill_round_rect(art, {rect.x + 6, rect.y + 6, filled, rect.h - 12}, 4, setup_yellow);
        // the original's meter look: dark notches across the fill
        SDL_SetRenderDrawColor(art.renderer, 120, 80, 0, 160);
        for (float x = rect.x + 20; x < rect.x + 6 + filled; x += 16) {
            const SDL_FRect notch{x, rect.y + 6, 3, rect.h - 12};
            SDL_RenderFillRect(art.renderer, &notch);
        }
    }
}
