#pragma once

#include <wx/arrstr.h>
#include <wx/dnd.h>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace nff::gui::wxbackend {

class WxFileDropTarget final : public wxFileDropTarget {
public:
    using Handler = std::function<bool(const std::vector<std::filesystem::path>&)>;

    explicit WxFileDropTarget(Handler handler) : handler_(std::move(handler)) {}

    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& filenames) override {
        if (!handler_ || filenames.empty()) {
            return false;
        }

        std::vector<std::filesystem::path> paths;
        paths.reserve(filenames.size());
        for (const auto& filename : filenames) {
            const auto buffer = filename.utf8_str();
            if (buffer.data() == nullptr || buffer.length() == 0U) {
                paths.emplace_back();
                continue;
            }
            std::u8string utf8;
            utf8.reserve(buffer.length());
            for (std::size_t index = 0U; index < buffer.length(); ++index) {
                utf8.push_back(static_cast<char8_t>(buffer.data()[index]));
            }
            paths.emplace_back(std::move(utf8));
        }
        return handler_(paths);
    }

private:
    Handler handler_{};
};

}
