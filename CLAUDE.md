# CLAUDE.md

> File này là bộ nhớ của dự án. Đọc toàn bộ ở đầu mỗi phiên làm việc, trước khi làm bất cứ việc gì khác.

## 0. Ngôn ngữ

- Trả lời bằng tiếng Việt.
- Giữ nguyên tiếng Anh cho: code, tên biến, tên hàm, đường dẫn file, thuật ngữ kỹ thuật (fallback, fail fast, runtime, stack trace, context window...). Không dịch.

## 1. Quy trình đầu phiên (bắt buộc)

1. Kiểm tra `CLAUDE.md` ở thư mục gốc dự án.
   - Chưa có: tạo mới, điền mục 6 (Cấu trúc dự án) và mục 7 (Tiến độ) từ những gì khảo sát được, rồi báo người dùng xác nhận.
   - Đã có: đọc và tuân theo. Nếu nội dung mâu thuẫn với code thực tế, báo người dùng, không tự đoán.
2. Xác nhận ngắn gọn (1-2 câu) phiên này sẽ làm gì, dựa trên mục 7.

## 2. Nguyên tắc thiết kế

- **Simple beats complex.** Không overengineer. Chọn giải pháp đơn giản nhất đáp ứng yêu cầu hiện tại.
- **One way, no fallbacks.** Mỗi việc có đúng một cách làm đúng. Không thêm đường thay thế, không thêm backup cho cơ chế chính. Tin cơ chế chính.
- **Fail fast.** Khi precondition không thỏa, `throw` error rõ ràng. Không nuốt lỗi, không trả về giá trị mặc định để che lỗi.
- **Clarity over compatibility.** Code rõ ràng quan trọng hơn backward compatibility. Không giữ code cũ chỉ để tương thích nếu người dùng chưa yêu cầu.
- **Separation of concerns.** Mỗi hàm chỉ có một trách nhiệm.
- **TypeScript trước runtime check.** Để type system bắt lỗi. Không thêm validation runtime thừa cho dữ liệu mà type đã đảm bảo.

## 3. Phương pháp phát triển

- **Surgical changes.** Chỉ sửa đúng chỗ cần sửa. Không refactor, đổi format, hay dọn code ngoài phạm vi yêu cầu. Thấy vấn đề khác thì báo, không tự sửa.
- **Fix root cause.** Sửa nguyên nhân gốc, không vá triệu chứng.
- **Evidence-based debugging.** Thêm logging tối thiểu, có mục tiêu. Gỡ log tạm sau khi xong.
- **Collaborative.** Khi có nhiều hướng hợp lý, nêu ngắn gọn các lựa chọn và đề xuất một hướng. Chờ người dùng chọn nếu thay đổi lớn.

## 4. Giao thức điều tra bug

Làm theo đúng thứ tự, không nhảy bước:

1. **Reproduce.** Tái hiện bug ổn định. Không tái hiện được thì chưa coi là bug; báo người dùng và xin thêm thông tin.
2. **Hypothesis.** Viết ra một giả thuyết cụ thể, kiểm chứng được, trước khi sửa code.
3. **Isolate.** Loại trừ từng biến cho đến khi chỉ còn một điểm lỗi hợp lý.
4. **Evidence.** Xác nhận bằng stack trace, log, hoặc test fail. Giả thuyết đẹp nhưng không có bằng chứng thì chưa đủ.
5. **Fix.** Chỉ sửa sau khi đã chứng minh được lỗi. Không sửa thứ chưa chứng minh là hỏng.
6. **Verify.** Chạy lại bước 1 để xác nhận bug đã hết.

## 5. Quản lý context window và "vùng ngu ngốc"

**Vùng ngu ngốc (dumb zone):** khi context window chứa quá nhiều thông tin (khoảng 200k-250k tokens), chất lượng suy luận và độ chính xác của model giảm rõ rệt. Biểu hiện: quên quy tắc trong file này, lặp lại lỗi đã sửa, bịa tên hàm hoặc đường dẫn, sửa sai chỗ.

**Claude phải chủ động cảnh báo người dùng:**

| Mức context (ước lượng) | Hành động |
|---|---|
| ~120k-150k | Nhắc nhẹ: context đang lớn, nên kết thúc tác vụ hiện tại gọn gàng. |
| ~150k-200k | Cảnh báo rõ: sắp vào vùng ngu ngốc. Cập nhật mục 7, đề xuất chạy `/compact` hoặc mở phiên mới. |
| 200k+ | Dừng nhận việc mới. Cập nhật mục 7 ngay, yêu cầu người dùng chạy `/clear` hoặc mở phiên mới. |

Quy tắc áp dụng:

- Dùng số liệu thực khi có (hiển thị usage, lệnh `/context`). Nếu không có, ước lượng dựa trên độ dài hội thoại, số file đã đọc, và output tool; nói rõ đó là ước lượng.
- Cảnh báo bằng 1-2 câu, nêu mức hiện tại và hành động đề xuất. Không giảng giải.
- Cũng cảnh báo sớm khi thấy dấu hiệu của chính mình đang suy giảm (lặp lại, mâu thuẫn với điều đã nói, quên quy tắc), dù chưa tới ngưỡng.
- Trước khi người dùng dọn context, luôn cập nhật mục 7 để phiên sau tiếp tục được mà không mất thông tin.
- Giữ context sạch: không đọc cả file lớn khi chỉ cần một đoạn, không dán lại output dài không cần thiết.

## 6. Cấu trúc dự án

**Dự án:** SHT40 Temperature and Humidity Monitor trên ESP32-C3 SuperMini, hiển thị trên OLED, kèm đo pin 1S LiPo. C++, framework Arduino-ESP32, build bằng PlatformIO. Không dùng WiFi.

**Phần cứng:**

