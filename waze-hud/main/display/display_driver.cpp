#include "display/display_driver.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";
constexpr gpio_num_t kPower = GPIO_NUM_15;
constexpr gpio_num_t kBacklight = GPIO_NUM_38;
constexpr gpio_num_t kReset = GPIO_NUM_5;
constexpr gpio_num_t kCs = GPIO_NUM_6;
constexpr gpio_num_t kDc = GPIO_NUM_7;
constexpr gpio_num_t kWr = GPIO_NUM_8;
constexpr gpio_num_t kRd = GPIO_NUM_9;
constexpr int kDataPins[8] = {39, 40, 41, 42, 45, 46, 47, 48};

struct InitCommand {
    uint8_t command;
    uint8_t data[14];
    uint8_t length;
    uint16_t delayMs;
};

// LilyGO's ST7789V tuning sequence for the 1.9-inch T-Display-S3 panel.
constexpr InitCommand kInitCommands[] = {
    {0x11, {}, 0, 120},
    {0x3A, {0x05}, 1, 0},
    {0xB2, {0x0B, 0x0B, 0x00, 0x33, 0x33}, 5, 0},
    {0xB7, {0x75}, 1, 0},
    {0xBB, {0x28}, 1, 0},
    {0xC0, {0x2C}, 1, 0},
    {0xC2, {0x01}, 1, 0},
    {0xC3, {0x1F}, 1, 0},
    {0xC6, {0x13}, 1, 0},
    {0xD0, {0xA7}, 1, 0},
    {0xD0, {0xA4, 0xA1}, 2, 0},
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xF0,0x05,0x0A,0x06,0x06,0x03,0x2B,0x32,0x43,0x36,0x11,0x10,0x2B,0x32}, 14, 0},
    {0xE1, {0xF0,0x08,0x0C,0x0B,0x09,0x24,0x2B,0x22,0x43,0x38,0x15,0x16,0x2F,0x37}, 14, 0},
};

bool onTransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &wake);
    return wake == pdTRUE;
}

esp_err_t configureOutput(gpio_num_t pin, int level) {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    config.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "GPIO %d configuration failed", pin);
    return gpio_set_level(pin, level);
}
}  // namespace

DisplayDriver &DisplayDriver::instance() {
    static DisplayDriver driver;
    return driver;
}

