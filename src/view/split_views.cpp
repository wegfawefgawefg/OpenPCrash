#include "view/split_views.hpp"

namespace {

// viewport_layouts 0x25B920 (x, y, width, height of each row)
constexpr float one_view[4] = {32, 16, 576, 448};
constexpr float two_views[2][4] = {{32, 16, 576, 220}, {32, 240, 576, 220}};
constexpr float four_views[4][4] = {
    {32, 16, 284, 220}, {320, 16, 284, 220}, {32, 240, 284, 220}, {320, 240, 284, 220}};

void place(SplitView& view, const float* rect) {
    for (int i = 0; i < 4; ++i) {
        view.rect[i] = rect[i];
        view.drawn[i] = rect[i];
    }
}

// the views' 3D stretched over the whole screen: the rectangle they cover together (retail's
// safe area, short of its right and bottom edges by the gaps between views) to the 640x480, so no
// dead strip is left at the window's edges; the gaps between them stay as dividers
void stretch(SplitLayout& layout) {
    float x0 = 640, y0 = 480, x1 = 0, y1 = 0;
    for (int v = 0; v < layout.count; ++v) {
        const float* r = layout.views[static_cast<std::size_t>(v)].rect;
        x0 = r[0] < x0 ? r[0] : x0;
        y0 = r[1] < y0 ? r[1] : y0;
        x1 = r[0] + r[2] > x1 ? r[0] + r[2] : x1;
        y1 = r[1] + r[3] > y1 ? r[1] + r[3] : y1;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    const float kx = 640.0f / (x1 - x0), ky = 480.0f / (y1 - y0);
    for (int v = 0; v < layout.count; ++v) {
        SplitView& view = layout.views[static_cast<std::size_t>(v)];
        view.drawn[0] = (view.rect[0] - x0) * kx;
        view.drawn[1] = (view.rect[1] - y0) * ky;
        view.drawn[2] = view.rect[2] * kx;
        view.drawn[3] = view.rect[3] * ky;
    }
}

} // namespace

SplitLayout split_layout(unsigned seat_mask, bool safe_rect) {
    SplitLayout layout;
    int seats[4];
    int count = 0;
    for (int seat = 0; seat < 4; ++seat) {
        if (seat_mask >> seat & 1u) {
            seats[count++] = seat;
        }
    }
    layout.view_count = count < 1 ? 1 : count;
    if (count <= 1) {
        layout.count = 1;
        layout.views[0].seat = count == 1 ? seats[0] : 0;
        place(layout.views[0], one_view);
    } else if (count == 2) {
        // the n-th seat gets the n-th strip
        layout.count = 2;
        for (int n = 0; n < 2; ++n) {
            layout.views[static_cast<std::size_t>(n)].seat = seats[n];
            place(layout.views[static_cast<std::size_t>(n)], two_views[n]);
        }
    } else {
        // every quarter by its seat; an empty one gets no view
        layout.count = count;
        for (int n = 0; n < count; ++n) {
            layout.views[static_cast<std::size_t>(n)].seat = seats[n];
            place(layout.views[static_cast<std::size_t>(n)], four_views[seats[n]]);
        }
    }
    if (!safe_rect) {
        stretch(layout);
    }
    return layout;
}

int split_view_of(const SplitLayout& layout, int seat) {
    for (int v = 0; v < layout.count; ++v) {
        if (layout.views[static_cast<std::size_t>(v)].seat == seat) {
            return v;
        }
    }
    return -1;
}
