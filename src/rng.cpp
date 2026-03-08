/// @file rng.cpp
/// @brief Non-template implementations for Rng: dice-expression parser and
///        weighted index selection.
#include <xebble/rng.hpp>

#include <charconv>
#include <numeric>
#include <stdexcept>

namespace xebble {

// ---------------------------------------------------------------------------
// Dice expression parser
// ---------------------------------------------------------------------------
// Grammar (case-insensitive 'd'):
//   expr  ::= [count] 'd' faces [modifier]
//   count ::= integer  (default 1 if absent)
//   faces ::= integer  (>= 1)
//   modifier ::= ('+' | '-') integer

int32_t Rng::roll(std::string_view expr) {
    if (expr.empty())
        throw std::invalid_argument("Rng::roll: empty expression");

    const char* p = expr.data();
    const char* end = p + expr.size();

    // Helper: parse an unsigned decimal integer at *p, advance p.
    auto parse_uint = [&](int32_t& out) -> bool {
        if (p >= end || *p < '0' || *p > '9')
            return false;
        auto [ptr, ec] = std::from_chars(p, end, out);
        if (ec != std::errc{})
            return false;
        p = ptr;
        return true;
    };

    // Optional count before 'd'.
    int32_t count = 1;
    if (p < end && *p != 'd' && *p != 'D') {
        if (!parse_uint(count) || count < 1)
            throw std::invalid_argument("Rng::roll: invalid die count in '" + std::string(expr) +
                                        '\'');
    }

    // 'd' separator.
    if (p >= end || (*p != 'd' && *p != 'D'))
        throw std::invalid_argument("Rng::roll: missing 'd' in '" + std::string(expr) + '\'');
    ++p;

    // Face count.
    int32_t faces = 0;
    if (!parse_uint(faces) || faces < 1)
        throw std::invalid_argument("Rng::roll: invalid face count in '" + std::string(expr) +
                                    '\'');

    // Optional modifier.
    int32_t modifier = 0;
    if (p < end) {
        if (*p != '+' && *p != '-')
            throw std::invalid_argument("Rng::roll: unexpected character '" + std::string(1, *p) +
                                        "' in '" + std::string(expr) + '\'');
        char sign = *p++;
        int32_t mag = 0;
        if (!parse_uint(mag))
            throw std::invalid_argument("Rng::roll: missing modifier value in '" +
                                        std::string(expr) + '\'');
        modifier = (sign == '+') ? mag : -mag;
    }

    // Trailing garbage.
    if (p != end)
        throw std::invalid_argument("Rng::roll: trailing characters in '" + std::string(expr) +
                                    '\'');

    return roll_dice(count, faces) + modifier;
}

// ---------------------------------------------------------------------------
// Weighted index selection
// ---------------------------------------------------------------------------

size_t Rng::weighted_index(std::span<const float> weights) {
    if (weights.empty())
        throw std::invalid_argument("Rng::weighted_index: weights must not be empty");

    float total = 0.0f;
    for (float w : weights) {
        if (w < 0.0f)
            throw std::invalid_argument("Rng::weighted_index: negative weight");
        total += w;
    }

    // All-zero fallback: uniform selection.
    if (total == 0.0f)
        return static_cast<size_t>(range(0, static_cast<int32_t>(weights.size()) - 1));

    float target = next_float() * total;
    float accum = 0.0f;
    for (size_t i = 0; i < weights.size(); ++i) {
        accum += weights[i];
        if (target < accum)
            return i;
    }

    // Floating-point rounding edge case: return the last non-zero entry.
    for (size_t i = weights.size(); i-- > 0;)
        if (weights[i] > 0.0f)
            return i;

    return weights.size() - 1;
}

} // namespace xebble
