#!/usr/bin/env python3
"""
gen_embedded_fonts.py
=====================
Generates embedded C++ font source files from TTF fonts using FreeType.

Run once from the repo root:
    python3 scripts/gen_embedded_fonts.py

Outputs:
    src/embedded_petme64.cpp     — BitmapFont, 8x8 fixed-cell, all glyphs
    src/embedded_petme642y.cpp   — BitmapFont, 8x16 fixed-cell, all glyphs
    src/embedded_berkelium64.cpp — Font (proportional), 8px high, all glyphs

Storage format:
  Fixed-cell fonts: 1-bit-per-pixel packed atlas (MSBit first per row),
    expanded to RGBA at runtime. ~30x smaller than raw RGBA.
  Proportional font: 1-byte-per-pixel alpha atlas (RGB implied 255),
    expanded to RGBA at runtime. 4x smaller than raw RGBA.

Each .cpp provides a create() function declared in
include/xebble/embedded_fonts.hpp.
"""

import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    import freetype
except ImportError:
    sys.exit("ERROR: freetype-py not installed. Run: pip install freetype-py")

try:
    from fontTools.ttLib import TTFont
except ImportError:
    sys.exit("ERROR: fontTools not installed. Run: pip install fonttools")

REPO      = Path(__file__).parent.parent
FONTS_DIR = REPO / "fonts"
SRC_DIR   = REPO / "src"

# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def load_cmap(ttf_path: Path) -> dict[int, str]:
    f = TTFont(str(ttf_path))
    cmap = f.getBestCmap()
    return cmap if cmap else {}


def bytes_to_c_array(data: bytes, cols: int = 16) -> str:
    lines = []
    for i in range(0, len(data), cols):
        chunk = data[i:i + cols]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def next_pow2(n: int) -> int:
    p = 1
    while p < n:
        p <<= 1
    return p


HEADER = """\
// GENERATED FILE — do not edit by hand.
// Re-generate with: python3 scripts/gen_embedded_fonts.py
"""

# ---------------------------------------------------------------------------
# Fixed-cell BitmapFont  (1bpp packed atlas)
# ---------------------------------------------------------------------------

@dataclass
class FixedGlyph:
    codepoint:  int
    tile_index: int
    bits:       bytearray   # 1bpp cell, row-major, MSBit first, rows padded to bytes

def render_fixed_glyph(face: freetype.Face, cp: int, cell_w: int, cell_h: int) -> bytearray:
    """Return a 1bpp cell for one codepoint (MSBit first, one byte per 8px column)."""
    row_bytes = (cell_w + 7) // 8
    bits = bytearray(row_bytes * cell_h)  # zero = blank

    try:
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    except Exception:
        return bits

    g  = face.glyph
    bm = g.bitmap
    if bm.width == 0 or bm.rows == 0:
        return bits

    ascender = face.size.ascender >> 6
    left     = g.bitmap_left
    top_px   = ascender - g.bitmap_top   # row offset from cell top

    src_pitch = abs(bm.pitch)
    src_buf   = bytes(bm.buffer)

    for row in range(bm.rows):
        dst_row = top_px + row
        if dst_row < 0 or dst_row >= cell_h:
            continue
        for col in range(bm.width):
            dst_col = left + col
            if dst_col < 0 or dst_col >= cell_w:
                continue
            byte_idx = row * src_pitch + col // 8
            bit = (src_buf[byte_idx] >> (7 - col % 8)) & 1
            if bit:
                dst_byte = dst_row * row_bytes + dst_col // 8
                dst_bit  = 7 - (dst_col % 8)
                bits[dst_byte] |= (1 << dst_bit)

    return bits


