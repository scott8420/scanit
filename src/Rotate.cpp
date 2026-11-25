#include "Rotate.hpp"

#include <gdkmm/pixbuf.h>
#include <gdkmm/pixbufloader.h>
#include <glibmm/miscutils.h>

#include <filesystem>
#include <fstream>
#include <iterator>

namespace scanit::rotate {

namespace fs = std::filesystem;

namespace {

int norm(int turns) { return ((turns % 4) + 4) % 4; }

// gdk-pixbuf's names: COUNTERCLOCKWISE = 90, UPSIDEDOWN = 180, CLOCKWISE = 270.
Gdk::Pixbuf::Rotation rotation(int turns) {
    switch (norm(turns)) {
        case 1: return Gdk::Pixbuf::Rotation::CLOCKWISE;
        case 2: return Gdk::Pixbuf::Rotation::UPSIDEDOWN;
        case 3: return Gdk::Pixbuf::Rotation::COUNTERCLOCKWISE;
    }
    return Gdk::Pixbuf::Rotation::NONE;
}

// The gdk-pixbuf saver for a MIME type or a file extension; "" if none.
std::string saver(const std::string& kind) {
    if (kind == "image/png" || kind == ".png") return "png";
    if (kind == "image/jpeg" || kind == ".jpg" || kind == ".jpeg") return "jpeg";
    if (kind == "image/tiff" || kind == ".tif" || kind == ".tiff") return "tiff";
    return "";
}

bool encode(const Glib::RefPtr<Gdk::Pixbuf>& pix, const std::string& type, std::vector<char>& out,
            std::string* err) {
    try {
        gchar* buf = nullptr;
        gsize len = 0;
        if (type == "jpeg") pix->save_to_buffer(buf, len, type, {"quality"}, {"95"});
        else                pix->save_to_buffer(buf, len, type);
        out.assign(buf, buf + len);
        g_free(buf);
        return true;
    } catch (const Glib::Error& e) {
        if (err) *err = "could not encode " + type + ": " + e.what();
        return false;
    }
}

Glib::RefPtr<Gdk::Pixbuf> decode(const std::vector<char>& data, std::string* err) {
    try {
        auto loader = Gdk::PixbufLoader::create();
        loader->write(reinterpret_cast<const guint8*>(data.data()), data.size());
        loader->close();
        return loader->get_pixbuf();
    } catch (const Glib::Error& e) {
        if (err) *err = std::string("could not read the image: ") + e.what();
        return {};
    }
}

bool read_all(const std::string& path, std::vector<char>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

bool write_all(const std::string& path, const std::vector<char>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// Where a file's as-saved bytes are kept: the whole path, flattened, so two
// books' name_0001.png never share a copy.
std::string kept_copy(const std::string& path) {
    std::string flat = path;
    for (char& c : flat) if (c == '/') c = '%';
    return Glib::get_user_cache_dir() + "/scanit/originals/" + flat;
}

}  // namespace

void forget(const std::string& path) {
    std::error_code ec;
    fs::remove(kept_copy(path), ec);
}

bool rotate_bytes(std::vector<char>& data, const std::string& mime, int turns, std::string* err) {
    if (norm(turns) == 0) return true;
    const std::string type = saver(mime);
    if (type.empty()) {
        if (err) *err = "cannot rotate " + mime;
        return false;
    }
    auto pix = decode(data, err);
    if (!pix) return false;
    auto turned = pix->rotate_simple(rotation(turns));
    std::vector<char> out;
    if (!encode(turned, type, out, err)) return false;
    data.swap(out);
    return true;
}

bool turn_file(const std::string& path, int turns, std::string* err) {
    const std::string type = saver(fs::path(path).extension().string());
    if (type.empty()) {
        if (err) *err = "cannot rotate " + fs::path(path).filename().string();
        return false;
    }
    // The saved bytes, kept once, beside nothing the user sees.
    const std::string dir = Glib::get_user_cache_dir() + "/scanit/originals";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string keep = kept_copy(path);
    if (!fs::exists(keep, ec) && !fs::copy_file(path, keep, ec)) {
        if (err) *err = "could not keep the original: " + ec.message();
        return false;
    }
    std::vector<char> data;
    if (!read_all(keep, data)) {
        if (err) *err = "could not read " + keep;
        return false;
    }
    if (norm(turns) != 0) {
        auto pix = decode(data, err);
        if (!pix) return false;
        std::vector<char> out;
        if (!encode(pix->rotate_simple(rotation(turns)), type, out, err)) return false;
        data.swap(out);
    }
    if (!write_all(path, data)) {
        if (err) *err = "could not write " + path;
        return false;
    }
    return true;
}

}  // namespace scanit::rotate
