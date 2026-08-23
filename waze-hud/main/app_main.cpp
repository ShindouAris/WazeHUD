#include "bluetooth/ble_transport.h"
#include "config/device_config.h"
#include "display/display_driver.h"
#include "display/hud_renderer.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "protocol/hlp_protocol.h"
#include "sdkconfig.h"
#include "state/hud_state_store.h"
#include <cstdio>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "APP";
uint32_t bootSequence = 0;
constexpr gpio_num_t kOrientationButton = GPIO_NUM_14;

void recordBootReason() {
    nvs_handle_t nvs;
    if (nvs_open("boot_diag", NVS_READWRITE, &nvs) != ESP_OK) return;
    (void)nvs_get_u32(nvs, "boots", &bootSequence);
    ++bootSequence;
    const uint32_t reason = static_cast<uint32_t>(esp_reset_reason());
    char key[8];
    std::snprintf(key, sizeof(key), "r_%02lu", static_cast<unsigned long>(reason));
    uint32_t reasonCount = 0;
    (void)nvs_get_u32(nvs, key, &reasonCount);
    if (reasonCount != UINT32_MAX) ++reasonCount;
    (void)nvs_set_u32(nvs, "boots", bootSequence);
    (void)nvs_set_u32(nvs, "last_reason", reason);
    (void)nvs_set_u32(nvs, key, reasonCount);
    const esp_err_t committed = nvs_commit(nvs);
    nvs_close(nvs);
    if (committed == ESP_OK)
        ESP_LOGW(kTag, "Boot %lu, reset reason=%lu, reason count=%lu",
                 static_cast<unsigned long>(bootSequence), static_cast<unsigned long>(reason),
                 static_cast<unsigned long>(reasonCount));
}

void stableBootTask(void *) {
    vTaskDelay(pdMS_TO_TICKS(10000));
    nvs_handle_t nvs;
    if (nvs_open("boot_diag", NVS_READWRITE, &nvs) == ESP_OK) {
        uint32_t stable = 0;
        (void)nvs_get_u32(nvs, "stable", &stable);
        if (stable != UINT32_MAX) ++stable;
        (void)nvs_set_u32(nvs, "stable", stable);
        (void)nvs_set_u32(nvs, "last_stable", bootSequence);
        (void)nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(kTag, "Boot %lu remained stable for 10 seconds", static_cast<unsigned long>(bootSequence));
    }
    vTaskDelete(nullptr);
}

