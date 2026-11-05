#include "catfeeder/Settings.h"

#include <cmath>
#include <set>

namespace catfeeder {
namespace {
bool readable(const std::string& value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (const unsigned char character : value) {
        if (character < 32 || character == 127) return false;
    }
    return value.find_first_not_of(' ') != std::string::npos;
}
} // namespace

bool validateSettings(const Settings& settings, std::string& error) {
    if (!std::isfinite(settings.portionGrams) || settings.portionGrams < 0 ||
        settings.portionGrams > 1000) {
        error = "Portion target must be between 0 and 1000 grams; 0 means unset.";
        return false;
    }
    if (settings.minimumFeedIntervalSeconds < 1 ||
        settings.minimumFeedIntervalSeconds > 86400) {
        error = "Minimum feeding interval must be between 1 and 86400 seconds.";
        return false;
    }
    if (settings.feedingTimes.size() > 6) {
        error = "Use at most six daily feeding times.";
        return false;
    }
    if (settings.cats.size() > 1) {
        error = "This feeder supports one cat profile.";
        return false;
    }
    std::set<std::string> times;
    for (const auto& time : settings.feedingTimes) {
        if (time.size() != 5 || time[2] != ':' || time[0] < '0' || time[0] > '2' ||
            time[1] < '0' || time[1] > '9' || time[3] < '0' || time[3] > '5' ||
            time[4] < '0' || time[4] > '9' || time.substr(0, 2) > "23" ||
            !times.insert(time).second) {
            error = "Feeding times must be unique 24-hour times in HH:MM format.";
            return false;
        }
    }
    for (const auto& cat : settings.cats) {
        if (!readable(cat.name, 32) || !readable(cat.tag, 64)) {
            error = "The cat needs a name (32 bytes max) and tag ID (64 bytes max).";
            return false;
        }
    }
    error.clear();
    return true;
}

Settings demoSettings() {
    Settings settings;
    settings.cats.push_back({"Demo cat", "mock-cat"});
    return settings;
}
} // namespace catfeeder
