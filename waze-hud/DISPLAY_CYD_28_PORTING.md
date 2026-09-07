# ESP32-2432S028 / CYD 2.8-inch port

This branch targets the classic ESP32-WROOM-32 Cheap Yellow Display marked
`ESP32-2432S028`. Some vendor documents append `R` for the resistive-touch SKU,
but WazeHUD does not use touch and identifies the board by its verified LCD
wiring. It does not target similarly named ESP32-S3 variants.

## Verified hardware assumptions

The manufacturer's specification identifies an ESP32-WROOM-32 module with
4 MB flash, a 240×320 ILI9341 TFT and a resistive touch overlay. The LCD wiring
used by known ESP-IDF examples is:

| Signal | GPIO | Notes |
|---|---:|---|
| TFT MOSI | 13 | SPI2 |
| TFT MISO | 12 | Wired, not needed for normal draws |
| TFT SCLK | 14 | 40 MHz |
| TFT CS | 15 | Boot strap pin; panel CS is already wired correctly |
| TFT DC | 2 | Boot strap pin; do not add external pull resistors |
| TFT reset | — | Tied to ESP32 EN |
| Backlight | 21 | LEDC PWM, active high |
| BOOT/status button | 0 | Single press rotates; double press mirrors; long press shows status |

Other onboard devices are deliberately not initialized: XPT2046 touch uses
GPIO 25/32/39/33/36, the SD slot uses GPIO 18/23/19/5, the active-low RGB LED
uses GPIO 4/16/17, and GPIO34 is the LDR input. The board exposes no
battery-voltage divider, so WazeHUD reports battery status as unavailable.

Primary/reference material:

- Manufacturer specification: https://mischianti.org/wp-content/uploads/2025/04/ESP32-2432S028-Specifications-EN.pdf
- ESP-IDF CYD example: https://github.com/limpens/esp32-2432S028
- Espressif ILI9341 component: https://components.espressif.com/components/espressif/esp_lcd_ili9341

## Display strategy

The renderer uses a native 320×240 landscape canvas. Vertical positions are
spread across the extra height, while fonts and bitmap assets remain at a 1:1
pixel aspect ratio instead of being stretched from the 1.9-inch layout. The
ILI9341 remains in native 240×320 portrait addressing. Every dirty region is
split into 20-row stripes, byte-swapped and
transposed before the SPI transfer. Mirror-HUD and 180-degree rotation are
composed in the same software transform, avoiding panel/LVGL rotation mismatch.

## Build

Activate ESP-IDF 5.5.5, then run:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
Set-Location D:\Code\WazeHUD\waze-hud
idf.py set-target esp32
idf.py build
```

The simpler branch-default flow is `idf.py set-target esp32`, `idf.py build`.
Flash with `idf.py -p COM_PORT flash monitor`. The partition table has two
1856 KiB OTA slots and fits the board's 4 MB flash.

## Hardware validation checklist

1. Confirm the PCB marking is `ESP32-2432S028` and the module is ESP32-WROOM-32.
2. Flash a mock build first and confirm black background, BGR colors and landscape orientation.
3. Press BOOT once to rotate, twice to mirror, or hold it to show BLE status.
4. Connect from Waze Mod and verify BLE updates while the display is active.
5. Run marquee and rapid navigation changes for at least 15 minutes and watch for SPI stripes or watchdog resets.
