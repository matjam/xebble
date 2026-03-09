/// @file audio_sid.hpp
/// @brief Internal libsidplayfp → miniaudio custom data source adapter.
///
/// This header is private to `audio.cpp` and must not be included elsewhere.
#pragma once

// miniaudio must already have its implementation compiled before this header
// is pulled in (done once in audio.cpp).
#include <cstdint>
#include <cstring>
#include <memory>
#include <miniaudio.h>
#include <sidplayfp/builders/residfp.h>
#include <sidplayfp/SidConfig.h>
#include <sidplayfp/SidInfo.h>
#include <sidplayfp/sidplayfp.h>
#include <sidplayfp/SidTune.h>
#include <vector>

namespace xebble::internal {

// ---------------------------------------------------------------------------
// SidDataSource
// ---------------------------------------------------------------------------

/// Wraps a libsidplayfp player as a miniaudio data source.
///
/// libsidplayfp emulates the SID chip and renders stereo s16le samples via
/// `sidplayfp::play()`. We expose this stream as an `ma_data_source` so
/// miniaudio can mix, volume-control, and route it through its node graph.
struct SidDataSource {
    ma_data_source_base base; ///< Must be the first member.

    // Heap-allocated so the sidplayfp constructor runs properly and the object
    // isn't clobbered by zero-initialisation of the enclosing struct.
    std::unique_ptr<sidplayfp> player;
    std::unique_ptr<ReSIDfpBuilder> builder;
    std::unique_ptr<SidTune> tune;
    std::vector<uint8_t> tune_data; ///< Owns the raw SID file bytes.
    std::vector<short> tmp_buf;     ///< Reusable render buffer — avoids per-callback allocation.
    bool looping = true;
    bool at_end = false;

    // miniaudio's node graph requires ma_format_f32.
    // libsidplayfp renders s16le internally; we convert in the read callback.
    static constexpr ma_format kFormat = ma_format_f32;
    static constexpr ma_uint32 kChannels = 2;
    ma_uint32 sample_rate = 48000; // set at init from the engine's actual device rate
};

// ---------------------------------------------------------------------------
// vtable callbacks
// ---------------------------------------------------------------------------

static ma_result sid_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount,
                             ma_uint64* pFramesRead) {
    auto* ds = static_cast<SidDataSource*>(pDataSource);
    if (ds->at_end) {
        if (pFramesRead != nullptr) {
            *pFramesRead = 0;
        }
        return MA_AT_END;
    }

    // Render into a reusable s16 buffer, then convert to f32.
    // libsidplayfp play() takes a sample (not frame) count.
    // One frame = 2 samples (stereo s16le).
    const auto sample_count = static_cast<uint_least32_t>(frameCount * SidDataSource::kChannels);
    if (ds->tmp_buf.size() < sample_count) {
        ds->tmp_buf.resize(sample_count);
    }

    // play(short*, count) is deprecated in libsidplayfp >= 2.14 but remains
    // functional. Suppress the warning; migration to the new cycle-based API
    // would require a more complex render loop.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    const uint_least32_t rendered = ds->player->play(ds->tmp_buf.data(), sample_count);
#pragma GCC diagnostic pop
    const ma_uint64 frames_rendered = rendered / SidDataSource::kChannels;

    // Convert s16 → f32 into the output buffer miniaudio provided.
    ma_pcm_s16_to_f32(pFramesOut, ds->tmp_buf.data(), rendered, ma_dither_mode_none);

    if (pFramesRead != nullptr) {
        *pFramesRead = frames_rendered;
    }

    // SID tunes nominally loop forever; we only stop when looping=false and
    // the tune signals completion (play() returns < requested).
    if (!ds->looping && rendered < sample_count) {
        ds->at_end = true;
    }

    return MA_SUCCESS;
}

static ma_result sid_ds_seek(ma_data_source* pDataSource, ma_uint64 /*frameIndex*/) {
    // libsidplayfp doesn't support mid-stream seek. Restart the tune.
    auto* ds = static_cast<SidDataSource*>(pDataSource);
    // Explicitly call sidplayfp::reset() (not unique_ptr::reset()) to restart
    // the emulator without destroying the player object.
    (*ds->player).reset();
    ds->player->load(ds->tune.get());
    ds->at_end = false;
    return MA_SUCCESS;
}

static ma_result sid_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat,
                                        ma_uint32* pChannels, ma_uint32* pSampleRate,
                                        ma_channel* /*pChannelMap*/, size_t /*channelMapCap*/) {
    const auto* ds = static_cast<const SidDataSource*>(pDataSource);
    if (pFormat != nullptr) {
        *pFormat = SidDataSource::kFormat;
    }
    if (pChannels != nullptr) {
        *pChannels = SidDataSource::kChannels;
    }
    if (pSampleRate != nullptr) {
        *pSampleRate = ds->sample_rate;
    }
    return MA_SUCCESS;
}

