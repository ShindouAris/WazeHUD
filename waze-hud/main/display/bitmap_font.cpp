#include "display/bitmap_font.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace waze_hud {
namespace {

struct GlyphPattern { char character; std::array<uint8_t, 5> columns; };

// Compact, deterministic 5x7 font. Application text deliberately uses a
// restrained all-caps automotive style at this small physical size.
constexpr GlyphPattern kPatterns[] = {
    {' ',{0,0,0,0,0}}, {'!',{0,0,0x5F,0,0}}, {'-',{0x08,0x08,0x08,0x08,0x08}},
    {'.',{0,0x60,0x60,0,0}}, {'/',{0x20,0x10,0x08,0x04,0x02}}, {':',{0,0x36,0x36,0,0}},
    {'0',{0x3E,0x51,0x49,0x45,0x3E}}, {'1',{0,0x42,0x7F,0x40,0}},
    {'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x21,0x41,0x45,0x4B,0x31}},
    {'4',{0x18,0x14,0x12,0x7F,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3C,0x4A,0x49,0x49,0x30}}, {'7',{0x01,0x71,0x09,0x05,0x03}},
    {'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{0x06,0x49,0x49,0x29,0x1E}},
    {'A',{0x7E,0x11,0x11,0x11,0x7E}}, {'B',{0x7F,0x49,0x49,0x49,0x36}},
    {'C',{0x3E,0x41,0x41,0x41,0x22}}, {'D',{0x7F,0x41,0x41,0x22,0x1C}},
    {'E',{0x7F,0x49,0x49,0x49,0x41}}, {'F',{0x7F,0x09,0x09,0x09,0x01}},
    {'G',{0x3E,0x41,0x49,0x49,0x7A}}, {'H',{0x7F,0x08,0x08,0x08,0x7F}},
    {'I',{0,0x41,0x7F,0x41,0}}, {'J',{0x20,0x40,0x41,0x3F,0x01}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}}, {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}}, {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}}, {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'Q',{0x3E,0x41,0x51,0x21,0x5E}}, {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{0x01,0x01,0x7F,0x01,0x01}},
    {'U',{0x3F,0x40,0x40,0x40,0x3F}}, {'V',{0x1F,0x20,0x40,0x20,0x1F}},
    {'W',{0x3F,0x40,0x38,0x40,0x3F}}, {'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
    {'?',{0x02,0x01,0x51,0x09,0x06}},
};

enum class Modifier : uint8_t { None, Circumflex, Breve, Horn, Stroke };
enum class Tone : uint8_t { None, Acute, Grave, Hook, Tilde, Dot };
struct DecodedGlyph { char base{'?'}; Modifier modifier{Modifier::None}; Tone tone{Tone::None}; };

const std::array<uint8_t, 5> &patternFor(char input) {
    const char c = input >= 'a' && input <= 'z' ? static_cast<char>(input - 32) : input;
    for (const auto &entry : kPatterns) if (entry.character == c) return entry.columns;
    for (const auto &entry : kPatterns) if (entry.character == '?') return entry.columns;
    return kPatterns[0].columns;
}

uint32_t nextCodepoint(const char *&text) {
    const auto first = static_cast<uint8_t>(*text++);
    if (first < 0x80) return first;
    if ((first & 0xE0) == 0xC0) {
        const uint8_t b = static_cast<uint8_t>(*text++);
        return ((first & 0x1F) << 6) | (b & 0x3F);
    }
    if ((first & 0xF0) == 0xE0) {
        const uint8_t b = static_cast<uint8_t>(*text++);
        const uint8_t c = static_cast<uint8_t>(*text++);
        return ((first & 0x0F) << 12) | ((b & 0x3F) << 6) | (c & 0x3F);
    }
    while ((*text & 0xC0) == 0x80) ++text;
    return '?';
}

DecodedGlyph decodeVietnamese(uint32_t cp) {
    if (cp < 128) return {static_cast<char>(cp), Modifier::None, Tone::None};
    switch (cp) {
        case 0x0110: case 0x0111: return {'D', Modifier::Stroke, Tone::None};
        case 0x00C0: case 0x00E0: return {'A', Modifier::None, Tone::Grave};
        case 0x00C1: case 0x00E1: return {'A', Modifier::None, Tone::Acute};
        case 0x00C2: case 0x00E2: return {'A', Modifier::Circumflex, Tone::None};
        case 0x00C3: case 0x00E3: return {'A', Modifier::None, Tone::Tilde};
        case 0x0102: case 0x0103: return {'A', Modifier::Breve, Tone::None};
        case 0x00C8: case 0x00E8: return {'E', Modifier::None, Tone::Grave};
        case 0x00C9: case 0x00E9: return {'E', Modifier::None, Tone::Acute};
        case 0x00CA: case 0x00EA: return {'E', Modifier::Circumflex, Tone::None};
        case 0x00CC: case 0x00EC: return {'I', Modifier::None, Tone::Grave};
        case 0x00CD: case 0x00ED: return {'I', Modifier::None, Tone::Acute};
        case 0x0128: case 0x0129: return {'I', Modifier::None, Tone::Tilde};
        case 0x00D2: case 0x00F2: return {'O', Modifier::None, Tone::Grave};
        case 0x00D3: case 0x00F3: return {'O', Modifier::None, Tone::Acute};
        case 0x00D4: case 0x00F4: return {'O', Modifier::Circumflex, Tone::None};
        case 0x00D5: case 0x00F5: return {'O', Modifier::None, Tone::Tilde};
        case 0x01A0: case 0x01A1: return {'O', Modifier::Horn, Tone::None};
        case 0x00D9: case 0x00F9: return {'U', Modifier::None, Tone::Grave};
        case 0x00DA: case 0x00FA: return {'U', Modifier::None, Tone::Acute};
        case 0x0168: case 0x0169: return {'U', Modifier::None, Tone::Tilde};
        case 0x01AF: case 0x01B0: return {'U', Modifier::Horn, Tone::None};
        case 0x00DD: case 0x00FD: return {'Y', Modifier::None, Tone::Acute};
        default: break;
    }

    // Vietnamese additions in the Latin Extended Additional block are
    // uppercase/lowercase pairs; masking bit zero normalizes each pair.
    const uint32_t pair = cp & ~1U;
    switch (pair) {
        case 0x1EA0: return {'A',Modifier::None,Tone::Dot};
        case 0x1EA2: return {'A',Modifier::None,Tone::Hook};
        case 0x1EA4: return {'A',Modifier::Circumflex,Tone::Acute};
        case 0x1EA6: return {'A',Modifier::Circumflex,Tone::Grave};
        case 0x1EA8: return {'A',Modifier::Circumflex,Tone::Hook};
        case 0x1EAA: return {'A',Modifier::Circumflex,Tone::Tilde};
        case 0x1EAC: return {'A',Modifier::Circumflex,Tone::Dot};
        case 0x1EAE: return {'A',Modifier::Breve,Tone::Acute};
        case 0x1EB0: return {'A',Modifier::Breve,Tone::Grave};
        case 0x1EB2: return {'A',Modifier::Breve,Tone::Hook};
        case 0x1EB4: return {'A',Modifier::Breve,Tone::Tilde};
        case 0x1EB6: return {'A',Modifier::Breve,Tone::Dot};
        case 0x1EB8: return {'E',Modifier::None,Tone::Dot};
        case 0x1EBA: return {'E',Modifier::None,Tone::Hook};
        case 0x1EBC: return {'E',Modifier::None,Tone::Tilde};
        case 0x1EBE: return {'E',Modifier::Circumflex,Tone::Acute};
        case 0x1EC0: return {'E',Modifier::Circumflex,Tone::Grave};
        case 0x1EC2: return {'E',Modifier::Circumflex,Tone::Hook};
        case 0x1EC4: return {'E',Modifier::Circumflex,Tone::Tilde};
        case 0x1EC6: return {'E',Modifier::Circumflex,Tone::Dot};
        case 0x1EC8: return {'I',Modifier::None,Tone::Hook};
        case 0x1ECA: return {'I',Modifier::None,Tone::Dot};
        case 0x1ECC: return {'O',Modifier::None,Tone::Dot};
        case 0x1ECE: return {'O',Modifier::None,Tone::Hook};
        case 0x1ED0: return {'O',Modifier::Circumflex,Tone::Acute};
        case 0x1ED2: return {'O',Modifier::Circumflex,Tone::Grave};
        case 0x1ED4: return {'O',Modifier::Circumflex,Tone::Hook};
        case 0x1ED6: return {'O',Modifier::Circumflex,Tone::Tilde};
        case 0x1ED8: return {'O',Modifier::Circumflex,Tone::Dot};
        case 0x1EDA: return {'O',Modifier::Horn,Tone::Acute};
        case 0x1EDC: return {'O',Modifier::Horn,Tone::Grave};
        case 0x1EDE: return {'O',Modifier::Horn,Tone::Hook};
        case 0x1EE0: return {'O',Modifier::Horn,Tone::Tilde};
        case 0x1EE2: return {'O',Modifier::Horn,Tone::Dot};
        case 0x1EE4: return {'U',Modifier::None,Tone::Dot};
        case 0x1EE6: return {'U',Modifier::None,Tone::Hook};
        case 0x1EE8: return {'U',Modifier::Horn,Tone::Acute};
        case 0x1EEA: return {'U',Modifier::Horn,Tone::Grave};
        case 0x1EEC: return {'U',Modifier::Horn,Tone::Hook};
        case 0x1EEE: return {'U',Modifier::Horn,Tone::Tilde};
        case 0x1EF0: return {'U',Modifier::Horn,Tone::Dot};
        case 0x1EF2: return {'Y',Modifier::None,Tone::Grave};
        case 0x1EF4: return {'Y',Modifier::None,Tone::Dot};
        case 0x1EF6: return {'Y',Modifier::None,Tone::Hook};
        case 0x1EF8: return {'Y',Modifier::None,Tone::Tilde};
        default: return {'?',Modifier::None,Tone::None};
    }
}

int countCodepoints(const char *text) {
    int count = 0;
    while (text && *text) { nextCodepoint(text); ++count; }
    return count;
}

void drawGlyph(Canvas &canvas, int x, int y, DecodedGlyph glyph, uint16_t color, int scale) {
    const auto &columns = patternFor(glyph.base);
    // Keep a separate row for a base modifier and for the tone mark so
    // combined Vietnamese diacritics remain distinguishable.
    const int baseY = y + 3 * scale;
    for (int col = 0; col < 5; ++col) {
        for (int row = 0; row < 7; ++row) {
            if ((columns[col] & (1U << row)) != 0)
                canvas.fillRect(x + col * scale, baseY + row * scale, scale, scale, color);
        }
    }
    if (glyph.modifier == Modifier::Stroke)
        canvas.line(x, baseY + 3 * scale, x + 4 * scale, baseY + 3 * scale, color, scale);
    else if (glyph.modifier == Modifier::Circumflex) {
        canvas.line(x + scale, y + scale, x + 2 * scale, y, color, scale);
        canvas.line(x + 2 * scale, y, x + 3 * scale, y + scale, color, scale);
    } else if (glyph.modifier == Modifier::Breve) {
        canvas.pixel(x + scale, y, color); canvas.pixel(x + 2 * scale, y + scale, color);
        canvas.pixel(x + 3 * scale, y, color);
    } else if (glyph.modifier == Modifier::Horn) {
        canvas.line(x + 4 * scale, baseY, x + 5 * scale, y, color, scale);
    }
    switch (glyph.tone) {
        case Tone::Acute: canvas.line(x + 2 * scale, y + scale, x + 3 * scale, y, color, scale); break;
        case Tone::Grave: canvas.line(x + scale, y, x + 2 * scale, y + scale, color, scale); break;
        case Tone::Hook: canvas.line(x + 2 * scale, y, x + 3 * scale, y, color, scale); canvas.pixel(x + 2 * scale, y + scale, color); break;
        case Tone::Tilde: canvas.pixel(x + scale, y, color); canvas.pixel(x + 2 * scale, y + scale, color); canvas.pixel(x + 3 * scale, y, color); break;
        case Tone::Dot: canvas.fillRect(x + 2 * scale, baseY + 8 * scale, scale, scale, color); break;
        case Tone::None: break;
    }
}
}  // namespace

