#pragma once

#include "notepadFasaFiso/search/TextSearch.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

namespace nff::search {

struct SourceReadResult final {
    std::size_t bytesRead{};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

class ITextSearchSource {
public:
    virtual ~ITextSearchSource() = default;

    [[nodiscard]] virtual std::uint64_t size() const noexcept = 0;
    [[nodiscard]] virtual SourceReadResult read(std::uint64_t offset,
                                                std::span<char> destination) = 0;
};

class MemoryTextSearchSource final : public ITextSearchSource {
public:
    explicit MemoryTextSearchSource(std::string_view text) noexcept;

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] SourceReadResult read(std::uint64_t offset,
                                        std::span<char> destination) override;

private:
    std::string_view text_;
};

struct StreamingSearchOptions final {
    std::size_t chunkBytes{4ULL * 1024ULL * 1024ULL};
    const std::atomic_bool* cancelFlag{nullptr};
};

struct StreamingSearchResult final {
    std::vector<SearchMatch> matches;
    std::uint64_t bytesScanned{};
    std::error_code error;
    bool truncated{false};
    bool cancelled{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

struct StreamingFindResult final {
    std::optional<SearchMatch> match;
    std::uint64_t bytesScanned{};
    std::error_code error;
    bool wrapped{false};
    bool cancelled{false};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !error;
    }
};

class StreamingTextSearch final {
public:
    [[nodiscard]] static StreamingFindResult find(
        ITextSearchSource& source,
        std::string_view pattern,
        std::uint64_t startOffset,
        SearchDirection direction,
        const SearchOptions& options = {},
        const StreamingSearchOptions& streamingOptions = {});

    [[nodiscard]] static StreamingSearchResult findAll(
        ITextSearchSource& source,
        std::string_view pattern,
        const SearchOptions& options = {},
        const StreamingSearchOptions& streamingOptions = {});
};

}
