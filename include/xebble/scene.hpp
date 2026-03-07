/// @file scene.hpp
/// @brief Stack-based scene manager for Xebble games.
///
/// A **scene** is an isolated game state — title screen, dungeon level, pause
/// menu, inventory screen — each backed by its own `World` with its own
/// entities, components, systems, and resources.  `SceneRouter` manages a
/// stack of live scenes and processes **transition commands** that systems post
/// via the `SceneTransition` resource.
///
/// ## Concepts
///
/// | Term | Meaning |
/// |---|---|
/// | **Scene factory** | `std::function<World(std::any payload)>` — called once when a scene is pushed or replaced |
/// | **Scene name** | A `std::string` key used to look up the factory |
/// | **Transition command** | `SceneTransition` resource — systems request push/pop/replace |
/// | **Payload** | `std::any` value forwarded to the factory of the incoming scene |
/// | **draw_below** | Whether the scene underneath the stack top keeps rendering |
///
/// ## Quick-start
///
/// ### 1. Define scene factories
///
/// @code
/// // gameplay.cpp
/// World make_gameplay(std::any payload) {
///     int floor = payload.has_value()
///         ? std::any_cast<int>(payload) : 1;
///
///     World world;
///     world.register_component<Position>();
///     world.register_component<Sprite>();
///     world.register_component<Monster>();
///     world.add_resource(GameState{ .floor = floor });
///     world.add_system<DungeonSystem>();
///     world.add_system<PlayerSystem>();
///     world.add_system<MonsterAISystem>();
///     return world;
/// }
///
/// // pause.cpp
/// World make_pause(std::any /*payload*/) {
///     World world;
///     world.add_system<PauseMenuSystem>();
///     return world;
/// }
/// @endcode
///
/// ### 2. Build a SceneRouter and call run()
///
/// @code
/// int main() {
///     SceneRouter router;
///     router.add_scene("gameplay", make_gameplay);
///     router.add_scene("pause",    make_pause);
///     router.set_initial("gameplay");
///
///     GameConfig cfg{ /* window, renderer, assets … */ };
///     return xebble::run(std::move(router), cfg);
/// }
/// @endcode
///
/// ### 3. Request transitions from systems
///
/// @code
/// // In PauseMenuSystem::update():
/// void PauseMenuSystem::update(World& world, float /*dt*/) {
///     auto& evt = world.resource<EventQueue>();
///     for (auto& e : evt.events) {
///         if (auto* key = std::get_if<KeyEvent>(&e)) {
///             if (key->key == Key::Escape && key->action == Action::Press)
///                 // Pop the pause menu — return to gameplay.
///                 world.resource<SceneTransition>() = SceneTransition::pop();
///         }
///     }
/// }
///
/// // In PlayerSystem::update() — open pause with Escape:
/// void PlayerSystem::update(World& world, float /*dt*/) {
///     for (auto& e : world.resource<EventQueue>().events) {
///         if (auto* key = std::get_if<KeyEvent>(&e)) {
///             if (key->key == Key::Escape && key->action == Action::Press)
///                 // Push the pause menu on top; gameplay keeps drawing below.
///                 world.resource<SceneTransition>() =
///                     SceneTransition::push("pause", {}, DrawBelow::Yes);
///         }
///     }
///     // Descend to the next dungeon floor:
///     if (player_reached_stairs) {
///         int next_floor = world.resource<GameState>().floor + 1;
///         world.resource<SceneTransition>() =
///             SceneTransition::replace("gameplay", next_floor);
///     }
/// }
/// @endcode
///
/// ## Transition semantics
///
/// | Command | Stack effect |
/// |---|---|
/// | `push(name, payload, draw_below)` | New scene placed on top; old scene paused (update halted) |
/// | `pop(payload)` | Top scene destroyed; previous scene resumes and receives payload |
/// | `replace(name, payload)` | Top scene destroyed; new scene pushed in its place |
/// | `pop_to(name, payload)` | Scenes popped until the named scene is on top; it receives payload |
/// | `pop_all_and_push(name, payload)` | Entire stack cleared; fresh scene pushed |
///
/// Only one transition fires per tick; the command is cleared after processing.
/// If the stack becomes empty the game loop exits cleanly.
///
/// ## Pop payloads
///
/// When a scene is popped, the payload is forwarded to the **resuming** scene
/// below via a call to `on_resume(payload)` on each of its systems.
///
/// @code
/// // System in the gameplay scene that reacts when we pop back from inventory:
/// class GameplaySystem : public System {
/// public:
///     void on_resume(World& world, std::any payload) override {
///         if (payload.has_value()) {
///             auto item = std::any_cast<SelectedItem>(payload);
///             equip_item(world, item);
///         }
///     }
/// };
/// @endcode
#pragma once

#include <xebble/world.hpp>

