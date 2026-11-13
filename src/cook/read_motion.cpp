// data/motion/*.sexp read back into a MotionSet (the extractor's motion family), for the cook.
#include "cook/cook_read.hpp"
#include "cook/sexp_read.hpp"

void read_motion_curve(gsexp::Node node, MotionCurve& curve) {
    if (!node.valid() || !node.is_list() || !node.head().is_atom("curve")) {
        return;
    }
    curve.present = true;
    curve.mode = atom_hex(node.child_at(1));
    const gsexp::FormView form(node);
    for (gsexp::Node time : form.find("times").children()) {
        if (!time.is_list() && !time.is_atom("times")) {
            curve.times.push_back(static_cast<std::uint16_t>(atom_int(time)));
        }
    }
    for (gsexp::Node value : form.find("values").children()) {
        if (!value.is_list() && !value.is_atom("values")) {
            curve.values.push_back(atom_float(value));
        }
    }
}

namespace {

void read_group(gsexp::Node group, MotionCurve curves[3]) {
    if (!group.valid()) {
        return;
    }
    for (int axis = 0; axis < 3; ++axis) {
        read_motion_curve(group.child_at(1 + axis), curves[axis]);
    }
}

} // namespace

bool read_motion_set(const std::string& path, MotionSet& out, std::string& error) {
    gsexp::ParseResult parsed;
    if (!parse_sexp_file(path, parsed, error)) {
        return false;
    }
    const gsexp::FormView set(parsed.root(0));
    out.asset = set.get_int("asset").value_or(-1);
    for (gsexp::Node clip : set.find("slot-clips").children()) {
        if (!clip.is_atom("slot-clips")) {
            out.slot_clips.push_back(atom_int(clip));
        }
    }
    for (gsexp::Node child : set.node().children()) {
        if (!child.is_list() || !child.head().is_atom("clip")) {
            continue;
        }
        // clips come in the file by offset, each named by its number
        const int number = atom_int(child.child_at(1));
        if (number < 0 || number > 4096) {
            continue;
        }
        MotionClip clip;
        for (gsexp::Node track_node : child.children()) {
            if (!track_node.is_list() || !track_node.head().is_atom("track")) {
                continue;
            }
            const gsexp::FormView form(track_node);
            MotionTrack track;
            track.flags = atom_hex(form.find("flags").child_at(1));
            track.null = form.find("null").valid();
            read_group(form.find("rotate"), track.rotate);
            read_group(form.find("translate"), track.translate);
            read_group(form.find("steps"), track.steps);
            clip.push_back(std::move(track));
        }
        if (number >= static_cast<int>(out.clips.size())) {
            out.clips.resize(number + 1);
        }
        out.clips[number] = std::move(clip);
    }
    return true;
}
