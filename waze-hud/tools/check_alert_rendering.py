"""Source regression guard; run: python tools/check_alert_rendering.py.

Checks rendering dispatch, not pixels or hardware. Firmware build remains required.
"""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "main/display/hud_renderer.cpp").read_text(encoding="utf-8")


def body(signature):
    start = source.index("{", source.index(signature))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1]


# Both minimalist slots must use value-aware selection and primitive fallback.
assert "alert.valueKmh" in source, "SpeedDrop value lost: generic hazard replaces speed sign"
selection = body("alertBitmap(")
assert "AlertKind::SpeedDrop" in selection
assert "assets::kSpeedLimitAssets" in selection
assert "entry.value != alert.valueKmh" in selection
assert "dominant ? entry.alertLarge : entry.alertSmall" in selection
icon = body("drawAlertIcon(")
assert "AlertKind::SpeedDrop" in icon and "alert.valueKmh" in icon
assert "canvas.fillCircle(" in icon and "canvas.circle(" in icon
assert "assets::kNumberMedium" in icon and "assets::kNumberSmall" in icon
assert "bitmap->pixels && bitmap->alpha" in icon
assert "drawAlertIcon(" in body("void HudRenderer::renderAlertSide(")
assert "drawAlertIcon(" in body("void HudRenderer::render(const HudState")
print("PASS: SpeedDrop value assets and primitive fallback in both layouts")

assert "state.noPassingRemainingM" in source, "Active no-passing avgL distance ignored"
primary = body("primaryAlert(")
assert "if (state.noPassingZone)" in primary
assert "alert.kind = AlertKind::NoPassing" in primary
assert "alert.distanceM = state.noPassingRemainingM" in primary
assert "return state.nearestAlert" in primary
render = body("void HudRenderer::render(const HudState")
assert "state.noPassingZone != previous_.noPassingZone" in render
assert "state.noPassingRemainingM != previous_.noPassingRemainingM" in render
assert "const AlertState alert = primaryAlert(state)" in render
assert "renderAlertSide(canvas, alert, 160)" in render
assert "alertBitmap(alert, false)" in render
assert "formatDistance(alert.distanceM" in render
assert "state.nearestAlert" not in render[render.index("const bool overspeed"):]
assert "distanceM" not in body("bool hasAlert("), "Alert visibility must not filter distance"
print("PASS: active no-passing priority, avgL redraw, both layouts, no distance filter")

for turn in ("SlightLeft", "SlightRight", "SharpLeft", "SharpRight"):
    assert f"case Maneuver::{turn}:" in source, f"{turn} has no primitive arrow"
arrows = body("drawTurnFallback(")
assert "canvas.line(" in arrows and "arrowHead(" in arrows
side = body("void HudRenderer::renderManeuverSide(")
assert "drawTurnFallback(canvas, startX, iconY, maneuver)" in side
assert "drawTurnFallback(canvas, 130, 40, state.maneuver)" in render
print("PASS: slight/sharp left/right primitive arrows in both layouts")
