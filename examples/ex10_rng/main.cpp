/// @file main.cpp  (ex10_rng)
/// @brief Random number generation: PCG32 Rng, dice, weighted selection,
///        shuffle, save/restore state.
///
/// Demonstrates:
///   - Constructing `Rng` from a seed
///   - `rng.roll("NdF+M")` dice notation
///   - `rng.range(min, max)` uniform integers
///   - `rng.weighted_index()` / `rng.weighted_choice()` loot tables
///   - `rng.shuffle()` Fisher-Yates permutation
///   - `rng.save()` / `rng.restore()` deterministic replay
///   - `rng.chance(p)` / `rng.one_in(n)` probability helpers

#include <xebble/xebble.hpp>

#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace {

struct RngDemo {
    // Current seed.
    uint64_t seed = 1234;

    // Most recent dice results.
    int d6 = 0;
    int d20 = 0;
    int sum3d6 = 0;
    int dmg = 0; // "2d6+3"

    // Loot table results.
    std::string last_loot;
    int loot_counts[4] = {0, 0, 0, 0};

    // Shuffle demo.
    std::vector<int> deck;

    // Replay demo: two rolls from the same saved state.
    int replay_a = 0;
    int replay_b = 0;
    bool replay_match = false;

    // Running roll history for histogram.
    int histogram[6] = {};
    int histogram_total = 0;

    // Generator saved state.
    xebble::RngState checkpoint{};
    bool has_checkpoint = false;
};

class RngSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        world.add_resource(RngDemo{});
        auto& demo = world.resource<RngDemo>();

        // Fresh deck 1-10.
        demo.deck.resize(10);
        for (int i = 0; i < 10; ++i)
            demo.deck[i] = i + 1;

        roll_all(world);
    }

    void update(xebble::World& world, float) override {
        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape)
                std::exit(0);
            if (k == xebble::Key::Space)
                roll_all(world);
            if (k == xebble::Key::S)
                save_checkpoint(world);
            if (k == xebble::Key::R)
                restore_replay(world);
            if (k == xebble::Key::N) {
                ++world.resource<RngDemo>().seed;
                roll_all(world);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& demo = world.resource<RngDemo>();
        auto& ui = world.resource<xebble::UIContext>();

        ui.panel("rng_main", {.anchor = xebble::Anchor::Center, .size = {500, 250}}, [&](auto& p) {
            p.text(u8"ex10 \u2014 RNG Demo", {.color = {220, 220, 100}});
            {
                auto s =
                    std::format("Seed {:d}  [Space] Re-roll  [N] New seed  [Esc] Quit", demo.seed);
                p.text(std::u8string(s.begin(), s.end()), {.color = {180, 180, 200}});
            }

            // Dice rolls.
            {
                auto s = std::format("d6={:2d}  d20={:2d}  3d6={:2d}  2d6+3={:2d}", demo.d6,
                                     demo.d20, demo.sum3d6, demo.dmg);
                p.text(std::u8string(s.begin(), s.end()), {.color = {160, 220, 160}});
            }

            // Histogram of d6 rolls.
            {
                std::string hist = "d6 histogram: ";
                for (int i = 0; i < 6; ++i)
                    hist += std::format("{:d}:{:3d}  ", i + 1, demo.histogram[i]);
                p.text(std::u8string(hist.begin(), hist.end()), {.color = {140, 200, 220}});
            }

            // Loot table.
            {
                auto s = std::format("Loot: {:s}  (gold:{:d} pot:{:d} scroll:{:d} rare:{:d})",
                                     demo.last_loot, demo.loot_counts[0], demo.loot_counts[1],
                                     demo.loot_counts[2], demo.loot_counts[3]);
                p.text(std::u8string(s.begin(), s.end()), {.color = {220, 180, 100}});
            }

            // Shuffle.
            {
                std::string deck_str = "Deck: ";
                for (int v : demo.deck)
                    deck_str += std::to_string(v) + " ";
                p.text(std::u8string(deck_str.begin(), deck_str.end()), {.color = {200, 160, 220}});
            }

            // Replay.
            p.text(u8"[S] Save checkpoint  [R] Restore+replay", {.color = {160, 160, 160}});
            if (demo.has_checkpoint) {
                auto s = std::format("Replay A={:2d}  B={:2d}  Match: {:s}", demo.replay_a,
                                     demo.replay_b, demo.replay_match ? "YES" : "NO");
                p.text(std::u8string(s.begin(), s.end()),
                       {.color = demo.replay_match ? xebble::Color{100, 220, 100}
                                                   : xebble::Color{220, 100, 100}});
            }
        });

        ui.panel("hud", {.anchor = xebble::Anchor::Bottom, .size = {1.0f, 20}}, [](auto& p) {
            p.text(u8"ex10 \u2014 RNG  |  [Esc] Quit", {.color = {200, 200, 200}});
        });
        xebble::debug_overlay(world, renderer);
    }

private:
    void roll_all(xebble::World& world) {
        auto& demo = world.resource<RngDemo>();
        xebble::Rng rng(demo.seed);

        demo.d6 = rng.roll_die(6);
        demo.d20 = rng.roll_die(20);
        demo.sum3d6 = rng.roll_dice(3, 6);
        demo.dmg = rng.roll("2d6+3");

        // Histogram: roll d6 100 times.
        std::fill(std::begin(demo.histogram), std::end(demo.histogram), 0);
        for (int i = 0; i < 200; ++i)
            ++demo.histogram[rng.roll_die(6) - 1];
        demo.histogram_total = 200;

        // Weighted loot table.
        std::vector<float> weights = {60.0f, 25.0f, 10.0f, 5.0f};
        std::vector<std::string> names = {"Gold", "Potion", "Scroll", "Rare sword"};
        size_t pick = rng.weighted_index(weights);
        demo.last_loot = names[pick];
        ++demo.loot_counts[pick];

        // Shuffle the deck.
        rng.shuffle(demo.deck);

        // Save a checkpoint for replay demo.
        if (!demo.has_checkpoint) {
            demo.checkpoint = rng.save();
        }
    }

    void save_checkpoint(xebble::World& world) {
        auto& demo = world.resource<RngDemo>();
        xebble::Rng rng(demo.seed);
        // Advance to a mid-stream point, then save.
        for (int i = 0; i < 50; ++i)
            (void)rng.roll_die(6);
        demo.checkpoint = rng.save();
        demo.has_checkpoint = true;
        demo.replay_a = rng.roll_die(20);
    }

    void restore_replay(xebble::World& world) {
        auto& demo = world.resource<RngDemo>();
        if (!demo.has_checkpoint)
            return;
        // Restore the saved state and roll again — must match.
        xebble::Rng rng(demo.checkpoint);
        demo.replay_b = rng.roll_die(20);
        demo.replay_match = (demo.replay_a == demo.replay_b);
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<RngSystem>();

    return xebble::run(std::move(world),
                       {
                           .window = {.title = "ex10 — RNG", .width = 1280, .height = 720},
                           .renderer = {.virtual_width = 640, .virtual_height = 360},
                       });
}
