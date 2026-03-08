/// @file rng.hpp
/// @brief Seedable, deterministic random number generation for roguelikes.
///
/// Provides a value-type RNG (`Rng`) built on the **PCG32** algorithm with a
/// suite of roguelike-friendly helpers on top:
///
///   - Integer and float range sampling
///   - Dice-notation evaluation (`roll("3d6+2")`)
///   - Weighted random selection from a loot/encounter table
///   - Fisher-Yates shuffle
///   - Serializable state (save/restore for replays and save files)
///
/// The RNG has **no dependency** on the ECS or any other Xebble subsystem.
/// Store it as a World resource, thread-locally, or keep multiple independent
/// generators running in parallel — whichever suits the use case.
///
/// ---
///
/// ## Typical setup
///
/// @code
/// #include <xebble/rng.hpp>
/// using namespace xebble;
///
/// // Seed from a fixed value for a reproducible game world.
/// Rng rng(12345);
///
/// // Or seed from the system clock for a different world each run.
/// #include <chrono>
/// Rng rng(std::chrono::steady_clock::now().time_since_epoch().count());
/// @endcode
///
/// ## Determinism and save files
///
/// Because the RNG state is fully captured by two 64-bit integers, you can
/// snapshot it before any sequence of random decisions and restore it later to
/// reproduce the exact same outcome — useful for save/load, replays, and tests.
///
/// @code
/// RngState saved = rng.save();
/// int roll1 = rng.roll("3d6");
/// rng.restore(saved);
/// int roll2 = rng.roll("3d6");
/// assert(roll1 == roll2);  // always true
/// @endcode
///
/// ## Using multiple independent generators
///
/// Keeping separate Rng objects for different subsystems prevents one system's
/// RNG calls from affecting another's sequence, making bugs easier to isolate.
///
/// @code
/// Rng world_rng(seed);        // dungeon layout, item placement
/// Rng combat_rng(seed + 1);   // attack rolls, damage
/// Rng ai_rng(seed + 2);       // monster decisions, patrol paths
/// @endcode
#pragma once

#include <xebble/serial.hpp>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// RngState — serializable snapshot of RNG internal state
// ---------------------------------------------------------------------------

/// @brief Plain-old-data snapshot of Rng internals. Safe to memcpy / serialize.
///
/// Store this in your save file to make RNG sequences fully reproducible across
/// sessions. Because it is just two integers it fits in any serialization format.
///
/// @code
/// // In your save-game writer:
/// RngState state = rng.save();
/// file.write(state.state);  // uint64_t
/// file.write(state.inc);    // uint64_t
///
/// // In your save-game reader:
/// RngState state;
/// state.state = file.read<uint64_t>();
/// state.inc   = file.read<uint64_t>();
/// rng.restore(state);
/// @endcode
struct RngState {
    uint64_t state = 0; ///< PCG32 internal state word.
    uint64_t inc = 0;   ///< PCG32 stream selector (always odd internally).

    bool operator==(const RngState&) const = default;
};

// ---------------------------------------------------------------------------
// Rng — PCG32 random number generator
// ---------------------------------------------------------------------------

