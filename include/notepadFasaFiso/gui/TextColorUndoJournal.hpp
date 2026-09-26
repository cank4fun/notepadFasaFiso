#pragma once

#include "notepadFasaFiso/metadata/MetadataStore.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <span>
#include <vector>

namespace nff::gui {

enum class TextColorUndoDirection : unsigned char {
    Undo,
    Redo,
};

class TextColorUndoJournal final {
public:
    [[nodiscard]] int record(const std::span<const metadata::TextColorSpan> before,
                             const std::span<const metadata::TextColorSpan> after) {
        if (before.size() == after.size() &&
            std::equal(before.begin(), before.end(), after.begin())) {
            return 0;
        }
        if (nextToken_ <= 0 || nextToken_ == std::numeric_limits<int>::max()) {
            return 0;
        }

        const int token = nextToken_++;
        entries_.emplace(token, Entry{std::vector<metadata::TextColorSpan>(before.begin(), before.end()),
                                     std::vector<metadata::TextColorSpan>(after.begin(), after.end())});
        return token;
    }

    [[nodiscard]] const std::vector<metadata::TextColorSpan>* snapshot(
        const int token,
        const TextColorUndoDirection direction) const noexcept {
        const auto iterator = entries_.find(token);
        if (iterator == entries_.end()) {
            return nullptr;
        }
        return direction == TextColorUndoDirection::Undo
                   ? &iterator->second.before
                   : &iterator->second.after;
    }

    void clear() noexcept {
        entries_.clear();
        nextToken_ = 1;
    }

private:
    struct Entry final {
        std::vector<metadata::TextColorSpan> before;
        std::vector<metadata::TextColorSpan> after;
    };

    std::map<int, Entry> entries_;
    int nextToken_{1};
};

}
