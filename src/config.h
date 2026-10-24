#pragma once
// =============================================================================
//  config.h  --  SHT40 Temperature & Humidity Monitor  --  ALL tuneable constants
//  Board: ESP32-C3 SuperMini (single-core RISC-V, 160 MHz)
// =============================================================================

// --- I2C (shared bus: SHT40 + OLED) ------------------------------------------
#define I2C_SDA_PIN             8           // GPIO8 -- also onboard LED, do not use as GPIO while I2C active
#define I2C_SCL_PIN             9           // GPIO9 -- also BOOT button, internal pull-up
#define I2C_FREQ_HZ             400000UL    // 400 kHz fast-mode

// --- SHT40 sensor -------------------------------------------------------------
#define SHT40_I2C_ADDR          0x44        // AD pin -> GND; 0x45=AD->VDD; 0x46=AD->SDA
#define SHT40_CMD_MEASURE_HIGH  0xFD        // High repeatability, tMEAS max 8.3 ms
#define SHT40_CMD_SOFT_RESET    0x94
#define SHT40_POWERUP_DELAY_MS  2UL         // datasheet: max 1 ms after power-up
#define SHT40_MEASURE_DELAY_MS  10UL        // > 8.3 ms max conversion time, with margin
#define SENSOR_READ_INTERVAL_MS 8000UL      // matches SHT40's own RH response time
                                             // (tau63% = 8s per Sensirion datasheet D1 v7.1) --
                                             // reading faster just re-samples a still-settling
                                             // signal, not genuinely new information

// --- OLED (SSD1306 128x64) ----------------------------------------------------
#define OLED_I2C_ADDR           0x3C
#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#define OLED_RESET_PIN          -1          // no dedicated reset pin on SuperMini
#define OLED_REFRESH_INTERVAL_MS 500UL      // throttle full-frame redraws
#define OLED_CONTRAST            1          // SSD1306 contrast, 0-255 (1 = near-minimum brightness)

// --- Serial --------------------------------------------------------------------
#define SERIAL_BAUD_RATE        115200

// --- Battery monitor (ADC + voltage divider) ----------------------------------
#define BATTERY_ADC_PIN          0           // GPIO0 / A0 -- ADC1_CH0, unused by I2C/OLED
#define BATTERY_DIVIDER_R1_KOHM  100.0f      // top resistor: VBAT -> ADC pin
#define BATTERY_DIVIDER_R2_KOHM  100.0f      // bottom resistor: ADC pin -> GND
#define BATTERY_READ_INTERVAL_MS 5000UL      // battery changes slowly -- 5s is plenty
#define BATTERY_SAMPLE_COUNT     16          // oversample to reduce ADC noise
// (BATTERY_VOLTAGE_MAX/MIN removed 2026-08-19: dead constants, unused anywhere in
// the codebase -- the real curve floor lives in kBatteryCurve[] in battery.h /
// BATTERY_CRITICAL_VOLTAGE_V below. Keeping unused constants around invites drift,
// exactly what happened here: MIN=3.30 no longer matched the curve's real floor.)

// Calibration factor for the divider+ADC chain (R1/R2 tolerance + ADC gain error
// combined). Measured BATTERY-ONLY (USB unplugged -- USB connected corrupts the
// reading, see BATTERY_USB_DETECT_THRESHOLD_V below), multimeter probed directly
// at the battery's JST pads at the same instant the OLED voltage was read:
//   Point 1 (near full charge): OLED 4.02V, multimeter (true) 4.20V -> ratio 1.04478
//   Point 2 (mid-range, ~40%):  OLED 3.80V, multimeter (true) 3.97V -> ratio 1.04474
// The two ratios agree to within 0.004% -- the error is a clean proportional
// (multiplicative) error, not an offset, so a single constant is correct and should
// hold across the rest of the range too. factor = multimeter_v / OLED_v.
#define BATTERY_CALIBRATION_FACTOR 1.0448f

// USB-connected detection, no extra wiring needed: confirmed by measurement that
// with USB plugged in, the divider node gets pulled toward USB VBUS and the computed
// voltage reads far above what a healthy 1S LiPo can physically reach (observed
// ~4.89V vs a real 4.2V cell). A calibrated reading should never exceed the curve's
// own max point (4.20V) by more than noise, so any reading above this threshold means
// "USB connected, reading unreliable" -- not "battery overcharged". Set comfortably
// above 4.20V for margin, well below the observed ~4.89V contaminated value.
#define BATTERY_USB_DETECT_THRESHOLD_V 4.35f

// Field-tested full discharge, USB unplugged, OLED-displayed (calibrated) voltage,
// battery-only load (2026-08-19): after the curve's nominal "0%" point, the board
// kept running for a long stretch, voltage sagging slowly from ~3.25V down to
// 2.83V, until the OLED went fully dark -- that final point is the true empty
// voltage for THIS pack + circuit (no over-discharge protection observed on this
// TP4056 board -- treat this as pack/board-specific, not a universal LiPo number).
// Repeatedly running a 1S LiPo down into this range under load is bad for cell
// health, so the app does NOT map % all the way down to 2.83V. Instead:
//   - kBatteryCurve[]'s 0% floor is pulled UP to a safe voltage (see
//     BATTERY_CRITICAL_VOLTAGE_V) well above 2.83V, so 0% means "recharge now",
//     not "already almost dead".
//   - Voltage between the safe floor and true empty is flagged `critical` and
//     shown as a distinct blinking OLED warning instead of a numeric %, since we
//     only have 2 measured points in that stretch -- not enough to trust a % curve
//     there, and no reason to compute one for a range we want to avoid anyway.
#define BATTERY_CRITICAL_VOLTAGE_V 3.00f     // = kBatteryCurve[] floor; single source
                                              // of truth, must match the last entry's
                                              // .volts in battery.h
#define BATTERY_TRUE_EMPTY_V       2.83f     // documentation only, not used in logic --
                                              // field-measured true dead point (see above)
#define BATTERY_CRITICAL_BLINK_MS  600UL     // blink period for the critical-battery
                                              // OLED warning

// --- Power saving (light sleep between sensor reads) --------------------------
// Sleep duration is now computed dynamically each cycle in main.cpp -- it sleeps
// exactly until the sooner of "next sensor action due" or "next battery read due",
// so the configured intervals above (SENSOR_READ_INTERVAL_MS, BATTERY_READ_INTERVAL_MS)
// are hit precisely instead of drifting when a fixed sleep length doesn't divide evenly.
#define LIGHT_SLEEP_GRACE_MS     15000UL     // stay awake this long after boot -- lets
                                              // PlatformIO/esptool auto-reset upload work
                                              // normally without needing manual BOOT+RESET


