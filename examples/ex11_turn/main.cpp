/// @file main.cpp  (ex11_turn)
/// @brief Turn scheduling: AlternatingScheduler, EnergyScheduler, InitiativeScheduler.
///
/// Demonstrates:
///   - `AlternatingScheduler` — classic player-then-monsters cycle
///   - `EnergyScheduler` — speed-based variable-rate actor scheduling
///   - `InitiativeScheduler` — D&D-style initiative-order combat
///   - Switching between schedulers at runtime
///   - Scheduling actors by ECS entity ID

#include <xebble/xebble.hpp>

#include <format>
#include <string>
#include <vector>

namespace {

enum class Mode { Alternating, Energy, Initiative };

struct Actor {
    std::string name;
    int speed;
    int actions_taken = 0;
};

struct TurnState {
    Mode mode = Mode::Alternating;
    std::vector<Actor> actors;

    xebble::AlternatingScheduler alt_sched;
    xebble::EnergyScheduler energy_sched;
    xebble::InitiativeScheduler init_sched;

    int tick_count = 0;
    float sim_time = 0.0f;
    bool auto_run = false;
    float auto_timer = 0.0f;

    // Log of last 8 actions.
    std::vector<std::string> log;
    void push_log(std::string s) {
        log.push_back(std::move(s));
        if (log.size() > 8)
            log.erase(log.begin());
    }
};

class TurnSystem : public xebble::System {
public:
    void init(xebble::World& world) override {
        TurnState ts;
        ts.actors = {
            {"Player", 10, 0},
            {"Goblin", 7, 0},
            {"Orc", 5, 0},
            {"Troll", 3, 0},
        };

        // Energy scheduler: use index as ID.
        ts.energy_sched = xebble::EnergyScheduler(100);
        for (size_t i = 0; i < ts.actors.size(); ++i)
            ts.energy_sched.add_actor(static_cast<uint64_t>(i), ts.actors[i].speed);

        // Initiative scheduler.
        xebble::Rng rng(42);
        for (size_t i = 0; i < ts.actors.size(); ++i)
            ts.init_sched.push(static_cast<uint64_t>(i), rng.roll("1d20"));

        world.add_resource(std::move(ts));
    }

    void update(xebble::World& world, float dt) override {
        auto& ts = world.resource<TurnState>();

        for (const auto& ev : world.resource<xebble::EventQueue>().events) {
            if (ev.type != xebble::EventType::KeyPress)
                continue;
            auto k = ev.key().key;
            if (k == xebble::Key::Escape)
                std::exit(0);
            if (k == xebble::Key::Space)
                step(ts);
            if (k == xebble::Key::A) {
                ts.mode = Mode::Alternating;
                reset(ts);
            }
            if (k == xebble::Key::E) {
                ts.mode = Mode::Energy;
                reset(ts);
            }
            if (k == xebble::Key::I) {
                ts.mode = Mode::Initiative;
                reset(ts);
            }
            if (k == xebble::Key::R) {
                ts.auto_run = !ts.auto_run;
            }
        }

        if (ts.auto_run) {
            ts.auto_timer += dt;
            if (ts.auto_timer >= 0.25f) {
                ts.auto_timer = 0.0f;
                step(ts);
            }
        }
    }

