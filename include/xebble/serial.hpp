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
/// **Trivially-copyable** types are serialized automatically via memcpy.
///
/// **Non-trivially-copyable** types (e.g. those containing `std::string`,
/// `std::vector`, etc.) can participate in save games by providing custom
/// serialization hooks:
///
/// ```cpp
/// void serialize(BinaryWriter& w) const;
/// static T deserialize(BinaryReader& r);
/// ```
///
/// ## Quick-start
///
/// ```cpp
/// // 1a. Trivially-copyable type — automatic serialization.
/// struct Health { int hp = 0; int max_hp = 0; };
/// template<> struct xebble::ComponentName<Health>
///     { static constexpr std::string_view value = "game::Health"; };
///
/// // 1b. Non-trivially-copyable type — custom hooks.
/// struct Inventory {
///     std::vector<std::string> items;
///
///     void serialize(xebble::BinaryWriter& w) const {
///         w.write(static_cast<uint32_t>(items.size()));
///         for (const auto& s : items) w.write_string(s);
///     }
///     static Inventory deserialize(xebble::BinaryReader& r) {
///         Inventory inv;
///         auto count = r.read<uint32_t>();
///         inv.items.reserve(count);
///         for (uint32_t i = 0; i < count; ++i)
///             inv.items.push_back(r.read_string());
///         return inv;
///     }
/// };
/// template<> struct xebble::ComponentName<Inventory>
///     { static constexpr std::string_view value = "game::Inventory"; };
///
/// // 2. Register components using the serializable overload.
/// world.register_serializable_component<Health>();
/// world.register_serializable_component<Inventory>();
///
/// // 3. Snapshot.
/// auto blob = world.snapshot();
///
/// // 4. Restore (in a fresh World with the same registrations).
/// World fresh;
/// fresh.register_serializable_component<Health>();
/// fresh.register_serializable_component<Inventory>();
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
/// [Component Sections]  (pool_count sections)
///   name_len         : uint16_t
///   name             : char[name_len]
///   record_count     : uint32_t
///   data_len         : uint32_t   — total byte size of all records
///   data             : uint8_t[data_len]
///
///   For trivially-copyable types, each record is:
///     slot : uint32_t + T : sizeof(T) bytes  (fixed size)
///
///   For custom-serializable types, each record is:
///     slot        : uint32_t
///     record_len  : uint32_t
///     payload     : uint8_t[record_len]      (variable size)
///
/// [Resource Sections]  (resource_count sections)
///   name_len         : uint16_t
///   name             : char[name_len]
///   data_len         : uint32_t
///   data             : uint8_t[data_len]
/// ```
#pragma once

#include <xebble/ecs.hpp>

#include <any>
#include <climits>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// BinaryWriter — sequential byte-buffer writer
// ---------------------------------------------------------------------------

/// @brief Sequential writer for building binary serialization payloads.
///
/// Wraps a `std::vector<uint8_t>` and provides typed write methods for
/// trivially-copyable primitives, strings, byte spans, and vectors.
///
/// Used by custom `serialize(BinaryWriter&)` hooks on non-trivially-copyable
/// components and resources.
///
/// @code
/// void serialize(BinaryWriter& w) const {
///     w.write(hp);
///     w.write(max_hp);
///     w.write_string(name);
///     w.write(static_cast<uint32_t>(items.size()));
///     for (const auto& item : items)
///         w.write_string(item);
/// }
/// @endcode
class BinaryWriter {
public:
    /// @brief Construct a writer that appends to @p buf.
    explicit BinaryWriter(std::vector<uint8_t>& buf) : buf_(buf) {}

    /// @brief Write a trivially-copyable value as raw bytes.
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void write(const T& value) {
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        buf_.insert(buf_.end(), p, p + sizeof(T));
    }

