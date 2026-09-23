#include "serial/serial_transport.h"

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <algorithm>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "SERIAL";
constexpr uart_port_t kPort = UART_NUM_0;
constexpr int kTxPin = 1;
constexpr int kRxPin = 3;
constexpr int kBaudRate = CONFIG_WAZE_HUD_SERIAL_BAUD;
constexpr int kRxBufferSize = 2048;
constexpr int kTxBufferSize = 1024;
QueueHandle_t uartEvents = nullptr;
}

SerialTransport &SerialTransport::instance() {
    static SerialTransport transport;
    return transport;
}

esp_err_t SerialTransport::init() {
    const uart_config_t config{
        .baud_rate = kBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };
    ESP_RETURN_ON_ERROR(uart_param_config(kPort, &config), kTag, "UART configuration failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(kPort, kTxPin, kRxPin,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
                        kTag, "UART pin setup failed");
    ESP_RETURN_ON_ERROR(uart_driver_install(kPort, kRxBufferSize, kTxBufferSize, 16,
                                            &uartEvents, 0),
                        kTag, "UART driver installation failed");
    ESP_LOGI(kTag, "HLP serial ready on UART0 at %d 8N1", kBaudRate);
    return ESP_OK;
}

bool SerialTransport::receive(SerialEvent &event, TickType_t timeout) {
    event = {};
    if (!uartEvents) return false;

    if (pendingBytes_ == 0) {
        uart_event_t uartEvent{};
        if (xQueueReceive(uartEvents, &uartEvent, timeout) != pdTRUE) return false;
        if (uartEvent.type == UART_FIFO_OVF || uartEvent.type == UART_BUFFER_FULL) {
            uart_flush_input(kPort);
            xQueueReset(uartEvents);
            event.kind = SerialEventKind::Reset;
            return true;
        }
        if (uartEvent.type != UART_DATA || uartEvent.size == 0) return false;
        pendingBytes_ = uartEvent.size;
    }

    const std::size_t requested = std::min<std::size_t>(pendingBytes_, sizeof(event.bytes));
    const int count = uart_read_bytes(kPort, event.bytes, requested, 0);
    if (count <= 0) {
        pendingBytes_ = 0;
        return false;
    }
    pendingBytes_ -= static_cast<std::size_t>(count);
    event.kind = SerialEventKind::Data;
    event.length = static_cast<uint16_t>(count);
    return true;
}

esp_err_t SerialTransport::sendLine(const char *line) {
    ESP_RETURN_ON_FALSE(line, ESP_ERR_INVALID_ARG, kTag, "HLP line is null");
    const std::size_t length = std::strlen(line);
    ESP_RETURN_ON_FALSE(length + 1 <= HLP_MAX_FRAME, ESP_ERR_INVALID_SIZE,
                        kTag, "HLP TX frame too large");
    const int lineWritten = uart_write_bytes(kPort, line, length);
    const int newlineWritten = lineWritten == static_cast<int>(length)
        ? uart_write_bytes(kPort, "\n", 1) : -1;
    return lineWritten == static_cast<int>(length) && newlineWritten == 1 ? ESP_OK : ESP_FAIL;
}

}  // namespace waze_hud
