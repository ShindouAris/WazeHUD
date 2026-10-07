#include "display/hud_renderer.h"

#include "display/colors.h"
#include "display/display_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";

bool hasAlert(const HudState &state) {
    return state.nearestAlert.kind != AlertKind::None && state.nearestAlert.distanceM >= 0;
}

bool hasManeuver(const HudState &state) {
    return state.navigationActive && state.maneuver != Maneuver::None &&
           state.maneuverDistanceM >= 0;
}

bool isDualSpeedLimit(const HudState &state) {
    return state.hasMinimumSpeed && state.minimumSpeedKmh > 0 && state.speedLimitKmh > 0;
}

bool isOverspeed(const HudState &state, const DeviceSettings &settings) {
    if (state.speedLimitKmh <= 0) return false;
    const int threshold = std::max(0, state.speedLimitKmh + static_cast<int>(settings.overspeedOffsetKmh));
    return state.speedKmh > threshold;
}

void formatDistance(int meters, char *output, size_t capacity) {
    if (meters < 0) { output[0] = 0; return; }
    if (meters < 1000) std::snprintf(output, capacity, "%dM", meters);
    else if (meters < 10000) std::snprintf(output, capacity, "%.1fKM", meters / 1000.0);
    else std::snprintf(output, capacity, "%dKM", (meters + 500) / 1000);
}

const assets::AlphaMask *maneuverAsset(Maneuver maneuver) {
    switch (maneuver) {
        case Maneuver::Continue: return &assets::kManeuverContinue;
        case Maneuver::Left: return &assets::kManeuverLeft;
        case Maneuver::Right: return &assets::kManeuverRight;
        case Maneuver::UTurn: return &assets::kManeuverUTurn;
        case Maneuver::Roundabout: return &assets::kManeuverRoundabout;
        case Maneuver::RoundaboutLeft: return &assets::kManeuverRoundaboutLeft;
        case Maneuver::RoundaboutRight: return &assets::kManeuverRoundaboutRight;
        case Maneuver::RoundaboutStraight: return &assets::kManeuverRoundaboutStraight;
        case Maneuver::RoundaboutUTurn: return &assets::kManeuverRoundaboutUTurn;
        case Maneuver::KeepLeft: return &assets::kManeuverExitLeft;
        case Maneuver::KeepRight: return &assets::kManeuverExitRight;
        case Maneuver::ExitLeft: return &assets::kManeuverExitLeft;
        case Maneuver::ExitRight: return &assets::kManeuverExitRight;
        case Maneuver::Arrive: return &assets::kManeuverArrive;
        default: return nullptr;
    }
}

const assets::ColorBitmap *alertAsset(AlertKind kind, bool dominant) {
    const uint8_t code = static_cast<uint8_t>(kind);
    for (std::size_t index = 0; index < assets::kAlertAssetCount; ++index) {
        const assets::AlertAssetSet &entry = assets::kAlertAssets[index];
        if (entry.code == code) return dominant ? entry.large : entry.small;
    }
    return nullptr;
}

const assets::ColorBitmap *trafficJamAsset(uint8_t severity, bool dominant) {
    switch (severity) {
        case 1: return dominant ? &assets::kAlertTrafficJam1Large : &assets::kAlertTrafficJam1Small;
        case 2: return dominant ? &assets::kAlertTrafficJam2Large : &assets::kAlertTrafficJam2Small;
        case 4: case 5: return dominant ? &assets::kAlertTrafficJam4Large : &assets::kAlertTrafficJam4Small;
        default: return alertAsset(AlertKind::TrafficJam, dominant);
    }
}

uint16_t bleSignalColor(const SystemStatusSnapshot &status) {
    if (!status.bleConnected) return colors::Muted;
    if (status.bleRssiDbm >= -60) return colors::Green;
    if (status.bleRssiDbm >= -75) return colors::Blue;
    if (status.bleRssiDbm >= -85) return colors::Amber;
    return colors::Red;
}
}  // namespace

