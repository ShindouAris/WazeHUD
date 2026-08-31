#pragma once

#include "sdkconfig.h"
#include <cstdint>

namespace waze_hud {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

namespace layout {
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
constexpr bool IsLargeDisplay = true;
constexpr const char *DeviceName = "ESP32-S3 3.5-inch HUD";
constexpr int Width = 320;
constexpr int Height = 213;
constexpr int PhysicalWidth = 480;
constexpr int PhysicalHeight = 320;
constexpr int MainHeight = 175;
constexpr int StreetHeight = Height - MainHeight;
#else
constexpr bool IsLargeDisplay = false;
constexpr const char *DeviceName = "LILYGO T-Display-S3";
constexpr int Width = 320;
constexpr int Height = 170;
constexpr int PhysicalWidth = 320;
constexpr int PhysicalHeight = 170;
constexpr int MainHeight = 140;
constexpr int StreetHeight = Height - MainHeight;
#endif

constexpr Rect Maneuver{0, 0, 85, MainHeight};
constexpr Rect Speed{85, 0, 80, MainHeight};
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
// The ST77922 QSPI driver requires horizontal transfer boundaries aligned to
// four physical pixels. These logical widths map to x={0,128,248,336,480}.
constexpr Rect Limits{165, 0, 59, MainHeight};
constexpr Rect Alerts{224, 0, 96, MainHeight};
#else
constexpr Rect Limits{165, 0, 60, MainHeight};
constexpr Rect Alerts{225, 0, 95, MainHeight};
#endif
constexpr Rect Street{0, MainHeight, Width, StreetHeight};
constexpr Rect Full{0, 0, Width, Height};

constexpr int scaleCoordinate(int value, int logicalExtent, int physicalExtent) {
    return (value * physicalExtent + logicalExtent / 2) / logicalExtent;
}

constexpr Rect physicalRect(const Rect &logical) {
    const int x0 = scaleCoordinate(logical.x, Width, PhysicalWidth);
    const int y0 = scaleCoordinate(logical.y, Height, PhysicalHeight);
    const int x1 = scaleCoordinate(logical.x + logical.width, Width, PhysicalWidth);
    const int y1 = scaleCoordinate(logical.y + logical.height, Height, PhysicalHeight);
    return {static_cast<int16_t>(x0), static_cast<int16_t>(y0),
            static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

constexpr int regionPixels(const Rect &logical) {
    const Rect physical = physicalRect(logical);
    return physical.width * physical.height;
}

constexpr int maxInt(int left, int right) { return left > right ? left : right; }
constexpr int MaxRegionPixels = maxInt(
    maxInt(regionPixels(Maneuver), regionPixels(Speed)),
    maxInt(maxInt(regionPixels(Limits), regionPixels(Alerts)), regionPixels(Street)));

static_assert(physicalRect(Full).x == 0 && physicalRect(Full).y == 0,
              "Display viewport must start at the framebuffer origin");
static_assert(physicalRect(Full).width == PhysicalWidth &&
              physicalRect(Full).height == PhysicalHeight,
              "Display viewport must cover the complete framebuffer");
static_assert(physicalRect(Maneuver).width + physicalRect(Speed).width +
              physicalRect(Limits).width + physicalRect(Alerts).width == PhysicalWidth,
              "HUD columns must cover the framebuffer without gaps");
static_assert(physicalRect(Maneuver).height + physicalRect(Street).height == PhysicalHeight,
              "HUD rows must cover the framebuffer without gaps");
#if CONFIG_WAZE_HUD_DISPLAY_35_480X320
static_assert(physicalRect(Maneuver).x % 4 == 0 &&
              (physicalRect(Maneuver).x + physicalRect(Maneuver).width) % 4 == 0 &&
              physicalRect(Speed).x % 4 == 0 &&
              (physicalRect(Speed).x + physicalRect(Speed).width) % 4 == 0 &&
              physicalRect(Limits).x % 4 == 0 &&
              (physicalRect(Limits).x + physicalRect(Limits).width) % 4 == 0 &&
              physicalRect(Alerts).x % 4 == 0 &&
              (physicalRect(Alerts).x + physicalRect(Alerts).width) % 4 == 0,
              "ST77922 dirty-region X coordinates must be four-pixel aligned");
#endif
}  // namespace layout

}  // namespace waze_hud
