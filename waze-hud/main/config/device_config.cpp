#include "config/device_config.h"

#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "state/hud_state_store.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace waze_hud {
namespace {
constexpr char kTag[] = "CONFIG";
constexpr char kNamespace[] = "hud_cfg";
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
constexpr int kItemCount = 16;
constexpr uint32_t kBitSpeedOffsetKmh = 1U << 14;
constexpr uint32_t kBitSpeedOffsetPct = 1U << 15;
#else
constexpr int kItemCount = 11;
constexpr uint32_t kBitSpeedOffsetKmh = 1U << 9;
constexpr uint32_t kBitSpeedOffsetPct = 1U << 10;
#endif
constexpr uint32_t kSchemaRevision = 10;

bool validBrightness(int value) {
    return value >= 10 && value <= 100 && ((value - 10) % 5) == 0;
}

struct PendingTransaction {
    bool active{false};
    uint32_t id{0};
    uint32_t mask{0};
    int count{0};
    DeviceSettings draft{};
};
PendingTransaction pending;
uint32_t lastTransaction = 0;
bool lastTransactionOk = false;
uint32_t lastTransactionRevision = 0;

cJSON *envelope(const char *type) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "t", type);
    return root;
}

void sendJson(cJSON *root, HlpSendLine send, void *context) {
    if (!root || !send) { if (root) cJSON_Delete(root); return; }
    char *line = cJSON_PrintUnformatted(root);
    if (line) { send(line, context); std::free(line); }
    cJSON_Delete(root);
}

bool exactInteger(const cJSON *item, int minimum, int maximum, int &result) {
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble ||
        item->valuedouble < minimum || item->valuedouble > maximum) return false;
    result = static_cast<int>(item->valuedouble);
    return true;
}

void sendAck(uint32_t tx, bool ok, uint32_t revision, const char *field,
             const char *error, HlpSendLine send, void *context) {
    cJSON *root = envelope("cfg_ack");
    if (!root) return;
    cJSON_AddNumberToObject(root, "tx", tx);
    cJSON_AddBoolToObject(root, "ok", ok);
    if (ok) cJSON_AddNumberToObject(root, "rev", revision);
    if (field) cJSON_AddStringToObject(root, "field", field);
    if (error) cJSON_AddStringToObject(root, "error", error);
    sendJson(root, send, context);
}

esp_err_t saveSettings(const DeviceSettings &settings) {
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(kNamespace, NVS_READWRITE, &nvs), kTag, "NVS open failed");
    esp_err_t result = nvs_set_u8(nvs, "brightness", settings.brightness);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    if (result == ESP_OK) result = nvs_set_u8(nvs, "auto_bright", settings.autoBrightness ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "invert_color", settings.invertColor ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "color_bgr", settings.colorBgr ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "bl_pin", settings.backlightPin);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "over_border", settings.overspeedBorder ? 1 : 0);
#endif
    if (result == ESP_OK) result = nvs_set_u8(nvs, "theme", static_cast<uint8_t>(settings.theme));
    if (result == ESP_OK) result = nvs_set_u8(nvs, "speed_mode", static_cast<uint8_t>(settings.speedDisplayMode));
    if (result == ESP_OK) result = nvs_set_u8(nvs, "street", settings.showStreet ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "mirror", settings.mirrorHud ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_u8(nvs, "rotate", settings.rotateDisplay ? 1 : 0);
    if (result == ESP_OK) result = nvs_set_i8(nvs, "over_offset", settings.overspeedOffsetKmh);
    if (result == ESP_OK) result = nvs_set_i8(nvs, "spd_off_kmh", settings.speedOffsetKmh);
    if (result == ESP_OK) result = nvs_set_i8(nvs, "spd_off_pct", settings.speedOffsetPercent);
    if (result == ESP_OK) result = nvs_set_i8(nvs, "offset_x", settings.offsetX);
    if (result == ESP_OK) result = nvs_set_i8(nvs, "offset_y", settings.offsetY);
    if (result == ESP_OK) result = nvs_set_u32(nvs, "revision", settings.revision);
    if (result == ESP_OK) result = nvs_set_u32(nvs, "schema_ver", kSchemaRevision);
    if (result == ESP_OK) result = nvs_commit(nvs);
    nvs_close(nvs);
    return result;
}

