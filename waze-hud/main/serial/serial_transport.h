#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "protocol/hlp_core.h"
#include <cstddef>
#include <cstdint>

namespace waze_hud {

enum class SerialEventKind : uint8_t { Data, Reset };

struct SerialEvent {
    SerialEventKind kind{SerialEventKind::Data};
    uint16_t length{0};
    uint8_t bytes[HLP_MAX_FRAME]{};
};

class SerialTransport {
public:
    static SerialTransport &instance();

    esp_err_t init();
    bool receive(SerialEvent &event, TickType_t timeout);
    esp_err_t sendLine(const char *line);

private:
    SerialTransport() = default;
    std::size_t pendingBytes_{0};
};

}  // namespace waze_hud
