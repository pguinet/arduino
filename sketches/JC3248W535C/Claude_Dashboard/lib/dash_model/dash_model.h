// Modele pur du tableau de bord (sans Arduino ni LVGL) : testable en natif.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dash {

constexpr int MAX_SESSIONS = 12;
constexpr int MAX_HOSTS = 4;
constexpr int MAX_ROWS = MAX_SESSIONS * MAX_HOSTS;
constexpr int64_t STALE_AFTER_S = 180;
// Hote muet depuis plus longtemps : son slot peut etre repris par un nouvel hote.
constexpr int64_t EVICT_AFTER_S = 6 * 3600;

enum class State : uint8_t { Working, Idle, Permission };
enum class Alert : uint8_t { None, Idle, Permission };

struct Limits {
    int h5 = -1;  // pourcentage 0..100, -1 si inconnu
    int64_t h5Reset = 0;
    int d7 = -1;
    int64_t d7Reset = 0;
};

struct Session {
    char id[16] = "";
    char project[33] = "";
    char model[25] = "";
    char tool[25] = "";
    State state = State::Idle;
    int64_t since = 0;
    int ctx = -1;  // pourcentage 0..100, -1 si inconnu
};

struct HostSnapshot {
    char host[33] = "";  // HOST_MAX = 32 cote serveur
    int64_t ts = 0;
    Limits limits;
    Session sessions[MAX_SESSIONS];
    int count = 0;
};

// Ligne affichable : une session et le nom de son hote
struct Row {
    const Session *session;
    const char *host;
};

// Parse un snapshot JSON. En cas d'echec (JSON invalide, host absent),
// retourne false et laisse `out` intact. Les sessions qui ne sont pas des
// objets ou sans "id" sont ignorees. Seules sessions[0..count) sont valides.
bool parseSnapshot(const char *json, size_t len, HostSnapshot &out);

class Dashboard {
public:
    // Integre un snapshot recu a `now` (epoch local). Retourne l'alerte a jouer
    // (aucune si le snapshot precedent de cet hote etait deja perime).
    // Nouvel hote avec MAX_HOSTS deja connus : reprend le slot de l'hote le plus
    // vieux si son age depasse EVICT_AFTER_S, sinon il est ignore.
    Alert apply(const HostSnapshot &snap, int64_t now);
    // Oublie un hote (payload retained vide : topic efface). Retourne true s'il
    // etait connu. Les hotes suivants sont decales (ordre conserve).
    bool removeHost(const char *host);
    // Lignes triees par urgence (permission > idle > working) puis anciennete
    // (since croissant ; since = 0, inconnu, passe en tete de son groupe).
    // Les pointeurs des Row ne sont valides que jusqu'au prochain apply() ou
    // removeHost() : ne pas les conserver.
    int rows(Row *out, int max) const;
    // Quotas du snapshot le plus recent.
    const Limits *limits() const;
    int hostCount() const { return count_; }
    const char *hostName(int i) const { return hosts_[i].snap.host; }
    // Age maximal parmi les hotes (0 si aucun) : now - snap.ts, ou now - reception
    // si ts inconnu ; borne a 0.
    int64_t staleSeconds(int64_t now) const;
    bool anyWaiting() const;

private:
    struct HostEntry {
        HostSnapshot snap;
        int64_t receivedAt = 0;
    };
    HostEntry hosts_[MAX_HOSTS];
    int count_ = 0;
};

// Helpers d'affichage
void formatDuration(int64_t seconds, char *out, size_t size);  // "0:42", "5:10", "1h05", "2j"
uint32_t colorForPercent(int pct);                             // vert / orange / rouge
const char *stateLabel(State s);                                // "TRAVAILLE", "ATTENTE", "PERMISSION"

// Veille : ecran allume si une session attend, ou activite recente (ms).
bool screenShouldBeOn(bool anyWaiting, uint32_t nowMs, uint32_t lastActivityMs,
                      uint32_t timeoutMs = 10UL * 60UL * 1000UL);

}  // namespace dash
