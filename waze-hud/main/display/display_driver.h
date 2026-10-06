#pragma once

#include "display/layout.h"
#include "esp_err.h"
#include <cstdint>

namespace waze_hud {

class DisplayDriver {
public:
    static DisplayDriver &instance();

    esp_err_t init();
    esp_err_t drawRegion(const Rect &region, uint16_t *pixels);
    esp_err_t setBrightness(uint8_t percent);
    esp_err_t setOrientation(bool mirrored, bool rotated180);
    esp_err_t setInvertColor(bool invert);
    esp_err_t setColorBgr(bool bgr);
    esp_err_t setBacklightPin(int pin);
    bool ready() const { return ready_; }

private:
    DisplayDriver() = default;
    bool ready_{false};
    void *panel_{nullptr};
    void *io_{nullptr};
    void *transferDone_{nullptr};
    void *rotationBuffer_{nullptr};
    bool mirrored_{false};
    bool rotated180_{false};
    bool currentInvertColor_{true};
    bool currentColorBgr_{true};
    int currentBacklightPin_{21};
    uint8_t currentBrightness_{70};
};

}  // namespace waze_hud