#include <any>
#include <cassert>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// DrawBelow — overlay drawing flag
// ---------------------------------------------------------------------------

/// @brief Controls whether the scene below the stack top continues drawing.
///
/// Pass `DrawBelow::Yes` when pushing an overlay (pause menu, inventory) that
/// should be rendered on top of the still-visible game world.
/// Pass `DrawBelow::No` (the default) for full scene replacements where the
/// scene below should not waste GPU time rendering.
///
/// @code
/// // Pause menu rendered over the game world.
/// SceneTransition::push("pause", {}, DrawBelow::Yes);
///
/// // Full opaque cutscene — suppress the game world draw.
/// SceneTransition::push("cutscene", {}, DrawBelow::No);
/// @endcode
enum class DrawBelow {
    No  = 0, ///< Only the top scene draws (default).
    Yes = 1, ///< The scene below the top also draws.
};

// ---------------------------------------------------------------------------
// SceneTransition — command posted by systems to request a scene change
// ---------------------------------------------------------------------------

/// @brief A scene-transition command written to the `SceneTransition` resource.
///
/// Systems request transitions by overwriting the `SceneTransition` resource
/// in their World.  `run()` reads it between ticks and applies it once.
///
/// Use the static factory methods — `push()`, `pop()`, `replace()`,
/// `pop_to()`, `pop_all_and_push()` — rather than constructing directly.
///
/// @code
/// // From inside any system:
/// auto& tr = world.resource<SceneTransition>();
///
/// tr = SceneTransition::push("inventory", {}, DrawBelow::Yes);
/// tr = SceneTransition::pop();
/// tr = SceneTransition::replace("game_over");
/// @endcode
struct SceneTransition {
    /// @brief Transition kind.
    enum class Kind {
        None,            ///< No pending transition (default, no-op).
        Push,            ///< Push a new scene onto the stack.
        Pop,             ///< Pop the current scene; resume the one below.
        Replace,         ///< Destroy current scene; push a new one.
        PopTo,           ///< Pop until the named scene is on top.
        PopAllAndPush,   ///< Clear the stack; push a fresh scene.
    };

    Kind        kind        = Kind::None; ///< Which transition to perform.
    std::string scene_name;               ///< Target scene name (unused for Pop).
    std::any    payload;                  ///< Forwarded to the incoming/resuming scene.
    DrawBelow   draw_below  = DrawBelow::No; ///< Only meaningful for Push.

    // -----------------------------------------------------------------------
    // Factory helpers
    // -----------------------------------------------------------------------

    /// @brief No pending transition (default state).
    static SceneTransition none() { return {}; }

    /// @brief Push a new scene on top of the current one.
    ///
    /// The current scene's systems stop receiving `update()` until this scene
    /// is popped.  If `draw_below == DrawBelow::Yes`, the scene below
    /// continues to have its `draw()` called each frame.
    ///
    /// @param name        Registered scene name to push.
    /// @param payload     Forwarded to the new scene's factory.
    /// @param draw_below  Whether the underlying scene keeps drawing.
    ///
    /// @code
    /// // Open a pause menu overlay.
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::push("pause", {}, DrawBelow::Yes);
    ///
    /// // Open inventory passing the player entity.
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::push("inventory", player_entity, DrawBelow::Yes);
    /// @endcode
    static SceneTransition push(std::string name,
                                std::any payload   = {},
                                DrawBelow draw_below = DrawBelow::No) {
        SceneTransition t;
        t.kind        = Kind::Push;
        t.scene_name  = std::move(name);
        t.payload     = std::move(payload);
        t.draw_below  = draw_below;
        return t;
    }

    /// @brief Pop the current scene and resume the one below.
    ///
    /// The payload is forwarded to `System::on_resume()` of the resuming
    /// scene's systems.
    ///
    /// @code
    /// // Close the inventory; tell the game world what item was selected.
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::pop(selected_item);
    ///
    /// // Close the pause menu with no payload.
    /// world.resource<SceneTransition>() = SceneTransition::pop();
    /// @endcode
    static SceneTransition pop(std::any payload = {}) {
        SceneTransition t;
        t.kind    = Kind::Pop;
        t.payload = std::move(payload);
        return t;
    }

    /// @brief Destroy the current scene and push a new one in its place.
    ///
    /// @code
    /// // Transition to the next dungeon floor.
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::replace("gameplay", next_floor_number);
    ///
    /// // Game over — go to the high-score screen.
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::replace("game_over");
    /// @endcode
    static SceneTransition replace(std::string name, std::any payload = {}) {
        SceneTransition t;
        t.kind       = Kind::Replace;
        t.scene_name = std::move(name);
        t.payload    = std::move(payload);
        return t;
    }