    void draw(xebble::World& world, xebble::Renderer& renderer) override {
        auto& ts = world.resource<TurnState>();
        auto& ui = world.resource<xebble::UIContext>();

        const char* mode_str = [&] {
            switch (ts.mode) {
            case Mode::Alternating:
                return "Alternating";
            case Mode::Energy:
                return "Energy";
            case Mode::Initiative:
                return "Initiative";
            }
            return "?";
        }();

        ui.panel("turn_main", {.anchor = xebble::Anchor::Center, .size = {540, 260}}, [&](auto& p) {
            {
                auto s = std::format("ex11 \u2014 Turn Scheduling  Mode: {:s}  Tick: {:d}",
                                     mode_str, ts.tick_count);
                p.text(std::u8string(s.begin(), s.end()), {.color = {220, 220, 100}});
            }
            p.text(u8"[A]lternating  [E]nergy  [I]nitiative  "
                   u8"[Space] step  [R] auto-run  [Esc] quit",
                   {.color = {160, 160, 200}});

            // Actor table.
            for (size_t i = 0; i < ts.actors.size(); ++i) {
                const auto& a = ts.actors[i];
                std::string extra;
                if (ts.mode == Mode::Energy) {
                    auto& actors = ts.energy_sched.actors();
                    if (i < actors.size())
                        extra = std::format(" [nrg:{:3d}]", actors[i].energy);
                }
                if (ts.mode == Mode::Initiative) {
                    for (auto& e : ts.init_sched.entries())
                        if (e.id == i)
                            extra = std::format(" [init:{:2d}]", e.initiative);
                }
                {
                    auto s = std::format("{:6s} spd={:2d}{:s} acts={:3d}", a.name, a.speed, extra,
                                         a.actions_taken);
                    p.text(std::u8string(s.begin(), s.end()), {.color = {180, 220, 180}});
                }
            }

            // Recent log.
            p.text(u8"--- Recent actions ---", {.color = {140, 140, 160}});
            for (const auto& line : ts.log)
                p.text(std::u8string(line.begin(), line.end()), {.color = {200, 200, 200}});
        });
        xebble::debug_overlay(world, renderer);
    }

private:
    void reset(TurnState& ts) {
        for (auto& a : ts.actors)
            a.actions_taken = 0;
        ts.tick_count = 0;
        ts.log.clear();

        if (ts.mode == Mode::Energy) {
            ts.energy_sched = xebble::EnergyScheduler(100);
            for (size_t i = 0; i < ts.actors.size(); ++i)
                ts.energy_sched.add_actor(static_cast<uint64_t>(i), ts.actors[i].speed);
        }
        if (ts.mode == Mode::Initiative) {
            ts.init_sched = xebble::InitiativeScheduler{};
            xebble::Rng rng(static_cast<uint64_t>(ts.tick_count + 1));
            for (size_t i = 0; i < ts.actors.size(); ++i)
                ts.init_sched.push(static_cast<uint64_t>(i), rng.roll("1d20"));
        }
    }

    void step(TurnState& ts) {
        ++ts.tick_count;

        switch (ts.mode) {
        case Mode::Alternating: {
            auto& s = ts.alt_sched;
            if (s.is_player_turn()) {
                ts.actors[0].actions_taken++;
                ts.push_log(std::format("Turn {:d}: Player acts (round {:d})", ts.tick_count,
                                        (int)s.turn()));
                s.end_player_turn();
            } else {
                for (size_t i = 1; i < ts.actors.size(); ++i) {
                    ts.actors[i].actions_taken++;
                    ts.push_log(
                        std::format("Turn {:d}: {:s} acts", ts.tick_count, ts.actors[i].name));
                }
                s.end_monster_turn();
            }
            break;
        }
        case Mode::Energy: {
            auto& s = ts.energy_sched;
            s.tick();
            while (s.has_ready()) {
                uint64_t id = s.next_actor();
                ts.actors[id].actions_taken++;
                ts.push_log(std::format("Turn {:d}: {:s} acts", ts.tick_count, ts.actors[id].name));
                s.end_turn(id);
            }
            break;
        }
        case Mode::Initiative: {
            auto& s = ts.init_sched;
            if (!s.empty()) {
                uint64_t id = s.current();
                ts.actors[id].actions_taken++;
                ts.push_log(std::format("Turn {:d}: {:s} acts (init {:d})", ts.tick_count,
                                        ts.actors[id].name, s.current_initiative()));
                s.advance();
            }
            break;
        }
        }
    }
};

} // namespace

int main() {
    xebble::World world;
    world.add_system<TurnSystem>();

    return xebble::run(
        std::move(world),
        {
            .window = {.title = "ex11 — Turn Scheduling", .width = 1280, .height = 720},
            .renderer = {.virtual_width = 640, .virtual_height = 360},
        });
}
