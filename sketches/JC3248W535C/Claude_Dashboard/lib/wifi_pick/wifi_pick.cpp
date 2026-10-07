#include "wifi_pick.h"

#include <string.h>

namespace wifi {

static bool isVisible(const char *ssid, const char *const *visible, int nVisible)
{
    if (!ssid || !*ssid) return false;
    for (int v = 0; v < nVisible; v++) {
        if (visible[v] && strcmp(ssid, visible[v]) == 0) return true;
    }
    return false;
}

// Premier reseau visible hors de `skip` (bit k = reseau k ignore)
static int pickVisible(const Network *known, int nKnown, const char *const *visible, int nVisible,
                       unsigned skip)
{
    if (!known || !visible) return -1;
    for (int k = 0; k < nKnown; k++) {
        bool skipped = k < 32 && (skip >> k) & 1u;
        if (!skipped && isVisible(known[k].ssid, visible, nVisible)) return k;
    }
    return -1;
}

int pickFirstVisible(const Network *known, int nKnown, const char *const *visible, int nVisible)
{
    return pickVisible(known, nKnown, visible, nVisible, 0);
}

int pickNextVisible(const Network *known, int nKnown, const char *const *visible, int nVisible,
                    unsigned &failed)
{
    int k = pickVisible(known, nKnown, visible, nVisible, failed);
    if (k >= 0) return k;
    failed = 0;  // tous en echec (ou aucun visible) : nouvelle tournee
    return pickVisible(known, nKnown, visible, nVisible, 0);
}

}  // namespace wifi
