// Logique pure du capteur DS18B20 (sans Arduino, testee en natif).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace temp {

// Lecture DS18B20 exploitable ? Rejette -127 (sonde absente / CRC faux),
// les valeurs hors plage, et 85 degres (valeur de mise sous tension) sauf
// si la lecture valide precedente en etait deja proche.
// previous est la derniere lecture VALIDE. L'appelant doit l'oublier
// (hasPrevious = false) quand la sonde passe hors ligne : une sonde
// rebranchee renvoie 85 a la mise sous tension, valeur alors rejetee.
bool isValidReading(float celsius, bool hasPrevious, float previous);

// Arrondi au dixieme de degre, en dixiemes entiers (21.44 -> 214).
int toTenths(float celsius);

// "21.4", "-0.5"... Ecrit au plus size-1 caracteres.
void formatTenths(int tenths, char *out, size_t size);

// Publie la premiere valeur, puis a chaque ecart >= minDeltaTenths avec la
// derniere publiee, ou quand maxIntervalMs s'est ecoule depuis (heartbeat).
class PublishPolicy {
  public:
    explicit PublishPolicy(int minDeltaTenths = 2, uint32_t maxIntervalMs = 300000)
        : minDelta_(minDeltaTenths), maxInterval_(maxIntervalMs) {}

    bool shouldPublish(int tenths, uint32_t nowMs) const;
    void markPublished(int tenths, uint32_t nowMs);
    // Oublie la derniere publication (reconnexion MQTT) : la suivante part.
    void reset() { hasPublished_ = false; }

  private:
    int minDelta_;
    uint32_t maxInterval_;
    bool hasPublished_ = false;
    int lastTenths_ = 0;
    uint32_t lastMs_ = 0;
};

// La sonde repond-elle ? Hors ligne apres maxFailures lectures invalides
// d'affilee, en ligne des la premiere lecture valide.
class ProbeHealth {
  public:
    // maxFailures vaut au moins 1 (avec 0, la sonde ne passerait jamais hors ligne).
    explicit ProbeHealth(uint8_t maxFailures = 3)
        : maxFailures_(maxFailures > 0 ? maxFailures : 1) {}

    // Enregistre une lecture ; renvoie true si l'etat en ligne a change.
    bool record(bool valid);
    bool online() const { return online_; }

  private:
    uint8_t maxFailures_;
    uint8_t failures_ = 0;
    bool online_ = false;
};

}  // namespace temp
