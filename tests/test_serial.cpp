/// @file test_serial.cpp
/// @brief Unit tests for World::snapshot() / World::restore() save-load system.
///
/// All tests use only the ECS (World, entities, components, resources) and the
/// serial infrastructure — no GPU context is needed.
#include <xebble/components.hpp>
#include <xebble/rng.hpp>
#include <xebble/serial.hpp>
#include <xebble/world.hpp>

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Test component types
// ---------------------------------------------------------------------------

/// @brief Simple position component used in tests.
struct TestPos {
    float x = 0.0f;
    float y = 0.0f;
};

/// @brief Simple health component used in tests.
struct TestHealth {
    int hp = 0;
    int max_hp = 0;
};

/// @brief A tag component (zero-size would be UB with trivial types; use 1 byte).
struct TestTag {
    uint8_t active = 1;
};

/// @brief A component NOT opted into serialization (should be silently skipped).
struct NonSerial {
    int value = 99;
};

/// @brief A resource type used in tests.
struct TestScore {
    int value = 0;
};

// ---------------------------------------------------------------------------
// ComponentName / ResourceName specializations
// ---------------------------------------------------------------------------

template<>
struct xebble::ComponentName<TestPos> {
    static constexpr std::string_view value = "test::TestPos";
};

template<>
struct xebble::ComponentName<TestHealth> {
    static constexpr std::string_view value = "test::TestHealth";
};

template<>
struct xebble::ComponentName<TestTag> {
    static constexpr std::string_view value = "test::TestTag";
};

