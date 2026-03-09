/// @file audio.hpp
/// @brief Audio engine for sound effects and music playback.
///
/// Xebble's audio system wraps three complementary backends behind a single
/// `AudioEngine` API:
///
/// | Format | Backend | Notes |
/// |---|---|---|
/// | WAV, FLAC, MP3, OGG Vorbis | miniaudio built-in decoders | Low-latency SFX + streaming music |
/// | MOD, XM, IT, S3M, and 50+ tracker formats | libxmp | Classic demoscene / Amiga music |
/// | SID (.sid) | libsidplayfp | C64 chiptune music |
///
/// SID support is always available — libsidplayfp and libresidfp are built
/// from source automatically via CMake ExternalProject.
///
/// ## Quick-start
///
/// `AudioEngine` is injected by `xebble::run()` as a World resource. Access
/// it from any system with `world.resource<AudioEngine>()`.
///
/// @code
/// void MySystem::init(World& world) {
///     auto& audio = world.resource<AudioEngine>();
///
///     // Pre-load a sound effect.
///     if (auto r = audio.load_sound("sfx/sword_hit.wav"); !r)
///         log(LogLevel::Warn, "Could not load sword sound");
///
///     // Start looping background music.
///     audio.play_music("music/dungeon_theme.xm");
/// }
///
/// void MySystem::update(World& world, float) {
///     auto& audio = world.resource<AudioEngine>();
///
///     if (player_attacked) {
///         audio.play_sound("sfx/sword_hit.wav");
///     }
///     if (player_died) {
///         audio.stop_music();
///         audio.play_sound("sfx/game_over.wav");
///     }
/// }
/// @endcode
///
/// ## Volume groups
///
/// There are three independent volume controls:
///   - `set_master_volume(v)` — scales all audio output (0.0 = silent, 1.0 = full).
///   - `set_sfx_volume(v)`    — scales all sound-effect playback.
///   - `set_music_volume(v)`  — scales the currently playing music track.
///
/// ## Sound caching
///
/// `load_sound()` decodes and caches an audio file in memory. Subsequent
/// `play_sound()` calls for the same path play from the cached data
/// with near-zero latency. Sounds loaded via the AssetManager manifest
/// (under `[sounds]`) are pre-loaded automatically.
///
/// ## Music streaming
///
/// Only one music track plays at a time. `play_music()` stops the current
/// track and starts the new one. MOD/SID tracks loop by default; PCM tracks
/// loop when `loop = true`.
///
/// @code
/// // Immediately cross-fade to a new track.
/// audio.play_music("music/boss_fight.it");
///
/// // One-shot fanfare then resume normal music.
/// audio.play_music("music/level_clear.wav", /*loop=*/false);
/// @endcode
#pragma once

#include <xebble/types.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace xebble {

/// @brief The Xebble audio engine.
///
/// Owns a miniaudio engine instance and manages sound-effect caching, music
/// streaming (PCM, MOD, SID), and per-group volume control.
///
/// Created by `xebble::run()` and injected as a World resource before
/// `System::init()` is called. Do not construct this directly.
///
/// Move-only; copying is disabled because the internal miniaudio state is
/// non-relocatable after initialisation.
class AudioEngine {
public:
    /// @brief Initialise the audio engine and open the default playback device.
    ///
    /// Called by `xebble::run()`. Returns an error if the audio device cannot
    /// be opened (e.g. no sound hardware). When this fails the engine falls
    /// back to a null (silent) mode so the game still runs.
    ///
    /// @return A ready `AudioEngine`, or an `Error` if the device failed to open.
    [[nodiscard]] static std::expected<AudioEngine, Error> create();

    ~AudioEngine();
    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // -----------------------------------------------------------------------
    // Sound effects
    // -----------------------------------------------------------------------

    /// @brief Pre-load and cache a sound file.
    ///
    /// The file is decoded fully into memory on the calling thread. Subsequent
    /// `play_sound()` calls for the same path use the cached data.
    ///
    /// Supported formats: WAV, FLAC, MP3, OGG Vorbis.
    ///
    /// @param path  Path to the audio file (relative or absolute).
    /// @return      Error message on failure; success is the empty expected.
    [[nodiscard]] std::expected<void, Error> load_sound(const std::filesystem::path& path);

    /// @brief Load a sound from raw in-memory bytes.
    ///
    /// Useful for sounds embedded in the binary or loaded from a ZIP archive.
    /// The `name` is the key used to identify the sound for `play_sound()`.
    ///
    /// @param name   Unique name for lookups in `play_sound()`.
    /// @param data   Pointer to the encoded audio data.
    /// @param size   Size of the data in bytes.
    [[nodiscard]] std::expected<void, Error>
    load_sound_from_memory(std::string_view name, const void* data, std::size_t size);