| Thiết bị | VCC | GND | SDA | SCL |
|---|---|---|---|---|
| SHT40 (Sensirion, `0x44`) | 3.3V | GND | GPIO8 | GPIO9 |
| OLED SSD1306 128x64 (`0x3C`) | 3.3V | GND | GPIO8 | GPIO9 |

- Chung một bus I2C, khác địa chỉ. Thư viện OLED: Adafruit SSD1306 + Adafruit GFX.
- GPIO8 cũng là LED trên board, GPIO9 cũng là nút BOOT. Không dùng LED_PIN/BOOT_PIN khi I2C đang chạy.
- Pin: `VBAT --[100k]-- GPIO0 --[100k]-- GND`. Khi cắm USB, node bị kéo về VBUS nên đọc pin không đáng tin (firmware tự nhận diện, hiện "USB").

**Cây thư mục (thực tế, header-only):**

```
platformio.ini         board esp32-c3-devkitm-1, platform espressif32@7.0.1, USB CDC on boot
README.md              wiring, calibration, light sleep, troubleshooting
src/main.cpp           setup()/loop(), điều phối + light sleep
src/config.h           mọi hằng số (pin, địa chỉ I2C, interval, calibration, ngưỡng pin)
src/sensor.h           SHT40Sensor: raw Wire, state machine không chặn, CRC-8
src/oled_display.h     OLEDDisplay: vẽ dữ liệu được truyền vào + badge pin
src/battery.h          BatteryMonitor: ADC + divider, đường cong LiPo, USB detect, critical
lib/ include/ test/    README mặc định của PlatformIO, chưa dùng
```

**Lệnh:**

- Build: `pio run`
- Nạp: `pio run -t upload` (sau nạp phải nhấn RESET)
- Monitor: `pio device monitor`
- Test/lint: chưa có.

**Quy ước:**

- Log: `Serial.printf("[Tag] message\n")`, mỗi module một tag (`[Setup]`, `[Sensor]`, `[SHT40]`, `[OLED]`, `[Battery]`).
- `Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN)` gọi đúng một lần trong `setup()`. Driver không tự gọi `Wire.begin()`. `sensor.h` dùng `Wire` global, `OLEDDisplay` truyền `&Wire` cho Adafruit.
- Thiếu thiết bị: thiếu OLED thì log và chạy tiếp; thiếu SHT40 thì fail fast (dừng hẳn trong `setup()`).
- Không `delay()` chờ cảm biến: gửi lệnh, ghi `millis()`, đọc sau `SHT40_MEASURE_DELAY_MS` (10 ms). Chỉ `delay()` ngắn lúc `begin()`.
- Kiểm tra CRC-8 (đa thức `0x31`, init `0xFF`) cho mọi lần đọc SHT40.
- `SENSOR_READ_INTERVAL_MS` = `8000UL` (bám τ63% độ ẩm của SHT40 là 8 s).
- `loop()` light sleep theo `msUntilNextAction()` / `msUntilNextRead()`; giữ thức `LIGHT_SLEEP_GRACE_MS` (15 s) sau boot để upload. Khi pin critical, giới hạn sleep còn nửa chu kỳ blink.
- Hằng số millis có hậu tố `UL`, hằng float có hậu tố `f`, cast dùng `static_cast`.

## 7. Tiến độ

- **Đã xong:** toàn bộ code theo cây thư mục ở mục 6: driver SHT40, OLED, đo pin (calibration `1.0448f`, USB detect `4.35f`, critical `3.00f` với cảnh báo blink "PIN YEU!"), light sleep động. README ghi đã hiệu chuẩn và field test xả pin trên board (2026-08-19). Chưa có test tự động. Claude chưa tự build hay chạy trên board.
- **Đang làm:** Đã xuất 3 file sửa (`src/main.cpp`: dừng hẳn khi thiếu SHT40; `src/oled_display.h`: sửa comment brightness theo `OLED_CONTRAST`; `README.md`: thêm mục "Missing devices", cập nhật troubleshooting). Chờ người dùng chép đè vào repo và chạy `pio run`. Claude chưa build được.
- **Việc tiếp theo:** Do người dùng quyết định. Sau khi build và thử trên board (rút SHT40 để kiểm tra nhánh halt), xóa mục này.
- **Quyết định quan trọng và lý do:**
  - Dùng raw `Wire`, không dùng thư viện C của Sensirion: ít file, ít patch. Đã áp dụng trong `sensor.h`.
  - Thiếu OLED: board vẫn chạy. Thiếu SHT40: fail fast, log lỗi rồi dừng trong `setup()`, vòng dừng vẫn gọi `oled.update()` để OLED hiện "Sensor not found". Board dừng ở trạng thái thức (không light sleep) tới khi RESET.
  - 0% pin = `BATTERY_CRITICAL_VOLTAGE_V` (3.00V), không phải điểm chết thật (2.83V), để tránh xả sâu làm hại cell. Dưới ngưỡng này hiện trạng thái critical thay vì %.
  - `SENSOR_READ_INTERVAL_MS` = 8 s theo τ63% độ ẩm, ưu tiên hơn lập luận tiết kiệm pin.
- **Vấn đề còn mở:**
  - Có cần pull-up ngoài cho SDA/SCL không (module thường đã có sẵn)?

## 8. Quy tắc cập nhật file này

- Cập nhật mục 6 và 7 ngay khi có thay đổi đáng kể, không dồn đến cuối phiên.
- Chỉ ghi thông tin sẽ còn đúng và hữu ích cho phiên sau. Xóa mục đã lỗi thời thay vì chồng thêm.
- Mục 2-5 chỉ sửa khi người dùng yêu cầu.
- Giữ file dưới ~200 dòng để tiết kiệm context mỗi lần đọc.
