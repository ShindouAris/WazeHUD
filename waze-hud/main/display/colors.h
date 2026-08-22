#pragma once

#include <cstdint>

namespace waze_hud::colors {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8U) << 8U) | ((g & 0xFCU) << 3U) | (b >> 3U));
}

constexpr uint16_t Background = rgb565(3, 5, 7);
constexpr uint16_t Panel = rgb565(8, 11, 14);
constexpr uint16_t Foreground = rgb565(238, 242, 244);
constexpr uint16_t Muted = rgb565(128, 139, 145);
constexpr uint16_t Red = rgb565(235, 39, 49);
constexpr uint16_t DarkRed = rgb565(142, 18, 26);
constexpr uint16_t Blue = rgb565(26, 105, 220);
constexpr uint16_t Amber = rgb565(255, 174, 31);
constexpr uint16_t Green = rgb565(55, 200, 105);
constexpr uint16_t White = rgb565(255, 255, 255);
constexpr uint16_t Black = rgb565(0, 0, 0);

}  // namespace waze_hud::colors