    /// @brief Play a previously loaded (or auto-loading) sound effect.
    ///
    /// Fire-and-forget: the engine manages the sound instance internally and
    /// recycles it when playback completes. Multiple concurrent instances of
    /// the same sound are supported.
    ///
    /// If `path` was not pre-loaded via `load_sound()`, the engine attempts a
    /// synchronous load on first play (may stutter on slow storage).
    ///
    /// @param path    Path (or name) of the sound asset.
    /// @param volume  Per-instance volume multiplier (1.0 = full, 0.0 = silent).
    void play_sound(const std::filesystem::path& path, float volume = 1.0f);

    /// @brief Unload a cached sound, freeing its memory.
    ///
    /// Safe to call even if the sound is not currently loaded.
    ///
    /// @param path  Path (or name) of the cached sound to remove.
    void unload_sound(const std::filesystem::path& path);

    // -----------------------------------------------------------------------
    // Music
    // -----------------------------------------------------------------------

    /// @brief Start playing a music track (replaces any currently playing track).
    ///
    /// The format is detected from the file extension:
    ///   - `.wav` / `.flac` / `.mp3` / `.ogg`  → miniaudio PCM decoder
    ///   - `.mod` / `.xm` / `.it` / `.s3m` / `.med` / … → libxmp
    ///   - `.sid` / `.psid` / `.rsid`           → libsidplayfp
    ///
    /// `play_music()` is non-blocking; the audio thread begins decoding
    /// immediately.
    ///
    /// @param path  Path to the music file.
    /// @param loop  Whether to loop the track (default: true).
    ///              MOD and SID tracks always loop regardless of this flag.
    void play_music(const std::filesystem::path& path, bool loop = true);

    /// @brief Load a music track from raw in-memory bytes and start playing it.
    ///
    /// The format is inferred from a `hint` extension (e.g. `".xm"`, `".sid"`).
    /// Useful for music packed in a ZIP archive or embedded in the binary.
    ///
    /// @param data  Pointer to the encoded music data.
    /// @param size  Size of the data in bytes.
    /// @param hint  File-extension hint used to select the decoder (e.g. `".mod"`).
    /// @param loop  Whether to loop (default: true).
    void play_music_from_memory(const void* data, std::size_t size, std::string_view hint,
                                bool loop = true);

    /// @brief Stop the currently playing music track.
    ///
    /// Idempotent — safe to call when no music is playing.
    void stop_music();

    /// @brief Pause or resume the currently playing music track.
    ///
    /// Has no effect when no music is playing.
    ///
    /// @param paused  `true` to pause, `false` to resume.
    void set_music_paused(bool paused);

    /// @brief Returns `true` if a music track is currently active (not stopped).
    ///
    /// Returns `true` even when the track is paused.
    [[nodiscard]] bool music_playing() const;

    /// @brief Returns `true` if the music track is paused.
    [[nodiscard]] bool music_paused() const;

    // -----------------------------------------------------------------------
    // Volume control
    // -----------------------------------------------------------------------

    /// @brief Set the master output volume for all audio (0.0–1.0).
    ///
    /// This scales all sounds and music uniformly. Values above 1.0 amplify.
    void set_master_volume(float volume);

    /// @brief Get the current master volume.
    [[nodiscard]] float master_volume() const;

    /// @brief Set the volume multiplier for sound effects (0.0–1.0).
    void set_sfx_volume(float volume);

    /// @brief Get the current SFX volume.
    [[nodiscard]] float sfx_volume() const;

    /// @brief Set the volume multiplier for music (0.0–1.0).
    void set_music_volume(float volume);

    /// @brief Get the current music volume.
    [[nodiscard]] float music_volume() const;

    // -----------------------------------------------------------------------
    // Capability queries
    // -----------------------------------------------------------------------

    /// @brief Returns `true` if libsidplayfp SID support is available.
    ///
    /// Always returns `true` — libsidplayfp is built from source and
    /// unconditionally linked.
    [[nodiscard]] static bool sid_supported();

    /// @brief Returns `true` if the audio device was successfully opened.
    ///
    /// When `false`, all play calls are silently ignored (the engine is in
    /// null/silent mode).
    [[nodiscard]] bool device_available() const;

    // -----------------------------------------------------------------------
    // Per-frame update (called by the engine, not by user code)
    // -----------------------------------------------------------------------

    /// @brief Advance the audio engine state for one frame.
    ///
    /// Called internally by the game loop. Processes end-of-track callbacks
    /// for non-looping music and recycles completed sound instances.
    void update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioEngine() = default;
};

} // namespace xebble
