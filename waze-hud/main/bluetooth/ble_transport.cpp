#include "bluetooth/ble_transport.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <algorithm>
#include <atomic>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "BLE";
constexpr char kDeviceName[] = "WazeHUD";

const ble_uuid128_t kService = BLE_UUID128_INIT(
    0x01,0x00,0x4c,0x50,0x4c,0x48,0x9d,0x9a,0x48,0x4c,0x6e,0x4d,0x01,0x00,0x7e,0x8a);
const ble_uuid128_t kTx = BLE_UUID128_INIT(
    0x01,0x00,0x4c,0x50,0x4c,0x48,0x9d,0x9a,0x48,0x4c,0x6e,0x4d,0x02,0x00,0x7e,0x8a);
const ble_uuid128_t kRx = BLE_UUID128_INIT(
    0x01,0x00,0x4c,0x50,0x4c,0x48,0x9d,0x9a,0x48,0x4c,0x6e,0x4d,0x03,0x00,0x7e,0x8a);
const ble_uuid128_t kCapabilities = BLE_UUID128_INIT(
    0x01,0x00,0x4c,0x50,0x4c,0x48,0x9d,0x9a,0x48,0x4c,0x6e,0x4d,0x04,0x00,0x7e,0x8a);

QueueHandle_t eventQueue = nullptr;
std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<bool> notifications{false};
uint16_t rxValueHandle = 0;
uint8_t ownAddressType = 0;
ble_gap_adv_params advertisingParameters{};

bool enqueueControl(BleEventKind kind) {
    BleEvent event{}; event.kind = kind;
    return eventQueue && xQueueSend(eventQueue, &event, 0) == pdTRUE;
}

int accessCallback(uint16_t, uint16_t, ble_gatt_access_ctxt *context, void *) {
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        const uint16_t length = OS_MBUF_PKTLEN(context->om);
        if (length == 0 || length > HLP_MAX_FRAME) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        BleEvent event{}; event.kind = BleEventKind::Data; event.length = length;
        if (os_mbuf_copydata(context->om, 0, length, event.bytes) != 0) return BLE_ATT_ERR_UNLIKELY;
        if (!eventQueue || xQueueSend(eventQueue, &event, 0) != pdTRUE) {
            ESP_LOGW(kTag, "RX queue full; applying GATT backpressure");
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        constexpr char caps[] = "{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}\n";
        return os_mbuf_append(context->om, caps, sizeof(caps) - 1) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

ble_gatt_chr_def characteristics[] = {
    {&kTx.u, accessCallback, nullptr, nullptr, BLE_GATT_CHR_F_WRITE, 0, nullptr, nullptr},
    {&kRx.u, accessCallback, nullptr, nullptr, BLE_GATT_CHR_F_NOTIFY, 0, &rxValueHandle, nullptr},
    {&kCapabilities.u, accessCallback, nullptr, nullptr, BLE_GATT_CHR_F_READ, 0, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};
const ble_gatt_svc_def services[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &kService.u, nullptr, characteristics},
    {0, nullptr, nullptr, nullptr},
};

int startAdvertising();

int gapEvent(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                connection.store(event->connect.conn_handle);
                notifications.store(false);
                ESP_LOGI(kTag, "Phone connected, handle=%u", event->connect.conn_handle);
                enqueueControl(BleEventKind::Connected);
            } else {
                ESP_LOGW(kTag, "Connection failed, status=%d", event->connect.status);
                connection.store(BLE_HS_CONN_HANDLE_NONE);
                startAdvertising();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGW(kTag, "Phone disconnected, reason=%d", event->disconnect.reason);
            connection.store(BLE_HS_CONN_HANDLE_NONE);
            notifications.store(false);
            enqueueControl(BleEventKind::Disconnected);
            startAdvertising();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == rxValueHandle) {
                notifications.store(event->subscribe.cur_notify != 0);
                ESP_LOGI(kTag, "RX notifications %s", notifications.load() ? "enabled" : "disabled");
                if (notifications.load()) enqueueControl(BleEventKind::NotificationsEnabled);
            }
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(kTag, "ATT MTU updated to %u", event->mtu.value);
            break;
        default: break;
    }
    return 0;
}

