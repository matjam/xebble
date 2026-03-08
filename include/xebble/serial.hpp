/// @file serial.hpp
/// @brief World serialization — snapshot all entities, components, and resources
///        to a binary blob and restore them later.
///
/// This is the save/load infrastructure for Xebble-based roguelikes.
///
/// ## Design Overview
///
/// Components and resources **opt in** to serialization by specialising the
/// `ComponentName<T>` or `ResourceName<T>` trait respectively.  Types that
/// don't specialise these traits are silently skipped during snapshot/restore,
/// which is the right behaviour for engine-owned things like `Renderer*` or
/// `EventQueue`.
///
/// Only **trivially-copyable** component and resource types may be registered.
/// Types containing pointers or `shared_ptr` members cannot be directly
/// serialized — use a separate, serializable value type to hold the relevant
/// data (e.g. a tile-index array for a TileMap, a string name for a
/// SpriteSheet reference, etc.).
///
/// ## Quick-start
///
/// ```cpp
/// // 1. Opt your types into serialization.
/// struct Health { int hp = 0; int max_hp = 0; };
/// struct Score  { int value = 0; };
///
/// template<> struct xebble::ComponentName<Health>
///     { static constexpr std::string_view value = "game::Health"; };
/// template<> struct xebble::ComponentName<Score>
///     { static constexpr std::string_view value = "game::Score"; };
///
/// // 2. Register components using the serializable overload.
/// world.register_serializable_component<Health>();
/// world.register_serializable_component<Score>();
///
/// // 3. Snapshot.
/// auto blob = world.snapshot();
///
/// // 4. Restore (in a fresh World with the same registrations).
/// World fresh;
/// fresh.register_serializable_component<Health>();
/// fresh.register_serializable_component<Score>();
/// auto result = fresh.restore(blob);
/// if (!result) { /* handle error */ }
/// ```
///
/// ## Binary Format
///
/// The blob is a simple self-describing binary structure:
///
/// ```
/// [Header]
///   magic            : uint32_t  = 0x58424C53 ("XBLS")
///   version          : uint32_t  = 1
///   entity_count     : uint32_t
///   pool_count       : uint32_t
///   resource_count   : uint32_t
///
/// [Entity Table]  (entity_count records)
///   slot_index       : uint32_t
///   generation       : uint32_t
///
/// [Component Sections]  (pool_count records)
///   name_len         : uint16_t
///   name             : char[name_len]
///   record_count     : uint32_t
///   data             : (uint32_t slot + T bytes)[record_count]
///
/// [Resource Sections]  (resource_count records)
///   name_len         : uint16_t
///   name             : char[name_len]
///   data_len         : uint32_t
///   data             : uint8_t[data_len]
/// ```
#pragma once

#include <xebble/ecs.hpp>

#include <any>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// ComponentName<T> — opt-in trait for serializable components
// ---------------------------------------------------------------------------

/// @brief Specialize this trait to opt a component type into serialization.
///
/// The `value` member must be a stable, unique ASCII string that identifies
/// this component type in save files.  Changing it will break compatibility
/// with existing saves.
///
/// @code
/// struct Health { int hp; int max_hp; };
/// template<> struct xebble::ComponentName<Health>
///     { static constexpr std::string_view value = "game::Health"; };
/// @endcode
template<typename T>
struct ComponentName {
    // Unspecialized — type is not serializable.
};

// ---------------------------------------------------------------------------
// ResourceName<T> — opt-in trait for serializable resources
// ---------------------------------------------------------------------------

/// @brief Specialize this trait to opt a resource type into serialization.
///
/// @code
/// struct WorldSeed { uint64_t value; };
/// template<> struct xebble::ResourceName<WorldSeed>
///     { static constexpr std::string_view value = "game::WorldSeed"; };
/// @endcode
template<typename T>
struct ResourceName {
    // Unspecialized — resource is not serializable.
};

// ---------------------------------------------------------------------------
// serial_detail helpers
// ---------------------------------------------------------------------------

namespace serial_detail {

/// @brief Write raw bytes of @p value into @p buf.
template<typename T>
void append(std::vector<uint8_t>& buf, const T& value) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    buf.insert(buf.end(), p, p + sizeof(T));
}

