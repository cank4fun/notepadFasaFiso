#include "notepadFasaFiso/search/SearchSource.hpp"
#include "notepadFasaFiso/search/TextSearch.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testLiteralSearch() {
    const std::string text = "alpha beta ALPHA alphabet alpha";

    nff::search::SearchOptions options;
    options.caseSensitive = false;
    options.wholeWord = true;

    const auto all = nff::search::TextSearch::findAll(text, "alpha", options);
    require(static_cast<bool>(all), "literal findAll succeeds");
    require(all.matches.size() == 3U, "case-insensitive whole-word finds three matches");
    require(all.matches[0].offset == 0U, "first literal offset");
    require(all.matches[1].offset == 11U, "second literal offset");
    require(all.matches[2].offset == 26U, "third literal offset");

    const auto next = nff::search::TextSearch::findNext(text, "alpha", 1U, options);
    require(static_cast<bool>(next) && next.match.has_value(), "findNext succeeds");
    require(next.match->offset == 11U, "findNext starts after requested offset");

    const auto previous = nff::search::TextSearch::findPrevious(text, "alpha", 11U, options);
    require(static_cast<bool>(previous) && previous.match.has_value(), "findPrevious succeeds");
    require(previous.match->offset == 0U, "findPrevious searches preceding range");

    const auto wrapped = nff::search::TextSearch::findNext(text, "alpha", text.size(), options);
    require(static_cast<bool>(wrapped) && wrapped.match.has_value(), "forward wrap succeeds");
    require(wrapped.match->offset == 0U, "forward wrap returns first match");
}

void testUtf8ByteOffsets() {
    const std::string text = "çile test çile";
    const auto result = nff::search::TextSearch::findAll(text, "çile");
    require(static_cast<bool>(result), "UTF-8 literal search succeeds");
    require(result.matches.size() == 2U, "UTF-8 exact search finds both values");
    require(result.matches[0].offset == 0U, "UTF-8 first byte offset");
    require(result.matches[1].offset == 11U, "UTF-8 second byte offset");
}

void testRegexSearchAndReplace() {
    nff::search::SearchOptions options;
    options.kind = nff::search::SearchKind::RegularExpression;
    options.caseSensitive = false;

    const std::string text = "error 12\nERROR 345\nok";
    const auto found = nff::search::TextSearch::findAll(text, R"(error ([0-9]+))", options);
    require(static_cast<bool>(found), "regex findAll succeeds");
    require(found.matches.size() == 2U, "regex finds two matches");

    const auto replaced =
        nff::search::TextSearch::replaceAll(text, R"(error ([0-9]+))", "E[$1]", options);
    require(static_cast<bool>(replaced), "regex replacement succeeds");
    require(replaced.replacements == 2U, "regex replacement count");
    require(replaced.text == "E[12]\nE[345]\nok", "regex capture replacement output");

    const auto invalid = nff::search::TextSearch::findAll(text, "(", options);
    require(!static_cast<bool>(invalid), "invalid regex is reported");
    require(invalid.error == std::make_error_code(std::errc::invalid_argument),
            "invalid regex error code");
}

void testLiteralReplace() {
    nff::search::SearchOptions options;
    options.caseSensitive = false;
    options.wholeWord = true;

    const auto replaced =
        nff::search::TextSearch::replaceAll("cat CAT scatter cat", "cat", "dog", options);
    require(static_cast<bool>(replaced), "literal replace succeeds");
    require(replaced.replacements == 3U, "literal replace count");
    require(replaced.text == "dog dog scatter dog", "literal replace respects whole word");
}