    /// @brief Pop scenes until the named scene is on top, then resume it.
    ///
    /// Useful for deep stack navigation — e.g. going directly back to the
    /// title screen from several levels deep.
    ///
    /// @code
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::pop_to("title");
    /// @endcode
    static SceneTransition pop_to(std::string name, std::any payload = {}) {
        SceneTransition t;
        t.kind       = Kind::PopTo;
        t.scene_name = std::move(name);
        t.payload    = std::move(payload);
        return t;
    }

    /// @brief Clear the entire stack and push a fresh scene.
    ///
    /// Useful for hard resets — e.g. "New Game" from the main menu.
    ///
    /// @code
    /// world.resource<SceneTransition>() =
    ///     SceneTransition::pop_all_and_push("gameplay", chosen_seed);
    /// @endcode
    static SceneTransition pop_all_and_push(std::string name, std::any payload = {}) {
        SceneTransition t;
        t.kind       = Kind::PopAllAndPush;
        t.scene_name = std::move(name);
        t.payload    = std::move(payload);
        return t;
    }

    /// @brief True if this is not the default no-op state.
    bool pending() const { return kind != Kind::None; }
};

// ---------------------------------------------------------------------------
// SceneRouter
// ---------------------------------------------------------------------------

/// @brief Registry of named scene factories and initial scene configuration.
///
/// `SceneRouter` holds a map of scene names to factory functions. Pass it to
/// `xebble::run(SceneRouter, GameConfig)` instead of a bare `World` to enable
/// multi-scene navigation.
///
/// A factory is a callable `World(std::any payload)` — it receives the payload
/// from the transition that triggered the scene and returns a fully configured
/// `World` (components registered, resources added, systems added).  The
/// engine will call `init_systems()` and inject engine resources (Renderer,
/// AssetManager, EventQueue, Camera, UI) automatically before the scene starts
/// ticking.
///
/// @code
/// SceneRouter router;
///
/// router.add_scene("title",    make_title_scene);
/// router.add_scene("gameplay", make_gameplay_scene);
/// router.add_scene("pause",    make_pause_scene);
/// router.add_scene("game_over",make_game_over_scene);
///
/// router.set_initial("title");
///
/// return xebble::run(std::move(router), cfg);
/// @endcode
class SceneRouter {
public:
    /// @brief Type of a scene factory function.
    using Factory = std::function<World(std::any payload)>;

    /// @brief Register a named scene factory.
    ///
    /// @param name     Unique scene identifier used in transition commands.
    /// @param factory  Callable `World(std::any)` producing the scene World.
    ///
    /// @code
    /// router.add_scene("gameplay", [](std::any payload) {
    ///     int floor = payload.has_value() ? std::any_cast<int>(payload) : 1;
    ///     World world;
    ///     world.register_component<Position>();
    ///     world.add_resource(FloorNumber{floor});
    ///     world.add_system<GameplaySystem>();
    ///     return world;
    /// });
    /// @endcode
    void add_scene(std::string name, Factory factory) {
        factories_[std::move(name)] = std::move(factory);
    }

    /// @brief Set the scene that is pushed when `run()` starts.
    ///
    /// @param name     Must match a name registered with `add_scene()`.
    /// @param payload  Forwarded to the initial scene's factory.
    ///
    /// @code
    /// router.set_initial("title");
    ///
    /// // Or with a payload (e.g. a loaded save file):
    /// router.set_initial("gameplay", loaded_save_data);
    /// @endcode
    void set_initial(std::string name, std::any payload = {}) {
        initial_name_    = std::move(name);
        initial_payload_ = std::move(payload);
    }

    /// @brief Build the initial World by invoking the initial scene's factory.
    ///
    /// Called internally by `run()`. Asserts if `set_initial()` was not called
    /// or the named scene does not exist.
    World build_initial() const {
        assert(!initial_name_.empty() && "SceneRouter: set_initial() not called");
        return build(initial_name_, initial_payload_);
    }

    /// @brief Build a World for the named scene with the given payload.
    ///
    /// Called internally by the scene stack when processing transitions.
    /// Asserts if the scene name is not registered.
    World build(const std::string& name, const std::any& payload = {}) const {
        auto it = factories_.find(name);
        assert(it != factories_.end() && "SceneRouter: unknown scene name");
        return it->second(payload);
    }

    /// @brief Return true if a scene with @p name has been registered.
    bool has_scene(const std::string& name) const {
        return factories_.contains(name);
    }

    /// @brief Return the initial scene name (empty if not set).
    const std::string& initial_name() const { return initial_name_; }

private:
    std::unordered_map<std::string, Factory> factories_;
    std::string initial_name_;
    std::any    initial_payload_;
};

// ---------------------------------------------------------------------------
// SceneStack — internal stack; also used by run()
// ---------------------------------------------------------------------------

