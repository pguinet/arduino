// Etat de la connexion reseau (WiFi, NTP, MQTT) et son texte de diagnostic
// a l'ecran (sans Arduino) : testable en natif.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace net {

enum class WifiPhase { Scanning, NoKnownNetwork, Connecting, Connected };

enum class MqttPhase { Waiting, BadCredentials, DnsFailed, ConnectFailed, Connected };

struct Status {
    WifiPhase wifi = WifiPhase::Scanning;
    char ssid[33] = "";               // reseau choisi (Connecting / Connected)
    int visibleCount = 0;             // NoKnownNetwork : reseaux vus au scan
    int disconnectReason = 0;         // derniere deconnexion du driver, 0 = aucune
    char disconnectReasonName[32] = "";
    char ip[16] = "";
    int rssi = 0;
    bool clockOk = false;
    uint32_t clockWaitS = 0;          // depuis la connexion WiFi, heure toujours inconnue
    MqttPhase mqtt = MqttPhase::Waiting;
    int mqttRc = 0;                   // ConnectFailed : PubSubClient::state()
    int tlsError = 0;                 // ConnectFailed : erreur mbedTLS, 0 = aucune
};

// Trois lignes separees par '\n' (WiFi, Heure, MQTT), tronquees a `len`.
void format(const Status &s, char *out, size_t len);

}  // namespace net
