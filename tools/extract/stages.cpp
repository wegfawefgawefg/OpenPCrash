#include "stages.hpp"

#include "addresses.hpp"
#include "disc.hpp"
#include "extract.hpp"
#include "sexp_out.hpp"
#include "stage_lighting.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

const char* stage_names[8] = {
    "Shinjuku day",           "Shinjuku night",          "Shibuya day",
    "Shibuya night",          "Umihotaru day",           "Umihotaru night",
    "test arena (choco) day", "test arena (choco) night"};

} // namespace

bool extract_stages(const Disc& disc, const ExtractOptions& options, Manifest& manifest) {
    const Xbe& xbe = disc.xbe;
    std::string text =
        ";; stage_scene: per stage selector (stage * 2 + night), from the XBE tables named below.\n"
        ";; Positions are in the port frame (the original's with z negated), yaws negated to "
        "match.\n";
    text += "(lod-distances (mid " + sx_float(xbe_f32(xbe, xbe_at::stage_lod_mid_distance)) +
            ") (far " + sx_float(xbe_f32(xbe, xbe_at::stage_lod_far_distance)) +
            "))  ; D_002211BC, D_002211C0\n";
    for (int selector = 0; selector < 8; ++selector) {
        text += "(stage (selector " + sx_int(selector) + ") (name " +
                sx_string(stage_names[selector]) + ")\n";
        text += "  (model-set " + sx_int(xbe_i32(xbe, xbe_at::stage_model_assets + selector * 4)) +
                ")  ; stage_model_assets\n";
        text += "  (collision " +
                sx_int(xbe_i32(xbe, xbe_at::stage_collision_assets + selector * 4)) +
                ")  ; stage_collision_assets\n";

        // stage_entry_points: {float (*points)[3]; int count; float *yaws}
        const std::uint32_t entry = xbe_at::stage_entry_points + selector * 12;
        const std::uint32_t points = xbe_u32(xbe, entry);
        const int count = xbe_i32(xbe, entry + 4);
        const std::uint32_t yaws = xbe_u32(xbe, entry + 8);
        text += "  (entry-points  ; stage_entry_points\n";
        for (int i = 0; i < count; ++i) {
            const float x = xbe_f32(xbe, points + i * 12), y = xbe_f32(xbe, points + i * 12 + 4);
            const float z = xbe_f32(xbe, points + i * 12 + 8), yaw = xbe_f32(xbe, yaws + i * 4);
            text += "    (point " + sx_float(x) + " " + sx_float(y) + " " + sx_float(-z) +
                    " (yaw " + sx_float(-yaw) + "))\n";
        }
        text += "  )\n";

        // StageObject {model, layer, use_submit, animate} rows ended by model -1, with the same row
        // of StageObjectLod {near, mid, far} and of the zone visibility bytes [5]
        const std::uint32_t objects = xbe_u32(xbe, xbe_at::stage_objects + selector * 4);
        const std::uint32_t lods = xbe_u32(xbe, xbe_at::stage_object_lods + selector * 4);
        const std::uint32_t zones = xbe_u32(xbe, xbe_at::stage_object_zones + selector * 4);
        text += "  (objects  ; D_00221204 rows, D_00221224 lods, D_00221244 zones\n";
        for (int row = 0;; ++row) {
            const std::uint32_t at = objects + row * 16;
            const int model = xbe_i32(xbe, at);
            if (model == -1 || row > 1000) {
                break;
            }
            text += "    (object (model " + sx_int(model) + ") (layer " +
                    sx_int(xbe_i32(xbe, at + 4)) + ") (submit " + sx_int(xbe_u8(xbe, at + 8)) +
                    ") (animate " + sx_int(xbe_i32(xbe, at + 12)) + ") (lods " +
                    sx_int(xbe_i32(xbe, lods + row * 12)) + " " +
                    sx_int(xbe_i32(xbe, lods + row * 12 + 4)) + " " +
                    sx_int(xbe_i32(xbe, lods + row * 12 + 8)) + ") (zones";
            for (int zone = 0; zone < 5; ++zone) {
                text += " " + sx_int(xbe_u8(xbe, zones + row * 5 + zone));
            }
            text += "))\n";
        }
        text += "  ))\n";
    }

    const std::string file = "tables/stage_scene.sexp";
    std::error_code made;
    std::filesystem::create_directories(options.out_dir + "/tables", made);
    std::ofstream out(options.out_dir + "/" + file, std::ios::binary);
    out << text;
    if (!out) {
        std::fprintf(stderr, "stages: can't write %s\n", file.c_str());
        return false;
    }
    manifest.entries.push_back(
        "(table (name stage-scene) (file " + sx_string(file) +
        ") (xbe stage_model_assets 0x2211c4 stage_objects 0x221204 stage_entry_points 0x2014b8))");

    // the lighting of each selector
    const std::string lighting_file = "tables/stage_lighting.sexp";
    std::ofstream lighting(options.out_dir + "/" + lighting_file, std::ios::binary);
    lighting << stage_lighting_text(xbe);
    if (!lighting) {
        std::fprintf(stderr, "stages: can't write %s\n", lighting_file.c_str());
        return false;
    }
    manifest.entries.push_back("(table (name stage-lighting) (file " + sx_string(lighting_file) +
                               ") (xbe records 0x2013e8 stage_lights 0x281648))");
    std::printf("stages: 8 selectors\n");
    return true;
}
