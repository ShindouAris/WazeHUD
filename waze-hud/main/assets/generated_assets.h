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

}  // namespace waze_hud::assets
