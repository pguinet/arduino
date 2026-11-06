#include "outdoor_logic.h"

#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace outdoor {

namespace {
// Plage plausible pour une temperature exterieure en Ile-de-France, avec
// une large marge : au-dela, c'est un message corrompu.
const float MIN_CELSIUS = -60.0f;
const float MAX_CELSIUS = 80.0f;

bool equalsText(const char *payload, size_t length, const char *text) {
    return length == strlen(text) && memcmp(payload, text, length) == 0;
}
}  // namespace

bool parseTemperature(const char *payload, size_t length, float &celsius) {
    JsonDocument filter;
    filter["temperature"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, payload, length, DeserializationOption::Filter(filter))) {
        return false;
    }
    JsonVariant value = doc["temperature"];
    // is<float>() accepte aussi les entiers JSON, refuse null et les chaines.
    if (!value.is<float>()) {
        return false;
    }
    const float t = value.as<float>();
    if (isnan(t) || t < MIN_CELSIUS || t > MAX_CELSIUS) {
        return false;
    }
    celsius = t;
    return true;
}

bool parseAvailability(const char *payload, size_t length, bool &online) {
    if (equalsText(payload, length, "online")) {
        online = true;
        return true;
    }
    if (equalsText(payload, length, "offline")) {
        online = false;
        return true;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload, length)) {
        return false;
    }
    const char *state = doc["state"] | "";
    if (strcmp(state, "online") == 0) {
        online = true;
        return true;
    }
    if (strcmp(state, "offline") == 0) {
        online = false;
        return true;
    }
    return false;
}

int toTenths(float celsius) {
    return static_cast<int>(lround(static_cast<double>(celsius) * 10.0));
}

void formatTenths(int tenths, char *out, size_t size) {
    const bool negative = tenths < 0;
    const int magnitude = negative ? -tenths : tenths;
    snprintf(out, size, "%s%d.%d", negative ? "-" : "", magnitude / 10, magnitude % 10);
}

void formatAge(uint32_t ageMs, char *out, size_t size) {
    const uint32_t minutes = ageMs / 60000UL;
    if (minutes == 0) {
        snprintf(out, size, "a l'instant");
    } else if (minutes < 60) {
        snprintf(out, size, "il y a %lu min", static_cast<unsigned long>(minutes));
    } else if (minutes < 24 * 60) {
        snprintf(out, size, "il y a %lu h %02lu", static_cast<unsigned long>(minutes / 60),
                 static_cast<unsigned long>(minutes % 60));
    } else {
        snprintf(out, size, "il y a %lu j", static_cast<unsigned long>(minutes / (24 * 60)));
    }
}

void SensorState::onSubscribed(uint32_t nowMs) {
    subscribed_ = true;
    subscribedMs_ = nowMs;
}

void SensorState::onTemperature(int tenths, uint32_t nowMs) {
    const bool retained = subscribed_ && (nowMs - subscribedMs_) <= retainedWindowMs_;
    if (retained && hasValue_ && tenths == tenths_) {
        // Le broker renvoie la valeur deja connue (reconnexion) : on garde
        // sa date de reception d'origine.
        return;
    }
    hasValue_ = true;
    tenths_ = tenths;
    ageKnown_ = !retained;
    receivedMs_ = nowMs;
}

Status SensorState::status(uint32_t nowMs) const {
    if (!hasValue_) {
        return Status::NoData;
    }
    if (!online_) {
        return Status::Offline;
    }
    if (ageMs(nowMs) > staleMs_) {
        return Status::Stale;
    }
    return ageKnown_ ? Status::Fresh : Status::AgeUnknown;
}

}  // namespace outdoor
