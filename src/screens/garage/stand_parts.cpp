// The stands' loose parts (decomp src/od/u_082de0.c): an incomplete machine laid out as its parts
// (scoobee_parts_draw_spread), the one part under a shop list's cursor growing in
// (model_viewer_fit_part, model_viewer_draw_part_effect). Each part is its static
// model at rest, submitted in pass 2 with flags 0x18040000 (model_submit_with_flag), in the
// original's frame turned into the port's (z flipped). The placement tables are sv_attach.c's,
// read from the ui data into StandTables (model_stand.cpp).
#include "render/mesh_queue.hpp"
#include "screens/garage/model_stand.hpp"
#include "screens/ui/ui.hpp"

namespace {

// model_submit_with_flag's model flags: opaque, no specular, 0x10000000
constexpr std::uint32_t part_flags = 0x18040000;

// a matrix of the original's frame as the port's (z flipped both sides)
Mat4 port_of(const Mat4& m) {
    const Mat4 flip = mat_scaling(1, 1, -1);
    return mat_multiply(mat_multiply(flip, m), flip);
}

// one part's static model at an original-frame matrix, in the build's paint
void part_item(const MechParts& mech, int part, const Mat4& world, std::vector<PassItem>& items) {
    if (!mech.loaded[part] || mech.models[part].objects.empty()) {
        return;
    }
    const std::size_t first = items.size();
    queue_object(mech.models[part].objects[0], mech.gpu[part].object_mesh[0], 0, port_of(world), 2,
                 part_flags, mat_identity(), items);
    for (std::size_t i = first; i < items.size(); ++i) {
        items[i].item.paint = mech.paint;
    }
}

} // namespace

void stand_parts_present(const ShopBuild& b, bool out[6]) {
    out[part_body] = b.body >= 0;
    out[part_legs] = b.leg >= 0;
    for (int m = 0; m < 4; ++m) {
        out[part_arm_a + m] = b.weapon[m] >= 0;
    }
}

int stand_part_place(const StandTables& tables, int kind, const MechParts& mech) {
    const auto row_of = [](const std::vector<int>& table, int index) {
        return index >= 0 && index < static_cast<int>(table.size())
                   ? table[static_cast<std::size_t>(index)]
                   : -1;
    };
    switch (kind) {
    case 0:
        return row_of(tables.by_body, mech.body_class);
    case 1:
        return row_of(tables.by_leg, mech.leg_class);
    case 2:
        return row_of(tables.by_arm, mech.weapon_class[0]);
    case 3:
        return row_of(tables.by_back, mech.weapon_class[2]);
    default:
        return -1;
    }
}

void stand_parts_items(const StandTables& tables, const StandPose& p,
                       std::vector<PassItem>& items) {
    if (!p.mech) {
        return;
    }
    const MechParts& mech = *p.mech;
    MatrixStack stack;
    stack_identity(stack);
    stack_translate(stack, p.point[0], p.point[1], p.point[2]);
    stack_rotate_y(stack, p.heading);
    if (p.spread >= 0) {
        // scoobee_parts_draw_spread: each part the build has at its own place, scaled
        const float(*rows)[6] = tables.spread[p.spread == 6 ? 1 : 0];
        for (int part = 0; part < part_count; ++part) {
            if (!p.present[part]) {
                continue;
            }
            const float* row = rows[part];
            stack_push(stack);
            stack_translate(stack, row[0], row[1], row[2]);
            stack_rotate_y(stack, row[4]);
            stack_scale(stack, p.scale, p.scale, p.scale);
            part_item(mech, part, stack_top(stack), items);
            stack_pop(stack);
        }
        return;
    }
    if (p.part_place < 0 || p.part_place >= static_cast<int>(tables.part_places.size())) {
        return;
    }
    // model_viewer_draw_part_effect: the part grown about a point 4 up, at its place row; the part
    // is the body, else the legs, else the last weapon the build has
    int part = -1;
    if (mech.loaded[part_body]) {
        part = part_body;
    } else if (mech.loaded[part_legs]) {
        part = part_legs;
    } else {
        for (int w : {part_arm_a, part_back_a, part_arm_b, part_back_b}) {
            if (mech.loaded[w]) {
                part = w;
            }
        }
    }
    if (part < 0) {
        return;
    }
    const float* row = tables.part_places[static_cast<std::size_t>(p.part_place)].data();
    stack_translate(stack, 0, 4, 0);
    stack_scale(stack, p.scale, p.scale, p.scale);
    stack_translate(stack, 0, -4, 0);
    stack_translate(stack, row[0], row[1], row[2]);
    stack_rotate_z(stack, row[5]);
    stack_rotate_y(stack, row[4]);
    stack_rotate_x(stack, row[3]);
    part_item(mech, part, stack_top(stack), items);
}
