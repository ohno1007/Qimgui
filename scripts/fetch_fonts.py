#!/usr/bin/env python3
"""Download, subset and embed the UI font and the icon font.

Regenerates jni/src/core/font_data.{h,cpp} and jni/include/ui/icons.h.
Run only when the font selection or the icon list changes; the generated files
are committed so a normal build needs no network.

    pip install fonttools brotli
    python3 scripts/fetch_fonts.py

Why these two faces
-------------------
Anthropic's brand typefaces are Styrene and Tiempos, licensed from Klim Type
Foundry. They are commercial fonts and cannot be downloaded or redistributed in
a binary, so this uses Inter — the open grotesque that stands in for that class
of UI face — under the SIL Open Font License. Font Awesome 6 Free is likewise
OFL for its font files.

Both are subset hard. The full Inter variable font is 856KB and Font Awesome
Solid is 416KB, against a 857KB binary; carrying either whole would be absurd
for the couple of hundred glyphs actually reachable.
"""

import io
import os
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

INTER_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/inter/"
             "Inter%5Bopsz%2Cwght%5D.ttf")
FA_URL = ("https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/"
          "webfonts/fa-solid-900.ttf")

# Inter carries the whole Latin world; the UI is Chinese with Latin numerals,
# labels and units, so Latin-1 plus the typographic punctuation and the arrows
# and symbols the UI actually draws is the whole requirement. Chinese comes from
# the device's own CJK font, merged on top at runtime.
LATIN = "".join(chr(c) for c in range(0x20, 0x7F))
LATIN += "".join(chr(c) for c in range(0xA0, 0x100))
EXTRA = "‐‑–—―‘’‚“”„†‡•…‰‹›€™←↑→↓↔⇄∞≈≠≤≥±×÷·°′″⌀△▲▽▼◀▶●○◆■□✓✕⚠"

# Icon set, by Font Awesome 6 glyph name. Codepoints are read out of the font's
# own cmap rather than written down here — a hand-copied private-use codepoint
# that drifts one release later is a silently wrong glyph, which is worse than
# a build error.
ICONS = [
    # navigation / structure
    ("HOUSE",         "house"),
    ("GEAR",          "gear"),
    ("SLIDERS",       "sliders"),
    ("GAUGE",         "gauge-high"),
    ("LAYER_GROUP",   "layer-group"),
    ("GRID",          "table-cells-large"),
    ("LIST",          "list"),
    ("BARS",          "bars"),
    ("CIRCLE_INFO",   "circle-info"),
    ("WINDOW",        "window-maximize"),
    # actions
    ("PLAY",          "play"),
    ("PAUSE",         "pause"),
    ("STOP",          "stop"),
    ("ARROW_ROTATE",  "arrow-rotate-right"),
    ("PLUS",          "plus"),
    ("MINUS",         "minus"),
    ("XMARK",         "xmark"),
    ("CHECK",         "check"),
    ("PEN",           "pen"),
    ("TRASH",         "trash"),
    ("COPY",          "copy"),
    ("DOWNLOAD",      "download"),
    ("UPLOAD",        "upload"),
    ("SHARE",         "share-nodes"),
    ("MAGNIFIER",     "magnifying-glass"),
    ("POWER",         "power-off"),
    # arrows / chevrons
    ("CHEVRON_UP",    "chevron-up"),
    ("CHEVRON_DOWN",  "chevron-down"),
    ("CHEVRON_LEFT",  "chevron-left"),
    ("CHEVRON_RIGHT", "chevron-right"),
    ("ARROW_UP",      "arrow-up"),
    ("ARROW_DOWN",    "arrow-down"),
    ("ARROW_LEFT",    "arrow-left"),
    ("ARROW_RIGHT",   "arrow-right"),
    ("EXPAND",        "expand"),
    ("COMPRESS",      "compress"),
    # status / feedback
    ("CIRCLE_CHECK",  "circle-check"),
    ("CIRCLE_XMARK",  "circle-xmark"),
    ("TRIANGLE_WARN", "triangle-exclamation"),
    ("BELL",          "bell"),
    ("STAR",          "star"),
    ("HEART",         "heart"),
    ("BOLT",          "bolt"),
    ("FIRE",          "fire"),
    ("SPINNER",       "spinner"),
    ("HOURGLASS",     "hourglass-half"),
    # device / system
    ("MOBILE",        "mobile-screen"),
    ("DESKTOP",       "desktop"),
    ("MICROCHIP",     "microchip"),
    ("MEMORY",        "memory"),
    ("BATTERY",       "battery-three-quarters"),
    ("WIFI",          "wifi"),
    ("SIGNAL",        "signal"),
    ("TOWER",         "tower-broadcast"),
    ("VOLUME",        "volume-high"),
    ("VOLUME_MUTE",   "volume-xmark"),
    ("CAMERA",        "camera"),
    ("VIDEO",         "video"),
    ("IMAGE",         "image"),
    ("FILE",          "file"),
    ("FOLDER",        "folder"),
    ("DATABASE",      "database"),
    ("CLOCK",         "clock"),
    ("CALENDAR",      "calendar"),
    ("USER",          "user"),
    ("LOCK",          "lock"),
    ("UNLOCK",        "lock-open"),
    ("EYE",           "eye"),
    ("EYE_SLASH",     "eye-slash"),
    ("PALETTE",       "palette"),
    ("DROPLET",       "droplet"),
    ("SUN",           "sun"),
    ("MOON",          "moon"),
    ("WAND",          "wand-magic-sparkles"),
    ("CUBE",          "cube"),
    ("ROBOT",         "robot"),
    ("COMMENT",       "comment"),
    ("PAPER_PLANE",   "paper-plane"),
    ("TERMINAL",      "terminal"),
    ("CODE",          "code"),
    ("LINK",          "link"),
    ("ROTATE",        "rotate"),
    ("THUMBTACK",     "thumbtack"),
]


