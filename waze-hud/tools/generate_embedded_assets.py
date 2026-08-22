#!/usr/bin/env python3
"""Generate deterministic, runtime-decode-free HUD assets from the source pack.

The generated C++ is checked in so a normal ESP-IDF build does not require
Python or Pillow. Re-run this script when the source PNG/TTF files change.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import unicodedata

from PIL import Image, ImageDraw, ImageFont


PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT.parent / "assets"
OUTPUT = PROJECT / "main" / "assets"


MANEUVERS = {
    "ManeuverContinue": "car_big_trans_direction_forward.png",
    "ManeuverLeft": "car_big_trans_direction_left.png",
    "ManeuverRight": "car_big_trans_direction_right.png",
    "ManeuverUTurn": "car_big_trans_direction_u_turn.png",
    "ManeuverRoundabout": "car_big_trans_directions_roundabout.png",
    "ManeuverRoundaboutLeft": "car_big_trans_directions_roundabout_l.png",
    "ManeuverRoundaboutRight": "car_big_trans_directions_roundabout_r.png",
    "ManeuverExitLeft": "car_big_trans_direction_exit_left.png",
    "ManeuverExitRight": "car_big_trans_direction_exit_right.png",
    "ManeuverArrive": "car_big_trans_direction_end.png",
}

ALERTS = {
    "AlertPolice": "alerts/bigpin_police.png",
    "AlertSpeedCamera": "fallbacks/penalty_camera.png",
    "AlertRedLightCamera": "alerts/bigpin_red_light_camera.png",
    "AlertHazard": "alerts/bigpin_hazard.png",
    "AlertAccident": "alerts/bigpin_accident.png",
    "AlertTrafficJam": "alerts/bigpin_traffic_3.png",
    "AlertRoadClosed": "alerts/bigpin_closure.png",
    "AlertNoPassing": "alerts/no_passing_in.png",
}

VIETNAMESE = (
    "ÀÁẢÃẠĂẮẰẲẴẶÂẤẦẨẪẬ"
    "àáảãạăắằẳẵặâấầẩẫậ"
    "Đđ"
    "ÈÉẺẼẸÊẾỀỂỄỆ"
    "èéẻẽẹêếềểễệ"
    "ÌÍỈĨỊìíỉĩị"
    "ÒÓỎÕỌÔỐỒỔỖỘƠỚỜỞỠỢ"
    "òóỏõọôốồổỗộơớờởỡợ"
    "ÙÚỦŨỤƯỨỪỬỮỰ"
    "ùúủũụưứừửữự"
    "ỲÝỶỸỴỳýỷỹỵ"
)
TEXT_CODEPOINTS = sorted(set(range(32, 127)) | {ord(c) for c in VIETNAMESE})
NUMBER_CODEPOINTS = [ord(c) for c in " -0123456789?"]


@dataclass
class Glyph:
    codepoint: int
    offset: int
    width: int
    height: int
    x_offset: int
    y_offset: int
    advance: int


def cpp_bytes(data: bytes, columns: int = 20) -> str:
    if not data:
        return "    0x00"
    rows = []
    for start in range(0, len(data), columns):
        chunk = data[start : start + columns]
        rows.append("    " + ",".join(f"0x{value:02X}" for value in chunk))
    return ",\n".join(rows)


def cpp_words(data: list[int], columns: int = 12) -> str:
    rows = []
    for start in range(0, len(data), columns):
        chunk = data[start : start + columns]
        rows.append("    " + ",".join(f"0x{value:04X}" for value in chunk))
    return ",\n".join(rows)


def fit_rgba(path: Path, size: int) -> Image.Image:
    image = Image.open(path).convert("RGBA")
    box = image.getchannel("A").getbbox()
    if box:
        image = image.crop(box)
    usable = size - 2
    scale = min(usable / image.width, usable / image.height)
    resized = image.resize(
        (max(1, round(image.width * scale)), max(1, round(image.height * scale))),
        Image.Resampling.LANCZOS,
    )
    result = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    result.alpha_composite(resized, ((size - resized.width) // 2, (size - resized.height) // 2))
    return result


def rgb565(pixel: tuple[int, int, int, int]) -> int:
    red, green, blue, _ = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def pack_nibbles(values: list[int]) -> bytes:
    result = bytearray()
    for index in range(0, len(values), 2):
        high = values[index] >> 4
        low = values[index + 1] >> 4 if index + 1 < len(values) else 0
        result.append((high << 4) | low)
    return bytes(result)


def build_font(path: Path, size: int, codepoints: list[int]) -> tuple[list[Glyph], bytes, int]:
    font = ImageFont.truetype(str(path), size)
    bounds = [font.getbbox(chr(cp), anchor="la") for cp in codepoints]
    global_top = min(box[1] for box in bounds)
    global_bottom = max(box[3] for box in bounds)
    line_height = global_bottom - global_top
    bitmap = bytearray()
    glyphs: list[Glyph] = []
    for codepoint, box in zip(codepoints, bounds):
        left, top, right, bottom = box
        width, height = max(0, right - left), max(0, bottom - top)
        offset = len(bitmap)
        if width and height:
            image = Image.new("L", (width, height), 0)
            draw = ImageDraw.Draw(image)
            draw.text((-left, -top), chr(codepoint), font=font, fill=255, anchor="la")
            bitmap.extend(pack_nibbles(list(image.get_flattened_data())))
        glyphs.append(
            Glyph(
                codepoint=codepoint,
                offset=offset,
                width=width,
                height=height,
                x_offset=left,
                y_offset=top - global_top,
                advance=max(1, round(font.getlength(chr(codepoint)))),
            )
        )
    return glyphs, bytes(bitmap), line_height


def emit_font(name: str, source: Path, size: int, cps: list[int]) -> tuple[str, str]:
    glyphs, bitmap, line_height = build_font(source, size, cps)
    glyph_rows = []
    for glyph in glyphs:
        glyph_rows.append(
            "    {0x%04X,%d,%d,%d,%d,%d,%d}"
            % (
                glyph.codepoint,
                glyph.offset,
                glyph.width,
                glyph.height,
                glyph.x_offset,
                glyph.y_offset,
                glyph.advance,
            )
        )
    body = (
        f"alignas(4) static const uint8_t k{name}Bitmap[] = {{\n{cpp_bytes(bitmap)}\n}};\n"
        f"static const FontGlyph k{name}Glyphs[] = {{\n" + ",\n".join(glyph_rows) + "\n};\n"
        f"const BitmapFont k{name}{{k{name}Glyphs, "
        f"sizeof(k{name}Glyphs) / sizeof(k{name}Glyphs[0]), k{name}Bitmap, {line_height}}};\n"
    )
    declaration = f"extern const BitmapFont k{name};"
    return declaration, body


def emit_mask(name: str, source: Path, size: int) -> tuple[str, str]:
    image = fit_rgba(source, size)
    alpha = bytes(image.getchannel("A").get_flattened_data())
    declaration = f"extern const AlphaMask k{name};"
    body = (
        f"alignas(4) static const uint8_t k{name}Alpha[] = {{\n{cpp_bytes(alpha)}\n}};\n"
        f"const AlphaMask k{name}{{{size},{size},k{name}Alpha}};\n"
    )
    return declaration, body


def emit_color_bitmap(name: str, source: Path, size: int) -> tuple[str, str]:
    image = fit_rgba(source, size)
    pixels = [rgb565(pixel) for pixel in image.get_flattened_data()]
    alpha = bytes(image.getchannel("A").get_flattened_data())
    declaration = f"extern const ColorBitmap k{name};"
    body = (
        f"alignas(4) static const uint16_t k{name}Pixels[] = {{\n{cpp_words(pixels)}\n}};\n"
        f"alignas(4) static const uint8_t k{name}Alpha[] = {{\n{cpp_bytes(alpha)}\n}};\n"
        f"const ColorBitmap k{name}{{{size},{size},k{name}Pixels,k{name}Alpha}};\n"
    )
    return declaration, body


def main() -> None:
    missing = []
    for filename in MANEUVERS.values():
        if not (SOURCE / "Waze" / filename).is_file():
            missing.append(filename)
    for filename in ALERTS.values():
        if not (SOURCE / filename).is_file():
            missing.append(filename)
    for filename in ("font_number.ttf", "font_text.otf"):
        if not (SOURCE / "fonts" / filename).is_file():
            missing.append(filename)
    if missing:
        raise SystemExit("Missing source assets: " + ", ".join(missing))

    declarations: list[str] = []
    bodies: list[str] = []
    for name, filename in MANEUVERS.items():
        declaration, body = emit_mask(name, SOURCE / "Waze" / filename, 60)
        declarations.append(declaration)
        bodies.append(body)
    for name, filename in ALERTS.items():
        for suffix, size in (("Large", 44), ("Small", 26)):
            declaration, body = emit_color_bitmap(name + suffix, SOURCE / filename, size)
            declarations.append(declaration)
            bodies.append(body)

    speed_limit_rows: list[str] = []
    speed_limit_sources: list[tuple[int, Path]] = []
    for path in (SOURCE / "speedLimit").glob("speed_limit_*.png"):
        match = re.fullmatch(r"speed_limit_(\d+)\.png", path.name)
        if match:
            speed_limit_sources.append((int(match.group(1)), path))
    for value, path in sorted(speed_limit_sources):
        names = []
        for suffix, size in (("Current", 56), ("AlertLarge", 44), ("AlertSmall", 26)):
            name = f"SpeedLimit{value}{suffix}"
            _, body = emit_color_bitmap(name, path, size)
            bodies.append(body)
            names.append(name)
        speed_limit_rows.append(
            f"    {{{value},&k{names[0]},&k{names[1]},&k{names[2]}}}"
        )
    bodies.append(
        "const SpeedLimitAssetSet kSpeedLimitAssets[] = {\n"
        + ",\n".join(speed_limit_rows)
        + "\n};\n"
        + "const std::size_t kSpeedLimitAssetCount = "
        + "sizeof(kSpeedLimitAssets) / sizeof(kSpeedLimitAssets[0]);\n"
    )

    font_specs = (
        ("TextSmall", SOURCE / "fonts" / "font_text.otf", 10, TEXT_CODEPOINTS),
        ("TextMedium", SOURCE / "fonts" / "font_text.otf", 16, TEXT_CODEPOINTS),
        ("TextLarge", SOURCE / "fonts" / "font_text.otf", 22, TEXT_CODEPOINTS),
        ("NumberSmall", SOURCE / "fonts" / "font_number.ttf", 13, NUMBER_CODEPOINTS),
        ("NumberMedium", SOURCE / "fonts" / "font_number.ttf", 27, NUMBER_CODEPOINTS),
        ("NumberLarge", SOURCE / "fonts" / "font_number.ttf", 64, NUMBER_CODEPOINTS),
    )
    for name, path, size, codepoints in font_specs:
        declaration, body = emit_font(name, path, size, codepoints)
        declarations.append(declaration)
        bodies.append(body)

    OUTPUT.mkdir(parents=True, exist_ok=True)
    header = """#pragma once

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

""" + "\n".join(declarations) + "\n\nextern const SpeedLimitAssetSet kSpeedLimitAssets[];\nextern const std::size_t kSpeedLimitAssetCount;\n\n}  // namespace waze_hud::assets\n"
    source = """// Generated by tools/generate_embedded_assets.py. Do not edit manually.
#include "assets/generated_assets.h"

namespace waze_hud::assets {

""" + "\n".join(bodies) + "\n}  // namespace waze_hud::assets\n"
    (OUTPUT / "generated_assets.h").write_text(header, encoding="utf-8", newline="\n")
    (OUTPUT / "generated_assets.cpp").write_text(source, encoding="utf-8", newline="\n")
    print(f"Generated {OUTPUT / 'generated_assets.h'}")
    print(f"Generated {OUTPUT / 'generated_assets.cpp'} ({len(source):,} chars)")


if __name__ == "__main__":
    main()
