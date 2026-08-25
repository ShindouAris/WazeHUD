# Hướng dẫn flash firmware WazeHUD

Tài liệu này hướng dẫn nạp firmware WazeHUD cho **LILYGO T-Display S3 (ESP32-S3, flash 16 MB)** trên Windows bằng file BIN hoặc trực tiếp từ mã nguồn ESP-IDF.

> [!WARNING]
> Lệnh `erase_flash` và file factory sẽ xóa cấu hình NVS, thông tin ghép đôi BLE và firmware cũ. Hãy dùng factory flash khi cài mới, khôi phục board hoặc khi firmware hiện tại hoạt động không ổn định.

> [!IMPORTANT]
> Nút đưa board vào chế độ tải firmware là **BOOT (GPIO0)**. Nút **KEY (GPIO14)** của WazeHUD không phải nút BOOT.

## Flash nhanh bằng factory BIN

Đây là cách được khuyến nghị cho người dùng cuối. Factory BIN đã chứa bootloader, bảng phân vùng, OTA data và ứng dụng.

### Chuẩn bị

- Cáp USB-C có truyền dữ liệu.
- Python 3.10 trở lên.
- File mới nhất có tên `dist/waze-hud-production-factory-*.bin`.
- Đóng ESP-IDF Monitor, Arduino Serial Monitor và các ứng dụng khác đang dùng cổng COM.

Cài `esptool`:

```powershell
python -m pip install --upgrade esptool
```

Mở PowerShell tại thư mục `waze-hud`, sau đó tìm cổng COM:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name
```

Ví dụ bên dưới dùng `COM9`. Thay `COM9` bằng cổng của board trên máy bạn.

### Xóa và flash factory BIN

Chọn tự động factory BIN mới nhất:

```powershell
$factoryFirmware = (Get-ChildItem .\dist\waze-hud-production-factory-*.bin |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1).FullName
```

Xóa flash cũ:

```powershell
python -m esptool --chip esp32s3 -p COM9 -b 460800 erase_flash
```

Nạp firmware tại offset `0x0`:

```powershell
python -m esptool --chip esp32s3 -p COM9 -b 460800 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_freq 80m --flash_size 16MB `
    0x0 $factoryFirmware
```

Khi xuất hiện `Hash of data verified` và `Hard resetting via RTS pin`, quá trình flash đã hoàn tất.

## Flash trực tiếp từ mã nguồn

Cách này dành cho người phát triển đã cài **ESP-IDF 5.5.5**.

Mở ESP-IDF PowerShell hoặc kích hoạt môi trường ESP-IDF 5.5.5, sau đó chạy tại thư mục `waze-hud`:

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM9 flash
```

Lệnh `idf.py flash` sử dụng đúng layout của project:

| Thành phần | Offset |
| --- | ---: |
| Bootloader | `0x0` |
| Partition table | `0x8000` |
| OTA data | `0xF000` |
| Ứng dụng `ota_0` | `0x20000` |

Để chỉ build và tạo BIN mà không flash:

```powershell
idf.py set-target esp32s3
idf.py build
```

Ứng dụng sau khi build nằm tại:

```text
build/waze_hud_tdisplay_s3.bin
```

## Phân biệt factory BIN và OTA BIN

| File | Mục đích | Offset khi flash bằng cáp |
| --- | --- | ---: |
| `waze-hud-production-factory-*.bin` | Cài mới hoặc khôi phục toàn bộ firmware | `0x0` |
| `waze-hud-production-ota-*.bin` | Chỉ chứa image ứng dụng để cập nhật đúng phân vùng OTA | Không flash tại `0x0` |

Không dùng OTA BIN để cài lên board trắng. Nếu bootloader, partition table hoặc OTA data trên board không khớp, hãy dùng factory BIN.

Việc ghi thủ công OTA BIN vào `0x20000` chỉ phù hợp khi chắc chắn board đang dùng đúng bảng phân vùng của project và boot từ `ota_0`. Trong các trường hợp còn lại, factory BIN an toàn hơn.

## Vào download mode thủ công

Nếu esptool dừng ở `Connecting...`, thực hiện theo thứ tự:

1. Giữ nút **BOOT**.
2. Trong khi vẫn giữ BOOT, nhấn rồi thả **RESET/RST**.
3. Thả nút BOOT.
4. Chạy lại lệnh flash.

Không cần giữ BOOT nếu esptool đã tự kết nối được.

## Kiểm tra sau khi flash

Sau khi board khởi động lại:

1. Màn hình phải hiện logo `WazeHUD` và trạng thái chờ điện thoại.
2. Điện thoại phải tìm thấy thiết bị BLE tên `WazeHUD`.
3. Kết nối bằng ứng dụng hỗ trợ HLP/1 và bắt đầu dẫn đường.
4. Bấm ngắn KEY để xoay màn hình; giữ KEY khoảng 1,2 giây để xem trạng thái pin và BLE.

Có thể xem log bằng:

```powershell
idf.py -p COM9 monitor
```

Thoát monitor bằng `Ctrl+]`. Hãy đóng monitor trước khi rút USB hoặc flash lại.

## Xử lý lỗi thường gặp

### Không thấy cổng COM

- Thử cáp USB-C khác có hỗ trợ dữ liệu.
- Đổi cổng USB trên máy tính, không dùng hub nếu có thể.
- Rút và cắm lại board rồi chạy lại lệnh liệt kê cổng COM.
- Thử vào download mode thủ công.

### Treo ở `Connecting...`

- Đóng mọi chương trình đang giữ cổng COM.
- Kiểm tra đã chọn đúng cổng.
- Thực hiện tổ hợp BOOT + RESET ở phần trên rồi flash lại.
- Nếu vẫn lỗi, giảm baud rate từ `460800` xuống `115200`.

### Màn hình trống hoặc board reset liên tục

Flash lại bằng factory BIN sau khi chạy `erase_flash`. Không chỉ ghi OTA BIN vì bootloader hoặc bảng phân vùng cũ có thể không tương thích.

```powershell
python -m esptool --chip esp32s3 -p COM9 -b 115200 erase_flash
python -m esptool --chip esp32s3 -p COM9 -b 115200 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_freq 80m --flash_size 16MB `
    0x0 $factoryFirmware
```

### Ứng dụng báo firmware không hợp lệ hoặc ngắt BLE

- Dùng đúng factory BIN mới nhất đi kèm source hiện tại.
- Xóa thiết bị `WazeHUD` đã lưu trong Bluetooth của điện thoại rồi kết nối lại.
- Kiểm tra ứng dụng sử dụng đúng HLP/1 và UUID BLE của SDK.
- Nếu vừa thay đổi bảng phân vùng, bắt buộc dùng factory BIN.

### Flash xong nhưng muốn dùng nguồn ngoài

Chờ esptool báo hoàn tất và hard reset, sau đó rút USB-C rồi cấp nguồn ngoài. Không cần mở monitor và không cần nhấn RESET nếu HUD đã tự khởi động.
