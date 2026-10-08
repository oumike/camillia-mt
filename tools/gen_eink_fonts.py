#!/usr/bin/env python3
"""Regenerates src/fonts/eink/ -- the T-Deck Pro's 1 bpp text faces.

The T-Deck Pro's e-paper is 1 bit per pixel: LVGL draws it in
LV_COLOR_FORMAT_I1, and lv_draw_sw_blend_to_i1.c folds every anti-aliased glyph
edge to black or white at draw time. With the 4 bpp Montserrat faces every
other board uses, that rounding is lopsided -- any partly covered pixel goes
black on a white background (bloated letters, closed counters at 10-14 px) and
only fully covered ones go white on black (thin, broken strokes on selected
rows).

These faces are Terminus instead: a bitmap font drawn by hand at each pixel
size, so each glyph is already exactly the black and white pixels it should
be. They are read straight from Terminus's BDF sources (not the TTF, whose
outlines only land on the pixel grid at one size) and written out as LVGL
1 bpp fonts under the Montserrat names (lv_font_montserrat_<N>), so every
&lv_font_montserrat_<N> in the source resolves to them unchanged. The tdeck-pro
env builds this directory in place of src/fonts/latin/.

Layout is kept the same way tools/gen_latin_fonts.sh keeps it: each face keeps
the line height of the Montserrat face it stands in for (or Terminus's cell
height where that is taller), with the extra space split above and below the
cell. The LVGL icon symbols (LV_SYMBOL_*) are LVGL's own Font Awesome set, cut
at 1 bpp at the Montserrat size by lv_font_conv into
lv_font_eink_symbols_<N>.c, and chained in as each face's fallback -- so icons
keep their old size too.

Text range is the Latin one gen_latin_fonts.sh uses (issue #99): ASCII,
Latin-1 Supplement, Latin Extended-A, S/T with comma below, bullet and euro.

Needs node (npx fetches lv_font_conv@1.5.3), an LVGL checkout under
.pio/libdeps (for the Font Awesome file), and the Terminus source release,
which carries the BDFs (ter-u<N>n.bdf / ter-u<N>b.bdf):

  https://sourceforge.net/projects/terminus-font/files/terminus-font-4.49/

  tools/gen_eink_fonts.py path/to/terminus-font-4.49.1
"""
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "fonts", "eink")

# Montserrat name -> (Terminus pixel size, weight). Terminus is cut at 12-32 px
# only, so 8 and 10 take its smallest face and 40 its largest. Same-size
# otherwise: Terminus's cell height is the px size, so its letters fill the
# Montserrat line box about as Montserrat's did.
FACES = [
    ("lv_font_montserrat_8", 8, 12, "n"),
    ("lv_font_montserrat_10", 10, 12, "n"),
    ("lv_font_montserrat_12", 12, 12, "n"),
    ("lv_font_montserrat_14", 14, 14, "n"),
    ("lv_font_montserrat_16", 16, 16, "n"),
    ("lv_font_montserrat_18", 18, 18, "n"),
    ("lv_font_montserrat_20", 20, 20, "n"),
    ("lv_font_montserrat_24", 24, 24, "n"),
    ("lv_font_montserrat_28", 28, 28, "n"),
    ("lv_font_montserrat_32", 32, 32, "n"),
    ("lv_font_montserrat_40", 40, 32, "n"),
    ("lv_font_montserrat_bold_12", 12, 12, "b"),
    # Bold twins of the chat sizes (Small..Extra Large), for sender names drawn
    # in the same line as regular text. Their metrics are the regular face's,
    # not Montserrat Bold's, so the two halves share a baseline.
    ("lv_font_eink_bold_12", 12, 12, "b"),
    ("lv_font_eink_bold_14", 14, 14, "b"),
    ("lv_font_eink_bold_16", 16, 16, "b"),
    ("lv_font_eink_bold_18", 18, 18, "b"),
]

TEXT_RANGES = [(0x20, 0x7E), (0xA0, 0x17F), (0x218, 0x21B), (0x2022, 0x2022),
               (0x20AC, 0x20AC)]

# Same list as tools/gen_latin_fonts.sh: LVGL 9.5's built-in LV_SYMBOL_* set.
SYMBOLS = "61441,61448,61451,61452,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"


def lvgl_dir():
    hits = sorted(glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "lvgl")))
    if not hits:
        sys.exit("No LVGL checkout under .pio/libdeps; run a build once first.")
    return hits[0]


