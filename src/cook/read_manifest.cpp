// data/manifest.sexp and data/ui/images.sexp read for the cook's plan.
#include "cook/cook_read.hpp"
#include "cook/sexp_read.hpp"

namespace {

int int_of(gsexp::Node node) {
    return node.valid() ? std::atoi(std::string(node.text()).c_str()) : -1;
}

// "a02_aeronmodel+0x2d2800" -> a02_aeronmodel, "Media/scroll/wild_tex.bin" -> wild_tex
std::string cabinet_of(const std::string& source) {
    std::string name = source.substr(0, source.find('+'));
    name = name.substr(name.find_last_of('/') + 1);
    name = name.substr(0, name.find('.'));
    for (char& c : name) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : keep ? c : '_';
    }
    return name;
}

void read_texture_rows(gsexp::Node set, const std::string& folder, ManifestAsset& asset) {
    for (gsexp::Node row : set.children()) {
        if (!row.is_list() || !row.head().is_atom("texture")) {
            continue;
        }
        const gsexp::FormView form(row);
        ManifestTexture texture;
        texture.file = folder + "/" + form.get_string("file").value_or("");
        texture.format = std::string(form.find("format").child_at(1).text());
        const gsexp::Node size = form.find("size");
        texture.width = int_of(size.child_at(1));
        texture.height = int_of(size.child_at(2));
        asset.textures.push_back(texture);
    }
}

} // namespace

bool read_manifest(const std::string& data_dir, ManifestRead& out, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/manifest.sexp", parsed, error)) {
        error += ": run pc_extract first (docs/assets.md)";
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const std::string kind(form.head().text());
        const std::string file = form.get_string("file").value_or("");
        if (kind == "model-set" || kind == "texture-set" || kind == "motion-set" ||
            kind == "collision-grid") {
            ManifestAsset asset;
            asset.id = form.get_int("asset").value_or(-1);
            asset.kind = kind;
            asset.file = kind == "texture-set" ? form.get_string("dir").value_or("") : file;
            asset.cabinet = cabinet_of(form.get_string("source").value_or(""));
            const gsexp::Node slots = form.find("slots");
            if (slots.valid() && slots.child_count() >= 3) {
                asset.first_slot = int_of(slots.child_at(1));
                asset.slot_count = int_of(slots.child_at(2));
            }
            asset.texture_set = form.get_int("texture-set").value_or(-1);
            if (kind == "texture-set") {
                read_texture_rows(form.node(), asset.file, asset);
            }
            if (asset.id >= 0 && !asset.file.empty()) {
                out.assets.push_back(asset);
            }
        } else if (kind == "ui-layouts") {
            out.layouts.push_back(file);
        } else if (kind == "ui-timeline") {
            out.timelines.push_back(file);
        } else if (kind == "table") {
            out.tables.push_back(file);
        } else if (kind == "text-lines" || kind == "text-strings" || kind == "scripts") {
            out.texts.push_back(file);
        } else if (kind == "nav-graph") {
            std::vector<int> selectors;
            const gsexp::Node list = form.find("selectors");
            for (std::size_t k = 1; list.valid() && k < list.child_count(); ++k) {
                selectors.push_back(int_of(list.child_at(k)));
            }
            out.navs.push_back({file, selectors});
        } else if (kind == "font") {
            out.font = file;
            out.font_map = form.get_string("map").value_or("");
        }
    }
    return true;
}

bool read_sprites(const std::string& data_dir, std::vector<SpriteRead>& out, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(data_dir + "/ui/images.sexp", parsed, error)) {
        return false;
    }
    for (std::size_t i = 0; i < parsed.root_count(); ++i) {
        const gsexp::FormView form(parsed.root(i));
        const bool standard = form.head().is_atom("standard-image");
        if (!standard && !form.head().is_atom("image")) {
            continue;
        }
        SpriteRead sprite;
        sprite.file = form.get_string("file").value_or("");
        sprite.standard = standard;
        bool head = true;
        for (gsexp::Node screen : form.find("screens").children()) {
            if (!head) {
                sprite.screens.emplace_back(screen.text());
            }
            head = false;
        }
        if (!sprite.file.empty()) {
            out.push_back(sprite);
        }
    }
    return true;
}