def fetch(url):
    sys.stderr.write("fetching %s\n" % url)
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def run_subset(data, args):
    """pyftsubset via the module API, in memory."""
    from fontTools import subset
    from fontTools.ttLib import TTFont
    font = TTFont(io.BytesIO(data))
    opts = subset.Options()
    opts.drop_tables += ["GSUB", "GPOS", "GDEF", "morx", "kern"]
    opts.layout_features = []
    opts.name_IDs = ["*"]
    opts.name_legacy = False
    opts.notdef_outline = True
    opts.recalc_bounds = True
    opts.glyph_names = False
    opts.hinting = True
    opts.legacy_kern = False
    subsetter = subset.Subsetter(options=opts)
    subsetter.populate(**args)
    subsetter.subset(font)
    out = io.BytesIO()
    font.save(out)
    return out.getvalue()


def instance_inter(data):
    """Pin the variable axes so no variation tables ride along.

    Inter's UI weight is 450 rather than 400 — at 400 it reads a shade light
    against a refracting background, and this text has to hold up over
    whatever happens to be behind the window.
    """
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer
    font = TTFont(io.BytesIO(data))
    instancer.instantiateVariableFont(font, {"wght": 450, "opsz": 20},
                                      inplace=True, updateFontNames=False)
    out = io.BytesIO()
    font.save(out)
    return out.getvalue()


def icon_codepoints(fa_bytes):
    """Read each icon's codepoint out of the font's own cmap, by glyph name."""
    from fontTools.ttLib import TTFont
    font = TTFont(io.BytesIO(fa_bytes))
    # Font Awesome maps a good many glyphs twice: once in the Private Use Area
    # and once at the plain Unicode character that resembles them — "plus" is at
    # both U+F067 and U+002B, "xmark" at both U+F00D and U+00D7. The plain one
    # is the wrong pick twice over: it is not the icon shape, and the Latin font
    # merged ahead of this one already owns that codepoint, so the icon would
    # never be reached at all. Prefer the PUA.
    rev = {}
    for cp, name in font.getBestCmap().items():
        pua = 0xE000 <= cp <= 0xF8FF
        if name not in rev or (pua and not (0xE000 <= rev[name] <= 0xF8FF)):
            rev[name] = cp
    out, missing = [], []
    for macro, glyph in ICONS:
        cp = rev.get(glyph)
        if cp is None:
            missing.append(glyph)
        else:
            out.append((macro, glyph, cp))
    if missing:
        raise SystemExit("glyph names not in the font: %s" % ", ".join(missing))
    return out


def utf8_escape(cp):
    return "".join("\\x%02x" % b for b in chr(cp).encode("utf-8"))


def c_array(name, data):
    lines = ["inline constexpr unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join("0x%02x," % b for b in data[i:i + 16]))
    lines.append("};")
    return "\n".join(lines)


def main():
    inter = instance_inter(fetch(INTER_URL))
    inter = run_subset(inter, dict(text=LATIN + EXTRA))
    fa_raw = fetch(FA_URL)
    cps = icon_codepoints(fa_raw)
    fa = run_subset(fa_raw, dict(unicodes=[cp for _, _, cp in cps]))

    sys.stderr.write("inter subset: %d bytes\nicons subset: %d bytes\n"
                     % (len(inter), len(fa)))

    lo = min(cp for _, _, cp in cps)
    hi = max(cp for _, _, cp in cps)

    hdr = os.path.join(ROOT, "jni/src/core/font_data.h")
    with open(hdr, "w") as f:
        f.write("""// AUTO-GENERATED by scripts/fetch_fonts.py. Do not edit.
//
// Inter (SIL Open Font License 1.1) subset to Latin-1 plus the typographic
// punctuation and symbols the UI draws, instantiated at wght 450 / opsz 20 so
// no variable-font tables ride along. Chinese is not here: it comes from the
// device's own CJK font, merged on top at runtime.
//
// Font Awesome 6 Free Solid (SIL Open Font License 1.1) subset to the icons
// listed in the script.
#pragma once

namespace aimgui::font_data {

%s

%s

// Inclusive range spanning every embedded icon, for ImGui's glyph ranges.
constexpr unsigned short kIconFirst = 0x%04X;
constexpr unsigned short kIconLast  = 0x%04X;

} // namespace aimgui::font_data
""" % (c_array("kUiFont", inter), c_array("kIconFont", fa), lo, hi))

    ico = os.path.join(ROOT, "jni/include/ui/icons.h")
    with open(ico, "w") as f:
        f.write("""// AUTO-GENERATED by scripts/fetch_fonts.py. Do not edit.
//
// Font Awesome 6 Free Solid, merged into the UI font at startup, so an icon is
// just a string and goes anywhere text goes:
//
//     ImGui::Button(ICON_FA_GEAR "  设置");
//     state->island_icon = ICON_FA_BOLT;
//
// Codepoints are read out of the font's own cmap by glyph name when this file
// is generated, so they cannot drift from what is actually embedded.
#pragma once

// clang-format off
""")
        width = max(len(m) for m, _, _ in cps)
        for macro, glyph, cp in cps:
            f.write("#define ICON_FA_%-*s \"%s\"   // %s U+%04X\n"
                    % (width, macro, utf8_escape(cp), glyph, cp))
        f.write("// clang-format on\n")

    sys.stderr.write("wrote %s\nwrote %s\n" % (hdr, ico))


if __name__ == "__main__":
    main()