def montserrat_metrics(lvgl, name, size):
    """Line height and baseline of the face being replaced."""
    if name == "lv_font_montserrat_bold_12":
        path = os.path.join(ROOT, "src", "fonts", "lv_font_montserrat_bold_12.c")
    else:
        path = os.path.join(lvgl, "src", "font", f"lv_font_montserrat_{size}.c")
    s = open(path, encoding="utf-8").read()
    return (int(re.search(r"\.line_height = (\d+)", s).group(1)),
            int(re.search(r"\.base_line = (\d+)", s).group(1)))


def parse_bdf(path):
    """{codepoint: (dwidth, w, h, xoff, yoff, rows)}, plus (ascent, descent)."""
    glyphs, props = {}, {}
    cur, rows, in_bitmap = None, None, False
    for line in open(path, encoding="latin-1"):
        parts = line.split()
        if not parts:
            continue
        key = parts[0]
        if in_bitmap:
            if key == "ENDCHAR":
                in_bitmap = False
                if cur["enc"] >= 0:
                    glyphs[cur["enc"]] = (cur["dw"], *cur["bbx"], rows)
            else:
                rows.append(int(key, 16))
            continue
        if key in ("FONT_ASCENT", "FONT_DESCENT"):
            props[key] = int(parts[1])
        elif key == "STARTCHAR":
            cur = {"enc": -1}
        elif key == "ENCODING":
            cur["enc"] = int(parts[1])
        elif key == "DWIDTH":
            cur["dw"] = int(parts[1])
        elif key == "BBX":
            cur["bbx"] = tuple(int(p) for p in parts[1:5])
        elif key == "BITMAP":
            rows, in_bitmap = [], True
    return glyphs, props["FONT_ASCENT"], props["FONT_DESCENT"]


def glyph_pixels(w, h, rows):
    """BDF rows are left-aligned and padded to whole bytes."""
    nbytes = (w + 7) // 8
    return [[(r >> (nbytes * 8 - 1 - x)) & 1 for x in range(w)] for r in rows[:h]]


def crop(w, h, xoff, yoff, px):
    """Tight box around the set pixels; LVGL places it by ofs_x/ofs_y."""
    ys = [y for y in range(h) if any(px[y])]
    xs = [x for x in range(w) if any(px[y][x] for y in range(h))]
    if not ys:
        return 0, 0, 0, 0, []
    y0, y1, x0, x1 = ys[0], ys[-1], xs[0], xs[-1]
    out = [row[x0:x1 + 1] for row in px[y0:y1 + 1]]
    # BDF yoff is the box bottom relative to the baseline, like LVGL's ofs_y;
    # cropping rows off the bottom raises it.
    return x1 - x0 + 1, y1 - y0 + 1, xoff + x0, yoff + (h - 1 - y1), out


def pack_bits(px):
    """LVGL plain 1 bpp: MSB first, rows run on without padding."""
    bits = [b for row in px for b in row]
    out = []
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8] + [0] * (8 - len(bits[i:i + 8]))
        out.append(sum(b << (7 - j) for j, b in enumerate(chunk)))
    return out


def codepoints(glyphs):
    return [c for lo, hi in TEXT_RANGES for c in range(lo, hi + 1) if c in glyphs]


def runs(cps):
    """Contiguous runs of codepoints, one FORMAT0_TINY cmap each."""
    out = []
    for c in cps:
        if out and c == out[-1][1] + 1:
            out[-1][1] = c
        else:
            out.append([c, c])
    return out


