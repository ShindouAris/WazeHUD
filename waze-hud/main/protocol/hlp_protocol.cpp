#include "protocol/hlp_protocol.h"

#include "serial/serial_transport.h"
#include "config/device_config.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "state/hud_state_store.h"
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "HLP";
constexpr TickType_t kStaleTimeout = pdMS_TO_TICKS(3000);
constexpr TickType_t kSessionTimeout = pdMS_TO_TICKS(10000);
}

HlpProtocol &HlpProtocol::instance() {
    static HlpProtocol protocol;
    return protocol;
}

esp_err_t HlpProtocol::start() {
    hlp_receiver_init(&receiver_, frameEntry, this);
    const BaseType_t result = xTaskCreate(taskEntry, "hlp_protocol", 7168, this, 6, nullptr);
    ESP_RETURN_ON_FALSE(result == pdPASS, ESP_ERR_NO_MEM, kTag, "Protocol task allocation failed");
    return ESP_OK;
}

void HlpProtocol::taskEntry(void *context) {
    static_cast<HlpProtocol *>(context)->run();
    vTaskDelete(nullptr);
}

void HlpProtocol::frameEntry(const char *line, size_t length, void *context) {
    static_cast<HlpProtocol *>(context)->handleFrame(line, length);
}

void HlpProtocol::sendEntry(const char *line, void *) {
    const esp_err_t result = SerialTransport::instance().sendLine(line);
    if (result != ESP_OK)
        ESP_LOGW(kTag, "HLP transmit failed: %s", esp_err_to_name(result));
}

void HlpProtocol::run() {
    SerialEvent event;
    for (;;) {
        if (SerialTransport::instance().receive(event, pdMS_TO_TICKS(100))) {
            if (event.kind == SerialEventKind::Reset) {
                decoder_.resetSession();
                hlp_receiver_init(&receiver_, frameEntry, this);
                sessionEstablished_ = false;
                lastPeerActivity_ = 0;
                HudStateStore::instance().publish(HudState{});
            } else {
                hlp_receiver_feed(&receiver_, event.bytes, event.length);
                if (receiver_.oversized != lastOversized_) {
                    lastOversized_ = receiver_.oversized;
                    ESP_LOGW(kTag, "Dropped oversized HLP frame (total=%lu)", static_cast<unsigned long>(lastOversized_));
                }
                if (receiver_.malformed_utf8 != lastMalformedUtf8_) {
                    lastMalformedUtf8_ = receiver_.malformed_utf8;
                    ESP_LOGW(kTag, "Dropped malformed UTF-8 frame (total=%lu)", static_cast<unsigned long>(lastMalformedUtf8_));
                }
            }
        }
        const TickType_t now = xTaskGetTickCount();
        HudState current = HudStateStore::instance().snapshot();
        if (current.connected && current.hasProducerState && lastPeerActivity_ != 0 &&
            now - lastPeerActivity_ > kStaleTimeout) {
            current.hasProducerState = false;
            current.signalStale = true;
            HudStateStore::instance().publish(current);
            ESP_LOGW(kTag, "HLP stream stale for 3 seconds");
        }
        if (sessionEstablished_ && lastPeerActivity_ != 0 &&
            now - lastPeerActivity_ > kSessionTimeout) {
            decoder_.resetSession();
            hlp_receiver_init(&receiver_, frameEntry, this);
            sessionEstablished_ = false;
            lastPeerActivity_ = 0;
            HudStateStore::instance().publish(HudState{});
        }
    }
}

void HlpProtocol::handleFrame(const char *line, size_t length) {
    if (length == 0) return;
    lastPeerActivity_ = xTaskGetTickCount();
    cJSON *root = cJSON_ParseWithLength(line, length);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        ++malformedJson_;
        ESP_LOGW(kTag, "Dropped malformed JSON frame (total=%lu)", static_cast<unsigned long>(malformedJson_));
        return;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsNumber(version) || version->valuedouble != 1.0 || !cJSON_IsString(type)) {
        ESP_LOGW(kTag, "Ignoring invalid/unsupported HLP envelope");
        cJSON_Delete(root);
        return;
    }

    // Heartbeat takes precedence over all configuration, decoding, and UI work.
    if (std::strcmp(type->valuestring, "ping") == 0) {
        sendEntry("{\"v\":1,\"t\":\"pong\"}", nullptr);
        cJSON_Delete(root);
        return;
    }

    if (std::strcmp(type->valuestring, "error") == 0) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        const cJSON *detail = cJSON_GetObjectItemCaseSensitive(root, "detail");
        ESP_LOGE(kTag, "Peer protocol error: code=%s detail=%s",
                 cJSON_IsString(code) ? code->valuestring : "(missing)",
                 cJSON_IsString(detail) ? detail->valuestring : "(missing)");
        cJSON_Delete(root);
        return;
    }

    HudState state = HudStateStore::instance().snapshot();
    bool changed = false;
    if (std::strcmp(type->valuestring, "hi") == 0) {
        const cJSON *app = cJSON_GetObjectItemCaseSensitive(root, "app");
        const cJSON *appVersion = cJSON_GetObjectItemCaseSensitive(root, "appv");
        const cJSON *rate = cJSON_GetObjectItemCaseSensitive(root, "rate");
        ESP_LOGI(kTag, "Received hi: app=%s appv=%s rate=%d",
                 cJSON_IsString(app) ? app->valuestring : "(missing)",
                 cJSON_IsString(appVersion) ? appVersion->valuestring : "(missing)",
                 cJSON_IsNumber(rate) ? rate->valueint : -1);
        changed = decoder_.handleHi(root, state);
        state.connected = true;
        state.signalStale = false;
        sessionEstablished_ = true;
        changed = true;
        (void)DeviceConfig::instance().handleMessage(root, sendEntry, nullptr);
    } else if (std::strcmp(type->valuestring, "s") == 0) {
        changed = decoder_.decodeState(root, state);
        if (changed && (++stateUpdates_ % 100U) == 0)
            ESP_LOGI(kTag, "Applied %lu state snapshots", static_cast<unsigned long>(stateUpdates_));
    } else if (std::strcmp(type->valuestring, "bye") == 0) {
        const cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        ESP_LOGW(kTag, "Peer sent bye: reason=%s",
                 cJSON_IsString(reason) ? reason->valuestring : "(not supplied)");
        state.hasProducerState = false;
        changed = true;
    } else {
        (void)DeviceConfig::instance().handleMessage(root, sendEntry, nullptr);
    }
    if (changed) HudStateStore::instance().publish(state);
    cJSON_Delete(root);
}

}  // namespace waze_hud
