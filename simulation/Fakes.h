#pragma once

#include "catfeeder/Controller.h"

// Simulation adapters only. None of these types reads or writes hardware.
namespace simulation {

struct MockAccess : catfeeder::AccessActuator {
    bool accepts = true;
    unsigned requests = 0;
    unsigned faults = 0;
    catfeeder::OperationStatus reported = catfeeder::OperationStatus::Running;

    bool requestAccess() override { ++requests; return accepts; }
    catfeeder::OperationStatus status() override { return reported; }
    void handleFault() override { ++faults; }
};

struct MockDispenser : catfeeder::Dispenser {
    bool accepts = true;
    unsigned requests = 0;
    unsigned faults = 0;
    std::uint32_t lastPortion = 0;
    catfeeder::OperationStatus reported = catfeeder::OperationStatus::Running;

    bool dispense(std::uint32_t units) override {
        ++requests;
        lastPortion = units;
        return accepts;
    }
    catfeeder::OperationStatus status() override { return reported; }
    void handleFault() override { ++faults; }
};

struct MemoryHistory : catfeeder::FeedingHistory {
    catfeeder::FeedRecord record = catfeeder::FeedRecord::NoFeed;
    bool readable = true;
    bool writable = true;

    bool load(catfeeder::FeedRecord& result) override {
        result = record;
        return readable;
    }
    bool save(catfeeder::FeedRecord next) override {
        if (!writable) return false;
        record = next;
        return true;
    }
};

inline catfeeder::Config mockConfig() {
    catfeeder::Config config;
    config.authorizedIds = {"mock-cat"};
    config.maxInputAgeMs = 100;
    config.accessTimeoutMs = 1000;
    config.dispenseTimeoutMs = 1000;
    config.minimumFeedIntervalMs = 5000;
    config.portionUnits = 1;
    return config; // Arbitrary simulation settings; not hardware calibration.
}

inline catfeeder::Inputs mockInputs(std::uint32_t nowMs) {
    catfeeder::Inputs inputs;
    inputs.sampledAtMs = nowMs;
    inputs.identification = catfeeder::IdentificationStatus::Identified;
    inputs.identifier = "mock-cat";
    inputs.accessSafety = catfeeder::Safety::Safe;
    inputs.dispenseSafety = catfeeder::Safety::Safe;
    return inputs;
}

} // namespace simulation
