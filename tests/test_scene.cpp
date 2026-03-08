/// @file test_scene.cpp
/// @brief Unit tests for SceneTransition, SceneRouter, and SceneStack.
///
/// These tests exercise the scene stack logic without a GPU / window.
/// They use a stub injector (no-op lambda) since inject_and_init is only
/// needed for GPU resources, which don't exist in the test environment.

#include <xebble/scene.hpp>

#include <gtest/gtest.h>

#include <any>
#include <string>
#include <vector>

using namespace xebble;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Counter resource placed on a world so we can verify which world is active.
struct SceneId {
    std::string name;
};

/// Tick counter resource — incremented by a simple update system.
struct TickCount {
    int value = 0;
};

/// Draw counter resource — incremented by a simple draw system.
struct DrawCount {
    int value = 0;
};

/// No-op injector: does not inject GPU resources (not available in tests).
static void noop_injector(World& /*w*/) {}

/// Build a minimal test world tagged with @p name.
static World make_world(const std::string& name, std::any /*payload*/ = {}) {
    World w;
    w.add_resource<SceneId>(SceneId{name});
    w.add_resource<SceneTransition>(SceneTransition{});
    w.add_resource<TickCount>(TickCount{});
    return w;
}

/// Build a router with scenes "a", "b", "c" that record their name.
static SceneRouter make_abc_router() {
    SceneRouter r;
    r.add_scene("a", [](std::any p) { return make_world("a", p); });
    r.add_scene("b", [](std::any p) { return make_world("b", p); });
    r.add_scene("c", [](std::any p) { return make_world("c", p); });
    r.set_initial("a");
    return r;
}

// ---------------------------------------------------------------------------
// SceneTransition factory methods
// ---------------------------------------------------------------------------

TEST(SceneTransition, NoneIsNotPending) {
    auto t = SceneTransition::none();
    EXPECT_FALSE(t.pending());
    EXPECT_EQ(t.kind, SceneTransition::Kind::None);
}

TEST(SceneTransition, PushIsPending) {
    auto t = SceneTransition::push("gameplay");
    EXPECT_TRUE(t.pending());
    EXPECT_EQ(t.kind, SceneTransition::Kind::Push);
    EXPECT_EQ(t.scene_name, "gameplay");
    EXPECT_EQ(t.draw_below, DrawBelow::No);
}

TEST(SceneTransition, PushWithDrawBelow) {
    auto t = SceneTransition::push("pause", {}, DrawBelow::Yes);
    EXPECT_EQ(t.draw_below, DrawBelow::Yes);
}

TEST(SceneTransition, PushWithPayload) {
    auto t = SceneTransition::push("gameplay", 42);
    EXPECT_TRUE(t.payload.has_value());
    EXPECT_EQ(std::any_cast<int>(t.payload), 42);
}

TEST(SceneTransition, PopIsPending) {
    auto t = SceneTransition::pop();
    EXPECT_TRUE(t.pending());
    EXPECT_EQ(t.kind, SceneTransition::Kind::Pop);
}

TEST(SceneTransition, PopWithPayload) {
    auto t = SceneTransition::pop(std::string("selected_item"));
    EXPECT_EQ(std::any_cast<std::string>(t.payload), "selected_item");
}

TEST(SceneTransition, Replace) {
    auto t = SceneTransition::replace("game_over");
    EXPECT_TRUE(t.pending());
    EXPECT_EQ(t.kind, SceneTransition::Kind::Replace);
    EXPECT_EQ(t.scene_name, "game_over");
}

TEST(SceneTransition, PopTo) {
    auto t = SceneTransition::pop_to("title");
    EXPECT_EQ(t.kind, SceneTransition::Kind::PopTo);
    EXPECT_EQ(t.scene_name, "title");
}

TEST(SceneTransition, PopAllAndPush) {
    auto t = SceneTransition::pop_all_and_push("gameplay", 99);
    EXPECT_EQ(t.kind, SceneTransition::Kind::PopAllAndPush);
    EXPECT_EQ(t.scene_name, "gameplay");
    EXPECT_EQ(std::any_cast<int>(t.payload), 99);
}

// ---------------------------------------------------------------------------
// SceneRouter
// ---------------------------------------------------------------------------

TEST(SceneRouter, AddSceneAndHasScene) {
    SceneRouter r;
    r.add_scene("title", [](std::any) { return World{}; });
    EXPECT_TRUE(r.has_scene("title"));
    EXPECT_FALSE(r.has_scene("gameplay"));
}

TEST(SceneRouter, SetAndGetInitialName) {
    SceneRouter r;
    r.add_scene("title", [](std::any) { return World{}; });
    r.set_initial("title");
    EXPECT_EQ(r.initial_name(), "title");
}

TEST(SceneRouter, BuildInvokesFactory) {
    SceneRouter r;
    bool called = false;
    r.add_scene("test", [&](std::any) {
        called = true;
        return World{};
    });
    r.set_initial("test");
    World w = r.build("test");
    EXPECT_TRUE(called);
}

