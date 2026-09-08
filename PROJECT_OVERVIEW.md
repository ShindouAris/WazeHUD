# Tổng quan nhanh dự án WazeHUD

> Cập nhật: 2026-09-07 · Branch hiện tại: `2.8-in-CYD` (CYD mặc định) · ESP-IDF 5.5.5

WazeHUD là firmware ESP32 nhận dữ liệu dẫn đường Waze qua BLE/HLP/1 và hiển thị HUD ô tô có độ trễ thấp. Mã nguồn ứng dụng nằm trong `waze-hud/`; assets gốc nằm trong `assets/` và được chuyển thành dữ liệu nhúng để ESP32 không phải giải mã PNG/font khi chạy.

## Trạng thái quan trọng

- Branch `2.8-in-CYD` dành riêng cho ESP32-2432S028 và build mặc định cho target `esp32`.
- Backend CYD dùng ILI9341, SPI2 40 MHz, BGR, `INVON` và xoay dirty stripe từ landscape native 320×240 sang panel native 240×320; font/icon giữ tỉ lệ pixel 1:1.
- Board có flash 4 MB, không PSRAM; partition table mặc định đã thu về hai OTA slot 1856 KiB.
- Build phần mềm đã thành công; màu, orientation, BLE và độ ổn định vẫn cần xác nhận trên phần cứng.
- Mốc code hiện tại là commit `85853d6` (`fix: black screen`).

## Phần cứng và profile hiển thị

| Profile | Panel | Giao tiếp | Kích thước UI | Cách build |
| --- | --- | --- | --- | --- |
| CYD 2.8 inch | ILI9341 | SPI2, 40 MHz | 320×240 landscape | `build/` (mặc định branch) |

Chân LCD của CYD 2.8:

| Tín hiệu | GPIO |
| --- | ---: |
| CS / DC | 15 / 2 |
| CLK | 14 |
| MOSI / MISO | 13 / 12 |
| Backlight PWM | 21 |
| KEY/BOOT | 0 |
| Battery ADC | Không có |

## Kiến trúc

```text
Điện thoại / Waze Mod
        │ BLE GATT
        ▼
NimBLE transport ── bounded queue ──► HLP protocol task
                                            │
                                  framing + JSON decode
                                            ▼
                                  HudStateStore snapshot
                                            │
                                            ▼
                         dirty-region renderer + embedded assets
                                            │
                                            ▼
                                  display driver RGB565
```

Các module chính:

| Đường dẫn | Trách nhiệm |
| --- | --- |
| `main/bluetooth/` | BLE GATT, advertising, write/notify và reconnect |
| `main/protocol/` | HLP framing, handshake, heartbeat và decode state |
| `main/state/` | Snapshot HUD cố định dung lượng, truyền an toàn giữa task |
| `main/display/` | Layout, renderer, font và driver LCD theo profile |
| `main/config/` | Cấu hình động, validation và lưu NVS |
| `main/system/` | Pin, RSSI Bluetooth và trạng thái hệ thống |
| `main/assets/` | RGB565, alpha mask và font đã generate |

BLE callback chỉ sao chép dữ liệu vào queue. JSON, cập nhật state và truyền LCD chạy ngoài callback. Renderer chỉ vẽ lại vùng bị thay đổi, không redraw toàn màn hình theo mỗi state.

## Khả năng hiện có

- Nhận HLP/1 qua BLE, frame tối đa 512 byte, xử lý BLE chunk tùy ý.
- Gửi `dev`, trả lời `ping/pong`, kiểm tra UTF-8 và bỏ frame lỗi an toàn.
- Hiển thị tốc độ, biển giới hạn, hướng rẽ, vòng xuyến/lối ra, tối đa 10 làn đường, ETA, tên đường Việt Nam và cảnh báo; đầu mũi tên lane dùng asset Waze, hàng guidance nằm trên tên đường với ETA bên trái, còn `alrs` thứ 2/3 nằm ngay dưới cảnh báo chính.
- Tên đường dài chạy marquee; đồng hồ có dấu `:` nhấp nháy theo giây.
- Cảnh báo gần dưới 500 m đổi màu khoảng cách sang xanh.
- LED RGB phía sau đổi màu liên tục khi chưa kết nối, xanh dương khi chờ state, xanh lá ở tốc độ bình thường và nháy đỏ 2 Hz khi quá tốc độ.
- Hỗ trợ mirror HUD (nhấn đúp BOOT), xoay 180° (nhấn đơn), độ sáng, theme, offset và ngưỡng quá tốc độ lưu trong NVS.
- Cấu hình `Hien thi toc do` cho phép giữ tốc độ xe làm chính hoặc dùng biển giới hạn lớn làm chính với tốc độ xe nhỏ ở góc dưới-phải.
- KEY hiển thị trạng thái pin/BLE và đổi hướng màn hình theo cấu hình phần cứng.
- Mock mode chạy UI không cần điện thoại.

## Build nhanh

Kích hoạt ESP-IDF trên máy Windows hiện tại:

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'
Set-Location D:\Code\WazeHUD\waze-hud
```

Build CYD 2.8 trên branch này:

```powershell
idf.py -B build build
```

Build mock CYD:

```powershell
idf.py -B build-mock `
  -D SDKCONFIG=sdkconfig.mock `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mock.defaults" `
  build
```

## Firmware phân phối

Các file đã đóng gói nằm trong `waze-hud/dist/`. Bản tối ưu CYD hiện tại là R2:

| File | Cách dùng |
| --- | --- |
| `WazeHUD-CYD28-V10-25082026-20260907-R2-Factory.bin` | Flash tại offset `0x0`; gồm bootloader, partition table, OTA data và app |
| `WazeHUD-CYD28-V10-25082026-20260907-R2-OTA.bin` | App image; dùng với đúng partition table, app offset `0x20000` |
| `WazeHUD-CYD28-V10-25082026-20260907-R2-SHA256.txt` | Hash kiểm tra tính toàn vẹn |

Factory BIN phù hợp để cài mới hoặc khôi phục. OTA BIN không được flash tại `0x0`.

## Việc cần xác nhận tiếp

1. Flash mock profile lên đúng board ESP32-2432S028 để xác nhận màu BGR, orientation, backlight và software transpose.
2. Kết nối Waze Mod để xác nhận BLE/HLP trong lúc LCD SPI hoạt động.
3. Chạy mock lâu để kiểm tra dirty-region, marquee và biến đổi mirror/rotation.

## Tài liệu liên quan

- `README.md`: mô tả firmware CYD và HLP/1.
- `waze-hud/DISPLAY_CYD_28_PORTING.md`: pin map, build và checklist xác nhận CYD 2.8 inch.
- `waze-hud/FLASH_FIRMWARE_VI.md`: hướng dẫn flash bằng tiếng Việt.
- `waze-hud-link-sdk-ai-bundle.md`: đặc tả HLP/1 nguồn chuẩn.
- `waze-hud/main/Kconfig.projbuild`: display profile và tùy chọn build.