template<>
struct xebble::ResourceName<TestScore> {
    static constexpr std::string_view value = "test::TestScore";
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a fresh World with serializable registrations.
static xebble::World make_world() {
    xebble::World w;
    w.register_serializable_component<TestPos>();
    w.register_serializable_component<TestHealth>();
    w.register_serializable_component<TestTag>();
    w.register_component<NonSerial>();
    return w;
}

// ---------------------------------------------------------------------------
// Tests: ComponentName / ResourceName traits
// ---------------------------------------------------------------------------

TEST(Serial_Traits, ComponentNameValue) {
    EXPECT_EQ(xebble::ComponentName<TestPos>::value, "test::TestPos");
    EXPECT_EQ(xebble::ComponentName<TestHealth>::value, "test::TestHealth");
    EXPECT_EQ(xebble::ComponentName<TestTag>::value, "test::TestTag");
}

TEST(Serial_Traits, ResourceNameValue) {
    EXPECT_EQ(xebble::ResourceName<TestScore>::value, "test::TestScore");
}

TEST(Serial_Traits, BuiltinComponentNamePosition) {
    EXPECT_EQ(xebble::ComponentName<xebble::Position>::value, "xebble::Position");
}

TEST(Serial_Traits, BuiltinResourceNameCamera) {
    EXPECT_EQ(xebble::ResourceName<xebble::Camera>::value, "xebble::Camera");
}

TEST(Serial_Traits, BuiltinResourceNameRngState) {
    EXPECT_EQ(xebble::ResourceName<xebble::RngState>::value, "xebble::RngState");
}

// ---------------------------------------------------------------------------
// Tests: snapshot of empty world
// ---------------------------------------------------------------------------

TEST(Serial_Snapshot, EmptyWorldProducesNonEmptyBlob) {
    xebble::World w = make_world();
    auto blob = w.snapshot();
    // Must at least contain the 5-field header (5 × uint32_t = 20 bytes).
    EXPECT_GE(blob.size(), 20u);
}

TEST(Serial_Snapshot, EmptyWorldHasCorrectMagicAndVersion) {
    xebble::World w = make_world();
    auto blob = w.snapshot();
    uint32_t magic = 0, version = 0;
    std::memcpy(&magic, blob.data(), sizeof(uint32_t));
    std::memcpy(&version, blob.data() + 4, sizeof(uint32_t));
    EXPECT_EQ(magic, 0x58424C53u);
    EXPECT_EQ(version, 1u);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — single entity, single component
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, SingleEntitySingleComponent) {
    xebble::World src = make_world();
    auto e = src.build_entity().with(TestPos{3.0f, 7.0f}).build();
    (void)e;

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    auto result = dst.restore(blob);
    EXPECT_TRUE(result.has_value()) << result.error().message;

    int count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos& p) {
        EXPECT_FLOAT_EQ(p.x, 3.0f);
        EXPECT_FLOAT_EQ(p.y, 7.0f);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(Serial_RoundTrip, MultipleEntities) {
    xebble::World src = make_world();
    (void)src.build_entity().with(TestPos{1.0f, 2.0f}).build();
    (void)src.build_entity().with(TestPos{3.0f, 4.0f}).build();
    (void)src.build_entity().with(TestPos{5.0f, 6.0f}).build();

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    float sum_x = 0.0f;
    dst.each<TestPos>([&](xebble::Entity, TestPos& p) {
        sum_x += p.x;
        ++count;
    });
    EXPECT_EQ(count, 3);
    EXPECT_FLOAT_EQ(sum_x, 1.0f + 3.0f + 5.0f);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — multiple components on the same entity
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, MultipleComponentsOnOneEntity) {
    xebble::World src = make_world();
    (void)src.build_entity()
        .with(TestPos{10.0f, 20.0f})
        .with(TestHealth{15, 30})
        .with(TestTag{1})
        .build();

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<TestPos, TestHealth, TestTag>(
        [&](xebble::Entity, TestPos& p, TestHealth& h, TestTag& t) {
            EXPECT_FLOAT_EQ(p.x, 10.0f);
            EXPECT_FLOAT_EQ(p.y, 20.0f);
            EXPECT_EQ(h.hp, 15);
            EXPECT_EQ(h.max_hp, 30);
            EXPECT_EQ(t.active, 1u);
            ++count;
        });
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — non-serializable component is silently skipped
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, NonSerializableComponentNotRestored) {
    xebble::World src = make_world();
    auto e = src.build_entity().with(TestPos{5.0f, 5.0f}).with(NonSerial{42}).build();
    (void)e;

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    ASSERT_TRUE(dst.restore(blob).has_value());

    // TestPos should be present, but NonSerial should not be on any entity
    // (it was not registered as serializable).
    int pos_count = 0;
    dst.each<TestPos>([&](xebble::Entity entity, TestPos& p) {
        EXPECT_FLOAT_EQ(p.x, 5.0f);
        EXPECT_FLOAT_EQ(p.y, 5.0f);
        // NonSerial was registered as a plain (non-serializable) component in dst.
        // It should NOT be present since it was never serialized.
        EXPECT_FALSE(dst.has<NonSerial>(entity));
        ++pos_count;
    });
    EXPECT_EQ(pos_count, 1);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — resource serialization
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, ResourceRoundTrip) {
    xebble::World src = make_world();
    src.add_serializable_resource(TestScore{999});

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    dst.add_serializable_resource(TestScore{0}); // default value
    ASSERT_TRUE(dst.restore(blob).has_value());

    EXPECT_EQ(dst.resource<TestScore>().value, 999);
}

TEST(Serial_RoundTrip, RngStateResourceRoundTrip) {
    xebble::World src = make_world();
    xebble::Rng rng(12345u);
    (void)rng.next_u32(); // advance a few times
    (void)rng.next_u32();
    xebble::RngState state = rng.save();
    src.add_serializable_resource(state);

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    dst.add_serializable_resource(xebble::RngState{}); // zeroed default
    ASSERT_TRUE(dst.restore(blob).has_value());

    xebble::RngState restored = dst.resource<xebble::RngState>();
    EXPECT_EQ(restored, state);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — components + resources together
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, ComponentsAndResourcesTogether) {
    xebble::World src = make_world();
    src.add_serializable_resource(TestScore{42});
    (void)src.build_entity().with(TestPos{1.0f, 2.0f}).build();
    (void)src.build_entity().with(TestHealth{10, 20}).build();

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    dst.add_serializable_resource(TestScore{0});
    ASSERT_TRUE(dst.restore(blob).has_value());

    EXPECT_EQ(dst.resource<TestScore>().value, 42);

    int pos_count = 0, hp_count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos&) { ++pos_count; });
    dst.each<TestHealth>([&](xebble::Entity, TestHealth&) { ++hp_count; });
    EXPECT_EQ(pos_count, 1);
    EXPECT_EQ(hp_count, 1);
}

