#include "state/hud_state_store.h"

#include "esp_log.h"

namespace waze_hud {
namespace {
constexpr char kTag[] = "STATE";
}

HudStateStore &HudStateStore::instance() {
    static HudStateStore store;
    return store;
}

bool HudStateStore::init() {
    if (updates_ != nullptr) return true;
    latest_ = xQueueCreate(1, sizeof(HudState));
    if (latest_ == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate latest-state queue");
        return false;
    }
    updates_ = xQueueCreate(1, sizeof(HudState));
    if (updates_ == nullptr) {
        ESP_LOGE(kTag, "Unable to allocate state snapshot queue");
        vQueueDelete(latest_);
        latest_ = nullptr;
        return false;
    }
    HudState initial{};
    xQueueOverwrite(latest_, &initial);
    return true;
}

void HudStateStore::publish(HudState state) {
    state.localGeneration = generation_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    if (latest_ != nullptr) xQueueOverwrite(latest_, &state);
    if (updates_ != nullptr) xQueueOverwrite(updates_, &state);
}

HudState HudStateStore::snapshot() const {
    HudState copy;
    if (latest_ != nullptr) (void)xQueuePeek(latest_, &copy, portMAX_DELAY);
    return copy;
}

bool HudStateStore::receive(HudState &state, TickType_t timeout) const {
    return updates_ != nullptr && xQueueReceive(updates_, &state, timeout) == pdTRUE;
}

void HudStateStore::refresh() {
    publish(snapshot());
}

}  // namespace waze_hud
