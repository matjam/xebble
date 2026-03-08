/// @file audio_xmp.hpp
/// @brief Internal libxmp → miniaudio custom data source adapter.
///
/// Implements the `ma_data_source` vtable so that libxmp's PCM output can be
/// fed directly into a `ma_sound` without an intermediate ring buffer.
///
/// This header is private to `audio.cpp` and must not be included elsewhere.
#pragma once

// miniaudio must be included with the implementation guard already defined
// (done once in audio.cpp before this header is pulled in).
#include <cstdint>
#include <cstring>
#include <miniaudio.h>
#include <vector>
#include <xmp.h>

namespace xebble::internal {

// ---------------------------------------------------------------------------
// XmpDataSource
// ---------------------------------------------------------------------------

/// Wraps an xmp_context as a miniaudio data source.
///
/// libxmp renders tracker module data to interleaved stereo s16le frames via
/// `xmp_play_buffer()`. The node graph requires f32 output, so we convert
/// s16 → f32 inside the read callback using a small stack-allocated temp buffer.
struct XmpDataSource {
    ma_data_source_base base; ///< Must be the first member.

    xmp_context ctx = nullptr;
    std::vector<int16_t> tmp_buf; ///< Reusable render buffer — avoids per-callback allocation.
    bool looping = true;
    bool at_end = false;

    // miniaudio's node graph requires ma_format_f32.
    static constexpr ma_format kFormat = ma_format_f32;
    static constexpr ma_uint32 kChannels = 2;
    ma_uint32 sample_rate = 48000; // set at init from the engine's actual device rate
};

// ---------------------------------------------------------------------------
// vtable callbacks
// ---------------------------------------------------------------------------

static ma_result xmp_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount,
                             ma_uint64* pFramesRead) {
    auto* ds = static_cast<XmpDataSource*>(pDataSource);
    if (ds->at_end) {
        if (pFramesRead != nullptr) {
            *pFramesRead = 0;
        }
        return MA_AT_END;
    }

    // Render into a reusable s16 buffer, then convert to f32.
    // libxmp works with byte counts: each frame = 2 ch × 2 bytes (s16le) = 4 bytes.
    const auto sample_count = static_cast<std::size_t>(frameCount * XmpDataSource::kChannels);
    if (ds->tmp_buf.size() < sample_count) {
        ds->tmp_buf.resize(sample_count);
    }
    const auto byte_count = static_cast<int>(sample_count * sizeof(int16_t));

    const int ret = xmp_play_buffer(ds->ctx, ds->tmp_buf.data(), byte_count, ds->looping ? 0 : 1);

    if (ret == -XMP_END) {
        if (!ds->looping) {
            ds->at_end = true;
        }
        // looping: libxmp already wrapped around; report a full buffer either way.
    }

    const ma_uint64 frames_rendered = (ds->at_end && ret == -XMP_END) ? 0 : frameCount;

    // Convert s16 → f32 into the output buffer miniaudio provided.
    ma_pcm_s16_to_f32(pFramesOut, ds->tmp_buf.data(), static_cast<ma_uint64>(sample_count),
                      ma_dither_mode_none);

    if (pFramesRead != nullptr) {
        *pFramesRead = frames_rendered;
    }

    return (ds->at_end && frames_rendered == 0) ? MA_AT_END : MA_SUCCESS;
}

static ma_result xmp_ds_seek(ma_data_source* pDataSource, ma_uint64 /*frameIndex*/) {
    // libxmp doesn't expose per-frame seeking. Reset to the start.
    auto* ds = static_cast<XmpDataSource*>(pDataSource);
    xmp_restart_module(ds->ctx);
    ds->at_end = false;
    return MA_SUCCESS;
}

