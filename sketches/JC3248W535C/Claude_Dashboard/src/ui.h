#pragma once
#include <time.h>
#include "dash_model.h"

// Toutes les fonctions ui_* supposent que l'appelant tient bsp_display_lock.

void ui_create();
// Reconstruit quotas + cartes a partir du modele (appel a chaque snapshot),
// puis rafraichit horloge/chronos/bandeau/pastille MQTT via ui_tick.
void ui_render(const dash::Dashboard &d, time_t now, bool mqttOk);
// Met a jour horloge, chronos, bandeau et pastille MQTT (appel chaque seconde).
void ui_tick(const dash::Dashboard &d, time_t now, bool mqttOk);
