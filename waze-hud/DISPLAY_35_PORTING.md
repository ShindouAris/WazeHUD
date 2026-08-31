# Hỗ trợ màn hình 3.5 inch 320×480

Branch `support-3.5-in-screen` có profile bố cục cho panel 3.5 inch độ phân giải gốc **320×480**, sử dụng ở chế độ landscape **480×320**.

## Build profile 480×320

Chạy trong ESP-IDF 5.5.5:

```powershell
idf.py -B build-35 `
    -D SDKCONFIG=sdkconfig.35 `
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.35.defaults" `
    build
```

Profile cũ 320×170 vẫn build theo cách thông thường:

```powershell
idf.py -B build build
```

Build mock 480×320 để kiểm tra trực tiếp trên module mà không cần điện thoại:

```powershell
idf.py -B build-35-mock `
    -D SDKCONFIG=sdkconfig.35.mock `
    -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.35.defaults;sdkconfig.mock.defaults" `
    build
```

## Thiết kế layout

Renderer sử dụng mặt phẳng thiết kế 320×213 và scale đều khoảng 1,5 lần sang framebuffer 480×320. Cách này giữ đúng tỷ lệ của:

- icon hướng rẽ;
- biển giới hạn tốc độ hình tròn;
- icon cảnh báo;
- font và nét vẽ primitive.

Các dirty region ở profile 3.5 inch:

| Vùng | Logical | Physical xấp xỉ |
| --- | --- | --- |
| Maneuver | 85×175 | 128×263 |
| Speed | 80×175 | 120×263 |
| Speed limits | 60×175 | 90×263 |
| Alerts | 95×175 | 142×263 |
| Street/clock | 320×38 | 480×57 |

HLP `dev.disp` tự khai báo `480×320` khi profile này được chọn.

## Backend phần cứng ES3C35P

Profile sử dụng driver chính thức `espressif/esp_lcd_st77922` với QSPI 40 MHz:

| Tín hiệu | GPIO |
| --- | ---: |
| LCD CS | 10 |
| LCD CLK | 12 |
| LCD D0 | 11 |
| LCD D1 | 13 |
| LCD D2 | 14 |
| LCD D3 | 9 |
| Backlight | 41 |
| KEY/BOOT | 0 |
| Battery ADC | 8 |

LCD reset dùng chung với chân EN của ESP32-S3, vì vậy firmware dùng software reset của ST77922 sau khi MCU khởi động. Dirty-region X được căn theo bội số bốn pixel theo yêu cầu của ST77922 QSPI.

Touch I²C chưa được firmware HUD sử dụng. Pin touch của module là SDA 38, SCL 39, RESET 48 và INT 47.
