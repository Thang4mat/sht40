#include <Arduino.h>
#include <Wire.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "config.h"
#include "sensor.h"
#include "oled_display.h"
#include "battery.h"

// =============================================================================
//  main.cpp -- SHT40 Temperature & Humidity Monitor (ESP32-C3 SuperMini + OLED)
//  main.cpp only wires objects together -- all logic lives in sensor.h / oled_display.h
// =============================================================================

static SHT40Sensor    sht40;
static OLEDDisplay    oled;
static BatteryMonitor battery;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(200);
    Serial.println("[Setup] SHT40 + OLED Monitor starting");

    // Wire.begin() called exactly once, before any driver touches the bus.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);

    bool oled_ok  = oled.begin();   // never calls Wire.begin() internally
    bool sht40_ok = sht40.begin();  // never calls Wire.begin() internally
    battery.begin();                // ADC, independent of I2C bus

    Serial.printf("[Setup] OLED present=%d, SHT40 present=%d\n",
                  static_cast<int>(oled_ok), static_cast<int>(sht40_ok));
    Serial.println("[Setup] Ready");
}

void loop() {
    bool fresh = sht40.update();
    battery.update(); // internally throttled to BATTERY_READ_INTERVAL_MS

    if (fresh) {
        const SHT40Reading& r = sht40.getReading();
        Serial.printf("[Sensor] T=%.2f C  RH=%.2f %%\n",
                      static_cast<double>(r.temperature_c),
                      static_cast<double>(r.humidity_pct));
    }

    // Refresh the OLED every loop iteration -- cheap, and keeps min/max stats current
    oled.update(sht40.getReading(), sht40.getStats(), sht40.isPresent(), battery.getReading());

    // Light sleep between wake-ups -- this project never uses WiFi, so sleep can run
    // unconditionally every cycle (no server/radio to keep responsive).
    // Skip sleep entirely for the first LIGHT_SLEEP_GRACE_MS after boot: light sleep
    // interferes with PlatformIO/esptool's USB auto-reset upload sequence (chip may be
    // asleep exactly when the reset signal arrives, and USB CDC cycling on/off confuses
    // the OS's COM port driver). Staying awake at boot gives a reliable window to
    // upload normally, without needing to hold BOOT+RESET manually every time.
    if (millis() < LIGHT_SLEEP_GRACE_MS) {
        return;
    }

    // Sleep exactly until the sooner of "sensor's next state-machine action" or
    // "next battery read" -- not a fixed guess. This guarantees the configured
    // SENSOR_READ_INTERVAL_MS / BATTERY_READ_INTERVAL_MS are hit precisely, including
    // the sensor's 10ms measure-to-read gap, instead of drifting to multiples of a
    // fixed sleep length that doesn't evenly divide either interval.
    uint32_t now          = millis();
    uint32_t sensor_wait  = sht40.msUntilNextAction(now);
    uint32_t battery_wait = battery.msUntilNextRead(now);
    uint32_t sleep_ms     = (sensor_wait < battery_wait) ? sensor_wait : battery_wait;

    // Battery critical: the OLED badge blinks every BATTERY_CRITICAL_BLINK_MS (see
    // oled_display.h), but that only works if loop() actually wakes up often enough
    // to redraw it. Normal sleep can run up to BATTERY_READ_INTERVAL_MS (5s), far
    // too coarse for a 600ms blink to be visible -- so cap sleep to half the blink
    // period in this state. Slightly more power draw right when the pack is nearly
    // empty, but the point here is a clearly visible warning, not maximum runtime.
    if (battery.getReading().critical) {
        uint32_t blink_cap = BATTERY_CRITICAL_BLINK_MS / 2;
        if (sleep_ms > blink_cap) sleep_ms = blink_cap;
    }

    if (sleep_ms == 0) {
        return; // something is due right now -- loop() re-enters immediately, no sleep needed
    }

    // gpio_hold_en/dis pins the I2C lines at their current level during sleep so they
    // can't float and corrupt the bus; released immediately on wake.
    gpio_hold_en(static_cast<gpio_num_t>(I2C_SDA_PIN));
    gpio_hold_en(static_cast<gpio_num_t>(I2C_SCL_PIN));
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sleep_ms) * 1000ULL);
    esp_light_sleep_start();
    gpio_hold_dis(static_cast<gpio_num_t>(I2C_SDA_PIN));
    gpio_hold_dis(static_cast<gpio_num_t>(I2C_SCL_PIN));
}

