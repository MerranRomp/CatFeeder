#include "catfeeder/Controller.h"

#include <algorithm>

namespace catfeeder {
namespace {
bool validInterval(std::uint32_t value) {
    return value > 0 && value <= 0x7fffffffU;
}

bool validConfig(const Config& config) {
    return validInterval(config.maxInputAgeMs) && validInterval(config.accessTimeoutMs) &&
           validInterval(config.dispenseTimeoutMs) &&
           validInterval(config.minimumFeedIntervalMs) && config.portionUnits > 0;
}

std::uint32_t elapsed(std::uint32_t now, std::uint32_t then) {
    return now - then; // Unsigned subtraction supports the millisecond counter wrapping.
}
} // namespace

Controller::Controller(const Config& config, AccessActuator& access,
                       Dispenser& dispenser, FeedingHistory& history,
                       std::uint32_t nowMs)
    : config_(config), access_(access), dispenser_(dispenser), history_(history),
      lastFeedAt_(nowMs) {
    if (!validConfig(config_)) {
        accessState_ = State::Fault;
        dispenseState_ = State::Fault;
        return;
    }

    FeedRecord record = FeedRecord::InProgress;
    if (!history_.load(record) || record == FeedRecord::InProgress) {
        dispenseState_ = State::Fault;
    } else if (record == FeedRecord::Completed) {
        // A monotonic clock cannot establish time spent powered off. Wait again.
        hasFed_ = true;
    } else if (record != FeedRecord::NoFeed) {
        dispenseState_ = State::Fault;
    }
    // Construction never commands physical movement, including on a fault.
}

bool Controller::reconfigure(const Config& config) {
    if (!validConfig(config) || accessState_ == State::Running ||
        dispenseState_ == State::Running) {
        return false;
    }
    config_ = config;
    return true;
}

bool Controller::authorized(std::uint32_t nowMs, const Inputs& inputs) const {
    return elapsed(nowMs, inputs.sampledAtMs) <= config_.maxInputAgeMs &&
           inputs.identification == IdentificationStatus::Identified &&
           !inputs.identifier.empty() &&
           std::find(config_.authorizedIds.begin(), config_.authorizedIds.end(),
                     inputs.identifier) != config_.authorizedIds.end();
}

void Controller::faultAccess() {
    accessState_ = State::Fault;
    access_.handleFault();
}

void Controller::faultDispense() {
    dispenseState_ = State::Fault;
    dispenser_.handleFault();
    // Keep InProgress in history: the delivered quantity is uncertain.
}

void Controller::update(std::uint32_t nowMs, const Inputs& inputs) {
    if (accessState_ == State::Running) {
        if (!authorized(nowMs, inputs) || inputs.accessSafety != Safety::Safe ||
            elapsed(nowMs, accessStartedAt_) >= config_.accessTimeoutMs) {
            faultAccess();
        } else {
            const auto status = access_.status();
            if (status == OperationStatus::Complete) {
                accessState_ = State::Complete;
            } else if (status != OperationStatus::Running) {
                faultAccess();
            }
        }
    }

    if (dispenseState_ == State::Running) {
        if (!authorized(nowMs, inputs) || inputs.dispenseSafety != Safety::Safe ||
            elapsed(nowMs, dispenseStartedAt_) >= config_.dispenseTimeoutMs) {
            faultDispense();
        } else {
            const auto status = dispenser_.status();
            if (status == OperationStatus::Complete) {
                if (history_.save(FeedRecord::Completed)) {
                    lastFeedAt_ = nowMs;
                    hasFed_ = true;
                    dispenseState_ = State::Complete;
                } else {
                    faultDispense();
                }
            } else if (status != OperationStatus::Running) {
                faultDispense();
            }
        }
    }
}

bool Controller::requestAccess(std::uint32_t nowMs, const Inputs& inputs) {
    update(nowMs, inputs);
    if (accessState_ == State::Fault || accessState_ == State::Running ||
        dispenseState_ == State::Running || dispenseState_ == State::Fault ||
        !authorized(nowMs, inputs) || inputs.accessSafety != Safety::Safe) {
        return false;
    }
    if (!access_.requestAccess()) {
        faultAccess();
        return false;
    }
    accessStartedAt_ = nowMs;
    accessState_ = State::Running;
    return true;
}

bool Controller::requestDispense(std::uint32_t nowMs, const Inputs& inputs) {
    update(nowMs, inputs);
    if (dispenseState_ == State::Fault || dispenseState_ == State::Running ||
        accessState_ == State::Running || accessState_ == State::Fault ||
        !authorized(nowMs, inputs) || inputs.dispenseSafety != Safety::Safe ||
        (hasFed_ && elapsed(nowMs, lastFeedAt_) < config_.minimumFeedIntervalMs)) {
        return false;
    }
    if (!history_.save(FeedRecord::InProgress)) {
        dispenseState_ = State::Fault;
        return false; // No movement if the durable intent could not be recorded.
    }
    if (!dispenser_.dispense(config_.portionUnits)) {
        faultDispense();
        return false;
    }
    dispenseStartedAt_ = nowMs;
    dispenseState_ = State::Running;
    return true;
}

} // namespace catfeeder
