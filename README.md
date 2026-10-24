# SHT40 Temperature & Humidity Monitor — ESP32-C3 SuperMini + OLED

Standalone I2C monitor: SHT40 sensor + SSD1306 128x64 OLED sharing one I2C bus
on the ESP32-C3 SuperMini. No WiFi — pure local display, minimal power draw.

## Wiring

Both devices share the same I2C bus (this is fine — different I2C addresses):

| Signal | SHT40 | OLED | ESP32-C3 SuperMini |
|--------|-------|------|---------------------|
| VCC    | VCC   | VCC  | 3.3V                |
| GND    | GND   | GND  | GND                 |
| SDA    | SDA   | SDA  | GPIO8               |
| SCL    | SCL   | SCL  | GPIO9               |

Notes specific to the C3 SuperMini:
- GPIO8 is also the onboard blue LED — do not drive it as a GPIO output while I2C
  is active, or you will corrupt the bus.
- GPIO9 is also the BOOT button, held HIGH by an internal pull-up — safe for SCL,
  just don't press BOOT while the firmware is running.
- SHT40 default I2C address is `0x44` (AD pin tied to GND). OLED is `0x3C`
  (standard SSD1306 default). No address conflict.

## Battery monitor wiring (optional)

Reads 1S LiPo voltage via ADC + resistive divider, shown in the top-right corner
of the OLED as `"X.XXV NN%"` (e.g. `4.02V 81%`) — voltage and percentage together.

| Signal | Connects to |
|--------|-------------|
| VBAT (battery + / TP4056 OUT+, after the power switch) | R1 (100kΩ) |
| Junction of R1 and R2 | GPIO0 (A0 / ADC1_CH0) |
| R2 (100kΩ) | GND |

`VBAT --[R1 100k]-- GPIO0 --[R2 100k]-- GND`. Equal resistors halve the voltage
(4.2V → 2.1V), landing inside the ESP32-C3's accurate ADC range. Draws ~21µA
continuously — negligible next to the sensor/display budget. Values are in
`config.h` (`BATTERY_DIVIDER_R1_KOHM` / `_R2_KOHM`) if you use different
resistors — just keep the same ratio math in mind.

The percentage uses a piecewise LiPo discharge-curve lookup (not a straight
line), since real LiPo voltage sags fast at the very top/bottom and stays flat
through the middle.

### Calibration

Resistor tolerance (5% parts) and the ESP32-C3's ADC gain error combine into a
small systematic error between the computed voltage and the real battery
voltage. This is corrected with a single multiplicative constant,
`BATTERY_CALIBRATION_FACTOR` in `config.h` (currently `1.0448f`, verified against
two multimeter readings at different charge levels that agreed to within
0.004% — confirming the error is proportional, not an offset).

To re-calibrate for your own board/resistors:
1. **Unplug USB** (see "USB detection" below for why this matters) and let the
   board run on battery only for a minute or two.
2. Read the voltage straight off the OLED badge (no Serial needed).
3. At the same moment, measure the real battery voltage with a multimeter at
   the JST/battery pads.
4. `BATTERY_CALIBRATION_FACTOR = (multimeter reading) / (OLED reading)`.
5. Ideally repeat at a second, meaningfully different charge level to confirm
   the ratio holds across the range before trusting a single constant.

### USB detection