cJSON *schemaItem(uint32_t revision, const char *id, const char *kind, const char *label) {
    cJSON *item = envelope("cfg_item");
    if (!item) return nullptr;
    cJSON_AddNumberToObject(item, "rev", revision);
    cJSON_AddStringToObject(item, "id", id);
    cJSON_AddStringToObject(item, "kind", kind);
    cJSON_AddStringToObject(item, "label", label);
    return item;
}

bool producerSupportsConfig(const cJSON *root) {
    const cJSON *caps = cJSON_GetObjectItemCaseSensitive(root, "caps");
    if (!cJSON_IsArray(caps)) return false;
    const cJSON *entry = nullptr;
    cJSON_ArrayForEach(entry, caps) {
        if (cJSON_IsString(entry) && std::strcmp(entry->valuestring, "device_config") == 0) return true;
    }
    return false;
}
}  // namespace

DeviceConfig &DeviceConfig::instance() {
    static DeviceConfig config;
    return config;
}

esp_err_t DeviceConfig::init() {
    active_.brightness = CONFIG_WAZE_HUD_DEFAULT_BRIGHTNESS;
    nvs_handle_t nvs;
    const esp_err_t opened = nvs_open(kNamespace, NVS_READONLY, &nvs);
    if (opened == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(kTag, "Using default device configuration");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(opened, kTag, "NVS configuration open failed");
    uint8_t byte = 0;
    if (nvs_get_u8(nvs, "brightness", &byte) == ESP_OK) {
        if (validBrightness(byte)) active_.brightness = byte;
        else ESP_LOGW(kTag, "Ignoring stored brightness %u: not aligned to 5%% step", byte);
    }
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    if (nvs_get_u8(nvs, "auto_bright", &byte) == ESP_OK) active_.autoBrightness = byte != 0;
    if (nvs_get_u8(nvs, "invert_color", &byte) == ESP_OK) active_.invertColor = byte != 0;
    if (nvs_get_u8(nvs, "color_bgr", &byte) == ESP_OK) active_.colorBgr = byte != 0;
    if (nvs_get_u8(nvs, "bl_pin", &byte) == ESP_OK && (byte == 21 || byte == 27)) active_.backlightPin = byte;
    if (nvs_get_u8(nvs, "over_border", &byte) == ESP_OK) active_.overspeedBorder = byte != 0;
#endif
    if (nvs_get_u8(nvs, "theme", &byte) == ESP_OK && byte <= static_cast<uint8_t>(UiTheme::Night))
        active_.theme = static_cast<UiTheme>(byte);
    if (nvs_get_u8(nvs, "speed_mode", &byte) == ESP_OK &&
        byte <= static_cast<uint8_t>(SpeedDisplayMode::NoNavigation))
        active_.speedDisplayMode = static_cast<SpeedDisplayMode>(byte);
    if (nvs_get_u8(nvs, "street", &byte) == ESP_OK) active_.showStreet = byte != 0;
    if (nvs_get_u8(nvs, "mirror", &byte) == ESP_OK) active_.mirrorHud = byte != 0;
    if (nvs_get_u8(nvs, "rotate", &byte) == ESP_OK) active_.rotateDisplay = byte != 0;
    int8_t offset = 0;
    if (nvs_get_i8(nvs, "over_offset", &offset) == ESP_OK && offset >= -10 && offset <= 5)
        active_.overspeedOffsetKmh = offset;
    int8_t speedOffset = 0;
    if (nvs_get_i8(nvs, "spd_off_kmh", &speedOffset) == ESP_OK && speedOffset >= -20 && speedOffset <= 20)
        active_.speedOffsetKmh = speedOffset;
    if (nvs_get_i8(nvs, "spd_off_pct", &speedOffset) == ESP_OK && speedOffset >= -20 && speedOffset <= 20)
        active_.speedOffsetPercent = speedOffset;
    if (nvs_get_i8(nvs, "offset_x", &offset) == ESP_OK && offset >= -5 && offset <= 5) active_.offsetX = offset;
    if (nvs_get_i8(nvs, "offset_y", &offset) == ESP_OK && offset >= -5 && offset <= 5) active_.offsetY = offset;
    (void)nvs_get_u32(nvs, "revision", &active_.revision);
    uint32_t storedSchemaRevision = 0;
    (void)nvs_get_u32(nvs, "schema_ver", &storedSchemaRevision);
    const bool migrateSchema = storedSchemaRevision < kSchemaRevision;
    if (migrateSchema) {
        if (active_.revision < kSchemaRevision) active_.revision = kSchemaRevision;
        else if (active_.revision != UINT32_MAX) ++active_.revision;
    }
    nvs_close(nvs);
    if (migrateSchema) {
        ESP_RETURN_ON_ERROR(saveSettings(active_), kTag, "NVS schema migration failed");
        ESP_LOGI(kTag, "Migrated configuration schema to %lu, revision %lu",
                 static_cast<unsigned long>(kSchemaRevision),
                 static_cast<unsigned long>(active_.revision));
    }
    ESP_LOGI(kTag, "Loaded revision %lu, brightness %u%%",
             static_cast<unsigned long>(active_.revision), active_.brightness);
    return ESP_OK;
}

DeviceSettings DeviceConfig::snapshot() const {
    DeviceSettings copy;
    taskENTER_CRITICAL(&lock_);
    copy = active_;
    taskEXIT_CRITICAL(&lock_);
    return copy;
}

esp_err_t DeviceConfig::toggleRotation() {
    DeviceSettings candidate = snapshot();
    candidate.rotateDisplay = !candidate.rotateDisplay;
    ++candidate.revision;
    const esp_err_t saved = saveSettings(candidate);
    if (saved != ESP_OK) {
        ESP_LOGE(kTag, "Button rotation save failed: %s", esp_err_to_name(saved));
        return saved;
    }
    taskENTER_CRITICAL(&lock_);
    active_ = candidate;
    taskEXIT_CRITICAL(&lock_);
    ESP_LOGI(kTag, "Display rotation changed to %s, revision %lu",
             candidate.rotateDisplay ? "USB-right" : "USB-left",
             static_cast<unsigned long>(candidate.revision));
    HudStateStore::instance().refresh();
    return ESP_OK;
}

esp_err_t DeviceConfig::toggleMirror() {
    DeviceSettings candidate = snapshot();
    candidate.mirrorHud = !candidate.mirrorHud;
    ++candidate.revision;
    const esp_err_t saved = saveSettings(candidate);
    if (saved != ESP_OK) {
        ESP_LOGE(kTag, "Button mirror save failed: %s", esp_err_to_name(saved));
        return saved;
    }
    taskENTER_CRITICAL(&lock_);
    active_ = candidate;
    taskEXIT_CRITICAL(&lock_);
    ESP_LOGI(kTag, "HUD mirror changed to %s, revision %lu",
             candidate.mirrorHud ? "on" : "off",
             static_cast<unsigned long>(candidate.revision));
    HudStateStore::instance().refresh();
    return ESP_OK;
}

void DeviceConfig::publishSchema(HlpSendLine send, void *context) {
    const DeviceSettings settings = snapshot();
    cJSON *root = envelope("cfg_begin");
    if (!root) return;
    cJSON_AddNumberToObject(root, "rev", settings.revision);
    cJSON_AddNumberToObject(root, "count", kItemCount);
    cJSON_AddStringToObject(root, "title", "Cấu hình Waze HUD");
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "speed_display", "selection", "Hiển thị tốc độ");
    const char *speedValues[] = {"current_main", "limit_main", "no_nav"};
    const char *speedLabels[] = {"Tốc độ hiện tại", "Biển giới hạn", "Không dẫn đường"};
    cJSON_AddStringToObject(root, "value",
        speedValues[static_cast<uint8_t>(settings.speedDisplayMode)]);
    cJSON *options = cJSON_AddArrayToObject(root, "options");
    for (int i = 0; i < 3; ++i) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", speedValues[i]);
        cJSON_AddStringToObject(option, "label", speedLabels[i]);
        cJSON_AddItemToArray(options, option);
    }
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "brightness", "slider", "Độ sáng");
    cJSON_AddNumberToObject(root, "value", settings.brightness);
    cJSON_AddNumberToObject(root, "min", 10); cJSON_AddNumberToObject(root, "max", 100);
    cJSON_AddNumberToObject(root, "step", 5); sendJson(root, send, context);

