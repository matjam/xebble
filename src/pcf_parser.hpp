/// @file pcf_parser.hpp
/// @brief Internal PCF binary-format parser helpers shared by BitmapFont and Font loaders.
///
/// Not part of the public API. Include only from font.cpp.
#pragma once

#include <xebble/types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace xebble::pcf {

// ---------------------------------------------------------------------------
// Table type flags
// ---------------------------------------------------------------------------

constexpr uint32_t PCF_PROPERTIES        = (1u << 0);
constexpr uint32_t PCF_ACCELERATORS      = (1u << 1);
constexpr uint32_t PCF_METRICS           = (1u << 2);
constexpr uint32_t PCF_BITMAPS           = (1u << 3);
constexpr uint32_t PCF_INK_METRICS       = (1u << 4);
constexpr uint32_t PCF_BDF_ENCODINGS     = (1u << 5);
constexpr uint32_t PCF_SWIDTHS           = (1u << 6);
constexpr uint32_t PCF_GLYPH_NAMES       = (1u << 7);
constexpr uint32_t PCF_BDF_ACCELERATORS  = (1u << 8);

// ---------------------------------------------------------------------------
// Format flags
// ---------------------------------------------------------------------------

constexpr uint32_t PCF_COMPRESSED_METRICS = 0x00000100u;
constexpr uint32_t PCF_BYTE_MASK          = (1u << 2);  ///< MSByte first if set.
constexpr uint32_t PCF_BIT_MASK           = (1u << 3);  ///< MSBit  first if set.
constexpr uint32_t PCF_GLYPH_PAD_MASK     = 3u;

// ---------------------------------------------------------------------------
// TOC entry
// ---------------------------------------------------------------------------

struct TocEntry {
    uint32_t type;
    uint32_t format;
    uint32_t size;
    uint32_t offset;
};

// ---------------------------------------------------------------------------
// Byte-order-aware buffer reader
// ---------------------------------------------------------------------------

struct Reader {
    const uint8_t* data;
    size_t         size;
    size_t         pos = 0;
    bool           msb = false; ///< Current table's byte order.

    bool can_read(size_t n) const noexcept { return pos + n <= size; }
    void seek(size_t p) noexcept { pos = p; }

    uint8_t u8() noexcept {
        if (!can_read(1)) return 0;
        return data[pos++];
    }

    uint16_t u16() noexcept {
        if (!can_read(2)) return 0;
        uint16_t a = data[pos], b = data[pos + 1];
        pos += 2;
        return msb ? static_cast<uint16_t>((a << 8) | b)
                   : static_cast<uint16_t>((b << 8) | a);
    }
    int16_t i16() noexcept { return static_cast<int16_t>(u16()); }

    uint32_t u32() noexcept {
        if (!can_read(4)) return 0;
        uint32_t a = data[pos], b = data[pos+1], c = data[pos+2], d = data[pos+3];
        pos += 4;
        return msb ? (a << 24) | (b << 16) | (c << 8) | d
                   : (d << 24) | (c << 16) | (b << 8) | a;
    }
    int32_t i32() noexcept { return static_cast<int32_t>(u32()); }

    /// Always LSB — used for the file header and per-table format words.
    uint32_t u32_lsb() noexcept {
        if (!can_read(4)) return 0;
        uint32_t a = data[pos], b = data[pos+1], c = data[pos+2], d = data[pos+3];
        pos += 4;
        return a | (b << 8) | (c << 16) | (d << 24);
    }
};

// ---------------------------------------------------------------------------
// Per-glyph metrics (raw from the file)
// ---------------------------------------------------------------------------

struct Metrics {
    int16_t left_bearing  = 0;
    int16_t right_bearing = 0;
    int16_t width         = 0;  ///< Advance width.
    int16_t ascent        = 0;
    int16_t descent       = 0;
};

inline Metrics read_compressed_metrics(Reader& r) noexcept {
    Metrics m;
    m.left_bearing  = static_cast<int16_t>(r.u8()) - 0x80;
    m.right_bearing = static_cast<int16_t>(r.u8()) - 0x80;
    m.width         = static_cast<int16_t>(r.u8()) - 0x80;
    m.ascent        = static_cast<int16_t>(r.u8()) - 0x80;
    m.descent       = static_cast<int16_t>(r.u8()) - 0x80;
    return m;
}

inline Metrics read_uncompressed_metrics(Reader& r) noexcept {
    Metrics m;
    m.left_bearing  = r.i16();
    m.right_bearing = r.i16();
    m.width         = r.i16();
    m.ascent        = r.i16();
    m.descent       = r.i16();
    r.i16(); // attributes — ignored
    return m;
}

// ---------------------------------------------------------------------------
// Bit-reversal helper
// ---------------------------------------------------------------------------

inline uint8_t reverse_bits(uint8_t b) noexcept {
    b = static_cast<uint8_t>(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));
    b = static_cast<uint8_t>(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));
    b = static_cast<uint8_t>(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));
    return b;
}

// ---------------------------------------------------------------------------
// File loader — reads the whole PCF into a buffer and validates the magic.
// ---------------------------------------------------------------------------

inline std::expected<std::vector<uint8_t>, xebble::Error>
read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(xebble::Error{"PCF: cannot open " + path.string()});
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    if (!f) return std::unexpected(xebble::Error{"PCF: read error on " + path.string()});
    if (sz < 4 || buf[0] != 0x01 || buf[1] != 'f' || buf[2] != 'c' || buf[3] != 'p')
        return std::unexpected(xebble::Error{"PCF: not a PCF file: " + path.string()});
    return buf;
}