static ma_result xmp_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat,
                                        ma_uint32* pChannels, ma_uint32* pSampleRate,
                                        ma_channel* /*pChannelMap*/, size_t /*channelMapCap*/) {
    const auto* ds = static_cast<const XmpDataSource*>(pDataSource);
    if (pFormat != nullptr) {
        *pFormat = XmpDataSource::kFormat;
    }
    if (pChannels != nullptr) {
        *pChannels = XmpDataSource::kChannels;
    }
    if (pSampleRate != nullptr) {
        *pSampleRate = ds->sample_rate;
    }
    return MA_SUCCESS;
}

static ma_result xmp_ds_get_cursor(ma_data_source* /*pDataSource*/, ma_uint64* pCursor) {
    if (pCursor != nullptr) {
        *pCursor = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static ma_result xmp_ds_get_length(ma_data_source* /*pDataSource*/, ma_uint64* pLength) {
    if (pLength != nullptr) {
        *pLength = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static const ma_data_source_vtable kXmpVtable = {
    xmp_ds_read,            // onRead
    xmp_ds_seek,            // onSeek
    xmp_ds_get_data_format, // onGetDataFormat
    xmp_ds_get_cursor,      // onGetCursor
    xmp_ds_get_length,      // onGetLength
    nullptr,                // onSetLooping (handled by ds->looping)
    0,                      // flags
};

// ---------------------------------------------------------------------------
// Lifecycle helpers
// ---------------------------------------------------------------------------

[[nodiscard]] inline ma_result xmp_ds_init_file(XmpDataSource& ds, const char* path, bool loop,
                                                ma_uint32 sample_rate) {
    ds.ctx = xmp_create_context();
    if (ds.ctx == nullptr) {
        return MA_OUT_OF_MEMORY;
    }

    if (xmp_load_module(ds.ctx, path) != 0) {
        xmp_free_context(ds.ctx);
        ds.ctx = nullptr;
        return MA_INVALID_FILE;
    }

    if (xmp_start_player(ds.ctx, static_cast<int>(sample_rate), 0) != 0) {
        xmp_release_module(ds.ctx);
        xmp_free_context(ds.ctx);
        ds.ctx = nullptr;
        return MA_ERROR;
    }

    ds.looping = loop;
    ds.at_end = false;
    ds.sample_rate = sample_rate;

    ma_data_source_config cfg = ma_data_source_config_init();
    cfg.vtable = &kXmpVtable;
    return ma_data_source_init(&cfg, &ds.base);
}

[[nodiscard]] inline ma_result xmp_ds_init_memory(XmpDataSource& ds, const void* data,
                                                  std::size_t size, bool loop,
                                                  ma_uint32 sample_rate) {
    ds.ctx = xmp_create_context();
    if (ds.ctx == nullptr) {
        return MA_OUT_OF_MEMORY;
    }

    // xmp_load_module_from_memory takes a non-const void* but does not mutate the data.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    if (xmp_load_module_from_memory(ds.ctx, const_cast<void*>(data), static_cast<long>(size)) !=
        0) {
        xmp_free_context(ds.ctx);
        ds.ctx = nullptr;
        return MA_INVALID_FILE;
    }

    if (xmp_start_player(ds.ctx, static_cast<int>(sample_rate), 0) != 0) {
        xmp_release_module(ds.ctx);
        xmp_free_context(ds.ctx);
        ds.ctx = nullptr;
        return MA_ERROR;
    }

    ds.looping = loop;
    ds.at_end = false;
    ds.sample_rate = sample_rate;

    ma_data_source_config cfg = ma_data_source_config_init();
    cfg.vtable = &kXmpVtable;
    return ma_data_source_init(&cfg, &ds.base);
}

/// @brief Release all resources held by the data source.
inline void xmp_ds_uninit(XmpDataSource& ds) {
    if (ds.ctx != nullptr) {
        xmp_end_player(ds.ctx);
        xmp_release_module(ds.ctx);
        xmp_free_context(ds.ctx);
        ds.ctx = nullptr;
    }
    ma_data_source_uninit(&ds.base);
}

} // namespace xebble::internal
