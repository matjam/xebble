/// @file test_turn.cpp
/// @brief Unit tests for AlternatingScheduler, EnergyScheduler, InitiativeScheduler.

#include <xebble/turn.hpp>

#include <gtest/gtest.h>

using namespace xebble;

// ---------------------------------------------------------------------------
// AlternatingScheduler
// ---------------------------------------------------------------------------

TEST(AlternatingScheduler, StartsOnPlayerTurn) {
    AlternatingScheduler s;
    EXPECT_TRUE(s.is_player_turn());
    EXPECT_FALSE(s.is_monster_turn());
    EXPECT_EQ(s.phase(), AlternatingScheduler::Phase::Player);
}

TEST(AlternatingScheduler, EndPlayerTurnSwitchesToMonsters) {
    AlternatingScheduler s;
    s.end_player_turn();
    EXPECT_TRUE(s.is_monster_turn());
    EXPECT_FALSE(s.is_player_turn());
}

TEST(AlternatingScheduler, EndMonsterTurnSwitchesToPlayer) {
    AlternatingScheduler s;
    s.end_player_turn();
    s.end_monster_turn();
    EXPECT_TRUE(s.is_player_turn());
}

TEST(AlternatingScheduler, TurnCounterIncrements) {
    AlternatingScheduler s;
    EXPECT_EQ(s.turn(), 0u);
    s.end_player_turn();
    EXPECT_EQ(s.turn(), 1u);
    s.end_monster_turn();
    s.end_player_turn();
    EXPECT_EQ(s.turn(), 2u);
}

TEST(AlternatingScheduler, FullCycle) {
    AlternatingScheduler s;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(s.is_player_turn());
        s.end_player_turn();
        EXPECT_TRUE(s.is_monster_turn());
        s.end_monster_turn();
    }
    EXPECT_EQ(s.turn(), 5u);
}

// ---------------------------------------------------------------------------
// EnergyScheduler
// ---------------------------------------------------------------------------

TEST(EnergyScheduler, DefaultThreshold100) {
    EnergyScheduler s;
    EXPECT_EQ(s.threshold(), 100);
}

TEST(EnergyScheduler, NoActorsNoneReady) {
    EnergyScheduler s;
    s.tick();
    EXPECT_FALSE(s.has_ready());
}

TEST(EnergyScheduler, ActorReadyAfterEnoughTicks) {
    EnergyScheduler s(10);
    s.add_actor(1, 5); // gains 5 per tick, needs 10
    EXPECT_FALSE(s.has_ready());
    s.tick();
    EXPECT_FALSE(s.has_ready());
    s.tick(); // 10 energy total
    EXPECT_TRUE(s.has_ready());
    EXPECT_EQ(s.next_actor(), 1u);
}

TEST(EnergyScheduler, EndTurnDeductsEnergy) {
    EnergyScheduler s(10);
    s.add_actor(1, 10);
    s.tick(); // 10 energy
    EXPECT_TRUE(s.has_ready());
    s.end_turn(1); // deduct 10
    EXPECT_FALSE(s.has_ready());
}

TEST(EnergyScheduler, FasterActorGoesFirst) {
    EnergyScheduler s(10);
    s.add_actor(1, 5);
    s.add_actor(2, 10);
    s.tick();
    EXPECT_TRUE(s.has_ready());
    EXPECT_EQ(s.next_actor(), 2u); // actor 2 has 10 energy; actor 1 has only 5
}

TEST(EnergyScheduler, RemoveActor) {
    EnergyScheduler s(10);
    s.add_actor(1, 10);
    s.add_actor(2, 10);
    EXPECT_EQ(s.size(), 2u);
    s.remove_actor(1);
    EXPECT_EQ(s.size(), 1u);
    s.tick();
    EXPECT_EQ(s.next_actor(), 2u);
}

TEST(EnergyScheduler, RemoveNonExistentActorNoOp) {
    EnergyScheduler s;
    s.add_actor(1, 5);
    s.remove_actor(99);
    EXPECT_EQ(s.size(), 1u);
}

TEST(EnergyScheduler, CustomActionCost) {
    EnergyScheduler s(10);
    s.add_actor(1, 10);
    s.tick(); // actor gains 10 energy → ready (10 >= threshold 10)
    EXPECT_TRUE(s.has_ready());
    s.end_turn(1, 5);            // cheap action costs 5; remaining energy = 10 - 5 = 5 < 10
    EXPECT_FALSE(s.has_ready()); // not enough energy for another action
}

TEST(EnergyScheduler, SetThreshold) {
    EnergyScheduler s(10);
    s.set_threshold(20);
    EXPECT_EQ(s.threshold(), 20);
    s.add_actor(1, 10);
    s.tick(); // 10 — not enough for 20
    EXPECT_FALSE(s.has_ready());
    s.tick(); // 20 — ready
    EXPECT_TRUE(s.has_ready());
}

// ---------------------------------------------------------------------------
// InitiativeScheduler
// ---------------------------------------------------------------------------

TEST(InitiativeScheduler, StartsEmpty) {
    InitiativeScheduler s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
}

TEST(InitiativeScheduler, PushAndCurrent) {
    InitiativeScheduler s;
    s.push(1, 15);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.current(), 1u);
}

TEST(InitiativeScheduler, HigherInitiativeGoesFirst) {
    InitiativeScheduler s;
    s.push(1, 5);
    s.push(2, 18);
    s.push(3, 12);
    EXPECT_EQ(s.current(), 2u); // highest initiative
}

TEST(InitiativeScheduler, AdvanceWraps) {
    InitiativeScheduler s;
    s.push(1, 10);
    s.push(2, 20);
    // Order: 2 (20), 1 (10)
    EXPECT_EQ(s.current(), 2u);
    s.advance();
    EXPECT_EQ(s.current(), 1u);
    s.advance();
    EXPECT_EQ(s.current(), 2u); // wrapped back
}

TEST(InitiativeScheduler, RemoveCurrent) {
    InitiativeScheduler s;
    s.push(1, 20);
    s.push(2, 10);
    // Order: 1 (20), 2 (10); current = 1
    s.remove(1);
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(s.current(), 2u);
}

TEST(InitiativeScheduler, RemoveNonCurrent) {
    InitiativeScheduler s;
    s.push(1, 20);
    s.push(2, 10);
    s.remove(2);
    EXPECT_EQ(s.size(), 1u);
    EXPECT_EQ(s.current(), 1u);
}

TEST(InitiativeScheduler, RemoveAllBecomesEmpty) {
    InitiativeScheduler s;
    s.push(1, 5);
    s.remove(1);
    EXPECT_TRUE(s.empty());
}

TEST(InitiativeScheduler, SortedOrder) {
    InitiativeScheduler s;
    s.push(3, 8);
    s.push(1, 20);
    s.push(2, 14);
    // Expected order: 1(20), 2(14), 3(8)
    EXPECT_EQ(s.entries()[0].id, 1u);
    EXPECT_EQ(s.entries()[1].id, 2u);
    EXPECT_EQ(s.entries()[2].id, 3u);
}

TEST(InitiativeScheduler, CurrentInitiative) {
    InitiativeScheduler s;
    s.push(1, 17);
    EXPECT_EQ(s.current_initiative(), 17);
}
