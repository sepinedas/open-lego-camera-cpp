#include "battery.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

namespace olc {

namespace {
// INA219 register map (only the ones we touch).
constexpr uint8_t kRegConfig = 0x00;
constexpr uint8_t kRegBusVoltage = 0x02;
constexpr uint8_t kRegCurrent = 0x04;
constexpr uint8_t kRegCalibration = 0x05;

// Matches Waveshare's set_calibration_32V_2A used by their UPS HAT samples:
// 32 V bus range, /8 PGA (320 mV shunt), 12-bit ADCs, continuous shunt+bus.
constexpr uint16_t kConfig = 0x2000 | 0x1800 | 0x0180 | 0x0018 | 0x0007;
constexpr uint16_t kCalibration = 4096;

// Current LSB that pairs with the calibration above: 0.1 mA per bit.
constexpr float kCurrentLsbMa = 0.1f;

// A single Li-ion cell runs ~4.2 V full down to ~3.0 V empty; map that span
// onto 0..100 %. This is the same simple model Waveshare's demo uses.
constexpr float kEmptyVolts = 3.0f;
constexpr float kFullVolts = 4.2f;

// Treat more than this much current flowing into the pack as "charging",
// so a little sensor noise around zero doesn't flip the icon.
constexpr float kChargeThresholdMa = 30.0f;
} // namespace

BatteryMonitor::~BatteryMonitor() {
    if (fd_ >= 0) ::close(fd_);
}

// INA219 registers are 16-bit, big-endian: [MSB, LSB].
bool BatteryMonitor::writeReg(uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return ::write(fd_, buf, sizeof(buf)) == (ssize_t)sizeof(buf);
}

bool BatteryMonitor::readReg(uint8_t reg, uint16_t& value) {
    if (::write(fd_, &reg, 1) != 1) return false;
    uint8_t buf[2] = {0, 0};
    if (::read(fd_, buf, 2) != 2) return false;
    value = (uint16_t)((buf[0] << 8) | buf[1]);
    return true;
}

bool BatteryMonitor::open(int bus, int address) {
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    fd_ = ::open(path, O_RDWR);
    if (fd_ < 0) {
        std::cerr << "battery: cannot open " << path << " (" << std::strerror(errno)
                  << "); indicator disabled. Enable I2C (raspi-config -> Interface "
                     "Options -> I2C) and add your user to the `i2c` group.\n";
        return false;
    }

    // Try the requested address first, then the other common INA219 addresses so
    // a board strapped to a different one still lights up. INA219 has no device-ID
    // register, so "responds" means: point at the config register, program it,
    // read it back, and require an ACK on every step.
    int candidates[] = {address, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45};
    for (int addr : candidates) {
        if (::ioctl(fd_, I2C_SLAVE, addr) < 0) continue;
        uint16_t probe = 0;
        if (writeReg(kRegCalibration, kCalibration) && writeReg(kRegConfig, kConfig) &&
            readReg(kRegConfig, probe) && probe == kConfig) {
            std::cout << "battery: INA219 ready on " << path << " at 0x" << std::hex
                      << addr << std::dec << "\n";
            return true;
        }
    }

    std::cerr << "battery: no INA219 responded on " << path << " (tried 0x"
              << std::hex << address
              << " then 0x40-0x45); indicator disabled. Check wiring and run "
                 "`i2cdetect -y " << std::dec << bus << "`.\n";
    ::close(fd_);
    fd_ = -1;
    return false;
}

bool BatteryMonitor::refresh() {
    uint16_t rawBus = 0, rawCurrent = 0;
    if (!readReg(kRegBusVoltage, rawBus) || !readReg(kRegCurrent, rawCurrent))
        return false;

    // Bus voltage lives in bits [15:3]; each step is 4 mV.
    float volts = (float)(rawBus >> 3) * 0.004f;
    // Current is signed; positive means current flowing into the battery.
    float milliAmps = (float)(int16_t)rawCurrent * kCurrentLsbMa;

    int pct = (int)((volts - kEmptyVolts) / (kFullVolts - kEmptyVolts) * 100.f + 0.5f);
    pct = std::max(0, std::min(100, pct));

    reading_.valid = true;
    reading_.voltage = volts;
    reading_.current = milliAmps;
    reading_.percent = pct;
    reading_.charging = milliAmps > kChargeThresholdMa;
    return true;
}

const BatteryMonitor::Reading& BatteryMonitor::poll(uint32_t nowMs,
                                                    uint32_t minIntervalMs) {
    if (fd_ < 0) return reading_;
    if (!polledOnce_ || nowMs - lastPollMs_ >= minIntervalMs) {
        polledOnce_ = true;
        lastPollMs_ = nowMs;
        if (!refresh()) reading_.valid = false; // keep last numbers but mark stale
    }
    return reading_;
}

} // namespace olc
