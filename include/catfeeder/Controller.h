#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace catfeeder {

enum class Safety { Unknown, Safe, Unsafe };
enum class IdentificationStatus { Unavailable, NoIdentifier, Identified };
enum class OperationStatus { Unknown, Running, Complete, Failed };
enum class State { Idle, Running, Complete, Fault };
enum class FeedRecord { NoFeed, InProgress, Completed };

struct Inputs {
    // Refresh the entire snapshot together; never timestamp cached sensor data as new.
    std::uint32_t sampledAtMs = 0;
    IdentificationStatus identification = IdentificationStatus::Unavailable;
    std::string identifier;
    Safety accessSafety = Safety::Unknown;
    Safety dispenseSafety = Safety::Unknown;
};

struct Config {
    std::vector<std::string> authorizedIds;
    // Zero means unconfigured. These values must be chosen explicitly.
    std::uint32_t maxInputAgeMs = 0;
    std::uint32_t accessTimeoutMs = 0;
    std::uint32_t dispenseTimeoutMs = 0;
    std::uint32_t minimumFeedIntervalMs = 0;
    // Abstract calibrated units, not grams or a promise of measured food weight.
    std::uint32_t portionUnits = 0;
};

class AccessActuator {
public:
    virtual ~AccessActuator() = default;
    virtual bool requestAccess() = 0;
    virtual OperationStatus status() = 0;
    // Implement the mechanism's validated fault response; do not assume closing is safe.
    virtual void handleFault() = 0;
};

class Dispenser {
public:
    virtual ~Dispenser() = default;
    virtual bool dispense(std::uint32_t portionUnits) = 0;
    virtual OperationStatus status() = 0;
    virtual void handleFault() = 0;
};

class FeedingHistory {
public:
    virtual ~FeedingHistory() = default;
    virtual bool load(FeedRecord& record) = 0;
    // Return true only after a durable commit. Torn/invalid records must fail load().
    virtual bool save(FeedRecord record) = 0;
};

// All methods, including adapter methods, must return promptly. Use one caller.
class Controller {
public:
    Controller(const Config& config, AccessActuator& access, Dispenser& dispenser,
               FeedingHistory& history, std::uint32_t nowMs);

    void update(std::uint32_t nowMs, const Inputs& inputs);
    bool requestAccess(std::uint32_t nowMs, const Inputs& inputs);
    bool requestDispense(std::uint32_t nowMs, const Inputs& inputs);
    // Update policy between operations without clearing faults or feeding history.
    bool reconfigure(const Config& config);

    State accessState() const { return accessState_; }
    State dispenseState() const { return dispenseState_; }

private:
    bool authorized(std::uint32_t nowMs, const Inputs& inputs) const;
    void faultAccess();
    void faultDispense();

    Config config_;
    AccessActuator& access_;
    Dispenser& dispenser_;
    FeedingHistory& history_;
    State accessState_ = State::Idle;
    State dispenseState_ = State::Idle;
    std::uint32_t accessStartedAt_ = 0;
    std::uint32_t dispenseStartedAt_ = 0;
    std::uint32_t lastFeedAt_ = 0;
    bool hasFed_ = false;
};

} // namespace catfeeder
