#pragma once

#include "display/layout.h"
#include "esp_err.h"
#include <cstdint>

namespace waze_hud {

class DisplayDriver {
public:
    static DisplayDriver &instance();

    esp_err_t init();
    esp_err_t drawRegion(const Rect &region, const uint16_t *pixels);
    esp_err_t setBrightness(uint8_t percent);
    esp_err_t setOrientation(bool mirrored, bool rotated180);
    bool ready() const { return ready_; }

private:
    DisplayDriver() = default;
    bool ready_{false};
    void *panel_{nullptr};
    void *io_{nullptr};
    void *transferDone_{nullptr};
};

}  // namespace waze_hud
