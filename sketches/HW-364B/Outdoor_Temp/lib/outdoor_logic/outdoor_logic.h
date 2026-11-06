// Logique pure de l'afficheur de temperature exterieure (sans Arduino,
// testee en natif).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace outdoor {

// Extrait le champ "temperature" d'un message Zigbee2MQTT (JSON). Les autres
// champs (dont le bloc "update" de plusieurs centaines d'octets) sont ignores
// par un filtre ArduinoJson. Renvoie false si le JSON est invalide, si le
// champ manque ou n'est pas un nombre, ou si la valeur n'est pas plausible.
bool parseTemperature(const char *payload, size_t length, float &celsius);

// Etat de disponibilite Zigbee2MQTT : {"state":"online"|"offline"}, ou le
// format historique en texte brut "online"/"offline".
bool parseAvailability(const char *payload, size_t length, bool &online);

// Arrondi au dixieme de degre, en dixiemes entiers (8.25 -> 83, -0.44 -> -4).
int toTenths(float celsius);

// "8.2", "-0.4", "0.0"... Ecrit au plus size-1 caracteres.
void formatTenths(int tenths, char *out, size_t size);

// Age d'une mesure en texte court ASCII : "a l'instant", "il y a 5 min",
// "il y a 2 h 05", "il y a 3 j".
void formatAge(uint32_t ageMs, char *out, size_t size);

enum class Status {
    NoData,      // aucune valeur recue
    Fresh,       // valeur recente, d'age connu
    AgeUnknown,  // valeur retenue par le broker : date de reception inconnue
    Stale,       // pas de nouvelle depuis trop longtemps
    Offline,     // Zigbee2MQTT signale la sonde hors ligne
};

// Suit la derniere temperature recue et sa fraicheur. PubSubClient n'expose
// pas le drapeau retain : un message recu peu apres l'abonnement est
// considere comme le message retenu, donc d'age inconnu.
class SensorState {
  public:
    explicit SensorState(uint32_t staleMs = 3600000UL, uint32_t retainedWindowMs = 3000UL)
        : staleMs_(staleMs), retainedWindowMs_(retainedWindowMs) {}

    // A appeler juste apres chaque (re)abonnement au topic de la sonde.
    void onSubscribed(uint32_t nowMs);
    void onTemperature(int tenths, uint32_t nowMs);
    void onAvailability(bool online) { online_ = online; }

    Status status(uint32_t nowMs) const;
    bool hasValue() const { return hasValue_; }
    int tenths() const { return tenths_; }
    bool ageKnown() const { return ageKnown_; }
    // Temps ecoule depuis la mesure (age connu) ou depuis sa reception.
    uint32_t ageMs(uint32_t nowMs) const { return nowMs - receivedMs_; }

  private:
    uint32_t staleMs_;
    uint32_t retainedWindowMs_;
    bool subscribed_ = false;
    uint32_t subscribedMs_ = 0;
    bool hasValue_ = false;
    int tenths_ = 0;
    bool ageKnown_ = false;
    uint32_t receivedMs_ = 0;
    bool online_ = true;  // inconnu au demarrage : on ne crie pas au loup
};

}  // namespace outdoor