// ---------------------------------------------------------------------------
// Tests: double restore clears old state
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, SecondRestoreClearsFirstRestore) {
    // Build world with 3 entities.
    xebble::World src = make_world();
    (void)src.build_entity().with(TestPos{1.0f, 0.0f}).build();
    (void)src.build_entity().with(TestPos{2.0f, 0.0f}).build();
    (void)src.build_entity().with(TestPos{3.0f, 0.0f}).build();
    auto blob3 = src.snapshot();

    // Snapshot a world with only 1 entity.
    xebble::World src2 = make_world();
    (void)src2.build_entity().with(TestPos{99.0f, 0.0f}).build();
    auto blob1 = src2.snapshot();

    xebble::World dst = make_world();
    ASSERT_TRUE(dst.restore(blob3).has_value()); // 3 entities

    int count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos&) { ++count; });
    EXPECT_EQ(count, 3);

    ASSERT_TRUE(dst.restore(blob1).has_value()); // now 1 entity — old ones gone
    count = 0;
    float x = 0.0f;
    dst.each<TestPos>([&](xebble::Entity, TestPos& p) {
        x = p.x;
        ++count;
    });
    EXPECT_EQ(count, 1);
    EXPECT_FLOAT_EQ(x, 99.0f);
}

// ---------------------------------------------------------------------------
// Tests: corrupt blob detection
// ---------------------------------------------------------------------------

TEST(Serial_Corrupt, EmptyBlobReturnsError) {
    xebble::World w = make_world();
    std::vector<uint8_t> empty;
    auto result = w.restore(empty);
    EXPECT_FALSE(result.has_value());
}

TEST(Serial_Corrupt, WrongMagicReturnsError) {
    xebble::World src = make_world();
    auto blob = src.snapshot();
    blob[0] = 0xDE;
    blob[1] = 0xAD;
    blob[2] = 0xBE;
    blob[3] = 0xEF;

    xebble::World dst = make_world();
    auto result = dst.restore(blob);
    EXPECT_FALSE(result.has_value());
}

TEST(Serial_Corrupt, WrongVersionReturnsError) {
    xebble::World src = make_world();
    auto blob = src.snapshot();
    // Version is the second uint32_t (bytes 4-7).
    uint32_t bad_version = 99u;
    std::memcpy(blob.data() + 4, &bad_version, sizeof(uint32_t));

    xebble::World dst = make_world();
    auto result = dst.restore(blob);
    EXPECT_FALSE(result.has_value());
}

