/// @file audio.cpp
/// @brief AudioEngine implementation: miniaudio + libxmp + libsidplayfp.
///
/// Translation unit layout:
///   1. miniaudio single-file implementation (MA_IMPLEMENTATION guard).
///   2. Internal XmpDataSource adapter (audio_xmp.hpp).
///   3. Internal SidDataSource adapter (audio_sid.hpp, optional).
///   4. AudioEngine::Impl — owns the ma_engine, active music state, and
///      cached sound buffer map.
///   5. AudioEngine public method bodies.

// ---------------------------------------------------------------------------
// 1. miniaudio implementation — compiled exactly once.
// ---------------------------------------------------------------------------
#define MA_IMPLEMENTATION
// On macOS the notarisation process requires no runtime linking.
#ifdef __APPLE__
#define MA_NO_RUNTIME_LINKING
#endif
#include <miniaudio.h>

// ---------------------------------------------------------------------------
// 2 & 3. Internal data source adapters.
// ---------------------------------------------------------------------------
#include "audio_sid.hpp"
#include "audio_xmp.hpp"

#include <xebble/audio.hpp>
#include <xebble/log.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Return the lowercase file extension (including the dot) from a path.
std::string ext_lower(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// Return true when the extension maps to a libxmp-handled module format.
bool is_xmp_ext(std::string_view ext) {
    // The formats libxmp supports (subset of the most common ones).
    static constexpr std::array kXmpExts{
        std::string_view{".mod"}, std::string_view{".xm"},  std::string_view{".it"},
        std::string_view{".s3m"}, std::string_view{".stm"}, std::string_view{".med"},
        std::string_view{".mtm"}, std::string_view{".669"}, std::string_view{".far"},
        std::string_view{".ult"}, std::string_view{".ptm"}, std::string_view{".okt"},
        std::string_view{".amf"}, std::string_view{".dbm"}, std::string_view{".dmf"},
        std::string_view{".dsm"}, std::string_view{".gdm"}, std::string_view{".imf"},
        std::string_view{".psm"}, std::string_view{".umx"},
    };
    return std::ranges::any_of(kXmpExts, [&](std::string_view e) { return e == ext; });
}

/// Return true when the extension maps to a SID file.
bool is_sid_ext(std::string_view ext) {
    return ext == ".sid" || ext == ".psid" || ext == ".rsid";
}

} // namespace

// ---------------------------------------------------------------------------
// MusicKind — active music backend tag
// ---------------------------------------------------------------------------

enum class MusicKind { None, Pcm, Xmp, Sid };

// ---------------------------------------------------------------------------
// AudioEngine::Impl
// ---------------------------------------------------------------------------

struct AudioEngine::Impl {
    // -----------------------------------------------------------------------
    // Core miniaudio engine
    // -----------------------------------------------------------------------
    ma_engine engine{};
    bool engine_ok = false;

    // -----------------------------------------------------------------------
    // Volume state
    // -----------------------------------------------------------------------
    float master_volume = 1.0f;
    float sfx_volume = 1.0f;
    float music_volume = 1.0f;

    // -----------------------------------------------------------------------
    // Sound-effect cache
    // Each entry holds either:
    //   - a file path to let the resource manager handle caching, or
    //   - decoded PCM data in an ma_audio_buffer for in-memory sounds.
    // -----------------------------------------------------------------------
    struct SfxEntry {
        // For in-memory sounds: decoded PCM stored in an ma_audio_buffer.
        // Heap-allocated via unique_ptr so its address stays stable when the
        // unordered_map rehashes — miniaudio's audio thread holds a raw pointer
        // to this buffer and would crash if it moved.
        std::unique_ptr<ma_audio_buffer> pcm_buf;
        bool pcm_buf_init = false;

        bool loaded = false;
    };
    std::unordered_map<std::string, SfxEntry> sfx_cache;
    std::mutex sfx_mutex;

