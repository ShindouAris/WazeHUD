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
    bool animationActive() const { return marqueeActive_ || clockActive_; }

private:
    void renderRegion(const Rect &region, const HudState &state, const DeviceSettings &settings,
                      const SystemStatusSnapshot &systemStatus);
    void renderSystemStatus(Canvas &canvas, const Rect &region,
                            const SystemStatusSnapshot &systemStatus,
                            const DeviceSettings &settings);
    void renderMainIndicators(Canvas &canvas, const Rect &region,
                              const SystemStatusSnapshot &systemStatus);
    void renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &settings);
    void renderLargeSpeedLimit(Canvas &canvas, const Rect &region, const HudState &state);
    void renderManeuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderSpeed(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderLimits(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderAlerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings);
    void renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings);

    uint16_t *buffer_{nullptr};
    HudState previous_{};
    DeviceSettings previousSettings_{};
    SystemStatusSnapshot previousSystemStatus_{};
    int64_t renderedClockMinute_{INT64_MIN};
    int8_t renderedClockPhase_{-1};
    uint64_t marqueeEpochMs_{0};
    int marqueeOffset_{0};
    int marqueeRenderedOffset_{-1};
    int marqueeTextWidth_{0};
    int marqueeAvailableWidth_{0};
    bool marqueeActive_{false};
    bool clockActive_{false};
    bool firstFrame_{true};
};

}  // namespace waze_hud