static ma_result sid_ds_get_cursor(ma_data_source* /*pDataSource*/, ma_uint64* pCursor) {
    if (pCursor != nullptr) {
        *pCursor = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static ma_result sid_ds_get_length(ma_data_source* /*pDataSource*/, ma_uint64* pLength) {
    if (pLength != nullptr) {
        *pLength = 0;
    }
    return MA_NOT_IMPLEMENTED;
}

static const ma_data_source_vtable kSidVtable = {
    sid_ds_read,            // onRead
    sid_ds_seek,            // onSeek
    sid_ds_get_data_format, // onGetDataFormat
    sid_ds_get_cursor,      // onGetCursor
    sid_ds_get_length,      // onGetLength
    nullptr,                // onSetLooping
    0,                      // flags
};

// ---------------------------------------------------------------------------
// Lifecycle helpers
// ---------------------------------------------------------------------------

/// @brief Common setup after tune and builder are ready.
[[nodiscard]] static ma_result sid_ds_configure(SidDataSource& ds, bool loop) {
    // Construct the player now that tune + builder are ready.
    ds.player = std::make_unique<sidplayfp>();

    // Detect the SID model the tune requests so the emulator uses the right
    // filter characteristics.  If the tune doesn't specify, default to 6581.
    SidConfig::sid_model_t model = SidConfig::MOS6581;
    if (ds.tune) {
        const SidTuneInfo* info = ds.tune->getInfo();
        if (info != nullptr && info->sidModel(0) == SidTuneInfo::SIDMODEL_8580) {
            model = SidConfig::MOS8580;
        }
    }

    // Configure the SID emulator.
    SidConfig cfg;
    cfg.frequency = ds.sample_rate; // use actual device rate
    cfg.samplingMethod = SidConfig::INTERPOLATE;
    cfg.fastSampling = false;
    cfg.playback = SidConfig::STEREO;
    cfg.sidEmulation = ds.builder.get();
    cfg.defaultSidModel = model;
    cfg.forceSidModel = true; // honour the tune's model, don't auto-detect

    if (!ds.player->config(cfg)) {
        return MA_ERROR;
    }

    if (!ds.player->load(ds.tune.get())) {
        return MA_ERROR;
    }

    ds.looping = loop;
    ds.at_end = false;

    ma_data_source_config base_cfg = ma_data_source_config_init();
    base_cfg.vtable = &kSidVtable;
    return ma_data_source_init(&base_cfg, &ds.base);
}

/// @brief Create the ReSIDfp builder (SID chip emulator instance).
[[nodiscard]] static bool sid_ds_make_builder(SidDataSource& ds) {
    ds.builder = std::make_unique<ReSIDfpBuilder>("xebble-residfp");
    // Create enough emulated SID chips to handle multi-SID tunes (up to 3).
    // We can't call info().maxsids() before config() so use the maximum supported.
    ds.builder->create(3);
    if (!ds.builder->getStatus()) {
        ds.builder.reset();
        return false;
    }
    ds.builder->filter6581Curve(0.5f);
    ds.builder->filter8580Curve(0.5f);
    return true;
}

/// @brief Load a SID tune from file.
[[nodiscard]] inline ma_result sid_ds_init_file(SidDataSource& ds, const char* path, bool loop,
                                                ma_uint32 sample_rate) {
    ds.sample_rate = sample_rate;
    ds.tune = std::make_unique<SidTune>(path);
    if (!ds.tune->getStatus()) {
        return MA_INVALID_FILE;
    }

    if (!sid_ds_make_builder(ds)) {
        return MA_ERROR;
    }

    return sid_ds_configure(ds, loop);
}

/// @brief Load a SID tune from memory.
[[nodiscard]] inline ma_result sid_ds_init_memory(SidDataSource& ds, const void* data,
                                                  std::size_t size, bool loop,
                                                  ma_uint32 sample_rate) {
    ds.sample_rate = sample_rate;
    // SidTune takes a raw pointer; we keep a copy in tune_data so the pointer
    // stays valid for the lifetime of the data source.
    ds.tune_data.assign(static_cast<const uint8_t*>(data),
                        static_cast<const uint8_t*>(data) + size);

    ds.tune = std::make_unique<SidTune>(ds.tune_data.data(), static_cast<uint_least32_t>(size));
    if (!ds.tune->getStatus()) {
        return MA_INVALID_FILE;
    }

    if (!sid_ds_make_builder(ds)) {
        return MA_ERROR;
    }

    return sid_ds_configure(ds, loop);
}

/// @brief Release all resources held by the data source.
inline void sid_ds_uninit(SidDataSource& ds) {
    // Explicitly call unique_ptr::reset() (not sidplayfp::reset()) to destroy
    // the player. Ordering matters: player before tune before builder.
    ds.player = nullptr;
    ds.tune = nullptr;
    ds.builder = nullptr;
    ds.tune_data.clear();
    ma_data_source_uninit(&ds.base);
}

} // namespace xebble::internal
