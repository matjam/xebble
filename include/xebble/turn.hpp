/// @file turn.hpp
/// @brief Turn scheduling for roguelikes — alternating, energy, and initiative models.
///
/// A **turn scheduler** decouples the question "who acts next?" from the ECS.
/// Systems query the scheduler each tick to determine which actors should
/// perform their action, then call `end_turn()` to advance the queue.
///
/// Three models are provided as value types — pick whichever matches your
/// game's needs, or compose them:
///
/// | Type | Model |
/// |---|---|
/// | `AlternatingScheduler` | Player → all monsters → player → … |
/// | `EnergyScheduler` | Actors accumulate energy each tick; act when threshold reached |
/// | `InitiativeScheduler` | Sorted priority queue by speed; fastest actor acts first |
///
/// All schedulers are **value types** with no ECS dependency.  Store them as
/// World resources and access from any system.
///
/// ## AlternatingScheduler — quick-start
///
/// @code
/// world.add_resource(AlternatingScheduler{});
///
/// // In PlayerSystem::update():
/// auto& sched = world.resource<AlternatingScheduler>();
/// if (sched.is_player_turn()) {
///     if (player_acted()) sched.end_player_turn();
/// }
///
/// // In MonsterAISystem::update():
/// auto& sched = world.resource<AlternatingScheduler>();
/// if (sched.is_monster_turn()) {
///     run_all_monster_ai(world);
///     sched.end_monster_turn();
/// }
/// @endcode
///
/// ## EnergyScheduler — quick-start
///
/// @code
/// EnergyScheduler sched;
/// sched.add_actor(player_entity, /*speed=*/10);
/// sched.add_actor(goblin_entity, /*speed=*/7);
/// sched.add_actor(troll_entity,  /*speed=*/4);
/// world.add_resource(std::move(sched));
///
/// // In your update system — each frame:
/// auto& sched = world.resource<EnergyScheduler>();
/// sched.tick();   // award energy to all actors
/// while (sched.has_ready()) {
///     uint64_t actor = sched.next_actor();
///     perform_ai_action(world, actor);
///     sched.end_turn(actor);
/// }
/// @endcode
///
/// ## InitiativeScheduler — quick-start
///
/// @code
/// InitiativeScheduler sched;
/// sched.push(player_entity, /*initiative=*/18);
/// sched.push(goblin1,       /*initiative=*/12);
/// sched.push(goblin2,       /*initiative=*/ 9);
/// world.add_resource(std::move(sched));
///
/// // In your update system:
/// auto& sched = world.resource<InitiativeScheduler>();
/// if (!sched.empty()) {
///     uint64_t actor = sched.current();
///     act(world, actor);
///     sched.advance();   // move to the next actor in initiative order
/// }
/// @endcode
#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// AlternatingScheduler
// ---------------------------------------------------------------------------

/// @brief Simple alternating turn scheduler: player phase → monster phase → repeat.
///
/// Models the most common roguelike turn structure where the player acts once,
/// then all monsters act, then the player again.
///
/// @code
/// AlternatingScheduler sched;
///
/// // Player system
/// if (sched.is_player_turn() && player_acted())
///     sched.end_player_turn();
///
/// // Monster AI system
/// if (sched.is_monster_turn()) {
///     for_each_monster([](Monster& m){ m.act(); });
///     sched.end_monster_turn();
/// }
/// @endcode
class AlternatingScheduler {
public:
    /// @brief Phase of the alternating schedule.
    enum class Phase { Player, Monsters };

    /// @brief Return the current phase.
    Phase phase() const { return phase_; }

    /// @brief True during the player's action window.
    bool is_player_turn()  const { return phase_ == Phase::Player; }

    /// @brief True during the monsters' action window.
    bool is_monster_turn() const { return phase_ == Phase::Monsters; }

    /// @brief End the player phase; advance to the monster phase.
    ///
    /// Call this after the player has successfully performed an action.
    ///
    /// @code
    /// if (sched.is_player_turn() && player_acted())
    ///     sched.end_player_turn();
    /// @endcode
    void end_player_turn() {
        assert(phase_ == Phase::Player);
        phase_ = Phase::Monsters;
        ++turn_;
    }