/// @brief Seedable, deterministic 32-bit RNG using the PCG32 algorithm.
///
/// PCG32 (Permuted Congruential Generator) passes all BigCrush statistical
/// tests, advances in a single multiply + xor-shift step, and has a full
/// 2^64 period. The complete state is two `uint64_t` values — trivially
/// copyable, storable in a save file, and suitable for passing by value.
///
/// ### Basic usage
///
/// @code
/// Rng rng(42);
///
/// // Coin flip.
/// bool heads = rng.coin_flip();
///
/// // A 1-in-4 chance (critical hit, rare drop, etc.).
/// if (rng.one_in(4)) { trigger_critical(); }
///
/// // Random integer in a range.
/// int gold = rng.range(10, 50);         // 10..50 inclusive
/// int floor_number = rng.range(5);      // 0..5  inclusive
///
/// // Dice rolls using standard notation.
/// int hit_roll  = rng.roll("1d20");     // d20 attack roll
/// int damage    = rng.roll("2d6+3");    // sword damage
/// int fireball  = rng.roll("8d6");      // fireball spell
///
/// // Probability threshold.
/// if (rng.chance(0.15f)) { spawn_bonus_item(); }  // 15% chance
/// @endcode
class Rng {
public:
    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// @brief Construct with a seed value.
    ///
    /// Two Rng objects created with different seeds produce completely
    /// independent streams. The same seed always produces the same sequence.
    ///
    /// @code
    /// Rng rng_a(1);
    /// Rng rng_b(2);
    /// // rng_a and rng_b will produce different values.
    ///
    /// Rng rng_c(1);
    /// // rng_c will produce the same values as rng_a.
    /// @endcode
    explicit Rng(uint64_t seed = 0) noexcept {
        state_ = 0u;
        inc_ = (seed << 1u) | 1u; // inc must be odd
        next_raw();
        state_ += seed;
        next_raw();
    }

    /// @brief Construct from a previously saved RngState, resuming that stream.
    ///
    /// @code
    /// RngState checkpoint = rng.save();
    /// // ... later, in a load routine ...
    /// Rng rng(checkpoint);
    /// @endcode
    explicit Rng(RngState s) noexcept : state_(s.state), inc_(s.inc) {}

    // -----------------------------------------------------------------------
    // State serialization
    // -----------------------------------------------------------------------

    /// @brief Capture the current generator state as a restorable snapshot.
    ///
    /// The snapshot is a cheap value copy (two 64-bit integers). It can be
    /// embedded in a save file, a game event log, or a test fixture.
    ///
    /// @code
    /// // Save state before a combat sequence so we can replay it.
    /// RngState pre_combat = rng.save();
    ///
    /// int result = resolve_combat(attacker, defender, rng);
    ///
    /// // To replay identically:
    /// rng.restore(pre_combat);
    /// int same_result = resolve_combat(attacker, defender, rng);
    /// assert(result == same_result);
    /// @endcode
    RngState save() const noexcept { return {state_, inc_}; }

    /// @brief Restore a previously saved state, rewinding the stream.
    ///
    /// All subsequent calls will produce the same values as they did after the
    /// original save() call. Useful for deterministic replays, procedural
    /// regeneration, and debugging.
    void restore(RngState s) noexcept {
        state_ = s.state;
        inc_ = s.inc;
    }

    // -----------------------------------------------------------------------
    // Core primitives
    // -----------------------------------------------------------------------

    /// @brief Return a uniformly distributed `uint32_t` in [0, 2^32).
    ///
    /// The raw output of one PCG32 step. Use this when you need bits directly
    /// or are building a higher-level distribution on top of the generator.
    uint32_t next_u32() noexcept { return next_raw(); }

    /// @brief Return a uniformly distributed `uint64_t` (two PCG32 steps).
    uint64_t next_u64() noexcept {
        uint64_t lo = next_raw();
        uint64_t hi = next_raw();
        return (hi << 32u) | lo;
    }

    /// @brief Return a uniformly distributed `float` in [0.0, 1.0).
    ///
    /// Suitable for probability thresholds, linear interpolation, or any
    /// calculation that expects a value in the unit interval.
    ///
    /// @code
    /// // Linearly interpolate between two colours by a random amount.
    /// float t = rng.next_float();
    /// Color c = lerp(color_a, color_b, t);
    /// @endcode
    float next_float() noexcept {
        // Multiply by 2^-32 to map [0, 2^32) -> [0, 1).
        return static_cast<float>(next_raw()) * 2.3283064365386963e-10f;
    }

    /// @brief Return a uniformly distributed `double` in [0.0, 1.0).
    double next_double() noexcept {
        return static_cast<double>(next_u64()) * 5.421010862427522e-20;
    }

