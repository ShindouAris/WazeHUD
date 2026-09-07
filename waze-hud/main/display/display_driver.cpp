#include "display/display_driver.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_timer.h"
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
#include "esp_lcd_st77922.h"
#elif CONFIG_WAZE_HUD_DISPLAY_CYD_28
#include "esp_lcd_ili9341.h"
#include "esp_lcd_ili9341_init_cmds_1.h"
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
constexpr gpio_num_t kBacklight = GPIO_NUM_41;
constexpr gpio_num_t kCs = GPIO_NUM_10;
constexpr gpio_num_t kClock = GPIO_NUM_12;
constexpr gpio_num_t kData0 = GPIO_NUM_11;
constexpr gpio_num_t kData1 = GPIO_NUM_13;
constexpr gpio_num_t kData2 = GPIO_NUM_14;
constexpr gpio_num_t kData3 = GPIO_NUM_9;
constexpr spi_host_device_t kLcdHost = SPI2_HOST;
constexpr int kNativeWidth = 320;
constexpr int kNativeHeight = 480;
static_assert(layout::PhysicalWidth == kNativeHeight &&
              layout::PhysicalHeight == kNativeWidth,
              "ES3C35P landscape surface must match the rotated native panel");
#elif CONFIG_WAZE_HUD_DISPLAY_CYD_28
constexpr gpio_num_t kBacklight = GPIO_NUM_21;
constexpr gpio_num_t kCs = GPIO_NUM_15;
constexpr gpio_num_t kDc = GPIO_NUM_2;
constexpr gpio_num_t kClock = GPIO_NUM_14;
constexpr gpio_num_t kMosi = GPIO_NUM_13;
constexpr gpio_num_t kMiso = GPIO_NUM_12;
constexpr spi_host_device_t kLcdHost = SPI2_HOST;
constexpr int kNativeWidth = 240;
constexpr int kNativeHeight = 320;
static_assert(layout::PhysicalWidth == kNativeHeight &&
              layout::PhysicalHeight == kNativeWidth,
              "CYD landscape surface must match the rotated native panel");
#else
constexpr gpio_num_t kPower = GPIO_NUM_15;
constexpr gpio_num_t kBacklight = GPIO_NUM_38;
constexpr gpio_num_t kReset = GPIO_NUM_5;
constexpr gpio_num_t kCs = GPIO_NUM_6;
constexpr gpio_num_t kDc = GPIO_NUM_7;
constexpr gpio_num_t kWr = GPIO_NUM_8;
constexpr gpio_num_t kRd = GPIO_NUM_9;
constexpr int kDataPins[8] = {39, 40, 41, 42, 45, 46, 47, 48};
#endif
#if CONFIG_WAZE_HUD_DISPLAY_CYD_28
// SPI submission/wakeup overhead dominates tiny transfers on the classic
// ESP32. Twenty rows keep the DMA buffer modest (12.8 KiB) while reducing a
// full-screen pass from 60 synchronous transactions to 12.
constexpr int kTransferRows = 20;
#else
constexpr int kTransferRows = 4;
#endif