void testReplaceSpecificMatch() {
    nff::search::SearchOptions literal;
    literal.caseSensitive = false;
    literal.wholeWord = true;
    const auto literalResult = nff::search::TextSearch::replaceMatch(
        "cat CAT scatter cat", "cat", "wolf",
        nff::search::SearchMatch{4U, 3U}, literal);
    require(static_cast<bool>(literalResult) && literalResult.replaced,
            "specific literal replacement succeeds");
    require(literalResult.text == "cat wolf scatter cat",
            "specific literal replacement touches only selected match");
    require(literalResult.replacementLength == 4U,
            "specific literal replacement reports replacement length");

    nff::search::SearchOptions regex;
    regex.kind = nff::search::SearchKind::RegularExpression;
    regex.caseSensitive = false;
    const std::string regexText = "error 12 ERROR 345";
    const auto matches = nff::search::TextSearch::findAll(
        regexText, R"(error ([0-9]+))", regex);
    require(static_cast<bool>(matches) && matches.matches.size() == 2U,
            "specific regex fixture has two matches");
    const auto regexResult = nff::search::TextSearch::replaceMatch(
        regexText, R"(error ([0-9]+))", "E[$1]", matches.matches[1], regex);
    require(static_cast<bool>(regexResult) && regexResult.replaced,
            "specific regex replacement succeeds");
    require(regexResult.text == "error 12 E[345]",
            "specific regex replacement expands captures at selected match");

    const auto stale = nff::search::TextSearch::replaceMatch(
        "cat dog", "cat", "wolf", nff::search::SearchMatch{4U, 3U}, literal);
    require(!static_cast<bool>(stale), "stale specific replacement is rejected");
}

void testResultLimit() {
    nff::search::SearchOptions options;
    options.maxResults = 2U;

    const auto found = nff::search::TextSearch::findAll("x x x x", "x", options);
    require(static_cast<bool>(found), "limited search succeeds");
    require(found.matches.size() == 2U, "limited search result count");
    require(found.truncated, "limited search reports truncation");
}

void testBackwardOverlapAndRegexAnchors() {
    nff::search::SearchOptions literal;
    literal.wrapAround = false;
    const auto previous = nff::search::TextSearch::findPrevious("aaa", "aa", 3U, literal);
    require(static_cast<bool>(previous) && previous.match.has_value(),
            "backward overlapping literal succeeds");
    require(previous.match->offset == 1U, "backward search finds nearest overlapping match");

    nff::search::SearchOptions regex;
    regex.kind = nff::search::SearchKind::RegularExpression;
    regex.wrapAround = false;
    const auto anchored = nff::search::TextSearch::findNext("abc\nabc", "^abc", 1U, regex);
    require(static_cast<bool>(anchored), "anchored regex search succeeds");
    require(!anchored.match.has_value(), "regex start anchor is not rebased to search offset");

    const auto zeroLength = nff::search::TextSearch::findAll("abc", R"((?=b))", regex);
    require(static_cast<bool>(zeroLength), "zero-length regex search succeeds");
    require(zeroLength.matches.size() == 1U, "zero-length regex is finite");
    require(zeroLength.matches[0].offset == 1U && zeroLength.matches[0].length == 0U,
            "zero-length regex offset is preserved");

    const auto endAnchor = nff::search::TextSearch::findAll("abc", "$", regex);
    require(static_cast<bool>(endAnchor), "regex end anchor search succeeds");
    require(endAnchor.matches.size() == 1U, "regex end anchor produces one match");
    require(endAnchor.matches[0].offset == 3U && endAnchor.matches[0].length == 0U,
            "regex end anchor preserves absolute end offset");

    const auto mixedAnchor = nff::search::TextSearch::findAll("a", "a|$", regex);
    require(static_cast<bool>(mixedAnchor), "regex terminal zero-length match succeeds");
    require(mixedAnchor.matches.size() == 2U, "regex keeps terminal zero-length match");
    require(mixedAnchor.matches[0].offset == 0U && mixedAnchor.matches[0].length == 1U,
            "regex non-empty match before terminal anchor");
    require(mixedAnchor.matches[1].offset == 1U && mixedAnchor.matches[1].length == 0U,
            "regex terminal anchor is not dropped");

    const auto lineStart = nff::search::TextSearch::findAll("abc\nabc", "^abc", regex);
    require(static_cast<bool>(lineStart), "absolute start anchor search succeeds");
    require(lineStart.matches.size() == 1U, "absolute start anchor ignores later line starts");
    require(lineStart.matches[0].offset == 0U, "absolute start anchor stays at input start");

    const auto lineEnd = nff::search::TextSearch::findAll("abc\nabc", "abc$", regex);
    require(static_cast<bool>(lineEnd), "absolute end anchor search succeeds");
    require(lineEnd.matches.size() == 1U, "absolute end anchor ignores earlier line ends");
    require(lineEnd.matches[0].offset == 4U, "absolute end anchor stays at input end");

    const auto startAlternative =
        nff::search::TextSearch::findAll("xxx\nabc def", "^abc|def", regex);
    require(static_cast<bool>(startAlternative), "anchored alternation search succeeds");
    require(startAlternative.matches.size() == 1U,
            "invalid rebased anchored alternative does not hide unanchored alternative");
    require(startAlternative.matches[0].offset == 8U,
            "unanchored alternative remains searchable after rejected anchor match");

    const auto escapedAnchors = nff::search::TextSearch::findAll("^ $", R"(\^|\$)", regex);
    require(static_cast<bool>(escapedAnchors), "escaped anchor literals search succeeds");
    require(escapedAnchors.matches.size() == 2U, "escaped anchors remain literals");
}