/// @brief Read sizeof(T) bytes from @p src into @p out. Returns false on overflow.
template<typename T>
bool read(const uint8_t* src, size_t src_size, size_t& offset, T& out) {
    if (offset + sizeof(T) > src_size)
        return false;
    std::memcpy(&out, src + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

/// @brief Write a uint16_t length-prefixed string into @p buf.
inline void append_string(std::vector<uint8_t>& buf, std::string_view s) {
    auto len = static_cast<uint16_t>(s.size());
    append(buf, len);
    buf.insert(buf.end(), s.begin(), s.end());
}

/// @brief Read a uint16_t length-prefixed string. Returns false on overflow.
inline bool read_string(const uint8_t* src, size_t src_size, size_t& offset, std::string& out) {
    uint16_t len = 0;
    if (!read(src, src_size, offset, len))
        return false;
    if (offset + len > src_size)
        return false;
    out.assign(reinterpret_cast<const char*>(src + offset), len);
    offset += len;
    return true;
}

/// @brief concept: ComponentName<T>::value exists (type is serializable).
template<typename T>
concept HasComponentName = requires { ComponentName<T>::value; };

/// @brief concept: ResourceName<T>::value exists (resource is serializable).
template<typename T>
concept HasResourceName = requires { ResourceName<T>::value; };

} // namespace serial_detail

// ---------------------------------------------------------------------------
// ISerializablePool — extends IComponentPool with serialization
// ---------------------------------------------------------------------------

/// @brief Extension of IComponentPool that supports binary serialization.
///
/// Concrete instances are `SerializableComponentPool<T>`, created by
/// `World::register_serializable_component<T>()`.
///
/// Inherits `IComponentPool` **virtually** so that `SerializableComponentPool<T>`
/// (which also derives from `ComponentPool<T> : virtual IComponentPool`) forms a
/// well-defined diamond with a single shared `IComponentPool` sub-object.
class ISerializablePool : public virtual IComponentPool {
public:
    /// @brief Stable name key as declared in `ComponentName<T>::value`.
    virtual std::string_view component_name() const = 0;

    /// @brief Number of bytes per serialized record: sizeof(uint32_t) + sizeof(T).
    virtual size_t bytes_per_record() const = 0;

    /// @brief Append all (entity_slot : uint32_t, T : sizeof(T)) pairs to @p out.
    ///        Sets @p record_count_out to the number of records written.
    virtual void serialize_all(std::vector<uint8_t>& out, uint32_t& record_count_out) const = 0;

    /// @brief Restore records from the blob, mapping saved slots through @p entity_map.
    ///
    /// @param data        Pointer to the raw bytes for this pool's section.
    /// @param size        Byte length of the section.
    /// @param entity_map  Maps saved entity slot index → live Entity handle.
    ///                    entity_map[i].id == 0 means slot was not recreated.
    virtual void deserialize_all(const uint8_t* data, size_t size,
                                 const std::vector<Entity>& entity_map) = 0;

    /// @brief Remove all entities currently in this pool.
    ///        Used by World::restore() to clear stale state before re-populating.
    virtual void drain() = 0;

    /// @brief Fill @p out with the dense entity handles currently in this pool.
    virtual void enumerate_entities(std::vector<Entity>& out) const = 0;
};

// ---------------------------------------------------------------------------
// SerializableComponentPool<T>
// ---------------------------------------------------------------------------

/// @brief Sparse-set component pool that additionally supports binary serialization.
///
/// Installed by `World::register_serializable_component<T>()` when
/// `ComponentName<T>` is specialized.  T must be trivially copyable.
template<typename T>
    requires serial_detail::HasComponentName<T> && std::is_trivially_copyable_v<T>
class SerializableComponentPool : public ComponentPool<T>, public ISerializablePool {
public:
    // IComponentPool pure virtuals delegated to ComponentPool<T>
    void remove(Entity e) override { ComponentPool<T>::remove(e); }
    bool has(Entity e) const override { return ComponentPool<T>::has(e); }

    std::string_view component_name() const override { return ComponentName<T>::value; }

    size_t bytes_per_record() const override { return sizeof(uint32_t) + sizeof(T); }

    void serialize_all(std::vector<uint8_t>& out, uint32_t& record_count_out) const override {
        record_count_out = static_cast<uint32_t>(this->size());
        for (size_t i = 0; i < this->size(); ++i) {
            uint32_t slot = ecs_detail::entity_index(this->dense_entity(i));
            serial_detail::append(out, slot);
            serial_detail::append(out, this->dense_component(i));
        }
    }

    void deserialize_all(const uint8_t* data, size_t size,
                         const std::vector<Entity>& entity_map) override {
        constexpr size_t record_size = sizeof(uint32_t) + sizeof(T);
        size_t offset = 0;
        while (offset + record_size <= size) {
            uint32_t saved_slot = 0;
            T value{};
            serial_detail::read(data, size, offset, saved_slot);
            serial_detail::read(data, size, offset, value);
            if (saved_slot < entity_map.size()) {
                Entity e = entity_map[saved_slot];
                // Entity{UINT32_MAX} is the "not present" sentinel set by restore().
                if (e.id != UINT32_MAX && !this->has(e))
                    this->add(e, std::move(value));
            }
        }
    }

    void drain() override {
        // Delegate to ComponentPool<T>::clear_all() for O(1) bulk clear.
        this->clear_all();
    }

    void enumerate_entities(std::vector<Entity>& out) const override {
        for (size_t i = 0; i < this->size(); ++i)
            out.push_back(this->dense_entity(i));
    }
};

// ---------------------------------------------------------------------------
// ResourceSerializer — typed pair of write/read functions for a resource
// ---------------------------------------------------------------------------

namespace serial_detail {

struct ResourceSerializer {
    std::string name;
    std::function<void(const std::any&, std::vector<uint8_t>&)> write;
    std::function<void(std::any&, const uint8_t*, size_t)> read;
};

} // namespace serial_detail

// ---------------------------------------------------------------------------
// Save format constants
// ---------------------------------------------------------------------------

namespace serial_detail {
constexpr uint32_t MAGIC = 0x58424C53u; // "XBLS"
constexpr uint32_t VERSION = 1u;
} // namespace serial_detail

} // namespace xebble
