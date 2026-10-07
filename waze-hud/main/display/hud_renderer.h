#pragma once

#include "config/device_config.h"
#include "display/bitmap_font.h"
#include "display/layout.h"
#include "state/hud_state.h"
#include "system/system_status.h"
#include "esp_err.h"
#include <cstdint>

namespace waze_hud {

class HudRenderer {
public:
    esp_err_t init();
    void render(const HudState &state, const DeviceSettings &settings,
                const SystemStatusSnapshot &systemStatus);
    bool animationActive() const { return false; }

private:
    void renderWaiting(Canvas &canvas);
    void renderSystemStatus(Canvas &canvas, const SystemStatusSnapshot &systemStatus,
                            const DeviceSettings &settings);
    void renderSpeedLimitSign(Canvas &canvas, int centerX, int centerY, int radius, int speedLimit);
    void renderMinSpeedSign(Canvas &canvas, int centerX, int centerY, int radius, int minSpeed);
    void renderAlertSide(Canvas &canvas, const AlertState &alert, int maxZoneWidth);
    void renderManeuverSide(Canvas &canvas, Maneuver maneuver, int exit, int distanceM, int maxZoneWidth);
    void renderSpeedSide(Canvas &canvas, int speedKmh, bool overspeed, int centerX, int centerY);

    uint16_t *buffer_{nullptr};
    HudState previous_{};
    DeviceSettings previousSettings_{};
    SystemStatusSnapshot previousSystemStatus_{};
    bool firstFrame_{true};
};

}  // namespace waze_hud