Canvas::Canvas(uint16_t *pixels, int width, int height) : pixels_(pixels), width_(width), height_(height) {}

void Canvas::clear(uint16_t color) { std::fill(pixels_, pixels_ + width_ * height_, color); }

void Canvas::pixel(int x, int y, uint16_t color) {
    x += translationX_;
    y += translationY_;
    if (x >= 0 && y >= 0 && x < width_ && y < height_) pixels_[y * width_ + x] = color;
}

void Canvas::fillRect(int x, int y, int width, int height, uint16_t color) {
    x += translationX_;
    y += translationY_;
    const int left = std::max(0, x), top = std::max(0, y);
    const int right = std::min(width_, x + width), bottom = std::min(height_, y + height);
    if (right <= left || bottom <= top) return;
    for (int row = top; row < bottom; ++row)
        std::fill(pixels_ + row * width_ + left, pixels_ + row * width_ + right, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint16_t color, int thickness) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        fillRect(x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void Canvas::circle(int cx, int cy, int radius, uint16_t color, int thickness) {
    for (int r = std::max(0, radius - thickness + 1); r <= radius; ++r) {
        int x = r, y = 0, error = 1 - r;
        while (x >= y) {
            pixel(cx+x,cy+y,color); pixel(cx+y,cy+x,color); pixel(cx-y,cy+x,color); pixel(cx-x,cy+y,color);
            pixel(cx-x,cy-y,color); pixel(cx-y,cy-x,color); pixel(cx+y,cy-x,color); pixel(cx+x,cy-y,color);
            ++y; if (error < 0) error += 2*y + 1; else { --x; error += 2*(y-x) + 1; }
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, uint16_t color) {
    for (int y = -radius; y <= radius; ++y) {
        int x = 0;
        while ((x + 1) * (x + 1) + y * y <= radius * radius) ++x;
        fillRect(cx - x, cy + y, 2 * x + 1, 1, color);
    }
}

void Canvas::triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint16_t color) {
    line(x0,y0,x1,y1,color); line(x1,y1,x2,y2,color); line(x2,y2,x0,y0,color);
}

void Canvas::alphaPixel(int x, int y, uint16_t color, uint8_t alpha) {
    x += translationX_;
    y += translationY_;
    if (alpha == 0 || x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint16_t &destination = pixels_[y * width_ + x];
    if (alpha == 255) {
        destination = color;
        return;
    }
    const uint32_t inverse = 255U - alpha;
    const uint32_t red = ((((color >> 11U) & 0x1FU) * alpha) +
                          (((destination >> 11U) & 0x1FU) * inverse) + 127U) / 255U;
    const uint32_t green = ((((color >> 5U) & 0x3FU) * alpha) +
                            (((destination >> 5U) & 0x3FU) * inverse) + 127U) / 255U;
    const uint32_t blue = (((color & 0x1FU) * alpha) +
                           ((destination & 0x1FU) * inverse) + 127U) / 255U;
    destination = static_cast<uint16_t>((red << 11U) | (green << 5U) | blue);
}

void Canvas::alphaMask(int x, int y, const assets::AlphaMask &mask, uint16_t color) {
    if (!mask.alpha) return;
    for (uint16_t row = 0; row < mask.height; ++row) {
        for (uint16_t column = 0; column < mask.width; ++column) {
            const std::size_t index = static_cast<std::size_t>(row) * mask.width + column;
            alphaPixel(x + column, y + row, color, mask.alpha[index]);
        }
    }
}

void Canvas::colorBitmap(int x, int y, const assets::ColorBitmap &bitmap) {
    if (!bitmap.pixels || !bitmap.alpha) return;
    for (uint16_t row = 0; row < bitmap.height; ++row) {
        for (uint16_t column = 0; column < bitmap.width; ++column) {
            const std::size_t index = static_cast<std::size_t>(row) * bitmap.width + column;
            alphaPixel(x + column, y + row, bitmap.pixels[index], bitmap.alpha[index]);
        }
    }
}

void Canvas::colorBitmapScaled(int x, int y, int width, int height,
                               const assets::ColorBitmap &bitmap) {
    if (!bitmap.pixels || !bitmap.alpha || width <= 0 || height <= 0 ||
        bitmap.width == 0 || bitmap.height == 0) return;
    for (int row = 0; row < height; ++row) {
        const uint16_t sourceRow = static_cast<uint16_t>(
            static_cast<uint32_t>(row) * bitmap.height / static_cast<uint32_t>(height));
        for (int column = 0; column < width; ++column) {
            const uint16_t sourceColumn = static_cast<uint16_t>(
                static_cast<uint32_t>(column) * bitmap.width / static_cast<uint32_t>(width));
            const std::size_t index = static_cast<std::size_t>(sourceRow) * bitmap.width + sourceColumn;
            alphaPixel(x + column, y + row, bitmap.pixels[index], bitmap.alpha[index]);
        }
    }
}

namespace {
const assets::FontGlyph *fontGlyph(const assets::BitmapFont &font, uint32_t codepoint) {
    std::size_t first = 0;
    std::size_t count = font.glyphCount;
    while (count > 0) {
        const std::size_t step = count / 2;
        const std::size_t index = first + step;
        if (font.glyphs[index].codepoint < codepoint) {
            first = index + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    if (first < font.glyphCount && font.glyphs[first].codepoint == codepoint)
        return &font.glyphs[first];
    if (codepoint != static_cast<uint32_t>('?')) return fontGlyph(font, '?');
    return nullptr;
}

int codepointWidth(const assets::BitmapFont &font, uint32_t codepoint) {
    const assets::FontGlyph *glyph = fontGlyph(font, codepoint);
    return glyph ? glyph->advance : 0;
}
}  // namespace

int Canvas::fontTextWidth(const char *utf8, const assets::BitmapFont &font) const {
    if (!utf8) return 0;
    int width = 0;
    const char *cursor = utf8;
    while (*cursor) width += codepointWidth(font, nextCodepoint(cursor));
    return width;
}

void Canvas::fontText(int x, int y, const char *utf8, const assets::BitmapFont &font,
                      uint16_t color, int maxWidth, bool centered) {
    if (!utf8 || !font.glyphs || !font.bitmap4bpp) return;
    std::array<uint32_t, 64> codepoints{};
    std::size_t count = 0;
    const char *cursor = utf8;
    while (*cursor && count < codepoints.size()) codepoints[count++] = nextCodepoint(cursor);

    std::size_t visible = count;
    bool ellipsis = false;
    int drawnWidth = 0;
    for (std::size_t index = 0; index < count; ++index)
        drawnWidth += codepointWidth(font, codepoints[index]);
    if (maxWidth > 0 && drawnWidth > maxWidth) {
        ellipsis = true;
        const int dotsWidth = 3 * codepointWidth(font, '.');
        visible = 0;
        drawnWidth = dotsWidth;
        while (visible < count) {
            const int next = codepointWidth(font, codepoints[visible]);
            if (drawnWidth + next > maxWidth) break;
            drawnWidth += next;
            ++visible;
        }
    }
    if (centered && maxWidth > 0) x += (maxWidth - drawnWidth) / 2;

    auto drawCodepoint = [&](uint32_t codepoint) {
        const assets::FontGlyph *glyph = fontGlyph(font, codepoint);
        if (!glyph) return;
        const std::size_t pixelCount = static_cast<std::size_t>(glyph->width) * glyph->height;
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
            const uint8_t packed = font.bitmap4bpp[glyph->bitmapOffset + pixelIndex / 2U];
            const uint8_t nibble = (pixelIndex & 1U) == 0 ? packed >> 4U : packed & 0x0FU;
            if (nibble != 0) {
                const int column = static_cast<int>(pixelIndex % glyph->width);
                const int row = static_cast<int>(pixelIndex / glyph->width);
                alphaPixel(x + glyph->xOffset + column, y + glyph->yOffset + row,
                           color, static_cast<uint8_t>(nibble * 17U));
            }
        }
        x += glyph->advance;
    };

    for (std::size_t index = 0; index < visible; ++index) drawCodepoint(codepoints[index]);
    if (ellipsis) for (int index = 0; index < 3; ++index) drawCodepoint('.');
}

int Canvas::textWidth(const char *utf8, int scale, int maxCells) const {
    int cells = countCodepoints(utf8);
    if (maxCells >= 0) cells = std::min(cells, maxCells);
    return cells == 0 ? 0 : cells * 6 * scale - scale;
}

void Canvas::text(int x, int y, const char *utf8, uint16_t color, int scale, int maxWidth, bool centered) {
    if (!utf8 || scale <= 0) return;
    const int cellWidth = 6 * scale;
    int maxCells = maxWidth > 0 ? std::max(1, (maxWidth + scale) / cellWidth) : countCodepoints(utf8);
    const int total = countCodepoints(utf8);
    const bool ellipsis = total > maxCells && maxCells >= 3;
    const int drawnCells = std::min(total, maxCells);
    if (centered) x += (maxWidth - (drawnCells * cellWidth - scale)) / 2;

    const char *cursor = utf8;
    for (int index = 0; index < drawnCells; ++index) {
        DecodedGlyph glyph;
        if (ellipsis && index >= drawnCells - 3) glyph = {'.', Modifier::None, Tone::None};
        else glyph = decodeVietnamese(nextCodepoint(cursor));
        drawGlyph(*this, x + index * cellWidth, y, glyph, color, scale);
    }
}

}  // namespace waze_hud
