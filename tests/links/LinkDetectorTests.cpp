#include "notepadFasaFiso/core/LinkDetector.hpp"

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

void testWebAndEmailLinks() {
    const std::string text =
        "Visit https://example.com/a?q=1, www.example.org/test. mail me@example.com or mailto:x@y.dev";
    const auto links = nff::core::LinkDetector::detect(text);
    require(links.size() == 4U, "web and email links detected");
    require(links[0].kind == nff::core::LinkKind::Https, "https classification");
    require(links[0].target == "https://example.com/a?q=1", "https punctuation trimmed");
    require(links[1].kind == nff::core::LinkKind::Www, "www classification");
    require(links[1].target == "https://www.example.org/test", "www target normalized");
    require(links[2].kind == nff::core::LinkKind::Email, "email classification");
    require(links[2].target == "mailto:me@example.com", "email target normalized");
    require(links[3].kind == nff::core::LinkKind::Mailto, "mailto classification");
}

void testLocalPathsAreIgnored() {
    const std::string text =
        "C:\\Projects\\example\\README.md /tmp/server.log ../config/app.ini "
        "\\\\server\\share\\x.txt .\\config\\windows.ini "
        "file:///C:/Projects/example/CMakeLists.txt";
    const auto links = nff::core::LinkDetector::detect(text);
    require(links.empty(), "local filesystem paths are not clickable links");
}

void testFalsePositivesAndLimit() {
    const auto links = nff::core::LinkDetector::detect(
        "normal text foo@bar sentence httpish://nope example.com", 10U);
    require(links.empty(), "plain prose does not produce links");

    const auto limited = nff::core::LinkDetector::detect(
        "https://a.test https://b.test https://c.test", 2U);
    require(limited.size() == 2U, "link result limit enforced");
}

}

int main() {
    testWebAndEmailLinks();
    testLocalPathsAreIgnored();
    testFalsePositivesAndLimit();
    return EXIT_SUCCESS;
}
