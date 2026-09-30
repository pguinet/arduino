// Choix du reseau WiFi parmi ceux configures (sans Arduino) : testable en natif.
#pragma once

namespace wifi {

struct Network {
    const char *ssid;
    const char *password;
};

// Premier reseau de `known` (ordre de declaration = priorite) dont le SSID
// figure dans `visible` (resultat d'un scan). Renvoie son index, -1 sinon.
// Les SSID vides ou nuls sont ignores des deux cotes.
int pickFirstVisible(const Network *known, int nKnown, const char *const *visible, int nVisible);

}  // namespace wifi