#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
    root = schemaItem(settings.revision, "auto_brightness", "toggle", "Tự động độ sáng");
    cJSON_AddBoolToObject(root, "value", settings.autoBrightness); sendJson(root, send, context);

    root = schemaItem(settings.revision, "invert_color", "toggle", "Đảo màu màn hình");
    cJSON_AddBoolToObject(root, "value", settings.invertColor); sendJson(root, send, context);

    root = schemaItem(settings.revision, "color_bgr", "toggle", "Thứ tự màu BGR (Tắt = RGB)");
    cJSON_AddBoolToObject(root, "value", settings.colorBgr); sendJson(root, send, context);

    root = schemaItem(settings.revision, "backlight_pin", "selection", "Chân đèn nền Backlight");
    cJSON_AddStringToObject(root, "value", settings.backlightPin == 27 ? "gpio_27" : "gpio_21");
    options = cJSON_AddArrayToObject(root, "options");
    cJSON *opt1 = cJSON_CreateObject();
    cJSON_AddStringToObject(opt1, "value", "gpio_21");
    cJSON_AddStringToObject(opt1, "label", "GPIO 21 (CYD 2.8\" mặc định)");
    cJSON_AddItemToArray(options, opt1);
    cJSON *opt2 = cJSON_CreateObject();
    cJSON_AddStringToObject(opt2, "value", "gpio_27");
    cJSON_AddStringToObject(opt2, "label", "GPIO 27 (CYD 2.4\" một số bản)");
    cJSON_AddItemToArray(options, opt2);
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "overspeed_border", "toggle", "Viền đỏ khi quá tốc");
    cJSON_AddBoolToObject(root, "value", settings.overspeedBorder); sendJson(root, send, context);