struct InitCommand {
    uint8_t command;
    uint8_t data[16];
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

#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
// LCDWiki ES3C35P panel tuning. The generic esp_lcd_st77922 defaults target a
// different glass geometry and leave this 320x480 panel blank. Keep the panel
// in its native portrait address space; landscape rotation is handled while
// copying each dirty stripe in drawRegion().
constexpr InitCommand kEs3c35pInitCommands[] = {
    {0xF1,{0x00},1,0},
    {0x60,{0x00,0x00,0x00},3,0},
    {0x65,{0x80},1,0},
    {0x79,{0x06},1,0},
    {0x7B,{0x00,0x08,0x08},3,0},
    {0x80,{0x55,0x62,0x2F,0x17,0xF0,0x52,0x70,0xD2,0x52,0x62,0xEA},11,0},
    {0x81,{0x26,0x52,0x72,0x27},4,0},
    {0x84,{0x92,0x25},2,0},
    {0x87,{0x10,0x10,0x58,0x00,0x02,0x3A},6,0},
    {0x88,{0x00,0x00,0x2C,0x10,0x04,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x06},15,0},
    {0x89,{0x00,0x00,0x00},3,0},
    {0x8A,{0x13,0x00,0x2C,0x00,0x00,0x2C,0x10,0x10,0x00,0x3E,0x19},11,0},
    {0x8B,{0x15,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x97,0x8E},9,0},
    {0x8C,{0x1D,0xB1,0xB1,0x44,0x96,0x2C,0x10,0x50,0x0F,0x01,0xC5,0x12,0x09},13,0},
    {0x8D,{0x0C},1,0},
    {0x8E,{0x33,0x01,0x0C,0x13,0x01,0x01},6,0},
    {0xB3,{0x00,0x30},2,0},
    {0xF1,{0x00},1,0},
    {0x71,{0xD0},1,0},
    {0x66,{0x02,0x3F},2,0},
    {0xBE,{0x26,0x00,0x9D},3,0},
    {0x70,{0x01,0xA0,0x11,0x40,0xE0,0x00,0x11,0x69,0x11,0x00,0x00,0x1A},12,0},
    {0x90,{0x04,0x04,0x55,0x74,0x00,0x40,0x43,0x27,0x27},9,0},
    {0x91,{0x04,0x04,0x55,0x75,0x00,0x40,0x42,0x27,0x27},9,0},
    {0x92,{0x04,0x44,0x55,0xC0,0x06,0x00,0x07,0x05,0x90,0x27},10,0},
    {0x93,{0x04,0x43,0x11,0x00,0x00,0x00,0x00,0x05,0x90,0x27},10,0},
    {0x94,{0x00,0x00,0x00,0x00,0x00,0x00},6,0},
    {0x95,{0x96,0x16,0x00,0x00,0xFF},5,0},
    {0x96,{0x44,0x53,0x03,0x12,0x23,0x24,0x06,0x05,0x94,0x27,0x00,0x44},12,0},
    {0x97,{0x44,0x53,0x47,0x56,0x20,0x20,0x02,0x01,0x94,0x27,0x00,0x44},12,0},
    {0xBA,{0x55,0x94,0x2D,0x94,0x27},5,0},
    {0x9A,{0x40,0x00,0x06,0x00,0x00,0x00,0x00},7,0},
    {0x9B,{0x00,0x00,0x06,0x00,0x00,0x00,0x00},7,0},
    {0x9C,{0x5C,0x12,0x00,0x00,0x10,0x12,0x00,0x00,0x10,0x02,0x00,0x00,0x00},13,0},
    {0x9D,{0x8A,0x51,0x00,0x00,0x00,0x80,0x1E,0x01},8,0},
    {0x9E,{0x51,0x00,0x00,0x00,0x80,0x1E,0x01},7,0},
    {0xB4,{0x1D,0x1C,0x1E,0x0B,0x14,0x02,0x13,0x09,0x1E,0x00,0x1E,0x10},12,0},
    {0xB5,{0x1D,0x1C,0x1E,0x0A,0x15,0x03,0x11,0x08,0x1E,0x01,0x1E,0x12},12,0},
    {0xB6,{0x77,0x77,0x00,0x0A,0xFF,0x0A,0xFF},7,0},
    {0x86,{0xCD,0x04,0xB1,0x02,0x58,0x12,0x58,0x0C,0x13,0x01,0xA5,0x00,0xA5,0xA5},14,0},
    {0xB7,{0x07,0x0A,0x0E,0x06,0x05,0x03,0x2B,0x03,0x03,0x42,0x07,0x10,0x10,0x2E,0x3F,0x0D},16,0},
    {0xB8,{0x07,0x0A,0x0D,0x05,0x05,0x02,0x2B,0x02,0x03,0x42,0x06,0x10,0x0F,0x2E,0x3F,0x0D},16,0},
    {0xB9,{0x23,0x23},2,0},
    {0xBF,{0x10,0x14,0x14,0x0B,0x0B,0x0B},6,0},
    {0xF2,{0x00},1,0},
    {0x73,{0x04,0xDA,0x12,0x54,0x47},5,0},
    {0x77,{0x6B,0x5B,0xFD,0xC3,0xC5},5,0},
    {0x7A,{0x15,0x27},2,0},
    {0x7B,{0x04,0x57},2,0},
    {0x7E,{0x01,0x0E},2,0},
    {0xBF,{0x36},1,0},
    {0xE3,{0x40,0x40},2,0},
    {0xF0,{0x00},1,0},
    {0xD0,{0x00},1,0},
    {0x2A,{0x00,0x00,0x01,0x3F},4,0},
    {0x2B,{0x00,0x00,0x01,0xDF},4,0},
    {0x21,{},0,0},
    {0x11,{},0,120},
    {0x29,{},0,0},
    {0x2C,{},0,0},
    {0x3A,{0x01},1,0},
    {0x36,{0x00},1,0},
    {0x35,{0x01},1,20},
};
#endif

bool onTransferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(ctx), &wake);
    return wake == pdTRUE;
}

