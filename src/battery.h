#pragma once
#include <Arduino.h>
#include "config.h"

// =============================================================================
//  battery.h -- 1S LiPo voltage monitor via ADC + resistive divider
//  Divider: VBAT --[R1]-- ADC_PIN --[R2]-- GND   (ratio = (R1+R2)/R2)
// =============================================================================

struct BatteryReading {
    float   voltage_v    = 0.0f;
    uint8_t percent      = 0;
    bool    valid        = false;
    bool    usb_detected = false;  // true = reading unreliable, USB is pulling the node up
    bool    critical     = false;  // true = below the curve's 0% floor but not yet the
                                    // field-measured true-empty point -- "borrowed time",
                                    // see BATTERY_CRITICAL_VOLTAGE_V in config.h
};

// Piecewise-linear approximation of a typical 1S LiPo discharge curve.
// Far more accurate than a straight 4.2V-3.3V linear map, since LiPo voltage
// sags quickly at the top and bottom but stays flat through the middle.
//
// The 0% floor is BATTERY_CRITICAL_VOLTAGE_V (config.h), NOT the pack's true
// empty voltage (BATTERY_TRUE_EMPTY_V, field-measured at 2.83V on this board --
// see config.h for the full story). Deliberately: field testing (2026-08-19)
// found this pack + board keeps running a long time past the old generic
// curve's 0% point (3.27V) down to 2.83V before truly dying, with no visible
// over-discharge protection -- mapping % all the way down there would both (a)
// require more measured points than we have in that stretch to be trustworthy,
// and (b) implicitly encourage repeatedly discharging the cell into a range
// that shortens its life. So 0% now means "recharge now, safely", and anything
// below that floor is handled as a separate `critical` state in BatteryMonitor,
// not as more %.
struct BatteryCurvePoint { float volts; uint8_t pct; };
static const BatteryCurvePoint kBatteryCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f, 75},  {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f, 50},  {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f, 25},  {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5},
    {BATTERY_CRITICAL_VOLTAGE_V, 0},
};
static const size_t kBatteryCurveLen = sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]);

static uint8_t batteryVoltageToPercent(float v) {
    if (v >= kBatteryCurve[0].volts) return 100;
    if (v <= kBatteryCurve[kBatteryCurveLen - 1].volts) return 0;

    for (size_t i = 0; i + 1 < kBatteryCurveLen; ++i) {
        if (v <= kBatteryCurve[i].volts && v >= kBatteryCurve[i + 1].volts) {
            float span_v   = kBatteryCurve[i].volts - kBatteryCurve[i + 1].volts;
            float span_pct = static_cast<float>(kBatteryCurve[i].pct - kBatteryCurve[i + 1].pct);
            float frac     = (v - kBatteryCurve[i + 1].volts) / span_v;
            return static_cast<uint8_t>(kBatteryCurve[i + 1].pct + frac * span_pct + 0.5f);
        }
    }
    return 0; // unreachable
}

// Single-factor calibration correction for the divider+ADC chain's raw computed
// voltage -- see BATTERY_CALIBRATION_FACTOR in config.h for the two measured points
// that confirmed this error is proportional (not an offset) across the range.
static float applyBatteryCalibration(float raw_v) {
    return raw_v * BATTERY_CALIBRATION_FACTOR;
}

class BatteryMonitor {
public:
    void begin() {
        analogReadResolution(12);              // 0-4095
        analogSetPinAttenuation(static_cast<uint8_t>(BATTERY_ADC_PIN), ADC_11db);
        pinMode(BATTERY_ADC_PIN, INPUT);
        Serial.println("[Battery] ADC monitor ready");
    }

    // Call every loop() -- internally throttled, cheap enough to call unconditionally
    bool update() {
        uint32_t now = millis();
        if (now - last_read_ms_ < BATTERY_READ_INTERVAL_MS) return false;
        last_read_ms_ = now;

        // Oversample: back-to-back ADC reads are microseconds each, no delay() needed
        uint32_t sum_mv = 0;
        for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
            sum_mv += analogReadMilliVolts(BATTERY_ADC_PIN);
        }
        float adc_v = (static_cast<float>(sum_mv) / BATTERY_SAMPLE_COUNT) / 1000.0f;

        constexpr float kDividerRatio =
            (BATTERY_DIVIDER_R1_KOHM + BATTERY_DIVIDER_R2_KOHM) / BATTERY_DIVIDER_R2_KOHM;
        float raw_vbat = adc_v * kDividerRatio;
        float vbat     = applyBatteryCalibration(raw_vbat);

        reading_.voltage_v    = vbat;
        reading_.percent      = batteryVoltageToPercent(vbat);
        reading_.valid        = true;
        reading_.usb_detected = vbat > BATTERY_USB_DETECT_THRESHOLD_V;
        // Below the curve's safe 0% floor = "borrowed time" territory (see comment
        // above kBatteryCurve[]). USB-contaminated readings never qualify -- a bogus
        // ~4.9V reading is nowhere near this floor anyway, but the explicit exclusion
        // keeps the two states mutually exclusive by construction, not by accident.
        reading_.critical     = !reading_.usb_detected && vbat <= BATTERY_CRITICAL_VOLTAGE_V;

        Serial.printf("[Battery] %.2f V (%u %%)%s%s  [adc_pin=%.3f V, raw_vbat=%.3f V]\n",
                      static_cast<double>(vbat), reading_.percent,
                      reading_.usb_detected ? "  USB DETECTED, UNRELIABLE" : "",
                      reading_.critical ? "  CRITICAL - RECHARGE NOW" : "",
                      static_cast<double>(adc_v), static_cast<double>(raw_vbat));
        return true;
    }

    const BatteryReading& getReading() const { return reading_; }

    // Milliseconds until the next battery read is due -- 0 means "due now".
    // Same purpose as SHT40Sensor::msUntilNextAction().
    uint32_t msUntilNextRead(uint32_t now) const {
        uint32_t elapsed = now - last_read_ms_;
        if (elapsed >= BATTERY_READ_INTERVAL_MS) return 0;
        return BATTERY_READ_INTERVAL_MS - elapsed;
    }

private:
    uint32_t       last_read_ms_ = 0;
    BatteryReading reading_      = {};
};
