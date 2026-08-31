#include "display/hud_renderer.h"

#include "display/colors.h"
#include "display/display_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "DISPLAY";

constexpr int mainY(int value) {
    return layout::IsLargeDisplay ? value * 5 / 4 : value;
}

constexpr int screenY(int value) {
    return layout::IsLargeDisplay ? value * 5 / 4 : value;
}

uint16_t foreground(const DeviceSettings &settings) {
    return settings.theme == UiTheme::Night ? colors::rgb565(220, 105, 80) : colors::Foreground;
}

template <typename Left, typename Right>
bool sameText(const Left &left, const Right &right) { return std::strcmp(left.data(), right.data()) == 0; }

bool maneuverChanged(const HudState &a, const HudState &b) {
    if (a.maneuver != b.maneuver || a.secondManeuver != b.secondManeuver ||
        a.maneuverDistanceM != b.maneuverDistanceM || a.roundaboutExit != b.roundaboutExit ||
        a.laneCount != b.laneCount || !sameText(a.eta, b.eta)) return true;
    for (uint8_t i = 0; i < a.laneCount; ++i) if (!(a.lanes[i] == b.lanes[i])) return true;
    return false;
}

bool isRoundaboutManeuver(Maneuver maneuver) {
    return maneuver == Maneuver::Roundabout || maneuver == Maneuver::RoundaboutLeft ||
           maneuver == Maneuver::RoundaboutRight ||
           maneuver == Maneuver::RoundaboutStraight ||
           maneuver == Maneuver::RoundaboutUTurn;
}

bool alertsChanged(const HudState &a, const HudState &b) {
    if (!(a.nearestAlert == b.nearestAlert) || a.upcomingAlertCount != b.upcomingAlertCount ||
        a.noPassingZone != b.noPassingZone || a.noPassingRemainingM != b.noPassingRemainingM) return true;
    for (uint8_t i = 0; i < a.upcomingAlertCount; ++i)
        if (!(a.upcomingAlerts[i] == b.upcomingAlerts[i])) return true;
    return false;
}

bool hasSettingsChanged(const DeviceSettings &a, const DeviceSettings &b) {
    return a.brightness != b.brightness || a.theme != b.theme || a.showStreet != b.showStreet ||
           a.mirrorHud != b.mirrorHud || a.rotateDisplay != b.rotateDisplay ||
           a.overspeedOffsetKmh != b.overspeedOffsetKmh ||
           a.offsetX != b.offsetX || a.offsetY != b.offsetY || a.revision != b.revision;
}

bool firmwareOverspeed(const HudState &state, const DeviceSettings &settings) {
    if (state.speedLimitKmh <= 0) return false;
    const int threshold = std::max(0, state.speedLimitKmh +
                                     static_cast<int>(settings.overspeedOffsetKmh));
    return state.speedKmh >= threshold;
}

uint16_t alertDistanceColor(int distanceM, uint16_t normalColor) {
    return distanceM >= 0 && distanceM < 500 ? colors::Blue : normalColor;
}

uint16_t bleSignalColor(const SystemStatusSnapshot &status) {
    if (!status.bleConnected) return colors::Muted;
    if (status.bleRssiDbm >= -60) return colors::Green;
    if (status.bleRssiDbm >= -75) return colors::Blue;
    if (status.bleRssiDbm >= -85) return colors::Amber;
    return colors::Red;
}

int64_t localClockMillis(const HudState &state) {
    if (state.clockUnixSeconds <= 0) return INT64_MIN;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const uint64_t elapsedMs = nowMs >= state.clockSyncMonotonicMs
        ? nowMs - state.clockSyncMonotonicMs : 0U;
    return state.clockUnixSeconds * 1000LL + static_cast<int64_t>(elapsedMs) +
           static_cast<int64_t>(state.timezoneOffsetMinutes) * 60000LL;
}

const char *displayStreet(const HudState &state) {
    // Hiển thị tên đường
    if (state.currentStreet[0] != 0) return state.currentStreet.data();
    return "Cầu đường chưa đặt tên";
}

bool sameRegion(const Rect &left, const Rect &right) {
    return left.x == right.x && left.y == right.y && left.width == right.width &&
           left.height == right.height;
}

void arrowHead(Canvas &canvas, int x, int y, int dx, int dy, uint16_t color, int thickness) {
    if (std::abs(dx) >= std::abs(dy)) {
        const int sign = dx >= 0 ? 1 : -1;
        canvas.line(x, y, x - sign * 9, y - 7, color, thickness);
        canvas.line(x, y, x - sign * 9, y + 7, color, thickness);
    } else {
        const int sign = dy >= 0 ? 1 : -1;
        canvas.line(x, y, x - 7, y - sign * 9, color, thickness);
        canvas.line(x, y, x + 7, y - sign * 9, color, thickness);
    }
}

