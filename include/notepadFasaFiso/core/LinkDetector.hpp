#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nff::core {

enum class LinkKind : std::uint8_t {
    Http,
    Https,
    Www,
    Mailto,
    Email,
    FileUri,
    FilePath,
};

struct LinkSpan final {
    std::size_t offset{0U};
    std::size_t length{0U};
    LinkKind kind{LinkKind::Http};
    std::string target;
};

class LinkDetector final {
public:
    [[nodiscard]] static std::vector<LinkSpan> detect(std::string_view utf8Text,
                                                      std::size_t maxResults = 4096U);
};

}
