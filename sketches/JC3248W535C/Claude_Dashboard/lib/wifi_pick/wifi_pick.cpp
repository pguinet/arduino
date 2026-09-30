#include "wifi_pick.h"

#include <string.h>

namespace wifi {

int pickFirstVisible(const Network *known, int nKnown, const char *const *visible, int nVisible)
{
    if (!known || !visible) return -1;
    for (int k = 0; k < nKnown; k++) {
        const char *ssid = known[k].ssid;
        if (!ssid || !*ssid) continue;
        for (int v = 0; v < nVisible; v++) {
            if (visible[v] && strcmp(ssid, visible[v]) == 0) return k;
        }
    }
    return -1;
}

}  // namespace wifi
