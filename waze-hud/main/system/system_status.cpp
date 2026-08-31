#include "system/system_status.h"

#include "bluetooth/ble_transport.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "sdkconfig.h"
#include <algorithm>
#include <array>

namespace waze_hud {
namespace {
constexpr char kTag[] = "SYSTEM";
constexpr adc_unit_t kBatteryAdcUnit = ADC_UNIT_1;
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
constexpr adc_channel_t kBatteryAdcChannel = ADC_CHANNEL_7;  // GPIO8 on ES3C35P.
constexpr int kBatteryGpio = 8;
#else
constexpr adc_channel_t kBatteryAdcChannel = ADC_CHANNEL_3;  // GPIO4 on ESP32-S3.
constexpr int kBatteryGpio = 4;
#endif
constexpr adc_atten_t kBatteryAdcAttenuation = ADC_ATTEN_DB_12;
constexpr unsigned kSampleCount = 16;
constexpr int kDividerRatio = 2;

adc_oneshot_unit_handle_t adcHandle = nullptr;
adc_cali_handle_t calibrationHandle = nullptr;
portMUX_TYPE statusLock = portMUX_INITIALIZER_UNLOCKED;
SystemStatusSnapshot current{};

struct VoltagePoint {
    uint16_t millivolts;
    uint8_t percent;
};

// A conservative unloaded single-cell LiPo discharge curve. It is intentionally
// approximate: T-Display S3 has no fuel gauge and only exposes battery voltage.
constexpr std::array<VoltagePoint, 11> kDischargeCurve{{
    {3300, 0}, {3400, 5}, {3500, 10}, {3600, 20}, {3700, 40}, {3800, 60},
    {3900, 75}, {4000, 88}, {4100, 96}, {4200, 100}, {4350, 100},
}};

uint8_t voltageToPercent(uint16_t millivolts) {
    if (millivolts <= kDischargeCurve.front().millivolts) return 0;
    for (std::size_t index = 1; index < kDischargeCurve.size(); ++index) {
        if (millivolts > kDischargeCurve[index].millivolts) continue;
        const VoltagePoint low = kDischargeCurve[index - 1];
        const VoltagePoint high = kDischargeCurve[index];
        const unsigned span = high.millivolts - low.millivolts;
        const unsigned position = millivolts - low.millivolts;
        return static_cast<uint8_t>(low.percent +
            (position * static_cast<unsigned>(high.percent - low.percent) + span / 2U) / span);
    }
    return 100;
}

bool sampleBattery(uint16_t &batteryMillivolts, uint8_t &batteryPercent) {
    if (!adcHandle) return false;
    int64_t sumMillivolts = 0;
    unsigned validSamples = 0;
    for (unsigned sample = 0; sample < kSampleCount; ++sample) {
        int raw = 0;
        if (adc_oneshot_read(adcHandle, kBatteryAdcChannel, &raw) != ESP_OK) continue;
        int pinMillivolts = 0;
        if (calibrationHandle) {
            if (adc_cali_raw_to_voltage(calibrationHandle, raw, &pinMillivolts) != ESP_OK) continue;
        } else {
            // 12 dB attenuation covers roughly 0..3100 mV on ESP32-S3.
            pinMillivolts = (raw * 3100 + 2047) / 4095;
        }
        sumMillivolts += pinMillivolts;
        ++validSamples;
    }
    if (validSamples == 0) return false;

    const int measured = static_cast<int>((sumMillivolts / validSamples) * kDividerRatio);
    // A reading outside the valid one-cell LiPo range cannot produce a safe %.
    // there is no usable battery voltage. Neither condition can yield a safe %.
    if (measured < 2800 || measured > 4400) {
        batteryMillivolts = static_cast<uint16_t>(std::clamp(measured, 0, 65535));
        return false;
    }
    batteryMillivolts = static_cast<uint16_t>(measured);
    batteryPercent = voltageToPercent(batteryMillivolts);
    return true;
}
}  // namespace

SystemStatus &SystemStatus::instance() {
    static SystemStatus status;
    return status;
}

esp_err_t SystemStatus::init() {
    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = kBatteryAdcUnit;
    unitConfig.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t result = adc_oneshot_new_unit(&unitConfig, &adcHandle);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Battery ADC unit initialization failed: %s", esp_err_to_name(result));
        adcHandle = nullptr;
        return result;
    }

    adc_oneshot_chan_cfg_t channelConfig{};
    channelConfig.atten = kBatteryAdcAttenuation;
    channelConfig.bitwidth = ADC_BITWIDTH_DEFAULT;
    result = adc_oneshot_config_channel(adcHandle, kBatteryAdcChannel, &channelConfig);
    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Battery ADC channel setup failed: %s", esp_err_to_name(result));
        return result;
    }

    adc_cali_curve_fitting_config_t calibrationConfig{};
    calibrationConfig.unit_id = kBatteryAdcUnit;
    calibrationConfig.chan = kBatteryAdcChannel;
    calibrationConfig.atten = kBatteryAdcAttenuation;
    calibrationConfig.bitwidth = ADC_BITWIDTH_DEFAULT;
    result = adc_cali_create_scheme_curve_fitting(&calibrationConfig, &calibrationHandle);
    if (result != ESP_OK) {
        calibrationHandle = nullptr;
        ESP_LOGW(kTag, "ADC calibration unavailable (%s); using bounded raw conversion",
                 esp_err_to_name(result));
    }
    ESP_LOGI(kTag, "Battery ADC ready on GPIO%d (2:1 divider)", kBatteryGpio);
    return ESP_OK;
}

bool SystemStatus::refresh() {
    uint16_t millivolts = 0;
    uint8_t percent = 0;
    const bool batteryPresent = sampleBattery(millivolts, percent);
    int8_t rssi = 0;
    const bool bleConnected = BleTransport::instance().readRssi(rssi);

    taskENTER_CRITICAL(&statusLock);
    const bool changed = current.batteryPresent != batteryPresent ||
                         current.batteryPercent != percent ||
                         current.bleConnected != bleConnected ||
                         current.bleRssiDbm != rssi;
    current.batteryPresent = batteryPresent;
    current.batteryPercent = percent;
    current.batteryMillivolts = millivolts;
    current.bleConnected = bleConnected;
    current.bleRssiDbm = rssi;
    if (changed) ++current.generation;
    taskEXIT_CRITICAL(&statusLock);
    return changed;
}

void SystemStatus::show() {
    (void)refresh();
    taskENTER_CRITICAL(&statusLock);
    current.visible = true;
    ++current.generation;
    const SystemStatusSnapshot logged = current;
    taskEXIT_CRITICAL(&statusLock);
    ESP_LOGI(kTag, "Status shown: battery=%s %u%% %umV, BLE=%s RSSI=%d dBm",
             logged.batteryPresent ? "present" : "unavailable", logged.batteryPercent,
             logged.batteryMillivolts, logged.bleConnected ? "connected" : "disconnected",
             static_cast<int>(logged.bleRssiDbm));
}

void SystemStatus::hide() {
    taskENTER_CRITICAL(&statusLock);
    current.visible = false;
    ++current.generation;
    taskEXIT_CRITICAL(&statusLock);
}

SystemStatusSnapshot SystemStatus::snapshot() const {
    taskENTER_CRITICAL(&statusLock);
    const SystemStatusSnapshot result = current;
    taskEXIT_CRITICAL(&statusLock);
    return result;
}

}  // namespace waze_hud