TEST(Serial_Corrupt, TruncatedBlobReturnsError) {
    xebble::World src = make_world();
    (void)src.build_entity().with(TestPos{1.0f, 2.0f}).build();
    auto blob = src.snapshot();
    blob.resize(blob.size() / 2); // truncate to half

    xebble::World dst = make_world();
    auto result = dst.restore(blob);
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// Tests: unknown pool in blob is skipped gracefully
// ---------------------------------------------------------------------------

TEST(Serial_Compat, UnknownPoolInBlobIsSkipped) {
    // Writer registers TestPos; reader only knows TestHealth.
    xebble::World src;
    src.register_serializable_component<TestPos>();
    src.register_serializable_component<TestHealth>();
    (void)src.build_entity().with(TestPos{7.0f, 8.0f}).with(TestHealth{5, 10}).build();
    auto blob = src.snapshot();

    // Destination does NOT register TestPos — should skip it gracefully.
    xebble::World dst;
    dst.register_serializable_component<TestHealth>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int hp_count = 0;
    dst.each<TestHealth>([&](xebble::Entity, TestHealth& h) {
        EXPECT_EQ(h.hp, 5);
        EXPECT_EQ(h.max_hp, 10);
        ++hp_count;
    });
    EXPECT_EQ(hp_count, 1);
}

// ---------------------------------------------------------------------------
// Tests: built-in Position component serialization
// ---------------------------------------------------------------------------

TEST(Serial_BuiltIn, PositionRoundTrip) {
    xebble::World src;
    src.register_serializable_component<xebble::Position>();
    (void)src.build_entity().with(xebble::Position{3.14f, 2.71f}).build();

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<xebble::Position>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<xebble::Position>([&](xebble::Entity, xebble::Position& p) {
        EXPECT_FLOAT_EQ(p.x, 3.14f);
        EXPECT_FLOAT_EQ(p.y, 2.71f);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Tests: built-in Camera resource serialization
// ---------------------------------------------------------------------------

TEST(Serial_BuiltIn, CameraResourceRoundTrip) {
    xebble::World src;
    src.add_serializable_resource(xebble::Camera{100.0f, 200.0f});

    auto blob = src.snapshot();

    xebble::World dst;
    dst.add_serializable_resource(xebble::Camera{0.0f, 0.0f});
    ASSERT_TRUE(dst.restore(blob).has_value());

    auto& cam = dst.resource<xebble::Camera>();
    EXPECT_FLOAT_EQ(cam.x, 100.0f);
    EXPECT_FLOAT_EQ(cam.y, 200.0f);
}

// ===========================================================================
// Custom serialization tests — non-trivially-copyable components
// ===========================================================================

// ---------------------------------------------------------------------------
// Test types with custom serialize/deserialize hooks
// ---------------------------------------------------------------------------

/// @brief A component containing a std::string (not trivially copyable).
struct NameTag {
    std::string name;

    void serialize(xebble::BinaryWriter& w) const { w.write_string(name); }
    static NameTag deserialize(xebble::BinaryReader& r) { return NameTag{r.read_string()}; }
};

/// @brief A component containing a vector of strings.
struct Inventory {
    std::vector<std::string> items;

    void serialize(xebble::BinaryWriter& w) const {
        w.write(static_cast<uint32_t>(items.size()));
        for (const auto& s : items) {
            w.write_string(s);
        }
    }
    static Inventory deserialize(xebble::BinaryReader& r) {
        Inventory inv;
        const auto count = r.read<uint32_t>();
        inv.items.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            inv.items.push_back(r.read_string());
        }
        return inv;
    }
};

/// @brief A component with mixed trivial and non-trivial data.
struct Stats {
    int strength = 0;
    int dexterity = 0;
    std::string title;
    std::vector<float> modifiers;

    void serialize(xebble::BinaryWriter& w) const {
        w.write(strength);
        w.write(dexterity);
        w.write_string(title);
        w.write_vector(modifiers);
    }
    static Stats deserialize(xebble::BinaryReader& r) {
        Stats s;
        s.strength = r.read<int>();
        s.dexterity = r.read<int>();
        s.title = r.read_string();
        s.modifiers = r.read_vector<float>();
        return s;
    }
};

/// @brief A custom-serializable resource (non-trivially-copyable).
struct GameLog {
    std::vector<std::string> entries;

    void serialize(xebble::BinaryWriter& w) const {
        w.write(static_cast<uint32_t>(entries.size()));
        for (const auto& e : entries) {
            w.write_string(e);
        }
    }
    static GameLog deserialize(xebble::BinaryReader& r) {
        GameLog log;
        const auto count = r.read<uint32_t>();
        log.entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            log.entries.push_back(r.read_string());
        }
        return log;
    }
};

// ComponentName / ResourceName specializations for custom types.
template<>
struct xebble::ComponentName<NameTag> {
    static constexpr std::string_view value = "test::NameTag";
};
template<>
struct xebble::ComponentName<Inventory> {
    static constexpr std::string_view value = "test::Inventory";
};
template<>
struct xebble::ComponentName<Stats> {
    static constexpr std::string_view value = "test::Stats";
};
template<>
struct xebble::ResourceName<GameLog> {
    static constexpr std::string_view value = "test::GameLog";
};

// ---------------------------------------------------------------------------
// Tests: BinaryWriter / BinaryReader round-trip
// ---------------------------------------------------------------------------

TEST(Serial_BinaryIO, WriterReaderRoundTrip) {
    std::vector<uint8_t> buf;
    xebble::BinaryWriter w(buf);

    w.write(int32_t{42});
    w.write(3.14f);
    w.write_string("hello world");
    const std::vector<int> vec = {1, 2, 3, 4, 5};
    w.write_vector(vec);

    xebble::BinaryReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read<int32_t>(), 42);
    EXPECT_FLOAT_EQ(r.read<float>(), 3.14f);
    EXPECT_EQ(r.read_string(), "hello world");
    auto restored_vec = r.read_vector<int>();
    EXPECT_EQ(restored_vec, vec);
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(Serial_BinaryIO, EmptyStringRoundTrip) {
    std::vector<uint8_t> buf;
    xebble::BinaryWriter w(buf);
    w.write_string("");

    xebble::BinaryReader r(buf.data(), buf.size());
    EXPECT_EQ(r.read_string(), "");
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(Serial_BinaryIO, EmptyVectorRoundTrip) {
    std::vector<uint8_t> buf;
    xebble::BinaryWriter w(buf);
    const std::vector<int> empty;
    w.write_vector(empty);

    xebble::BinaryReader r(buf.data(), buf.size());
    auto v = r.read_vector<int>();
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(r.remaining(), 0u);
}

TEST(Serial_BinaryIO, ReaderThrowsOnOverrun) {
    std::vector<uint8_t> buf = {0x01, 0x02}; // only 2 bytes
    xebble::BinaryReader r(buf.data(), buf.size());
    EXPECT_THROW(static_cast<void>(r.read<uint32_t>()), std::runtime_error);
}

TEST(Serial_BinaryIO, WriteAndReadBytes) {
    std::vector<uint8_t> buf;
    xebble::BinaryWriter w(buf);
    std::vector<uint8_t> raw_data = {0xDE, 0xAD, 0xBE, 0xEF};
    w.write_bytes(raw_data);

    xebble::BinaryReader r(buf.data(), buf.size());
    auto result = r.read_bytes(4);
    EXPECT_EQ(result, raw_data);
    EXPECT_EQ(r.remaining(), 0u);
}

// ---------------------------------------------------------------------------
// Tests: custom serializable concept check
// ---------------------------------------------------------------------------

static_assert(!std::is_trivially_copyable_v<NameTag>,
              "NameTag must NOT be trivially copyable for this test");
static_assert(!std::is_trivially_copyable_v<Inventory>,
              "Inventory must NOT be trivially copyable for this test");
static_assert(!std::is_trivially_copyable_v<Stats>,
              "Stats must NOT be trivially copyable for this test");
static_assert(xebble::serial_detail::CustomSerializable<NameTag>,
              "NameTag must satisfy CustomSerializable");
static_assert(xebble::serial_detail::CustomSerializable<Inventory>,
              "Inventory must satisfy CustomSerializable");
static_assert(xebble::serial_detail::CustomSerializable<Stats>,
              "Stats must satisfy CustomSerializable");

// ---------------------------------------------------------------------------
// Tests: single custom-serializable component round-trip
// ---------------------------------------------------------------------------

TEST(Serial_Custom, SingleEntityNameTag) {
    xebble::World src;
    src.register_serializable_component<NameTag>();
    (void)src.build_entity().with(NameTag{"Goblin King"}).build();

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<NameTag>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<NameTag>([&](xebble::Entity, NameTag& n) {
        EXPECT_EQ(n.name, "Goblin King");
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(Serial_Custom, MultipleEntitiesWithInventory) {
    xebble::World src;
    src.register_serializable_component<Inventory>();
    (void)src.build_entity().with(Inventory{{"sword", "shield", "potion"}}).build();
    (void)src.build_entity().with(Inventory{{"bow", "arrows"}}).build();
    (void)src.build_entity().with(Inventory{{}}).build(); // empty inventory

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<Inventory>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    size_t total_items = 0;
    dst.each<Inventory>([&](xebble::Entity, Inventory& inv) {
        total_items += inv.items.size();
        ++count;
    });
    EXPECT_EQ(count, 3);
    EXPECT_EQ(total_items, 5u); // 3 + 2 + 0
}

TEST(Serial_Custom, MixedTrivialAndCustomComponents) {
    xebble::World src;
    src.register_serializable_component<TestPos>();
    src.register_serializable_component<NameTag>();
    (void)src.build_entity().with(TestPos{1.0f, 2.0f}).with(NameTag{"Hero"}).build();
    (void)src.build_entity().with(TestPos{3.0f, 4.0f}).with(NameTag{"Villain"}).build();

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<TestPos>();
    dst.register_serializable_component<NameTag>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<TestPos, NameTag>([&](xebble::Entity, TestPos& p, NameTag& n) {
        if (n.name == "Hero") {
            EXPECT_FLOAT_EQ(p.x, 1.0f);
            EXPECT_FLOAT_EQ(p.y, 2.0f);
        } else {
            EXPECT_EQ(n.name, "Villain");
            EXPECT_FLOAT_EQ(p.x, 3.0f);
            EXPECT_FLOAT_EQ(p.y, 4.0f);
        }
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST(Serial_Custom, ComplexComponentWithMixedData) {
    xebble::World src;
    src.register_serializable_component<Stats>();
    (void)src.build_entity().with(Stats{18, 14, "Warrior", {1.0f, 1.5f, 0.8f}}).build();

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<Stats>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<Stats>([&](xebble::Entity, Stats& s) {
        EXPECT_EQ(s.strength, 18);
        EXPECT_EQ(s.dexterity, 14);
        EXPECT_EQ(s.title, "Warrior");
        ASSERT_EQ(s.modifiers.size(), 3u);
        EXPECT_FLOAT_EQ(s.modifiers[0], 1.0f);
        EXPECT_FLOAT_EQ(s.modifiers[1], 1.5f);
        EXPECT_FLOAT_EQ(s.modifiers[2], 0.8f);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Tests: custom-serializable resource round-trip
// ---------------------------------------------------------------------------

TEST(Serial_Custom, CustomResourceRoundTrip) {
    xebble::World src;
    src.add_serializable_resource(GameLog{{"You entered the dungeon.", "A goblin appears!"}});

    auto blob = src.snapshot();

    xebble::World dst;
    dst.add_serializable_resource(GameLog{{}});
    ASSERT_TRUE(dst.restore(blob).has_value());

    auto& log = dst.resource<GameLog>();
    ASSERT_EQ(log.entries.size(), 2u);
    EXPECT_EQ(log.entries[0], "You entered the dungeon.");
    EXPECT_EQ(log.entries[1], "A goblin appears!");
}

// ---------------------------------------------------------------------------
// Tests: custom component + resource + trivial all together
// ---------------------------------------------------------------------------

TEST(Serial_Custom, EverythingTogether) {
    xebble::World src;
    src.register_serializable_component<TestPos>();
    src.register_serializable_component<TestHealth>();
    src.register_serializable_component<NameTag>();
    src.register_serializable_component<Inventory>();
    src.add_serializable_resource(TestScore{777});
    src.add_serializable_resource(GameLog{{"Turn 1", "Turn 2"}});

    (void)src.build_entity()
        .with(TestPos{10.0f, 20.0f})
        .with(TestHealth{50, 100})
        .with(NameTag{"Player"})
        .with(Inventory{{"magic staff", "robe"}})
        .build();

    auto blob = src.snapshot();

    xebble::World dst;
    dst.register_serializable_component<TestPos>();
    dst.register_serializable_component<TestHealth>();
    dst.register_serializable_component<NameTag>();
    dst.register_serializable_component<Inventory>();
    dst.add_serializable_resource(TestScore{0});
    dst.add_serializable_resource(GameLog{{}});
    ASSERT_TRUE(dst.restore(blob).has_value());

    // Verify components
    int count = 0;
    dst.each<TestPos, TestHealth, NameTag, Inventory>(
        [&](xebble::Entity, TestPos& p, TestHealth& h, NameTag& n, Inventory& inv) {
            EXPECT_FLOAT_EQ(p.x, 10.0f);
            EXPECT_FLOAT_EQ(p.y, 20.0f);
            EXPECT_EQ(h.hp, 50);
            EXPECT_EQ(h.max_hp, 100);
            EXPECT_EQ(n.name, "Player");
            ASSERT_EQ(inv.items.size(), 2u);
            EXPECT_EQ(inv.items[0], "magic staff");
            EXPECT_EQ(inv.items[1], "robe");
            ++count;
        });
    EXPECT_EQ(count, 1);

    // Verify resources
    EXPECT_EQ(dst.resource<TestScore>().value, 777);
    auto& log = dst.resource<GameLog>();
    ASSERT_EQ(log.entries.size(), 2u);
    EXPECT_EQ(log.entries[0], "Turn 1");
    EXPECT_EQ(log.entries[1], "Turn 2");
}

// ---------------------------------------------------------------------------
// Tests: unknown custom pool in blob is skipped gracefully
// ---------------------------------------------------------------------------

TEST(Serial_Custom, UnknownCustomPoolSkipped) {
    xebble::World src;
    src.register_serializable_component<NameTag>();
    src.register_serializable_component<TestPos>();
    (void)src.build_entity().with(NameTag{"test"}).with(TestPos{5.0f, 5.0f}).build();

    auto blob = src.snapshot();

    // Destination only knows TestPos — NameTag (custom) should be skipped.
    xebble::World dst;
    dst.register_serializable_component<TestPos>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos& p) {
        EXPECT_FLOAT_EQ(p.x, 5.0f);
        ++count;
    });
    EXPECT_EQ(count, 1);
}

// ---------------------------------------------------------------------------
// Tests: double restore with custom components
// ---------------------------------------------------------------------------

TEST(Serial_Custom, DoubleRestoreClearsCustomComponents) {
    xebble::World src1;
    src1.register_serializable_component<NameTag>();
    (void)src1.build_entity().with(NameTag{"First"}).build();
    (void)src1.build_entity().with(NameTag{"Second"}).build();
    auto blob1 = src1.snapshot();

    xebble::World src2;
    src2.register_serializable_component<NameTag>();
    (void)src2.build_entity().with(NameTag{"Only"}).build();
    auto blob2 = src2.snapshot();

    xebble::World dst;
    dst.register_serializable_component<NameTag>();
    ASSERT_TRUE(dst.restore(blob1).has_value());

    int count = 0;
    dst.each<NameTag>([&](xebble::Entity, NameTag&) { ++count; });
    EXPECT_EQ(count, 2);

    ASSERT_TRUE(dst.restore(blob2).has_value());
    count = 0;
    std::string name;
    dst.each<NameTag>([&](xebble::Entity, NameTag& n) {
        name = n.name;
        ++count;
    });
    EXPECT_EQ(count, 1);
    EXPECT_EQ(name, "Only");
}
