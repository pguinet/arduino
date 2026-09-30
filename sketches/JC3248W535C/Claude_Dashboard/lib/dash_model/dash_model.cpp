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

    // Remise a zero en place (pas de HostSnapshot temporaire de ~1.3 Ko sur
    // la pile du callback MQTT) : count borne les lectures et chaque slot
    // utilise est entierement reecrit.
    copyStr(out.host, sizeof out.host, host);
    out.ts = parseEpoch(doc["ts"]);
    out.limits = Limits();
    out.count = 0;

    JsonObjectConst lim = doc["limits"];
    if (!lim.isNull()) {
        out.limits.h5 = parsePercent(lim["h5"]);
        out.limits.h5Reset = parseEpoch(lim["h5_reset"]);
        out.limits.d7 = parsePercent(lim["d7"]);
        out.limits.d7Reset = parseEpoch(lim["d7_reset"]);
    }

    for (JsonVariantConst v : doc["sessions"].as<JsonArrayConst>()) {
        if (out.count >= MAX_SESSIONS) break;
        JsonObjectConst js = v.as<JsonObjectConst>();
        const char *id = js["id"];
        if (!id || !*id) continue;  // pas un objet, ou id absent / vide
        Session &s = out.sessions[out.count++];
        copyStr(s.id, sizeof s.id, id);
        copyStr(s.project, sizeof s.project, js["project"]);
        copyStr(s.model, sizeof s.model, js["model"]);
        copyStr(s.tool, sizeof s.tool, js["tool"]);
        s.state = parseState(js["state"]);
        s.since = parseEpoch(js["since"]);
        s.ctx = parsePercent(js["ctx"]);
    }
    return true;
}

// --- Dashboard : agregation multi-hote, tri, transitions, staleness ---

static int urgency(State s) {
    switch (s) {
        case State::Permission: return 0;
        case State::Idle: return 1;
        default: return 2;
    }
}

static const Session *findSession(const HostSnapshot &snap, const char *id) {
    for (int i = 0; i < snap.count; i++)
        if (strcmp(snap.sessions[i].id, id) == 0) return &snap.sessions[i];
    return nullptr;
}

// Age d'un snapshot : depuis son ts (heure du serveur) s'il est connu, sinon
// depuis sa reception. Un message retained d'un agent mort depuis longtemps
// est ainsi perime des sa reception. Borne a 0 (horloge decalee ou non
// synchronisee).
static int64_t snapshotAge(const HostSnapshot &snap, int64_t receivedAt, int64_t now) {
    int64_t age = now - (snap.ts > 0 ? snap.ts : receivedAt);
    return age < 0 ? 0 : age;
}

Alert Dashboard::apply(const HostSnapshot &snap, int64_t now) {
    int idx = -1;
    for (int i = 0; i < count_; i++)
        if (strcmp(hosts_[i].snap.host, snap.host) == 0) idx = i;

    Alert alert = Alert::None;
    if (idx >= 0) {
        const HostSnapshot &prev = hosts_[idx].snap;
        // Snapshot precedent deja perime : ses etats n'etaient plus fiables,
        // pas de salve de bips au retour du serveur.
        bool prevStale = snapshotAge(prev, hosts_[idx].receivedAt, now) > STALE_AFTER_S;
        for (int i = 0; i < snap.count && !prevStale; i++) {
            const Session &cur = snap.sessions[i];
            const Session *old = findSession(prev, cur.id);
            if (!old || old->state == cur.state) continue;
            if (cur.state == State::Permission) alert = Alert::Permission;
            else if (cur.state == State::Idle && alert == Alert::None) alert = Alert::Idle;
        }
    } else {
        if (count_ >= MAX_HOSTS) return Alert::None;  // hote ignore
        idx = count_++;
    }
    hosts_[idx].snap = snap;
    hosts_[idx].receivedAt = now;
    return alert;
}

int Dashboard::rows(Row *out, int max) const {
    int n = 0;
    for (int h = 0; h < count_; h++)
        for (int i = 0; i < hosts_[h].snap.count && n < max; i++)
            out[n++] = {&hosts_[h].snap.sessions[i], hosts_[h].snap.host};

    // Ordre strict : a egalite, l'ordre d'origine est conserve (tri stable).
    auto before = [](const Row &a, const Row &b) {
        int ua = urgency(a.session->state), ub = urgency(b.session->state);
        return ua != ub ? ua < ub : a.session->since < b.session->since;
    };
    // Tri par insertion (n <= MAX_ROWS = 48)
    for (int i = 1; i < n; i++) {
        Row r = out[i];
        int j = i - 1;
        while (j >= 0 && before(r, out[j])) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = r;
    }
    return n;
}

const Limits *Dashboard::limits() const {
    const HostSnapshot *best = nullptr;
    for (int i = 0; i < count_; i++) {
        const HostSnapshot &s = hosts_[i].snap;
        if (s.limits.h5 < 0 && s.limits.d7 < 0) continue;
        if (!best || s.ts > best->ts) best = &s;
    }
    return best ? &best->limits : nullptr;
}

int64_t Dashboard::staleSeconds(int64_t now) const {
    int64_t worst = 0;
    for (int i = 0; i < count_; i++) {
        int64_t age = snapshotAge(hosts_[i].snap, hosts_[i].receivedAt, now);
        if (age > worst) worst = age;
    }
    return worst;
}

bool Dashboard::anyWaiting() const {
    for (int h = 0; h < count_; h++)
        for (int i = 0; i < hosts_[h].snap.count; i++)
            if (hosts_[h].snap.sessions[i].state != State::Working) return true;
    return false;
}

// --- Stubs : implementes par la Task 11 (helpers) ---

void formatDuration(int64_t, char *out, size_t size) {
    if (size) out[0] = '\0';
}

uint32_t colorForPercent(int) { return 0; }

const char *stateLabel(State) { return ""; }

bool screenShouldBeOn(bool, uint32_t, uint32_t, uint32_t) { return true; }

}  // namespace dash
