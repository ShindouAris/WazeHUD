#pragma once

#include "protocol/hlp_core.h"
#include "protocol/hlp_decoder.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace waze_hud {

class HlpProtocol {
public:
    static HlpProtocol &instance();
    esp_err_t start();

private:
    HlpProtocol() = default;
    static void taskEntry(void *context);
    static void frameEntry(const char *line, size_t length, void *context);
    static void sendEntry(const char *line, void *context);
    void run();
    void handleFrame(const char *line, size_t length);
    void sendDeviceDeclaration();

    hlp_receiver_t receiver_{};
    HlpDecoder decoder_{};
    uint32_t stateUpdates_{0};
    uint32_t malformedJson_{0};
    uint32_t lastOversized_{0};
    uint32_t lastMalformedUtf8_{0};
    TickType_t lastPeerActivity_{0};
};

}  // namespace waze_hud
