#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace waze_hud {

constexpr std::size_t kMaxStreetUtf8Bytes = 160;
constexpr std::size_t kMaxAlerts = 4;
constexpr std::size_t kMaxLanes = 6;

enum class Maneuver : uint8_t {
    None = 0,
    Continue = 1,
    Left = 2,
    Right = 3,
    SlightLeft = 4,
    SlightRight = 5,
    SharpLeft = 6,
    SharpRight = 7,
    UTurn = 8,
    UTurnRightReserved = 9,
    Roundabout = 10,
    RoundaboutLeft = 11,
    RoundaboutRight = 12,
    KeepLeft = 13,
    KeepRight = 14,
    ExitLeft = 15,
    ExitRight = 16,
    Arrive = 17,
    FerryReserved = 18,
};

enum class AlertKind : uint8_t {
    None = 0,
    Police = 1,
    SpeedCamera = 2,
    RedLightCamera = 3,
    Hazard = 4,
    Accident = 5,
    TrafficJam = 6,
    RoadClosed = 7,
    SpeedDrop = 8,
    NoPassing = 9,
};

enum class LaneDirection : uint8_t {
    Straight,
    SlightLeft,
    Left,
    SharpLeft,
    SlightRight,
    Right,
    SharpRight,
    UTurn,
};

struct AlertState {
    AlertKind kind{AlertKind::None};
    int distanceM{-1};
    int valueKmh{0};

    bool operator==(const AlertState &other) const {
        return kind == other.kind && distanceM == other.distanceM && valueKmh == other.valueKmh;
    }
};

struct LaneState {
    LaneDirection direction{LaneDirection::Straight};
    bool recommended{false};

    bool operator==(const LaneState &other) const {
        return direction == other.direction && recommended == other.recommended;
    }
};

// Fixed-capacity snapshot copied across a length-one FreeRTOS queue. Optional
// mock/future fields are represented by explicit presence flags and are never
// populated by the HLP/1 decoder without a normative wire field.
struct HudState {
    bool connected{false};
    bool signalStale{false};
    bool hasProducerState{false};
    bool navigationActive{false};

    int speedKmh{0};
    int speedLimitKmh{0};
    bool overSpeed{false};

    Maneuver maneuver{Maneuver::None};
    Maneuver secondManeuver{Maneuver::None};
    int maneuverDistanceM{-1};
    int roundaboutExit{0};

    std::array<char, kMaxStreetUtf8Bytes + 1> currentStreet{};
    std::array<char, kMaxStreetUtf8Bytes + 1> nextStreet{};
    std::array<char, 8> eta{};
    int remainingMinutes{0};
    float remainingKm{0.0F};

    bool noPassingZone{false};
    int noPassingRemainingM{0};
    int noPassingRecommendedKmh{0};
    int noPassingProgressPct{0};

    AlertState nearestAlert{};
    std::array<AlertState, kMaxAlerts> upcomingAlerts{};
    uint8_t upcomingAlertCount{0};

    bool hasMinimumSpeed{false};
    int minimumSpeedKmh{0};
    std::array<LaneState, kMaxLanes> lanes{};
    uint8_t laneCount{0};

    uint32_t sessionId{0};
    uint32_t producerTimestamp{0};
    uint32_t localGeneration{0};
};

}  // namespace waze_hud
