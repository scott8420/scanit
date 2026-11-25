#pragma once

namespace scanit {

// Scanit's own stylesheet, beside the look's (resources/look.css). Installed
// once in App::ensure_shell at APPLICATION priority. Only classes the look
// does not carry live here, under window.look like the look's own rules.
inline const char* SCANIT_CSS = R"CSS(
    /* The scanned page on the results state: a white sheet on the gradient. */
    window.look picture.preview {
        background-color: rgba(0,0,0,0.25);
        border-radius: 12px;
        padding: 8px;
    }
)CSS";

}  // namespace scanit
