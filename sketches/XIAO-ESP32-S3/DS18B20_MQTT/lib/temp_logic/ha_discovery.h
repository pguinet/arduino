// Identifiant, sujets MQTT et message de decouverte Home Assistant.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace temp {

struct Topics {
    char state[64];
    char status[64];
    char probe[64];
    char config[96];
};

// "xiao_ds18b20_" + 3 derniers octets de la MAC en hexa minuscule.
void makeDeviceId(const uint8_t mac[6], char *out, size_t size);

// deviceId : 32 caracteres au plus (taille des tampons de Topics). Au-dela,
// snprintf tronque les sujets sans le signaler.
void makeTopics(const char *deviceId, Topics &t);

// JSON de decouverte du capteur de temperature. Renvoie la longueur ecrite,
// 0 si le tampon est trop petit.
size_t buildDiscovery(const char *deviceId, const Topics &t, char *out, size_t size);

}  // namespace temp
