/// @file msglog.hpp
/// @brief Scrollable, color-coded message log with deduplication.
///
/// `MessageLog` is a fixed-capacity FIFO of colored, categorized game messages.
/// It handles the three features that make roguelike logs feel polished:
///
/// - **Deduplication**: repeated consecutive messages collapse to "Message ×N".
/// - **Scrollback**: random-access to any past message by index.
/// - **Filtering**: iterate only messages of a given category.
///
/// The log has no rendering dependency — call `messages()` or `visible()` to
/// get the text/colors and submit them through any UI or renderer you like.
///
/// ## Quick-start
///
/// @code
/// #include <xebble/msglog.hpp>
/// using namespace xebble;
///
/// MessageLog log(100);  // keep the last 100 messages
///
/// log.push("You attack the goblin for 5 damage.");
/// log.push("The goblin hits you for 3 damage.", {220, 80, 80, 255});
/// log.push("You attack the goblin for 5 damage.");  // deduplicates!
///
/// // In your HUD system — show the last 4 messages:
/// for (auto& msg : log.visible(4)) {
///     renderer.draw_text(msg.text, hud_x, y, msg.color);
///     y += line_height;
/// }
/// @endcode
///
/// ## Categories
///
/// @code
/// log.push("You find a healing potion.", {100, 200, 100, 255}, "loot");
/// log.push("The kobold bites you for 2 damage.", {220, 80, 80, 255}, "combat");
/// log.push("You descend to floor 2.",            {200, 200, 100, 255}, "story");
///
/// // Only show combat messages in a combat log panel.
/// for (auto& msg : log.filtered("combat", 10)) { … }
/// @endcode
#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace xebble {

// ---------------------------------------------------------------------------
// LogColor — RGBA color for a message
// ---------------------------------------------------------------------------

/// @brief RGBA color for a log message.
///
/// Defaults to white (255,255,255,255) so plain `push(text)` calls need no color.
struct LogColor {
    uint8_t r = 255, g = 255, b = 255, a = 255;

    bool operator==(const LogColor&) const = default;
};

// ---------------------------------------------------------------------------
// LogMessage — a single entry in the log
// ---------------------------------------------------------------------------

/// @brief A single log entry.
struct LogMessage {
    std::u8string text; ///< Display text encoded as UTF-8 (includes u8" ×N" suffix if repeated).
    LogColor color;     ///< Text color.
    std::string
        category;  ///< Optional category tag for filtering (ASCII identifier, not displayed).
    int count = 1; ///< Raw repeat count (before suffix was appended).
};

// ---------------------------------------------------------------------------
// MessageLog
// ---------------------------------------------------------------------------

/// @brief Scrollable, deduplicating game message log.
///
/// Stores up to `capacity` messages in a ring; older messages are discarded
/// when the capacity is exceeded.
///
/// @code
/// MessageLog log(200);
///
/// // Plain white message.
/// log.push("You open the chest.");
///
/// // Colored message.
/// log.push("Critical hit! 18 damage.", {255, 220, 50, 255});
///
/// // Categorized message (for filtered display).
/// log.push("Floor 3 — The Obsidian Vaults", {180, 140, 255, 255}, "story");
///
/// // Repeated messages deduplicate automatically:
/// log.push("You miss.");
/// log.push("You miss.");
/// log.push("You miss.");
/// // log.newest().text == "You miss. ×3"
/// @endcode
class MessageLog {
public:
    /// @brief Construct a log with the given maximum message capacity.
    explicit MessageLog(size_t capacity = 100) : capacity_(capacity) {}

    // -----------------------------------------------------------------------
    // Adding messages
    // -----------------------------------------------------------------------

    /// @brief Add a message to the log.
    ///
    /// If the new message has the same text and category as the most recent
    /// entry, the count is incremented and the display text is updated
    /// (e.g. "You miss. ×3") rather than adding a duplicate entry.
    ///
    /// @param text      Message body.
    /// @param color     Text color (default white).
    /// @param category  Optional category tag (default "").
    ///
    /// @code
    /// log.push("You attack the goblin for 4 damage.");
    /// log.push("The goblin flees!", {200, 200, 80, 255});
    /// log.push("You miss.", {180, 180, 180, 255}, "combat");
    /// log.push("You miss.", {180, 180, 180, 255}, "combat");
    /// // → "You miss. ×2"
    /// @endcode
    void push(std::u8string text, LogColor color = {}, std::string category = "") {
        if (!messages_.empty()) {
            auto& last = messages_.back();
            if (last.category == category && last_raw_text_ == text) {
                ++last.count;
                // U+00D7 MULTIPLICATION SIGN ×, followed by repeat count.
                // Digits are ASCII so safe to reinterpret as char8_t.
                auto count_str = std::to_string(last.count);
                std::u8string suffix = u8" \u00D7";
                for (char c : count_str)
                    suffix += static_cast<char8_t>(c);
                last.text = text + suffix;
                last.color = color;
                return;
            }
        }
        last_raw_text_ = text;
        if (messages_.size() >= capacity_)
            messages_.pop_front();
        messages_.push_back(LogMessage{std::move(text), color, std::move(category), 1});
    }

    // -----------------------------------------------------------------------
    // Querying
    // -----------------------------------------------------------------------

    /// @brief True if no messages have been pushed.
    bool empty() const { return messages_.empty(); }

    /// @brief Total number of messages currently stored.
    size_t size() const { return messages_.size(); }

    /// @brief Maximum number of messages retained.
    size_t capacity() const { return capacity_; }

    /// @brief Clear all messages.
    void clear() {
        messages_.clear();
        last_raw_text_.clear();
    }

    /// @brief Return the most recently added message.
    ///
    /// @pre `!empty()`
    const LogMessage& newest() const { return messages_.back(); }

    /// @brief Return the oldest retained message.
    ///
    /// @pre `!empty()`
    const LogMessage& oldest() const { return messages_.front(); }

    /// @brief Access a message by index (0 = oldest).
    const LogMessage& operator[](size_t i) const { return messages_[i]; }

    /// @brief Return the last @p n messages (most recent last), suitable for HUD display.
    ///
    /// If fewer than @p n messages exist, returns all of them.
    ///
    /// @code
    /// // Display the last 5 messages in a HUD panel.
    /// for (auto& msg : log.visible(5)) {
    ///     draw_text(msg.text, x, y, msg.color);
    ///     y += line_h;
    /// }
    /// @endcode
    std::vector<const LogMessage*> visible(size_t n) const {
        std::vector<const LogMessage*> out;
        size_t start = messages_.size() > n ? messages_.size() - n : 0;
        for (size_t i = start; i < messages_.size(); ++i)
            out.push_back(&messages_[i]);
        return out;
    }

    /// @brief Return all messages matching @p category (most recent last).
    ///
    /// @code
    /// // Show only combat events.
    /// for (auto* msg : log.filtered("combat", 8)) { … }
    /// @endcode
    std::vector<const LogMessage*> filtered(const std::string& category,
                                            size_t max_results = SIZE_MAX) const {
        std::vector<const LogMessage*> out;
        for (auto it = messages_.rbegin(); it != messages_.rend() && out.size() < max_results; ++it)
            if (it->category == category)
                out.push_back(&*it);
        std::reverse(out.begin(), out.end());
        return out;
    }

    /// @brief Read-only access to all messages (oldest first).
    const std::deque<LogMessage>& messages() const { return messages_; }

private:
    size_t capacity_;
    std::deque<LogMessage> messages_;
    std::u8string last_raw_text_;
};

} // namespace xebble
