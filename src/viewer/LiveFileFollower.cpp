#include "notepadFasaFiso/viewer/LiveFileFollower.hpp"

namespace nff::viewer {

LiveFileFollower::LiveFileFollower(LargeFileViewer& viewer, FollowOptions options) noexcept
    : viewer_(&viewer), options_(options) {}

void LiveFileFollower::setOptions(const FollowOptions options) noexcept { options_ = options; }

const FollowOptions& LiveFileFollower::options() const noexcept { return options_; }

TailWindowResult LiveFileFollower::snapshot() {
    if (viewer_ == nullptr) {
        TailWindowResult result;
        result.error = std::make_error_code(std::errc::bad_file_descriptor);
        snapshotPending_ = true;
        return result;
    }

    auto result = viewer_->readTail(options_.tailLines, options_.maximumDecodedBytes);
    snapshotPending_ = !static_cast<bool>(result);
    return result;
}

FollowUpdate LiveFileFollower::poll() {
    FollowUpdate update;
    if (viewer_ == nullptr) {
        update.error = std::make_error_code(std::errc::bad_file_descriptor);
        return update;
    }

    update.refresh = viewer_->refresh();
    if (!update.refresh) {
        update.error = update.refresh.error;
        return update;
    }

    if (update.refresh.kind == ViewerRefreshKind::Deleted ||
        update.refresh.kind == ViewerRefreshKind::Inaccessible) {
        snapshotPending_ = true;
        return update;
    }

    if (update.refresh.changed()) {
        snapshotPending_ = true;
    }
    if (!snapshotPending_) {
        return update;
    }

    update.tail = snapshot();
    if (!update.tail) {
        update.error = update.tail.error;
        return update;
    }
    update.hasTail = true;
    return update;
}

}