// ---------------------------------------------------------------------------
// TOC parser
// ---------------------------------------------------------------------------

inline std::vector<TocEntry> read_toc(Reader& r) {
    r.seek(4);
    uint32_t n = r.u32_lsb();
    std::vector<TocEntry> toc(n);
    for (auto& e : toc) {
        e.type   = r.u32_lsb();
        e.format = r.u32_lsb();
        e.size   = r.u32_lsb();
        e.offset = r.u32_lsb();
    }
    return toc;
}

inline const TocEntry* find_table(const std::vector<TocEntry>& toc, uint32_t type) {
    for (const auto& e : toc)
        if (e.type == type) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Metrics table reader
// ---------------------------------------------------------------------------

inline std::expected<std::vector<Metrics>, xebble::Error>
read_metrics_table(Reader& r, const TocEntry& entry) {
    r.seek(entry.offset);
    uint32_t fmt = r.u32_lsb();
    r.msb = (fmt & PCF_BYTE_MASK) != 0;

    std::vector<Metrics> metrics;
    bool compressed = (fmt & 0xFF00u) == PCF_COMPRESSED_METRICS;
    if (compressed) {
        int16_t count = r.i16();
        metrics.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            metrics.push_back(read_compressed_metrics(r));
    } else {
        int32_t count = r.i32();
        metrics.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i)
            metrics.push_back(read_uncompressed_metrics(r));
    }
    if (metrics.empty())
        return std::unexpected(xebble::Error{"PCF: empty metrics"});
    return metrics;
}

// ---------------------------------------------------------------------------
// Bitmap offsets reader
// ---------------------------------------------------------------------------

struct BitmapInfo {
    std::vector<uint32_t> offsets;
    size_t   data_offset = 0; ///< Byte offset in the file buffer where bitmap data starts.
    bool     msbit_first = false;
    uint32_t pad_bytes   = 1; ///< Row-padding unit in bytes.
};

inline std::expected<BitmapInfo, xebble::Error>
read_bitmap_info(Reader& r, const TocEntry& entry, size_t expected_glyph_count) {
    r.seek(entry.offset);
    uint32_t fmt = r.u32_lsb();
    r.msb = (fmt & PCF_BYTE_MASK) != 0;

    BitmapInfo info;
    info.msbit_first = (fmt & PCF_BIT_MASK) != 0;
    info.pad_bytes   = 1u << (fmt & PCF_GLYPH_PAD_MASK);

    int32_t count = r.i32();
    if (count <= 0 || static_cast<size_t>(count) != expected_glyph_count)
        return std::unexpected(xebble::Error{"PCF: metrics/bitmap count mismatch"});

    info.offsets.resize(static_cast<size_t>(count));
    for (auto& o : info.offsets) o = r.u32();

    for (int i = 0; i < 4; ++i) r.u32(); // bitmapSizes[4] — skip

    info.data_offset = r.pos;
    return info;
}

// ---------------------------------------------------------------------------
// Encoding table reader  →  codepoint → glyph-index map
// ---------------------------------------------------------------------------

inline std::expected<std::unordered_map<uint32_t, uint32_t>, xebble::Error>
read_encodings(Reader& r, const TocEntry& entry, size_t num_glyphs) {
    r.seek(entry.offset);
    uint32_t fmt = r.u32_lsb();
    r.msb = (fmt & PCF_BYTE_MASK) != 0;

    int16_t min_byte2 = r.i16();
    int16_t max_byte2 = r.i16();
    int16_t min_byte1 = r.i16();
    int16_t max_byte1 = r.i16();
    r.i16(); // default_char

    int32_t enc_count = (max_byte2 - min_byte2 + 1) * (max_byte1 - min_byte1 + 1);
    std::vector<uint16_t> indices(static_cast<size_t>(enc_count));
    for (auto& idx : indices) idx = static_cast<uint16_t>(r.u16());

    std::unordered_map<uint32_t, uint32_t> map;
    bool single = (min_byte1 == 0 && max_byte1 == 0);

    if (single) {
        for (int16_t enc = min_byte2; enc <= max_byte2; ++enc) {
            size_t i = static_cast<size_t>(enc - min_byte2);
            if (i >= indices.size()) continue;
            uint16_t gi = indices[i];
            if (gi == 0xFFFFu || gi >= num_glyphs) continue;
            map[static_cast<uint32_t>(enc)] = static_cast<uint32_t>(gi);
        }
    } else {
        for (int16_t b1 = min_byte1; b1 <= max_byte1; ++b1) {
            for (int16_t b2 = min_byte2; b2 <= max_byte2; ++b2) {
                size_t i = static_cast<size_t>((b1 - min_byte1) * (max_byte2 - min_byte2 + 1)
                                               + (b2 - min_byte2));
                if (i >= indices.size()) continue;
                uint16_t gi = indices[i];
                if (gi == 0xFFFFu || gi >= num_glyphs) continue;
                uint32_t cp = (static_cast<uint32_t>(b1) << 8) | static_cast<uint32_t>(b2);
                map[cp] = static_cast<uint32_t>(gi);
            }
        }
    }
    return map;
}

} // namespace xebble::pcf