int startAdvertising() {
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t *>(kDeviceName);
    fields.name_len = sizeof(kDeviceName) - 1;
    fields.name_is_complete = 1;
    fields.uuids128 = const_cast<ble_uuid128_t *>(&kService);
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) { ESP_LOGE(kTag, "Advertising fields failed: %d", result); return result; }
    advertisingParameters = {};
    advertisingParameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    advertisingParameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(ownAddressType, nullptr, BLE_HS_FOREVER, &advertisingParameters, gapEvent, nullptr);
    if (result == 0) ESP_LOGI(kTag, "Advertising as %s", kDeviceName);
    else ESP_LOGE(kTag, "Advertising start failed: %d", result);
    return result;
}

void onSync() {
    const int result = ble_hs_id_infer_auto(0, &ownAddressType);
    if (result != 0) { ESP_LOGE(kTag, "Unable to infer BLE address type: %d", result); return; }
    startAdvertising();
}

void onReset(int reason) { ESP_LOGE(kTag, "NimBLE host reset, reason=%d", reason); }

void hostTask(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(nullptr);
}
}  // namespace

BleTransport &BleTransport::instance() {
    static BleTransport transport;
    return transport;
}

esp_err_t BleTransport::init() {
    eventQueue = xQueueCreate(16, sizeof(BleEvent));
    ESP_RETURN_ON_FALSE(eventQueue != nullptr, ESP_ERR_NO_MEM, kTag, "BLE event queue allocation failed");
    ESP_RETURN_ON_ERROR(nimble_port_init(), kTag, "NimBLE initialization failed");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    int result = ble_gatts_count_cfg(services);
    if (result == 0) result = ble_gatts_add_svcs(services);
    if (result == 0) result = ble_svc_gap_device_name_set(kDeviceName);
    ESP_RETURN_ON_FALSE(result == 0, ESP_FAIL, kTag, "GATT service setup failed: %d", result);
    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.reset_cb = onReset;
    nimble_port_freertos_init(hostTask);
    ESP_LOGI(kTag, "NimBLE host started");
    return ESP_OK;
}

bool BleTransport::receive(BleEvent &event, TickType_t timeout) const {
    if (!eventQueue) {
        vTaskDelay(timeout == 0 ? 1 : timeout);
        return false;
    }
    return xQueueReceive(eventQueue, &event, timeout) == pdTRUE;
}

esp_err_t BleTransport::sendLine(const char *line) {
    ESP_RETURN_ON_FALSE(line && notifications.load() && connected(), ESP_ERR_INVALID_STATE,
                        kTag, "Cannot notify before subscription");
    const size_t lineLength = std::strlen(line);
    ESP_RETURN_ON_FALSE(lineLength + 1 <= HLP_MAX_FRAME, ESP_ERR_INVALID_SIZE, kTag, "HLP TX frame too large");
    uint8_t frame[HLP_MAX_FRAME];
    std::memcpy(frame, line, lineLength); frame[lineLength] = '\n';
    const size_t length = lineLength + 1;
    const uint16_t handle = connection.load();
    const uint16_t mtu = ble_att_mtu(handle);
    const size_t payload = mtu > 3 ? mtu - 3 : BLE_ATT_MTU_DFLT - 3;
    for (size_t offset = 0; offset < length; offset += payload) {
        const size_t count = std::min(payload, length - offset);
        int result = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < 50 && result == BLE_HS_ENOMEM; ++attempt) {
            os_mbuf *buffer = ble_hs_mbuf_from_flat(frame + offset, count);
            result = buffer ? ble_gatts_notify_custom(handle, rxValueHandle, buffer) : BLE_HS_ENOMEM;
            if (result == BLE_HS_ENOMEM) vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (result != 0) {
            ESP_LOGW(kTag, "RX notification failed at offset %u: %d", static_cast<unsigned>(offset), result);
            return ESP_FAIL;
        }
        // Notifications are unacknowledged. Small pacing prevents the host and
        // controller mbuf pools from being exhausted when MTU is still 23.
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    return ESP_OK;
}

bool BleTransport::connected() const { return connection.load() != BLE_HS_CONN_HANDLE_NONE; }

bool BleTransport::readRssi(int8_t &rssiDbm) const {
    const uint16_t handle = connection.load();
    if (handle == BLE_HS_CONN_HANDLE_NONE) return false;
    int8_t value = 0;
    const int result = ble_gap_conn_rssi(handle, &value);
    if (result != 0) {
        ESP_LOGW(kTag, "Unable to read connection RSSI: %d", result);
        return false;
    }
    rssiDbm = value;
    return true;
}

}  // namespace waze_hud
