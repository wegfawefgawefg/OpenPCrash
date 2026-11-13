// The cockpit instruments' queue (hud_draw_meters' model_render_submit calls, pass 11): each model
// the HUD picked (src/hud/hud_meters.cpp) with its gauge on its second node's scale.x
// (ModelNode +0x5C +0x38), its colour as the texture factor (flag 0x20) and its row of
// cockpit_uv_set's offsets, which the cockpitUV program adds to its uvs. Then the weapon
// readout's panels (hud_draw_readout in the world, src/hud/hud_readout.cpp): uv_override_submit's
// textured quads, each the unit quad with its sprite's own cut-out on stage 0 and its colour as
// the factor. They draw under the HUD's identity view (pass_view_set), so the view is only the
// port's z flip.
#include "effects/effects.hpp"
#include "hud/hud_cockpit.hpp"
#include "render/mesh_flags.hpp"
#include "render/mesh_queue.hpp"

void effects_queue_cockpit(Effects& fx, const HudCockpit& cockpit, const ModelObject& quad,
                           int quad_mesh, std::vector<PassItem>& items) {
    if (!cockpit.shown) {
        return;
    }
    const Mat4 view = mat_scaling(1, 1, -1);
    for (const CockpitModel& m : cockpit.models) {
        for (const auto& [slot, where] : fx.assets.slots) {
            if (slot != m.slot) {
                continue;
            }
            ViewModelSet& set = fx.assets.sets[static_cast<std::size_t>(where.first)];
            ModelObject& object = set.set.objects[static_cast<std::size_t>(where.second)];
            if (m.gauge >= 0 && object.nodes.size() > 1) {
                object.nodes[1].scale[0] = m.gauge;
            }
            const std::size_t first = items.size();
            queue_object(object, set.gpu.object_mesh[static_cast<std::size_t>(where.second)],
                         set.texture_base, port_matrix(m.world), 11, mesh_factor, view, items);
            const int row = m.slot - cockpit_first_model;
            for (std::size_t i = first; i < items.size(); ++i) {
                items[i].item.factor = m.factor;
                if (row >= 0 && row < cockpit_model_count) {
                    items[i].item.effect[0] = cockpit.uv[row][0];
                    items[i].item.effect[1] = cockpit.uv[row][1];
                }
            }
            break;
        }
    }
    if (quad_mesh < 0) {
        return;
    }
    for (const CockpitQuad& q : cockpit.quads) {
        if (q.texture < 0) {
            continue;
        }
        const std::size_t first = items.size();
        queue_object(quad, quad_mesh, 0, port_matrix(q.world), 11, mesh_factor, view, items);
        for (std::size_t i = first; i < items.size(); ++i) {
            items[i].item.factor = q.color;
            items[i].item.paint = q.texture;
        }
    }
}
