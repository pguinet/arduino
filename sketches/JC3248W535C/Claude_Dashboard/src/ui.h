#pragma once
#include <time.h>
#include "dash_model.h"
#include "net_status.h"

// Toutes les fonctions ui_* supposent que l'appelant tient bsp_display_lock.

void ui_create();
// Reconstruit quotas + cartes a partir du modele (appel a chaque snapshot),
// puis rafraichit horloge/chronos/bandeau/pastille MQTT/etat reseau via ui_tick.
void ui_render(const dash::Dashboard &d, time_t now, const net::Status &netStatus);
// Met a jour horloge, chronos, bandeau, pastille MQTT et, tant qu'aucun serveur
// n'a ete recu, l'etat reseau (appel chaque seconde).
void ui_tick(const dash::Dashboard &d, time_t now, const net::Status &netStatus);