#endif

    root = schemaItem(settings.revision, "theme", "selection", "Giao diện");
    const char *theme = settings.theme == UiTheme::Day ? "day" : settings.theme == UiTheme::Night ? "night" : "auto";
    cJSON_AddStringToObject(root, "value", theme);
    options = cJSON_AddArrayToObject(root, "options");
    const char *values[] = {"auto", "day", "night"};
    const char *labels[] = {"Tự động", "Ban ngày", "Ban đêm"};
    for (int i = 0; i < 3; ++i) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", values[i]); cJSON_AddStringToObject(option, "label", labels[i]);
        cJSON_AddItemToArray(options, option);
    }
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "show_street", "toggle", "Hiện tên đường");
    cJSON_AddBoolToObject(root, "value", settings.showStreet); sendJson(root, send, context);

    root = schemaItem(settings.revision, "mirror_hud", "toggle", "Phản chiếu HUD (Kính lái)");
    cJSON_AddBoolToObject(root, "value", settings.mirrorHud); sendJson(root, send, context);

    root = schemaItem(settings.revision, "rotate_display", "toggle", "Xoay 180° (USB bên phải)");
    cJSON_AddBoolToObject(root, "value", settings.rotateDisplay); sendJson(root, send, context);

    root = schemaItem(settings.revision, "overspeed_offset", "slider", "Ngưỡng cảnh báo tốc độ");
    cJSON_AddNumberToObject(root, "value", settings.overspeedOffsetKmh);
    cJSON_AddNumberToObject(root, "min", -10); cJSON_AddNumberToObject(root, "max", 5);
    cJSON_AddNumberToObject(root, "step", 1);
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "speed_offset_kmh", "slider", "Bù tốc độ (km/h)");
    cJSON_AddNumberToObject(root, "value", settings.speedOffsetKmh);
    cJSON_AddNumberToObject(root, "min", -20); cJSON_AddNumberToObject(root, "max", 20);
    cJSON_AddNumberToObject(root, "step", 1);
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "speed_offset_pct", "slider", "Bù tốc độ (%)");
    cJSON_AddNumberToObject(root, "value", settings.speedOffsetPercent);
    cJSON_AddNumberToObject(root, "min", -20); cJSON_AddNumberToObject(root, "max", 20);
    cJSON_AddNumberToObject(root, "step", 1);
    sendJson(root, send, context);

    root = schemaItem(settings.revision, "offset_x", "integer", "Dịch ngang");
    cJSON_AddNumberToObject(root, "value", settings.offsetX);
    cJSON_AddNumberToObject(root, "min", -5); cJSON_AddNumberToObject(root, "max", 5); sendJson(root, send, context);

    root = schemaItem(settings.revision, "offset_y", "integer", "Dịch dọc");
    cJSON_AddNumberToObject(root, "value", settings.offsetY);
    cJSON_AddNumberToObject(root, "min", -5); cJSON_AddNumberToObject(root, "max", 5); sendJson(root, send, context);

    root = envelope("cfg_end"); cJSON_AddNumberToObject(root, "rev", settings.revision); sendJson(root, send, context);
    ESP_LOGI(kTag, "Published device configuration revision %lu", static_cast<unsigned long>(settings.revision));
}

