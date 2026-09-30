// Modele pur du tableau de bord (sans Arduino ni LVGL) : testable en natif.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace dash {

constexpr int MAX_SESSIONS = 12;
constexpr int MAX_HOSTS = 4;
constexpr int MAX_ROWS = MAX_SESSIONS * MAX_HOSTS;
constexpr int64_t STALE_AFTER_S = 180;
// Hote muet depuis plus longtemps (agent plante, serveur en veille) : oublie par
// expire(), et son slot peut etre repris par un nouvel hote.
constexpr int64_t FORGET_AFTER_S = 3600;

enum class State : uint8_t { Working, Idle, Permission };
enum class Alert : uint8_t { None, Idle, Permission };

struct Limits {
    int h5 = -1;  // pourcentage 0..100, -1 si inconnu
    int64_t h5Reset = 0;
    int d7 = -1;
    int64_t d7Reset = 0;
    int64_t updated = 0;  // date de lecture des quotas par l'agent, 0 si inconnue (agent ancien)
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
    // vieux si son age depasse FORGET_AFTER_S, sinon il est ignore.
    // *changed (optionnel) : vrai si l'affichage doit etre reconstruit (nouvel hote,
    // sessions ou quotas affiches differents). Un heartbeat ou seul ts change -> faux.
    Alert apply(const HostSnapshot &snap, int64_t now, bool *changed = nullptr);
    // Oublie un hote (payload vide : arret propre de l'agent, ou topic retained
    // efface). Retourne true s'il etait connu. Les hotes suivants sont decales
    // (ordre conserve).
    bool removeHost(const char *host);
    // Oublie les hotes dont l'age (meme regle que staleSeconds) depasse
    // FORGET_AFTER_S. Retourne le nombre d'hotes retires (ordre conserve).
    int expire(int64_t now);
    // Lignes triees par urgence (permission > idle > working) puis anciennete
    // (since croissant ; since = 0, inconnu, passe en tete de son groupe).
    // Les pointeurs des Row ne sont valides que jusqu'au prochain apply(),
    // removeHost() ou expire() : ne pas les conserver.
    int rows(Row *out, int max) const;
    // Quotas affiches, fenetre par fenetre (5h et 7j independamment) : ceux de
    // l'hote aux quotas les plus frais (limits.updated, sinon ts du snapshot),
    // en ignorant une fenetre dont le reset est passe (valeur perimee). Sans
    // candidat, la fenetre vaut -1 (reset 0) ; updated reste a 0. Un autre hote dont la statusline
    // n'a pas tourne depuis des heures ne masque donc pas les valeurs a jour.
    Limits limits(int64_t now) const;
    int hostCount() const { return count_; }
    const char *hostName(int i) const { return hosts_[i].snap.host; }
    // Age maximal parmi les hotes (0 si aucun) : now - snap.ts, ou now - reception
    // si ts inconnu ; borne a 0.
    int64_t staleSeconds(int64_t now) const;
    // Nombre total de sessions listees (tous hotes, tous etats, perimes compris).
    int sessionCount() const;

private:
    struct HostEntry {
        HostSnapshot snap;
        int64_t receivedAt = 0;
    };
    HostEntry hosts_[MAX_HOSTS];
    int count_ = 0;
};

// Helpers d'affichage
void formatDuration(int64_t seconds, char *out, size_t size);  // "0:42", "5:10", "1h05", "2j", "999j+"
uint32_t colorForPercent(int pct);  // vert < 60 / orange < 85 / rouge (pct < 0 : vert, a eviter)
const char *stateLabel(State s);                                // "TRAVAILLE", "ATTENTE", "PERMISSION"

// Veille : ecran allume tant qu'au moins une session est listee ; sans session,
// eteint apres timeoutMs sans activite (toucher, alerte, disparition de la
// derniere session), mesuree depuis lastActivityMs (robuste au rebouclage).
bool screenShouldBeOn(bool hasSessions, uint32_t nowMs, uint32_t lastActivityMs,
                      uint32_t timeoutMs = 10UL * 60UL * 1000UL);

}  // namespace dash