    // Fire-and-forget SFX sounds: started, kept alive until finished, then
    // cleaned up in update(). Each entry is heap-allocated so its address
    // stays stable while miniaudio's audio thread reads from it.
    std::vector<std::unique_ptr<ma_sound>> active_sfx;
    std::mutex active_sfx_mutex;

    // -----------------------------------------------------------------------
    // Active music state — only one track at a time.
    // -----------------------------------------------------------------------
    MusicKind music_kind = MusicKind::None;
    bool music_paused_flag = false;

    // PCM music via miniaudio's built-in decoder + streaming sound.
    ma_sound pcm_sound{};
    bool pcm_sound_init = false;

    // MOD/XM/IT/S3M music via libxmp.
    internal::XmpDataSource xmp_ds{};
    ma_sound xmp_sound{};
    bool xmp_sound_init = false;
    bool xmp_ds_init = false;

#ifdef XEBBLE_HAS_SIDPLAYFP
    // SID chiptune music via libsidplayfp.
    internal::SidDataSource sid_ds{};
    ma_sound sid_sound{};
    bool sid_sound_init = false;
    bool sid_ds_init = false;
#endif

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void stop_current_music() {
        switch (music_kind) {
        case MusicKind::Pcm:
            if (pcm_sound_init) {
                ma_sound_uninit(&pcm_sound);
                pcm_sound_init = false;
            }
            break;
        case MusicKind::Xmp:
            if (xmp_sound_init) {
                ma_sound_uninit(&xmp_sound);
                xmp_sound_init = false;
            }
            if (xmp_ds_init) {
                internal::xmp_ds_uninit(xmp_ds);
                xmp_ds_init = false;
            }
            break;
#ifdef XEBBLE_HAS_SIDPLAYFP
        case MusicKind::Sid:
            if (sid_sound_init) {
                ma_sound_uninit(&sid_sound);
                sid_sound_init = false;
            }
            if (sid_ds_init) {
                internal::sid_ds_uninit(sid_ds);
                sid_ds_init = false;
            }
            break;
#endif
        case MusicKind::None:
            break;
        }
        music_kind = MusicKind::None;
        music_paused_flag = false;
    }

    void apply_music_volume() {
        if (!engine_ok) {
            return;
        }
        const float vol = master_volume * music_volume;
        switch (music_kind) {
        case MusicKind::Pcm:
            if (pcm_sound_init) {
                ma_sound_set_volume(&pcm_sound, vol);
            }
            break;
        case MusicKind::Xmp:
            if (xmp_sound_init) {
                ma_sound_set_volume(&xmp_sound, vol);
            }
            break;
#ifdef XEBBLE_HAS_SIDPLAYFP
        case MusicKind::Sid:
            if (sid_sound_init) {
                ma_sound_set_volume(&sid_sound, vol);
            }
            break;
#endif
        case MusicKind::None:
            break;
        }
    }
};

// ---------------------------------------------------------------------------
// AudioEngine factory
// ---------------------------------------------------------------------------

std::expected<AudioEngine, Error> AudioEngine::create() {
    AudioEngine ae;
    ae.impl_ = std::make_unique<Impl>();

    ma_engine_config cfg = ma_engine_config_init();
    // Use a modest channel count and sample rate suitable for games.
    cfg.channels = 2;
    cfg.sampleRate = 44100;

    const ma_result result = ma_engine_init(&cfg, &ae.impl_->engine);
    if (result != MA_SUCCESS) {
        log(LogLevel::Warn, "AudioEngine: failed to open audio device (" +
                                std::string(ma_result_description(result)) +
                                "). Audio will be silent.");
        // Fall through with engine_ok = false — the engine is still usable
        // as a no-op so the game doesn't crash.
        ae.impl_->engine_ok = false;
    } else {
        ae.impl_->engine_ok = true;
        log(LogLevel::Info, "AudioEngine: audio device opened (44100 Hz, stereo).");
    }

    return ae;
}

// ---------------------------------------------------------------------------
// Move / destructor
// ---------------------------------------------------------------------------

