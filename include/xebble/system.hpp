/// @file system.hpp
/// @brief System base class for ECS game logic and rendering.
#pragma once

namespace xebble {

class World;
class Renderer;

/// @brief Base class for ECS systems.
///
/// Override one or more methods. Systems run in registration order.
/// init() is called once after engine resources are available.
/// update() is called at fixed timestep. draw() is called each frame.
class System {
public:
    virtual ~System() = default;

    /// @brief Called once after all systems are added and engine resources are available.
    virtual void init(World& world) {}

    /// @brief Called at fixed timestep for game logic.
    virtual void update(World& world, float dt) {}

    /// @brief Called each frame for rendering.
    virtual void draw(World& world, Renderer& renderer) {}
};

} // namespace xebble
