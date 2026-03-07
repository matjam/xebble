/// @file test_serial.cpp
/// @brief Unit tests for World::snapshot() / World::restore() save-load system.
///
/// All tests use only the ECS (World, entities, components, resources) and the
/// serial infrastructure — no GPU context is needed.
#include <xebble/world.hpp>
#include <xebble/serial.hpp>
#include <xebble/components.hpp>
#include <xebble/rng.hpp>

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Test component types
// ---------------------------------------------------------------------------

/// @brief Simple position component used in tests.
struct TestPos { float x = 0.0f; float y = 0.0f; };

/// @brief Simple health component used in tests.
struct TestHealth { int hp = 0; int max_hp = 0; };

/// @brief A tag component (zero-size would be UB with trivial types; use 1 byte).
struct TestTag { uint8_t active = 1; };

/// @brief A component NOT opted into serialization (should be silently skipped).
struct NonSerial { int value = 99; };

/// @brief A resource type used in tests.
struct TestScore { int value = 0; };

// ---------------------------------------------------------------------------
// ComponentName / ResourceName specializations
// ---------------------------------------------------------------------------

template<> struct xebble::ComponentName<TestPos>
    { static constexpr std::string_view value = "test::TestPos"; };

template<> struct xebble::ComponentName<TestHealth>
    { static constexpr std::string_view value = "test::TestHealth"; };

template<> struct xebble::ComponentName<TestTag>
    { static constexpr std::string_view value = "test::TestTag"; };

template<> struct xebble::ResourceName<TestScore>
    { static constexpr std::string_view value = "test::TestScore"; };

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
    EXPECT_EQ(xebble::ComponentName<TestPos>::value,    "test::TestPos");
    EXPECT_EQ(xebble::ComponentName<TestHealth>::value, "test::TestHealth");
    EXPECT_EQ(xebble::ComponentName<TestTag>::value,    "test::TestTag");
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
    std::memcpy(&magic,   blob.data(),     sizeof(uint32_t));
    std::memcpy(&version, blob.data() + 4, sizeof(uint32_t));
    EXPECT_EQ(magic,   0x58424C53u);
    EXPECT_EQ(version, 1u);
}

// ---------------------------------------------------------------------------
// Tests: round-trip — single entity, single component
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, SingleEntitySingleComponent) {
    xebble::World src = make_world();
    auto e = src.build_entity()
        .with(TestPos{3.0f, 7.0f})
        .build();
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
    src.build_entity().with(TestPos{1.0f, 2.0f}).build();
    src.build_entity().with(TestPos{3.0f, 4.0f}).build();
    src.build_entity().with(TestPos{5.0f, 6.0f}).build();

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
    src.build_entity()
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
            EXPECT_EQ(h.hp,     15);
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
    auto e = src.build_entity()
        .with(TestPos{5.0f, 5.0f})
        .with(NonSerial{42})
        .build();
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
    rng.next_u32(); // advance a few times
    rng.next_u32();
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
    src.build_entity().with(TestPos{1.0f, 2.0f}).build();
    src.build_entity().with(TestHealth{10, 20}).build();

    auto blob = src.snapshot();

    xebble::World dst = make_world();
    dst.add_serializable_resource(TestScore{0});
    ASSERT_TRUE(dst.restore(blob).has_value());

    EXPECT_EQ(dst.resource<TestScore>().value, 42);

    int pos_count = 0, hp_count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos&) { ++pos_count; });
    dst.each<TestHealth>([&](xebble::Entity, TestHealth&) { ++hp_count; });
    EXPECT_EQ(pos_count, 1);
    EXPECT_EQ(hp_count,  1);
}

// ---------------------------------------------------------------------------
// Tests: double restore clears old state
// ---------------------------------------------------------------------------

TEST(Serial_RoundTrip, SecondRestoreClearsFirstRestore) {
    // Build world with 3 entities.
    xebble::World src = make_world();
    src.build_entity().with(TestPos{1.0f, 0.0f}).build();
    src.build_entity().with(TestPos{2.0f, 0.0f}).build();
    src.build_entity().with(TestPos{3.0f, 0.0f}).build();
    auto blob3 = src.snapshot();

    // Snapshot a world with only 1 entity.
    xebble::World src2 = make_world();
    src2.build_entity().with(TestPos{99.0f, 0.0f}).build();
    auto blob1 = src2.snapshot();

    xebble::World dst = make_world();
    ASSERT_TRUE(dst.restore(blob3).has_value()); // 3 entities

    int count = 0;
    dst.each<TestPos>([&](xebble::Entity, TestPos&) { ++count; });
    EXPECT_EQ(count, 3);

    ASSERT_TRUE(dst.restore(blob1).has_value()); // now 1 entity — old ones gone
    count = 0;
    float x = 0.0f;
    dst.each<TestPos>([&](xebble::Entity, TestPos& p) { x = p.x; ++count; });
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
    blob[0] = 0xDE; blob[1] = 0xAD; blob[2] = 0xBE; blob[3] = 0xEF;

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
    src.build_entity().with(TestPos{1.0f, 2.0f}).build();
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
    src.build_entity()
        .with(TestPos{7.0f, 8.0f})
        .with(TestHealth{5, 10})
        .build();
    auto blob = src.snapshot();

    // Destination does NOT register TestPos — should skip it gracefully.
    xebble::World dst;
    dst.register_serializable_component<TestHealth>();
    ASSERT_TRUE(dst.restore(blob).has_value());

    int hp_count = 0;
    dst.each<TestHealth>([&](xebble::Entity, TestHealth& h) {
        EXPECT_EQ(h.hp,     5);
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
    src.build_entity().with(xebble::Position{3.14f, 2.71f}).build();

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
