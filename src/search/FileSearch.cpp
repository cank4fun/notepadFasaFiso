#include "notepadFasaFiso/search/FileSearch.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace nff::search {
namespace {

constexpr std::uint32_t noRootIndex = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] constexpr char asciiLower(const char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] std::string lowerAscii(std::string value) {
    std::ranges::transform(value, value.begin(), asciiLower);
    return value;
}

[[nodiscard]] bool asciiEqualInsensitive(const std::string_view left,
                                         const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (asciiLower(left[index]) != asciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool pathKeyEqual(const std::string_view left,
                                const std::string_view right) noexcept {
#ifdef _WIN32
    return asciiEqualInsensitive(left, right);
#else
    return left == right;
#endif
}

[[nodiscard]] bool pathKeyHasPrefix(const std::string_view value,
                                    const std::string_view prefix) noexcept {
    if (prefix.empty()) {
        return true;
    }
    if (value.size() < prefix.size()) {
        return false;
    }
#ifdef _WIN32
    if (!asciiEqualInsensitive(value.substr(0U, prefix.size()), prefix)) {
        return false;
    }
#else
    if (!value.starts_with(prefix)) {
        return false;
    }
#endif
    return value.size() == prefix.size() || value[prefix.size()] == '/';
}

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view value) {
    const auto* begin = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(begin, begin + value.size()));
}

[[nodiscard]] bool hiddenName(const std::filesystem::path& path) {
    const auto name = path.filename().native();
    if (name.empty()) {
        return false;
    }
#ifdef _WIN32
    return name.front() == L'.';
#else
    return name.front() == '.';
#endif
}

[[nodiscard]] bool excludedDirectory(const std::filesystem::path& path,
                                     const FileSearchBuildOptions& options) {
    const auto filename = pathToUtf8(path.filename());
    return std::ranges::any_of(options.excludedDirectoryNames, [&](const std::string& excluded) {
        return asciiEqualInsensitive(filename, excluded);
    });
}

#ifdef _WIN32
[[nodiscard]] bool namespaceLinkReparseTag(const DWORD tag) noexcept {

    return tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT;
}
#endif

[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return normalized;
    }
    error.clear();
    normalized = std::filesystem::absolute(path, error);
    if (!error) {
        return normalized.lexically_normal();
    }
    return path.lexically_normal();
}

[[nodiscard]] bool pathWithinRoot(const std::filesystem::path& path,
                                  const std::filesystem::path& root) {
    if (root.empty()) {
        return false;
    }
    const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty()) {
        return path.lexically_normal() == root.lexically_normal();
    }
    if (relative.is_absolute()) {
        return false;
    }
    const auto first = relative.begin();
    return first != relative.end() && *first != std::filesystem::path{".."};
}

[[nodiscard]] std::size_t filenameOffset(const std::string_view relativePath) noexcept {
    const auto separator = relativePath.find_last_of('/');
    return separator == std::string_view::npos ? 0U : separator + 1U;
}

[[nodiscard]] bool appendPath(std::string& arena,
                              const std::string_view path,
                              std::uint32_t& offset) {
    constexpr auto maximum = static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    if (arena.size() >= maximum || path.size() > maximum - arena.size() - 1U) {
        return false;
    }
    offset = static_cast<std::uint32_t>(arena.size());
    arena.append(path);
    arena.push_back('\0');
    return true;
}

[[nodiscard]] std::vector<std::string> splitTokens(const std::string_view query) {
    std::vector<std::string> tokens;
    std::size_t begin = 0U;
    while (begin < query.size()) {
        while (begin < query.size() &&
               (query[begin] == ' ' || query[begin] == '\t' ||
                query[begin] == '\r' || query[begin] == '\n')) {
            ++begin;
        }
        if (begin == query.size()) {
            break;
        }
        auto end = begin;
        while (end < query.size() && query[end] != ' ' && query[end] != '\t' &&
               query[end] != '\r' && query[end] != '\n') {
            ++end;
        }
        tokens.push_back(lowerAscii(std::string(query.substr(begin, end - begin))));
        begin = end;
    }
    return tokens;
}

struct TokenScore final {
    bool matched{false};
    std::uint32_t score{0U};
};