/// @brief Live stack of scenes managed during a `run(SceneRouter, …)` session.
///
/// Normally you do not need to interact with `SceneStack` directly — it is
/// created and managed by `run()`.  It is exposed in this header for advanced
/// use cases (e.g. embedding Xebble in a larger application that manages its
/// own window loop).
///
/// Each entry on the stack stores:
///   - The live `World` for the scene.
///   - The **scene name** (for `pop_to` navigation).
///   - The **draw_below** flag (controls whether the entry below keeps drawing).
class SceneStack {
public:
    explicit SceneStack(const SceneRouter& router) : router_(router) {}

    /// @brief Push the initial scene onto the stack.
    ///
    /// Pushes the scene named by `SceneRouter::initial_name()`.  Must be
    /// called exactly once before the first `tick()`.
    ///
    /// @param injector  Callable `void(World&)` that injects engine resources.
    void push_initial(const std::function<void(World&)>& injector) {
        push_scene(router_.initial_name(), router_.build_initial(), injector);
    }

    /// @brief Return true if there are no scenes on the stack.
    bool empty() const { return frames_.empty(); }

    /// @brief Return a reference to the top-scene's World.
    World& top_world() {
        assert(!frames_.empty());
        return frames_.back().world;
    }

    /// @brief Process a pending `SceneTransition` from the top scene's World.
    ///
    /// Called by `run()` after each fixed-update tick.  If the top World has
    /// a `SceneTransition` resource with `pending() == true`, this method
    /// applies it and clears the command.
    ///
    /// @param injector  Callable `void(World&)` that injects engine resources
    ///                  into newly created Worlds.
    void apply_transition(const std::function<void(World&)>& injector) {
        if (frames_.empty()) return;

        auto& top = frames_.back().world;
        if (!top.has_resource<SceneTransition>()) return;

        SceneTransition& tr = top.resource<SceneTransition>();
        if (!tr.pending()) return;

        // Capture the transition then immediately clear it so the old World
        // is in a clean state before we potentially destroy it.
        SceneTransition cmd = std::move(tr);
        tr = SceneTransition::none();

        switch (cmd.kind) {
            case SceneTransition::Kind::Push: {
                World new_world = router_.build(cmd.scene_name, cmd.payload);
                frames_.back().draw_below = (cmd.draw_below == DrawBelow::Yes);
                push_scene(cmd.scene_name, std::move(new_world), injector);
                break;
            }
            case SceneTransition::Kind::Pop: {
                frames_.pop_back();
                if (!frames_.empty()) {
                    notify_resume(frames_.back().world, std::move(cmd.payload));
                }
                break;
            }
            case SceneTransition::Kind::Replace: {
                frames_.pop_back();
                World new_world = router_.build(cmd.scene_name, cmd.payload);
                push_scene(cmd.scene_name, std::move(new_world), injector);
                break;
            }
            case SceneTransition::Kind::PopTo: {
                while (frames_.size() > 1 && frames_.back().name != cmd.scene_name)
                    frames_.pop_back();
                if (!frames_.empty())
                    notify_resume(frames_.back().world, std::move(cmd.payload));
                break;
            }
            case SceneTransition::Kind::PopAllAndPush: {
                frames_.clear();
                World new_world = router_.build(cmd.scene_name, cmd.payload);
                push_scene(cmd.scene_name, std::move(new_world), injector);
                break;
            }
            case SceneTransition::Kind::None:
                break;
        }
    }

    /// @brief Run one fixed-timestep update on the top scene.
    void tick_update(float dt) {
        if (!frames_.empty())
            frames_.back().world.tick_update(dt);
    }

    /// @brief Run one draw pass.
    ///
    /// If the top scene's `draw_below` flag is set, also calls `tick_draw` on
    /// the scene underneath.
    void tick_draw(Renderer& renderer) {
        if (frames_.empty()) return;

        // Find the lowest frame that still needs to draw.
        size_t start = frames_.size() - 1;
        while (start > 0 && frames_[start - 1].draw_below)
            --start;

        for (size_t i = start; i < frames_.size(); ++i)
            frames_[i].world.tick_draw(renderer);
    }

private:
    struct Frame {
        std::string name;
        World       world;
        bool        draw_below = false; ///< True if the frame BELOW this one keeps drawing.
    };

    void push_scene(const std::string& name, World world,
                    const std::function<void(World&)>& injector) {
        injector(world);
        world.init_systems();
        frames_.push_back(Frame{ name, std::move(world), false });
    }

    static void notify_resume(World& world, std::any payload) {
        // Systems opt in to resume notifications by overriding on_resume().
        // We call it via the World's tick hook by injecting a one-shot
        // resource that UIInputSystem / user systems can observe.
        // For simplicity we store the payload as a resource; systems read it
        // and remove it in their next update().
        world.add_resource<std::any>(std::move(payload));
    }

    const SceneRouter& router_;
    std::vector<Frame> frames_;
};

} // namespace xebble
