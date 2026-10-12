"""Source regression guard: python tools/check_display_startup.py."""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "main/display/hud_renderer.cpp").read_text(encoding="utf-8")
render = source[source.index("void HudRenderer::render(const HudState"):]
startup = render[:render.index("firstFrame_ = false;")]
assert "setBrightness(settings.brightness)" in startup, "Backlight stays at startup duty=0"
assert "setOrientation(settings.mirrorHud, settings.rotateDisplay)" in startup, "Display orientation is not applied"
print("PASS: brightness and orientation applied before first-frame acknowledgement")
