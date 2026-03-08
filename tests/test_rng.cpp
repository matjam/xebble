/// @file test_rng.cpp
/// @brief Unit tests for xebble::Rng (PCG32 RNG).
///
/// Tests cover:
///  - Determinism: same seed → same sequence.
///  - Divergence: different seeds → different sequences.
///  - State save/restore: snapshot and replay produce identical output.
///  - range(): bounds, single-value degenerate case, negative ranges.
///  - roll_die() / roll_dice(): output within expected bounds.
///  - roll(): dice expression parser — valid and invalid expressions.
///  - weighted_index(): selection proportional to weights, all-zero fallback.
///  - weighted_choice(): correct value returned.
///  - shuffle(): length preserved, elements permuted.
///  - coin_flip(): returns only true/false.
///  - one_in(): fires with roughly the expected frequency.
///  - chance(): fires with roughly the expected probability.
///  - next_u32 / next_u64 / next_float / next_double: primitive output ranges.

#include <xebble/rng.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace xebble;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Run @p fn @p trials times; return the fraction where fn returns true.
static double sample_rate(int trials, auto fn) {
    int hits = 0;
    for (int i = 0; i < trials; ++i)
        if (fn())
            ++hits;
    return static_cast<double>(hits) / trials;
}

// ---------------------------------------------------------------------------
// Construction & determinism
// ---------------------------------------------------------------------------

TEST(Rng, SameSeedSameSequence) {
    Rng a(42), b(42);
    for (int i = 0; i < 200; ++i)
        EXPECT_EQ(a.next_u32(), b.next_u32());
}

TEST(Rng, DifferentSeedsDifferentSequence) {
    Rng a(1), b(2);
    bool any_diff = false;
    for (int i = 0; i < 100; ++i)
        if (a.next_u32() != b.next_u32()) {
            any_diff = true;
            break;
        }
    EXPECT_TRUE(any_diff);
}

TEST(Rng, DefaultSeedProducesOutput) {
    Rng rng; // seed 0
    // Must not crash, and should not hang.
    (void)rng.next_u32();
}

// ---------------------------------------------------------------------------
// State save / restore
// ---------------------------------------------------------------------------

TEST(Rng, SaveRestoreReproducesSequence) {
    Rng rng(99);
    RngState snap = rng.save();

    std::vector<uint32_t> first;
    for (int i = 0; i < 50; ++i)
        first.push_back(rng.next_u32());

    rng.restore(snap);

    std::vector<uint32_t> second;
    for (int i = 0; i < 50; ++i)
        second.push_back(rng.next_u32());

    EXPECT_EQ(first, second);
}

TEST(Rng, ConstructFromState) {
    Rng rng(7);
    RngState snap = rng.save();

    Rng rng2(snap);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(rng.next_u32(), rng2.next_u32());
}

TEST(Rng, SavedStateEquality) {
    Rng a(42), b(42);
    EXPECT_EQ(a.save(), b.save());
    a.next_u32();
    EXPECT_NE(a.save(), b.save());
}

// ---------------------------------------------------------------------------
// Primitive outputs
// ---------------------------------------------------------------------------

TEST(Rng, NextFloatInUnitInterval) {
    Rng rng(1);
    for (int i = 0; i < 1000; ++i) {
        float f = rng.next_float();
        EXPECT_GE(f, 0.0f);
        EXPECT_LT(f, 1.0f);
    }
}

TEST(Rng, NextDoubleInUnitInterval) {
    Rng rng(2);
    for (int i = 0; i < 1000; ++i) {
        double d = rng.next_double();
        EXPECT_GE(d, 0.0);
        EXPECT_LT(d, 1.0);
    }
}

TEST(Rng, NextU64CoversBothHalves) {
    Rng rng(3);
    bool high_set = false;
    for (int i = 0; i < 200; ++i) {
        uint64_t v = rng.next_u64();
        if (v >> 32) {
            high_set = true;
            break;
        }
    }
    EXPECT_TRUE(high_set);
}

// ---------------------------------------------------------------------------
// range()
// ---------------------------------------------------------------------------

TEST(Rng, RangeWithinBounds) {
    Rng rng(10);
    for (int i = 0; i < 1000; ++i) {
        int v = rng.range(-5, 5);
        EXPECT_GE(v, -5);
        EXPECT_LE(v, 5);
    }
}

TEST(Rng, RangeSingleValue) {
    Rng rng(11);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(rng.range(7, 7), 7);
}

TEST(Rng, RangeZeroToMax) {
    Rng rng(12);
    for (int i = 0; i < 1000; ++i) {
        int v = rng.range(9);
        EXPECT_GE(v, 0);
        EXPECT_LE(v, 9);
    }
}

