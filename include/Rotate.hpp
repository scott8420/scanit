#pragma once
// Rotate -- turning scanned pages, in quarter turns clockwise (1 = 90 right,
// 2 = 180, 3 = 90 left). App layer: gdk-pixbuf does the pixels, so this is
// safe on the worker thread (no GTK widgets).
//
// eSCL has no rotate, so a page is turned after it arrives: rotate_bytes()
// before the file is written (the quick setting, every page, continuous
// mode too), turn_file() after (the results page's buttons, undoable).
//
// No generation loss: the first time a saved file is turned, its bytes as
// saved are kept in ~/.cache/scanit/originals/; every turn renders from that
// copy, so turning a JPEG round and round (or undo / redo) never re-encodes
// a re-encode.

#include <string>
#include <vector>

namespace scanit::rotate {

// Turn an encoded image in place (PNG, JPEG, TIFF). False + *err for a format
// it cannot turn (a PDF) -- the caller keeps the page unturned.
bool rotate_bytes(std::vector<char>& data, const std::string& mime, int turns, std::string* err);

// Make the file at `path` its saved self turned by `turns` (0 = as saved).
bool turn_file(const std::string& path, int turns, std::string* err);

// A new scan was just written at `path`: any kept copy from an older file of
// the same name is stale (a book rescanned under the same base).
void forget(const std::string& path);

}  // namespace scanit::rotate
