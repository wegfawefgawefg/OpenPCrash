#include "talk/sprite_registry.hpp"

#include "cook/sexp_read.hpp"

#include <algorithm>

namespace {

int nth(gsexp::Node field, int n) {
    return atom_int(field.child_at(static_cast<std::size_t>(n) + 1));
}

bool load_images(SpriteRegistry& registry, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/ui/images.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        if (!form.head().is_atom("image")) {
            continue;
        }
        const int id = form.get_int("id").value_or(-1);
        if (id < 0) {
            continue;
        }
        if (static_cast<std::size_t>(id) >= registry.sprites.size()) {
            registry.sprites.resize(static_cast<std::size_t>(id) + 1);
        }
        RegistrySprite& sprite = registry.sprites[static_cast<std::size_t>(id)];
        sprite.slot = form.get_int("slot").value_or(-1);
        sprite.asset = form.get_int("asset").value_or(-1);
        const gsexp::Node rect = form.find("rect");
        const gsexp::Node sheet = form.find("sheet");
        for (int k = 0; k < 4; ++k) {
            sprite.rect[k] = static_cast<float>(nth(rect, k));
        }
        sprite.sheet[0] = static_cast<float>(nth(sheet, 0));
        sprite.sheet[1] = static_cast<float>(nth(sheet, 1));
    }
    return true;
}

bool load_portraits(SpriteRegistry& registry, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/ui/layouts/portraits.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node table = parsed.root(i);
        if (!table.is_list() || !table.head().is_atom("table")) {
            continue;
        }
        const std::string_view name = table.child_at(1).text();
        for (gsexp::Node row : table.children()) {
            const gsexp::FormView form(row);
            if (name == "portrait_bias_table" && form.head().is_atom("values")) {
                for (int k = 0; k < 28; ++k) {
                    registry.bias[k] = atom_int(row.child_at(static_cast<std::size_t>(k) + 1));
                }
            }
            if (!row.is_list() || !form.head().is_atom("row")) {
                continue;
            }
            if (name == "portrait_actor_table") {
                PortraitActor actor;
                actor.texture_asset = form.get_int("texture").value_or(-1);
                for (int k = 0; k < 9; ++k) {
                    actor.expression[k] = nth(form.find("expression"), k);
                }
                registry.actors.push_back(actor);
            } else if (name == "portrait_sprite_table") {
                PortraitSprite sprite;
                sprite.base = form.get_int("base").value_or(-1);
                sprite.overlay = form.get_int("overlay").value_or(-1);
                for (int k = 0; k < 3; ++k) {
                    sprite.phase[k] = nth(form.find("phase"), k);
                    sprite.phase_offset[k][0] = nth(form.find("phase_offset"), k * 2);
                    sprite.phase_offset[k][1] = nth(form.find("phase_offset"), k * 2 + 1);
                }
                sprite.overlay_offset[0] = nth(form.find("overlay_offset"), 0);
                sprite.overlay_offset[1] = nth(form.find("overlay_offset"), 1);
                registry.portraits.push_back(sprite);
            }
        }
    }
    if (registry.actors.size() != 28 || registry.portraits.size() != 180) {
        error = "portraits.sexp: expected 28 actors and 180 portrait sprites";
        return false;
    }
    return true;
}

bool load_texture_sets(SpriteRegistry& registry, const std::string& data_dir, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/manifest.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::Node node = parsed.root(i);
        const gsexp::FormView form(node);
        if (!form.head().is_atom("texture-set")) {
            continue;
        }
        TextureSetFiles set;
        set.first_slot = nth(form.find("slots"), 0);
        const std::string dir = data_dir + "/" + form.get_string("dir").value_or("");
        for (gsexp::Node texture : node.children()) {
            const gsexp::FormView entry(texture);
            if (!texture.is_list() || !entry.head().is_atom("texture")) {
                continue;
            }
            const int index = entry.get_int("index").value_or(-1);
            if (index < 0) {
                continue;
            }
            if (static_cast<std::size_t>(index) >= set.files.size()) {
                set.files.resize(static_cast<std::size_t>(index) + 1);
            }
            set.files[static_cast<std::size_t>(index)] =
                dir + "/" + entry.get_string("file").value_or("");
        }
        registry.texture_sets[form.get_int("asset").value_or(-1)] = set;
    }
    return true;
}

} // namespace

int overlay_pass(int layer) {
    if (layer >= 0 && layer <= 8) {
        return 0;
    }
    if (layer == 0x7FFF) {
        return 15;
    }
    if (layer >= 0x1000 && layer < 0x7FFF) {
        return 14;
    }
    return 12;
}

bool overlay_clipped(int layer) {
    const int pass = overlay_pass(layer);
    return pass == 0 || pass == 12;
}

void submit_quad(Draw2D& draw, Quad2D quad) {
    quad.sequence = static_cast<int>(draw.quads.size());
    draw.quads.push_back(quad);
}

void sort_quads(Draw2D& draw) {
    std::stable_sort(draw.quads.begin(), draw.quads.end(), [](const Quad2D& a, const Quad2D& b) {
        const int pass_a = overlay_pass(a.layer);
        const int pass_b = overlay_pass(b.layer);
        if (pass_a != pass_b) {
            return pass_a < pass_b;
        }
        return a.layer < b.layer;
    });
}

bool load_sprite_registry(SpriteRegistry& registry, const std::string& data_dir,
                          std::string& error) {
    return load_images(registry, data_dir, error) && load_portraits(registry, data_dir, error) &&
           load_texture_sets(registry, data_dir, error);
}

bool bind_texture_set(SlotBindings& bindings, const SpriteRegistry& registry, int asset,
                      int first_slot) {
    const auto found = registry.texture_sets.find(asset);
    if (found == registry.texture_sets.end()) {
        return false;
    }
    const int slot = first_slot >= 0 ? first_slot : found->second.first_slot;
    for (std::size_t k = 0; k < found->second.files.size(); ++k) {
        bindings.files[slot + static_cast<int>(k)] = found->second.files[k];
    }
    return true;
}

void submit_sprite(Draw2D& draw, const SpriteRegistry& registry, int id, float x, float y,
                   int layer, std::uint32_t color, float alpha, int bias) {
    if (id < 0 || static_cast<std::size_t>(id) >= registry.sprites.size()) {
        return;
    }
    const RegistrySprite& sprite = registry.sprites[static_cast<std::size_t>(id)];
    if (sprite.slot < 0) {
        return;
    }
    Quad2D quad;
    quad.slot = sprite.slot + bias;
    quad.u0 = sprite.rect[0];
    quad.v0 = sprite.rect[1];
    quad.u1 = sprite.rect[2];
    quad.v1 = sprite.rect[3];
    quad.sheet_w = sprite.sheet[0];
    quad.sheet_h = sprite.sheet[1];
    quad.x = x;
    quad.y = y;
    quad.w = sprite.rect[2] - sprite.rect[0];
    quad.h = sprite.rect[3] - sprite.rect[1];
    quad.color = color;
    quad.alpha = alpha;
    quad.layer = layer;
    submit_quad(draw, quad);
}
