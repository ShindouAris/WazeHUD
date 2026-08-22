#pragma once

#include "state/hud_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <atomic>

namespace waze_hud {

class HudStateStore {
public:
    static HudStateStore &instance();

    bool init();
    void publish(HudState state);
    HudState snapshot() const;
    bool receive(HudState &state, TickType_t timeout) const;
    void refresh();

private:
    HudStateStore() = default;
    QueueHandle_t latest_{nullptr};
    QueueHandle_t updates_{nullptr};
    std::atomic<uint32_t> generation_{0};
};

}  // namespace waze_hud