AudioEngine::~AudioEngine() {
    if (impl_) {
        impl_->stop_current_music();
        // Stop and release any in-flight fire-and-forget SFX sounds.
        {
            const std::scoped_lock lk(impl_->active_sfx_mutex);
            for (auto& s : impl_->active_sfx) {
                ma_sound_stop(s.get());
                ma_sound_uninit(s.get());
            }
            impl_->active_sfx.clear();
        }
        // Release any ma_audio_buffer instances for in-memory sounds.
        for (auto& [key, entry] : impl_->sfx_cache) {
            if (entry.pcm_buf_init && entry.pcm_buf) {
                ma_audio_buffer_uninit(entry.pcm_buf.get());
            }
        }
        if (impl_->engine_ok) {
            ma_engine_uninit(&impl_->engine);
        }
    }
}

AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

// ---------------------------------------------------------------------------
// Sound effects
// ---------------------------------------------------------------------------

std::expected<void, Error> AudioEngine::load_sound(const std::filesystem::path& path) {
    if (!impl_->engine_ok) {
        return {}; // Silent mode — pretend success.
    }

    const std::string key = path.string();
    {
        const std::scoped_lock lk(impl_->sfx_mutex);
        if (impl_->sfx_cache.contains(key)) {
            return {}; // Already loaded.
        }

        // Pre-load: store the encoded bytes for later use.
        // miniaudio will decode from memory on first play_sound() call.
        // For simplicity we just mark the path as known.
        impl_->sfx_cache[key] = Impl::SfxEntry{};
    }

    // Warm the resource manager by loading the file into miniaudio's cache.
    // MA_SOUND_FLAG_DECODE decodes fully on load; MA_SOUND_FLAG_NO_SPATIALIZATION
    // keeps it as a 2D sound (no positional audio overhead).
    ma_sound warm;
    const ma_result r = ma_sound_init_from_file(
        &impl_->engine, key.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
        nullptr, nullptr, &warm);

    if (r != MA_SUCCESS) {
        const std::scoped_lock lk(impl_->sfx_mutex);
        impl_->sfx_cache.erase(key);
        return std::unexpected(
            Error{"AudioEngine: failed to load sound '" + key + "': " + ma_result_description(r)});
    }

    // Immediately release — miniaudio's resource manager keeps the decoded
    // data cached; future sounds referencing the same file will reuse it.
    ma_sound_uninit(&warm);

    const std::scoped_lock lk(impl_->sfx_mutex);
    impl_->sfx_cache[key].loaded = true;
    return {};
}