esp_err_t HudRenderer::init() {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(layout::MaxRegionPixels * sizeof(uint16_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buffer_) {
        ESP_LOGE(kTag, "Unable to allocate %d-byte frame buffer",
                 layout::MaxRegionPixels * static_cast<int>(sizeof(uint16_t)));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void HudRenderer::renderWaiting(Canvas &canvas) {
    canvas.clear(colors::Black);
    if (assets::kBootIcon.pixels && assets::kBootIcon.alpha) {
        canvas.colorBitmap(layout::Width / 2 - assets::kBootIcon.width / 2, 40, assets::kBootIcon);
    }
    canvas.fontText(0, 115, "CHỜ KẾT NỐI...", assets::kTextMedium, colors::White, layout::Width, true);
}

void HudRenderer::renderSystemStatus(Canvas &canvas, const SystemStatusSnapshot &systemStatus,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Black);
    canvas.fontText(0, 7, "TRẠNG THÁI", assets::kTextMedium, colors::White, layout::Width, true);

    constexpr int batteryX = 25;
    constexpr int batteryY = 47;
    constexpr int batteryWidth = 52;
    constexpr int batteryHeight = 27;
    const uint16_t batteryColor = systemStatus.batteryPresent
        ? (systemStatus.batteryPercent <= 15 ? colors::Red
           : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green)
        : colors::Muted;
    canvas.fillRect(batteryX, batteryY, batteryWidth, batteryHeight, batteryColor);
    canvas.fillRect(batteryX + 3, batteryY + 3, batteryWidth - 6, batteryHeight - 6, colors::Black);
    canvas.fillRect(batteryX + batteryWidth, batteryY + 8, 5, batteryHeight - 16, batteryColor);
    if (systemStatus.batteryPresent) {
        const int fillWidth = (batteryWidth - 10) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 5, batteryY + 5, fillWidth, batteryHeight - 10, batteryColor);
    } else {
        canvas.line(batteryX + 8, batteryY + 6, batteryX + batteryWidth - 8, batteryY + batteryHeight - 6, colors::Red, 3);
        canvas.line(batteryX + batteryWidth - 8, batteryY + 6, batteryX + 8, batteryY + batteryHeight - 6, colors::Red, 3);
    }
    char batteryText[24];
    if (systemStatus.batteryPresent)
        std::snprintf(batteryText, sizeof(batteryText), "PIN %u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
    else
        std::snprintf(batteryText, sizeof(batteryText), "KHÔNG CÓ PIN");
    canvas.fontText(95, 49, batteryText, assets::kTextMedium, batteryColor, 215, false);

    constexpr int bluetoothX = 49;
    constexpr int bluetoothTop = 92;
    constexpr int bluetoothBottom = 132;
    const uint16_t bluetoothColor = bleSignalColor(systemStatus);
    canvas.line(bluetoothX, bluetoothTop, bluetoothX, bluetoothBottom, bluetoothColor, 3);
    canvas.line(bluetoothX, bluetoothTop, bluetoothX + 13, 103, bluetoothColor, 3);
    canvas.line(bluetoothX + 13, 103, bluetoothX - 10, 122, bluetoothColor, 3);
    canvas.line(bluetoothX - 10, 101, bluetoothX + 13, 122, bluetoothColor, 3);
    canvas.line(bluetoothX + 13, 122, bluetoothX, bluetoothBottom, bluetoothColor, 3);

    int signalBars = 0;
    if (systemStatus.bleConnected) {
        signalBars = systemStatus.bleRssiDbm >= -55 ? 4 :
                     systemStatus.bleRssiDbm >= -67 ? 3 :
                     systemStatus.bleRssiDbm >= -78 ? 2 :
                     systemStatus.bleRssiDbm >= -90 ? 1 : 0;
    }
    constexpr int barBaseX = 95;
    constexpr int barBottom = 132;
    for (int bar = 0; bar < 4; ++bar) {
        const int height = 10 + bar * 8;
        const uint16_t color = bar < signalBars ? bluetoothColor : colors::Muted;
        canvas.fillRect(barBaseX + bar * 11, barBottom - height, 6, height, color);
    }
    char rssiText[32];
    if (systemStatus.bleConnected)
        std::snprintf(rssiText, sizeof(rssiText), "BLE %d dBm", systemStatus.bleRssiDbm);
    else
        std::snprintf(rssiText, sizeof(rssiText), "CHƯA KẾT NỐI BLE");
    canvas.fontText(150, 102, rssiText, assets::kTextMedium, bluetoothColor, 160, false);
}

void HudRenderer::renderSpeedLimitSign(Canvas &canvas, int centerX, int centerY, int radius, int speedLimit) {
    if (speedLimit <= 0) return;
    const int innerRadius = radius * 74 / 100;
    canvas.fillCircle(centerX, centerY, radius, colors::Red);
    canvas.fillCircle(centerX, centerY, innerRadius, colors::White);
    char limitStr[12];
    std::snprintf(limitStr, sizeof(limitStr), "%d", speedLimit);
    const int fontW = radius * 14 / 10;
    canvas.fontText(centerX - fontW / 2, centerY - assets::kNumberLarge.lineHeight / 2,
                    limitStr, assets::kNumberLarge, colors::Black, fontW, true);
}

void HudRenderer::renderMinSpeedSign(Canvas &canvas, int centerX, int centerY, int radius, int minSpeed) {
    if (minSpeed <= 0) return;
    canvas.fillCircle(centerX, centerY, radius, colors::Blue);
    char limitStr[12];
    std::snprintf(limitStr, sizeof(limitStr), "%d", minSpeed);
    const int fontW = radius * 14 / 10;
    canvas.fontText(centerX - fontW / 2, centerY - assets::kNumberLarge.lineHeight / 2,
                    limitStr, assets::kNumberLarge, colors::White, fontW, true);
}

void HudRenderer::renderAlertSide(Canvas &canvas, const AlertState &alert, int maxZoneWidth) {
    const assets::ColorBitmap *bitmap = alert.kind == AlertKind::TrafficJam
        ? trafficJamAsset(alert.trafficSeverity, true)
        : alertAsset(alert.kind, true);
    if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, true);

    char distanceStr[16];
    formatDistance(alert.distanceM, distanceStr, sizeof(distanceStr));
    const int textW = canvas.fontTextWidth(distanceStr, assets::kTextLarge);

    const int iconW = bitmap ? bitmap->width : 44;
    const int iconH = bitmap ? bitmap->height : 44;
    const int gap = 12;
    const int totalW = iconW + gap + textW;
    const int startX = std::max(6, (maxZoneWidth - totalW) / 2);

    if (bitmap && bitmap->pixels && bitmap->alpha) {
        canvas.colorBitmap(startX, 85 - iconH / 2, *bitmap);
    }
    canvas.fontText(startX + iconW + gap, 85 - assets::kTextLarge.lineHeight / 2,
                    distanceStr, assets::kTextLarge, colors::White, textW + 10, false);
}

void HudRenderer::renderManeuverSide(Canvas &canvas, Maneuver maneuver, int exit, int distanceM, int maxZoneWidth) {
    const assets::AlphaMask *mask = maneuverAsset(maneuver);

    char distanceStr[16];
    formatDistance(distanceM, distanceStr, sizeof(distanceStr));
    const int textW = canvas.fontTextWidth(distanceStr, assets::kTextLarge);

    const int iconW = mask ? mask->width : 60;
    const int iconH = mask ? mask->height : 60;
    const int gap = 10;
    const int totalW = iconW + gap + textW;
    const int startX = std::max(4, (maxZoneWidth - totalW) / 2);
    const int iconY = 85 - iconH / 2;

    if (mask && mask->alpha) {
        canvas.alphaMask(startX, iconY, *mask, colors::White);
        if (exit > 0) {
            char exitStr[16];
            std::snprintf(exitStr, sizeof(exitStr), "%d", std::clamp(exit, 0, 99));
            canvas.fontText(startX + iconW / 2 - 12, 85 - assets::kNumberSmall.lineHeight / 2,
                            exitStr, assets::kNumberSmall, colors::White, 24, true);
        }
    }
    canvas.fontText(startX + iconW + gap, 85 - assets::kTextLarge.lineHeight / 2,
                    distanceStr, assets::kTextLarge, colors::White, textW + 10, false);
}

void HudRenderer::renderSpeedSide(Canvas &canvas, int speedKmh, bool overspeed, int centerX, int centerY) {
    char speedStr[12];
    std::snprintf(speedStr, sizeof(speedStr), "%d", std::clamp(speedKmh, 0, 999));
    const uint16_t color = overspeed ? colors::Red : colors::White;
    canvas.fontText(centerX - 45, centerY - 28, speedStr, assets::kNumberLarge, color, 90, true);
    canvas.fontText(centerX - 30, centerY + 18, "km/h", assets::kTextSmall, colors::Muted, 60, true);
}

void HudRenderer::render(const HudState &state, const DeviceSettings &settings,
                         const SystemStatusSnapshot &systemStatus) {
    const bool changed = firstFrame_ ||
        state.connected != previous_.connected ||
        state.hasProducerState != previous_.hasProducerState ||
        state.speedKmh != previous_.speedKmh ||
        state.speedLimitKmh != previous_.speedLimitKmh ||
        state.hasMinimumSpeed != previous_.hasMinimumSpeed ||
        state.minimumSpeedKmh != previous_.minimumSpeedKmh ||
        !(state.nearestAlert == previous_.nearestAlert) ||
        state.maneuver != previous_.maneuver ||
        state.maneuverDistanceM != previous_.maneuverDistanceM ||
        state.roundaboutExit != previous_.roundaboutExit ||
        state.navigationActive != previous_.navigationActive ||
        systemStatus.visible != previousSystemStatus_.visible ||
        settings.overspeedOffsetKmh != previousSettings_.overspeedOffsetKmh ||
        settings.offsetX != previousSettings_.offsetX ||
        settings.offsetY != previousSettings_.offsetY ||
        settings.revision != previousSettings_.revision;

    if (!changed) return;

    firstFrame_ = false;
    previous_ = state;
    previousSettings_ = settings;
    previousSystemStatus_ = systemStatus;

    Canvas canvas(buffer_, layout::Width, layout::Height);
    canvas.clear(colors::Black);
    canvas.setTranslation(settings.offsetX, settings.offsetY);

    if (systemStatus.visible) {
        renderSystemStatus(canvas, systemStatus, settings);
        DisplayDriver::instance().drawRegion(layout::Full, buffer_);
        return;
    }

    if (!state.connected || !state.hasProducerState) {
        renderWaiting(canvas);
        DisplayDriver::instance().drawRegion(layout::Full, buffer_);
        return;
    }

    const bool overspeed = isOverspeed(state, settings);

    if (isDualSpeedLimit(state)) {
        // Case 3: Dual speed limits (Max limit on left, Min limit on right, Alert/Maneuver in center)
        renderSpeedLimitSign(canvas, 65, 85, 55, state.speedLimitKmh);
        renderMinSpeedSign(canvas, 255, 85, 55, state.minimumSpeedKmh);

        if (hasAlert(state)) {
            const assets::ColorBitmap *bitmap = state.nearestAlert.kind == AlertKind::TrafficJam
                ? trafficJamAsset(state.nearestAlert.trafficSeverity, false)
                : alertAsset(state.nearestAlert.kind, false);
            if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, false);

            if (bitmap && bitmap->pixels && bitmap->alpha) {
                canvas.colorBitmap(160 - bitmap->width / 2, 48, *bitmap);
            }
            char distanceStr[16];
            formatDistance(state.nearestAlert.distanceM, distanceStr, sizeof(distanceStr));
            canvas.fontText(120, 84, distanceStr, assets::kTextSmall, colors::White, 80, true);
        } else if (hasManeuver(state)) {
            const assets::AlphaMask *mask = maneuverAsset(state.maneuver);
            if (mask && mask->alpha) {
                canvas.alphaMask(160 - mask->width / 2, 40, *mask, colors::White);
            }
            char distanceStr[16];
            formatDistance(state.maneuverDistanceM, distanceStr, sizeof(distanceStr));
            canvas.fontText(120, 105, distanceStr, assets::kTextSmall, colors::White, 80, true);
        } else {
            // Cruising dual limits: show current speed in center
            char speedStr[12];
            std::snprintf(speedStr, sizeof(speedStr), "%d", std::clamp(state.speedKmh, 0, 999));
            const uint16_t color = overspeed ? colors::Red : colors::White;
            canvas.fontText(120, 58, speedStr, assets::kTextLarge, color, 80, true);
            canvas.fontText(120, 92, "km/h", assets::kTextSmall, colors::Muted, 80, true);
        }
    } else {
        // Standard 2-Zone Mode (Case 1, Case 2, Cruising)
        const bool alertActive = hasAlert(state);
        const bool maneuverActive = hasManeuver(state);

        // Right Zone: Speed limit sign or current speed
        if (state.speedLimitKmh > 0) {
            renderSpeedLimitSign(canvas, 236, 85, 66, state.speedLimitKmh);
        } else if (alertActive || maneuverActive) {
            // No speed limit sign, but left zone is occupied: right zone shows speed
            renderSpeedSide(canvas, state.speedKmh, false, 240, 85);
        }

        // Left Zone: Priority: Alert -> Maneuver -> Current Speed
        if (alertActive) {
            // Case 1: Alert + Distance
            renderAlertSide(canvas, state.nearestAlert, 160);
        } else if (maneuverActive) {
            // Case 2: Maneuver + Distance
            renderManeuverSide(canvas, state.maneuver, state.roundaboutExit,
                               state.maneuverDistanceM, 160);
        } else {
            // Cruising: No alert, no maneuver
            if (state.speedLimitKmh > 0) {
                renderSpeedSide(canvas, state.speedKmh, overspeed, 80, 85);
            } else {
                // Neither speed limit nor alert: center the vehicle speed
                renderSpeedSide(canvas, state.speedKmh, false, 160, 85);
            }
        }
    }

    DisplayDriver::instance().drawRegion(layout::Full, buffer_);
}

}  // namespace waze_hud
