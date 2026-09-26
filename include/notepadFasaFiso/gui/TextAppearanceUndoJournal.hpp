#pragma once

#include "notepadFasaFiso/metadata/MetadataStore.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace nff::gui {

enum class TextAppearanceUndoDirection : unsigned char {
    Undo,
    Redo,
};

struct TextAppearanceDelta final {
    std::uint64_t begin{0U};
    std::uint64_t end{0U};
    std::vector<metadata::TextAppearanceSpan> spans;
};

class TextAppearanceUndoJournal final {
public:
    [[nodiscard]] int record(const std::uint64_t begin,
                             const std::uint64_t end,
                             const std::span<const metadata::TextAppearanceSpan> before,
                             const std::span<const metadata::TextAppearanceSpan> after) {
        if (begin >= end ||
            (before.size() == after.size() && std::equal(before.begin(), before.end(), after.begin()))) {
            return 0;
        }
        if (nextToken_ <= 0 || nextToken_ == std::numeric_limits<int>::max()) {
            return 0;
        }

        const int token = nextToken_++;
        Entry entry;
        entry.undo.begin = begin;
        entry.undo.end = end;
        entry.undo.spans.assign(before.begin(), before.end());
        entry.redo.begin = begin;
        entry.redo.end = end;
        entry.redo.spans.assign(after.begin(), after.end());
        entries_.emplace(token, std::move(entry));
        return token;
    }

    [[nodiscard]] const TextAppearanceDelta* delta(
        const int token,
        const TextAppearanceUndoDirection direction) const noexcept {
        const auto iterator = entries_.find(token);
        if (iterator == entries_.end()) {
            return nullptr;
        }
        return direction == TextAppearanceUndoDirection::Undo
                   ? &iterator->second.undo
                   : &iterator->second.redo;
    }

    void clear() noexcept {
        entries_.clear();
        nextToken_ = 1;
    }

private:
    struct Entry final {
        TextAppearanceDelta undo;
        TextAppearanceDelta redo;
    };

    std::map<int, Entry> entries_;
    int nextToken_{1};
};

}
