// PC: turning the machine on a stand by hand (owner, 2026-11-12). Retail has no control for it
// on any stand (docs/port/screens.md "The model stand": no stick rotation, no tilt, no zoom), so
// the pad keeps none. A left press inside the screen's stand area (clear of its menus' hit boxes)
// grabs the machine: it turns with the pointer while held and is let go on release, the press
// kept from the screen's own click handling and the drag from its hover. The two bound keys
// (PC CONFIG's BINDINGS, "TURN STAND LEFT / RIGHT") turn it while held. While either holds it the
// stand's own turn waits, and picks up from where the hand left it.
#include "input/bindings.hpp"
#include "screens/garage/model_stand.hpp"
#include "screens/ui/ui.hpp"

namespace {

// radians a pointer pixel (640x480 screen): a drag across the whole screen turns it about once
// and a half; the keys turn 3 degrees an update (a turn in two seconds)
constexpr float drag_radians = 0.015f;
constexpr float key_radians = 3.0f * 3.14159265f / 180.0f;

// something on the stand to turn
bool stand_has_machine(const ModelStand& stand) {
    if (stand.mode == 6) {
        return true; // the hangar's row
    }
    return stand.shown || stand.part || stand.line_shown >= 0;
}

} // namespace

void stand_turn_input(Ui& ui, ModelStand& stand) {
    stand.user_turn = 0.0f;
    MenuPointer& pointer = ui.pointer;
    // the grab: a fresh press inside the area while a machine is up
    if (!stand.dragging && pointer.left && pointer_inside(pointer, stand.area) &&
        stand_has_machine(stand)) {
        stand.dragging = true;
        stand.drag_x = pointer.x;
        pointer.left = false; // the screen's click handling never sees it
    }
    if (stand.dragging) {
        if (!pointer.held || !pointer.visible) {
            stand.dragging = false; // let go (or the pointer hid on a key or a pad press)
        } else {
            // the near side follows the pointer: a rising heading carries it to the screen's left
            // (matrix_rotate_y about the camera's +z view), so a drag right lowers it
            stand.user_turn -= (pointer.x - stand.drag_x) * drag_radians;
            stand.drag_x = pointer.x;
            pointer.moved = false; // no hovering menus on the way
        }
    }
    // the keys as a drag: left carries the near side left
    const int keys =
        ((ui.pc_held & pc_stand_left) ? 1 : 0) - ((ui.pc_held & pc_stand_right) ? 1 : 0);
    stand.user_turn += static_cast<float>(keys) * key_radians;
    stand.user_held = stand.dragging || (ui.pc_held & (pc_stand_left | pc_stand_right)) != 0;
}
