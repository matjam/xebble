#include <gtest/gtest.h>
#include <xebble/ecs.hpp>

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
    struct Pos { int x, y; };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    EXPECT_TRUE(pool.has(e));
    EXPECT_EQ(pool.get(e).x, 10);
    EXPECT_EQ(pool.get(e).y, 20);
}

TEST(ComponentPool, Remove) {
    struct Pos { int x, y; };
    ComponentPool<Pos> pool;
    Entity e{1};
    pool.add(e, Pos{10, 20});
    pool.remove(e);
    EXPECT_FALSE(pool.has(e));
}

TEST(ComponentPool, IterateDenseArray) {
    struct Pos { int x, y; };
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
    struct Val { int v; };
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
    struct Id { uint32_t v; };
    ComponentPool<Id> pool;
    for (uint32_t i = 0; i < 1000; i++) {
        pool.add(Entity{i}, Id{i * 2});
    }
    EXPECT_EQ(pool.size(), 1000u);
    for (uint32_t i = 0; i < 1000; i++) {
        EXPECT_EQ(pool.get(Entity{i}).v, i * 2);
    }
}