def emit_face(name, mont_size, ter_size, weight, bdf_dir, lvgl):
    bdf = os.path.join(bdf_dir, f"ter-u{ter_size}{weight}.bdf")
    glyphs, ascent, descent = parse_bdf(bdf)
    cell = ascent + descent
    m_lh, m_bl = montserrat_metrics(lvgl, name, mont_size)
    line_height = max(cell, m_lh)
    base_line = descent + (line_height - cell) // 2

    cps = codepoints(glyphs)
    bitmap, dscs, comments = [], [], []
    for c in cps:
        dw, w, h, xoff, yoff, rows = glyphs[c]
        bw, bh, ox, oy, px = crop(w, h, xoff, yoff, glyph_pixels(w, h, rows))
        dscs.append((len(bitmap), dw * 16, bw, bh, ox, oy))
        data = pack_bits(px)
        ch = chr(c) if c != 0x22 and c != 0x5C else "\\" + chr(c)
        comments.append((c, ch, data))
        bitmap.extend(data)

    weight_name = "Bold" if weight == "b" else "Medium"
    symbols = f"lv_font_eink_symbols_{mont_size}"
    L = []
    L.append(f"/* Generated by tools/gen_eink_fonts.py -- do not edit.\n"
             f" * Terminus {weight_name} {ter_size} px (ter-u{ter_size}{weight}.bdf), 1 bpp, standing in\n"
             f" * as {name} on the T-Deck Pro's e-paper; line metrics\n"
             f" * {line_height}/{base_line} (Montserrat's were {m_lh}/{m_bl}). Icons fall back to\n"
             f" * {symbols}.\n"
             f" * Terminus Font: Copyright (C) 2020 Dimitar Toshkov Zhekov, SIL OFL 1.1\n"
             f" * (see OFL-Terminus.txt). */\n")
    L.append("#include <lvgl.h>\n")
    L.append(f"LV_FONT_DECLARE({symbols})\n")
    L.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    for c, ch, data in comments:
        L.append(f"    /* U+{c:04X} \"{ch}\" */")
        for i in range(0, len(data), 12):
            L.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 12]) + ",")
    L.append("    0x00 /* keeps the array non-empty */\n};\n")
    L.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    L.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,")
    for bi, adv, bw, bh, ox, oy in dscs:
        L.append(f"    {{.bitmap_index = {bi}, .adv_w = {adv}, .box_w = {bw}, .box_h = {bh}, .ofs_x = {ox}, .ofs_y = {oy}}},")
    L.append("};\n")
    L.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    gid = 1
    rs = runs(cps)
    for lo, hi in rs:
        L.append(f"    {{.range_start = {lo}, .range_length = {hi - lo + 1}, .glyph_id_start = {gid},\n"
                 f"     .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY}},")
        gid += hi - lo + 1
    L.append("};\n")
    L.append("static const lv_font_fmt_txt_dsc_t font_dsc = {\n"
             "    .glyph_bitmap = glyph_bitmap,\n"
             "    .glyph_dsc = glyph_dsc,\n"
             "    .cmaps = cmaps,\n"
             "    .kern_dsc = NULL,\n"
             "    .kern_scale = 0,\n"
             f"    .cmap_num = {len(rs)},\n"
             "    .bpp = 1,\n"
             "    .kern_classes = 0,\n"
             "    .bitmap_format = 0,\n"
             "};\n")
    L.append(f"const lv_font_t {name} = {{\n"
             "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n"
             "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n"
             f"    .line_height = {line_height},\n"
             f"    .base_line = {base_line},\n"
             "    .subpx = LV_FONT_SUBPX_NONE,\n"
             "    .underline_position = -1,\n"
             "    .underline_thickness = 1,\n"
             "    .dsc = &font_dsc,\n"
             f"    .fallback = &{symbols},\n"
             "    .user_data = NULL,\n"
             "};")
    path = os.path.join(OUT, f"{name}.c")
    open(path, "w", encoding="utf-8").write("\n".join(L) + "\n")
    print(f"wrote {path} ({len(cps)} glyphs, {len(bitmap)} bitmap bytes, {line_height}/{base_line})")


def emit_symbols(size, lvgl):
    path = os.path.join(OUT, f"lv_font_eink_symbols_{size}.c")
    src = os.path.join(lvgl, "scripts", "built_in_font")
    subprocess.run(["npx", "--yes", "lv_font_conv@1.5.3", "--no-compress", "--no-prefilter",
                    "--bpp", "1", "--size", str(size),
                    "--font", "FontAwesome5-Solid+Brands+Regular.woff", "-r", SYMBOLS,
                    "--format", "lvgl", "-o", path, "--force-fast-kern-format"],
                   cwd=src, check=True)
    s = open(path, encoding="utf-8").read()
    s = re.sub(r'#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "[^"]*"\n#endif',
               "#include <lvgl.h>", s)
    # The generator puts the absolute output path in its Opts banner.
    s = s.replace(path, os.path.relpath(path, ROOT))
    banner = ("/* Generated by tools/gen_eink_fonts.py -- do not edit.\n"
              f" * LVGL's LV_SYMBOL_* icons (Font Awesome 5 Free, SIL OFL 1.1) at {size} px,\n"
              " * 1 bpp: the fallback of the T-Deck Pro's Terminus faces. */\n\n")
    open(path, "w", encoding="utf-8").write(banner + s)
    print(f"wrote {path}")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    bdf_dir = sys.argv[1]
    lvgl = lvgl_dir()
    os.makedirs(OUT, exist_ok=True)
    for size in sorted({m for _, m, _, _ in FACES}):
        emit_symbols(size, lvgl)
    for name, mont_size, ter_size, weight in FACES:
        emit_face(name, mont_size, ter_size, weight, bdf_dir, lvgl)


if __name__ == "__main__":
    main()