    /// @brief Write a raw byte span.
    void write_bytes(std::span<const uint8_t> bytes) {
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());
    }

    /// @brief Write a string as a uint32_t length prefix followed by raw characters.
    void write_string(std::string_view s) {
        write(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    /// @brief Write a vector of trivially-copyable elements as count + raw data.
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    void write_vector(const std::vector<T>& vec) {
        write(static_cast<uint32_t>(vec.size()));
        const auto* p = reinterpret_cast<const uint8_t*>(vec.data());
        buf_.insert(buf_.end(), p, p + (vec.size() * sizeof(T)));
    }

    /// @brief Return the current write position (bytes written so far).
    [[nodiscard]] size_t size() const { return buf_.size(); }

    /// @brief Direct access to the underlying buffer.
    [[nodiscard]] std::vector<uint8_t>& buffer() { return buf_; }

private:
    std::vector<uint8_t>& buf_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

// ---------------------------------------------------------------------------
// BinaryReader — sequential byte-buffer reader
// ---------------------------------------------------------------------------

/// @brief Sequential reader for consuming binary serialization payloads.
///
/// Wraps a `span<const uint8_t>` with a read cursor and provides typed read
/// methods matching `BinaryWriter`.
///
/// Throws `std::runtime_error` on buffer overrun, so callers should catch
/// exceptions when reading untrusted data.
///
/// Used by static `T::deserialize(BinaryReader&)` factory methods on
/// non-trivially-copyable components and resources.
///
/// @code
/// static MyType deserialize(BinaryReader& r) {
///     MyType obj;
///     obj.hp = r.read<int>();
///     obj.max_hp = r.read<int>();
///     obj.name = r.read_string();
///     auto count = r.read<uint32_t>();
///     obj.items.reserve(count);
///     for (uint32_t i = 0; i < count; ++i)
///         obj.items.push_back(r.read_string());
///     return obj;
/// }
/// @endcode
class BinaryReader {
public:
    /// @brief Construct a reader over the given byte span.
    BinaryReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    /// @brief Construct a reader over a span.
    explicit BinaryReader(std::span<const uint8_t> s) : data_(s.data()), size_(s.size()) {}

    /// @brief Read a trivially-copyable value from the current position.
    /// @throws std::runtime_error if there are not enough bytes remaining.
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] T read() {
        if (offset_ + sizeof(T) > size_) {
            throw std::runtime_error("BinaryReader: unexpected end of data");
        }
        T value{};
        std::memcpy(&value, data_ + offset_, sizeof(T));
        offset_ += sizeof(T);
        return value;
    }

    /// @brief Read @p count raw bytes and return them as a vector.
    /// @throws std::runtime_error if there are not enough bytes remaining.
    [[nodiscard]] std::vector<uint8_t> read_bytes(size_t count) {
        if (offset_ + count > size_) {
            throw std::runtime_error("BinaryReader: unexpected end of data");
        }
        std::vector<uint8_t> result(data_ + offset_, data_ + offset_ + count);
        offset_ += count;
        return result;
    }

    /// @brief Read a uint32_t-length-prefixed string.
    /// @throws std::runtime_error if there are not enough bytes remaining.
    [[nodiscard]] std::string read_string() {
        const auto len = read<uint32_t>();
        if (offset_ + len > size_) {
            throw std::runtime_error("BinaryReader: unexpected end of data");
        }
        std::string result(reinterpret_cast<const char*>(data_ + offset_), len);
        offset_ += len;
        return result;
    }

    /// @brief Read a vector of trivially-copyable elements (count + raw data).
    /// @throws std::runtime_error if there are not enough bytes remaining.
    template<typename T>
        requires std::is_trivially_copyable_v<T>
    [[nodiscard]] std::vector<T> read_vector() {
        const auto count = read<uint32_t>();
        const size_t byte_count = static_cast<size_t>(count) * sizeof(T);
        if (offset_ + byte_count > size_) {
            throw std::runtime_error("BinaryReader: unexpected end of data");
        }
        std::vector<T> result(count);
        std::memcpy(result.data(), data_ + offset_, byte_count);
        offset_ += byte_count;
        return result;
    }

    /// @brief Return the current read position.
    [[nodiscard]] size_t offset() const { return offset_; }

    /// @brief Return the total number of bytes remaining.
    [[nodiscard]] size_t remaining() const { return size_ - offset_; }

    /// @brief Return the total size of the underlying data.
    [[nodiscard]] size_t size() const { return size_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
};

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

/// @brief concept: T provides custom serialize/deserialize hooks.
///
/// A type satisfies `CustomSerializable` if it has:
/// - `void serialize(BinaryWriter&) const`
/// - `static T deserialize(BinaryReader&)`
template<typename T>
concept CustomSerializable = requires(const T& ct, BinaryWriter& w, BinaryReader& r) {
    { ct.serialize(w) } -> std::same_as<void>;
    { T::deserialize(r) } -> std::same_as<T>;
};

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
    [[nodiscard]] virtual std::string_view component_name() const = 0;

    /// @brief Number of bytes per serialized record: sizeof(uint32_t) + sizeof(T).
    [[nodiscard]] virtual size_t bytes_per_record() const = 0;

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

    // enumerate_entities() is inherited from IComponentPool (virtual base).
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
    [[nodiscard]] bool has(Entity e) const override { return ComponentPool<T>::has(e); }

    [[nodiscard]] std::string_view component_name() const override {
        return ComponentName<T>::value;
    }

    [[nodiscard]] size_t bytes_per_record() const override { return sizeof(uint32_t) + sizeof(T); }

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
// CustomSerializableComponentPool<T>
// ---------------------------------------------------------------------------

/// @brief Sparse-set component pool with custom binary serialization for
///        non-trivially-copyable types.
///
/// Installed by `World::register_serializable_component<T>()` when
/// `ComponentName<T>` is specialized and T provides custom
/// `serialize(BinaryWriter&)` / `static T deserialize(BinaryReader&)` hooks.
///
/// Each record in the binary format is variable-length:
///   [uint32_t slot][uint32_t record_len][uint8_t payload[record_len]]
template<typename T>
    requires serial_detail::HasComponentName<T> && serial_detail::CustomSerializable<T>
class CustomSerializableComponentPool : public ComponentPool<T>, public ISerializablePool {
public:
    // IComponentPool pure virtuals delegated to ComponentPool<T>
    void remove(Entity e) override { ComponentPool<T>::remove(e); }
    [[nodiscard]] bool has(Entity e) const override { return ComponentPool<T>::has(e); }

    [[nodiscard]] std::string_view component_name() const override {
        return ComponentName<T>::value;
    }

    /// @brief Returns 0 to indicate variable-size records.
    [[nodiscard]] size_t bytes_per_record() const override { return 0; }

    void serialize_all(std::vector<uint8_t>& out, uint32_t& record_count_out) const override {
        record_count_out = static_cast<uint32_t>(this->size());
        for (size_t i = 0; i < this->size(); ++i) {
            const uint32_t slot = ecs_detail::entity_index(this->dense_entity(i));
            serial_detail::append(out, slot);

            // Write a length-prefixed payload produced by the custom hook.
            std::vector<uint8_t> payload;
            BinaryWriter writer(payload);
            this->dense_component(i).serialize(writer);
            const auto payload_len = static_cast<uint32_t>(payload.size());
            serial_detail::append(out, payload_len);
            out.insert(out.end(), payload.begin(), payload.end());
        }
    }

    void deserialize_all(const uint8_t* data, size_t size,
                         const std::vector<Entity>& entity_map) override {
        size_t offset = 0;
        while (offset < size) {
            // Read slot index.
            uint32_t saved_slot = 0;
            if (!serial_detail::read(data, size, offset, saved_slot)) {
                break;
            }

            // Read per-record payload length.
            uint32_t payload_len = 0;
            if (!serial_detail::read(data, size, offset, payload_len)) {
                break;
            }

            if (offset + payload_len > size) {
                break;
            }

            if (saved_slot < entity_map.size()) {
                const Entity e = entity_map[saved_slot];
                if (e.id != UINT32_MAX && !this->has(e)) {
                    BinaryReader reader(data + offset, payload_len);
                    T value = T::deserialize(reader);
                    this->add(e, std::move(value));
                }
            }
            offset += payload_len;
        }
    }

    void drain() override { this->clear_all(); }

    void enumerate_entities(std::vector<Entity>& out) const override {
        for (size_t i = 0; i < this->size(); ++i) {
            out.push_back(this->dense_entity(i));
        }
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
