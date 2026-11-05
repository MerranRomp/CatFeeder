#include "Fakes.h"
#include "catfeeder/Settings.h"

#include <iostream>
#include <stdexcept>
#include <limits>

using namespace catfeeder;
using namespace simulation;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    MockAccess access;
    MockDispenser dispenser;
    MemoryHistory history;
    Controller controller{mockConfig(), access, dispenser, history, 0};
};

void startupAndRecognitionDoNotMove() {
    Fixture f;
    for (std::uint32_t now = 0; now < 100; ++now) {
        f.controller.update(now, mockInputs(now));
    }
    require(f.access.requests == 0 && f.dispenser.requests == 0,
            "Startup or repeated recognition moved a mechanism");
    require(f.access.faults == 0 && f.dispenser.faults == 0,
            "Startup invoked a physical fault response");
}

void identificationAndFreshnessGateRequests() {
    Fixture f;
    auto inputs = mockInputs(0);
    inputs.identifier = "unknown-cat";
    require(!f.controller.requestAccess(0, inputs), "Unknown cat got access");
    require(!f.controller.requestDispense(0, inputs), "Unknown cat got food");
    inputs.identifier = "mock-cat";
    inputs.identification = IdentificationStatus::Unavailable;
    require(!f.controller.requestAccess(0, inputs), "Unavailable reader granted access");
    inputs.identification = IdentificationStatus::NoIdentifier;
    require(!f.controller.requestDispense(0, inputs), "Missing ID allowed feeding");
    inputs.identification = IdentificationStatus::Identified;
    require(!f.controller.requestAccess(101, inputs), "Stale snapshot granted access");
    require(!f.controller.requestDispense(101, inputs), "Stale snapshot allowed food");
    inputs.sampledAtMs = 200;
    require(!f.controller.requestDispense(100, inputs), "Future timestamp allowed food");
    require(f.access.requests == 0 && f.dispenser.requests == 0,
            "Rejected requests reached hardware");
}

void safetyDefaultsDenyMovement() {
    Fixture f;
    auto inputs = mockInputs(0);
    inputs.accessSafety = Safety::Unknown;
    inputs.dispenseSafety = Safety::Unknown;
    require(!f.controller.requestAccess(0, inputs), "Unknown safety granted access");
    require(!f.controller.requestDispense(0, inputs), "Unknown safety allowed feeding");
    inputs.accessSafety = Safety::Unsafe;
    inputs.dispenseSafety = Safety::Unsafe;
    require(!f.controller.requestAccess(0, inputs), "Unsafe access request accepted");
    require(!f.controller.requestDispense(0, inputs), "Unsafe feeding accepted");
}

void operationsCompleteAndPreventOverlap() {
    Fixture f;
    require(f.controller.requestAccess(0, mockInputs(0)), "Access request rejected");
    require(f.controller.accessState() == State::Running, "Command counted as completion");
    require(!f.controller.requestAccess(1, mockInputs(1)), "Duplicate access accepted");
    require(!f.controller.requestDispense(1, mockInputs(1)), "Overlapping feeding accepted");
    f.access.reported = OperationStatus::Complete;
    f.controller.update(2, mockInputs(2));
    require(f.controller.accessState() == State::Complete, "Access did not complete");
    require(f.controller.requestDispense(3, mockInputs(3)), "Feeding request rejected");
    require(f.history.record == FeedRecord::InProgress, "Feeding intent not recorded");
    require(f.dispenser.lastPortion == 1, "Configured portion was not forwarded");
    require(!f.controller.requestAccess(4, mockInputs(4)), "Overlapping access accepted");
    require(!f.controller.requestDispense(4, mockInputs(4)), "Duplicate feed accepted");
    f.dispenser.reported = OperationStatus::Complete;
    f.controller.update(5, mockInputs(5));
    require(f.controller.dispenseState() == State::Complete, "Feeding did not complete");
    require(f.history.record == FeedRecord::Completed, "Completion not recorded");
}

