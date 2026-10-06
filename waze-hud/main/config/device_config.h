#pragma once

#include "cJSON.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace waze_hud {

enum class UiTheme : uint8_t { Auto, Day, Night };
enum class SpeedDisplayMode : uint8_t { CurrentPrimary, LimitPrimary, NoNavigation };

struct DeviceSettings {
    uint8_t brightness{70};
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    bool autoBrightness{true};
    bool invertColor{true};
    bool colorBgr{true};
    uint8_t backlightPin{21};
    bool overspeedBorder{true};
#endif
    UiTheme theme{UiTheme::Auto};
    SpeedDisplayMode speedDisplayMode{SpeedDisplayMode::CurrentPrimary};
    bool showStreet{true};
    bool mirrorHud{false};
    bool rotateDisplay{false};
    int8_t overspeedOffsetKmh{0};
    int8_t speedOffsetKmh{0};
    int8_t speedOffsetPercent{0};
    int8_t offsetX{0};
    int8_t offsetY{0};
    uint32_t revision{10};
};

inline int adjustedSpeed(int rawSpeed, const DeviceSettings &settings) {
    if (rawSpeed <= 0) return 0;
    if (settings.speedOffsetKmh != 0) {
        return std::clamp(rawSpeed + static_cast<int>(settings.speedOffsetKmh), 0, 999);
    }
    if (settings.speedOffsetPercent != 0) {
        const float adjusted = rawSpeed * (100.0f + static_cast<float>(settings.speedOffsetPercent)) / 100.0f;
        return std::clamp(static_cast<int>(std::round(adjusted)), 0, 999);
    }
    return std::clamp(rawSpeed, 0, 999);
}

using HlpSendLine = void (*)(const char *line, void *context);

class DeviceConfig {
public:
    static DeviceConfig &instance();

    esp_err_t init();
    DeviceSettings snapshot() const;
    esp_err_t toggleRotation();
    esp_err_t toggleMirror();
    bool handleMessage(const cJSON *root, HlpSendLine send, void *context);
    void publishSchema(HlpSendLine send, void *context);

private:
    DeviceConfig() = default;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    DeviceSettings active_{};
};

}  // namespace waze_hud
