/// @file main.cpp  (ex17_audio)
/// @brief Audio engine demonstration — sound effects, MP3/SID music.
///
/// Demonstrates:
///   - Accessing AudioEngine from a World resource
///   - Loading and playing a WAV sound effect from a file
///   - Playing an MP3 music track from a file path
///   - Playing a SID chiptune from a file path (when libsidplayfp is available)
///   - Master, SFX, and music volume controls via the immediate-mode UI
///   - Checking AudioEngine::sid_supported() at runtime
///
/// Bundled assets (copied next to the binary by CMake at build time):
///   coin.wav   — WAV sound effect
///   test.mp3   — MP3 music track
///   test.sid   — SID chiptune (Secret of Monkey Island)

#include <xebble/xebble.hpp>

#include <filesystem>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// AudioSystem
// ---------------------------------------------------------------------------

class AudioSystem : public xebble::System {
    bool sfx_loaded_ = false;
    std::string sfx_path_;

    std::string mp3_path_;
    std::string sid_path_;

    float master_vol_ = 1.0f;
    float sfx_vol_ = 1.0f;
    float music_vol_ = 1.0f;

public:
    void init(xebble::World& world) override {
        auto& audio = *world.resource<xebble::AudioEngine*>();

        // Search cwd then next to the executable.
        auto find_asset = [](std::initializer_list<const char*> names) -> std::string {
            namespace fs = std::filesystem;
            std::vector<fs::path> search_dirs{fs::current_path()};
            std::error_code ec;
            const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
            if (!ec) {
                search_dirs.emplace_back(exe.parent_path());
            }
            for (const auto* name : names) {
                for (const auto& dir : search_dirs) {
                    const fs::path p = dir / name;
                    if (fs::exists(p)) {
                        return p.string();
                    }
                }
            }
            return {};
        };

        // --- Load WAV SFX from file ---
        sfx_path_ = find_asset({"coin.wav"});
        if (!sfx_path_.empty()) {
            auto r = audio.load_sound(sfx_path_);
            if (r) {
                sfx_loaded_ = true;
            } else {
                xebble::log(xebble::LogLevel::Warn, "ex17: " + r.error().message);
            }
        }

        // --- Locate music files ---
        mp3_path_ = find_asset({"test.mp3", "test.ogg", "test.wav"});
        if (xebble::AudioEngine::sid_supported()) {
            sid_path_ = find_asset({"test.sid", "test.psid", "test.rsid"});
        }

        // Sync volume members with the engine's current state.
        master_vol_ = audio.master_volume();
        sfx_vol_ = audio.sfx_volume();
        music_vol_ = audio.music_volume();
    }

    void update(xebble::World& world, float /*dt*/) override {
        for (const auto& e : world.resource<xebble::EventQueue>().events) {
            if (e.type == xebble::EventType::KeyPress && e.key().key == xebble::Key::Escape) {
                std::exit(0);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& audio = *world.resource<xebble::AudioEngine*>();
        auto& ui = world.resource<xebble::UIContext>();

        // ---- Main panel ----
        ui.panel("main", {.anchor = xebble::Anchor::Center, .size = {520, 340}}, [&](auto& p) {
            p.text(u8"ex17 \u2014 Audio Engine", {.color = {220, 220, 100}});
            p.text(u8" ");

            // --- Sound effect ---
            p.text(u8"Sound Effects", {.color = {180, 220, 180}});
            if (sfx_loaded_) {
                const auto label =
                    std::u8string(u8"Play ") +
                    std::u8string(std::filesystem::path(sfx_path_).filename().u8string());
                if (p.button(label)) {
                    audio.play_sound(sfx_path_);
                }
            } else {
                p.text(u8"  [coin.wav not found next to binary]", {.color = {180, 80, 80}});
            }
            p.text(u8" ");

            // --- MP3 / PCM music ---
            p.text(u8"Music (MP3/OGG/WAV via miniaudio)", {.color = {180, 220, 180}});
            if (mp3_path_.empty()) {
                p.text(u8"  Place test.mp3 next to the executable.", {.color = {140, 140, 140}});
            } else {
                const auto fname =
                    std::u8string(std::filesystem::path(mp3_path_).filename().u8string());
                if (p.button(u8"Play " + fname + u8" (loop)")) {
                    audio.play_music(mp3_path_);
                }
                if (p.button(u8"Stop music")) {
                    audio.stop_music();
                }
            }
            p.text(u8" ");

            // --- SID chiptune ---
            p.text(u8"SID Chiptune (libsidplayfp)", {.color = {180, 220, 180}});
            if (!xebble::AudioEngine::sid_supported()) {
                p.text(u8"  Not compiled in.", {.color = {140, 140, 140}});
            } else if (sid_path_.empty()) {
                p.text(u8"  Place test.sid next to the executable.", {.color = {140, 140, 140}});
            } else {
                const auto fname =
                    std::u8string(std::filesystem::path(sid_path_).filename().u8string());
                if (p.button(u8"Play " + fname + u8" (loop)")) {
                    audio.play_music(sid_path_);
                }
                if (p.button(u8"Stop music")) {
                    audio.stop_music();
                }
            }
        });

        // ---- Volume panel ----
        ui.panel("volume",
                 {.anchor = xebble::Anchor::BottomRight, .size = {280, 120}, .offset = {-8, -8}},
                 [&](auto& p) {
                     p.text(u8"Volume Controls", {.color = {200, 200, 200}});

                     bool mute = (master_vol_ < 0.01f);
                     const bool prev_mute = mute;
                     p.checkbox(u8"Mute master", mute);
                     if (mute != prev_mute) {
                         master_vol_ = mute ? 0.0f : 1.0f;
                         audio.set_master_volume(master_vol_);
                     }

                     bool quiet_sfx = (sfx_vol_ < 0.51f);
                     const bool prev_quiet_sfx = quiet_sfx;
                     p.checkbox(u8"SFX 50 %", quiet_sfx);
                     if (quiet_sfx != prev_quiet_sfx) {
                         sfx_vol_ = quiet_sfx ? 0.5f : 1.0f;
                         audio.set_sfx_volume(sfx_vol_);
                     }

                     bool quiet_music = (music_vol_ < 0.51f);
                     const bool prev_quiet_music = quiet_music;
                     p.checkbox(u8"Music 50 %", quiet_music);
                     if (quiet_music != prev_quiet_music) {
                         music_vol_ = quiet_music ? 0.5f : 1.0f;
                         audio.set_music_volume(music_vol_);
                     }

                     const char8_t* status = u8"stopped";
                     if (audio.music_playing()) {
                         status = audio.music_paused() ? u8"paused" : u8"playing";
                     }
                     p.text(std::u8string(u8"Music: ") + status, {.color = {160, 160, 160}});
                 });

        // ---- Bottom bar ----
        ui.panel("bar", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 20}}, [&](auto& p) {
            p.text(u8"[Esc] Quit  |  SID support: " +
                       std::u8string(xebble::AudioEngine::sid_supported() ? u8"yes" : u8"no"),
                   {.color = {160, 160, 160}});
        });

        xebble::debug_overlay(world, renderer);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<AudioSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex17 \xe2\x80\x94 Audio", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 960, .virtual_height = 540},
        });
}