#if !CONFIG_WAZE_HUD_DISPLAY_35_480X320 && !CONFIG_WAZE_HUD_DISPLAY_CYD_28
esp_err_t configureOutput(gpio_num_t pin, int level) {
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << static_cast<unsigned>(pin);
    config.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "GPIO %d configuration failed", pin);
    return gpio_set_level(pin, level);
}
#endif

}  // namespace

DisplayDriver &DisplayDriver::instance() {
    static DisplayDriver driver;
    return driver;
}

esp_err_t DisplayDriver::init() {
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
    ESP_LOGI(kTag, "Initializing ES3C35P ST77922 QSPI panel at 480x320 landscape");
#elif CONFIG_WAZE_HUD_DISPLAY_CYD_28
    ESP_LOGI(kTag, "Initializing ESP32-2432S028 ILI9341 SPI panel at 320x240 landscape");
#else
    ESP_LOGI(kTag, "Initializing T-Display-S3 ST7789V i80 panel");
    ESP_RETURN_ON_ERROR(configureOutput(kPower, 1), kTag, "Peripheral power enable failed");
    ESP_RETURN_ON_ERROR(configureOutput(kRd, 1), kTag, "LCD RD setup failed");
    vTaskDelay(pdMS_TO_TICKS(10));
#endif

    auto semaphore = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(semaphore != nullptr, ESP_ERR_NO_MEM, kTag, "Transfer semaphore allocation failed");
    transferDone_ = semaphore;

#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
    spi_bus_config_t bus{};
    bus.sclk_io_num = kClock;
    bus.data0_io_num = kData0;
    bus.data1_io_num = kData1;
    bus.data2_io_num = kData2;
    bus.data3_io_num = kData3;
    bus.max_transfer_sz = layout::PhysicalWidth * 10 * static_cast<int>(sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(kLcdHost, &bus, SPI_DMA_CH_AUTO),
                        kTag, "ST77922 QSPI bus initialization failed");

    esp_lcd_panel_io_spi_config_t ioConfig{};
    ioConfig.cs_gpio_num = kCs;
    ioConfig.dc_gpio_num = -1;
    ioConfig.spi_mode = 0;
    ioConfig.pclk_hz = 80 * 1000 * 1000;
    ioConfig.on_color_trans_done = onTransferDone;
    ioConfig.user_ctx = semaphore;
    ioConfig.lcd_cmd_bits = 32;
    ioConfig.lcd_param_bits = 8;
    ioConfig.flags.quad_mode = true;
    // Rendering is synchronous at the display boundary, so one outstanding
    // transaction is enough and keeps the DMA footprint deterministic.
    ioConfig.trans_queue_depth = 1;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost),
                                 &ioConfig, &io),
        kTag, "ST77922 QSPI panel IO creation failed");
    io_ = io;

    constexpr size_t kEs3c35pInitCount =
        sizeof(kEs3c35pInitCommands) / sizeof(kEs3c35pInitCommands[0]);
    static st77922_lcd_init_cmd_t nativeInit[kEs3c35pInitCount]{};
    for (size_t index = 0; index < kEs3c35pInitCount; ++index) {
        nativeInit[index].cmd = kEs3c35pInitCommands[index].command;
        nativeInit[index].data = kEs3c35pInitCommands[index].data;
        nativeInit[index].data_bytes = kEs3c35pInitCommands[index].length;
        nativeInit[index].delay_ms = kEs3c35pInitCommands[index].delayMs;
    }
    st77922_vendor_config_t vendorConfig{};
    vendorConfig.init_cmds = nativeInit;
    vendorConfig.init_cmds_size = kEs3c35pInitCount;
    vendorConfig.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panelConfig{};
    // LCD RESET is tied to ESP32-S3 EN on ES3C35P. Use the controller's
    // software reset after the MCU has booted instead of toggling EN.
    panelConfig.reset_gpio_num = -1;
    panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panelConfig.bits_per_pixel = 16;
    panelConfig.vendor_config = &vendorConfig;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77922(io, &panelConfig, &panel),
                        kTag, "ST77922 driver creation failed");
    panel_ = panel;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), kTag, "ST77922 software reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), kTag, "ST77922 initialization failed");
    // This ES3C35P glass uses inverted source polarity: RGB565 zero must be
    // driven through INVON to appear as the HUD's black background.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), kTag,
                        "ST77922 inversion setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), kTag,
                        "ST77922 display enable failed");

    rotationBuffer_ = heap_caps_malloc(
        layout::PhysicalWidth * kTransferRows * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(rotationBuffer_ != nullptr, ESP_ERR_NO_MEM, kTag,
                        "ST77922 rotation stripe allocation failed");
#elif CONFIG_WAZE_HUD_DISPLAY_CYD_28
    spi_bus_config_t bus{};
    bus.mosi_io_num = kMosi;
    bus.miso_io_num = kMiso;
    bus.sclk_io_num = kClock;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = layout::PhysicalWidth * kTransferRows *
                          static_cast<int>(sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(kLcdHost, &bus, SPI_DMA_CH_AUTO),
                        kTag, "ILI9341 SPI bus initialization failed");

    esp_lcd_panel_io_spi_config_t ioConfig{};
    ioConfig.cs_gpio_num = kCs;
    ioConfig.dc_gpio_num = kDc;
    ioConfig.spi_mode = 0;
    ioConfig.pclk_hz = 40 * 1000 * 1000;
    ioConfig.on_color_trans_done = onTransferDone;
    ioConfig.user_ctx = semaphore;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;
    ioConfig.trans_queue_depth = 1;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost),
                                 &ioConfig, &io),
        kTag, "ILI9341 panel IO creation failed");
    io_ = io;

    ili9341_vendor_config_t vendorConfig{};
    vendorConfig.init_cmds = ili9341_lcd_init_vendor;
    vendorConfig.init_cmds_size =
        sizeof(ili9341_lcd_init_vendor) / sizeof(ili9341_lcd_init_cmd_t);
    esp_lcd_panel_dev_config_t panelConfig{};
    // TFT reset is tied to the ESP32 EN signal on the resistive CYD revision.
    panelConfig.reset_gpio_num = -1;
    panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panelConfig.bits_per_pixel = 16;
    panelConfig.vendor_config = &vendorConfig;
    esp_lcd_panel_handle_t panel = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panelConfig, &panel),
                        kTag, "ILI9341 driver creation failed");
    panel_ = panel;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), kTag, "ILI9341 software reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), kTag, "ILI9341 initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, true), kTag,
                        "ILI9341 inversion setup failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), kTag,
                        "ILI9341 display enable failed");

    rotationBuffer_ = heap_caps_malloc(
        layout::PhysicalWidth * kTransferRows * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(rotationBuffer_ != nullptr, ESP_ERR_NO_MEM, kTag,
                        "ILI9341 rotation stripe allocation failed");
#else
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
#endif

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

    // Clear retained panel GRAM before the UI task starts. The controller can
    // preserve the previous firmware's pixels across an MCU-only reset.
    auto *clearBuffer = static_cast<uint16_t *>(heap_caps_malloc(
        layout::PhysicalWidth * kTransferRows * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    ESP_RETURN_ON_FALSE(clearBuffer != nullptr, ESP_ERR_NO_MEM, kTag, "Startup clear buffer allocation failed");
    std::fill(clearBuffer, clearBuffer + layout::PhysicalWidth * kTransferRows, static_cast<uint16_t>(0x0000));
    const int64_t clearStartedUs = esp_timer_get_time();
    for (int y = 0; y < layout::PhysicalHeight; y += kTransferRows) {
        const Rect stripe{0, static_cast<int16_t>(y), layout::PhysicalWidth,
                          static_cast<int16_t>(std::min(kTransferRows, layout::PhysicalHeight - y))};
        const esp_err_t clearResult = drawRegion(stripe, clearBuffer);
        if (clearResult != ESP_OK) {
            heap_caps_free(clearBuffer);
            ESP_LOGE(kTag, "Startup LCD clear failed at row %d", y);
            return clearResult;
        }
    }
    heap_caps_free(clearBuffer);
    ESP_LOGI(kTag, "Startup LCD clear completed in %lld ms",
             static_cast<long long>((esp_timer_get_time() - clearStartedUs) / 1000));
    ESP_LOGI(kTag, "Display ready at %dx%d landscape",
             layout::PhysicalWidth, layout::PhysicalHeight);
    return ESP_OK;
}

esp_err_t DisplayDriver::drawRegion(const Rect &region, uint16_t *pixels) {
    ESP_RETURN_ON_FALSE(ready_ && panel_ != nullptr && pixels != nullptr, ESP_ERR_INVALID_STATE,
                        kTag, "Display is not ready");
    ESP_RETURN_ON_FALSE(region.x >= 0 && region.y >= 0 && region.width > 0 && region.height > 0 &&
                        region.x + region.width <= layout::PhysicalWidth &&
                        region.y + region.height <= layout::PhysicalHeight,
                        ESP_ERR_INVALID_ARG, kTag, "Invalid dirty region");
    auto panel = static_cast<esp_lcd_panel_handle_t>(panel_);
    // Keep transfers in short descriptors. Rotated panels also use each row
    // stripe as a bounded software-transpose unit.
    for (int row = 0; row < region.height; row += kTransferRows) {
        const int rows = std::min(kTransferRows, region.height - row);
        uint16_t *stripe = pixels + row * region.width;
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320 || CONFIG_WAZE_HUD_DISPLAY_CYD_28
        auto *rotated = static_cast<uint16_t *>(rotationBuffer_);
        ESP_RETURN_ON_FALSE(rotated != nullptr, ESP_ERR_INVALID_STATE, kTag,
                            "LCD rotation buffer unavailable");

        // Compose the user-facing landscape transforms first, then rotate the
        // result clockwise into the ST77922 native 320x480 address space.
        auto mapPoint = [this](int landscapeX, int landscapeY, int &nativeX, int &nativeY) {
            int transformedX = landscapeX;
            int transformedY = landscapeY;
            if (rotated180_) {
                transformedX = layout::PhysicalWidth - 1 - transformedX;
                transformedY = layout::PhysicalHeight - 1 - transformedY;
            }
            if (mirrored_) transformedX = layout::PhysicalWidth - 1 - transformedX;
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
            nativeX = kNativeWidth - 1 - transformedY;
#else
            nativeX = transformedY;
#endif
            nativeY = transformedX;
        };

        int nativeX0 = kNativeWidth;
        int nativeY0 = kNativeHeight;
        int nativeX1 = -1;
        int nativeY1 = -1;
        const int cornersX[] = {region.x, region.x + region.width - 1};
        const int cornersY[] = {region.y + row, region.y + row + rows - 1};
        for (int sourceX : cornersX) {
            for (int sourceY : cornersY) {
                int nativeX = 0;
                int nativeY = 0;
                mapPoint(sourceX, sourceY, nativeX, nativeY);
                nativeX0 = std::min(nativeX0, nativeX);
                nativeY0 = std::min(nativeY0, nativeY);
                nativeX1 = std::max(nativeX1, nativeX);
                nativeY1 = std::max(nativeY1, nativeY);
            }
        }
        const int nativeWidth = nativeX1 - nativeX0 + 1;
        const int nativeHeight = nativeY1 - nativeY0 + 1;
        ESP_RETURN_ON_FALSE(nativeWidth * nativeHeight == region.width * rows,
                            ESP_ERR_INVALID_SIZE, kTag, "Invalid rotated dirty stripe");
        for (int sourceY = 0; sourceY < rows; ++sourceY) {
            for (int sourceX = 0; sourceX < region.width; ++sourceX) {
                int nativeX = 0;
                int nativeY = 0;
                mapPoint(region.x + sourceX, region.y + row + sourceY, nativeX, nativeY);
                rotated[(nativeY - nativeY0) * nativeWidth + (nativeX - nativeX0)] =
                    __builtin_bswap16(stripe[sourceY * region.width + sourceX]);
            }
        }
        const esp_err_t drawResult = esp_lcd_panel_draw_bitmap(
            panel, nativeX0, nativeY0, nativeX1 + 1, nativeY1 + 1, rotated);
#else
        const esp_err_t drawResult = esp_lcd_panel_draw_bitmap(
            panel, region.x, region.y + row,
            region.x + region.width, region.y + row + rows, stripe);
#endif
        if (drawResult != ESP_OK) {
            ESP_LOGE(kTag, "LCD transfer failed: %s", esp_err_to_name(drawResult));
            return drawResult;
        }
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

esp_err_t DisplayDriver::setOrientation(bool mirrored, bool rotated180) {
    ESP_RETURN_ON_FALSE(ready_ && panel_ != nullptr, ESP_ERR_INVALID_STATE,
                        kTag, "Display is not ready");
    // XY is swapped for 320x170 landscape. Native X toggles the 180-degree
    // mounting orientation; native Y is the logical horizontal mirror used
    // for windshield projection. Compose both transforms rather than letting
    // one setting overwrite the other.
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320 || CONFIG_WAZE_HUD_DISPLAY_CYD_28
    // Both portrait-native panels use the deterministic software rotation in
    // drawRegion(), so compose HUD mirror/rotation there as well.
    mirrored_ = mirrored;
    rotated180_ = rotated180;
    return ESP_OK;
#else
    const bool mirrorX = rotated180;
    const bool baseMirrorY = !rotated180;
    const bool mirrorY = mirrored ? !baseMirrorY : baseMirrorY;
    return esp_lcd_panel_mirror(static_cast<esp_lcd_panel_handle_t>(panel_), mirrorX, mirrorY);
#endif
}

}  // namespace waze_hud