void laneArrowHead(Canvas &canvas, int x, int y, int dx, int dy, uint16_t color, int thickness) {
    constexpr int size = 2;
    if (std::abs(dx) >= std::abs(dy)) {
        const int sign = dx >= 0 ? 1 : -1;
        canvas.line(x,y,x-sign*size,y-size,color,thickness);
        canvas.line(x,y,x-sign*size,y+size,color,thickness);
    } else {
        const int sign = dy >= 0 ? 1 : -1;
        canvas.line(x,y,x-size,y-sign*size,color,thickness);
        canvas.line(x,y,x+size,y-sign*size,color,thickness);
    }
}

void drawLane(Canvas &canvas, int x, int branchWidth, const LaneState &lane, uint16_t fg) {
    const bool recommendedLane = lane.selectedMask != 0;
    canvas.line(x,mainY(29),x,mainY(16),recommendedLane ? fg : colors::Muted,
                recommendedLane ? 2 : 1);
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t flag = static_cast<uint8_t>(1U << bit);
        if ((lane.directionMask & flag) == 0) continue;
        const bool selected = (lane.selectedMask & flag) != 0;
        const uint16_t color = selected ? fg : colors::Muted;
        const int thickness = selected ? 2 : 1;
        int endX = x;
        int endY = mainY(5);
        switch (bit) {
            case 1: endX = x - std::max(1, branchWidth / 2); endY = mainY(6); break;
            case 2: endX = x - branchWidth; endY = mainY(9); break;
            case 3: endX = x - branchWidth; endY = mainY(13); break;
            case 4: endX = x + std::max(1, branchWidth / 2); endY = mainY(6); break;
            case 5: endX = x + branchWidth; endY = mainY(9); break;
            case 6: endX = x + branchWidth; endY = mainY(13); break;
            case 7:
                endX = x - branchWidth;
                endY = mainY(15);
                canvas.line(x,mainY(16),endX,mainY(10),color,thickness);
                canvas.line(endX,mainY(10),endX,mainY(15),color,thickness);
                laneArrowHead(canvas,endX,mainY(15),0,1,color,thickness);
                continue;
            default: break;
        }
        canvas.line(x,mainY(16),endX,endY,color,thickness);
        laneArrowHead(canvas,endX,endY,endX-x,endY-mainY(16),color,thickness);
    }
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

void drawRoundaboutExit(Canvas &canvas, int exit, uint16_t color) {
    if (exit <= 0) return;
    char number[12];
    std::snprintf(number,sizeof(number),"%d",exit);
    constexpr int centerX = 42;
    constexpr int width = 36;
    canvas.fontText(centerX-width/2,mainY(65)-assets::kNumberMedium.lineHeight/2,
                    number,assets::kNumberMedium,color,width,true);
}

enum class SpeedSignContext { Current, AlertLarge, AlertSmall };

const assets::ColorBitmap *speedLimitAsset(int value, SpeedSignContext context) {
    for (std::size_t index = 0; index < assets::kSpeedLimitAssetCount; ++index) {
        const assets::SpeedLimitAssetSet &entry = assets::kSpeedLimitAssets[index];
        if (entry.value != value) continue;
        switch (context) {
            case SpeedSignContext::Current: return entry.current;
            case SpeedSignContext::AlertLarge: return entry.alertLarge;
            case SpeedSignContext::AlertSmall: return entry.alertSmall;
        }
    }
    return nullptr;
}

