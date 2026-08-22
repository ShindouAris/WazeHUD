#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "protocol/hlp_core.h"
#include <cstdint>

namespace waze_hud {

enum class BleEventKind : uint8_t { Connected, Disconnected, NotificationsEnabled, Data };

struct BleEvent {
    BleEventKind kind{BleEventKind::Data};
    uint16_t length{0};
    uint8_t bytes[HLP_MAX_FRAME]{};
};

class BleTransport {
public:
    static BleTransport &instance();

    esp_err_t init();
    bool receive(BleEvent &event, TickType_t timeout) const;
    esp_err_t sendLine(const char *line);
    bool connected() const;
};

}  // namespace waze_hud
