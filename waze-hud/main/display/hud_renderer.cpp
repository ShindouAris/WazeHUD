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

int64_t localClockMinute(const HudState &state) {
    if (state.clockUnixSeconds <= 0) return INT64_MIN;
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const uint64_t elapsedSeconds = nowMs >= state.clockSyncMonotonicMs
        ? (nowMs - state.clockSyncMonotonicMs) / 1000U : 0U;
    return (state.clockUnixSeconds + static_cast<int64_t>(elapsedSeconds) +
            static_cast<int64_t>(state.timezoneOffsetMinutes) * 60) / 60;
}

const char *displayStreet(const HudState &state) {
    if (state.currentStreet[0] != 0) return state.currentStreet.data();
    return state.navigationActive ? "Đang điều hướng" : "Đã sẵn sàng";
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
    canvas.line(x,29,x,16,recommendedLane ? fg : colors::Muted,recommendedLane ? 2 : 1);
    for (int bit = 0; bit < 8; ++bit) {
        const uint8_t flag = static_cast<uint8_t>(1U << bit);
        if ((lane.directionMask & flag) == 0) continue;
        const bool selected = (lane.selectedMask & flag) != 0;
        const uint16_t color = selected ? fg : colors::Muted;
        const int thickness = selected ? 2 : 1;
        int endX = x;
        int endY = 5;
        switch (bit) {
            case 1: endX = x - std::max(1, branchWidth / 2); endY = 6; break;
            case 2: endX = x - branchWidth; endY = 9; break;
            case 3: endX = x - branchWidth; endY = 13; break;
            case 4: endX = x + std::max(1, branchWidth / 2); endY = 6; break;
            case 5: endX = x + branchWidth; endY = 9; break;
            case 6: endX = x + branchWidth; endY = 13; break;
            case 7:
                endX = x - branchWidth;
                endY = 15;
                canvas.line(x,16,endX,10,color,thickness);
                canvas.line(endX,10,endX,15,color,thickness);
                laneArrowHead(canvas,endX,15,0,1,color,thickness);
                continue;
            default: break;
        }
        canvas.line(x,16,endX,endY,color,thickness);
        laneArrowHead(canvas,endX,endY,endX-x,endY-16,color,thickness);
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
        case Maneuver::KeepLeft: return &assets::kManeuverExitLeft;
        case Maneuver::KeepRight: return &assets::kManeuverExitRight;
        case Maneuver::ExitLeft: return &assets::kManeuverExitLeft;
        case Maneuver::ExitRight: return &assets::kManeuverExitRight;
        case Maneuver::Arrive: return &assets::kManeuverArrive;
        default: return nullptr;
    }
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
    constexpr int top = 38;
    constexpr int bottom = 92;
    const int thick = 5;
    if (maneuver == Maneuver::None) return;
    if (const assets::AlphaMask *asset = maneuverAsset(maneuver)) {
        canvas.alphaMask(12, 34, *asset, color);
        if ((maneuver == Maneuver::Roundabout || maneuver == Maneuver::RoundaboutLeft ||
             maneuver == Maneuver::RoundaboutRight || maneuver == Maneuver::RoundaboutStraight) &&
            exit > 0) {
            char number[12];
            std::snprintf(number, sizeof(number), "%d", exit);
            canvas.fontText(30, 58, number, assets::kNumberSmall, color, 24, true);
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
    if (maneuver == Maneuver::Roundabout || maneuver == Maneuver::RoundaboutLeft ||
        maneuver == Maneuver::RoundaboutRight || maneuver == Maneuver::RoundaboutStraight) {
        canvas.circle(cx, 65, 20, color, 4);
        canvas.line(cx, bottom, cx, 83, color, thick);
        if (maneuver == Maneuver::RoundaboutStraight) {
            canvas.line(cx, 45, cx, top, color, thick);
            arrowHead(canvas, cx, top, 0, -1, color, 3);
            if (exit > 0) {
                char number[12]; std::snprintf(number, sizeof(number), "%d", exit);
                canvas.text(cx - 9, 55, number, colors::Background, 2, 18, true);
            }
            return;
        }
        const bool left = maneuver == Maneuver::RoundaboutLeft;
        const int endX = left ? cx - 27 : cx + 27;
        canvas.line(left ? cx - 19 : cx + 19, 65, endX, 65, color, thick);
        arrowHead(canvas, endX, 65, left ? -1 : 1, 0, color, 3);
        if (exit > 0) {
            char number[12]; std::snprintf(number, sizeof(number), "%d", exit);
            canvas.text(cx - 9, 55, number, colors::Background, 2, 18, true);
        }
        return;
    }
    if (maneuver == Maneuver::UTurn || maneuver == Maneuver::UTurnRightReserved) {
        const bool right = maneuver == Maneuver::UTurnRightReserved;
        const int side = right ? 1 : -1;
        canvas.line(cx, bottom, cx, 55, color, thick);
        canvas.line(cx, 55, cx + side * 16, 43, color, thick);
        canvas.line(cx + side * 16, 43, cx + side * 27, 55, color, thick);
        canvas.line(cx + side * 27, 55, cx + side * 27, 68, color, thick);
        arrowHead(canvas, cx + side * 27, 68, 0, 1, color, 3);
        return;
    }

    int endX = cx, endY = top;
    switch (maneuver) {
        case Maneuver::Left: endX = 15; endY = 53; break;
        case Maneuver::Right: endX = 69; endY = 53; break;
        case Maneuver::SlightLeft: case Maneuver::KeepLeft: endX = 21; endY = 42; break;
        case Maneuver::SlightRight: case Maneuver::KeepRight: endX = 63; endY = 42; break;
        case Maneuver::SharpLeft: case Maneuver::ExitLeft: endX = 14; endY = 73; break;
        case Maneuver::SharpRight: case Maneuver::ExitRight: endX = 70; endY = 73; break;
        default: break;
    }
    canvas.line(cx, bottom, cx, 69, color, thick);
    canvas.line(cx, 69, endX, endY, color, thick);
    arrowHead(canvas, endX, endY, endX - cx, endY - 69, color, 3);
}

const assets::ColorBitmap *alertAsset(AlertKind kind, bool dominant) {
    const uint8_t code = static_cast<uint8_t>(kind);
    for (std::size_t index = 0; index < assets::kAlertAssetCount; ++index) {
        const assets::AlertAssetSet &entry = assets::kAlertAssets[index];
        if (entry.code == code) return dominant ? entry.large : entry.small;
    }
    return nullptr;
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

    const assets::ColorBitmap *bitmap = alertAsset(alert.kind, dominant);
    if (!bitmap) bitmap = alertAsset(AlertKind::Hazard, dominant);
    if (bitmap && bitmap->pixels && bitmap->alpha) {
        canvas.colorBitmap(cx - bitmap->width / 2, cy - bitmap->height / 2, *bitmap);
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

void HudRenderer::render(const HudState &state, const DeviceSettings &settings) {
    const int64_t currentClockMinute = localClockMinute(state);
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
    bool streetRendered = false;
    if (statusChanged || configChanged || !state.connected || !state.hasProducerState) {
        renderRegion(layout::Maneuver,state,settings); renderRegion(layout::Speed,state,settings);
        renderRegion(layout::Limits,state,settings); renderRegion(layout::Alerts,state,settings);
        renderRegion(layout::Street,state,settings);
        streetRendered = true;
    } else {
        if (maneuverChanged(state, previous_)) renderRegion(layout::Maneuver,state,settings);
        if (state.speedKmh != previous_.speedKmh ||
            state.speedLimitKmh != previous_.speedLimitKmh)
            renderRegion(layout::Speed,state,settings);
        if (state.speedLimitKmh != previous_.speedLimitKmh || state.hasMinimumSpeed != previous_.hasMinimumSpeed ||
            state.minimumSpeedKmh != previous_.minimumSpeedKmh) renderRegion(layout::Limits,state,settings);
        if (alertsChanged(state, previous_)) renderRegion(layout::Alerts,state,settings);
        if (streetChanged ||
            settings.showStreet != previousSettings_.showStreet ||
            currentClockMinute != renderedClockMinute_ || marqueeFrameChanged) {
            renderRegion(layout::Street,state,settings);
            streetRendered = true;
        }
    }
    previous_ = state;
    previousSettings_ = settings;
    renderedClockMinute_ = currentClockMinute;
    if (streetRendered) marqueeRenderedOffset_ = marqueeOffset_;
    firstFrame_ = false;
}

void HudRenderer::renderRegion(const Rect &region, const HudState &state, const DeviceSettings &settings) {
    Canvas canvas(buffer_, region.width, region.height);
    canvas.setTranslation(settings.offsetX, settings.offsetY);
    if (!state.connected || !state.hasProducerState) renderStatus(canvas, region, state, settings);
    else if (sameRegion(region, layout::Maneuver)) renderManeuver(canvas,state,settings);
    else if (sameRegion(region, layout::Speed)) renderSpeed(canvas,state,settings);
    else if (sameRegion(region, layout::Limits)) renderLimits(canvas,state,settings);
    else if (sameRegion(region, layout::Alerts)) renderAlerts(canvas,state,settings);
    else renderStreet(canvas,state,settings);
    const esp_err_t result = DisplayDriver::instance().drawRegion(region, buffer_);
    if (result != ESP_OK) ESP_LOGE(kTag, "Dirty region (%d,%d %dx%d) failed: %s",
                                   region.x,region.y,region.width,region.height,esp_err_to_name(result));
}

void HudRenderer::renderStatus(Canvas &canvas, const Rect &region, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    canvas.colorBitmap(16 - region.x, 25 - region.y, assets::kBootIcon);
    constexpr int copyX = 120;
    constexpr int copyWidth = 190;
    canvas.fontText(copyX - region.x, 34 - region.y, "WazeHUD", assets::kTextLarge,
                    colors::Foreground, copyWidth, true);
    const char *status = state.signalStale ? "Mất tín hiệu" : state.connected ? "Đã kết nối" : "Đang chờ thiết bị";
    const uint16_t statusColor = state.signalStale ? colors::Amber : state.connected ? colors::Green : colors::Muted;
    canvas.fontText(copyX - region.x, 73 - region.y, status, assets::kTextMedium,
                    statusColor, copyWidth, true);
    const char *detail = state.signalStale ? "Đang đợi dữ liệu" : state.connected ? "Đang chờ WazeMod" : "Đang chờ kết nối";
    canvas.fontText(copyX - region.x, 103 - region.y, detail, assets::kTextSmall,
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
    canvas.fontText(2,108,distance,assets::kTextSmall,fg,81,true);
    if (state.eta[0] != 0) {
        char eta[16];
        std::snprintf(eta, sizeof(eta), "ETA %s", state.eta.data());
        canvas.fontText(2,124,eta,assets::kTextSmall,colors::Muted,81,true);
    }
}

void HudRenderer::renderSpeed(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Background);
    const uint16_t color = firmwareOverspeed(state, settings) ? colors::Red : foreground(settings);
    char speed[5]; std::snprintf(speed,sizeof(speed),"%d",std::clamp(state.speedKmh,0,999));
    canvas.fontText(2,26,speed,assets::kNumberLarge,color,canvas.width()-4,true);
    canvas.fontText(2,90,"km/h",assets::kTextSmall,colors::Muted,canvas.width()-4,true);
}

void HudRenderer::renderLimits(Canvas &canvas, const HudState &state, const DeviceSettings &) {
    canvas.clear(colors::Panel);
    if (state.speedLimitKmh > 0) {
        const assets::ColorBitmap *sign = speedLimitAsset(state.speedLimitKmh, SpeedSignContext::Current);
        if (sign && sign->pixels && sign->alpha) {
            canvas.colorBitmap(30 - sign->width / 2, 48 - sign->height / 2, *sign);
        } else {
            canvas.fillCircle(30,48,28,colors::White); canvas.circle(30,48,28,colors::Red,6);
            char value[5]; std::snprintf(value,sizeof(value),"%d",state.speedLimitKmh);
            canvas.fontText(2,48-assets::kNumberMedium.lineHeight/2,value,
                            assets::kNumberMedium,colors::Black,56,true);
        }
    } else if (assets::kNoSpeedCurrent.pixels && assets::kNoSpeedCurrent.alpha) {
        canvas.colorBitmap(30 - assets::kNoSpeedCurrent.width / 2,
                           48 - assets::kNoSpeedCurrent.height / 2,
                           assets::kNoSpeedCurrent);
    }
    if (state.hasMinimumSpeed) {
        canvas.fillCircle(42,101,17,colors::Blue);
        char value[5]; std::snprintf(value,sizeof(value),"%d",state.minimumSpeedKmh);
        canvas.fontText(25,101-assets::kNumberSmall.lineHeight/2,value,
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
        drawAlertIcon(canvas,47,34,22,primary,true);
        char distance[16]; formatDistance(primary.distanceM,distance,sizeof(distance));
        canvas.fontText(2,60,distance,assets::kTextSmall,
                        alertDistanceColor(primary.distanceM, foreground(settings)),91,true);
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
            drawAlertIcon(canvas,47,105,13,upcoming,false);
            char distance[12]; formatDistance(upcoming.distanceM,distance,sizeof(distance));
            canvas.fontText(24,121,distance,assets::kTextSmall,
                            alertDistanceColor(upcoming.distanceM, colors::Muted),47,true);
        }
    } else {
        const uint8_t count = std::min<uint8_t>(2,state.upcomingAlertCount);
        for (uint8_t i=0;i<count;++i) {
            drawAlertIcon(canvas,20+i*48,105,13,state.upcomingAlerts[i],false);
            char distance[12]; formatDistance(state.upcomingAlerts[i].distanceM,distance,sizeof(distance));
            canvas.fontText(i*48,121,distance,assets::kTextSmall,
                            alertDistanceColor(state.upcomingAlerts[i].distanceM, colors::Muted),47,true);
        }
    }
}

void HudRenderer::renderStreet(Canvas &canvas, const HudState &state, const DeviceSettings &settings) {
    canvas.clear(colors::Panel);
    const int64_t minute = localClockMinute(state);
    const bool haveClock = minute != INT64_MIN;
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
        const int normalizedMinute = static_cast<int>((minute % 1440 + 1440) % 1440);
        char clock[8];
        std::snprintf(clock,sizeof(clock),"%02d:%02d",normalizedMinute / 60,normalizedMinute % 60);
        canvas.fontText(260,0,clock,assets::kTextMedium,colors::Muted,55,true);
    }
}

}  // namespace waze_hud