TEST(Rng, RangeCoversBothEnds) {
    Rng rng(13);
    bool saw_min = false, saw_max = false;
    for (int i = 0; i < 10000 && !(saw_min && saw_max); ++i) {
        int v = rng.range(0, 1);
        if (v == 0)
            saw_min = true;
        if (v == 1)
            saw_max = true;
    }
    EXPECT_TRUE(saw_min);
    EXPECT_TRUE(saw_max);
}

// ---------------------------------------------------------------------------
// Dice — roll_die / roll_dice
// ---------------------------------------------------------------------------

TEST(Rng, RollDieWithinBounds) {
    Rng rng(20);
    for (int i = 0; i < 1000; ++i) {
        int v = rng.roll_die(6);
        EXPECT_GE(v, 1);
        EXPECT_LE(v, 6);
    }
}

TEST(Rng, RollDieD20) {
    Rng rng(21);
    for (int i = 0; i < 1000; ++i) {
        int v = rng.roll_die(20);
        EXPECT_GE(v, 1);
        EXPECT_LE(v, 20);
    }
}

TEST(Rng, RollDieOneFacedAlwaysOne) {
    Rng rng(22);
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(rng.roll_die(1), 1);
}

TEST(Rng, RollDiceSum) {
    Rng rng(30);
    for (int i = 0; i < 1000; ++i) {
        int v = rng.roll_dice(3, 6);
        EXPECT_GE(v, 3);
        EXPECT_LE(v, 18);
    }
}

// ---------------------------------------------------------------------------
// Dice expression parser — roll()
// ---------------------------------------------------------------------------

TEST(Rng, RollExprSimple) {
    Rng rng(40);
    for (int i = 0; i < 500; ++i) {
        int v = rng.roll("1d6");
        EXPECT_GE(v, 1);
        EXPECT_LE(v, 6);
    }
}

TEST(Rng, RollExprOmittedCount) {
    Rng rng(41);
    for (int i = 0; i < 500; ++i) {
        int v = rng.roll("d20");
        EXPECT_GE(v, 1);
        EXPECT_LE(v, 20);
    }
}

TEST(Rng, RollExprPositiveModifier) {
    Rng rng(42);
    for (int i = 0; i < 500; ++i) {
        int v = rng.roll("2d6+3");
        EXPECT_GE(v, 5);  // 2*1 + 3
        EXPECT_LE(v, 15); // 2*6 + 3
    }
}

TEST(Rng, RollExprNegativeModifier) {
    Rng rng(43);
    for (int i = 0; i < 500; ++i) {
        int v = rng.roll("1d20-2");
        EXPECT_GE(v, -1); // 1 - 2
        EXPECT_LE(v, 18); // 20 - 2
    }
}

TEST(Rng, RollExprUpperCaseD) {
    Rng rng(44);
    // Should not throw.
    int v = rng.roll("3D6");
    EXPECT_GE(v, 3);
    EXPECT_LE(v, 18);
}

TEST(Rng, RollExprDeterministic) {
    Rng a(50), b(50);
    for (int i = 0; i < 100; ++i)
        EXPECT_EQ(a.roll("4d6+2"), b.roll("4d6+2"));
}

TEST(Rng, RollExprEmpty_Throws) {
    Rng rng(60);
    EXPECT_THROW(rng.roll(""), std::invalid_argument);
}

TEST(Rng, RollExprMissingD_Throws) {
    Rng rng(61);
    EXPECT_THROW(rng.roll("3"), std::invalid_argument);
}

TEST(Rng, RollExprMissingFaces_Throws) {
    Rng rng(62);
    EXPECT_THROW(rng.roll("3d"), std::invalid_argument);
}

TEST(Rng, RollExprTrailingGarbage_Throws) {
    Rng rng(63);
    EXPECT_THROW(rng.roll("2d6x"), std::invalid_argument);
}

