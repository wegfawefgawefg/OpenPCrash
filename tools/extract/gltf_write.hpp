#pragma once
// A model set as glTF 2.0 (one .gltf with its .bin beside it), in the port's frame: the original's
// left-handed D3D frame with z negated, so right-handed and y up as glTF wants (docs/assets.md,
// "Frame"). One root node per object (its model slot), the object's node tree under it with each
// node's sections as one mesh, a material per section with the original's fields in extras.
#include "model_decode.hpp"

#include <string>
#include <vector>

// where a section's tex[0] looks: the PNGs of one texture set
struct TextureBinding {
    std::string dir_uri;      // "../textures/1558_b00_shinjuku", empty for none
    int count = 0;            // pictures in the set
    bool part_colour = false; // a fighter part: tex[0] is forced to 0 and the set is the paint's
};

struct GltfModelInfo {
    int asset = -1;
    int first_slot = -1; // the descriptor's first model slot, -1 for none
    TextureBinding textures;
};

// writes path (.gltf) and the .bin beside it; false with error on a failed write
bool write_model_gltf(const std::string& path, const std::vector<DiscObject>& objects,
                      const GltfModelInfo& info, std::string& error);