void testStreamingSearchAcrossChunks() {
    const std::string text = "zero ERROR one two ERROR three ERROR";
    nff::search::MemoryTextSearchSource source(text);

    nff::search::SearchOptions options;
    options.wholeWord = true;

    nff::search::StreamingSearchOptions streaming;
    streaming.chunkBytes = 7U;

    const auto found = nff::search::StreamingTextSearch::findAll(source, "ERROR", options, streaming);
    require(static_cast<bool>(found), "streaming search succeeds");
    require(found.matches.size() == 3U, "streaming search finds matches across chunk edges");
    require(found.matches[0].offset == 5U, "streaming first offset");
    require(found.matches[1].offset == 19U, "streaming second offset");
    require(found.matches[2].offset == 31U, "streaming third offset");
    require(found.bytesScanned == text.size(), "streaming reports scanned byte count");
}

void testStreamingCaseAndWordBoundaries() {
    const std::string text = "CAT scatter cat Catfish CAT";
    nff::search::MemoryTextSearchSource source(text);

    nff::search::SearchOptions options;
    options.caseSensitive = false;
    options.wholeWord = true;

    nff::search::StreamingSearchOptions streaming;
    streaming.chunkBytes = 4U;

    const auto found = nff::search::StreamingTextSearch::findAll(source, "cat", options, streaming);
    require(static_cast<bool>(found), "streaming case-insensitive search succeeds");
    require(found.matches.size() == 3U, "streaming whole-word filtering works");
    require(found.matches[0].offset == 0U, "streaming word match zero");
    require(found.matches[1].offset == 12U, "streaming word match one");
    require(found.matches[2].offset == 24U, "streaming word match two");
}

void testStreamingFindNavigation() {
    const std::string text = "zero alpha one alpha two alpha";
    nff::search::MemoryTextSearchSource source(text);

    nff::search::SearchOptions options;
    options.wrapAround = true;

    nff::search::StreamingSearchOptions streaming;
    streaming.chunkBytes = 5U;

    const auto next = nff::search::StreamingTextSearch::find(
        source, "alpha", 6U, nff::search::SearchDirection::Forward, options, streaming);
    require(static_cast<bool>(next) && next.match.has_value(),
            "streaming forward find succeeds");
    require(next.match->offset == 15U, "streaming forward find honors start offset");
    require(!next.wrapped, "streaming forward find does not report false wrap");

    const auto previous = nff::search::StreamingTextSearch::find(
        source, "alpha", 15U, nff::search::SearchDirection::Backward, options, streaming);
    require(static_cast<bool>(previous) && previous.match.has_value(),
            "streaming backward find succeeds");
    require(previous.match->offset == 5U, "streaming backward find returns nearest match");

    const auto wrappedForward = nff::search::StreamingTextSearch::find(
        source, "alpha", text.size(), nff::search::SearchDirection::Forward, options, streaming);
    require(static_cast<bool>(wrappedForward) && wrappedForward.match.has_value(),
            "streaming forward wrap succeeds");
    require(wrappedForward.match->offset == 5U && wrappedForward.wrapped,
            "streaming forward wrap returns first match");

    const auto wrappedBackward = nff::search::StreamingTextSearch::find(
        source, "alpha", 0U, nff::search::SearchDirection::Backward, options, streaming);
    require(static_cast<bool>(wrappedBackward) && wrappedBackward.match.has_value(),
            "streaming backward wrap succeeds");
    require(wrappedBackward.match->offset == 25U && wrappedBackward.wrapped,
            "streaming backward wrap returns final match");
}

