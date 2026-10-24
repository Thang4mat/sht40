#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "sensor.h"
#include "battery.h"

// =============================================================================
//  oled_display.h -- SSD1306 128x64 display manager
// =============================================================================

class OLEDDisplay {
public:
    // Call once in setup() AFTER Wire.begin() -- never calls Wire.begin() itself
    bool begin() {
        // Probe before init -- prevents hang/crash when the OLED is not wired
        Wire.beginTransmission(static_cast<uint8_t>(OLED_I2C_ADDR));
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            Serial.printf("[OLED] Not found at 0x%02X (I2C err=%u) -- disabled\n",
                          OLED_I2C_ADDR, err);
            present_ = false;
            return false;
        }

        if (!display_.begin(SSD1306_SWITCHCAPVCC, static_cast<uint8_t>(OLED_I2C_ADDR))) {
            Serial.println("[OLED] SSD1306 init failed -- disabled");
            present_ = false;
            return false;
        }

        present_ = true;

        // Reduce brightness to ~50% -- SSD1306 SETCONTRAST takes a 0-255 value
        display_.ssd1306_command(SSD1306_SETCONTRAST);
        display_.ssd1306_command(static_cast<uint8_t>(OLED_CONTRAST));

        display_.clearDisplay();
        display_.setTextColor(SSD1306_WHITE);
        display_.setTextSize(1);
        display_.setCursor(0, 0);
        display_.println("SHT40 Monitor");
        display_.println("Waiting for sensor...");
        display_.display();
        Serial.println("[OLED] Display ready");
        return true;
    }

    bool isPresent() const { return present_; }

    // Redraw current reading + stats. Cheap enough to call every loop() --
    // Adafruit_SSD1306 only pushes the framebuffer over I2C on display().
    void update(const SHT40Reading& r, const SHT40Stats& stats, bool sensor_present,
                const BatteryReading& battery) {
        if (!present_) return;

        uint32_t now = millis();
        if (now - last_update_ms_ < OLED_REFRESH_INTERVAL_MS) return;
        last_update_ms_ = now;

        display_.clearDisplay();
        drawBatteryBadge(battery);

        if (!sensor_present) {
            display_.setCursor(0, 20);
            display_.println("Sensor not found!");
            display_.setCursor(0, 32);
            display_.println("Check wiring:");
            display_.println("SDA=GPIO8 SCL=GPIO9");
            display_.display();
            return;
        }

        if (!r.valid) {
            display_.setCursor(0, 24);
            display_.setTextSize(1);
            display_.println("Reading sensor...");
            display_.display();
            return;
        }

        char line[24];

        snprintf(line, sizeof(line), "%.2f C", static_cast<double>(r.temperature_c));
        drawCentered(line, 14, 2);

        snprintf(line, sizeof(line), "%.2f %%", static_cast<double>(r.humidity_pct));
        drawCentered(line, 34, 2);

        display_.setTextSize(1);
        display_.setCursor(0, 55);
        snprintf(line, sizeof(line), "Min/Max T:%.1f/%.1f",
                 static_cast<double>(stats.temp_min), static_cast<double>(stats.temp_max));
        display_.println(line);

        display_.display();
    }

private:
    // Shows raw voltage next to the badge (kept permanently per user preference, not
    // just for calibration). When USB is connected, the sensed node is pulled toward
    // USB VBUS and % is unreliable (see BATTERY_USB_DETECT_THRESHOLD_V in config.h) --
    // show "USB" instead of a misleading percentage.
    //
    // When critical (below the safe 0% floor -- see BATTERY_CRITICAL_VOLTAGE_V in
    // config.h), the badge blinks between the raw voltage and "PIN YEU!" every
    // BATTERY_CRITICAL_BLINK_MS, so this state reads as visibly different from a
    // normal "0%" at a glance instead of just sitting there looking the same.
    // Priority: USB > critical > normal (matches BatteryMonitor's own mutual
    // exclusion between usb_detected and critical).
    void drawBatteryBadge(const BatteryReading& battery) {
        char buf[16];
        if (battery.valid && battery.usb_detected) {
            snprintf(buf, sizeof(buf), "%.2fV USB", static_cast<double>(battery.voltage_v));
        } else if (battery.valid && battery.critical) {
            bool show_warning = (millis() / BATTERY_CRITICAL_BLINK_MS) % 2 == 0;
            if (show_warning) {
                snprintf(buf, sizeof(buf), "PIN YEU!");
            } else {
                snprintf(buf, sizeof(buf), "%.2fV", static_cast<double>(battery.voltage_v));
            }
        } else if (battery.valid) {
            snprintf(buf, sizeof(buf), "%.2fV %u%%", static_cast<double>(battery.voltage_v),
                     battery.percent);
        } else {
            snprintf(buf, sizeof(buf), "--%%");
        }
        display_.setTextSize(1);
        int16_t x1, y1;
        uint16_t w, h;
        display_.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
        display_.setCursor(static_cast<int16_t>(OLED_WIDTH - w - 2), 0);
        display_.print(buf);
    }

    void drawCentered(const char* text, int16_t y, uint8_t textSize) {
        display_.setTextSize(textSize);
        int16_t x1, y1;
        uint16_t w, h;
        display_.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
        display_.setCursor(static_cast<int16_t>((OLED_WIDTH - w) / 2), y);
        display_.print(text);
    }

    Adafruit_SSD1306 display_{OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN};
    bool     present_        = false;
    uint32_t last_update_ms_ = 0;
};