void drawManeuverIcon(Canvas &canvas, Maneuver maneuver, int exit, uint16_t color) {
    constexpr int cx = 42;
    const int top = mainY(38);
    const int bottom = mainY(92);
    const int thick = 5;
    if (maneuver == Maneuver::None) return;
    if (const assets::AlphaMask *asset = maneuverAsset(maneuver)) {
        canvas.alphaMask(12, mainY(34), *asset, color);
        if (isRoundaboutManeuver(maneuver) && exit > 0) {
            drawRoundaboutExit(canvas,exit,color);
        }
        return;
    }
    if (maneuver == Maneuver::Arrive) {
        canvas.line(cx - 18, top + 7, cx - 18, bottom - 3, color, 3);
        canvas.fillRect(cx - 15, top + 8, 28, 18, color);
        canvas.fillRect(cx - 10, top + 13, 5, 5, colors::Background);
        canvas.fillRect(cx, top + 13, 5, 5, colors::Background);
        return;
    }
    if (isRoundaboutManeuver(maneuver)) {
        canvas.circle(cx, mainY(65), 20, color, 4);
        canvas.line(cx, bottom, cx, mainY(83), color, thick);
        if (maneuver == Maneuver::RoundaboutStraight) {
            canvas.line(cx, mainY(45), cx, top, color, thick);
            arrowHead(canvas, cx, top, 0, -1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        if (maneuver == Maneuver::RoundaboutUTurn) {
            const int endX = cx - 27;
            canvas.line(cx - 19, mainY(65), endX, mainY(65), color, thick);
            canvas.line(endX, mainY(65), endX, mainY(78), color, thick);
            arrowHead(canvas, endX, mainY(78), 0, 1, color, 3);
            drawRoundaboutExit(canvas,exit,color);
            return;
        }
        const bool left = maneuver == Maneuver::RoundaboutLeft;
        const int endX = left ? cx - 27 : cx + 27;
        canvas.line(left ? cx - 19 : cx + 19, mainY(65), endX, mainY(65), color, thick);
        arrowHead(canvas, endX, mainY(65), left ? -1 : 1, 0, color, 3);
        drawRoundaboutExit(canvas,exit,color);
        return;
    }
    if (maneuver == Maneuver::UTurn || maneuver == Maneuver::UTurnRightReserved) {
        const bool right = maneuver == Maneuver::UTurnRightReserved;
        const int side = right ? 1 : -1;
        canvas.line(cx, bottom, cx, mainY(55), color, thick);
        canvas.line(cx, mainY(55), cx + side * 16, mainY(43), color, thick);
        canvas.line(cx + side * 16, mainY(43), cx + side * 27, mainY(55), color, thick);
        canvas.line(cx + side * 27, mainY(55), cx + side * 27, mainY(68), color, thick);
        arrowHead(canvas, cx + side * 27, mainY(68), 0, 1, color, 3);
        return;
    }

    int endX = cx, endY = top;
    switch (maneuver) {
        case Maneuver::Left: endX = 15; endY = mainY(53); break;
        case Maneuver::Right: endX = 69; endY = mainY(53); break;
        case Maneuver::SlightLeft: case Maneuver::KeepLeft: endX = 21; endY = mainY(42); break;
        case Maneuver::SlightRight: case Maneuver::KeepRight: endX = 63; endY = mainY(42); break;
        case Maneuver::SharpLeft: case Maneuver::ExitLeft: endX = 14; endY = mainY(73); break;
        case Maneuver::SharpRight: case Maneuver::ExitRight: endX = 70; endY = mainY(73); break;
        default: break;
    }
    canvas.line(cx, bottom, cx, mainY(69), color, thick);
    canvas.line(cx, mainY(69), endX, endY, color, thick);
    arrowHead(canvas, endX, endY, endX - cx, endY - mainY(69), color, 3);
}

const assets::ColorBitmap *alertAsset(AlertKind kind, bool dominant) {
    const uint8_t code = static_cast<uint8_t>(kind);
    for (std::size_t index = 0; index < assets::kAlertAssetCount; ++index) {
        const assets::AlertAssetSet &entry = assets::kAlertAssets[index];
        if (entry.code == code) return dominant ? entry.large : entry.small;
    }
    return nullptr;
}

uint16_t trafficSeverityColor(uint8_t severity) {
    switch (severity) {
        case 1: return colors::Green;
        case 2: return colors::Amber;
        case 3: return colors::rgb565(255, 112, 24);
        case 4: case 5: return colors::Red;
        default: return colors::Muted;
    }
}

const assets::ColorBitmap *trafficJamAsset(uint8_t severity, bool dominant) {
    switch (severity) {
        case 1: return dominant ? &assets::kAlertTrafficJam1Large
                                : &assets::kAlertTrafficJam1Small;
        case 2: return dominant ? &assets::kAlertTrafficJam2Large
                                : &assets::kAlertTrafficJam2Small;
        case 4: case 5: return dominant ? &assets::kAlertTrafficJam4Large
                                        : &assets::kAlertTrafficJam4Small;
        default: return alertAsset(AlertKind::TrafficJam, dominant);
    }
}

const char *trafficSeverityLabel(uint8_t severity) {
    switch (severity) {
        case 1: return "NHẸ";
        case 2: return "VỪA";
        case 3: return "NẶNG";
        case 4: return "ĐỨNG IM";
        case 5: return "ĐƯỜNG ĐÓNG";
        default: return "KẸT XE";
    }
}

void drawTrafficSeverityTicks(Canvas &canvas, int centerX, int y,
                              uint8_t severity, uint16_t color) {
    if (severity == 0) return;
    constexpr int tickWidth = 4;
    constexpr int gap = 2;
    constexpr int totalWidth = 5 * tickWidth + 4 * gap;
    const int startX = centerX - totalWidth / 2;
    for (int tick = 0; tick < 5; ++tick)
        canvas.fillRect(startX + tick * (tickWidth + gap), y, tickWidth, 3,
                        tick < severity ? color : colors::Muted);
}

void drawAlertIcon(Canvas &canvas, int cx, int cy, int radius, const AlertState &alert, bool dominant) {
    if (alert.kind == AlertKind::None) return;
    if (alert.kind == AlertKind::SpeedDrop) {
        const assets::ColorBitmap *sign = speedLimitAsset(
            alert.valueKmh, dominant ? SpeedSignContext::AlertLarge : SpeedSignContext::AlertSmall);
        if (sign && sign->pixels && sign->alpha) {
            canvas.colorBitmap(cx - sign->width / 2, cy - sign->height / 2, *sign);
            return;
        }
        const int thick = dominant ? 3 : 2;
        char value[5]{};
        canvas.fillCircle(cx,cy,radius,colors::White);
        canvas.circle(cx,cy,radius,colors::Red,thick);
        std::snprintf(value,sizeof(value),"%d",alert.valueKmh);
        if (dominant)
            canvas.fontText(cx-radius,cy-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,2*radius,true);
        else
            canvas.fontText(cx-radius,cy-assets::kNumberSmall.lineHeight/2,value,
                            assets::kNumberSmall,colors::Black,2*radius,true);
        return;
    }

    const bool trafficJam = alert.kind == AlertKind::TrafficJam;
    const uint16_t severityColor = trafficSeverityColor(alert.trafficSeverity);
    if (trafficJam && alert.trafficSeverity > 0)
        canvas.circle(cx, cy, radius + (dominant ? 3 : 2), severityColor,
                      dominant ? 2 : 1);
    const assets::ColorBitmap *bitmap = trafficJam
        ? trafficJamAsset(alert.trafficSeverity, dominant) : alertAsset(alert.kind, dominant);
    if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, dominant);
    if (bitmap && bitmap->pixels && bitmap->alpha) {
        canvas.colorBitmap(cx - bitmap->width / 2, cy - bitmap->height / 2, *bitmap);
        if (trafficJam && !dominant)
            drawTrafficSeverityTicks(canvas, cx, cy + radius + 3,
                                     alert.trafficSeverity, severityColor);
        return;
    }

    // Last-resort primitive only when the generated hazard asset is absent.
    canvas.triangle(cx,cy-radius,cx-radius,cy+radius,cx+radius,cy+radius,colors::Amber);
    canvas.fontText(cx-radius,cy-assets::kTextMedium.lineHeight/2,"!",assets::kTextMedium,
                    colors::Amber,2*radius,true);
}

void formatDistance(int meters, char *output, size_t capacity) {
    if (meters < 0) { output[0] = 0; return; }
    if (meters < 1000) std::snprintf(output, capacity, "%d M", meters);
    else if (meters < 10000) std::snprintf(output, capacity, "%.1f KM", meters / 1000.0);
    else std::snprintf(output, capacity, "%d KM", (meters + 500) / 1000);
}
}  // namespace