    // -----------------------------------------------------------------------
    // Integer range sampling
    // -----------------------------------------------------------------------

    /// @brief Return a uniform integer in [min, max] (both inclusive).
    ///
    /// Uses rejection sampling to eliminate modulo bias, so every value in the
    /// range has exactly equal probability regardless of range size.
    ///
    /// @pre min <= max
    ///
    /// @code
    /// // Spawn 3 to 7 goblins in a room.
    /// int count = rng.range(3, 7);
    ///
    /// // Random X position anywhere on a 40-column map.
    /// int x = rng.range(0, 39);
    ///
    /// // Random Y position in the top half of a 25-row map.
    /// int y = rng.range(0, 11);
    ///
    /// // Negative ranges are fine.
    /// int temperature = rng.range(-20, 40);
    /// @endcode
    int32_t range(int32_t min, int32_t max) noexcept {
        if (min == max)
            return min;
        uint32_t span = static_cast<uint32_t>(max - min) + 1u;
        uint32_t threshold = (~span + 1u) % span; // = 2^32 mod span
        uint32_t r;
        do {
            r = next_raw();
        } while (r < threshold);
        return min + static_cast<int32_t>(r % span);
    }

    /// @brief Return a uniform integer in [0, max] (both inclusive).
    ///
    /// Convenience overload for when the minimum is zero.
    ///
    /// @pre max >= 0
    ///
    /// @code
    /// // Pick a random index into a vector.
    /// int idx = rng.range((int)items.size() - 1);
    /// auto& item = items[idx];
    /// @endcode
    int32_t range(int32_t max) noexcept { return range(0, max); }

    // -----------------------------------------------------------------------
    // Dice
    // -----------------------------------------------------------------------

    /// @brief Roll a single die with @p faces faces, returning a value in [1, faces].
    ///
    /// @pre faces >= 1 (a 1-faced die always returns 1)
    ///
    /// @code
    /// int d6    = rng.roll_die(6);   // 1..6
    /// int d20   = rng.roll_die(20);  // 1..20
    /// int d100  = rng.roll_die(100); // 1..100  (percentile roll)
    /// @endcode
    int32_t roll_die(int32_t faces) noexcept {
        if (faces <= 1)
            return 1;
        return range(1, faces);
    }

    /// @brief Roll @p count dice each with @p faces faces and return the sum.
    ///
    /// Equivalent to the dice-notation `NdF` where N = count, F = faces.
    ///
    /// @pre count >= 1, faces >= 1
    ///
    /// @code
    /// int stat  = rng.roll_dice(3, 6);   // 3d6 — classic D&D ability score
    /// int burst = rng.roll_dice(4, 8);   // 4d8 — burst fire damage
    /// @endcode
    int32_t roll_dice(int32_t count, int32_t faces) noexcept {
        int32_t total = 0;
        for (int32_t i = 0; i < count; ++i)
            total += roll_die(faces);
        return total;
    }

    /// @brief Evaluate a dice expression in standard RPG notation.
    ///
    /// Supported grammar: `[N]d[F][+/-M]`
    ///
    /// | Part | Meaning | Default |
    /// |------|---------|---------|
    /// | N    | Number of dice | 1 |
    /// | F    | Faces per die  | (required) |
    /// | M    | Flat modifier  | 0 |
    ///
    /// The expression is **case-insensitive** for `d`. Modifiers use `+` or `-`.
    ///
    /// @throws std::invalid_argument if the expression cannot be parsed.
    ///
    /// @code
    /// // Single die (N omitted defaults to 1):
    /// rng.roll("d6");       // 1..6
    /// rng.roll("d20");      // 1..20
    ///
    /// // Multiple dice:
    /// rng.roll("2d6");      // 2..12   — sword damage
    /// rng.roll("3d6");      // 3..18   — ability score roll
    /// rng.roll("8d6");      // 8..48   — fireball
    ///
    /// // With modifiers:
    /// rng.roll("2d6+3");    // 5..15   — weapon + strength bonus
    /// rng.roll("1d20-2");   // -1..18  — attack with penalty
    /// rng.roll("4d4+4");    // 8..20   — classic D&D starting HP
    ///
    /// // Store expressions in data (e.g. loaded from a TOML item definition).
    /// std::string_view dmg = weapon["damage"].value_or("1d4");
    /// int damage = rng.roll(dmg);
    /// @endcode
    int32_t roll(std::string_view expr);