std::expected<void, Error> AudioEngine::load_sound_from_memory(std::string_view name,
                                                               const void* data, std::size_t size) {
    if (!impl_->engine_ok) {
        return {};
    }

    const std::string key(name);
    {
        const std::scoped_lock lk(impl_->sfx_mutex);
        if (impl_->sfx_cache.contains(key)) {
            return {};
        }
    }

    // Decode the encoded bytes into a raw PCM ma_audio_buffer so that
    // play_sound() can fire off ma_sound instances that reference the buffer
    // directly (zero-copy, no temp files, proper lifetime management).
    const ma_decoder_config dec_cfg =
        ma_decoder_config_init(ma_format_f32, 2, ma_engine_get_sample_rate(&impl_->engine));
    ma_decoder dec{};
    ma_result r = ma_decoder_init_memory(data, size, &dec_cfg, &dec);
    if (r != MA_SUCCESS) {
        return std::unexpected(Error{"AudioEngine: failed to decode sound '" + key +
                                     "': " + ma_result_description(r)});
    }

    // Get total frame count so we can allocate the right-sized PCM buffer.
    ma_uint64 frame_count = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frame_count);

    if (frame_count == 0) {
        // Decoder doesn't report length; read all frames into a dynamic vector.
        static constexpr ma_uint64 kChunk = 4096;
        std::vector<float> pcm;
        ma_uint64 total = 0;
        for (;;) {
            pcm.resize((total + kChunk) * 2);
            ma_uint64 read = 0;
            ma_decoder_read_pcm_frames(&dec, pcm.data() + (total * 2), kChunk, &read);
            total += read;
            if (read < kChunk) {
                break;
            }
        }
        pcm.resize(total * 2);
        frame_count = total;

        Impl::SfxEntry entry;
        entry.pcm_buf = std::make_unique<ma_audio_buffer>();
        const ma_audio_buffer_config buf_cfg =
            ma_audio_buffer_config_init(ma_format_f32, 2, frame_count, pcm.data(), nullptr);
        r = ma_audio_buffer_init(&buf_cfg, entry.pcm_buf.get());
        ma_decoder_uninit(&dec);
        if (r != MA_SUCCESS) {
            return std::unexpected(Error{"AudioEngine: failed to create audio buffer for '" + key +
                                         "': " + ma_result_description(r)});
        }
        entry.pcm_buf_init = true;
        entry.loaded = true;
        const std::scoped_lock lk(impl_->sfx_mutex);
        impl_->sfx_cache[key] = std::move(entry);
        return {};
    }

    // Allocate PCM storage and decode all frames.
    std::vector<float> pcm(static_cast<std::size_t>(frame_count) * 2);
    ma_uint64 frames_read = 0;
    ma_decoder_read_pcm_frames(&dec, pcm.data(), frame_count, &frames_read);
    ma_decoder_uninit(&dec);

    Impl::SfxEntry entry;
    entry.pcm_buf = std::make_unique<ma_audio_buffer>();
    const ma_audio_buffer_config buf_cfg =
        ma_audio_buffer_config_init(ma_format_f32, 2, frames_read, pcm.data(), nullptr);
    r = ma_audio_buffer_init(&buf_cfg, entry.pcm_buf.get());
    if (r != MA_SUCCESS) {
        return std::unexpected(Error{"AudioEngine: failed to create audio buffer for '" + key +
                                     "': " + ma_result_description(r)});
    }
    entry.pcm_buf_init = true;
    entry.loaded = true;
    const std::scoped_lock lk(impl_->sfx_mutex);
    impl_->sfx_cache[key] = std::move(entry);
    return {};
}

void AudioEngine::play_sound(const std::filesystem::path& path, float volume) {
    if (!impl_->engine_ok) {
        return;
    }

    const std::string key = path.string();
    const float final_vol = impl_->master_volume * impl_->sfx_volume * volume;

    // Check if this is an in-memory (pre-decoded) sound.
    {
        const std::scoped_lock lk(impl_->sfx_mutex);
        auto it = impl_->sfx_cache.find(key);
        if (it != impl_->sfx_cache.end() && it->second.pcm_buf_init && it->second.pcm_buf) {
            // Seek the audio buffer back to the start and play a new sound from it.
            ma_audio_buffer_seek_to_pcm_frame(it->second.pcm_buf.get(), 0);

            auto s = std::make_unique<ma_sound>();
            ma_sound_config scfg = ma_sound_config_init();
            scfg.pDataSource = it->second.pcm_buf.get();
            scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
            if (ma_sound_init_ex(&impl_->engine, &scfg, s.get()) == MA_SUCCESS) {
                ma_sound_set_volume(s.get(), final_vol);
                ma_sound_start(s.get());
                // Keep alive until finished; update() polls and uninits done sounds.
                const std::scoped_lock lk2(impl_->active_sfx_mutex);
                impl_->active_sfx.push_back(std::move(s));
            }
            return;
        }
    }

    // Play from file path (miniaudio resource manager handles caching).
    const ma_result r = ma_engine_play_sound(&impl_->engine, key.c_str(), nullptr);
    if (r != MA_SUCCESS) {
        log(LogLevel::Warn,
            "AudioEngine: play_sound('" + key + "') failed: " + ma_result_description(r));
        return;
    }
    // ma_engine_play_sound doesn't return a handle, so per-play volume isn't
    // supported via this path. The SFX group volume is the primary control.
    (void)final_vol;
}