def pack_atlas_1bpp(glyphs: list[FixedGlyph], cell_w: int, cell_h: int,
                    cols: int) -> bytes:
    """Pack glyph cells into a 1bpp atlas row-major, MSBit first."""
    rows = math.ceil(len(glyphs) / cols)
    atlas_w = cols * cell_w
    atlas_h = rows * cell_h
    row_bytes = (atlas_w + 7) // 8
    atlas = bytearray(row_bytes * atlas_h)

    cell_row_bytes = (cell_w + 7) // 8

    for g in glyphs:
        ac = g.tile_index % cols
        ar = g.tile_index // cols
        for row in range(cell_h):
            atlas_row  = ar * cell_h + row
            src_start  = row * cell_row_bytes
            # Destination bit offset within this atlas row
            dst_bit_off = ac * cell_w
            dst_byte_off = atlas_row * row_bytes + dst_bit_off // 8
            bit_shift    = dst_bit_off % 8

            for b in range(cell_row_bytes):
                src_byte = g.bits[src_start + b]
                if bit_shift == 0:
                    atlas[dst_byte_off + b] |= src_byte
                else:
                    # Straddles two destination bytes
                    atlas[dst_byte_off + b]     |= src_byte >> bit_shift
                    if dst_byte_off + b + 1 < len(atlas):
                        atlas[dst_byte_off + b + 1] |= src_byte << (8 - bit_shift) & 0xFF

    return bytes(atlas)


def gen_fixed_cell(
    ttf_path:   Path,
    cpp_symbol: str,
    cell_w:     int,
    cell_h:     int,
    pixel_size: int,
    out_path:   Path,
):
    print(f"  Rasterising {ttf_path.name} @ {pixel_size}px (fixed {cell_w}x{cell_h}, 1bpp)...")

    cmap = load_cmap(ttf_path)
    face = freetype.Face(str(ttf_path))
    face.set_pixel_sizes(0, pixel_size)

    glyphs: list[FixedGlyph] = []
    for tile_index, cp in enumerate(sorted(cmap.keys())):
        bits = render_fixed_glyph(face, cp, cell_w, cell_h)
        glyphs.append(FixedGlyph(cp, tile_index, bits))

    num_glyphs = len(glyphs)
    cols       = math.ceil(math.sqrt(num_glyphs))
    rows       = math.ceil(num_glyphs / cols)
    atlas_w    = cols * cell_w
    atlas_h    = rows * cell_h

    atlas_1bpp = pack_atlas_1bpp(glyphs, cell_w, cell_h, cols)
    raw_rgba_bytes  = atlas_w * atlas_h * 4
    print(f"    {num_glyphs} glyphs, atlas {atlas_w}x{atlas_h}, "
          f"1bpp={len(atlas_1bpp):,}B vs RGBA={raw_rgba_bytes:,}B "
          f"({raw_rgba_bytes//len(atlas_1bpp)}x saving)")

    map_entries = "\n".join(
        f"    {{0x{g.codepoint:06X}u, {g.tile_index}u}},"
        for g in glyphs
    )

    cpp = HEADER + f"""
#include <xebble/embedded_fonts.hpp>
#include <xebble/font.hpp>
#include <xebble/spritesheet.hpp>
#include <xebble/texture.hpp>

#include <vector>

namespace xebble::embedded_fonts::{cpp_symbol} {{

constexpr uint32_t CELL_W      = {cell_w}u;
constexpr uint32_t CELL_H      = {cell_h}u;
constexpr uint32_t ATLAS_W     = {atlas_w}u;
constexpr uint32_t ATLAS_H     = {atlas_h}u;
constexpr uint32_t NUM_GLYPHS  = {num_glyphs}u;

// 1-bit-per-pixel atlas, MSBit first, rows padded to bytes.
// Expand to RGBA at runtime via expand_1bpp_to_rgba().
// {atlas_w}x{atlas_h} = {atlas_w*atlas_h} pixels packed into {len(atlas_1bpp)} bytes
// (vs {raw_rgba_bytes:,} bytes as raw RGBA).
static const uint8_t ATLAS_1BPP[] = {{
{bytes_to_c_array(atlas_1bpp)}
}};

// Codepoint -> tile index
struct CpEntry {{ uint32_t codepoint; uint32_t tile; }};
static const CpEntry CODEPOINT_MAP[NUM_GLYPHS] = {{
{map_entries}
}};

static std::vector<uint8_t> expand_1bpp_to_rgba() {{
    constexpr uint32_t row_bytes = (ATLAS_W + 7u) / 8u;
    std::vector<uint8_t> rgba(ATLAS_W * ATLAS_H * 4u, 0u);
    for (uint32_t y = 0; y < ATLAS_H; ++y) {{
        for (uint32_t x = 0; x < ATLAS_W; ++x) {{
            uint32_t byte_idx = y * row_bytes + x / 8u;
            uint8_t  bit      = (ATLAS_1BPP[byte_idx] >> (7u - x % 8u)) & 1u;
            if (bit) {{
                uint32_t dst = (y * ATLAS_W + x) * 4u;
                rgba[dst]     = 255u;
                rgba[dst + 1] = 255u;
                rgba[dst + 2] = 255u;
                rgba[dst + 3] = 255u;
            }}
        }}
    }}
    return rgba;
}}

std::expected<BitmapFont, Error> create(vk::Context& ctx) {{
    auto rgba  = expand_1bpp_to_rgba();
    auto tex   = Texture::create_from_pixels(ctx, rgba.data(), ATLAS_W, ATLAS_H);
    if (!tex) return std::unexpected(tex.error());

    auto sheet = SpriteSheet::from_texture(std::move(*tex), CELL_W, CELL_H);
    if (!sheet) return std::unexpected(sheet.error());

    BitmapFontData data(CELL_W, CELL_H);
    for (const auto& e : CODEPOINT_MAP)
        data.add_glyph(e.codepoint, e.tile);

    return BitmapFont::from_data(std::move(*sheet), std::move(data));
}}

}} // namespace xebble::embedded_fonts::{cpp_symbol}
"""
    out_path.write_text(cpp)
    print(f"    Written {out_path}")


