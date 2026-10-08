#include "net_status.h"

#include <stdarg.h>
#include <stdio.h>

namespace net {

namespace {

// Ajout borne a `out` : la sortie reste terminee par 0 meme tronquee.
struct Writer {
    char *out;
    size_t len;
    size_t pos = 0;

    void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        if (pos + 1 >= len) return;
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(out + pos, len - pos, fmt, args);
        va_end(args);
        if (n < 0) return;
        pos += (size_t)n < len - pos ? (size_t)n : len - pos - 1;
    }
};

void addWifi(Writer &w, const Status &s)
{
    switch (s.wifi) {
    case WifiPhase::Scanning:
        w.add("WiFi : scan en cours");
        return;
    case WifiPhase::NoKnownNetwork:
        w.add("WiFi : aucun reseau connu (%d visibles)", s.visibleCount);
        return;
    case WifiPhase::Connecting:
        if (!s.disconnectReason) {
            w.add("WiFi : connexion a %s...", s.ssid);
            return;
        }
        w.add("WiFi : connexion a %s, echec %s (%d)", s.ssid, s.disconnectReasonName, s.disconnectReason);
        return;
    case WifiPhase::Connected:
        w.add("WiFi : %s  %s  %d dBm", s.ssid, s.ip, s.rssi);
        return;
    }
}

void addClock(Writer &w, const Status &s)
{
    if (s.clockOk) {
        switch (s.clockSource) {
        case ClockSource::Ntp:
            w.add("Heure : OK (NTP)");
            return;
        case ClockSource::Https:
            w.add("Heure : OK (HTTPS, NTP filtre)");
            return;
        case ClockSource::None:
            w.add("Heure : OK");
            return;
        }
    }
    if (s.wifi != WifiPhase::Connected) {
        w.add("Heure : attente WiFi");
        return;
    }
    if (s.clockWaitS < 60)
        w.add("Heure : attente NTP (%lu s)", (unsigned long)s.clockWaitS);
    else
        w.add("Heure : attente NTP (%lu min)", (unsigned long)(s.clockWaitS / 60));
    if (s.httpsTimeFails) w.add(", HTTPS echec x%d", s.httpsTimeFails);
}

void addMqtt(Writer &w, const Status &s)
{
    if (s.mqtt == MqttPhase::BadCredentials) {
        w.add("MQTT : certificats invalides");
        return;
    }
    if (s.wifi != WifiPhase::Connected) {
        w.add("MQTT : attente WiFi");
        return;
    }
    if (!s.clockOk) {
        w.add("MQTT : attente heure");  // TLS exige l'heure
        return;
    }
    switch (s.mqtt) {
    case MqttPhase::Waiting:
    case MqttPhase::BadCredentials:
        w.add("MQTT : connexion...");
        return;
    case MqttPhase::DnsFailed:
        w.add("MQTT : echec DNS");
        return;
    case MqttPhase::ConnectFailed:
        if (!s.tlsError) {
            w.add("MQTT : echec rc=%d", s.mqttRc);
            return;
        }
        w.add("MQTT : echec rc=%d, TLS %d", s.mqttRc, s.tlsError);
        return;
    case MqttPhase::Connected:
        w.add("MQTT : connecte, attente des donnees");
        return;
    }
}

}  // namespace

void format(const Status &s, char *out, size_t len)
{
    if (!out || !len) return;
    out[0] = 0;
    Writer w{out, len};
    addWifi(w, s);
    w.add("\n");
    addClock(w, s);
    w.add("\n");
    addMqtt(w, s);
}

}  // namespace net
