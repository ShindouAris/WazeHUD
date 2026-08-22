#include "protocol/hlp_decoder.h"

#include "protocol/hlp_core.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace waze_hud {
namespace {
constexpr char kTag[] = "HLP";

int integerOr(const cJSON *root, const char *key, int fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble) return fallback;
    return static_cast<int>(item->valuedouble);
}

float numberOr(const cJSON *root, const char *key, float fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble)
        ? static_cast<float>(item->valuedouble) : fallback;
}

uint32_t unsigned32Or(const cJSON *root, const char *key, uint32_t fallback) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) ||
        std::floor(item->valuedouble) != item->valuedouble || item->valuedouble < 0.0 ||
        item->valuedouble > 4294967295.0) return fallback;
    return static_cast<uint32_t>(item->valuedouble);
}

template <std::size_t Capacity>
void copyUtf8(const cJSON *root, const char *key, std::array<char, Capacity> &destination) {
    destination.fill(0);
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return;
    std::size_t count = std::min<std::size_t>(std::strlen(item->valuestring), Capacity - 1);
    while (count > 0 && !hlp_utf8_valid(reinterpret_cast<const uint8_t *>(item->valuestring), count)) --count;
    std::memcpy(destination.data(), item->valuestring, count);
}

Maneuver maneuverValue(int value) {
    return value >= 0 && value <= 19 ? static_cast<Maneuver>(value) : Maneuver::None;
}

AlertKind alertValue(int value) {
    if (value == 0) return AlertKind::None;
    if (value > 0 && value <= 74) return static_cast<AlertKind>(value);
    return value > 0 ? AlertKind::Hazard : AlertKind::None;
}

bool speedDropTieComesBefore(const AlertState &left, const AlertState &right) {
    return left.kind == AlertKind::SpeedDrop && right.kind == AlertKind::SpeedDrop &&
           left.distanceM == right.distanceM && left.valueKmh > right.valueKmh;
}
}  // namespace

void HlpDecoder::resetSession() {
    session_ = 0;
    lastTimestamp_ = 0;
    haveTimestamp_ = false;
}

bool HlpDecoder::handleHi(const cJSON *root, HudState &state) {
    const uint32_t session = unsigned32Or(root, "sess", 0);
    const cJSON *protocolItem = cJSON_GetObjectItemCaseSensitive(root, "proto");
    const int protocol = protocolItem ? integerOr(root, "proto", -1) : 1;
    if (protocol != 1) {
        ESP_LOGW(kTag, "Producer selected unsupported HLP/%d", protocol);
        return false;
    }
    if (session != session_) {
        const bool connected = state.connected;
        state = {};
        state.connected = connected;
        session_ = session;
        state.sessionId = session_;
        haveTimestamp_ = false;
        ESP_LOGI(kTag, "HLP session %lu established", static_cast<unsigned long>(session_));
        return true;
    }
    state.sessionId = session_;
    return false;
}

