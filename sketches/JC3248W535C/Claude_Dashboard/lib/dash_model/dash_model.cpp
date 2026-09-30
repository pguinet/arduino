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
// depuis sa reception. Un vieux message (retained ou retarde) d'un agent muet
// depuis longtemps est ainsi perime des sa reception. Borne a 0 (horloge
// decalee ou non synchronisee).
static int64_t snapshotAge(const HostSnapshot &snap, int64_t receivedAt, int64_t now) {
    int64_t age = now - (snap.ts > 0 ? snap.ts : receivedAt);
    return age < 0 ? 0 : age;
}

static bool sameLimits(const Limits &a, const Limits &b) {
    return a.h5 == b.h5 && a.h5Reset == b.h5Reset && a.d7 == b.d7 && a.d7Reset == b.d7Reset;
}

static bool sameLimits(const Limits *a, const Limits *b) {
    if (!a || !b) return a == b;
    return sameLimits(*a, *b);
}

static bool sameSession(const Session &a, const Session &b) {
    return strcmp(a.id, b.id) == 0 && strcmp(a.project, b.project) == 0 &&
           strcmp(a.model, b.model) == 0 && strcmp(a.tool, b.tool) == 0 && a.state == b.state &&
           a.since == b.since && a.ctx == b.ctx;
}

// Compare le contenu affiche de deux snapshots du meme hote (ts ignore).
static bool sameContent(const HostSnapshot &a, const HostSnapshot &b) {
    if (a.count != b.count || !sameLimits(a.limits, b.limits)) return false;
    for (int i = 0; i < a.count; i++)
        if (!sameSession(a.sessions[i], b.sessions[i])) return false;
    return true;
}

Alert Dashboard::apply(const HostSnapshot &snap, int64_t now, bool *changed) {
    if (changed) *changed = false;
    int idx = -1;
    for (int i = 0; i < count_; i++)
        if (strcmp(hosts_[i].snap.host, snap.host) == 0) {
            idx = i;
            break;
        }

    Alert alert = Alert::None;
    bool isNew = idx < 0;
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
    } else if (count_ < MAX_HOSTS) {
        idx = count_++;
    } else {
        // Tableau plein : reprendre le slot de l'hote muet depuis le plus
        // longtemps, s'il l'est depuis plus de FORGET_AFTER_S.
        int64_t oldest = FORGET_AFTER_S;
        for (int i = 0; i < count_; i++) {
            int64_t age = snapshotAge(hosts_[i].snap, hosts_[i].receivedAt, now);
            if (age > oldest) {
                oldest = age;
                idx = i;
            }
        }
        if (idx < 0) return Alert::None;  // hote ignore
    }

    bool diff = isNew || !sameContent(hosts_[idx].snap, snap);
    Limits shownBefore;
    const Limits *lb = limits();
    if (lb) shownBefore = *lb;

    hosts_[idx].snap = snap;
    hosts_[idx].receivedAt = now;

    if (changed) *changed = diff || !sameLimits(lb ? &shownBefore : nullptr, limits());
    return alert;
}

bool Dashboard::removeHost(const char *host) {
    for (int i = 0; i < count_; i++) {
        if (strcmp(hosts_[i].snap.host, host) != 0) continue;
        for (int j = i + 1; j < count_; j++) hosts_[j - 1] = hosts_[j];
        count_--;
        return true;
    }
    return false;
}

int Dashboard::expire(int64_t now) {
    int kept = 0;
    for (int i = 0; i < count_; i++) {
        if (snapshotAge(hosts_[i].snap, hosts_[i].receivedAt, now) > FORGET_AFTER_S) continue;
        if (kept != i) hosts_[kept] = hosts_[i];
        kept++;
    }
    int removed = count_ - kept;
    count_ = kept;
    return removed;
}

int Dashboard::rows(Row *out, int max) const {
    int n = 0;
    for (int h = 0; h < count_; h++)
        for (int i = 0; i < hosts_[h].snap.count && n < max; i++)
            out[n++] = {&hosts_[h].snap.sessions[i], hosts_[h].snap.host};

    // Ordre strict : a egalite, l'ordre d'origine est conserve (tri stable).
    // since = 0 (inconnu) passe en tete de son groupe d'urgence.
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

int Dashboard::sessionCount() const {
    int n = 0;
    for (int h = 0; h < count_; h++) n += hosts_[h].snap.count;
    return n;
}

// --- Helpers d'affichage et veille ---

// Au-dela de 999 jours (since aberrant : 0, horloge non synchronisee), on
// affiche "999j+" : borne aussi les casts en int.
constexpr int64_t MAX_DAYS = 999;

void formatDuration(int64_t s, char *out, size_t size) {
    if (size == 0) return;
    if (s < 0) s = 0;
    if (s < 3600)
        snprintf(out, size, "%d:%02d", (int)(s / 60), (int)(s % 60));
    else if (s < 86400)
        snprintf(out, size, "%dh%02d", (int)(s / 3600), (int)(s % 3600 / 60));
    else if (s / 86400 <= MAX_DAYS)
        snprintf(out, size, "%dj", (int)(s / 86400));
    else
        snprintf(out, size, "%dj+", (int)MAX_DAYS);
}

// Memes seuils que la statusline du terminal : vert < 60, orange < 85, rouge.
uint32_t colorForPercent(int pct) {
    if (pct < 60) return 0x4caf50;
    if (pct < 85) return 0xfca311;
    return 0xf72585;
}

const char *stateLabel(State s) {
    switch (s) {
        case State::Permission: return "PERMISSION";
        case State::Idle: return "ATTENTE";
        default: return "TRAVAILLE";
    }
}

// Difference en uint32_t : correcte au debordement de millis() (~49 j).
bool screenShouldBeOn(bool hasSessions, uint32_t nowMs, uint32_t lastActivityMs,
                      uint32_t timeoutMs) {
    return hasSessions || (uint32_t)(nowMs - lastActivityMs) < timeoutMs;
}

}  // namespace dash
