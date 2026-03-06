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