TEST(Rng, RollExprMissingModValue_Throws) {
    Rng rng(64);
    EXPECT_THROW(rng.roll("1d6+"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Weighted selection
// ---------------------------------------------------------------------------

TEST(Rng, WeightedIndexInRange) {
    Rng rng(70);
    std::vector<float> w = {1.0f, 2.0f, 3.0f};
    for (int i = 0; i < 1000; ++i) {
        size_t idx = rng.weighted_index(w);
        EXPECT_LT(idx, w.size());
    }
}

TEST(Rng, WeightedIndexHighWeightFavoured) {
    // Weight 90 vs 10 — the heavy side should win >> 50% of the time.
    Rng rng(71);
    std::vector<float> w = {10.0f, 90.0f};
    int hits1 = 0;
    const int N = 5000;
    for (int i = 0; i < N; ++i)
        if (rng.weighted_index(w) == 1)
            ++hits1;
    // Expect roughly 90% — allow generous tolerance.
    EXPECT_GT(hits1, N * 70 / 100);
}

TEST(Rng, WeightedIndexAllZeroFallback) {
    Rng rng(72);
    std::vector<float> w = {0.0f, 0.0f, 0.0f};
    // Must not throw or loop forever.
    size_t idx = rng.weighted_index(w);
    EXPECT_LT(idx, w.size());
}

TEST(Rng, WeightedIndexEmptyThrows) {
    Rng rng(73);
    std::vector<float> w;
    EXPECT_THROW(rng.weighted_index(w), std::invalid_argument);
}

TEST(Rng, WeightedIndexNegativeWeightThrows) {
    Rng rng(74);
    std::vector<float> w = {1.0f, -1.0f, 2.0f};
    EXPECT_THROW(rng.weighted_index(w), std::invalid_argument);
}

TEST(Rng, WeightedChoiceReturnsCorrectValue) {
    Rng rng(80);
    std::vector<float> weights = {0.0f, 0.0f, 100.0f};
    std::vector<std::string> values = {"a", "b", "c"};
    // Only "c" has non-zero weight — always selected.
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(rng.weighted_choice(weights, values), "c");
}

TEST(Rng, WeightedChoiceSizeMismatchThrows) {
    Rng rng(81);
    std::vector<float> w = {1.0f, 2.0f};
    std::vector<int> v = {10};
    EXPECT_THROW(rng.weighted_choice(w, v), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Shuffle
// ---------------------------------------------------------------------------

TEST(Rng, ShufflePreservesElements) {
    Rng rng(90);
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<int> orig = v;

    rng.shuffle(v);

    ASSERT_EQ(v.size(), orig.size());
    std::sort(v.begin(), v.end());
    std::sort(orig.begin(), orig.end());
    EXPECT_EQ(v, orig);
}

TEST(Rng, ShuffleProducesPermutation) {
    // Run many shuffles and verify at least one is different from the original.
    Rng rng(91);
    std::vector<int> orig = {1, 2, 3, 4, 5};
    bool any_diff = false;
    for (int t = 0; t < 100; ++t) {
        std::vector<int> v = orig;
        rng.shuffle(v);
        if (v != orig) {
            any_diff = true;
            break;
        }
    }
    EXPECT_TRUE(any_diff);
}

TEST(Rng, ShuffleEmptyVectorNoOp) {
    Rng rng(92);
    std::vector<int> v;
    rng.shuffle(v); // must not crash
    EXPECT_TRUE(v.empty());
}

TEST(Rng, ShuffleSingleElementNoOp) {
    Rng rng(93);
    std::vector<int> v = {42};
    rng.shuffle(v);
    EXPECT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0], 42);
}

// ---------------------------------------------------------------------------
// Convenience helpers
// ---------------------------------------------------------------------------

TEST(Rng, CoinFlipOnlyTrueOrFalse) {
    Rng rng(100);
    for (int i = 0; i < 200; ++i) {
        bool v = rng.coin_flip();
        EXPECT_TRUE(v == true || v == false);
    }
}

TEST(Rng, CoinFlipRoughlyHalf) {
    Rng rng(101);
    double rate = sample_rate(10000, [&] { return rng.coin_flip(); });
    EXPECT_GT(rate, 0.45);
    EXPECT_LT(rate, 0.55);
}

TEST(Rng, OneInBounds) {
    Rng rng(110);
    // one_in(1) should always fire.
    for (int i = 0; i < 50; ++i)
        EXPECT_TRUE(rng.one_in(1));
}

TEST(Rng, OneInRoughlyCorrectRate) {
    Rng rng(111);
    // one_in(10) → ~10%.
    double rate = sample_rate(10000, [&] { return rng.one_in(10); });
    EXPECT_GT(rate, 0.07);
    EXPECT_LT(rate, 0.13);
}

TEST(Rng, ChanceNeverFires) {
    Rng rng(120);
    for (int i = 0; i < 1000; ++i)
        EXPECT_FALSE(rng.chance(0.0f));
}

TEST(Rng, ChanceAlwaysFires) {
    Rng rng(121);
    for (int i = 0; i < 1000; ++i)
        EXPECT_TRUE(rng.chance(1.0f));
}

TEST(Rng, ChanceRoughlyCorrectRate) {
    Rng rng(122);
    double rate = sample_rate(10000, [&] { return rng.chance(0.25f); });
    EXPECT_GT(rate, 0.22);
    EXPECT_LT(rate, 0.28);
}