void orientationButtonTask(void *) {
    for (;;) {
        if (gpio_get_level(kOrientationButton) == 0) {
            vTaskDelay(pdMS_TO_TICKS(40));
            if (gpio_get_level(kOrientationButton) == 0) {
                const esp_err_t result = DeviceConfig::instance().toggleRotation();
                if (result != ESP_OK)
                    ESP_LOGE(kTag, "Orientation button update failed: %s", esp_err_to_name(result));
                while (gpio_get_level(kOrientationButton) == 0)
                    vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t startOrientationButton() {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(kOrientationButton);
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "GPIO14 orientation button setup failed");
    const BaseType_t created = xTaskCreate(orientationButtonTask, "hud_button", 2560,
                                           nullptr, 3, nullptr);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void uiTask(void *) {
    ESP_LOGI(kTag, "UI task started");
    const esp_err_t displayResult = DisplayDriver::instance().init();
    if (displayResult != ESP_OK) {
        ESP_LOGE(kTag, "Display unavailable: %s; UI task stopped", esp_err_to_name(displayResult));
        vTaskDelete(nullptr);
        return;
    }
    HudRenderer renderer;
    if (renderer.init() != ESP_OK) {
        ESP_LOGE(kTag, "Renderer initialization failed; UI task stopped");
        vTaskDelete(nullptr);
        return;
    }
    HudState state = HudStateStore::instance().snapshot();
    renderer.render(state, DeviceConfig::instance().snapshot());
    ESP_LOGI(kTag, "Initial UI frame rendered");
    for (;;) {
        const TickType_t timeout = renderer.animationActive() ? pdMS_TO_TICKS(80) : portMAX_DELAY;
        if (HudStateStore::instance().receive(state, timeout))
            renderer.render(state, DeviceConfig::instance().snapshot());
        else if (renderer.animationActive())
            renderer.render(state, DeviceConfig::instance().snapshot());
    }
}

template <std::size_t Size>
void setText(std::array<char, Size> &target, const char *text) {
    std::snprintf(target.data(), target.size(), "%s", text);
}

#if CONFIG_WAZE_HUD_MOCK_MODE
HudState baseMock() {
    HudState state{};
    state.connected = true;
    state.hasProducerState = true;
    state.navigationActive = true;
    state.speedKmh = 70;
    state.speedLimitKmh = 50;
    state.maneuver = Maneuver::UTurn;
    state.maneuverDistanceM = 350;
    state.hasMinimumSpeed = true;
    state.minimumSpeedKmh = 60;
    state.laneCount = 3;
    state.lanes[0] = {0x05, 0x04};  // straight + left, selected left
    state.lanes[1] = {0x01, 0x01};  // straight, selected
    state.lanes[2] = {0x30, 0x10};  // slight/right, selected slight-right
    setText(state.currentStreet, "Đường Phạm Văn Đồng");
    setText(state.eta, "20:01");
    state.clockUnixSeconds = 1787478627;
    state.timezoneOffsetMinutes = 420;
    state.clockSyncMonotonicMs = 0;
    return state;
}

void mockTask(void *) {
    ESP_LOGW(kTag, "Renderer mock mode enabled; BLE/HLP input is disabled");
    vTaskDelay(pdMS_TO_TICKS(1200));
    constexpr uint32_t kScenarioCount = 10;
    uint32_t scenario = 0;
    for (;;) {
        HudState state = baseMock();
        switch (scenario % kScenarioCount) {
            case 0: break;
            case 1:
                state.nearestAlert = {AlertKind::SpeedCamera, 300, 0};
                state.upcomingAlerts[0] = {AlertKind::Police, 850, 0}; state.upcomingAlertCount = 1;
                break;
            case 2:
                state.nearestAlert = {AlertKind::Police, 120, 0};
                state.upcomingAlerts[0] = {AlertKind::Hazard, 600, 0};
                state.upcomingAlerts[1] = {AlertKind::SpeedDrop, 900, 40}; state.upcomingAlertCount = 2;
                break;
            case 3:
                state.noPassingZone = true; state.noPassingRemainingM = 1250;
                state.nearestAlert = {AlertKind::NoPassing, 0, 0};
                break;
            case 4:
                state.maneuver = Maneuver::RoundaboutRight; state.roundaboutExit = 3; state.maneuverDistanceM = 1200;
                setText(state.currentStreet, "Võ Nguyên Giáp"); break;
            case 5:
                state.speedKmh = 86; state.speedLimitKmh = 60; state.overSpeed = true;
                state.maneuver = Maneuver::SharpLeft; state.maneuverDistanceM = 35; break;
            case 6:
                state = {}; break;
            case 7:
                setText(state.currentStreet, "Đường Cách Mạng Tháng Tám, Phường Bến Thành");
                state.maneuver = Maneuver::ExitRight; state.maneuverDistanceM = 980; break;
            case 8:
                state.maneuver = Maneuver::RoundaboutStraight; state.roundaboutExit = 2;
                state.maneuverDistanceM = 420;
                state.nearestAlert = {AlertKind::PhoneCamera, 650, 0};
                break;
            case 9:
                state.nearestAlert = {AlertKind::Railway, 180, 0};
                state.upcomingAlerts[0] = {AlertKind::Flood, 900, 0};
                state.upcomingAlerts[1] = {AlertKind::SchoolZone, 1400, 0};
                state.upcomingAlertCount = 2;
                break;
        }
        HudStateStore::instance().publish(state);
        ESP_LOGI(kTag, "Mock renderer scenario %lu",
                 static_cast<unsigned long>(scenario % kScenarioCount));
        ++scenario;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif
}  // namespace
}  // namespace waze_hud

extern "C" void app_main() {
    using namespace waze_hud;
    ESP_LOGI("APP", "Waze HUD firmware booting");
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("APP", "NVS requires recovery erase");
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    recordBootReason();
    ESP_ERROR_CHECK(DeviceConfig::instance().init());
    ESP_ERROR_CHECK(HudStateStore::instance().init() ? ESP_OK : ESP_ERR_NO_MEM);

#if !CONFIG_WAZE_HUD_HEADLESS_DIAGNOSTIC
    const BaseType_t created = xTaskCreatePinnedToCore(uiTask, "hud_ui", 12288, nullptr, 5, nullptr,
                                                      CONFIG_WAZE_HUD_UI_TASK_CORE);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#else
    ESP_LOGW("APP", "Headless diagnostic mode: LCD and UI task disabled");
#endif

    ESP_ERROR_CHECK(startOrientationButton());

#if CONFIG_WAZE_HUD_MOCK_MODE
    ESP_ERROR_CHECK(xTaskCreate(mockTask, "hud_mock", 4096, nullptr, 4, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#else
    ESP_ERROR_CHECK(BleTransport::instance().init());
    ESP_ERROR_CHECK(HlpProtocol::instance().start());
#endif

    ESP_ERROR_CHECK(xTaskCreate(stableBootTask, "boot_stable", 2048, nullptr, 2, nullptr) == pdPASS
                        ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_LOGI("APP", "Startup complete; free internal=%lu, PSRAM=%lu",
             static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