bool HlpDecoder::decodeState(const cJSON *root, HudState &state) {
    const uint32_t timestamp = unsigned32Or(root, "ts", 0);
    if (haveTimestamp_ && static_cast<int32_t>(timestamp - lastTimestamp_) <= 0) {
        ESP_LOGD(kTag, "Discarding stale state ts=%lu last=%lu",
                 static_cast<unsigned long>(timestamp), static_cast<unsigned long>(lastTimestamp_));
        return false;
    }

    HudState decoded{};
    decoded.connected = true;
    decoded.hasProducerState = true;
    decoded.sessionId = session_;
    decoded.navigationActive = integerOr(root, "nav", 0) != 0;
    decoded.speedKmh = std::clamp(integerOr(root, "spd", 0), 0, 999);
    decoded.speedLimitKmh = std::max(0, integerOr(root, "lim", 0));
    decoded.overSpeed = integerOr(root, "over", 0) != 0;
    const int maneuverCode = integerOr(root, "trn", 0);
    const int secondManeuverCode = integerOr(root, "trn2", 0);
    decoded.maneuver = maneuverValue(maneuverCode);
    decoded.secondManeuver = maneuverValue(secondManeuverCode);
    decoded.maneuverDistanceM = integerOr(root, "dst", -1);
    decoded.roundaboutExit = std::max(0, integerOr(root, "exit", 0));
    copyUtf8(root, "st", decoded.currentStreet);
    copyUtf8(root, "st2", decoded.nextStreet);
    copyUtf8(root, "eta", decoded.eta);
    decoded.remainingMinutes = std::max(0, integerOr(root, "rmin", 0));
    const int exactRemainingMeters = integerOr(root, "rm", -1);
    const float legacyRemainingKm = std::max(0.0F, numberOr(root, "rkm", 0.0F));
    decoded.remainingMeters = exactRemainingMeters >= 0
        ? exactRemainingMeters : static_cast<int>(std::lround(legacyRemainingKm * 1000.0F));
    decoded.remainingKm = exactRemainingMeters >= 0
        ? static_cast<float>(exactRemainingMeters) / 1000.0F : legacyRemainingKm;
    decoded.noPassingZone = integerOr(root, "avg", 0) != 0;
    decoded.noPassingRemainingM = std::max(0, integerOr(root, "avgL", 0));
    decoded.noPassingRecommendedKmh = std::max(0, integerOr(root, "avgR", 0));
    decoded.noPassingProgressPct = std::clamp(integerOr(root, "avgP", 0), 0, 100);
    decoded.nearestAlert.kind = alertValue(integerOr(root, "alr", 0));
    decoded.nearestAlert.distanceM = integerOr(root, "alrD", -1);
    decoded.nearestAlert.valueKmh = std::max(0, integerOr(root, "alrV", 0));

    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alrs");
    if (cJSON_IsArray(alerts)) {
        std::array<AlertState, kMaxAlerts> parsedAlerts{};
        uint8_t parsedAlertCount = 0;
        const cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, alerts) {
            if (parsedAlertCount >= kMaxAlerts || !cJSON_IsObject(entry)) break;
            AlertState alert;
            alert.kind = alertValue(integerOr(entry, "k", 0));
            alert.distanceM = integerOr(entry, "d", -1);
            alert.valueKmh = std::max(0, integerOr(entry, "v", 0));
            if (alert.kind == AlertKind::None) continue;
            bool duplicate = false;
            for (uint8_t index = 0; index < parsedAlertCount && !duplicate; ++index)
                duplicate = alert == parsedAlerts[index];
            if (!duplicate) parsedAlerts[parsedAlertCount++] = alert;
        }

        // The producer already orders alrs near-to-far. Preserve that order,
        // but for speed-limit drops at the exact same position prefer the
        // higher limit first. Insertion sorting only this tie condition is
        // stable, bounded, and allocation-free.
        for (uint8_t index = 1; index < parsedAlertCount; ++index) {
            const AlertState candidate = parsedAlerts[index];
            uint8_t position = index;
            while (position > 0 && speedDropTieComesBefore(candidate, parsedAlerts[position - 1])) {
                parsedAlerts[position] = parsedAlerts[position - 1];
                --position;
            }
            parsedAlerts[position] = candidate;
        }

        if (parsedAlertCount > 0) {
            // alr/alrD/alrV is only the compatibility mirror of alrs[0]. Once
            // the full opt-in list is present, derive both the dominant and
            // upcoming normalized state from that single canonical ordering.
            decoded.nearestAlert = parsedAlerts[0];
            for (uint8_t index = 1; index < parsedAlertCount; ++index)
                decoded.upcomingAlerts[decoded.upcomingAlertCount++] = parsedAlerts[index];
        }
    }

    // No HLP/1 fields exist for these capabilities. Real protocol state always
    // clears them; only the compile-time mock source may populate them.
    decoded.hasMinimumSpeed = false;
    decoded.laneCount = 0;
    decoded.producerTimestamp = timestamp;
    state = decoded;
    lastTimestamp_ = timestamp;
    haveTimestamp_ = true;
    return true;
}

}  // namespace waze_hud
