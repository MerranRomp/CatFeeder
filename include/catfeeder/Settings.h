#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace catfeeder {
struct CatProfile {
    std::string name;
    std::string tag;
};

struct Settings {
    std::uint32_t revision = 0;
    double portionGrams = 0; // Zero means unset; no calibration is implied.
    std::uint32_t minimumFeedIntervalSeconds = 5; // Existing mock demo policy.
    std::vector<std::string> feedingTimes; // Daily HH:MM targets, currently inactive.
    std::vector<CatProfile> cats; // Zero or one profile; retain the v1 JSON shape.
};

bool validateSettings(const Settings& settings, std::string& error);
Settings demoSettings();
} // namespace catfeeder