TEST(SceneRouter, BuildForwardsPayload) {
    SceneRouter r;
    int received = 0;
    r.add_scene("scene", [&](std::any p) {
        if (p.has_value())
            received = std::any_cast<int>(p);
        return World{};
    });
    r.set_initial("scene", 7);
    (void)r.build_initial();
    EXPECT_EQ(received, 7);
}

TEST(SceneRouter, BuildInitialUsesInitialFactory) {
    SceneRouter r;
    r.add_scene("a", [](std::any) {
        World w;
        w.add_resource<SceneId>(SceneId{"a"});
        return w;
    });
    r.set_initial("a");
    World w = r.build_initial();
    EXPECT_EQ(w.resource<SceneId>().name, "a");
}

// ---------------------------------------------------------------------------
// SceneStack — basic push / pop / replace
// ---------------------------------------------------------------------------

TEST(SceneStack, PushInitialNotEmpty) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);
    EXPECT_FALSE(stack.empty());
}

TEST(SceneStack, PushInitialTopIsInitialScene) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);
    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "a");
}

TEST(SceneStack, PushTransitionAddsScene) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);

    stack.top_world().resource<SceneTransition>() = SceneTransition::push("b");
    stack.apply_transition(noop_injector);

    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "b");
}

TEST(SceneStack, PopTransitionRestoresPrevious) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);

    // Push "b".
    stack.top_world().resource<SceneTransition>() = SceneTransition::push("b");
    stack.apply_transition(noop_injector);
    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "b");

    // Pop back to "a".
    stack.top_world().resource<SceneTransition>() = SceneTransition::pop();
    stack.apply_transition(noop_injector);
    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "a");
}

TEST(SceneStack, PopEmptiesStackWhenOnlyOneScene) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);

    stack.top_world().resource<SceneTransition>() = SceneTransition::pop();
    stack.apply_transition(noop_injector);
    EXPECT_TRUE(stack.empty());
}

TEST(SceneStack, ReplaceSwapsScene) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector); // "a"

    stack.top_world().resource<SceneTransition>() = SceneTransition::replace("c");
    stack.apply_transition(noop_injector);

    // Stack depth stays 1, top is "c".
    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "c");

    // Pop should now empty the stack (there's no "a" underneath).
    stack.top_world().resource<SceneTransition>() = SceneTransition::pop();
    stack.apply_transition(noop_injector);
    EXPECT_TRUE(stack.empty());
}

TEST(SceneStack, PopToRemovesIntermediateScenes) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector); // "a"

    stack.top_world().resource<SceneTransition>() = SceneTransition::push("b");
    stack.apply_transition(noop_injector); // stack: a, b

    stack.top_world().resource<SceneTransition>() = SceneTransition::push("c");
    stack.apply_transition(noop_injector); // stack: a, b, c

    // Pop all the way back to "a".
    stack.top_world().resource<SceneTransition>() = SceneTransition::pop_to("a");
    stack.apply_transition(noop_injector); // stack: a

    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "a");

    // Only one frame left.
    stack.top_world().resource<SceneTransition>() = SceneTransition::pop();
    stack.apply_transition(noop_injector);
    EXPECT_TRUE(stack.empty());
}

TEST(SceneStack, PopAllAndPushClearsAndPushes) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector); // "a"

    stack.top_world().resource<SceneTransition>() = SceneTransition::push("b");
    stack.apply_transition(noop_injector); // a, b
    stack.top_world().resource<SceneTransition>() = SceneTransition::push("c");
    stack.apply_transition(noop_injector); // a, b, c

    stack.top_world().resource<SceneTransition>() = SceneTransition::pop_all_and_push("a");
    stack.apply_transition(noop_injector); // only "a"

    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "a");

    // Exactly one frame.
    stack.top_world().resource<SceneTransition>() = SceneTransition::pop();
    stack.apply_transition(noop_injector);
    EXPECT_TRUE(stack.empty());
}

TEST(SceneStack, NoPendingTransitionIsNoop) {
    auto router = make_abc_router();
    SceneStack stack(router);
    stack.push_initial(noop_injector);

    // No transition posted — top should still be "a".
    stack.apply_transition(noop_injector);
    EXPECT_EQ(stack.top_world().resource<SceneId>().name, "a");
}

TEST(SceneStack, PayloadForwardedToNewScene) {
    SceneRouter router;
    router.add_scene("a", [](std::any) { return make_world("a"); });
    router.add_scene("b", [](std::any p) {
        World w = make_world("b");
        if (p.has_value())
            w.add_resource<int>(std::any_cast<int>(p));
        return w;
    });
    router.set_initial("a");

    SceneStack stack(router);
    stack.push_initial(noop_injector);

    stack.top_world().resource<SceneTransition>() = SceneTransition::push("b", 42);
    stack.apply_transition(noop_injector);

    EXPECT_EQ(std::any_cast<int>(stack.top_world().resource<int>()), 42);
}
