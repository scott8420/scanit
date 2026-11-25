#include "CropView.hpp"

#include <gdkmm/general.h>

#include <algorithm>
#include <cmath>

namespace scanit {

namespace escl = model::escl;

namespace {
constexpr double kMargin = 14;     // px around the glass
constexpr double kGrab   = 10;     // px: how near an edge counts as on it
constexpr int    kMinBox = 75;     // 1/4": a click without a drag makes no box
}  // namespace

CropView::CropView() {
    set_name("scan.crop");
    set_hexpand(true);
    set_vexpand(true);
    set_content_width(320);
    set_content_height(420);
    set_draw_func(sigc::mem_fun(*this, &CropView::draw));
    set_tooltip_text("Drag to draw the crop box; drag inside it to move it, at an edge to size it");

    m_drag = Gtk::GestureDrag::create();
    m_drag->signal_drag_begin().connect(sigc::mem_fun(*this, &CropView::on_begin));
    m_drag->signal_drag_update().connect(sigc::mem_fun(*this, &CropView::on_update));
    m_drag->signal_drag_end().connect(sigc::mem_fun(*this, &CropView::on_end));
    add_controller(m_drag);
}

void CropView::set_bed(int w, int h) {
    if (w <= 0 || h <= 0) return;
    m_bed_w = w;
    m_bed_h = h;
    queue_draw();
}
void CropView::set_preview(const Glib::RefPtr<Gdk::Pixbuf>& pix) { m_preview = pix; queue_draw(); }
void CropView::set_region(const escl::Region& r) { m_region = r; queue_draw(); }
void CropView::set_content(const std::optional<escl::Region>& r) { m_content = r; queue_draw(); }

CropView::Fit CropView::fit() const {
    Fit f;
    const double w = std::max(1, get_width()) - 2 * kMargin, h = std::max(1, get_height()) - 2 * kMargin;
    f.scale = std::max(1e-6, std::min(w / m_bed_w, h / m_bed_h));
    f.ox = (get_width() - m_bed_w * f.scale) / 2;
    f.oy = (get_height() - m_bed_h * f.scale) / 2;
    return f;
}

void CropView::draw(const Cairo::RefPtr<Cairo::Context>& cr, int, int) {
    const Fit f = fit();
    const double gx = f.ox, gy = f.oy, gw = m_bed_w * f.scale, gh = m_bed_h * f.scale;

    // The glass.
    cr->set_source_rgba(1, 1, 1, 0.07);
    cr->rectangle(gx, gy, gw, gh);
    cr->fill();
    if (m_preview) {
        cr->save();
        cr->rectangle(gx, gy, gw, gh);
        cr->clip();
        cr->translate(gx, gy);
        cr->scale(gw / m_preview->get_width(), gh / m_preview->get_height());
        Gdk::Cairo::set_source_pixbuf(cr, m_preview, 0, 0);
        cr->paint();
        cr->restore();
    } else {
        cr->set_source_rgba(1, 1, 1, 0.45);
        cr->select_font_face("Sans", Cairo::ToyFontFace::Slant::NORMAL, Cairo::ToyFontFace::Weight::NORMAL);
        cr->set_font_size(13);
        Cairo::TextExtents te;
        const char* hint = "Preview to see the glass";
        cr->get_text_extents(hint, te);
        cr->move_to(gx + (gw - te.width) / 2, gy + gh / 2);
        cr->show_text(hint);
    }
    cr->set_source_rgba(1, 1, 1, 0.35);
    cr->set_line_width(1);
    cr->rectangle(gx + 0.5, gy + 0.5, gw - 1, gh - 1);
    cr->stroke();

    // The crop box: dim outside it, a line round it, grips at the corners.
    const double rx = gx + m_region.x * f.scale, ry = gy + m_region.y * f.scale;
    const double rw = m_region.w * f.scale, rh = m_region.h * f.scale;
    cr->save();
    cr->set_fill_rule(Cairo::Context::FillRule::EVEN_ODD);
    cr->rectangle(gx, gy, gw, gh);
    cr->rectangle(rx, ry, rw, rh);
    cr->set_source_rgba(0, 0, 0, 0.45);
    cr->fill();
    cr->restore();

    // The preview's content, dashed.
    if (m_content) {
        cr->save();
        cr->set_source_rgba(1.0, 0.78, 0.25, 0.9);
        cr->set_line_width(1.5);
        std::vector<double> dash{5, 4};
        cr->set_dash(dash, 0);
        cr->rectangle(gx + m_content->x * f.scale, gy + m_content->y * f.scale, m_content->w * f.scale,
                      m_content->h * f.scale);
        cr->stroke();
        cr->restore();
    }

    cr->set_source_rgba(1, 1, 1, 0.95);
    cr->set_line_width(2);
    cr->rectangle(rx, ry, rw, rh);
    cr->stroke();
    const double g = 7;
    for (double cx : {rx, rx + rw})
        for (double cy : {ry, ry + rh}) {
            cr->rectangle(cx - g / 2, cy - g / 2, g, g);
            cr->fill();
        }

    // Its size, inside the top-left corner.
    const std::string size = escl::inches_label(m_region.w, m_region.h);
    cr->set_font_size(12);
    Cairo::TextExtents te;
    cr->get_text_extents(size, te);
    if (rw > te.width + 16 && rh > 26) {
        cr->set_source_rgba(0, 0, 0, 0.55);
        cr->rectangle(rx + 4, ry + 4, te.width + 10, 18);
        cr->fill();
        cr->set_source_rgba(1, 1, 1, 0.95);
        cr->move_to(rx + 9, ry + 17);
        cr->show_text(size);
    }
}

void CropView::on_begin(double x, double y) {
    const Fit f = fit();
    const double ux = (x - f.ox) / f.scale, uy = (y - f.oy) / f.scale;
    const double grab = kGrab / f.scale;
    const auto& r = m_region;
    m_start = r;
    m_sx = std::clamp(ux, 0.0, double(m_bed_w));
    m_sy = std::clamp(uy, 0.0, double(m_bed_h));
    const bool in_y = uy > r.y - grab && uy < r.y + r.h + grab;
    const bool in_x = ux > r.x - grab && ux < r.x + r.w + grab;
    m_mode = None;
    if (in_y && std::abs(ux - r.x) < grab) m_mode |= L;
    if (in_y && std::abs(ux - (r.x + r.w)) < grab) m_mode |= R;
    if (in_x && std::abs(uy - r.y) < grab) m_mode |= T;
    if (in_x && std::abs(uy - (r.y + r.h)) < grab) m_mode |= B;
    if (m_mode == None) {
        const bool inside = ux > r.x && ux < r.x + r.w && uy > r.y && uy < r.y + r.h;
        // Inside a box smaller than the glass moves it; inside the whole-glass
        // box (or outside any box) draws a new one.
        const bool whole = r.x == 0 && r.y == 0 && r.w >= m_bed_w && r.h >= m_bed_h;
        m_mode = inside && !whole ? Move : New;
    }
    signal_crop_begin.emit();
}

void CropView::on_update(double dx, double dy) {
    if (m_mode == None) return;
    const Fit f = fit();
    const int du = int(std::lround(dx / f.scale)), dv = int(std::lround(dy / f.scale));
    escl::Region r = m_start;
    if (m_mode == Move) {
        r.x = std::clamp(m_start.x + du, 0, std::max(0, m_bed_w - r.w));
        r.y = std::clamp(m_start.y + dv, 0, std::max(0, m_bed_h - r.h));
    } else if (m_mode == New) {
        const int ax = int(m_sx), ay = int(m_sy);
        const int bx = std::clamp(ax + du, 0, m_bed_w), by = std::clamp(ay + dv, 0, m_bed_h);
        if (std::abs(bx - ax) < kMinBox || std::abs(by - ay) < kMinBox) return;
        r = {std::min(ax, bx), std::min(ay, by), std::abs(bx - ax), std::abs(by - ay)};
    } else {
        int x0 = m_start.x, y0 = m_start.y, x1 = m_start.x + m_start.w, y1 = m_start.y + m_start.h;
        if (m_mode & L) x0 = std::clamp(x0 + du, 0, x1 - kMinBox);
        if (m_mode & R) x1 = std::clamp(x1 + du, x0 + kMinBox, m_bed_w);
        if (m_mode & T) y0 = std::clamp(y0 + dv, 0, y1 - kMinBox);
        if (m_mode & B) y1 = std::clamp(y1 + dv, y0 + kMinBox, m_bed_h);
        r = {x0, y0, x1 - x0, y1 - y0};
    }
    signal_crop_move.emit(r);
}

void CropView::on_end(double, double) {
    m_mode = None;
    signal_crop_end.emit();
}

}  // namespace scanit