void AudioEngine::unload_sound(const std::filesystem::path& path) {
    const std::scoped_lock lk(impl_->sfx_mutex);
    auto it = impl_->sfx_cache.find(path.string());
    if (it != impl_->sfx_cache.end()) {
        if (it->second.pcm_buf_init && it->second.pcm_buf) {
            ma_audio_buffer_uninit(it->second.pcm_buf.get());
        }
        impl_->sfx_cache.erase(it);
    }
    // For file-based sounds, the resource manager will garbage-collect when
    // all referencing sounds finish playing.
}

// ---------------------------------------------------------------------------
// Music — format dispatch
// ---------------------------------------------------------------------------

void AudioEngine::play_music(const std::filesystem::path& path, bool loop) {
    if (!impl_->engine_ok) {
        return;
    }

    impl_->stop_current_music();

    const std::string key = path.string();
    const std::string ext = ext_lower(path);

    if (is_sid_ext(ext)) {
#ifdef XEBBLE_HAS_SIDPLAYFP
        const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
        ma_result r = internal::sid_ds_init_file(impl_->sid_ds, key.c_str(), loop, rate);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn,
                "AudioEngine: failed to load SID '" + key + "': " + ma_result_description(r));
            return;
        }
        impl_->sid_ds_init = true;

        ma_sound_config scfg = ma_sound_config_init();
        scfg.pDataSource = &impl_->sid_ds;
        scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        r = ma_sound_init_ex(&impl_->engine, &scfg, &impl_->sid_sound);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn, "AudioEngine: failed to create SID sound node: " +
                                    std::string(ma_result_description(r)));
            internal::sid_ds_uninit(impl_->sid_ds);
            impl_->sid_ds_init = false;
            return;
        }
        impl_->sid_sound_init = true;
        ma_sound_set_volume(&impl_->sid_sound, impl_->master_volume * impl_->music_volume);
        ma_sound_start(&impl_->sid_sound);
        impl_->music_kind = MusicKind::Sid;
#else
        log(LogLevel::Warn, "AudioEngine: SID support not compiled in. "
                            "Install libsidplayfp and rebuild.");
#endif
        return;
    }

    if (is_xmp_ext(ext)) {
        const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
        ma_result r = internal::xmp_ds_init_file(impl_->xmp_ds, key.c_str(), loop, rate);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn,
                "AudioEngine: failed to load module '" + key + "': " + ma_result_description(r));
            return;
        }
        impl_->xmp_ds_init = true;

        ma_sound_config scfg = ma_sound_config_init();
        scfg.pDataSource = &impl_->xmp_ds;
        scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        r = ma_sound_init_ex(&impl_->engine, &scfg, &impl_->xmp_sound);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn, "AudioEngine: failed to create XMP sound node: " +
                                    std::string(ma_result_description(r)));
            internal::xmp_ds_uninit(impl_->xmp_ds);
            impl_->xmp_ds_init = false;
            return;
        }
        impl_->xmp_sound_init = true;
        ma_sound_set_volume(&impl_->xmp_sound, impl_->master_volume * impl_->music_volume);
        ma_sound_start(&impl_->xmp_sound);
        impl_->music_kind = MusicKind::Xmp;
        return;
    }

    // PCM path (WAV / FLAC / MP3 / OGG).
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    const ma_result r = ma_sound_init_from_file(&impl_->engine, key.c_str(), flags, nullptr,
                                                nullptr, &impl_->pcm_sound);
    if (r != MA_SUCCESS) {
        log(LogLevel::Warn,
            "AudioEngine: failed to load music '" + key + "': " + ma_result_description(r));
        return;
    }
    impl_->pcm_sound_init = true;
    ma_sound_set_looping(&impl_->pcm_sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&impl_->pcm_sound, impl_->master_volume * impl_->music_volume);
    ma_sound_start(&impl_->pcm_sound);
    impl_->music_kind = MusicKind::Pcm;
}

