#include <xebble/ecs.hpp>
#include <xebble/system.hpp>
#include <xebble/world.hpp>

#include <gtest/gtest.h>

using namespace xebble;

TEST(Entity, DefaultIsNull) {
    Entity e{};
    EXPECT_EQ(e.id, 0u);
}

TEST(Entity, Equality) {
    Entity a{1};
    Entity b{1};
    Entity c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(EntityAllocator, CreateSequentialEntities) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    auto e1 = alloc.create();
    EXPECT_NE(e0, e1);
    EXPECT_TRUE(alloc.alive(e0));
    EXPECT_TRUE(alloc.alive(e1));
}

TEST(EntityAllocator, DestroyAndRecycleSlot) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    alloc.destroy(e0);
    EXPECT_FALSE(alloc.alive(e0));

    auto e1 = alloc.create();
    EXPECT_TRUE(alloc.alive(e1));
    EXPECT_NE(e0, e1);
}

TEST(EntityAllocator, StaleHandleNotAlive) {
    EntityAllocator alloc;
    auto e0 = alloc.create();
    alloc.destroy(e0);
    auto e1 = alloc.create();
    EXPECT_FALSE(alloc.alive(e0));
    EXPECT_TRUE(alloc.alive(e1));
}

TEST(ComponentPool, AddAndGet) {
    struct Pos {
        int x, y;
    };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    EXPECT_TRUE(pool.has(e));
    EXPECT_EQ(pool.get(e).x, 10);
    EXPECT_EQ(pool.get(e).y, 20);
}

TEST(ComponentPool, Remove) {
    struct Pos {
        int x, y;
    };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    pool.remove(e);
    EXPECT_FALSE(pool.has(e));
}

TEST(ComponentPool, IterateDenseArray) {
    struct Pos {
        int x, y;
    };
    ComponentPool<Pos> pool;
    Entity e0{0}, e1{1}, e2{2};
    pool.add(e0, Pos{1, 1});
    pool.add(e1, Pos{2, 2});
    pool.add(e2, Pos{3, 3});

    int sum = 0;
    for (size_t i = 0; i < pool.size(); i++) {
        sum += pool.dense_component(i).x;
    }
    EXPECT_EQ(sum, 6);
}

TEST(ComponentPool, RemoveSwapsLast) {
    struct Val {
        int v;
    };
    ComponentPool<Val> pool;
    Entity e0{0}, e1{1}, e2{2};
    pool.add(e0, Val{10});
    pool.add(e1, Val{20});
    pool.add(e2, Val{30});
    pool.remove(e0);
    EXPECT_EQ(pool.size(), 2u);
    EXPECT_TRUE(pool.has(e1));
    EXPECT_TRUE(pool.has(e2));
    EXPECT_EQ(pool.get(e1).v, 20);
    EXPECT_EQ(pool.get(e2).v, 30);
}

TEST(ComponentPool, ManyEntities) {
    struct Id {
        uint32_t v;
    };
    ComponentPool<Id> pool;
    for (uint32_t i = 0; i < 1000; i++) {
        pool.add(Entity{i}, Id{i * 2});
    }
    EXPECT_EQ(pool.size(), 1000u);
    for (uint32_t i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.get(Entity{i}).v, i * 2);
    }
}

// --- Test components (in anonymous namespace) ---
namespace {
struct Position {
    int x, y;
};
struct Velocity {
    int dx, dy;
};
struct Health {
    int hp;
};
} // namespace

TEST(World, CreateAndDestroyEntity) {
    World world;
    world.register_component<Position>();

    auto e = world.create_entity();
    EXPECT_TRUE(world.alive(e));
    world.add<Position>(e, {10, 20});
    EXPECT_TRUE(world.has<Position>(e));
    EXPECT_EQ(world.get<Position>(e).x, 10);

    world.destroy(e);
    world.flush_destroyed();
    EXPECT_FALSE(world.alive(e));
}

TEST(World, EntityBuilder) {
    World world;
    world.register_component<Position>();
    world.register_component<Velocity>();

    auto e = world.build_entity().with<Position>({5, 10}).with<Velocity>({1, -1}).build();

    EXPECT_TRUE(world.alive(e));
    EXPECT_EQ(world.get<Position>(e).x, 5);
    EXPECT_EQ(world.get<Velocity>(e).dx, 1);
}

TEST(World, EachSingleComponent) {
    World world;
    world.register_component<Position>();

    world.add<Position>(world.create_entity(), {1, 0});
    world.add<Position>(world.create_entity(), {2, 0});
    world.add<Position>(world.create_entity(), {3, 0});

    int sum = 0;
    world.each<Position>([&](Entity, Position& p) { sum += p.x; });
    EXPECT_EQ(sum, 6);
}

TEST(World, EachMultipleComponents) {
    World world;
    world.register_component<Position>();
    world.register_component<Velocity>();

    auto e0 = world.create_entity();
    world.add<Position>(e0, {1, 0});
    world.add<Velocity>(e0, {10, 0});

    auto e1 = world.create_entity();
    world.add<Position>(e1, {2, 0});
    // e1 has no Velocity

    int count = 0;
    world.each<Position, Velocity>([&](Entity, Position& p, Velocity& v) {
        count++;
        EXPECT_EQ(p.x, 1);
        EXPECT_EQ(v.dx, 10);
    });
    EXPECT_EQ(count, 1);
}

TEST(World, Resources) {
    struct GameState {
        int score;
    };

    World world;
    world.add_resource<GameState>(GameState{42});
    EXPECT_TRUE(world.has_resource<GameState>());
    EXPECT_EQ(world.resource<GameState>().score, 42);

    world.resource<GameState>().score = 100;
    EXPECT_EQ(world.resource<GameState>().score, 100);

    world.remove_resource<GameState>();
    EXPECT_FALSE(world.has_resource<GameState>());
}

TEST(World, SystemInitAndUpdate) {
    struct Counter {
        int value = 0;
    };

    struct CountSystem : System {
        void init(World& w) override { w.resource<Counter>().value = 10; }
        void update(World& w, float) override { w.resource<Counter>().value++; }
    };

    World world;
    world.add_resource<Counter>({});
    world.add_system<CountSystem>();
    world.init_systems();
    EXPECT_EQ(world.resource<Counter>().value, 10);

    world.tick_update(1.0f / 60.0f);
    EXPECT_EQ(world.resource<Counter>().value, 11);
}

TEST(World, PrependSystem) {
    struct OrderTracker {
        std::vector<int> order;
    };

    World world;
    world.add_resource<OrderTracker>({});

    struct SecondSystem : System {
        void update(World& w, float) override { w.resource<OrderTracker>().order.push_back(2); }
    };
    struct FirstSystem : System {
        void update(World& w, float) override { w.resource<OrderTracker>().order.push_back(1); }
    };

    world.add_system<SecondSystem>();
    world.prepend_system<FirstSystem>();

    world.init_systems();
    world.tick_update(0.0f);

    auto& result = world.resource<OrderTracker>().order;
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[1], 2);
}

TEST(World, DeferredDestruction) {
    World world;
    world.register_component<Position>();

    auto e = world.create_entity();
    world.add<Position>(e, {1, 2});
    world.destroy(e);

    // Still alive until flush
    EXPECT_TRUE(world.alive(e));
    EXPECT_TRUE(world.has<Position>(e));

    world.flush_destroyed();
    EXPECT_FALSE(world.alive(e));
}