**With USB connected, the battery reading is not reliable.** On this board, the
divider node gets pulled toward USB VBUS (~5V) through the power-path
circuitry when USB is plugged in, so the computed voltage no longer reflects
the real battery — readings can spike close to or above the physical max a 1S
LiPo can ever reach. The firmware detects this automatically: any calibrated
reading above `BATTERY_USB_DETECT_THRESHOLD_V` (`4.35f` in `config.h`, comfortably
above a full charge's 4.20V) is treated as "USB connected", and the OLED badge
shows `"X.XXV USB"` instead of a misleading percentage. **Always calibrate, and
trust the %, with USB unplugged.**

### Critical battery ("PIN YEU!" warning)

Field testing (2026-08-19) found that on this specific board/pack, the battery
keeps running for a long time after the discharge curve's old 0% point (~3.27V)
before actually dying — voltage sagged slowly from ~3.25V down to 2.83V (OLED
went dark there) with no sign of over-discharge protection on this TP4056
board. Repeatedly discharging a 1S LiPo that deep shortens its life, so instead
of stretching the % curve down to the true empty voltage, the curve's 0% floor
was moved up to a safe **3.00V** (`BATTERY_CRITICAL_VOLTAGE_V` in `config.h`).

Below that floor, the OLED badge stops showing "0%" and instead **blinks**
between the raw voltage and `"PIN YEU!"` every 600ms
(`BATTERY_CRITICAL_BLINK_MS`) — a clearly different look from normal operation,
meant to say "recharge now" rather than implying the battery is already fully
dead. To make the blink actually visible, `main.cpp` shortens the light-sleep
duration while in this state (trading a bit of extra power draw for a display
that updates often enough to blink).

If you recalibrate this project for a different battery/board, re-run the same
full-discharge field test (watch the OLED until it goes dark, unplugged from
USB) and adjust `BATTERY_CRITICAL_VOLTAGE_V` — the safe margin needed above the
true-empty point depends on that specific pack and power-path circuit, not a
universal LiPo number.

## Power saving — light sleep

Between sensor reads, the CPU enters ESP32 light sleep instead of spinning idle. This is
safe here specifically because the project **never uses WiFi** — there's no server/radio
that needs to stay responsive, which is the usual reason light sleep is unsafe on ESP32
(it would drop incoming connections).

**Sleep duration is computed dynamically every cycle**, not fixed: `main.cpp` asks the
sensor state machine and the battery monitor "how long until you next need the CPU
awake?" and sleeps for exactly the sooner of the two answers
(`SHT40Sensor::msUntilNextAction()` / `BatteryMonitor::msUntilNextRead()`). This hits the
configured `SENSOR_READ_INTERVAL_MS` and `BATTERY_READ_INTERVAL_MS` precisely — including
the sensor's required ~10ms measure-to-read gap — instead of drifting to some multiple of
a fixed sleep length that doesn't evenly divide either interval.

Expected effect: average current drops from roughly ~25-45mA (CPU always-on) down to
roughly ~2.5mA — turning a single-charge runtime of ~6-9 hours (300mAh LiPo) into
several days.

`gpio_hold_en/dis` pins the I2C lines (SDA/SCL) at their current level for the duration
of sleep so they can't float and corrupt the bus — released immediately on wake.

**Note if debugging over USB:** light sleep may briefly interrupt the USB CDC Serial
connection each cycle (the Serial Monitor may show brief reconnects). This is expected
and harmless — it only affects the USB debug link, not sensor accuracy, display
correctness, or battery-only operation.

**Uploading new firmware:** the board stays fully awake for the first
`LIGHT_SLEEP_GRACE_MS` (15s) after every boot/reset specifically so PlatformIO's normal
USB auto-reset upload works without any manual button presses — just click Upload right
after powering on or pressing RESET. If you miss that window (light sleep has already
started), either press RESET and upload again within 15s, or force download mode
manually: hold BOOT → press RESET → release RESET → release BOOT, then upload.

## Project layout

```
sht40_c3_monitor/
├── platformio.ini        # board, libraries, USB CDC build flags
└── src/
    ├── main.cpp           # setup()/loop() wiring only
    ├── config.h            # all tuneable constants
    ├── sensor.h            # SHT40 driver: non-blocking state machine + CRC-8
    ├── battery.h           # ADC + voltage-divider LiPo percentage
    └── oled_display.h      # SSD1306 display manager
```

## How it works

- `Wire.begin(8, 9)` is called exactly once in `setup()`, before either driver
  touches the bus.
- Both `SHT40Sensor::begin()` and `OLEDDisplay::begin()` **probe** their I2C
  address first (`Wire.beginTransmission` / `endTransmission`) so a missing or
  unwired device is logged and disabled instead of hanging the board.
- The SHT40 driver never blocks: it sends the measure command, then waits via
  `millis()` for the ~8.3 ms conversion time before reading back 6 bytes
  (2x temperature, 2x humidity, 2x CRC-8). Each half is verified against the
  Sensirion CRC-8 (poly `0x31`, init `0xFF`); a failed reading is discarded and
  logged rather than displayed as garbage.
- A new reading is taken every 8 s (`SENSOR_READ_INTERVAL_MS`) — chosen to match the
  SHT40's own humidity response time (tau63% = 8s per the Sensirion datasheet); reading
  faster than the sensor's own physical settling time would just re-sample a still-lagging
  signal, not genuinely new data. The OLED redraws whenever a wake-up happens (driven by
  the sensor/battery schedule, see "Power saving" above) showing the current temperature,
  humidity, and the min/max temperature seen since boot.
- If the sensor isn't found at startup, the OLED shows a "Sensor not found"
  screen with the expected wiring instead of blank/garbage data.

## Build & flash (PlatformIO)

1. Install the [PlatformIO extension](https://platformio.org/) for VS Code, or
   the `pio` CLI.
2. Open this folder as a PlatformIO project.
3. Connect the C3 SuperMini via a **data-capable** USB-C cable (charge-only
   cables won't show up as a serial port).
4. Build & upload:
   ```
   pio run -t upload
   ```
5. **Press the RESET button after upload** — the board ends upload in download
   mode and needs a reset to boot the new firmware.
6. Open the serial monitor at 115200 baud:
   ```
   pio device monitor
   ```

## Arduino IDE alternative

- Board: `ESP32C3 Dev Module`
- Tools → USB CDC On Boot → **Enabled** (required for `Serial` output over the
  USB-C port — without it, nothing prints)
- Install libraries: **Adafruit SSD1306** and **Adafruit GFX Library** (via
  Library Manager)
- After uploading, press **RESET** to run the sketch

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| No serial output | USB CDC On Boot not enabled — see build flags / IDE setting above |
| Port not detected | Charge-only USB-C cable — use a data-capable one |
| Firmware doesn't run after upload | Press RESET — upload leaves the board in download mode |
| `[SHT40] Not found at 0x44` | Check SDA/SCL wiring and that SHT40 VCC is 3.3V |
| `[OLED] Not found at 0x3C` | Check SDA/SCL wiring; some SSD1306 boards use `0x3D` — update `OLED_I2C_ADDR` in `config.h` if so |
| `[SHT40] CRC error` in log | Noisy/long I2C wires — add 4.7kΩ pull-ups on SDA/SCL if not already on the breakout boards, keep wires short |
| Battery `%` reads wrong / stuck at 0 or 100 | Check R1/R2 wiring and values match `config.h`; verify GPIO0 isn't shared with another peripheral; also check `BATTERY_CALIBRATION_FACTOR` is calibrated for your actual resistors/board — see "Calibration" above |
| Battery badge shows `USB` instead of a percentage | Expected — USB is connected and the reading is unreliable on this board (see "USB detection" above). Unplug USB to see the real percentage |
| Battery `%` reads high/wrong specifically while USB is plugged in | Expected on this board design — see "USB detection" above. Never calibrate or trust the battery reading with USB connected |
| Battery badge blinks `PIN YEU!` | Expected — voltage dropped below the safe `BATTERY_CRITICAL_VOLTAGE_V` floor (see "Critical battery" above). Recharge; this isn't a bug |
| Serial Monitor disconnects/reconnects every few seconds | Expected with light sleep enabled (USB CDC pauses during sleep) — harmless, doesn't affect operation. Sleep length varies dynamically (sensor/battery timing), so the interval isn't perfectly regular. Only noticeable when debugging over USB |
| Upload fails: `Could not open COM/port busy` / `PermissionError` | Light sleep already started (past the 15s grace window) and is interfering with the USB auto-reset sequence. Press RESET and upload again within 15s, or force download mode: hold BOOT → press RESET → release RESET → release BOOT, then upload |