void AudioEngine::play_music_from_memory(const void* data, std::size_t size, std::string_view hint,
                                         bool loop) {
    if (!impl_->engine_ok) {
        return;
    }

    impl_->stop_current_music();

    std::string ext = std::string(hint);
    // Normalise to lowercase.
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Ensure it starts with a dot.
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }

    if (is_sid_ext(ext)) {
#ifdef XEBBLE_HAS_SIDPLAYFP
        const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
        ma_result r = internal::sid_ds_init_memory(impl_->sid_ds, data, size, loop, rate);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn, "AudioEngine: failed to load SID from memory: " +
                                    std::string(ma_result_description(r)));
            return;
        }
        impl_->sid_ds_init = true;

        ma_sound_config scfg = ma_sound_config_init();
        scfg.pDataSource = &impl_->sid_ds;
        scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        r = ma_sound_init_ex(&impl_->engine, &scfg, &impl_->sid_sound);
        if (r != MA_SUCCESS) {
            internal::sid_ds_uninit(impl_->sid_ds);
            impl_->sid_ds_init = false;
            return;
        }
        impl_->sid_sound_init = true;
        ma_sound_set_volume(&impl_->sid_sound, impl_->master_volume * impl_->music_volume);
        ma_sound_start(&impl_->sid_sound);
        impl_->music_kind = MusicKind::Sid;
#else
        log(LogLevel::Warn, "AudioEngine: SID support not compiled in.");
#endif
        return;
    }

    if (is_xmp_ext(ext)) {
        const ma_uint32 rate = ma_engine_get_sample_rate(&impl_->engine);
        ma_result r = internal::xmp_ds_init_memory(impl_->xmp_ds, data, size, loop, rate);
        if (r != MA_SUCCESS) {
            log(LogLevel::Warn, "AudioEngine: failed to load module from memory: " +
                                    std::string(ma_result_description(r)));
            return;
        }
        impl_->xmp_ds_init = true;

        ma_sound_config scfg = ma_sound_config_init();
        scfg.pDataSource = &impl_->xmp_ds;
        scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        r = ma_sound_init_ex(&impl_->engine, &scfg, &impl_->xmp_sound);
        if (r != MA_SUCCESS) {
            internal::xmp_ds_uninit(impl_->xmp_ds);
            impl_->xmp_ds_init = false;
            return;
        }
        impl_->xmp_sound_init = true;
        ma_sound_set_volume(&impl_->xmp_sound, impl_->master_volume * impl_->music_volume);
        ma_sound_start(&impl_->xmp_sound);
        impl_->music_kind = MusicKind::Xmp;
        return;
    }

    // PCM from memory.
    // miniaudio can decode from a memory buffer using ma_decoder as a data source.
    const ma_decoder_config dec_cfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
    // We need to keep the decoder alive for the duration of the sound.
    // Use a static to simplify — only one music track is ever active.
    static ma_decoder s_pcm_decoder{};
    static bool s_decoder_init = false;
    if (s_decoder_init) {
        ma_decoder_uninit(&s_pcm_decoder);
        s_decoder_init = false;
    }

    ma_result r = ma_decoder_init_memory(data, size, &dec_cfg, &s_pcm_decoder);
    if (r != MA_SUCCESS) {
        log(LogLevel::Warn, "AudioEngine: failed to decode PCM music from memory: " +
                                std::string(ma_result_description(r)));
        return;
    }
    s_decoder_init = true;

    ma_sound_config scfg = ma_sound_config_init();
    scfg.pDataSource = &s_pcm_decoder;
    scfg.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
    r = ma_sound_init_ex(&impl_->engine, &scfg, &impl_->pcm_sound);
    if (r != MA_SUCCESS) {
        ma_decoder_uninit(&s_pcm_decoder);
        s_decoder_init = false;
        return;
    }
    impl_->pcm_sound_init = true;
    ma_sound_set_looping(&impl_->pcm_sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_volume(&impl_->pcm_sound, impl_->master_volume * impl_->music_volume);
    ma_sound_start(&impl_->pcm_sound);
    impl_->music_kind = MusicKind::Pcm;
}

void AudioEngine::stop_music() {
    if (impl_) {
        impl_->stop_current_music();
    }
}

