#pragma once

#include "notepadFasaFiso/core/DocumentManager.hpp"
#include "notepadFasaFiso/recovery/RecoveryManager.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <system_error>
#include <vector>

namespace nff::recovery {

struct AutoSavePolicy {
    bool recoveryEnabled{true};
    std::chrono::milliseconds recoveryDelay{1500};
    bool saveRealFiles{false};
    std::chrono::milliseconds saveDelay{5000};
    bool saveOnFocusLoss{false};
};

enum class AutoSaveOutcome : std::uint8_t {
    None,
    RecoveryCheckpoint,
    FileSaved,
    ExternalConflict,
    Failed
};

struct AutoSaveResult {
    core::DocumentId documentId{};
    AutoSaveOutcome outcome{AutoSaveOutcome::None};
    std::error_code error{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return outcome != AutoSaveOutcome::Failed && !error;
    }
};

class AutoSaveManager final {
public:
    using Clock = std::chrono::steady_clock;

    AutoSaveManager(core::DocumentManager& documents,
                    RecoveryManager& recovery,
                    AutoSavePolicy policy = {});

    void setPolicy(AutoSavePolicy policy) noexcept;
    [[nodiscard]] const AutoSavePolicy& policy() const noexcept;

    void noteEdited(core::DocumentId documentId, Clock::time_point now = Clock::now());
    [[nodiscard]] std::vector<AutoSaveResult> poll(Clock::time_point now = Clock::now());
    [[nodiscard]] AutoSaveResult focusLost(core::DocumentId documentId,
                                           Clock::time_point now = Clock::now());
    void forget(core::DocumentId documentId) noexcept;

private:
    struct TrackedDocument {
        Clock::time_point lastEdit{};
        std::uint64_t observedRevision{0};
        std::uint64_t recoveryRevision{0};
        std::uint64_t savedRevision{0};
    };

    [[nodiscard]] AutoSaveResult process(core::DocumentId documentId,
                                         TrackedDocument& tracked,
                                         Clock::time_point now,
                                         bool focusLoss);
    [[nodiscard]] AutoSaveResult checkpoint(core::DocumentId documentId,
                                            core::Document& document,
                                            TrackedDocument& tracked);

    core::DocumentManager& documents_;
    RecoveryManager& recovery_;
    AutoSavePolicy policy_{};
    std::map<core::DocumentId, TrackedDocument> tracked_;
};

}
