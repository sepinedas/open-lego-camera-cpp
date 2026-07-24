#pragma once

#include <cstdint>

namespace olc {

// Reads a Waveshare-style UPS HAT (built around a TI INA219 current/voltage
// monitor) over I2C and exposes a battery percentage plus a charging flag.
//
// It is designed to fail soft: when the I2C bus or the sensor isn't present --
// e.g. on a desktop dev machine, or a Pi without the HAT fitted -- open()
// returns false and every reading stays invalid, so the UI simply omits the
// battery indicator instead of showing bogus numbers.
class BatteryMonitor {
public:
    struct Reading {
        bool valid = false;    // a sensor reading was successfully obtained
        float voltage = 0.f;   // battery/bus voltage in volts
        float current = 0.f;   // current in mA (positive = charging)
        int percent = 0;       // estimated charge, 0..100
        bool charging = false; // battery is currently being charged
    };

    BatteryMonitor() = default;
    ~BatteryMonitor();

    BatteryMonitor(const BatteryMonitor&) = delete;
    BatteryMonitor& operator=(const BatteryMonitor&) = delete;

    // Open the I2C device and initialise the INA219. `bus` is the /dev/i2c-N
    // index (1 on a modern Pi); `address` is the 7-bit I2C address to try first
    // (0x43 for the Pi-Zero-sized "UPS HAT (C)", 0x42 for the full-size "UPS
    // HAT"). If nothing answers there we auto-probe the other common INA219
    // addresses (0x40..0x45), so a board wired to a different one still works.
    // Returns false -- leaving the monitor unavailable -- if the bus can't be
    // opened or no INA219 responds anywhere.
    bool open(int bus, int address);

    // True once the sensor has been opened and configured.
    bool available() const { return fd_ >= 0; }

    // Refresh the reading, but no more often than `minIntervalMs`. Between polls
    // the previous reading is returned unchanged, so this is cheap enough to call
    // once per rendered frame.
    const Reading& poll(uint32_t nowMs, uint32_t minIntervalMs = 2000);

    // The most recent reading without triggering a new I2C transaction.
    const Reading& last() const { return reading_; }

private:
    bool writeReg(uint8_t reg, uint16_t value);
    bool readReg(uint8_t reg, uint16_t& value);
    bool refresh();

    int fd_ = -1;
    Reading reading_;
    uint32_t lastPollMs_ = 0;
    bool polledOnce_ = false;
};

} // namespace olc
