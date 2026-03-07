/// @file test_msglog.cpp
/// @brief Unit tests for xebble::MessageLog.
///
/// Tests cover:
///  - Construction and capacity.
///  - push(): basic insertion, color, category.
///  - Deduplication: repeated consecutive messages → "text ×N".
///  - Dedup resets when a different message is pushed.
///  - Capacity eviction: oldest message dropped when full.
///  - empty() / size() / clear().
///  - newest() / oldest() / operator[].
///  - visible(n): returns last n messages, pointer stability.
///  - filtered(category, max): ordering and count limiting.

#include <xebble/msglog.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using namespace xebble;

// Helper: compare u8string as string_view so GTest can print failure messages.
static std::string_view sv(const std::u8string& s) {
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
static std::string_view sv(std::u8string_view s) {
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(MessageLog, DefaultCapacity) {
    MessageLog log;
    EXPECT_EQ(log.capacity(), 100u);
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0u);
}

TEST(MessageLog, CustomCapacity) {
    MessageLog log(50);
    EXPECT_EQ(log.capacity(), 50u);
}

// ---------------------------------------------------------------------------
// Basic push / size / empty
// ---------------------------------------------------------------------------

TEST(MessageLog, PushOneMessage) {
    MessageLog log(10);
    log.push(u8"Hello, adventurer.");
    EXPECT_FALSE(log.empty());
    EXPECT_EQ(log.size(), 1u);
}

TEST(MessageLog, PushMultipleDistinct) {
    MessageLog log(10);
    log.push(u8"First");
    log.push(u8"Second");
    log.push(u8"Third");
    EXPECT_EQ(log.size(), 3u);
}

TEST(MessageLog, PushPreservesText) {
    MessageLog log(10);
    log.push(u8"You find a healing potion.");
    EXPECT_EQ(sv(log.newest().text), "You find a healing potion.");
}

TEST(MessageLog, PushPreservesColor) {
    MessageLog log(10);
    LogColor red{220, 80, 80, 255};
    log.push(u8"Ouch!", red);
    EXPECT_EQ(log.newest().color.r, 220);
    EXPECT_EQ(log.newest().color.g, 80);
    EXPECT_EQ(log.newest().color.b, 80);
    EXPECT_EQ(log.newest().color.a, 255);
}

TEST(MessageLog, PushPreservesCategory) {
    MessageLog log(10);
    log.push(u8"You descend.", {}, "story");
    EXPECT_EQ(log.newest().category, "story");
}

TEST(MessageLog, DefaultColorIsWhite) {
    MessageLog log(10);
    log.push(u8"Plain message.");
    LogColor white;
    EXPECT_EQ(log.newest().color, white);
}

TEST(MessageLog, InitialCountIsOne) {
    MessageLog log(10);
    log.push(u8"Something happens.");
    EXPECT_EQ(log.newest().count, 1);
}

// ---------------------------------------------------------------------------
// Deduplication
// ---------------------------------------------------------------------------

TEST(MessageLog, DuplicateCollapsesToSingleEntry) {
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    EXPECT_EQ(log.size(), 1u);
}

TEST(MessageLog, DuplicateIncrementsCount) {
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    EXPECT_EQ(log.newest().count, 3);
}

TEST(MessageLog, DuplicateTextHasMultiplierSuffix) {
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    // After two identical pushes the display text should contain "×2".
    EXPECT_NE(sv(log.newest().text).find("2"), std::string_view::npos);
}

TEST(MessageLog, DuplicateUpdatesColor) {
    // The color of the latest push should win.
    MessageLog log(10);
    LogColor white;
    LogColor red{220, 80, 80, 255};
    log.push(u8"You miss.", white);
    log.push(u8"You miss.", red);
    EXPECT_EQ(log.newest().color, red);
}

TEST(MessageLog, DedupResetsOnDifferentText) {
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    log.push(u8"You hit!");    // different text → new entry
    EXPECT_EQ(log.size(), 2u);
    EXPECT_EQ(log.newest().count, 1);
    EXPECT_EQ(sv(log.newest().text), "You hit!");
}

TEST(MessageLog, DedupResetsOnDifferentCategory) {
    // Same text but different category → two entries.
    MessageLog log(10);
    log.push(u8"Event", {}, "combat");
    log.push(u8"Event", {}, "story");
    EXPECT_EQ(log.size(), 2u);
}

TEST(MessageLog, DedupDoesNotCollapseAfterReset) {
    // miss → miss → hit → miss  should give 3 entries, last miss count=1.
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    log.push(u8"You hit!");
    log.push(u8"You miss.");
    EXPECT_EQ(log.size(), 3u);
    EXPECT_EQ(log.newest().count, 1);
}

// ---------------------------------------------------------------------------
// Capacity eviction
// ---------------------------------------------------------------------------

TEST(MessageLog, OldestDroppedWhenFull) {
    MessageLog log(3);
    log.push(u8"A");
    log.push(u8"B");
    log.push(u8"C");
    log.push(u8"D");
    EXPECT_EQ(log.size(), 3u);
    EXPECT_EQ(sv(log.oldest().text), "B");
}

TEST(MessageLog, CapacityOneKeepsNewest) {
    MessageLog log(1);
    log.push(u8"First");
    log.push(u8"Second");
    EXPECT_EQ(log.size(), 1u);
    EXPECT_EQ(sv(log.newest().text), "Second");
}

// ---------------------------------------------------------------------------
// clear()
// ---------------------------------------------------------------------------

TEST(MessageLog, ClearEmptiesLog) {
    MessageLog log(10);
    log.push(u8"A");
    log.push(u8"B");
    log.clear();
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0u);
}

