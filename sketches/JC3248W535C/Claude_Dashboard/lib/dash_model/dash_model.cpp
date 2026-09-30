#include "dash_model.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

namespace dash {

// Copie tronquee ; nullptr (absent ou pas une chaine) -> ""
static void copyStr(char *dst, size_t size, const char *src) {
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static State parseState(const char *s) {
    if (s && strcmp(s, "working") == 0) return State::Working;
    if (s && strcmp(s, "permission") == 0) return State::Permission;
    return State::Idle;
}

// Pourcentage 0..100 ; absent, pas un entier ou negatif -> -1
static int parsePercent(JsonVariantConst v) {
    if (!v.is<int>()) return -1;
    int p = v.as<int>();
    if (p < 0) return -1;
    return p > 100 ? 100 : p;
}

// Epoch en secondes ; absent, pas un entier ou negatif -> 0
static int64_t parseEpoch(JsonVariantConst v) {
    int64_t t = v | (int64_t)0;
    return t < 0 ? 0 : t;
}

bool parseSnapshot(const char *json, size_t len, HostSnapshot &out) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) return false;
    const char *host = doc["host"];
    if (!host || !*host) return false;

    out = HostSnapshot();
    copyStr(out.host, sizeof out.host, host);
    out.ts = parseEpoch(doc["ts"]);

    JsonObjectConst lim = doc["limits"];
    if (!lim.isNull()) {
        out.limits.h5 = parsePercent(lim["h5"]);
        out.limits.h5Reset = parseEpoch(lim["h5_reset"]);
        out.limits.d7 = parsePercent(lim["d7"]);
        out.limits.d7Reset = parseEpoch(lim["d7_reset"]);
    }

    for (JsonObjectConst js : doc["sessions"].as<JsonArrayConst>()) {
        if (out.count >= MAX_SESSIONS) break;
        Session &s = out.sessions[out.count++];
        copyStr(s.id, sizeof s.id, js["id"]);
        copyStr(s.project, sizeof s.project, js["project"]);
        copyStr(s.model, sizeof s.model, js["model"]);
        copyStr(s.tool, sizeof s.tool, js["tool"]);
        s.state = parseState(js["state"]);
        s.since = parseEpoch(js["since"]);
        s.ctx = parsePercent(js["ctx"]);
    }
    return true;
}

// --- Stubs : implementes par les Tasks 10 (Dashboard) et 11 (helpers) ---

Alert Dashboard::apply(const HostSnapshot &, int64_t) { return Alert::None; }

int Dashboard::rows(Row *, int) const { return 0; }

const Limits *Dashboard::limits() const { return nullptr; }

int64_t Dashboard::staleSeconds(int64_t) const { return 0; }

bool Dashboard::anyWaiting() const { return false; }

void formatDuration(int64_t, char *out, size_t size) {
    if (size) out[0] = '\0';
}

uint32_t colorForPercent(int) { return 0; }

const char *stateLabel(State) { return ""; }

bool screenShouldBeOn(bool, uint32_t, uint32_t, uint32_t) { return true; }

}  // namespace dash
