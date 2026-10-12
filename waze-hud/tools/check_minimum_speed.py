"""Check minimum-speed negotiation and decoder presence; no board required."""
from pathlib import Path
import json
import re

main = Path(__file__).resolve().parents[1] / "main"
protocol = (main / "protocol/hlp_protocol.cpp").read_text(encoding="utf-8")
declaration = protocol.split("constexpr char declaration[] =", 1)[1].split(";", 1)[0]
declaration = declaration.replace("WAZE_HUD_FIRMWARE_VERSION", '"test"')
message = json.loads("".join(json.loads(token) for token in re.findall(r'"(?:\\.|[^"\\])*"', declaration)))
assert "min" in message["want"]["fields"], "Minimum speed is decoded but never requested from producer"
assert len(json.dumps(message, separators=(",", ":")).encode()) + 1 <= 512
source = (main / "protocol/hlp_decoder.cpp").read_text(encoding="utf-8")
assert 'cJSON_GetObjectItemCaseSensitive(root, "min")' in source
assert "decoded.minimumSpeedKmh" in source
print("PASS: min requested and decoded; declaration fits HLP frame")