    /// @brief End the monster phase; advance to the next player turn.
    ///
    /// Call this after all monsters have acted.
    ///
    /// @code
    /// if (sched.is_monster_turn()) {
    ///     run_monster_ai(world);
    ///     sched.end_monster_turn();
    /// }
    /// @endcode
    void end_monster_turn() {
        assert(phase_ == Phase::Monsters);
        phase_ = Phase::Player;
    }

    /// @brief Total number of full rounds completed (incremented after player turn ends).
    uint64_t turn() const { return turn_; }

private:
    Phase    phase_ = Phase::Player;
    uint64_t turn_  = 0;
};

// ---------------------------------------------------------------------------
// EnergyScheduler
// ---------------------------------------------------------------------------

/// @brief Energy-based turn scheduler.
///
/// Each actor accumulates energy proportional to their speed each `tick()`.
/// When an actor's energy reaches or exceeds the threshold (default 100), they
/// are "ready" and appear in `next_actor()`. After acting, call `end_turn(id)`
/// to deduct the action cost from their energy.
///
/// This naturally handles slow/fast actors without any round structure:
/// a speed-10 actor acts twice as often as a speed-5 actor.
///
/// Actor IDs are arbitrary `uint64_t` values — use ECS entity IDs, indices,
/// or any other identifier.
///
/// @code
/// EnergyScheduler sched;
/// sched.set_threshold(100);
/// sched.add_actor(PLAYER_ID, 10);   // fast
/// sched.add_actor(GOBLIN_ID,  7);   // medium
/// sched.add_actor(TROLL_ID,   4);   // slow
///
/// // Each game tick:
/// sched.tick();
/// while (sched.has_ready()) {
///     uint64_t who = sched.next_actor();
///     do_action(world, who);
///     sched.end_turn(who);  // default action cost = threshold
/// }
/// @endcode
class EnergyScheduler {
public:
    struct Actor {
        uint64_t id;      ///< Arbitrary actor identifier.
        int      speed;   ///< Energy gained per tick.
        int      energy;  ///< Current energy.
    };

    explicit EnergyScheduler(int threshold = 100) : threshold_(threshold) {}

    /// @brief Set the energy threshold required to act.
    void set_threshold(int t) { threshold_ = t; }

    /// @brief Return the current energy threshold.
    int threshold() const { return threshold_; }

    /// @brief Register an actor with a given speed.
    ///
    /// @param id     Unique actor identifier.
    /// @param speed  Energy gained per `tick()` call.
    ///
    /// @code
    /// sched.add_actor(player_id, 10);
    /// sched.add_actor(goblin_id,  7);
    /// @endcode
    void add_actor(uint64_t id, int speed) {
        actors_.push_back(Actor{id, speed, 0});
    }

    /// @brief Remove an actor by ID. No-op if not found.
    ///
    /// @code
    /// sched.remove_actor(dead_monster_id);
    /// @endcode
    void remove_actor(uint64_t id) {
        actors_.erase(std::remove_if(actors_.begin(), actors_.end(),
            [id](const Actor& a){ return a.id == id; }), actors_.end());
    }

    /// @brief Award energy to all actors proportional to their speed.
    ///
    /// Call once per game tick (or fixed-step frame).
    void tick() {
        for (auto& a : actors_) a.energy += a.speed;
    }

    /// @brief True if at least one actor has reached the action threshold.
    bool has_ready() const {
        for (auto& a : actors_) if (a.energy >= threshold_) return true;
        return false;
    }

    /// @brief Return the ID of the highest-energy ready actor.
    ///
    /// @pre `has_ready()` must be true.
    uint64_t next_actor() const {
        const Actor* best = nullptr;
        for (auto& a : actors_)
            if (a.energy >= threshold_ && (!best || a.energy > best->energy))
                best = &a;
        assert(best && "next_actor() called with no ready actors");
        return best->id;
    }