void AudioEngine::set_music_paused(bool paused) {
    if (!impl_->engine_ok) {
        return;
    }
    impl_->music_paused_flag = paused;
    switch (impl_->music_kind) {
    case MusicKind::Pcm:
        if (impl_->pcm_sound_init) {
            if (paused) {
                ma_sound_stop(&impl_->pcm_sound);
            } else {
                ma_sound_start(&impl_->pcm_sound);
            }
        }
        break;
    case MusicKind::Xmp:
        if (impl_->xmp_sound_init) {
            if (paused) {
                ma_sound_stop(&impl_->xmp_sound);
            } else {
                ma_sound_start(&impl_->xmp_sound);
            }
        }
        break;
#ifdef XEBBLE_HAS_SIDPLAYFP
    case MusicKind::Sid:
        if (impl_->sid_sound_init) {
            if (paused) {
                ma_sound_stop(&impl_->sid_sound);
            } else {
                ma_sound_start(&impl_->sid_sound);
            }
        }
        break;
#endif
    case MusicKind::None:
        break;
    }
}

bool AudioEngine::music_playing() const {
    return impl_ && impl_->music_kind != MusicKind::None;
}

bool AudioEngine::music_paused() const {
    return impl_ && impl_->music_paused_flag;
}

// ---------------------------------------------------------------------------
// Volume
// ---------------------------------------------------------------------------

void AudioEngine::set_master_volume(float volume) {
    if (!impl_) {
        return;
    }
    impl_->master_volume = volume;
    if (impl_->engine_ok) {
        ma_engine_set_volume(&impl_->engine, volume);
    }
    impl_->apply_music_volume();
}

float AudioEngine::master_volume() const {
    return impl_ ? impl_->master_volume : 1.0f;
}

void AudioEngine::set_sfx_volume(float volume) {
    if (impl_) {
        impl_->sfx_volume = volume;
    }
}

float AudioEngine::sfx_volume() const {
    return impl_ ? impl_->sfx_volume : 1.0f;
}

void AudioEngine::set_music_volume(float volume) {
    if (!impl_) {
        return;
    }
    impl_->music_volume = volume;
    impl_->apply_music_volume();
}

float AudioEngine::music_volume() const {
    return impl_ ? impl_->music_volume : 1.0f;
}

// ---------------------------------------------------------------------------
// Capability queries
// ---------------------------------------------------------------------------

bool AudioEngine::sid_supported() {
#ifdef XEBBLE_HAS_SIDPLAYFP
    return true;
#else
    return false;
#endif
}

bool AudioEngine::device_available() const {
    return impl_ && impl_->engine_ok;
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void AudioEngine::update() {
    if (!impl_ || !impl_->engine_ok) {
        return;
    }

    // Reap finished fire-and-forget SFX sounds.
    {
        const std::scoped_lock lk(impl_->active_sfx_mutex);
        auto& vec = impl_->active_sfx;
        std::erase_if(vec, [](const std::unique_ptr<ma_sound>& s) {
            if (ma_sound_at_end(s.get()) != 0U) {
                ma_sound_uninit(s.get());
                return true;
            }
            return false;
        });
    }

    // For non-looping PCM tracks: check if playback has ended.
    if (impl_->music_kind == MusicKind::Pcm && impl_->pcm_sound_init) {
        if (ma_sound_at_end(&impl_->pcm_sound) != 0U) {
            impl_->stop_current_music();
        }
    }

    // XMP and SID: the data source sets at_end when done; stop the sound.
    if (impl_->music_kind == MusicKind::Xmp && impl_->xmp_sound_init) {
        if (impl_->xmp_ds.at_end) {
            impl_->stop_current_music();
        }
    }
#ifdef XEBBLE_HAS_SIDPLAYFP
    if (impl_->music_kind == MusicKind::Sid && impl_->sid_sound_init) {
        if (impl_->sid_ds.at_end) {
            impl_->stop_current_music();
        }
    }
#endif
}

} // namespace xebble