    // -----------------------------------------------------------------------
    // Weighted selection
    // -----------------------------------------------------------------------

    /// @brief Return an index from [0, weights.size()) chosen proportional to weights.
    ///
    /// Weights do not need to be normalised — any non-negative values work and
    /// are treated as relative probabilities. An entry with weight 4.0 is twice
    /// as likely to be chosen as one with weight 2.0.
    ///
    /// If all weights are zero, the selection falls back to a uniform random
    /// index (no bias toward any entry).
    ///
    /// @param weights  Non-negative weight for each option. Must not be empty.
    /// @return The selected index.
    /// @throws std::invalid_argument if @p weights is empty or contains a negative value.
    ///
    /// @code
    /// // Weighted loot table: common items appear more often.
    /// std::vector<float> weights = {
    ///     60.0f,  // Gold coin   — 60% of total weight
    ///     25.0f,  // Health pot  — 25%
    ///     10.0f,  // Magic scroll— 10%
    ///      5.0f,  // Rare sword  —  5%
    /// };
    /// size_t pick = rng.weighted_index(weights);
    /// // pick == 0 about 60% of the time, etc.
    /// @endcode
    size_t weighted_index(std::span<const float> weights);

    /// @brief Vector overload for weighted_index.
    size_t weighted_index(const std::vector<float>& weights) {
        return weighted_index(std::span<const float>(weights));
    }

    /// @brief Select a value from a table using parallel weight and value collections.
    ///
    /// A convenience wrapper around weighted_index that returns the
    /// corresponding value directly instead of an index.
    ///
    /// @param weights  Non-negative weight for each entry.
    /// @param values   Values to pick from; must be the same length as @p weights.
    /// @return The selected value (copied).
    /// @throws std::invalid_argument if sizes differ, weights is empty, or a weight is negative.
    ///
    /// @code
    /// // Monster encounter table for dungeon level 3.
    /// std::vector<float>       weights = { 4.0f, 3.0f, 2.0f, 1.0f };
    /// std::vector<std::string> names   = { "Goblin", "Orc", "Troll", "Dragon" };
    ///
    /// std::string encounter = rng.weighted_choice(weights, names);
    /// spawn_monster(encounter);
    ///
    /// // Tile distribution for a cave generator.
    /// std::vector<float>  tile_weights = { 70.0f, 20.0f, 10.0f };
    /// std::vector<Tile>   tile_types   = { Tile::Floor, Tile::Rubble, Tile::Water };
    ///
    /// Tile t = rng.weighted_choice(tile_weights, tile_types);
    /// @endcode
    template<typename T>
    T weighted_choice(std::span<const float> weights, std::span<const T> values) {
        if (weights.size() != values.size())
            throw std::invalid_argument("weights and values must have the same length");
        return values[weighted_index(weights)];
    }

    /// @brief Vector overload for weighted_choice.
    template<typename T>
    T weighted_choice(const std::vector<float>& weights, const std::vector<T>& values) {
        return weighted_choice(std::span<const float>(weights), std::span<const T>(values));
    }

    // -----------------------------------------------------------------------
    // Shuffle
    // -----------------------------------------------------------------------

