#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// =============================================================================
//  sensor.h -- SHT40 driver (raw I2C, non-blocking state machine)
// =============================================================================

struct SHT40Reading {
    float    temperature_c = 0.0f;
    float    humidity_pct  = 0.0f;
    bool     valid         = false;
    uint32_t timestamp_ms  = 0;
};

struct SHT40Stats {
    float    temp_min   = 1000.0f;
    float    temp_max   = -1000.0f;
    float    hum_min    = 1000.0f;
    float    hum_max    = -1000.0f;
    uint32_t read_count = 0;
};

enum class SHT40State : uint8_t { IDLE, MEASURING };

class SHT40Sensor {
public:
    // Call once in setup() AFTER Wire.begin() -- never calls Wire.begin() itself
    bool begin() {
        // Probe the device before touching it, so a missing sensor never hangs setup()
        Wire.beginTransmission(static_cast<uint8_t>(SHT40_I2C_ADDR));
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            Serial.printf("[SHT40] Not found at 0x%02X (I2C err=%u) -- disabled\n",
                          SHT40_I2C_ADDR, err);
            present_ = false;
            return false;
        }

        delay(SHT40_POWERUP_DELAY_MS);
        if (!softReset()) {
            Serial.println("[SHT40] Soft reset failed -- disabled");
            present_ = false;
            return false;
        }

        present_     = true;
        initialized_ = true;
        Serial.println("[SHT40] Sensor OK");
        return true;
    }

    // Call every loop() -- returns true only when a fresh, valid reading landed
    bool update() {
        if (!initialized_) return false;
        uint32_t now = millis();

        switch (state_) {
            case SHT40State::IDLE:
                if (now - last_read_ms_ >= SENSOR_READ_INTERVAL_MS) {
                    if (sendMeasureCommand()) {
                        measure_start_ms_ = now;
                        state_ = SHT40State::MEASURING;
                    }
                }
                break;

            case SHT40State::MEASURING:
                if (now - measure_start_ms_ >= SHT40_MEASURE_DELAY_MS) {
                    bool ok = readMeasurement();
                    last_read_ms_ = now;
                    state_ = SHT40State::IDLE;
                    return ok;
                }
                break;
        }
        return false;
    }

    bool isPresent() const { return present_; }
    const SHT40Reading& getReading() const { return reading_; }
    const SHT40Stats&   getStats()   const { return stats_; }

    // Milliseconds until this state machine next needs the CPU awake --
    // 0 means "due now". Used by main.cpp to size the light-sleep duration
    // exactly, instead of guessing with a fixed sleep length.
    uint32_t msUntilNextAction(uint32_t now) const {
        if (!initialized_) return SENSOR_READ_INTERVAL_MS; // sensor absent -- no urgency
        switch (state_) {
            case SHT40State::IDLE: {
                uint32_t elapsed = now - last_read_ms_;
                if (elapsed >= SENSOR_READ_INTERVAL_MS) return 0;
                return SENSOR_READ_INTERVAL_MS - elapsed;
            }
            case SHT40State::MEASURING: {
                uint32_t elapsed = now - measure_start_ms_;
                if (elapsed >= SHT40_MEASURE_DELAY_MS) return 0;
                return SHT40_MEASURE_DELAY_MS - elapsed;
            }
        }
        return SENSOR_READ_INTERVAL_MS; // unreachable -- all enum cases handled above
    }

private:
    bool softReset() {
        Wire.beginTransmission(static_cast<uint8_t>(SHT40_I2C_ADDR));
        Wire.write(static_cast<uint8_t>(SHT40_CMD_SOFT_RESET));
        uint8_t err = Wire.endTransmission();
        delay(SHT40_POWERUP_DELAY_MS);
        if (err != 0) {
            Serial.printf("[SHT40] Soft reset I2C err=%u\n", err);
            return false;
        }
        return true;
    }

    bool sendMeasureCommand() {
        Wire.beginTransmission(static_cast<uint8_t>(SHT40_I2C_ADDR));
        Wire.write(static_cast<uint8_t>(SHT40_CMD_MEASURE_HIGH));
        uint8_t err = Wire.endTransmission();
        if (err != 0) {
            Serial.printf("[SHT40] Measure cmd I2C err=%u\n", err);
            return false;
        }
        return true;
    }

    bool readMeasurement() {
        uint8_t buf[6] = {};
        uint8_t received = Wire.requestFrom(
            static_cast<uint8_t>(SHT40_I2C_ADDR), static_cast<uint8_t>(6));
        if (received != 6) {
            Serial.printf("[SHT40] Short read: %u bytes\n", received);
            reading_.valid = false;
            return false;
        }
        for (uint8_t i = 0; i < 6; ++i) buf[i] = static_cast<uint8_t>(Wire.read());

        if (crc8(buf, 2) != buf[2])   { Serial.println("[SHT40] CRC error T");  reading_.valid = false; return false; }
        if (crc8(buf + 3, 2) != buf[5]) { Serial.println("[SHT40] CRC error RH"); reading_.valid = false; return false; }

        uint16_t t_ticks  = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
        uint16_t rh_ticks = static_cast<uint16_t>((buf[3] << 8) | buf[4]);

        float temp_c = -45.0f + 175.0f * (static_cast<float>(t_ticks)  / 65535.0f);
        float rh_pct = -6.0f  + 125.0f * (static_cast<float>(rh_ticks) / 65535.0f);
        if (rh_pct < 0.0f)   rh_pct = 0.0f;
        if (rh_pct > 100.0f) rh_pct = 100.0f;

        reading_.temperature_c = temp_c;
        reading_.humidity_pct  = rh_pct;
        reading_.valid         = true;
        reading_.timestamp_ms  = millis();
        updateStats(temp_c, rh_pct);
        return true;
    }

    void updateStats(float temp_c, float rh_pct) {
        if (temp_c < stats_.temp_min) stats_.temp_min = temp_c;
        if (temp_c > stats_.temp_max) stats_.temp_max = temp_c;
        if (rh_pct  < stats_.hum_min) stats_.hum_min  = rh_pct;
        if (rh_pct  > stats_.hum_max) stats_.hum_max  = rh_pct;
        stats_.read_count++;
    }

    // Sensirion CRC-8: poly 0x31, init 0xFF, no reflect. crc8({0xBE,0xEF}) == 0x92
    static uint8_t crc8(const uint8_t* data, size_t len) {
        uint8_t crc = 0xFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (uint8_t b = 0; b < 8; ++b) {
                crc = (crc & 0x80)
                    ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                    : static_cast<uint8_t>(crc << 1);
            }
        }
        return crc;
    }

    SHT40State   state_             = SHT40State::IDLE;
    uint32_t     last_read_ms_      = 0;
    uint32_t     measure_start_ms_  = 0;
    bool         initialized_       = false;
    bool         present_           = false;
    SHT40Reading reading_           = {};
    SHT40Stats   stats_             = {};
};