TEST(MessageLog, ClearResetsDedup) {
    // After clear, the same text should be a fresh entry (count=1).
    MessageLog log(10);
    log.push(u8"You miss.");
    log.push(u8"You miss.");
    log.clear();
    log.push(u8"You miss.");
    EXPECT_EQ(log.size(), 1u);
    EXPECT_EQ(log.newest().count, 1);
}

// ---------------------------------------------------------------------------
// newest() / oldest() / operator[]
// ---------------------------------------------------------------------------

TEST(MessageLog, NewestIsLastPushed) {
    MessageLog log(10);
    log.push(u8"First");
    log.push(u8"Second");
    EXPECT_EQ(sv(log.newest().text), "Second");
}

TEST(MessageLog, OldestIsFirstPushed) {
    MessageLog log(10);
    log.push(u8"First");
    log.push(u8"Second");
    EXPECT_EQ(sv(log.oldest().text), "First");
}

TEST(MessageLog, IndexAccess) {
    MessageLog log(10);
    log.push(u8"Alpha");
    log.push(u8"Beta");
    log.push(u8"Gamma");
    EXPECT_EQ(sv(log[0].text), "Alpha");
    EXPECT_EQ(sv(log[1].text), "Beta");
    EXPECT_EQ(sv(log[2].text), "Gamma");
}

// ---------------------------------------------------------------------------
// visible(n)
// ---------------------------------------------------------------------------

TEST(MessageLog, VisibleReturnsLastN) {
    MessageLog log(10);
    log.push(u8"A");
    log.push(u8"B");
    log.push(u8"C");
    log.push(u8"D");
    auto v = log.visible(2);
    EXPECT_EQ(v.size(), 2u);
    EXPECT_EQ(sv(v[0]->text), "C");
    EXPECT_EQ(sv(v[1]->text), "D");
}

TEST(MessageLog, VisibleAllWhenFewerThanN) {
    MessageLog log(10);
    log.push(u8"X");
    log.push(u8"Y");
    auto v = log.visible(10);
    EXPECT_EQ(v.size(), 2u);
}

TEST(MessageLog, VisibleZeroReturnsEmpty) {
    MessageLog log(10);
    log.push(u8"A");
    auto v = log.visible(0);
    EXPECT_TRUE(v.empty());
}

TEST(MessageLog, VisibleOrderOldestFirst) {
    MessageLog log(10);
    log.push(u8"First");
    log.push(u8"Second");
    log.push(u8"Third");
    auto v = log.visible(3);
    EXPECT_EQ(sv(v[0]->text), "First");
    EXPECT_EQ(sv(v[2]->text), "Third");
}

TEST(MessageLog, VisiblePointersValid) {
    MessageLog log(10);
    log.push(u8"Hello");
    auto v = log.visible(5);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(sv(v[0]->text), "Hello");
}

// ---------------------------------------------------------------------------
// filtered(category, max)
// ---------------------------------------------------------------------------

TEST(MessageLog, FilteredReturnsOnlyMatchingCategory) {
    MessageLog log(20);
    log.push(u8"combat A", {}, "combat");
    log.push(u8"story 1",  {}, "story");
    log.push(u8"combat B", {}, "combat");
    log.push(u8"story 2",  {}, "story");

    auto combat = log.filtered("combat");
    EXPECT_EQ(combat.size(), 2u);
    for (auto* m : combat)
        EXPECT_EQ(m->category, "combat");
}

TEST(MessageLog, FilteredOrderOldestFirst) {
    MessageLog log(20);
    log.push(u8"first",  {}, "combat");
    log.push(u8"other",  {}, "story");
    log.push(u8"second", {}, "combat");

    auto v = log.filtered("combat");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(sv(v[0]->text), "first");
    EXPECT_EQ(sv(v[1]->text), "second");
}

TEST(MessageLog, FilteredRespectsMaxResults) {
    MessageLog log(20);
    for (int i = 0; i < 8; ++i)
        log.push(u8"hit", {}, "combat");
    // Each push after first deduplicates, so only 1 entry with count=8.
    // Use distinct texts to get multiple entries.
    MessageLog log2(20);
    log2.push(u8"hit 1", {}, "combat");
    log2.push(u8"hit 2", {}, "combat");
    log2.push(u8"hit 3", {}, "combat");
    log2.push(u8"hit 4", {}, "combat");
    log2.push(u8"hit 5", {}, "combat");

    auto v = log2.filtered("combat", 3);
    EXPECT_EQ(v.size(), 3u);
}

TEST(MessageLog, FilteredEmptyCategory) {
    MessageLog log(10);
    log.push(u8"A", {}, "foo");
    auto v = log.filtered("bar");
    EXPECT_TRUE(v.empty());
}

TEST(MessageLog, FilteredEmptyCategoryString) {
    // Messages pushed without category default to "".
    MessageLog log(10);
    log.push(u8"Plain A");
    log.push(u8"Plain B");
    log.push(u8"Categorized", {}, "cat");

    auto v = log.filtered("");
    EXPECT_EQ(v.size(), 2u);
}

// ---------------------------------------------------------------------------
// messages() raw access
// ---------------------------------------------------------------------------

TEST(MessageLog, MessagesDequeSize) {
    MessageLog log(10);
    log.push(u8"one");
    log.push(u8"two");
    EXPECT_EQ(log.messages().size(), 2u);
}