    /// @brief Shuffle @p items in place using Fisher-Yates.
    ///
    /// Produces an unbiased random permutation in O(n) time. Every possible
    /// ordering has exactly equal probability.
    ///
    /// @code
    /// // Randomise the draw order of a deck of cards.
    /// std::vector<Card> deck = make_full_deck();
    /// rng.shuffle(deck);
    ///
    /// // Distribute random loot to players in a fair order.
    /// std::vector<Item> loot = generate_loot();
    /// rng.shuffle(loot);
    /// for (int i = 0; i < num_players; ++i)
    ///     players[i].give(loot[i]);
    ///
    /// // Randomise a spawn list without repeats.
    /// std::vector<IVec2> spawn_points = get_spawn_points();
    /// rng.shuffle(spawn_points);
    /// for (int i = 0; i < num_enemies; ++i)
    ///     spawn_enemy(spawn_points[i]);
    /// @endcode
    template<typename T>
    void shuffle(std::span<T> items) noexcept {
        for (size_t i = items.size(); i > 1; --i) {
            size_t j = static_cast<size_t>(range(0, static_cast<int32_t>(i - 1)));
            std::swap(items[i - 1], items[j]);
        }
    }

    /// @brief Vector overload for shuffle.
    template<typename T>
    void shuffle(std::vector<T>& items) noexcept {
        shuffle(std::span<T>(items));
    }

    // -----------------------------------------------------------------------
    // Convenience helpers
    // -----------------------------------------------------------------------

    /// @brief Return `true` with probability 0.5 (fair coin flip).
    ///
    /// @code
    /// // Randomly choose which side of a corridor a door appears on.
    /// bool left_side = rng.coin_flip();
    ///
    /// // Randomly mirror a prefab room horizontally.
    /// if (rng.coin_flip()) room.flip_horizontal();
    /// @endcode
    bool coin_flip() noexcept { return (next_raw() & 1u) != 0u; }

    /// @brief Return `true` with probability 1/@p n.
    ///
    /// @pre n >= 1
    ///
    /// @code
    /// // 1-in-20 chance of a critical hit.
    /// if (rng.one_in(20)) { trigger_critical_hit(); }
    ///
    /// // 1-in-100 chance of a legendary item drop.
    /// if (rng.one_in(100)) { drop_legendary(enemy_pos); }
    ///
    /// // Each step has a 1-in-6 chance of making a noise.
    /// if (rng.one_in(6)) { alert_nearby_monsters(player_pos); }
    /// @endcode
    bool one_in(int32_t n) noexcept { return range(1, n) == 1; }

    /// @brief Return `true` with probability @p probability.
    ///
    /// @param probability Clamped to [0, 1]. 0.0 = never, 1.0 = always.
    ///
    /// @code
    /// // 15% chance of a bonus item when clearing a room.
    /// if (rng.chance(0.15f)) { place_bonus_item(room_centre); }
    ///
    /// // 80% chance a merchant is open for business.
    /// if (rng.chance(0.80f)) { open_shop(); }
    ///
    /// // Increasing hit chance based on attacker level.
    /// float hit_chance = 0.5f + attacker.level * 0.05f;
    /// if (rng.chance(hit_chance)) { land_hit(attacker, defender); }
    /// @endcode
    bool chance(float probability) noexcept { return next_float() < probability; }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 1;

    // PCG32 output function: one LCG step + permuted output.
    uint32_t next_raw() noexcept {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xsh = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xsh >> rot) | (xsh << ((-rot) & 31u));
    }
};

} // namespace xebble

// ---------------------------------------------------------------------------
// Serialization opt-in for RngState
// ---------------------------------------------------------------------------

/// @brief Opt `RngState` into World resource serialization.
///
/// Specializing `ResourceName` here lets the RNG state be saved and restored
/// via `add_serializable_resource<RngState>()` / `World::snapshot()`.
template<>
struct xebble::ResourceName<xebble::RngState> {
    static constexpr std::string_view value = "xebble::RngState";
};
