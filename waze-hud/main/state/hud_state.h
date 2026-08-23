#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace waze_hud {

constexpr std::size_t kMaxStreetUtf8Bytes = 160;
constexpr std::size_t kMaxAlerts = 4;
constexpr std::size_t kMaxLanes = 12;

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
    RoundaboutStraight = 19,
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
    EndNoPassing = 10,
    Railway = 11,
    TollBooth = 12,
    StoppedVehicle = 13,
    Construction = 14,
    Pothole = 15,
    Weather = 16,
    BlockedLane = 17,
    DangerousRoad = 18,
    ExpresswayExit = 19,
    ExpresswayRestStop = 20,
    RestStop = 21,
    EndSpeedRestriction = 22,
    ResidentialStart = 23,
    ResidentialEnd = 24,
    EndAllProhibitions = 25,
    NoCar = 26,
    NoMotorcycle = 27,
    NoLeftTurn = 28,
    NoRightTurn = 29,
    NoUTurn = 30,
    NoStraight = 31,
    MandatoryStraight = 32,
    MandatoryRight = 33,
    MandatoryLeft = 34,
    CarLane = 35,
    MotorcycleLane = 36,
    OneWay = 37,
    ProhibitedRoad = 38,
    CombinedTurnRestriction = 39,
    PhoneCamera = 40,
    DummyCamera = 41,
    SeatbeltCamera = 42,
    DistanceCamera = 43,
    BusLaneCamera = 44,
    NoiseCamera = 45,
    StopSignCamera = 46,
    Animal = 47,
    ObjectOnRoad = 48,
    Roadkill = 49,
    Flood = 50,
    Fog = 51,
    Hail = 52,
    Snow = 53,
    Ice = 54,
    SlipperyRoad = 55,
    SpeedBump = 56,
    SchoolZone = 57,
    LanesMerging = 58,
    DangerousCurve = 59,
    Fork = 60,
    BrokenLight = 61,
    Cyclist = 62,
    EmergencyVehicle = 63,
    PersonalSafety = 64,
    NoStraightAndRight = 65,
    NoLeftAndUTurn = 66,
    NoStraightAndLeft = 67,
    NoLeftAndRight = 68,
    CarNoLeftAndUTurn = 69,
    CarNoRightAndUTurn = 70,
    NoRightAndUTurn = 71,
    CarNoLeftTurn = 72,
    CarNoRightTurn = 73,
    CarNoUTurn = 74,
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
    uint8_t directionMask{0};
    uint8_t selectedMask{0};

    bool operator==(const LaneState &other) const {
        return directionMask == other.directionMask && selectedMask == other.selectedMask;
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
    int remainingMeters{0};
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
    int64_t clockUnixSeconds{0};
    int timezoneOffsetMinutes{0};
    uint64_t clockSyncMonotonicMs{0};
    uint32_t producerTimestamp{0};
    uint32_t localGeneration{0};
};

}  // namespace waze_hud