void repeatIntervalStartsAtCompletion() {
    Fixture f;
    require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    f.dispenser.reported = OperationStatus::Complete;
    f.controller.update(100, mockInputs(100));
    require(!f.controller.requestDispense(5099, mockInputs(5099)), "Fed before interval");
    f.dispenser.reported = OperationStatus::Running;
    require(f.controller.requestDispense(5100, mockInputs(5100)), "Interval never expired");
    require(f.dispenser.requests == 2, "Unexpected number of feed commands");
}

void deadlinesLatchFaults() {
    Fixture f;
    require(f.controller.requestAccess(0, mockInputs(0)), "Initial access rejected");
    f.controller.update(999, mockInputs(999));
    require(f.controller.accessState() == State::Running, "Access timed out early");
    f.controller.update(1000, mockInputs(1000));
    f.controller.update(1001, mockInputs(1001));
    require(f.controller.accessState() == State::Fault, "Access deadline not enforced");
    require(f.access.faults == 1, "Fault response repeated or omitted");
    require(!f.controller.requestAccess(1002, mockInputs(1002)), "Fault reset itself");

    Fixture g;
    require(g.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    g.controller.update(1000, mockInputs(1000));
    require(g.controller.dispenseState() == State::Fault, "Feed deadline not enforced");
    require(g.dispenser.faults == 1, "Dispenser fault response missing");
    require(g.history.record == FeedRecord::InProgress, "Timeout erased uncertainty");
}

void lossOfInputsFaultsRunningOperations() {
    Fixture f;
    require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    f.controller.update(101, mockInputs(0));
    require(f.controller.dispenseState() == State::Fault, "Stale input did not fault");

    Fixture g;
    require(g.controller.requestAccess(0, mockInputs(0)), "Initial access rejected");
    auto lost = mockInputs(1);
    lost.identification = IdentificationStatus::Unavailable;
    g.controller.update(1, lost);
    require(g.controller.accessState() == State::Fault, "Lost reader did not fault");

    Fixture h;
    require(h.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    auto unsafe = mockInputs(1);
    unsafe.dispenseSafety = Safety::Unsafe;
    h.controller.update(1, unsafe);
    require(h.controller.dispenseState() == State::Fault, "Unsafe input did not fault");
}

void failedAndUnknownStatusAreNotCompletion() {
    const OperationStatus statuses[] = {OperationStatus::Failed, OperationStatus::Unknown};
    for (const auto status : statuses) {
        Fixture f;
        require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
        f.dispenser.reported = status;
        f.controller.update(1, mockInputs(1));
        require(f.controller.dispenseState() == State::Fault, "Bad dispenser status accepted");
        require(f.history.record == FeedRecord::InProgress, "Bad status cleared history");
        Fixture g;
        require(g.controller.requestAccess(0, mockInputs(0)), "Initial access rejected");
        g.access.reported = status;
        g.controller.update(1, mockInputs(1));
        require(g.controller.accessState() == State::Fault, "Bad access status accepted");
    }
}

void rejectedCommandsFaultConservatively() {
    Fixture f;
    f.dispenser.accepts = false;
    require(!f.controller.requestDispense(0, mockInputs(0)), "Rejected feed accepted");
    require(f.controller.dispenseState() == State::Fault, "Rejected feed did not fault");
    require(f.history.record == FeedRecord::InProgress, "Rejection erased uncertainty");
    Fixture g;
    g.access.accepts = false;
    require(!g.controller.requestAccess(0, mockInputs(0)), "Rejected access accepted");
    require(g.access.faults == 1, "Rejected access did not request fault response");
}

void interruptedFeedSurvivesControllerRestart() {
    Fixture f;
    require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    Controller restarted(mockConfig(), f.access, f.dispenser, f.history, 10);
    require(restarted.dispenseState() == State::Fault, "Interrupted feed forgotten");
    require(!restarted.requestDispense(10000, mockInputs(10000)), "Uncertain feed retried");
    require(!restarted.requestAccess(10000, mockInputs(10000)), "Uncertain feed allowed access");
    require(f.dispenser.requests == 1 && f.dispenser.faults == 0,
            "Restart automatically commanded hardware");
}

void completedFeedRestartsCooldown() {
    Fixture f;
    require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    f.dispenser.reported = OperationStatus::Complete;
    f.controller.update(1, mockInputs(1));
    Controller restarted(mockConfig(), f.access, f.dispenser, f.history, 0);
    require(!restarted.requestDispense(4999, mockInputs(4999)), "Reboot bypassed interval");
    require(restarted.requestDispense(5000, mockInputs(5000)), "Reboot interval stuck");
}

void storageFailuresInhibitFeeding() {
    Fixture f;
    f.history.writable = false;
    require(!f.controller.requestDispense(0, mockInputs(0)), "Unrecorded feed accepted");
    require(f.dispenser.requests == 0, "Motor commanded before durable intent");

    Fixture g;
    require(g.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    g.history.writable = false;
    g.dispenser.reported = OperationStatus::Complete;
    g.controller.update(1, mockInputs(1));
    require(g.controller.dispenseState() == State::Fault, "Failed completion save ignored");
    require(g.history.record == FeedRecord::InProgress, "Failed save erased uncertainty");

    MockAccess access;
    MockDispenser dispenser;
    MemoryHistory history;
    history.readable = false;
    Controller unreadable(mockConfig(), access, dispenser, history, 0);
    require(!unreadable.requestDispense(0, mockInputs(0)), "Unreadable history allowed food");
}

void unconfiguredControllerIsInert() {
    MockAccess access;
    MockDispenser dispenser;
    MemoryHistory history;
    Controller controller(Config{}, access, dispenser, history, 0);
    require(!controller.requestAccess(0, mockInputs(0)), "Unconfigured access accepted");
    require(!controller.requestDispense(0, mockInputs(0)), "Unconfigured feeding accepted");
    require(access.requests == 0 && dispenser.requests == 0, "Defaults moved hardware");
}

void clockWrapPreservesDeadlinesAndFreshness() {
    Fixture f;
    const std::uint32_t beforeWrap = 0xfffffff0U;
    require(f.controller.requestDispense(beforeWrap, mockInputs(beforeWrap)), "Feed rejected");
    f.controller.update(10, mockInputs(beforeWrap));
    require(f.controller.dispenseState() == State::Running, "Clock wrap caused early fault");
    f.controller.update(984, mockInputs(984));
    require(f.controller.dispenseState() == State::Fault, "Wrapped deadline missed");

    Fixture g;
    require(g.controller.requestDispense(beforeWrap, mockInputs(beforeWrap)), "Feed rejected");
    g.dispenser.reported = OperationStatus::Complete;
    g.controller.update(beforeWrap, mockInputs(beforeWrap));
    require(!g.controller.requestDispense(4983, mockInputs(4983)), "Wrapped interval too short");
    require(g.controller.requestDispense(4984, mockInputs(4984)), "Wrapped interval stuck");
}

void policyUpdatesRespectActiveOperations() {
    Fixture f;
    require(f.controller.requestAccess(0, mockInputs(0)), "Initial access rejected");
    auto config = mockConfig();
    config.authorizedIds = {"another-cat"};
    require(!f.controller.reconfigure(config), "Policy changed during access movement");
    f.access.reported = OperationStatus::Complete;
    f.controller.update(1, mockInputs(1));
    require(f.controller.reconfigure(config), "Idle policy update rejected");
    require(!f.controller.requestAccess(2, mockInputs(2)), "Removed tag still authorized");
    auto other = mockInputs(3);
    other.identifier = "another-cat";
    require(f.controller.requestAccess(3, other), "New tag did not become authorized");
}

void policyUpdatesPreserveFeedingHistoryAndFaults() {
    Fixture f;
    require(f.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    auto config = mockConfig();
    require(!f.controller.reconfigure(config), "Policy changed during dispensing");
    f.dispenser.reported = OperationStatus::Complete;
    f.controller.update(1, mockInputs(1));
    require(f.controller.reconfigure(config), "Idle policy update rejected");
    require(!f.controller.requestDispense(2, mockInputs(2)), "Saving policy erased cooldown");
    config.minimumFeedIntervalMs = 0;
    require(!f.controller.reconfigure(config), "Invalid policy accepted");

    Fixture g;
    require(g.controller.requestDispense(0, mockInputs(0)), "Initial feed rejected");
    g.controller.update(1000, mockInputs(1000));
    require(g.controller.reconfigure(mockConfig()), "Faulted configuration update rejected");
    require(g.controller.dispenseState() == State::Fault, "Saving settings cleared a fault");
    require(g.history.record == FeedRecord::InProgress, "Saving settings erased uncertainty");
}

void settingsValidateTimesAndPolicyBounds() {
    auto settings = demoSettings();
    std::string error;
    require(validateSettings(settings, error), "Demo defaults invalid");
    settings.feedingTimes = {"07:00", "18:30"};
    settings.portionGrams = 12.5;
    require(validateSettings(settings, error), "Valid portion and plan rejected");
    settings.feedingTimes = {"24:00"};
    require(!validateSettings(settings, error), "Invalid hour accepted");
    settings.feedingTimes = {"07:00", "07:00"};
    require(!validateSettings(settings, error), "Duplicate meals accepted");
    settings.feedingTimes = {"7:00"};
    require(!validateSettings(settings, error), "Malformed time accepted");
    settings.feedingTimes.clear();
    settings.portionGrams = std::numeric_limits<double>::quiet_NaN();
    require(!validateSettings(settings, error), "NaN portion accepted");
    settings.portionGrams = 0;
    settings.minimumFeedIntervalSeconds = 0;
    require(!validateSettings(settings, error), "Zero repeat interval accepted");
    settings.minimumFeedIntervalSeconds = 86401;
    require(!validateSettings(settings, error), "Unbounded repeat interval accepted");
}

void settingsValidateCatsAndAllowAnEmptyAllowlist() {
    auto settings = demoSettings();
    std::string error;
    settings.cats.push_back({"Another cat", "different-tag"});
    require(!validateSettings(settings, error), "More than one cat accepted");
    settings.cats = {{" ", "tag"}};
    require(!validateSettings(settings, error), "Blank cat name accepted");
    settings.cats = {{"Name", std::string(65, 'x')}};
    require(!validateSettings(settings, error), "Oversized tag accepted");
    settings.cats = {{"Name", "tag\n"}};
    require(!validateSettings(settings, error), "Control character accepted");
    settings.cats.clear();
    require(validateSettings(settings, error), "Empty authorization list must be supported");
}
} // namespace

int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[] = {
        {"startup and recognition", startupAndRecognitionDoNotMove},
        {"identification and freshness", identificationAndFreshnessGateRequests},
        {"safety defaults", safetyDefaultsDenyMovement},
        {"completion and overlap", operationsCompleteAndPreventOverlap},
        {"repeat interval", repeatIntervalStartsAtCompletion},
        {"operation deadlines", deadlinesLatchFaults},
        {"input loss", lossOfInputsFaultsRunningOperations},
        {"failed and unknown status", failedAndUnknownStatusAreNotCompletion},
        {"command rejection", rejectedCommandsFaultConservatively},
        {"interrupted restart", interruptedFeedSurvivesControllerRestart},
        {"completed restart", completedFeedRestartsCooldown},
        {"storage failures", storageFailuresInhibitFeeding},
        {"unconfigured defaults", unconfiguredControllerIsInert},
        {"clock rollover", clockWrapPreservesDeadlinesAndFreshness},
        {"policy update during operations", policyUpdatesRespectActiveOperations},
        {"policy update history and faults", policyUpdatesPreserveFeedingHistoryAndFaults},
        {"settings times and bounds", settingsValidateTimesAndPolicyBounds},
        {"settings cat validation", settingsValidateCatsAndAllowAnEmptyAllowlist},
    };
    unsigned failed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL: " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << (sizeof(tests) / sizeof(tests[0])) << " scenarios, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
