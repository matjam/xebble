/// @file utf8.hpp
/// @brief Minimal UTF-8 codepoint decoder.
///
/// Provides a forward iterator over a `std::u8string_view` (or any range of
/// `char8_t`) that yields `uint32_t` codepoints one at a time.  Invalid byte
/// sequences emit U+FFFD (REPLACEMENT CHARACTER) and advance by one byte so
/// iteration always terminates.
///
/// ### Usage
///
/// @code
/// #include "utf8.hpp"
///
/// std::string_view sv = "héllo — world";
/// for (uint32_t cp : utf8::codepoints(sv))
///     process(cp);
/// @endcode
///
/// `std::string_view` is accepted via implicit reinterpret because the
/// underlying byte values are identical; the helper `utf8::codepoints()`
/// accepts both `std::string_view` and `std::u8string_view`.
///
/// Not part of the public API.  Include only from implementation files.
#pragma once

#include <cstdint>
#include <string_view>

namespace xebble::utf8 {

// ---------------------------------------------------------------------------
// Codepoint iterator
// ---------------------------------------------------------------------------

class Iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type        = uint32_t;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const uint32_t*;
    using reference         = uint32_t;

    constexpr Iterator() noexcept = default;
    constexpr explicit Iterator(const char8_t* p, const char8_t* end) noexcept
        : p_(p), end_(end) { decode(); }

    /// Construct a sentinel (end) iterator pointing at @p end.
    static constexpr Iterator make_end(const char8_t* end) noexcept {
        Iterator it;
        it.p_ = it.next_ = it.end_ = end;
        return it;
    }

    constexpr uint32_t operator*()  const noexcept { return cp_; }
    constexpr bool operator==(const Iterator& o) const noexcept { return p_ == o.p_; }
    constexpr bool operator!=(const Iterator& o) const noexcept { return p_ != o.p_; }

    constexpr Iterator& operator++() noexcept {
        p_ = next_;
        decode();
        return *this;
    }
    constexpr Iterator operator++(int) noexcept {
        Iterator tmp = *this;
        ++*this;
        return tmp;
    }

private:
    const char8_t* p_    = nullptr;
    const char8_t* end_  = nullptr;
    const char8_t* next_ = nullptr;
    uint32_t       cp_   = 0;

    // Decode one codepoint starting at p_, set cp_ and next_.
    constexpr void decode() noexcept {
        if (p_ >= end_) { next_ = p_; cp_ = 0; return; }

        auto b0 = static_cast<uint8_t>(*p_);

        if (b0 < 0x80u) {
            // 0xxxxxxx — ASCII
            cp_   = b0;
            next_ = p_ + 1;
        } else if (b0 < 0xC0u) {
            // 10xxxxxx — unexpected continuation byte → replacement
            cp_   = 0xFFFDu;
            next_ = p_ + 1;
        } else if (b0 < 0xE0u) {
            // 110xxxxx 10xxxxxx — 2-byte sequence
            if (p_ + 2 <= end_ && cont(p_[1])) {
                cp_   = ((b0 & 0x1Fu) << 6) | cont_bits(p_[1]);
                next_ = p_ + 2;
            } else {
                cp_ = 0xFFFDu; next_ = p_ + 1;
            }
        } else if (b0 < 0xF0u) {
            // 1110xxxx 10xxxxxx 10xxxxxx — 3-byte sequence
            if (p_ + 3 <= end_ && cont(p_[1]) && cont(p_[2])) {
                cp_   = ((b0 & 0x0Fu) << 12) | (cont_bits(p_[1]) << 6) | cont_bits(p_[2]);
                next_ = p_ + 3;
            } else {
                cp_ = 0xFFFDu; next_ = p_ + 1;
            }
        } else if (b0 < 0xF8u) {
            // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx — 4-byte sequence
            if (p_ + 4 <= end_ && cont(p_[1]) && cont(p_[2]) && cont(p_[3])) {
                cp_   = ((b0 & 0x07u) << 18) | (cont_bits(p_[1]) << 12)
                      | (cont_bits(p_[2]) << 6) | cont_bits(p_[3]);
                next_ = p_ + 4;
            } else {
                cp_ = 0xFFFDu; next_ = p_ + 1;
            }
        } else {
            // Invalid lead byte
            cp_ = 0xFFFDu; next_ = p_ + 1;
        }
    }

    static constexpr bool     cont(char8_t b)  noexcept { return (static_cast<uint8_t>(b) & 0xC0u) == 0x80u; }
    static constexpr uint32_t cont_bits(char8_t b) noexcept { return static_cast<uint32_t>(static_cast<uint8_t>(b) & 0x3Fu); }
};

// ---------------------------------------------------------------------------
// Range wrapper
// ---------------------------------------------------------------------------

class CodepointRange {
public:
    constexpr explicit CodepointRange(std::u8string_view sv) noexcept : sv_(sv) {}

    constexpr Iterator begin() const noexcept { return Iterator(sv_.data(), sv_.data() + sv_.size()); }
    constexpr Iterator end()   const noexcept { return Iterator::make_end(sv_.data() + sv_.size()); }

    /// Count the number of codepoints (O(n)).
    size_t count() const noexcept {
        size_t n = 0;
        for (auto it = begin(); it != end(); ++it) ++n;
        return n;
    }

private:
    std::u8string_view sv_;
};

// ---------------------------------------------------------------------------
// Convenience helpers
// ---------------------------------------------------------------------------

/// Iterate codepoints in a u8string_view.
inline constexpr CodepointRange codepoints(std::u8string_view sv) noexcept {
    return CodepointRange{sv};
}

/// Iterate codepoints in a plain string_view (reinterpret bytes as char8_t).
inline CodepointRange codepoints(std::string_view sv) noexcept {
    return CodepointRange{std::u8string_view{
        reinterpret_cast<const char8_t*>(sv.data()), sv.size()}};
}

/// Count codepoints in a string_view (O(n)).
inline size_t count(std::string_view sv) noexcept {
    return codepoints(sv).count();
}

} // namespace xebble::utf8