[[nodiscard]] std::size_t findContiguousInsensitive(const std::string_view candidate,
                                                    const std::string_view lowerToken) noexcept {
    if (lowerToken.empty()) {
        return 0U;
    }
    if (lowerToken.size() > candidate.size()) {
        return std::string_view::npos;
    }

    const auto last = candidate.size() - lowerToken.size();
    for (std::size_t start = 0U; start <= last; ++start) {
        if (asciiLower(candidate[start]) != lowerToken.front()) {
            continue;
        }
        std::size_t matched = 1U;
        while (matched < lowerToken.size() &&
               asciiLower(candidate[start + matched]) == lowerToken[matched]) {
            ++matched;
        }
        if (matched == lowerToken.size()) {
            return start;
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] TokenScore scoreToken(const std::string_view candidate,
                                    const std::string_view lowerToken) noexcept {
    if (lowerToken.empty()) {
        return {true, 0U};
    }

    const auto contiguous = findContiguousInsensitive(candidate, lowerToken);
    if (contiguous != std::string_view::npos) {
        const auto positionPenalty = static_cast<std::uint32_t>(
            std::min<std::size_t>(contiguous, std::size_t{500U}));
        const auto base = static_cast<std::uint32_t>(2000U + lowerToken.size() * 20U);
        return {true, base - std::min(base, positionPenalty)};
    }

    std::size_t queryIndex = 0U;
    std::size_t firstMatch = 0U;
    std::size_t previous = 0U;
    std::size_t gapCost = 0U;
    bool haveMatch = false;
    for (std::size_t index = 0U;
         index < candidate.size() && queryIndex < lowerToken.size(); ++index) {
        if (asciiLower(candidate[index]) != lowerToken[queryIndex]) {
            continue;
        }
        if (!haveMatch) {
            firstMatch = index;
            haveMatch = true;
        } else {
            gapCost += index - previous - 1U;
        }
        previous = index;
        ++queryIndex;
    }

    if (queryIndex != lowerToken.size()) {
        return {};
    }

    const auto penalty = std::min<std::size_t>(firstMatch + gapCost, std::size_t{700U});
    const auto base = static_cast<std::uint32_t>(900U + lowerToken.size() * 10U);
    return {true, base - std::min(base, static_cast<std::uint32_t>(penalty))};
}

[[nodiscard]] bool equalsInsensitiveLower(const std::string_view candidate,
                                          const std::string_view lowerValue) noexcept {
    if (candidate.size() != lowerValue.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < candidate.size(); ++index) {
        if (asciiLower(candidate[index]) != lowerValue[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool startsWithInsensitiveLower(const std::string_view candidate,
                                              const std::string_view lowerValue) noexcept {
    if (candidate.size() < lowerValue.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lowerValue.size(); ++index) {
        if (asciiLower(candidate[index]) != lowerValue[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t scoreEntry(const std::string_view relativePath,
                                       const std::string_view filename,
                                       const FileSearchEntryKind kind,
                                       const std::vector<std::string>& tokens) noexcept {
    std::uint64_t total = 0U;
    for (const auto& token : tokens) {
        auto score = scoreToken(relativePath, token);
        if (!score.matched) {
            return 0U;
        }

        const auto filenameScore = scoreToken(filename, token);
        if (filenameScore.matched) {
            score.score += 1000U;
            if (equalsInsensitiveLower(filename, token)) {
                score.score += 5000U;
            } else if (startsWithInsensitiveLower(filename, token)) {
                score.score += 1800U;
            }
        }
        total += score.score;
    }

    if (kind == FileSearchEntryKind::File) {
        total += 50U;
    }
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(total, std::numeric_limits<std::uint32_t>::max()));
}

struct RankedCandidate final {
    std::string_view relativePath;
    std::uint32_t rootIndex{noRootIndex};
    std::uint32_t score{0U};
    FileSearchEntryKind kind{FileSearchEntryKind::File};
};

[[nodiscard]] bool candidateBetter(const RankedCandidate& left,
                                   const RankedCandidate& right) noexcept {
    if (left.score != right.score) {
        return left.score > right.score;
    }
    return left.relativePath < right.relativePath;
}

struct WorseCandidate final {
    bool operator()(const RankedCandidate& left, const RankedCandidate& right) const noexcept {
        return candidateBetter(left, right);
    }
};

[[nodiscard]] bool hitBetter(const FileSearchHit& left, const FileSearchHit& right) noexcept {
    if (left.score != right.score) {
        return left.score > right.score;
    }
    return left.relativePathUtf8 < right.relativePathUtf8;
}

}

FileSearchBuildResult FileSearchIndex::rebuild(
    const std::span<const std::filesystem::path> roots,
    const FileSearchBuildOptions& options,
    const CancelPredicate& shouldCancel) {
    FileSearchBuildResult result;
    if (options.maxEntries == 0U || options.maxDepth == 0U || roots.empty()) {
        result.error = std::make_error_code(std::errc::invalid_argument);
        return result;
    }

    struct PendingDirectory final {
        std::filesystem::path path;
        std::size_t depth{0U};
        std::uint32_t rootIndex{noRootIndex};
    };

    std::vector<Entry> nextEntries;
    std::string nextArena;
    std::vector<std::filesystem::path> nextRoots;
    std::deque<PendingDirectory> pending;
    nextEntries.reserve(std::min<std::size_t>(options.maxEntries, std::size_t{1'024U}));
    nextRoots.reserve(roots.size());

    const auto appendEntry = [&](const std::filesystem::path& entryPath,
                                 const std::filesystem::path& root,
                                 const std::uint32_t rootIndex,
                                 const FileSearchEntryKind kind) -> bool {
        const auto relative = entryPath.lexically_relative(root);
        const auto relativeUtf8 = relative.empty() ? pathToUtf8(entryPath)
                                                   : pathToUtf8(relative);
        if (relativeUtf8.size() > static_cast<std::size_t>(
                                      std::numeric_limits<std::uint32_t>::max())) {
            result.error = std::make_error_code(std::errc::value_too_large);
            return false;
        }

        std::uint32_t pathOffset = 0U;
        if (!appendPath(nextArena, relativeUtf8, pathOffset)) {
            result.error = std::make_error_code(std::errc::value_too_large);
            return false;
        }
        const auto filename = filenameOffset(relativeUtf8);
        nextEntries.push_back(Entry{
            pathOffset,
            static_cast<std::uint32_t>(relativeUtf8.size()),
            static_cast<std::uint32_t>(filename),
            rootIndex,
            kind,
        });
        if (kind == FileSearchEntryKind::Directory) {
            ++result.stats.indexedDirectories;
        } else {
            ++result.stats.indexedFiles;
        }
        if (nextEntries.size() >= options.maxEntries) {
            result.stats.truncated = true;
        }
        return true;
    };

    for (const auto& inputRoot : roots) {
        if (shouldCancel && shouldCancel()) {
            result.stats.cancelled = true;
            result.error = std::make_error_code(std::errc::operation_canceled);
            return result;
        }

        const auto root = normalizedPath(inputRoot);
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) {
            if (error) {
                ++result.stats.errors;
                continue;
            }
            result.error = std::make_error_code(std::errc::not_a_directory);
            return result;
        }

        const auto existingRoot = std::ranges::find(nextRoots, root);
        if (existingRoot != nextRoots.end()) {
            continue;
        }
        if (nextRoots.size() >= static_cast<std::size_t>(noRootIndex)) {
            result.error = std::make_error_code(std::errc::value_too_large);
            return result;
        }
        const auto rootIndex = static_cast<std::uint32_t>(nextRoots.size());
        nextRoots.push_back(root);
        pending.push_back({root, 0U, rootIndex});
    }

    std::unordered_set<std::string> followedDirectoryTargets;
    if (options.followDirectorySymlinks) {
        for (const auto& root : nextRoots) {
            auto key = pathToUtf8(normalizedPath(root));
#ifdef _WIN32
            key = lowerAscii(std::move(key));
#endif
            followedDirectoryTargets.insert(std::move(key));
        }
    }

    while (!pending.empty() && !result.stats.truncated) {
        if (shouldCancel && shouldCancel()) {
            result.stats.cancelled = true;
            result.error = std::make_error_code(std::errc::operation_canceled);
            return result;
        }

        auto current = std::move(pending.front());
        pending.pop_front();
        if (current.rootIndex >= nextRoots.size()) {
            continue;
        }
        const auto& root = nextRoots[current.rootIndex];

        const auto processEntry = [&](const std::filesystem::path& entryPath,
                                      const bool isDirectory,
                                      const bool regularFile,
                                      const bool linkLike) -> bool {
            if (isDirectory) {
                const bool hidden = !options.includeHidden && hiddenName(entryPath);
                const bool excluded = excludedDirectory(entryPath, options);
                if (hidden || excluded) {
                    ++result.stats.skippedEntries;
                    return true;
                }
                if (options.includeDirectories &&
                    !appendEntry(entryPath, root, current.rootIndex,
                                 FileSearchEntryKind::Directory)) {
                    return false;
                }
                if (!result.stats.truncated && current.depth + 1U < options.maxDepth) {
                    bool enqueue = !linkLike;
                    if (linkLike && options.followDirectorySymlinks) {

                        std::error_code targetError;
                        const auto target = std::filesystem::weakly_canonical(entryPath, targetError);
                        if (!targetError) {

                            if (!pathWithinRoot(target, root)) {
                                auto key = pathToUtf8(target);
#ifdef _WIN32
                                key = lowerAscii(std::move(key));
#endif
                                enqueue = followedDirectoryTargets.insert(std::move(key)).second;
                            }
                        } else {
                            ++result.stats.errors;
                        }
                    }
                    if (enqueue) {
                        pending.push_back({entryPath, current.depth + 1U, current.rootIndex});
                    }
                }
                return true;
            }

            if (!regularFile) {
                return true;
            }
            const bool hidden = !options.includeHidden && hiddenName(entryPath);
            if (hidden) {
                ++result.stats.skippedEntries;
                return true;
            }
            return appendEntry(entryPath, root, current.rootIndex,
                               FileSearchEntryKind::File);
        };

#ifdef _WIN32

        const auto pattern = current.path / L"*";
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                       FindExSearchNameMatch, nullptr, 0U);
        if (find == INVALID_HANDLE_VALUE) {
            ++result.stats.errors;
            continue;
        }

        bool keepGoing = true;
        do {
            if (shouldCancel && shouldCancel()) {
                FindClose(find);
                result.stats.cancelled = true;
                result.error = std::make_error_code(std::errc::operation_canceled);
                return result;
            }

            const std::wstring_view name(data.cFileName);
            if (name == L"." || name == L"..") {
                continue;
            }

            const bool isDirectory =
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
            const bool reparsePoint =
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
            const bool linkLike = reparsePoint && namespaceLinkReparseTag(data.dwReserved0);
            const bool regularFile = !isDirectory;
            keepGoing = processEntry(current.path / data.cFileName,
                                     isDirectory, regularFile, linkLike);
            if (!keepGoing || result.stats.truncated) {
                break;
            }
        } while (FindNextFileW(find, &data) != FALSE);

        if (keepGoing && !result.stats.truncated) {
            const auto findError = GetLastError();
            if (findError != ERROR_NO_MORE_FILES) {
                ++result.stats.errors;
            }
        }
        FindClose(find);
        if (!keepGoing) {
            return result;
        }
#else
        const auto directoryOptions = std::filesystem::directory_options::skip_permission_denied;
        std::error_code error;
        std::filesystem::directory_iterator iterator(current.path, directoryOptions, error);
        const std::filesystem::directory_iterator end;
        if (error) {
            ++result.stats.errors;
            continue;
        }

        while (iterator != end && !result.stats.truncated) {
            if (shouldCancel && shouldCancel()) {
                result.stats.cancelled = true;
                result.error = std::make_error_code(std::errc::operation_canceled);
                return result;
            }

            const auto entryPath = iterator->path();
            std::error_code typeError;
            const bool isDirectory = iterator->is_directory(typeError);
            if (typeError) {
                ++result.stats.errors;
            } else {
                bool regularFile = false;
                bool linkLike = false;
                if (isDirectory) {
                    std::error_code symlinkError;
                    linkLike = iterator->is_symlink(symlinkError);
                    if (symlinkError) {
                        ++result.stats.errors;
                        linkLike = true;
                    }
                } else {
                    typeError.clear();
                    regularFile = iterator->is_regular_file(typeError);
                    if (typeError) {
                        ++result.stats.errors;
                        regularFile = false;
                    }
                }
                if (!processEntry(entryPath, isDirectory, regularFile, linkLike)) {
                    return result;
                }
            }

            std::error_code incrementError;
            iterator.increment(incrementError);
            if (incrementError) {
                ++result.stats.errors;
                break;
            }
        }
#endif
    }

    {
        std::unique_lock lock(mutex_);
        entries_.swap(nextEntries);
        pathArena_.swap(nextArena);
        roots_.swap(nextRoots);
        ++generation_;
    }
    return result;
}

std::vector<FileSearchHit> FileSearchIndex::search(const std::string_view query,
                                                   const std::size_t maxResults,
                                                   const CancelPredicate& shouldCancel) const {
    if (maxResults == 0U) {
        return {};
    }
    const auto tokens = splitTokens(query);
    if (tokens.empty()) {
        return {};
    }

    std::priority_queue<RankedCandidate, std::vector<RankedCandidate>, WorseCandidate> best;
    std::shared_lock lock(mutex_);
    constexpr std::size_t cancellationCheckStride = 256U;
    std::size_t scannedEntries = 0U;
    for (const auto& entry : entries_) {
        if ((scannedEntries % cancellationCheckStride) == 0U && shouldCancel && shouldCancel()) {
            return {};
        }
        ++scannedEntries;
        const auto relativePath = std::string_view(
            pathArena_.data() + entry.pathOffset, entry.pathLength);
        const auto filename = relativePath.substr(entry.filenameOffset);
        const auto score = scoreEntry(relativePath, filename, entry.kind, tokens);
        if (score == 0U) {
            continue;
        }

        RankedCandidate candidate{relativePath, entry.rootIndex, score, entry.kind};
        if (best.size() < maxResults) {
            best.push(candidate);
            continue;
        }
        if (candidateBetter(candidate, best.top())) {
            best.pop();
            best.push(candidate);
        }
    }

    if (shouldCancel && shouldCancel()) {
        return {};
    }

    std::vector<FileSearchHit> results;
    results.reserve(best.size());
    while (!best.empty()) {
        const auto candidate = best.top();
        best.pop();

        auto path = pathFromUtf8(candidate.relativePath);
        if (candidate.rootIndex != noRootIndex && candidate.rootIndex < roots_.size()) {
            path = roots_[candidate.rootIndex] / path;
        }
        results.push_back(FileSearchHit{
            std::move(path),
            std::string(candidate.relativePath),
            candidate.score,
            candidate.kind,
        });
    }
    std::ranges::sort(results, hitBetter);
    return results;
}

bool FileSearchIndex::upsert(const std::filesystem::path& path,
                             const std::filesystem::path& root) {
    std::error_code error;
    FileSearchEntryKind kind{};
    if (std::filesystem::is_regular_file(path, error) && !error) {
        kind = FileSearchEntryKind::File;
    } else {
        error.clear();
        if (!std::filesystem::is_directory(path, error) || error) {
            return false;
        }
        kind = FileSearchEntryKind::Directory;
    }

    const auto normalized = normalizedPath(path);
    const auto normalizedRoot = root.empty() ? std::filesystem::path{} : normalizedPath(root);
    const auto relativeUtf8 = normalizedRoot.empty()
                                  ? pathToUtf8(normalized)
                                  : pathToUtf8(normalized.lexically_relative(normalizedRoot));
    if (relativeUtf8.empty() ||
        relativeUtf8.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    std::unique_lock lock(mutex_);
    std::uint32_t rootIndex = noRootIndex;
    if (!normalizedRoot.empty()) {
        const auto existingRoot = std::ranges::find(roots_, normalizedRoot);
        if (existingRoot == roots_.end()) {
            if (roots_.size() >= static_cast<std::size_t>(noRootIndex)) {
                return false;
            }
            rootIndex = static_cast<std::uint32_t>(roots_.size());
            roots_.push_back(normalizedRoot);
        } else {
            rootIndex = static_cast<std::uint32_t>(std::distance(roots_.begin(), existingRoot));
        }
    }

    const auto iterator = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
        if (entry.rootIndex != rootIndex) {
            return false;
        }
        const auto candidate = std::string_view(
            pathArena_.data() + entry.pathOffset, entry.pathLength);
        return pathKeyEqual(candidate, relativeUtf8);
    });

    if (iterator != entries_.end()) {
        iterator->kind = kind;
        ++generation_;
        return true;
    }

    std::uint32_t pathOffset = 0U;
    if (!appendPath(pathArena_, relativeUtf8, pathOffset)) {
        return false;
    }
    entries_.push_back(Entry{
        pathOffset,
        static_cast<std::uint32_t>(relativeUtf8.size()),
        static_cast<std::uint32_t>(filenameOffset(relativeUtf8)),
        rootIndex,
        kind,
    });
    ++generation_;
    return true;
}

bool FileSearchIndex::erase(const std::filesystem::path& path) {
    const auto normalized = normalizedPath(path);
    const auto absoluteUtf8 = pathToUtf8(normalized);

    std::unique_lock lock(mutex_);
    std::vector<std::optional<std::string>> relativeTargets(roots_.size());
    for (std::size_t index = 0U; index < roots_.size(); ++index) {
        if (pathWithinRoot(normalized, roots_[index])) {
            const auto relative = normalized.lexically_relative(roots_[index]);
            relativeTargets[index] = relative == std::filesystem::path{"."}
                                         ? std::string{}
                                         : pathToUtf8(relative);
        }
    }

    const auto originalSize = entries_.size();
    std::erase_if(entries_, [&](const Entry& entry) {
        const auto candidate = std::string_view(
            pathArena_.data() + entry.pathOffset, entry.pathLength);
        if (entry.rootIndex == noRootIndex) {
            return pathKeyEqual(candidate, absoluteUtf8);
        }
        if (entry.rootIndex >= relativeTargets.size() || !relativeTargets[entry.rootIndex]) {
            return false;
        }
        return pathKeyEqual(candidate, *relativeTargets[entry.rootIndex]);
    });
    if (entries_.size() == originalSize) {
        return false;
    }
    ++generation_;
    return true;
}

std::size_t FileSearchIndex::eraseSubtree(const std::filesystem::path& path) {
    const auto normalized = normalizedPath(path);
    const auto absoluteUtf8 = pathToUtf8(normalized);

    std::unique_lock lock(mutex_);
    std::vector<std::optional<std::string>> relativeTargets(roots_.size());
    for (std::size_t index = 0U; index < roots_.size(); ++index) {
        if (pathWithinRoot(normalized, roots_[index])) {
            const auto relative = normalized.lexically_relative(roots_[index]);
            relativeTargets[index] = relative == std::filesystem::path{"."}
                                         ? std::string{}
                                         : pathToUtf8(relative);
        }
    }

    const auto originalSize = entries_.size();
    std::erase_if(entries_, [&](const Entry& entry) {
        const auto candidate = std::string_view(
            pathArena_.data() + entry.pathOffset, entry.pathLength);
        if (entry.rootIndex == noRootIndex) {
            return pathKeyHasPrefix(candidate, absoluteUtf8);
        }
        if (entry.rootIndex >= relativeTargets.size() || !relativeTargets[entry.rootIndex]) {
            return false;
        }
        return pathKeyHasPrefix(candidate, *relativeTargets[entry.rootIndex]);
    });
    const auto erased = originalSize - entries_.size();
    if (erased != 0U) {
        ++generation_;
    }
    return erased;
}

void FileSearchIndex::clear() noexcept {
    std::unique_lock lock(mutex_);
    std::vector<Entry>{}.swap(entries_);
    std::string{}.swap(pathArena_);
    std::vector<std::filesystem::path>{}.swap(roots_);
    ++generation_;
}

std::size_t FileSearchIndex::size() const noexcept {
    std::shared_lock lock(mutex_);
    return entries_.size();
}

std::uint64_t FileSearchIndex::generation() const noexcept {
    std::shared_lock lock(mutex_);
    return generation_;
}

std::size_t FileSearchIndex::estimatedStorageBytes() const noexcept {
    std::shared_lock lock(mutex_);
    std::size_t bytes = entries_.capacity() * sizeof(Entry) +
                        (pathArena_.empty() ? 0U : pathArena_.capacity()) +
                        roots_.capacity() * sizeof(std::filesystem::path);
    for (const auto& root : roots_) {
        if (!root.empty()) {
            bytes += root.native().capacity() * sizeof(std::filesystem::path::value_type);
        }
    }
    return bytes;
}

}