    /// @brief Deduct one action's energy cost from @p id after they act.
    ///
    /// The cost defaults to the threshold (i.e. one full action). Pass a
    /// different value for variable-cost actions (e.g. a quick stab = 50,
    /// a heavy swing = 150).
    ///
    /// @code
    /// sched.end_turn(actor_id);               // standard cost
    /// sched.end_turn(actor_id, threshold/2);  // half-cost quick action
    /// @endcode
    void end_turn(uint64_t id, int cost = -1) {
        if (cost < 0) cost = threshold_;
        for (auto& a : actors_)
            if (a.id == id) { a.energy -= cost; return; }
    }

    /// @brief Return all actors (read-only).
    const std::vector<Actor>& actors() const { return actors_; }

    /// @brief Number of registered actors.
    size_t size() const { return actors_.size(); }

private:
    std::vector<Actor> actors_;
    int threshold_;
};

// ---------------------------------------------------------------------------
// InitiativeScheduler
// ---------------------------------------------------------------------------

/// @brief Initiative-based turn scheduler — sorted by a numeric initiative score.
///
/// Actors are sorted by their initiative value (highest first) and act in that
/// order each round. After all actors have acted, `advance()` wraps back to
/// the highest-initiative actor.
///
/// Useful for classic D&D-style initiative where every combatant rolls a
/// number at the start of combat and acts in descending order.
///
/// @code
/// InitiativeScheduler sched;
/// sched.push(player_id,  18);
/// sched.push(goblin_id1, 12);
/// sched.push(goblin_id2,  9);
/// sched.push(troll_id,    5);
///
/// while (!sched.empty()) {
///     uint64_t who = sched.current();
///     bool alive = do_combat_action(world, who);
///     if (!alive) sched.remove(who);
///     else        sched.advance();
/// }
/// @endcode
class InitiativeScheduler {
public:
    struct Entry {
        uint64_t id;          ///< Actor identifier.
        int      initiative;  ///< Sort key — higher acts first.
    };

    /// @brief Add an actor with their initiative score.
    ///
    /// The queue is automatically re-sorted after insertion.
    ///
    /// @code
    /// sched.push(player_id,  rng.roll("1d20") + dex_mod);
    /// sched.push(monster_id, rng.roll("1d20") + monster_dex);
    /// @endcode
    void push(uint64_t id, int initiative) {
        queue_.push_back({id, initiative});
        std::stable_sort(queue_.begin(), queue_.end(),
            [](const Entry& a, const Entry& b){ return a.initiative > b.initiative; });
        current_ = 0;
    }

    /// @brief Remove an actor by ID. Adjusts the current index if needed.
    ///
    /// @code
    /// // Remove a killed combatant mid-round.
    /// sched.remove(dead_entity_id);
    /// @endcode
    void remove(uint64_t id) {
        size_t before = queue_.size();
        size_t removed_before_current = 0;
        for (size_t i = 0; i < queue_.size() && i < current_; ++i)
            if (queue_[i].id == id) ++removed_before_current;

        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
            [id](const Entry& e){ return e.id == id; }), queue_.end());

        if (queue_.empty()) { current_ = 0; return; }
        if (removed_before_current > 0 && current_ > 0)
            current_ -= removed_before_current;
        if (current_ >= queue_.size()) current_ = 0;
    }

    /// @brief True if no actors are registered.
    bool empty() const { return queue_.empty(); }

    /// @brief Number of actors in the queue.
    size_t size() const { return queue_.size(); }

    /// @brief Return the ID of the actor whose turn it currently is.
    ///
    /// @pre `!empty()`
    uint64_t current() const {
        assert(!queue_.empty());
        return queue_[current_].id;
    }

    /// @brief Return the initiative of the current actor.
    int current_initiative() const {
        assert(!queue_.empty());
        return queue_[current_].initiative;
    }

    /// @brief Advance to the next actor in initiative order, wrapping around.
    ///
    /// @code
    /// sched.advance();  // next combatant's turn
    /// @endcode
    void advance() {
        if (queue_.empty()) return;
        current_ = (current_ + 1) % queue_.size();
    }

    /// @brief Index of the current actor within the sorted queue (0 = highest init).
    size_t current_index() const { return current_; }

    /// @brief Read-only view of all entries in initiative order.
    const std::vector<Entry>& entries() const { return queue_; }

private:
    std::vector<Entry> queue_;
    size_t current_ = 0;
};

} // namespace xebble
