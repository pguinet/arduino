#include "temp_logic.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace temp {

namespace {
constexpr float kDisconnected = -127.0f;
constexpr float kPowerOn = 85.0f;
constexpr float kPowerOnTolerance = 10.0f;
constexpr float kMin = -55.0f;
constexpr float kMax = 125.0f;
}  // namespace

bool isValidReading(float celsius, bool hasPrevious, float previous) {
    if (celsius == kDisconnected) return false;
    if (!(celsius >= kMin && celsius <= kMax)) return false;  // rejette aussi NaN
    if (celsius == kPowerOn) {
        return hasPrevious && fabsf(previous - kPowerOn) <= kPowerOnTolerance;
    }
    return true;
}

int toTenths(float celsius) {
    return (int)lroundf(celsius * 10.0f);
}

void formatTenths(int tenths, char *out, size_t size) {
    const int a = abs(tenths);
    snprintf(out, size, "%s%d.%d", tenths < 0 ? "-" : "", a / 10, a % 10);
}

bool PublishPolicy::shouldPublish(int tenths, uint32_t nowMs) const {
    if (!hasPublished_) return true;
    if (abs(tenths - lastTenths_) >= minDelta_) return true;
    return (uint32_t)(nowMs - lastMs_) >= maxInterval_;
}

void PublishPolicy::markPublished(int tenths, uint32_t nowMs) {
    hasPublished_ = true;
    lastTenths_ = tenths;
    lastMs_ = nowMs;
}

bool ProbeHealth::record(bool valid) {
    const bool before = online_;
    if (valid) {
        failures_ = 0;
        online_ = true;
    } else if (failures_ < maxFailures_ && ++failures_ >= maxFailures_) {
        online_ = false;
    }
    return online_ != before;
}

}  // namespace temp
