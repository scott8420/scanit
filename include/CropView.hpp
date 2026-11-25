#pragma once
// CropView -- the glass, drawn: the preview (when there is one), the crop box
// over it, and the preview's detected content as a dashed outline. Dragging
// draws a new box, moves the box, or pulls an edge / corner.
//
// It holds no plan of its own. A drag says begin / move(region) / end, and the
// Shell sends those through model::plan::Book's crop gesture (one undo step);
// the Shell paints the plan's region back in with set_region(). Everything is
// in eSCL's 1/300 inch; the view converts to pixels only to draw and to hit.

#include "model/Escl.hpp"

#include <gdkmm/pixbuf.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/gesturedrag.h>

#include <optional>

namespace scanit {

class CropView : public Gtk::DrawingArea {
public:
    CropView();

    void set_bed(int w, int h);                                  // 1/300"
    void set_preview(const Glib::RefPtr<Gdk::Pixbuf>& pix);      // the whole bed; null clears
    void set_region(const model::escl::Region& r);
    void set_content(const std::optional<model::escl::Region>& r);

    sigc::signal<void()>                            signal_crop_begin;
    sigc::signal<void(const model::escl::Region&)>  signal_crop_move;
    sigc::signal<void()>                            signal_crop_end;

private:
    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void on_begin(double x, double y);
    void on_update(double dx, double dy);
    void on_end(double dx, double dy);

    // Widget pixels <-> 1/300".
    struct Fit { double ox = 0, oy = 0, scale = 1; };
    Fit fit() const;

    int m_bed_w = 2550, m_bed_h = 3508;
    model::escl::Region                   m_region{0, 0, 2550, 3508};
    std::optional<model::escl::Region>    m_content;
    Glib::RefPtr<Gdk::Pixbuf>             m_preview;
    Glib::RefPtr<Gtk::GestureDrag>        m_drag;

    // The drag in progress.
    enum Edge { None = 0, L = 1, R = 2, T = 4, B = 8, Move = 16, New = 32 };
    int                 m_mode = None;
    model::escl::Region m_start;
    double              m_sx = 0, m_sy = 0;   // press, in units
};

}  // namespace scanit
