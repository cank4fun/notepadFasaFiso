#pragma once

#include "notepadFasaFiso/viewer/LargeFileViewer.hpp"

#include <cstddef>
#include <cstdint>
#include <system_error>

namespace nff::viewer {

struct FollowOptions final {
    std::uint64_t tailLines{200U};
    std::size_t maximumDecodedBytes{16U * 1024U * 1024U};
};

struct FollowUpdate final {
    ViewerRefreshResult refresh;
    TailWindowResult tail;
    bool hasTail{false};
    std::error_code error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error; }
};

class LiveFileFollower final {
public:
    explicit LiveFileFollower(LargeFileViewer& viewer, FollowOptions options = {}) noexcept;

    void setOptions(FollowOptions options) noexcept;
    [[nodiscard]] const FollowOptions& options() const noexcept;

    [[nodiscard]] TailWindowResult snapshot();
    [[nodiscard]] FollowUpdate poll();

private:
    LargeFileViewer* viewer_{};
    FollowOptions options_{};
    bool snapshotPending_{false};
};

}
