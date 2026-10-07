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

// Comme pickFirstVisible, en sautant les reseaux marques en echec dans `failed`
// (bit k = reseau k : visible mais connexion impossible, ex. mot de passe refuse
// ou portail captif). Si tous les reseaux connus visibles sont en echec, le
// masque est remis a zero et le choix reprend par ordre de priorite.
int pickNextVisible(const Network *known, int nKnown, const char *const *visible, int nVisible,
                    unsigned &failed);

}  // namespace wifi