esp_err_t DisplayDriver::init() {
    ESP_LOGI(kTag, "Initializing T-Display-S3 ST7789V i80 panel");
    ESP_RETURN_ON_ERROR(configureOutput(kPower, 1), kTag, "Peripheral power enable failed");
    ESP_RETURN_ON_ERROR(configureOutput(kRd, 1), kTag, "LCD RD setup failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    auto semaphore = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(semaphore != nullptr, ESP_ERR_NO_MEM, kTag, "Transfer semaphore allocation failed");
    transferDone_ = semaphore;

    esp_lcd_i80_bus_config_t bus{};
    bus.dc_gpio_num = kDc;
    bus.wr_gpio_num = kWr;
    bus.clk_src = LCD_CLK_SRC_PLL160M;
    for (int i = 0; i < 8; ++i) bus.data_gpio_nums[i] = kDataPins[i];
    bus.bus_width = 8;
    bus.max_transfer_bytes = layout::MaxRegionPixels * sizeof(uint16_t);
    bus.dma_burst_size = 64;
    esp_lcd_i80_bus_handle_t i80 = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus, &i80), kTag, "i80 bus creation failed");

    esp_lcd_panel_io_i80_config_t ioConfig{};
    ioConfig.cs_gpio_num = kCs;
    ioConfig.pclk_hz = 10 * 1000 * 1000;
    ioConfig.trans_queue_depth = 1;
    ioConfig.on_color_trans_done = onTransferDone;
    ioConfig.user_ctx = semaphore;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;
    ioConfig.dc_levels.dc_idle_level = 0;
    ioConfig.dc_levels.dc_cmd_level = 0;
    ioConfig.dc_levels.dc_dummy_level = 0;
    ioConfig.dc_levels.dc_data_level = 1;
    ioConfig.flags.swap_color_bytes = 1;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i80(i80, &ioConfig, &io), kTag, "panel IO creation failed");
    io_ = io;

    esp_lcd_panel_dev_config_t panelConfig{};
    panelConfig.reset_gpio_num = kReset;
    panelConfig.rgb_endian = LCD_RGB_ENDIAN_RGB;
    panelConfig.bits_per_pixel = 16;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panelConfig, &panel), kTag, "ST7789 driver creation failed");
    panel_ = panel;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), kTag, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), kTag, "panel initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), kTag, "panel inversion failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel, true), kTag, "landscape transform failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel, false, true), kTag, "panel mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, 0, 35), kTag, "panel gap setup failed");

    for (const auto &command : kInitCommands) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command.command, command.data, command.length),
                            kTag, "panel command 0x%02x failed", command.command);
        if (command.delayMs != 0) vTaskDelay(pdMS_TO_TICKS(command.delayMs));
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), kTag, "display enable failed");

    ledc_timer_config_t timer{};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.timer_num = LEDC_TIMER_0;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), kTag, "backlight timer failed");
    ledc_channel_config_t channel{};
    channel.gpio_num = kBacklight;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.intr_type = LEDC_INTR_DISABLE;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), kTag, "backlight channel failed");

    ready_ = true;

    // Clear retained ST7789 GRAM before the UI task starts. The controller can
    // preserve the previous firmware's pixels across an MCU-only reset.
    constexpr int kClearRows = 10;
    auto *clearBuffer = static_cast<uint16_t *>(heap_caps_malloc(
        layout::Width * kClearRows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    ESP_RETURN_ON_FALSE(clearBuffer != nullptr, ESP_ERR_NO_MEM, kTag, "Startup clear buffer allocation failed");
    std::fill(clearBuffer, clearBuffer + layout::Width * kClearRows, static_cast<uint16_t>(0x0000));
    for (int y = 0; y < layout::Height; y += kClearRows) {
        const Rect stripe{0, static_cast<int16_t>(y), layout::Width,
                          static_cast<int16_t>(std::min(kClearRows, layout::Height - y))};
        const esp_err_t clearResult = drawRegion(stripe, clearBuffer);
        if (clearResult != ESP_OK) {
            heap_caps_free(clearBuffer);
            ESP_LOGE(kTag, "Startup LCD clear failed at row %d", y);
            return clearResult;
        }
    }
    heap_caps_free(clearBuffer);
    ESP_LOGI(kTag, "Startup LCD clear completed");
    ESP_LOGI(kTag, "Display ready at 320x170 landscape");
    return ESP_OK;
}

esp_err_t DisplayDriver::drawRegion(const Rect &region, const uint16_t *pixels) {
    ESP_RETURN_ON_FALSE(ready_ && panel_ != nullptr && pixels != nullptr, ESP_ERR_INVALID_STATE,
                        kTag, "Display is not ready");
    ESP_RETURN_ON_FALSE(region.x >= 0 && region.y >= 0 && region.width > 0 && region.height > 0 &&
                        region.x + region.width <= layout::Width && region.y + region.height <= layout::Height,
                        ESP_ERR_INVALID_ARG, kTag, "Invalid dirty region");
    auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
    // The S3 i80/GDMA path is most reliable with short descriptors. Keep the
    // renderer's dirty regions, but transmit them as bounded row stripes.
    constexpr int kTransferRows = 10;
    for (int row = 0; row < region.height; row += kTransferRows) {
        const int rows = std::min(kTransferRows, region.height - row);
        const uint16_t *stripe = pixels + row * region.width;
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, region.x, region.y + row,
                                                      region.x + region.width, region.y + row + rows, stripe),
                            kTag, "LCD transfer failed");
        if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(transferDone_), pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGE(kTag, "LCD transfer timed out at row %d", region.y + row);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

esp_err_t DisplayDriver::setBrightness(uint8_t percent) {
    percent = percent > 100 ? 100 : percent;
    const uint32_t duty = (1023U * percent) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty), kTag, "brightness duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

}  // namespace waze_hud