void testStreamingFindOverlapWordAndCancellation() {
    {
        const std::string text = "aaa";
        nff::search::MemoryTextSearchSource source(text);
        nff::search::SearchOptions options;
        options.wrapAround = false;
        nff::search::StreamingSearchOptions streaming;
        streaming.chunkBytes = 2U;
        const auto previous = nff::search::StreamingTextSearch::find(
            source, "aa", 3U, nff::search::SearchDirection::Backward, options, streaming);
        require(static_cast<bool>(previous) && previous.match.has_value(),
                "streaming backward overlap succeeds");
        require(previous.match->offset == 1U,
                "streaming backward overlap returns nearest overlapping match");
    }

    {
        const std::string text = "CAT scatter cat Catfish CAT";
        nff::search::MemoryTextSearchSource source(text);
        nff::search::SearchOptions options;
        options.caseSensitive = false;
        options.wholeWord = true;
        options.wrapAround = false;
        nff::search::StreamingSearchOptions streaming;
        streaming.chunkBytes = 4U;
        const auto next = nff::search::StreamingTextSearch::find(
            source, "cat", 1U, nff::search::SearchDirection::Forward, options, streaming);
        require(static_cast<bool>(next) && next.match.has_value(),
                "streaming find whole-word succeeds");
        require(next.match->offset == 12U,
                "streaming find skips embedded non-word candidate");
    }

    {
        const std::string text(1024U, 'x');
        nff::search::MemoryTextSearchSource source(text);
        std::atomic_bool cancelled{true};
        nff::search::StreamingSearchOptions streaming;
        streaming.chunkBytes = 64U;
        streaming.cancelFlag = &cancelled;
        const auto result = nff::search::StreamingTextSearch::find(
            source, "z", 0U, nff::search::SearchDirection::Forward, {}, streaming);
        require(!static_cast<bool>(result) && result.cancelled,
                "streaming find cancellation is reported");
        require(result.error == std::make_error_code(std::errc::operation_canceled),
                "streaming find cancellation error code");
    }
}

void testStreamingCancellationAndRegexGuard() {
    const std::string text(1024U, 'x');
    nff::search::MemoryTextSearchSource source(text);

    std::atomic_bool cancelled{true};
    nff::search::StreamingSearchOptions streaming;
    streaming.chunkBytes = 64U;
    streaming.cancelFlag = &cancelled;

    const auto cancelledResult =
        nff::search::StreamingTextSearch::findAll(source, "x", {}, streaming);
    require(!static_cast<bool>(cancelledResult), "cancelled streaming search reports error");
    require(cancelledResult.cancelled, "cancelled streaming search sets flag");
    require(cancelledResult.error == std::make_error_code(std::errc::operation_canceled),
            "cancelled streaming search error code");

    nff::search::SearchOptions regexOptions;
    regexOptions.kind = nff::search::SearchKind::RegularExpression;
    const auto regexResult =
        nff::search::StreamingTextSearch::findAll(source, "x+", regexOptions, {});
    require(!static_cast<bool>(regexResult), "streaming regex is explicitly rejected");
    require(regexResult.error == std::make_error_code(std::errc::operation_not_supported),
            "streaming regex unsupported error code");
}

}

int main() {
    testLiteralSearch();
    testUtf8ByteOffsets();
    testRegexSearchAndReplace();
    testLiteralReplace();
    testReplaceSpecificMatch();
    testResultLimit();
    testBackwardOverlapAndRegexAnchors();
    testStreamingSearchAcrossChunks();
    testStreamingCaseAndWordBoundaries();
    testStreamingFindNavigation();
    testStreamingFindOverlapWordAndCancellation();
    testStreamingCancellationAndRegexGuard();
    return EXIT_SUCCESS;
}
