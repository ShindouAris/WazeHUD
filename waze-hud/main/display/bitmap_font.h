#pragma once

#include "assets/generated_assets.h"
#include <cstddef>
#include <cstdint>

namespace waze_hud {

class Canvas {
public:
    Canvas(uint16_t *pixels, int width, int height);

    void clear(uint16_t color);
    void setTranslation(int x, int y) { translationX_ = x; translationY_ = y; }
    void pixel(int x, int y, uint16_t color);
    void fillRect(int x, int y, int width, int height, uint16_t color);
    void line(int x0, int y0, int x1, int y1, uint16_t color, int thickness = 1);
    void circle(int centerX, int centerY, int radius, uint16_t color, int thickness = 1);
    void fillCircle(int centerX, int centerY, int radius, uint16_t color);
    void triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
    void alphaMask(int x, int y, const assets::AlphaMask &mask, uint16_t color);
    void colorBitmap(int x, int y, const assets::ColorBitmap &bitmap);

    int textWidth(const char *utf8, int scale, int maxCells = -1) const;
    void text(int x, int y, const char *utf8, uint16_t color, int scale = 1,
              int maxWidth = -1, bool centered = false);
    int fontTextWidth(const char *utf8, const assets::BitmapFont &font) const;
    void fontText(int x, int y, const char *utf8, const assets::BitmapFont &font,
                  uint16_t color, int maxWidth = -1, bool centered = false);

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void alphaPixel(int x, int y, uint16_t color, uint8_t alpha);

    uint16_t *pixels_;
    int width_;
    int height_;
    int translationX_{0};
    int translationY_{0};
};

}  // namespace waze_hud
