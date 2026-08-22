#pragma once

#include <cstddef>
#include <cstdint>

namespace waze_hud::assets {

struct AlphaMask {
    uint16_t width;
    uint16_t height;
    const uint8_t *alpha;
};

struct ColorBitmap {
    uint16_t width;
    uint16_t height;
    const uint16_t *pixels;
    const uint8_t *alpha;
};

struct SpeedLimitAssetSet {
    int value;
    const ColorBitmap *current;
    const ColorBitmap *alertLarge;
    const ColorBitmap *alertSmall;
};

struct AlertAssetSet {
    uint8_t code;
    const ColorBitmap *large;
    const ColorBitmap *small;
};

struct FontGlyph {
    uint32_t codepoint;
    uint32_t bitmapOffset;
    uint8_t width;
    uint8_t height;
    int8_t xOffset;
    int8_t yOffset;
    uint8_t advance;
};

struct BitmapFont {
    const FontGlyph *glyphs;
    std::size_t glyphCount;
    const uint8_t *bitmap4bpp;
    uint8_t lineHeight;
};

extern const AlphaMask kManeuverContinue;
extern const AlphaMask kManeuverLeft;
extern const AlphaMask kManeuverRight;
extern const AlphaMask kManeuverUTurn;
extern const AlphaMask kManeuverRoundabout;
extern const AlphaMask kManeuverRoundaboutLeft;
extern const AlphaMask kManeuverRoundaboutRight;
extern const AlphaMask kManeuverRoundaboutStraight;
extern const AlphaMask kManeuverExitLeft;
extern const AlphaMask kManeuverExitRight;
extern const AlphaMask kManeuverArrive;
extern const ColorBitmap kAlertPoliceLarge;
extern const ColorBitmap kAlertPoliceSmall;
extern const ColorBitmap kAlertSpeedCameraLarge;
extern const ColorBitmap kAlertSpeedCameraSmall;
extern const ColorBitmap kAlertRedLightCameraLarge;
extern const ColorBitmap kAlertRedLightCameraSmall;
extern const ColorBitmap kAlertHazardLarge;
extern const ColorBitmap kAlertHazardSmall;
extern const ColorBitmap kAlertAccidentLarge;
extern const ColorBitmap kAlertAccidentSmall;
extern const ColorBitmap kAlertTrafficJamLarge;
extern const ColorBitmap kAlertTrafficJamSmall;
extern const ColorBitmap kAlertRoadClosedLarge;
extern const ColorBitmap kAlertRoadClosedSmall;
extern const ColorBitmap kAlertNoPassingLarge;
extern const ColorBitmap kAlertNoPassingSmall;
extern const ColorBitmap kAlertEndNoPassingLarge;
extern const ColorBitmap kAlertEndNoPassingSmall;
extern const ColorBitmap kAlertRailwayLarge;
extern const ColorBitmap kAlertRailwaySmall;
extern const ColorBitmap kAlertTollBoothLarge;
extern const ColorBitmap kAlertTollBoothSmall;
extern const ColorBitmap kAlertStoppedVehicleLarge;
extern const ColorBitmap kAlertStoppedVehicleSmall;
extern const ColorBitmap kAlertConstructionLarge;
extern const ColorBitmap kAlertConstructionSmall;
extern const ColorBitmap kAlertPotholeLarge;
extern const ColorBitmap kAlertPotholeSmall;
extern const ColorBitmap kAlertWeatherLarge;
extern const ColorBitmap kAlertWeatherSmall;
extern const ColorBitmap kAlertBlockedLaneLarge;
extern const ColorBitmap kAlertBlockedLaneSmall;
extern const ColorBitmap kAlertDangerousRoadLarge;
extern const ColorBitmap kAlertDangerousRoadSmall;
extern const ColorBitmap kAlertExpresswayExitLarge;
extern const ColorBitmap kAlertExpresswayExitSmall;
extern const ColorBitmap kAlertRestStopLarge;
extern const ColorBitmap kAlertRestStopSmall;
extern const ColorBitmap kAlertEndProhibitionsLarge;
extern const ColorBitmap kAlertEndProhibitionsSmall;
extern const ColorBitmap kAlertResidentialStartLarge;
extern const ColorBitmap kAlertResidentialStartSmall;
extern const ColorBitmap kAlertResidentialEndLarge;
extern const ColorBitmap kAlertResidentialEndSmall;
extern const ColorBitmap kAlertNoCarLarge;
extern const ColorBitmap kAlertNoCarSmall;
extern const ColorBitmap kAlertNoMotorcycleLarge;
extern const ColorBitmap kAlertNoMotorcycleSmall;
extern const ColorBitmap kAlertNoLeftTurnLarge;
extern const ColorBitmap kAlertNoLeftTurnSmall;
extern const ColorBitmap kAlertNoRightTurnLarge;
extern const ColorBitmap kAlertNoRightTurnSmall;
extern const ColorBitmap kAlertNoUTurnLarge;
extern const ColorBitmap kAlertNoUTurnSmall;
extern const ColorBitmap kAlertMandatoryStraightLarge;
extern const ColorBitmap kAlertMandatoryStraightSmall;
extern const ColorBitmap kAlertMandatoryRightLarge;
extern const ColorBitmap kAlertMandatoryRightSmall;
extern const ColorBitmap kAlertMandatoryLeftLarge;
extern const ColorBitmap kAlertMandatoryLeftSmall;
extern const ColorBitmap kAlertPhoneCameraLarge;
extern const ColorBitmap kAlertPhoneCameraSmall;
extern const ColorBitmap kAlertDummyCameraLarge;
extern const ColorBitmap kAlertDummyCameraSmall;
extern const ColorBitmap kAlertSeatbeltCameraLarge;
extern const ColorBitmap kAlertSeatbeltCameraSmall;
extern const ColorBitmap kAlertDistanceCameraLarge;
extern const ColorBitmap kAlertDistanceCameraSmall;
extern const ColorBitmap kAlertBusLaneCameraLarge;
extern const ColorBitmap kAlertBusLaneCameraSmall;
extern const ColorBitmap kAlertNoiseCameraLarge;
extern const ColorBitmap kAlertNoiseCameraSmall;
extern const ColorBitmap kAlertStopSignCameraLarge;
extern const ColorBitmap kAlertStopSignCameraSmall;
extern const ColorBitmap kAlertAnimalLarge;
extern const ColorBitmap kAlertAnimalSmall;
extern const ColorBitmap kAlertObjectOnRoadLarge;
extern const ColorBitmap kAlertObjectOnRoadSmall;
extern const ColorBitmap kAlertRoadkillLarge;
extern const ColorBitmap kAlertRoadkillSmall;
extern const ColorBitmap kAlertFloodLarge;
extern const ColorBitmap kAlertFloodSmall;
extern const ColorBitmap kAlertFogLarge;
extern const ColorBitmap kAlertFogSmall;
extern const ColorBitmap kAlertHailLarge;
extern const ColorBitmap kAlertHailSmall;
extern const ColorBitmap kAlertSnowLarge;
extern const ColorBitmap kAlertSnowSmall;
extern const ColorBitmap kAlertIceLarge;
extern const ColorBitmap kAlertIceSmall;
extern const ColorBitmap kAlertSlipperyRoadLarge;
extern const ColorBitmap kAlertSlipperyRoadSmall;
extern const ColorBitmap kAlertSpeedBumpLarge;
extern const ColorBitmap kAlertSpeedBumpSmall;
extern const ColorBitmap kAlertSchoolZoneLarge;
extern const ColorBitmap kAlertSchoolZoneSmall;
extern const ColorBitmap kAlertLanesMergingLarge;
extern const ColorBitmap kAlertLanesMergingSmall;
extern const ColorBitmap kAlertDangerousCurveLarge;
extern const ColorBitmap kAlertDangerousCurveSmall;
extern const ColorBitmap kAlertForkLarge;
extern const ColorBitmap kAlertForkSmall;
extern const ColorBitmap kAlertBrokenLightLarge;
extern const ColorBitmap kAlertBrokenLightSmall;
extern const ColorBitmap kAlertCyclistLarge;
extern const ColorBitmap kAlertCyclistSmall;
extern const ColorBitmap kAlertEmergencyVehicleLarge;
extern const ColorBitmap kAlertEmergencyVehicleSmall;
extern const ColorBitmap kAlertPersonalSafetyLarge;
extern const ColorBitmap kAlertPersonalSafetySmall;
extern const ColorBitmap kAlertNoLeftAndUTurnLarge;
extern const ColorBitmap kAlertNoLeftAndUTurnSmall;
extern const ColorBitmap kAlertNoRightAndUTurnLarge;
extern const ColorBitmap kAlertNoRightAndUTurnSmall;
extern const ColorBitmap kBootIcon;
extern const ColorBitmap kNoSpeedCurrent;
extern const BitmapFont kTextSmall;
extern const BitmapFont kTextMedium;
extern const BitmapFont kTextLarge;
extern const BitmapFont kNumberSmall;
extern const BitmapFont kNumberMedium;
extern const BitmapFont kNumberLarge;

extern const SpeedLimitAssetSet kSpeedLimitAssets[];
extern const std::size_t kSpeedLimitAssetCount;
extern const AlertAssetSet kAlertAssets[];
extern const std::size_t kAlertAssetCount;

}  // namespace waze_hud::assets
