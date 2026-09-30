#pragma once
#include <stddef.h>
#include <stdint.h>

// Bip sur l'ampli I2S NS4168 (BCK 42, LRCK 2, DOUT 41).
// Le canal I2S n'est actif que pendant un bip : sans horloge, le NS4168 se met en veille
// (pas de souffle permanent entre deux alertes).

#ifndef BEEP_VOLUME
#define BEEP_VOLUME 35  // 0-100 (% de la pleine echelle)
#endif

struct BeepNote {
    uint16_t freqHz;      // 0 = silence
    uint16_t durationMs;
};

// Cree le canal I2S (desactive). Journalise un echec ; les bips sont alors ignores.
bool beep_begin();

// Joue une suite de notes (sinus, enveloppe 5 ms) de facon synchrone, puis coupe le canal I2S.
// Bloque ~la duree totale + 100 ms de queue silencieuse.
void beep_play(const BeepNote *notes, size_t count, uint8_t volume = BEEP_VOLUME);