# ---------------------------------------------------------------------------
# Proportional Font  (1-byte-per-pixel alpha atlas)
# ---------------------------------------------------------------------------

@dataclass
class PropGlyph:
    codepoint: int
    advance:   float
    bearing_x: float
    bearing_y: float
    width:     float
    height:    float
    uv_x:      float = 0.0
    uv_y:      float = 0.0
    uv_w:      float = 0.0
    uv_h:      float = 0.0
    alpha:     bytes = field(default_factory=bytes)   # 1 byte/pixel alpha channel


def gen_proportional(
    ttf_path:   Path,
    cpp_symbol: str,
    pixel_size: int,
    out_path:   Path,
):
    print(f"  Rasterising {ttf_path.name} @ {pixel_size}px (proportional, 1bpp mono)...")

    cmap = load_cmap(ttf_path)
    face = freetype.Face(str(ttf_path))
    face.set_pixel_sizes(0, pixel_size)

    # Compute actual ascender/line-height from ASCII printable range only (32-126).
    # Extended glyphs (accents, etc.) can have taller diacritics that would bloat
    # the line height; the font's designed metrics are defined by its ASCII set.
    asc_px = 0
    max_descent = 0
    for cp in range(32, 127):
        if cp not in cmap:
            continue
        try:
            face.load_char(cp, freetype.FT_LOAD_RENDER)
        except Exception:
            continue
        g = face.glyph
        if g.bitmap.rows > 0:
            asc_px = max(asc_px, g.bitmap_top)
            descent = g.bitmap.rows - g.bitmap_top
            max_descent = max(max_descent, descent)
    line_height = asc_px + max(max_descent, 0)

    glyphs:      list[PropGlyph] = []
    total_width: int = 0
    max_h:       int = 0

    for cp in sorted(cmap.keys()):
        try:
            face.load_char(cp, freetype.FT_LOAD_RENDER)   # grayscale, not mono
        except Exception:
            continue
        g  = face.glyph
        bm = g.bitmap
        w, h = bm.width, bm.rows
        # Store as 1bpp: one bit per pixel, MSBit first, rows padded to bytes
        bits = b''

        if w > 0 and h > 0:
            src_pitch  = abs(bm.pitch)
            src_buf    = bytes(bm.buffer)
            row_bytes  = (w + 7) // 8
            packed     = bytearray(row_bytes * h)
            for row in range(h):
                for col in range(w):
                    val = src_buf[row * src_pitch + col]   # grayscale byte
                    if val > 0:                             # any coverage = pixel on
                        dst_byte = row * row_bytes + col // 8
                        packed[dst_byte] |= 1 << (7 - col % 8)
            bits = bytes(packed)
            total_width += w + 1
            max_h = max(max_h, h)

        glyphs.append(PropGlyph(
            codepoint = cp,
            advance   = g.advance.x / 64.0,
            bearing_x = float(g.bitmap_left),
            bearing_y = float(g.bitmap_top),
            width     = float(w),
            height    = float(h),
            alpha     = bits,   # reusing field; actually 1bpp here
        ))

    num_glyphs = len(glyphs)

    # Row-pack atlas (pixel positions track per-glyph bounding boxes)
    atlas_w    = min(next_pow2(max(total_width, 1)), 2048)
    row_height = max_h + 1
    rows_needed = 1
    cx = 0
    for g in glyphs:
        if not g.alpha:
            continue
        w = int(g.width)
        if cx + w + 1 > atlas_w:
            cx = 0
            rows_needed += 1
        cx += w + 1
    atlas_h = next_pow2(rows_needed * row_height)

    # Build 1bpp atlas row-by-row
    atlas_row_bytes = (atlas_w + 7) // 8
    atlas_1bpp = bytearray(atlas_row_bytes * atlas_h)
    cx = cy = 0
    aw, ah = float(atlas_w), float(atlas_h)

    for g in glyphs:
        if not g.alpha:
            continue
        w, h = int(g.width), int(g.height)
        if cx + w + 1 > atlas_w:
            cx = 0
            cy += row_height
        src_row_bytes = (w + 7) // 8
        for row in range(h):
            atlas_row = cy + row
            for col in range(w):
                src_byte = row * src_row_bytes + col // 8
                bit = (g.alpha[src_byte] >> (7 - col % 8)) & 1
                if bit:
                    dst_col  = cx + col
                    dst_byte = atlas_row * atlas_row_bytes + dst_col // 8
                    atlas_1bpp[dst_byte] |= 1 << (7 - dst_col % 8)
        g.uv_x = cx / aw
        g.uv_y = cy / ah
        g.uv_w = w  / aw
        g.uv_h = h  / ah
        cx += w + 1

    atlas_bytes    = bytes(atlas_1bpp)
    raw_rgba_bytes = atlas_w * atlas_h * 4
    print(f"    {num_glyphs} glyphs, atlas {atlas_w}x{atlas_h}, "
          f"1bpp={len(atlas_bytes):,}B vs RGBA={raw_rgba_bytes:,}B "
          f"({raw_rgba_bytes//max(len(atlas_bytes),1)}x saving)")

    metrics_entries = "\n".join(
        f"    {{0x{g.codepoint:06X}u, "
        f"{{{g.advance:.4f}f, {g.bearing_x:.4f}f, {g.bearing_y:.4f}f, "
        f"{g.width:.4f}f, {g.height:.4f}f, "
        f"{{{g.uv_x:.8f}f, {g.uv_y:.8f}f, {g.uv_w:.8f}f, {g.uv_h:.8f}f}}}}}},"
        for g in glyphs
    )

    cpp = HEADER + f"""
#include <xebble/embedded_fonts.hpp>
#include <xebble/font.hpp>
#include <xebble/texture.hpp>

#include <unordered_map>
#include <vector>

namespace xebble::embedded_fonts::{cpp_symbol} {{

constexpr uint32_t ATLAS_W     = {atlas_w}u;
constexpr uint32_t ATLAS_H     = {atlas_h}u;
constexpr uint32_t NUM_GLYPHS  = {num_glyphs}u;
constexpr float    LINE_HEIGHT  = {float(line_height):.4f}f;
constexpr float    ASCENDER     = {float(asc_px):.4f}f;
constexpr uint32_t PIXEL_SIZE   = {pixel_size}u;

// 1-bit-per-pixel atlas, MSBit first, rows padded to bytes.
// {atlas_w}x{atlas_h} = {atlas_w*atlas_h} pixels, {len(atlas_bytes)} bytes
// (vs {raw_rgba_bytes:,} bytes as raw RGBA).
static const uint8_t ATLAS_1BPP[] = {{
{bytes_to_c_array(atlas_bytes)}
}};

struct MetricsEntry {{
    uint32_t     codepoint;
    GlyphMetrics metrics;
}};
static const MetricsEntry METRICS_MAP[NUM_GLYPHS] = {{
{metrics_entries}
}};

static std::vector<uint8_t> expand_1bpp_to_rgba() {{
    constexpr uint32_t row_bytes = (ATLAS_W + 7u) / 8u;
    std::vector<uint8_t> rgba(ATLAS_W * ATLAS_H * 4u, 0u);
    for (uint32_t y = 0; y < ATLAS_H; ++y) {{
        for (uint32_t x = 0; x < ATLAS_W; ++x) {{
            uint32_t byte_idx = y * row_bytes + x / 8u;
            uint8_t  bit      = (ATLAS_1BPP[byte_idx] >> (7u - x % 8u)) & 1u;
            if (bit) {{
                uint32_t dst = (y * ATLAS_W + x) * 4u;
                rgba[dst]     = 255u;
                rgba[dst + 1] = 255u;
                rgba[dst + 2] = 255u;
                rgba[dst + 3] = 255u;
            }}
        }}
    }}
    return rgba;
}}

std::expected<Font, Error> create(vk::Context& ctx) {{
    auto rgba = expand_1bpp_to_rgba();
    auto tex  = Texture::create_from_pixels(ctx, rgba.data(), ATLAS_W, ATLAS_H);
    if (!tex) return std::unexpected(tex.error());

    std::unordered_map<uint32_t, GlyphMetrics> glyphs;
    glyphs.reserve(NUM_GLYPHS);
    for (const auto& e : METRICS_MAP)
        glyphs[e.codepoint] = e.metrics;

    return Font::from_atlas(std::move(*tex), std::move(glyphs), PIXEL_SIZE, LINE_HEIGHT, ASCENDER);
}}

}} // namespace xebble::embedded_fonts::{cpp_symbol}
"""
    out_path.write_text(cpp)
    print(f"    Written {out_path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("Generating embedded fonts...")

    gen_fixed_cell(
        ttf_path   = FONTS_DIR / "PetMe64.ttf",
        cpp_symbol = "petme64",
        cell_w     = 8,
        cell_h     = 8,
        pixel_size = 8,
        out_path   = SRC_DIR / "embedded_petme64.cpp",
    )

    gen_fixed_cell(
        ttf_path   = FONTS_DIR / "PetMe642Y.ttf",
        cpp_symbol = "petme642y",
        cell_w     = 8,
        cell_h     = 16,
        pixel_size = 16,
        out_path   = SRC_DIR / "embedded_petme642y.cpp",
    )

    gen_proportional(
        ttf_path   = FONTS_DIR / "Berkelium64.ttf",
        cpp_symbol = "berkelium64",
        pixel_size = 10,
        out_path   = SRC_DIR / "embedded_berkelium64.cpp",
    )

    print("Done.")


if __name__ == "__main__":
    main()
