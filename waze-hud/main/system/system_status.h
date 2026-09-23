#pragma once

#include "esp_err.h"
#include <cstdint>

namespace waze_hud {

struct SystemStatusSnapshot {
    bool visible{false};
    bool batteryPresent{false};
    uint8_t batteryPercent{0};
    uint16_t batteryMillivolts{0};
    bool transportConnected{false};
    uint32_t generation{0};

    bool operator==(const SystemStatusSnapshot &other) const {
        return visible == other.visible && batteryPresent == other.batteryPresent &&
               batteryPercent == other.batteryPercent &&
               batteryMillivolts == other.batteryMillivolts &&
                transportConnected == other.transportConnected &&
               generation == other.generation;
    }
    bool operator!=(const SystemStatusSnapshot &other) const { return !(*this == other); }
};

class SystemStatus {
public:
    static SystemStatus &instance();

    esp_err_t init();
    bool refresh();
    void show();
    void hide();
    SystemStatusSnapshot snapshot() const;

private:
    SystemStatus() = default;
};

}  // namespace waze_hud