esp_err_t HudRenderer::init() {
    buffer_ = static_cast<uint16_t *>(heap_caps_malloc(layout::MaxRegionPixels * sizeof(uint16_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!buffer_) {
        ESP_LOGE(kTag, "Unable to allocate %d-byte dirty-region buffer",
                 layout::MaxRegionPixels * static_cast<int>(sizeof(uint16_t)));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void HudRenderer::render(const HudState &state, const DeviceSettings &settings,
                         const SystemStatusSnapshot &systemStatus) {
    const int64_t currentClockMillis = localClockMillis(state);
    const int64_t currentClockSecond = currentClockMillis == INT64_MIN
        ? INT64_MIN : currentClockMillis / 1000LL;
    const int64_t currentClockMinute = currentClockSecond == INT64_MIN
        ? INT64_MIN : currentClockSecond / 60;
    const int8_t currentClockPhase = currentClockMillis == INT64_MIN
        ? -1 : static_cast<int8_t>((currentClockMillis % 1000LL) < 500LL);
    clockActive_ = state.connected && state.hasProducerState && currentClockSecond != INT64_MIN;
    const bool streetChanged = firstFrame_ || !sameText(state.currentStreet, previous_.currentStreet);
    const int availableStreetWidth = currentClockMinute != INT64_MIN ? 248 : 310;
    Canvas metrics(buffer_, layout::Street.width, layout::Street.height);
    const int streetWidth = settings.showStreet
        ? metrics.fontTextWidth(displayStreet(state), assets::kTextMedium) : 0;
    const bool shouldMarquee = state.connected && state.hasProducerState && settings.showStreet &&
                               streetWidth > availableStreetWidth;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    if (!shouldMarquee) {
        marqueeActive_ = false;
        marqueeOffset_ = 0;
    } else {
        if (!marqueeActive_ || streetChanged || streetWidth != marqueeTextWidth_ ||
            availableStreetWidth != marqueeAvailableWidth_) {
            marqueeEpochMs_ = nowMs;
            marqueeOffset_ = 0;
            marqueeRenderedOffset_ = -1;
        }
        marqueeActive_ = true;
        marqueeTextWidth_ = streetWidth;
        marqueeAvailableWidth_ = availableStreetWidth;
        constexpr uint64_t kStartHoldMs = 1200;
        constexpr uint64_t kEndHoldMs = 900;
        constexpr uint64_t kMsPerPixel = 45;
        const int overflow = streetWidth - availableStreetWidth;
        const uint64_t scrollMs = static_cast<uint64_t>(overflow) * kMsPerPixel;
        const uint64_t cycleMs = kStartHoldMs + scrollMs + kEndHoldMs;
        const uint64_t elapsed = cycleMs > 0 ? (nowMs - marqueeEpochMs_) % cycleMs : 0;
        if (elapsed < kStartHoldMs) marqueeOffset_ = 0;
        else if (elapsed < kStartHoldMs + scrollMs)
            marqueeOffset_ = std::min(overflow, static_cast<int>((elapsed - kStartHoldMs) / kMsPerPixel));
        else marqueeOffset_ = overflow;
    }
    const bool marqueeFrameChanged = marqueeActive_ && marqueeOffset_ != marqueeRenderedOffset_;
    const bool statusChanged = firstFrame_ || state.connected != previous_.connected ||
                               state.hasProducerState != previous_.hasProducerState ||
                               state.navigationActive != previous_.navigationActive;
    const bool configChanged = firstFrame_ || hasSettingsChanged(settings, previousSettings_);
    if (configChanged) {
        const esp_err_t brightnessResult = DisplayDriver::instance().setBrightness(settings.brightness);
        if (brightnessResult != ESP_OK)
            ESP_LOGE(kTag, "Brightness update failed: %s", esp_err_to_name(brightnessResult));
        const esp_err_t orientationResult = DisplayDriver::instance().setOrientation(
            settings.mirrorHud, settings.rotateDisplay);
        if (orientationResult != ESP_OK)
            ESP_LOGE(kTag, "HUD orientation update failed: %s", esp_err_to_name(orientationResult));
    }
    const bool systemStatusChanged = firstFrame_ || systemStatus != previousSystemStatus_;
    if (systemStatus.visible) {
        if (systemStatusChanged || configChanged) {
            renderRegion(layout::Maneuver,state,settings,systemStatus);
            renderRegion(layout::Speed,state,settings,systemStatus);
            renderRegion(layout::Limits,state,settings,systemStatus);
            renderRegion(layout::Alerts,state,settings,systemStatus);
            renderRegion(layout::Street,state,settings,systemStatus);
        }
        previous_ = state;
        previousSettings_ = settings;
        previousSystemStatus_ = systemStatus;
        firstFrame_ = false;
        return;
    }
    const bool systemStatusClosed = previousSystemStatus_.visible;
    bool streetRendered = false;
    if (systemStatusClosed || statusChanged || configChanged || !state.connected || !state.hasProducerState) {
        renderRegion(layout::Maneuver,state,settings,systemStatus);
        renderRegion(layout::Speed,state,settings,systemStatus);
        renderRegion(layout::Limits,state,settings,systemStatus);
        renderRegion(layout::Alerts,state,settings,systemStatus);
        renderRegion(layout::Street,state,settings,systemStatus);
        streetRendered = true;
    } else {
        if (maneuverChanged(state, previous_)) renderRegion(layout::Maneuver,state,settings,systemStatus);
        if (state.speedKmh != previous_.speedKmh ||
            state.speedLimitKmh != previous_.speedLimitKmh)
            renderRegion(layout::Speed,state,settings,systemStatus);
        if (state.speedLimitKmh != previous_.speedLimitKmh || state.hasMinimumSpeed != previous_.hasMinimumSpeed ||
            state.minimumSpeedKmh != previous_.minimumSpeedKmh) renderRegion(layout::Limits,state,settings,systemStatus);
        if (alertsChanged(state, previous_)) renderRegion(layout::Alerts,state,settings,systemStatus);
        if (streetChanged ||
            settings.showStreet != previousSettings_.showStreet ||
            currentClockMinute != renderedClockMinute_ ||
            currentClockPhase != renderedClockPhase_ || marqueeFrameChanged) {
            renderRegion(layout::Street,state,settings,systemStatus);
            streetRendered = true;
        }
        if (systemStatusChanged) {
            renderRegion(layout::Speed,state,settings,systemStatus);
            renderRegion(layout::Limits,state,settings,systemStatus);
            renderRegion(layout::Alerts,state,settings,systemStatus);
        }
    }
    previous_ = state;
    previousSettings_ = settings;
    previousSystemStatus_ = systemStatus;
    renderedClockMinute_ = currentClockMinute;
    renderedClockPhase_ = currentClockPhase;
    if (streetRendered) marqueeRenderedOffset_ = marqueeOffset_;
    firstFrame_ = false;
}

void HudRenderer::renderRegion(const Rect &region, const HudState &state,
                               const DeviceSettings &settings,
                               const SystemStatusSnapshot &systemStatus) {
    const Rect physicalRegion = layout::physicalRect(region);
    Canvas canvas(buffer_, physicalRegion.width, physicalRegion.height,
                  region.width, region.height);
    canvas.setTranslation(settings.offsetX, settings.offsetY);
    if (systemStatus.visible) renderSystemStatus(canvas, region, systemStatus, settings);
    else if (!state.connected || !state.hasProducerState) renderStatus(canvas, region, state, settings);
    else if (sameRegion(region, layout::Maneuver)) renderManeuver(canvas,state,settings);
    else if (sameRegion(region, layout::Speed)) renderSpeed(canvas,state,settings);
    else if (sameRegion(region, layout::Limits)) renderLimits(canvas,state,settings);
    else if (sameRegion(region, layout::Alerts)) renderAlerts(canvas,state,settings);
    else renderStreet(canvas,state,settings);
    if (!systemStatus.visible && state.connected && state.hasProducerState)
        renderMainIndicators(canvas, region, systemStatus);
    const esp_err_t result = DisplayDriver::instance().drawRegion(physicalRegion, buffer_);
    if (result != ESP_OK) ESP_LOGE(kTag, "Dirty region (%d,%d %dx%d) failed: %s",
                                   region.x,region.y,region.width,region.height,esp_err_to_name(result));
}

void HudRenderer::renderMainIndicators(Canvas &canvas, const Rect &region,
                                       const SystemStatusSnapshot &systemStatus) {
    // Battery is centered across the upper HUD. It is omitted entirely when
    // GPIO4 does not contain a plausible single-cell LiPo voltage.
    if (systemStatus.batteryPresent &&
        (sameRegion(region, layout::Speed) || sameRegion(region, layout::Limits))) {
        constexpr int batteryX = 134;
        const int batteryY = mainY(5);
        constexpr int batteryWidth = 20;
        constexpr int batteryHeight = 11;
        const uint16_t batteryColor = systemStatus.batteryPercent <= 15 ? colors::Red
            : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green;
        canvas.fillRect(batteryX - region.x, batteryY - region.y,
                        batteryWidth, batteryHeight, batteryColor);
        canvas.fillRect(batteryX + 2 - region.x, batteryY + 2 - region.y,
                        batteryWidth - 4, batteryHeight - 4, colors::Background);
        canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 3 - region.y,
                        3, batteryHeight - 6, batteryColor);
        const int fillWidth = (batteryWidth - 6) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                        fillWidth, batteryHeight - 6, batteryColor);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
        canvas.fontText(160 - region.x, mainY(0) - region.y, percent, assets::kTextSmall,
                        batteryColor, 38, false);
    }

    if (sameRegion(region, layout::Alerts)) {
        // Compact Bluetooth rune and four RSSI bars in the upper-right corner.
        constexpr int bluetoothX = 303;
        const int top = mainY(2);
        const int bottom = mainY(14);
        const uint16_t color = bleSignalColor(systemStatus);
        canvas.line(bluetoothX - region.x, top - region.y,
                    bluetoothX - region.x, bottom - region.y, color, 1);
        canvas.line(bluetoothX - region.x, top - region.y,
                    bluetoothX + 4 - region.x, mainY(6) - region.y, color, 1);
        canvas.line(bluetoothX + 4 - region.x, mainY(6) - region.y,
                    bluetoothX - 3 - region.x, mainY(12) - region.y, color, 1);
        canvas.line(bluetoothX - 3 - region.x, mainY(5) - region.y,
                    bluetoothX + 4 - region.x, mainY(11) - region.y, color, 1);
        canvas.line(bluetoothX + 4 - region.x, mainY(11) - region.y,
                    bluetoothX - region.x, bottom - region.y, color, 1);

        int signalBars = 0;
        if (systemStatus.bleConnected) {
            signalBars = systemStatus.bleRssiDbm >= -60 ? 3 :
                         systemStatus.bleRssiDbm >= -75 ? 2 :
                         systemStatus.bleRssiDbm >= -90 ? 1 : 0;
        }
        for (int bar = 0; bar < 3; ++bar) {
            const int height = 3 + bar * 3;
            canvas.fillRect(310 + bar * 4 - region.x, mainY(14) - height - region.y,
                            2, height, bar < signalBars ? color : colors::Muted);
        }
    }
}

void HudRenderer::renderSystemStatus(Canvas &canvas, const Rect &region,
                                     const SystemStatusSnapshot &systemStatus,
                                     const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const uint16_t fg = foreground(settings);
    canvas.fontText(0 - region.x, screenY(7) - region.y, "TRẠNG THÁI",
                    assets::kTextMedium, fg, layout::Width, true);

    // Battery body and terminal. The fill is proportional to the estimated
    // single-cell LiPo charge; an X marks an unavailable/non-battery reading.
    constexpr int batteryX = 25;
    const int batteryY = screenY(47);
    constexpr int batteryWidth = 52;
    constexpr int batteryHeight = 27;
    const uint16_t batteryColor = systemStatus.batteryPresent
        ? (systemStatus.batteryPercent <= 15 ? colors::Red
           : systemStatus.batteryPercent <= 35 ? colors::Amber : colors::Green)
        : colors::Muted;
    canvas.fillRect(batteryX - region.x, batteryY - region.y,
                    batteryWidth, batteryHeight, batteryColor);
    canvas.fillRect(batteryX + 3 - region.x, batteryY + 3 - region.y,
                    batteryWidth - 6, batteryHeight - 6, colors::Background);
    canvas.fillRect(batteryX + batteryWidth - region.x, batteryY + 8 - region.y,
                    5, batteryHeight - 16, batteryColor);
    if (systemStatus.batteryPresent) {
        const int fillWidth = (batteryWidth - 10) * systemStatus.batteryPercent / 100;
        canvas.fillRect(batteryX + 5 - region.x, batteryY + 5 - region.y,
                        fillWidth, batteryHeight - 10, batteryColor);
    } else {
        canvas.line(batteryX + 8 - region.x, batteryY + 6 - region.y,
                    batteryX + batteryWidth - 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
        canvas.line(batteryX + batteryWidth - 8 - region.x, batteryY + 6 - region.y,
                    batteryX + 8 - region.x,
                    batteryY + batteryHeight - 6 - region.y, colors::Red, 3);
    }
    char batteryText[24];
    if (systemStatus.batteryPresent)
        std::snprintf(batteryText, sizeof(batteryText), "PIN %u%%",
                      static_cast<unsigned>(systemStatus.batteryPercent));
    else
        std::snprintf(batteryText, sizeof(batteryText), "KHÔNG CÓ PIN");
    canvas.fontText(95 - region.x, screenY(49) - region.y, batteryText,
                    assets::kTextMedium,
                    batteryColor, 215, false);

    // Bluetooth rune plus four qualitative signal bars.
    constexpr int bluetoothX = 49;
    const int bluetoothTop = screenY(92);
    const int bluetoothBottom = screenY(132);
    const uint16_t bluetoothColor = bleSignalColor(systemStatus);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - region.x, bluetoothTop - region.y,
                bluetoothX + 13 - region.x, screenY(103) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(103) - region.y,
                bluetoothX - 10 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX - 10 - region.x, screenY(101) - region.y,
                bluetoothX + 13 - region.x, screenY(122) - region.y, bluetoothColor, 3);
    canvas.line(bluetoothX + 13 - region.x, screenY(122) - region.y,
                bluetoothX - region.x, bluetoothBottom - region.y, bluetoothColor, 3);

    int signalBars = 0;
    if (systemStatus.bleConnected) {
        signalBars = systemStatus.bleRssiDbm >= -55 ? 4 :
                     systemStatus.bleRssiDbm >= -67 ? 3 :
                     systemStatus.bleRssiDbm >= -78 ? 2 :
                     systemStatus.bleRssiDbm >= -90 ? 1 : 0;
    }
    for (int bar = 0; bar < 4; ++bar) {
        const int height = 5 + bar * 5;
        canvas.fillRect(69 + bar * 6 - region.x, screenY(132) - height - region.y,
                        4, height, bar < signalBars ? bluetoothColor : colors::Muted);
    }
    char bleText[28];
    if (systemStatus.bleConnected)
        std::snprintf(bleText, sizeof(bleText), "BLE %d dBm",
                      static_cast<int>(systemStatus.bleRssiDbm));
    else
        std::snprintf(bleText, sizeof(bleText), "BLE CHƯA KẾT NỐI");
    canvas.fontText(100 - region.x, screenY(102) - region.y, bleText,
                    assets::kTextMedium, bluetoothColor, 210, false);
}

void HudRenderer::renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    canvas.colorBitmap(16 - region.x, screenY(25) - region.y, assets::kBootIcon);
    constexpr int copyX = 120;
    constexpr int copyWidth = 190;
    canvas.fontText(copyX - region.x, screenY(34) - region.y, "WazeHUD", assets::kTextLarge,
                    colors::Foreground, copyWidth, true);
    const char *status = state.signalStale ? "Mất tín hiệu" : state.connected ? "Đã kết nối" : "Đang chờ thiết bị";
    const uint16_t statusColor = state.signalStale ? colors::Amber : state.connected ? colors::Green : colors::Muted;
    canvas.fontText(copyX - region.x, screenY(73) - region.y, status, assets::kTextMedium,
                    statusColor, copyWidth, true);
    const char *detail = state.signalStale ? "Đang đợi dữ liệu" : state.connected ? "Đang chờ WazeMod" : "Đang chờ kết nối";
    canvas.fontText(copyX - region.x, screenY(103) - region.y, detail, assets::kTextSmall,
                    foreground(settings), copyWidth, true);
}

void HudRenderer::renderManeuver(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const uint16_t fg = foreground(settings);
    if (state.laneCount > 0) {
        const int spacing = std::max(6, 80 / static_cast<int>(state.laneCount));
        const int totalWidth = spacing * static_cast<int>(state.laneCount);
        const int firstX = (canvas.width() - totalWidth) / 2 + spacing / 2;
        const int branchWidth = std::max(2, spacing / 2 - 1);
        for (uint8_t i=0;i<state.laneCount;++i)
            drawLane(canvas,firstX+i*spacing,branchWidth,state.lanes[i],fg);
    }
    drawManeuverIcon(canvas,state.maneuver,state.roundaboutExit,fg);
    char distance[16]; formatDistance(state.maneuverDistanceM,distance,sizeof(distance));
    canvas.fontText(2,mainY(108),distance,assets::kTextSmall,fg,81,true);
    if (state.eta[0] != 0) {
        char eta[16];
        std::snprintf(eta, sizeof(eta), "ETA %s", state.eta.data());
        canvas.fontText(2,mainY(124),eta,assets::kTextSmall,colors::Muted,81,true);
    }
}

void HudRenderer::renderSpeed(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const uint16_t color = firmwareOverspeed(state, settings) ? colors::Red : foreground(settings);
    char speed[5]; std::snprintf(speed,sizeof(speed),"%d",std::clamp(state.speedKmh,0,999));
    canvas.fontText(2,mainY(26),speed,assets::kNumberLarge,color,canvas.width()-4,true);
    canvas.fontText(2,mainY(90),"km/h",assets::kTextSmall,colors::Muted,canvas.width()-4,true);
}

void HudRenderer::renderLimits(Canvas &canvas, const HudState &state, const DeviceSettings &) {
    canvas.clear(colors::Panel);
    if (state.speedLimitKmh > 0) {
        const assets::ColorBitmap *sign = speedLimitAsset(state.speedLimitKmh, SpeedSignContext::Current);
        if (sign && sign->pixels && sign->alpha) {
            canvas.colorBitmap(30 - sign->width / 2, mainY(48) - sign->height / 2, *sign);
        } else {
            canvas.fillCircle(30,mainY(48),28,colors::White);
            canvas.circle(30,mainY(48),28,colors::Red,6);
            char value[5]; std::snprintf(value,sizeof(value),"%d",state.speedLimitKmh);
            canvas.fontText(2,mainY(48)-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,56,true);
        }
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(30 - assets::kNoSpeedCurrent.width / 2,
                           mainY(48) - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }
    if (state.hasMinimumSpeed) {
        canvas.fillCircle(42,mainY(101),17,colors::Blue);
        char value[5]; std::snprintf(value,sizeof(value),"%d",state.minimumSpeedKmh);
        canvas.fontText(25,mainY(101)-assets::kNumberSmall.lineHeight/2,value,
                        assets::kNumberSmall,colors::White,34,true);
    }
}

void HudRenderer::renderAlerts(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const bool activeZone = state.noPassingZone;
    AlertState primary = state.nearestAlert;
    if (activeZone) {
        primary.kind = AlertKind::NoPassing;
        primary.distanceM = state.noPassingRemainingM;
        primary.valueKmh = 0;
    }
    if (primary.kind != AlertKind::None) {
        drawAlertIcon(canvas,47,mainY(34),22,primary,true);
        char distance[16]; formatDistance(primary.distanceM,distance,sizeof(distance));
        canvas.fontText(2,mainY(60),distance,assets::kTextSmall,
                        alertDistanceColor(primary.distanceM, foreground(settings)),91,true);
        if (primary.kind == AlertKind::TrafficJam) {
            char trafficDetail[48];
            if (primary.trafficDelayMinutes >= 0)
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s +%d PH",
                              trafficSeverityLabel(primary.trafficSeverity),
                              primary.trafficDelayMinutes);
            else
                std::snprintf(trafficDetail, sizeof(trafficDetail), "%.20s",
                              trafficSeverityLabel(primary.trafficSeverity));
            canvas.fontText(1,mainY(78),trafficDetail,assets::kTextSmall,
                            trafficSeverityColor(primary.trafficSeverity),93,true);
        }
    }

    if (activeZone) {
        // alr mirrors alrs[0] and is intentionally absent from the normalized
        // upcoming array. While an active zone owns the dominant slot, restore
        // that nearest-ahead alert as the single centered upcoming item.
        AlertState upcoming = state.nearestAlert;
        if (upcoming.kind == AlertKind::NoPassing) upcoming = {};
        for (uint8_t index = 0; upcoming.kind == AlertKind::None &&
                                index < state.upcomingAlertCount; ++index) {
            if (state.upcomingAlerts[index].kind != AlertKind::NoPassing)
                upcoming = state.upcomingAlerts[index];
        }
        if (upcoming.kind != AlertKind::None && !(upcoming == primary)) {
            drawAlertIcon(canvas,47,mainY(105),13,upcoming,false);
            char distance[12]; formatDistance(upcoming.distanceM,distance,sizeof(distance));
            canvas.fontText(24,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(upcoming.distanceM, colors::Muted),47,true);
        }
    } else {
        const uint8_t count = std::min<uint8_t>(2,state.upcomingAlertCount);
        for (uint8_t i=0;i<count;++i) {
            drawAlertIcon(canvas,20+i*48,mainY(105),13,state.upcomingAlerts[i],false);
            char distance[12]; formatDistance(state.upcomingAlerts[i].distanceM,distance,sizeof(distance));
            canvas.fontText(i*48,mainY(121),distance,assets::kTextSmall,
                            alertDistanceColor(state.upcomingAlerts[i].distanceM, colors::Muted),47,true);
        }
    }
}

void HudRenderer::renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const int64_t millis = localClockMillis(state);
    const int64_t second = millis == INT64_MIN ? INT64_MIN : millis / 1000LL;
    const bool haveClock = second != INT64_MIN;
    if (settings.showStreet) {
        const char *street = displayStreet(state);
        if (marqueeActive_)
            canvas.fontText(5-marqueeOffset_,0,street,assets::kTextMedium,
                            foreground(settings),-1,false);
        else
            canvas.fontText(5,0,street,assets::kTextMedium,foreground(settings),
                            haveClock ? 248 : 310,true);
    }
    if (haveClock) {
        // Clip marquee pixels before painting the independent clock column.
        canvas.fillRect(255,0,65,layout::Street.height,colors::Panel);
        const int64_t minute = second / 60;
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        const char separator = (millis % 1000LL) < 500LL ? ':' : ' ';
        char clock[8];
        std::snprintf(clock,sizeof(clock),"%02d%c%02d",
                      normalizedMinute / 60,separator,normalizedMinute % 60);
        canvas.fontText(260,0,clock,assets::kTextMedium,colors::Muted,55,true);
    }
}

}  // namespace waze_hud