bool DeviceConfig::handleMessage(const cJSON *root, HlpSendLine send, void *context) {
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(type)) return false;
    if (std::strcmp(type->valuestring, "hi") == 0) {
        if (producerSupportsConfig(root)) publishSchema(send, context);
        return false;  // hi must also reach the session decoder.
    }
    if (std::strcmp(type->valuestring, "cfg_set_begin") == 0) {
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        const cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
        const cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
        int txValue, revValue, countValue;
        if (!exactInteger(tx, 1, INT32_MAX, txValue) || !exactInteger(rev, 1, INT32_MAX, revValue) ||
            !exactInteger(count, 0, 32, countValue)) return true;
        const DeviceSettings current = snapshot();
        if (static_cast<uint32_t>(revValue) != current.revision || countValue != kItemCount) {
            sendAck(txValue, false, current.revision, nullptr, "schema revision/count mismatch", send, context);
            pending = {};
            return true;
        }
        pending = {true, static_cast<uint32_t>(txValue), 0, 0, current};
        return true;
    }
    if (std::strcmp(type->valuestring, "cfg_set") == 0) {
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
        int txValue;
        if (!pending.active || !exactInteger(tx, 1, INT32_MAX, txValue) ||
            static_cast<uint32_t>(txValue) != pending.id || !cJSON_IsString(id) || !value) return true;
        uint32_t bit = 0;
        bool valid = true;
        int integer = 0;
        if (std::strcmp(id->valuestring, "brightness") == 0) {
            bit = 1U << 0; valid = exactInteger(value, 10, 100, integer) && validBrightness(integer);
            if (valid) pending.draft.brightness = static_cast<uint8_t>(integer);
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
        } else if (std::strcmp(id->valuestring, "auto_brightness") == 0) {
            bit = 1U << 9; valid = cJSON_IsBool(value);
            if (valid) pending.draft.autoBrightness = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "invert_color") == 0) {
            bit = 1U << 10; valid = cJSON_IsBool(value);
            if (valid) pending.draft.invertColor = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "color_bgr") == 0) {
            bit = 1U << 11; valid = cJSON_IsBool(value);
            if (valid) pending.draft.colorBgr = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "backlight_pin") == 0) {
            bit = 1U << 12; valid = cJSON_IsString(value);
            if (valid && std::strcmp(value->valuestring, "gpio_21") == 0) {
                pending.draft.backlightPin = 21;
            } else if (valid && std::strcmp(value->valuestring, "gpio_27") == 0) {
                pending.draft.backlightPin = 27;
            } else {
                valid = false;
            }
        } else if (std::strcmp(id->valuestring, "overspeed_border") == 0) {
            bit = 1U << 13; valid = cJSON_IsBool(value);
            if (valid) pending.draft.overspeedBorder = cJSON_IsTrue(value);
#endif
        } else if (std::strcmp(id->valuestring, "theme") == 0) {
            bit = 1U << 1; valid = cJSON_IsString(value);
            if (valid && std::strcmp(value->valuestring, "auto") == 0) pending.draft.theme = UiTheme::Auto;
            else if (valid && std::strcmp(value->valuestring, "day") == 0) pending.draft.theme = UiTheme::Day;
            else if (valid && std::strcmp(value->valuestring, "night") == 0) pending.draft.theme = UiTheme::Night;
            else valid = false;
        } else if (std::strcmp(id->valuestring, "show_street") == 0) {
            bit = 1U << 2; valid = cJSON_IsBool(value); if (valid) pending.draft.showStreet = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "mirror_hud") == 0) {
            bit = 1U << 5; valid = cJSON_IsBool(value); if (valid) pending.draft.mirrorHud = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "rotate_display") == 0) {
            bit = 1U << 6; valid = cJSON_IsBool(value); if (valid) pending.draft.rotateDisplay = cJSON_IsTrue(value);
        } else if (std::strcmp(id->valuestring, "overspeed_offset") == 0) {
            bit = 1U << 7; valid = exactInteger(value, -10, 5, integer);
            if (valid) pending.draft.overspeedOffsetKmh = static_cast<int8_t>(integer);
        } else if (std::strcmp(id->valuestring, "speed_offset_kmh") == 0) {
            bit = kBitSpeedOffsetKmh; valid = exactInteger(value, -20, 20, integer);
            if (valid) pending.draft.speedOffsetKmh = static_cast<int8_t>(integer);
        } else if (std::strcmp(id->valuestring, "speed_offset_pct") == 0) {
            bit = kBitSpeedOffsetPct; valid = exactInteger(value, -20, 20, integer);
            if (valid) pending.draft.speedOffsetPercent = static_cast<int8_t>(integer);
        } else if (std::strcmp(id->valuestring, "offset_x") == 0) {
            bit = 1U << 3; valid = exactInteger(value, -5, 5, integer); if (valid) pending.draft.offsetX = integer;
        } else if (std::strcmp(id->valuestring, "offset_y") == 0) {
            bit = 1U << 4; valid = exactInteger(value, -5, 5, integer); if (valid) pending.draft.offsetY = integer;
        } else if (std::strcmp(id->valuestring, "speed_display") == 0) {
            bit = 1U << 8; valid = cJSON_IsString(value);
            if (valid && std::strcmp(value->valuestring, "current_main") == 0)
                pending.draft.speedDisplayMode = SpeedDisplayMode::CurrentPrimary;
            else if (valid && std::strcmp(value->valuestring, "limit_main") == 0)
                pending.draft.speedDisplayMode = SpeedDisplayMode::LimitPrimary;
            else if (valid && std::strcmp(value->valuestring, "no_nav") == 0)
                pending.draft.speedDisplayMode = SpeedDisplayMode::NoNavigation;
            else valid = false;
        } else valid = false;
        if ((pending.mask & bit) != 0) valid = false;
        if (!valid) {
            sendAck(pending.id, false, snapshot().revision, id->valuestring, "invalid value", send, context);
            pending = {};
        } else { pending.mask |= bit; ++pending.count; }
        return true;
    }
    if (std::strcmp(type->valuestring, "cfg_set_commit") == 0) {
        const cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        int txValue;
        if (!exactInteger(tx, 1, INT32_MAX, txValue)) return true;
        if (!pending.active || static_cast<uint32_t>(txValue) != pending.id) {
            if (static_cast<uint32_t>(txValue) == lastTransaction)
                sendAck(lastTransaction, lastTransactionOk, lastTransactionRevision, nullptr,
                        lastTransactionOk ? nullptr : "previous transaction failed", send, context);
            return true;
        }
        if (pending.count != kItemCount || pending.mask != ((1U << kItemCount) - 1U)) {
            sendAck(pending.id, false, snapshot().revision, nullptr, "incomplete transaction", send, context);
            pending = {};
            return true;
        }

        const DeviceSettings current = snapshot();
        const int8_t draftKmh = pending.draft.speedOffsetKmh;
        const int8_t draftPct = pending.draft.speedOffsetPercent;
        const int8_t currKmh = current.speedOffsetKmh;
        const int8_t currPct = current.speedOffsetPercent;

        bool offsetResetOccurred = false;
        if (draftKmh != currKmh && draftPct == currPct) {
            // User adjusted km/h slider -> reset percent slider to 0
            if (draftKmh != 0 && pending.draft.speedOffsetPercent != 0) {
                pending.draft.speedOffsetPercent = 0;
                offsetResetOccurred = true;
            }
        } else if (draftPct != currPct && draftKmh == currKmh) {
            // User adjusted percent slider -> reset km/h slider to 0
            if (draftPct != 0 && pending.draft.speedOffsetKmh != 0) {
                pending.draft.speedOffsetKmh = 0;
                offsetResetOccurred = true;
            }
        } else if (draftKmh != 0 && draftPct != 0) {
            // Both non-zero: keep km/h, reset percent
            pending.draft.speedOffsetPercent = 0;
            offsetResetOccurred = true;
        }

        pending.draft.revision++;
        const esp_err_t saved = saveSettings(pending.draft);
        lastTransaction = pending.id;
        lastTransactionOk = saved == ESP_OK;
        lastTransactionRevision = saved == ESP_OK ? pending.draft.revision : snapshot().revision;
        if (saved == ESP_OK) {
            taskENTER_CRITICAL(&lock_); active_ = pending.draft; taskEXIT_CRITICAL(&lock_);
            sendAck(pending.id, true, pending.draft.revision, nullptr, nullptr, send, context);
            ESP_LOGI(kTag, "Committed configuration revision %lu (spd_off_kmh: %d, spd_off_pct: %d)",
                     static_cast<unsigned long>(pending.draft.revision),
                     pending.draft.speedOffsetKmh, pending.draft.speedOffsetPercent);
            HudStateStore::instance().refresh();
            if (offsetResetOccurred) {
                publishSchema(send, context);
            }
        } else {
            ESP_LOGE(kTag, "NVS commit failed: %s", esp_err_to_name(saved));
            sendAck(pending.id, false, snapshot().revision, nullptr, "NVS write failed", send, context);
        }
        pending = {};
        return true;
    }
    return false;
}

}  // namespace waze_hud
