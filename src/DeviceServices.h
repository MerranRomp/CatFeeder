#pragma once

#include "catfeeder/Controller.h"
#include "catfeeder/Settings.h"
#include "esp_err.h"

namespace device {
// ESP-IDF-only boundary. HTTP callbacks never invoke the controller.
esp_err_t startServices();
catfeeder::Settings currentSettings();
bool takePendingSettings(catfeeder::Settings& settings);
void publishStatus(catfeeder::State access, catfeeder::State dispensing,
                   bool recognized, std::uint32_t nowMs);
void printWifiDetails(); // Called only through the local serial interface.
} // namespace device
