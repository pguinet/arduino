/*
 * Claude_Dashboard - JC3248W535C
 *
 * Tableau de bord Claude Code : quotas du forfait, etat des sessions
 * (travaille / attente / permission) et contexte, recus d'un serveur
 * distant via MQTT/TLS. Bip et reveil de l'ecran quand une session
 * attend une reponse.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.4.x, ArduinoJson, PubSubClient
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <PubSubClient.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <lvgl.h>
#include <sys/time.h>
#include <time.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "credentials.h"
#include "beep.h"
#include "dash_model.h"
#include "http_date.h"
#include "net_status.h"
#include "ui.h"
#include "wifi_pick.h"

// Modele : sketches/common/credentials.h.example
#ifndef WIFI_SSID
#error "credentials.h : WIFI_SSID manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef WIFI_PASSWORD
#error "credentials.h : WIFI_PASSWORD manquant (voir sketches/common/credentials.h.example)"
#endif
#if defined(WIFI_SSID_2) != defined(WIFI_PASSWORD_2) || defined(WIFI_SSID_3) != defined(WIFI_PASSWORD_3) \
    || defined(WIFI_SSID_4) != defined(WIFI_PASSWORD_4)
#error "credentials.h : chaque WIFI_SSID_n doit avoir son WIFI_PASSWORD_n"
#endif
#ifndef DASH_MQTT_SERVER
#error "credentials.h : DASH_MQTT_SERVER manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef DASH_MQTT_PORT
#error "credentials.h : DASH_MQTT_PORT manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef DASH_MQTT_CLIENT_ID
#error "credentials.h : DASH_MQTT_CLIENT_ID manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef DASH_MQTT_CA_CERT
#error "credentials.h : DASH_MQTT_CA_CERT manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef DASH_MQTT_CLIENT_CERT
#error "credentials.h : DASH_MQTT_CLIENT_CERT manquant (voir sketches/common/credentials.h.example)"
#endif
#ifndef DASH_MQTT_CLIENT_KEY
#error "credentials.h : DASH_MQTT_CLIENT_KEY manquant (voir sketches/common/credentials.h.example)"
#endif

#define WDT_TIMEOUT_SEC      30
#define MQTT_TOPIC           "claude-dash/+/state"
#define MQTT_BUFFER_SIZE     4096
#define WIFI_FORCE_AFTER_MS  30000UL   // reconnexion forcee apres 30 s sans WiFi
#define WIFI_SCAN_TIMEOUT_MS 15000UL   // scan asynchrone abandonne au-dela
#define WIFI_SCAN_RETRY_MS   1000UL    // scan refuse (station encore en connexion) : nouvel essai
#define MQTT_RETRY_MIN_MS    5000UL
#define MQTT_RETRY_MAX_MS    60000UL
#define CLOCK_SKEW_WARN_S    30
#define CLOCK_SKEW_REPEAT_MS (10UL * 60UL * 1000UL)  // un avertissement par hote / 10 min
#define HOST_MISMATCH_REPEAT_MS (10UL * 60UL * 1000UL)  // un avertissement host != topic / 10 min
#define TZ_PARIS             "CET-1CEST,M3.5.0,M10.5.0/3"
static const time_t CLOCK_VALID_AFTER = 1704067200;  // 2024-01-01

// Repli quand le reseau filtre NTP (UDP 123, WiFi invites) : l'heure est lue
// dans l'en-tete Date d'une reponse HTTPS (443 passe partout).
#ifndef TIME_HTTPS_HOST
#define TIME_HTTPS_HOST      "www.google.com"
#endif
#define TIME_HTTPS_AFTER_MS  10000UL                 // NTP muet 10 s apres le WiFi
#define TIME_HTTPS_RETRY_MS  60000UL                 // echec : nouvel essai
#define TIME_HTTPS_RESYNC_MS (6UL * 3600UL * 1000UL) // sans NTP, recalage periodique (derive)
#define TIME_HTTPS_READ_MS   5000UL                  // reponse attendue au plus

// Pire cas d'une tentative MQTT (bloquante) : DNS ~10 s (resolu a part, WDT
// reinitialise avant et apres), puis TCP 5 s + handshake TLS 8 s + CONNACK 5 s
// = 18 s < WDT 30 s.
#define TCP_CONNECT_TIMEOUT_MS 5000
#define TLS_HANDSHAKE_TIMEOUT_S 8
#define MQTT_SOCKET_TIMEOUT_S  5

#ifndef SCREEN_TIMEOUT_MS
#define SCREEN_TIMEOUT_MS    (10UL * 60UL * 1000UL)  // sans session : veille apres 10 min sans activite
#endif

static NetworkClientSecure tls;
static PubSubClient mqtt(tls);
static dash::Dashboard dashboard;
static dash::HostSnapshot incoming;  // ~1 Ko : hors pile
static bool renderNeeded = false;
static dash::Alert pendingAlert = dash::Alert::None;
static uint32_t lastActivityMs = 0;  // dernier toucher, alerte, fin de la derniere session ou demarrage
static bool screenOn = true;
static net::Status netStatus;  // diagnostic a l'ecran tant qu'aucun serveur n'est recu
static volatile int wifiDisconnectReason = 0;  // ecrit par la tache d'evenements WiFi

static volatile bool ntpSynced = false;  // ecrit par la tache SNTP (lwIP)

static bool clockValid() { return time(nullptr) > CLOCK_VALID_AFTER; }

static void printHeap(const char *tag)
{
    Serial.printf("[%s] heap interne libre=%u min=%u | psram libre=%u\n", tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static int fillRandom(void *, unsigned char *out, size_t len)
{
    esp_fill_random(out, len);
    return 0;
}

// Verifie une fois au demarrage les PEM de credentials.h : l'erreur mbedTLS du
// handshake ne dit pas lequel est en cause. Rien n'est affiche de leur contenu.
static bool pemValid(const char *name, const char *pem, bool isKey)
{
    int ret;
    if (isKey) {
        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        ret = mbedtls_pk_parse_key(&pk, reinterpret_cast<const unsigned char *>(pem),
                                   strlen(pem) + 1, nullptr, 0, fillRandom, nullptr);
        mbedtls_pk_free(&pk);
    } else {
        mbedtls_x509_crt crt;
        mbedtls_x509_crt_init(&crt);
        ret = mbedtls_x509_crt_parse(&crt, reinterpret_cast<const unsigned char *>(pem),
                                     strlen(pem) + 1);
        mbedtls_x509_crt_free(&crt);
    }
    if (ret == 0) return true;
    if (ret > 0) {  // x509 : au moins un certificat lu, ret = nombre d'illisibles
        Serial.printf("Attention credentials.h : %s, %d certificat(s) ignore(s)\n", name, ret);
        return true;
    }
    Serial.printf("ERREUR credentials.h : %s invalide (mbedTLS -0x%04x), MQTT desactive "
                  "(voir sketches/common/credentials.h.example)\n", name, (unsigned)-ret);
    return false;
}

static bool checkCredentials()
{
    // Pas de && : les trois PEM sont verifies (et signales) meme si le premier echoue.
    bool ok = pemValid("DASH_MQTT_CA_CERT", DASH_MQTT_CA_CERT, false);
    ok &= pemValid("DASH_MQTT_CLIENT_CERT", DASH_MQTT_CLIENT_CERT, false);
    ok &= pemValid("DASH_MQTT_CLIENT_KEY", DASH_MQTT_CLIENT_KEY, true);
    return ok;
}

// "claude-dash/<host>/state" exactement -> host. Faux sinon.
static bool hostFromTopic(const char *topic, char *host)
{
    int n = 0;
    return sscanf(topic, "claude-dash/%32[^/]/state%n", host, &n) == 1 && n > 0 &&
           topic[n] == '\0';
}

// Vrai si l'avertissement de decalage d'horloge pour cet hote est du
// (au plus un par hote toutes les 10 min).
static bool skewWarningDue(const char *host, uint32_t ms)
{
    static struct {
        char host[sizeof(dash::HostSnapshot::host)];
        uint32_t at;
    } seen[dash::MAX_HOSTS];
    int slot = 0;
    for (int i = 0; i < dash::MAX_HOSTS; i++) {
        if (strcmp(seen[i].host, host) == 0) {
            if (ms - seen[i].at < CLOCK_SKEW_REPEAT_MS) return false;
            slot = i;
            break;
        }
        if (seen[i].at < seen[slot].at) slot = i;  // sinon : le plus ancien
    }
    strlcpy(seen[slot].host, host, sizeof seen[slot].host);
    seen[slot].at = ms;
    return true;
}

// Appele depuis mqtt.loop(), donc dans la tache loop() : pas de concurrence.
static void onMessage(char *topic, byte *payload, unsigned int len)
{
    if (!clockValid()) return;  // staleness et transitions n'ont pas de sens sans heure
    time_t now = time(nullptr);

    char host[sizeof incoming.host];
    if (!hostFromTopic(topic, host)) return;

    if (len == 0) {
        // Message vide : arret propre de l'agent (ou topic retained efface),
        // l'hote est retire.
        if (dashboard.removeHost(host)) {
            Serial.printf("Hote retire : %s\n", host);
            renderNeeded = true;
        }
        return;
    }
    if (!dash::parseSnapshot(reinterpret_cast<const char *>(payload), len, incoming)) {
        Serial.printf("Snapshot invalide sur %s (%u o)\n", topic, len);
        return;
    }
    if (strcmp(incoming.host, host) != 0) {
        // Les filtres Scaleway limitent chaque serveur a son topic : un host
        // different dans le payload usurperait un autre serveur. Compte et
        // signale au plus toutes les 10 min (le premier tout de suite).
        static uint32_t mismatches = 0;
        static uint32_t lastLog = 0;
        static bool loggedOnce = false;
        mismatches++;
        uint32_t ms = millis();
        if (!loggedOnce || ms - lastLog >= HOST_MISMATCH_REPEAT_MS) {
            loggedOnce = true;
            lastLog = ms;
            Serial.printf("Snapshot ignore : host \"%s\" different du topic %s (%lu depuis le demarrage)\n",
                          incoming.host, topic, (unsigned long)mismatches);
        }
        return;
    }
    // Un decalage d'horloge fait juger les snapshots perimes, ce qui rend
    // l'ecran muet (alertes supprimees) : le signaler.
    long long skew = (long long)now - (long long)incoming.ts;
    if (incoming.ts > 0 && llabs(skew) > CLOCK_SKEW_WARN_S && skewWarningDue(host, millis()))
        Serial.printf("Attention : horloge decalee de %lld s avec %s (verifier NTP sur le serveur)\n",
                      skew, incoming.host);

    bool changed = false;
    dash::Alert a = dashboard.apply(incoming, now, &changed);
    if (a == dash::Alert::Permission || pendingAlert == dash::Alert::None) pendingAlert = a;
    if (changed) renderNeeded = true;
}

// Heure lue dans l'en-tete Date d'une reponse HTTPS. Le certificat n'est pas
// verifie (impossible sans heure) : la date n'est qu'une approximation a
// +-1 s, et un faux serveur ne pourrait au pire que faire accepter un
// certificat expire du hub. Pire cas : TCP 5 s + TLS 8 s + reponse 5 s < WDT.
static bool syncClockHttps()
{
    esp_task_wdt_reset();
    NetworkClientSecure client;
    client.setInsecure();
    client.setConnectionTimeout(TCP_CONNECT_TIMEOUT_MS);
    client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
    uint32_t t0 = millis();
    if (!client.connect(TIME_HTTPS_HOST, 443)) {
        Serial.printf("Heure HTTPS : connexion a %s impossible\n", TIME_HTTPS_HOST);
        esp_task_wdt_reset();
        return false;
    }
    esp_task_wdt_reset();
    client.print("HEAD / HTTP/1.1\r\nHost: " TIME_HTTPS_HOST "\r\nConnection: close\r\n\r\n");

    char line[128];
    size_t len = 0;
    time_t t = 0;
    bool found = false;
    uint32_t start = millis();
    while (!found && millis() - start < TIME_HTTPS_READ_MS && (client.connected() || client.available())) {
        int c = client.read();
        if (c < 0) {
            delay(10);
            continue;
        }
        if (c != '\n') {
            if (len < sizeof line - 1) line[len++] = (char)c;
            continue;
        }
        line[len] = 0;
        if (len == 0 || (len == 1 && line[0] == '\r')) break;  // fin des en-tetes
        found = httpdate::parseHeaderLine(line, &t);
        len = 0;
    }
    client.stop();
    esp_task_wdt_reset();
    if (!found || t <= CLOCK_VALID_AFTER) {
        Serial.printf("Heure HTTPS : pas d'en-tete Date valide de %s\n", TIME_HTTPS_HOST);
        return false;
    }
    // La date est celle de l'envoi de la reponse : compter la moitie de l'aller-retour.
    struct timeval tv = {};
    tv.tv_sec = t + (time_t)((millis() - t0) / 2000);
    settimeofday(&tv, nullptr);
    Serial.printf("Heure HTTPS : recue de %s en %lu ms (NTP muet)\n", TIME_HTTPS_HOST,
                  (unsigned long)(millis() - t0));
    return true;
}

// Repli HTTPS tant que NTP n'a rien donne : d'abord 10 s apres le WiFi, puis
// toutes les minutes en cas d'echec, et toutes les 6 h pour rattraper la derive.
static void handleClockFallback(uint32_t ms)
{
    static uint32_t wifiUpAt = 0;
    static bool wifiWasUp = false;
    static uint32_t lastAttempt = 0;
    static uint32_t nextDelay = 0;

    bool wifiUp = WiFi.status() == WL_CONNECTED;
    if (wifiUp && !wifiWasUp) {
        wifiUpAt = ms;
        nextDelay = 0;
        lastAttempt = ms;
    }
    wifiWasUp = wifiUp;
    if (!wifiUp || ntpSynced) return;
    if (!clockValid() && ms - wifiUpAt < TIME_HTTPS_AFTER_MS) return;  // laisser sa chance a NTP
    if (ms - lastAttempt < nextDelay) return;

    if (syncClockHttps()) {
        netStatus.clockSource = net::ClockSource::Https;
        netStatus.httpsTimeFails = 0;
        nextDelay = TIME_HTTPS_RESYNC_MS;
    } else {
        netStatus.httpsTimeFails++;
        nextDelay = TIME_HTTPS_RETRY_MS;
    }
    lastAttempt = millis();
}

static bool mqttConnect()
{
    // DNS a part : la resolution peut durer plusieurs secondes, le WDT est
    // reinitialise avant la connexion TLS (le resultat est mis en cache par lwIP).
    esp_task_wdt_reset();
    IPAddress ip;
    uint32_t tDns = millis();
    bool resolved = WiFi.hostByName(DASH_MQTT_SERVER, ip);
    tDns = millis() - tDns;
    esp_task_wdt_reset();
    if (!resolved) {
        Serial.printf("MQTT : echec DNS du broker apres %lu ms\n", (unsigned long)tDns);
        netStatus.mqtt = net::MqttPhase::DnsFailed;
        return false;  // meme backoff qu'un echec de connexion
    }
    static bool dnsShown = false;
    if (!dnsShown) {
        dnsShown = true;
        Serial.printf("DNS du broker resolu en %lu ms\n", (unsigned long)tDns);
    }

    uint32_t t0 = millis();
    if (!mqtt.connect(DASH_MQTT_CLIENT_ID)) {
        char err[100];
        int code = tls.lastError(err, sizeof err);
        Serial.printf("MQTT echec rc=%d apres %lu ms (TLS %d : %s)\n", mqtt.state(),
                      (unsigned long)(millis() - t0), code, code ? err : "-");
        netStatus.mqtt = net::MqttPhase::ConnectFailed;
        netStatus.mqttRc = mqtt.state();
        netStatus.tlsError = code;
        return false;
    }
    Serial.printf("MQTT connecte en %lu ms (client %.8s...)\n", (unsigned long)(millis() - t0),
                  DASH_MQTT_CLIENT_ID);
    // Plan Shared de Scaleway : abonnement QoS 0 uniquement, pas de retained.
    if (!mqtt.subscribe(MQTT_TOPIC, 0)) {
        Serial.println("MQTT : echec abonnement, deconnexion");
        mqtt.disconnect();
        return false;
    }
    Serial.println("Abonne a " MQTT_TOPIC " (QoS 0) : etat attendu au prochain heartbeat (<= heartbeat de l'agent)");

    static bool heapShown = false;
    if (!heapShown) {
        heapShown = true;
        printHeap("TLS connecte");
    }
    return true;
}

// Reseaux connus, par ordre de priorite : le premier visible au scan gagne
// (l'ecran change de site, pas de course au meilleur signal), sauf s'il vient
// d'echouer : on passe alors au suivant visible (voir wifiFailed).
static const wifi::Network WIFI_NETWORKS[] = {
    {WIFI_SSID, WIFI_PASSWORD},
#ifdef WIFI_SSID_2
    {WIFI_SSID_2, WIFI_PASSWORD_2},
#endif
#ifdef WIFI_SSID_3
    {WIFI_SSID_3, WIFI_PASSWORD_3},
#endif
#ifdef WIFI_SSID_4
    {WIFI_SSID_4, WIFI_PASSWORD_4},
#endif
};
static constexpr int WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

static bool wifiScanning = false;
static uint32_t wifiScanStartMs = 0;
static bool wifiScanRetry = false;  // scan refuse : a relancer apres WIFI_SCAN_RETRY_MS
static int wifiTried = -1;         // index du reseau en cours de connexion (-1 : aucun)
static unsigned wifiFailed = 0;    // bit k : reseau k visible mais connexion en echec

// WiFi.disconnect() ne fait rien tant que la station n'est pas connectee : il
// laisserait tourner une tentative en echec, que le core relance a chaque
// deconnexion. esp_wifi_disconnect() l'interrompt (raison ASSOC_LEAVE, que le
// core ne relance pas).
static void stopWifiAttempt()
{
    esp_wifi_disconnect();
}

static void setWifiConnecting(const char *ssid)
{
    netStatus.wifi = net::WifiPhase::Connecting;
    strlcpy(netStatus.ssid, ssid, sizeof netStatus.ssid);
}

// Un seul reseau : begin() direct (marche aussi avec un SSID cache). Sinon scan
// asynchrone, conclu par pollWifiScan() ; scan refuse par le driver : nouvel
// essai apres 1 s, sinon le cycle de 30 s de handleWifi() relance.
static void startWifi(uint32_t ms)
{
    if (WIFI_NETWORK_COUNT == 1) {
        setWifiConnecting(WIFI_NETWORKS[0].ssid);
        WiFi.begin(WIFI_NETWORKS[0].ssid, WIFI_NETWORKS[0].password);
        return;
    }
    netStatus.wifi = net::WifiPhase::Scanning;
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
        // Station encore en connexion (l'arret d'une tentative est asynchrone) :
        // le driver refuse le scan. On recoupe et on retente vite.
        Serial.println("Scan WiFi impossible : nouvel essai dans 1 s");
        stopWifiAttempt();
        wifiScanRetry = true;
        wifiScanStartMs = ms;
        return;
    }
    wifiScanRetry = false;
    wifiScanning = true;
    wifiScanStartMs = ms;
    Serial.println("Scan WiFi...");
}

static void pollWifiScan(uint32_t ms)
{
    int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && ms - wifiScanStartMs < WIFI_SCAN_TIMEOUT_MS) return;
    wifiScanning = false;
    if (n < 0) {
        Serial.println(n == WIFI_SCAN_RUNNING ? "Scan WiFi trop long : abandon" : "Scan WiFi en echec");
        WiFi.scanDelete();
        return;
    }
    // Tous les resultats sont examines : un resultat par point d'acces (BSSID),
    // si bien qu'un bureau a plusieurs bornes en aligne vite des dizaines. Seuls
    // les SSID declares sont retenus, chacun une fois.
    const char *visible[WIFI_NETWORK_COUNT] = {};
    bool seen[WIFI_NETWORK_COUNT] = {};
    int count = 0;
    for (int i = 0; i < n && count < WIFI_NETWORK_COUNT; i++) {
        String ssid = WiFi.SSID(i);
        for (int j = 0; j < WIFI_NETWORK_COUNT; j++) {
            if (!seen[j] && ssid == WIFI_NETWORKS[j].ssid) {
                seen[j] = true;
                visible[count++] = WIFI_NETWORKS[j].ssid;
            }
        }
    }
    WiFi.scanDelete();
    int k = wifi::pickNextVisible(WIFI_NETWORKS, WIFI_NETWORK_COUNT, visible, count, wifiFailed);
    wifiTried = k;
    if (k < 0) {
        Serial.printf("Aucun reseau connu parmi %d visibles\n", n);
        netStatus.wifi = net::WifiPhase::NoKnownNetwork;
        netStatus.visibleCount = n;
        return;
    }
    Serial.printf("Reseau connu trouve : %s\n", WIFI_NETWORKS[k].ssid);
    setWifiConnecting(WIFI_NETWORKS[k].ssid);
    WiFi.begin(WIFI_NETWORKS[k].ssid, WIFI_NETWORKS[k].password);
}

// La reconnexion automatique du driver (sur le reseau choisi) fait l'essentiel ;
// on ne relance disconnect() + startWifi() (nouveau choix de reseau) qu'apres
// 30 s continues sans WiFi, y compris au demarrage.
static void handleWifi(uint32_t ms)
{
    static uint32_t downSince = 0;  // debut de la coupure (0 = demarrage)
    static bool wasConnected = false;
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) {
        wasConnected = connected;
        if (connected) {
            Serial.printf("WiFi connecte a %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            wifiFailed = 0;
        } else {
            Serial.println("WiFi perdu");
        }
        downSince = ms;
    }
    if (wifiScanning) {
        pollWifiScan(ms);
        return;
    }
    if (wifiScanRetry && !connected) {
        if (ms - wifiScanStartMs >= WIFI_SCAN_RETRY_MS) startWifi(ms);
        return;
    }
    wifiScanRetry = false;
    if (!connected && ms - downSince >= WIFI_FORCE_AFTER_MS) {
        downSince = ms;
        Serial.println("WiFi absent depuis 30 s : reconnexion forcee");
        if (wifiTried >= 0 && WIFI_NETWORK_COUNT > 1) {
            // Visible mais injoignable (mot de passe, portail, DHCP...) : le
            // prochain scan essaiera le reseau suivant
            int reason = wifiDisconnectReason;
            Serial.printf("Echec sur %s (%s) : essai du reseau suivant\n", WIFI_NETWORKS[wifiTried].ssid,
                          reason ? WiFi.disconnectReasonName((wifi_err_reason_t)reason) : "sans deconnexion");
            wifiFailed |= 1u << wifiTried;
            wifiTried = -1;
        }
        stopWifiAttempt();
        startWifi(ms);
    }
}

// Etat MQTT suivi ici : apres un echec de connexion, NetworkClientSecure::connected()
// (appele par mqtt.connected()) journalise une erreur setSocketOption a chaque appel.
// On ne l'interroge donc que via mqtt.loop(), tant que la liaison est etablie.
static bool mqttUp = false;
static bool credentialsOk = false;

static void setScreen(bool on, const char *why)
{
    if (on == screenOn) return;
    screenOn = on;
    if (on) bsp_display_backlight_on();
    else bsp_display_backlight_off();
    Serial.printf("Ecran %s (%s)\n", on ? "allume" : "eteint", why);
}

// Alerte (transition vers attente/permission) : ecran rallume puis bip.
// dash_model ne leve pas d'alerte pour une session inconnue : pas de bip au
// demarrage ni au premier snapshot.
static void alertTriggered(dash::Alert a)
{
    static const BeepNote PERMISSION[] = {{1760, 90}, {0, 40}, {1760, 90}};  // deux notes aigues
    static const BeepNote IDLE[] = {{660, 150}};                             // une note grave
    lastActivityMs = millis();
    setScreen(true, "alerte");
    uint32_t t0 = millis();
    if (a == dash::Alert::Permission) beep_play(PERMISSION, sizeof PERMISSION / sizeof PERMISSION[0]);
    else if (a == dash::Alert::Idle) beep_play(IDLE, sizeof IDLE / sizeof IDLE[0]);
    Serial.printf("Alerte : %s (bip %lu ms)\n",
                  a == dash::Alert::Permission ? "permission, 2 bips aigus" : "attente, 1 bip grave",
                  (unsigned long)(millis() - t0));
}

// Toucher, lu a chaque tour de loop() (reveil immediat) via l'horodatage
// d'activite que LVGL tient pour l'indev tactile (lu meme retroeclairage
// eteint) : pas de callback a poser sur des objets qui consomment les pressions.
// Seul un toucher recent compte : ms - inactive compare a lastActivityMs
// deborderait apres ~24,8 j sans toucher.
#define TOUCH_RECENT_MS 1500
static void pollTouch()
{
    bsp_display_lock(0);
    uint32_t inactive = lv_disp_get_inactive_time(nullptr);
    bsp_display_unlock();
    if (inactive >= TOUCH_RECENT_MS) return;
    uint32_t touchAt = millis() - inactive;
    if ((int32_t)(touchAt - lastActivityMs) > 0) {
        lastActivityMs = touchAt;
        setScreen(true, "toucher");
    }
}

// Hotes muets depuis plus de FORGET_AFTER_S (agent plante, serveur en veille) :
// oublies, pour que l'ecran puisse se mettre en veille et le bandeau disparaitre.
static void expireHosts()
{
    if (!clockValid()) return;
    int n = dashboard.expire(time(nullptr));
    if (n > 0) {
        Serial.printf("Hote oublie apres 1 h sans nouvelles (%d)\n", n);
        renderNeeded = true;
    }
}

// Veille, evaluee chaque seconde.
static void updateScreen()
{
    uint32_t ms = millis();
    // Disparition de la derniere session : le compte a rebours part de la,
    // pour ne pas eteindre d'un coup a la fin de la derniere session.
    static int lastCount = 0;
    int count = dashboard.sessionCount();
    if (lastCount > 0 && count == 0) lastActivityMs = ms;
    lastCount = count;
    bool on = dash::screenShouldBeOn(count > 0, ms, lastActivityMs, SCREEN_TIMEOUT_MS);
    setScreen(on, on ? "session listee" : "inactif");
}

static void handleMqtt(uint32_t ms)
{
    static uint32_t lastAttempt = 0;
    static uint32_t retryDelay = 0;  // 0 : tentative immediate
    static bool wifiWasUp = false;

    bool wifiUp = WiFi.status() == WL_CONNECTED;
    if (wifiUp != wifiWasUp) {
        wifiWasUp = wifiUp;
        if (wifiUp) retryDelay = 0;  // WiFi revenu : retenter tout de suite
    }
    if (mqttUp && !wifiUp) {
        // Sans WiFi, la socket TLS ne le verrait qu'au keepalive (jusqu'a 90 s).
        mqtt.disconnect();
        mqttUp = false;
        netStatus.mqtt = net::MqttPhase::Waiting;
        Serial.println("MQTT deconnecte (WiFi perdu)");
    }
    if (mqttUp) {
        if (mqtt.loop()) return;
        mqttUp = false;
        netStatus.mqtt = net::MqttPhase::ConnectFailed;
        netStatus.mqttRc = mqtt.state();
        netStatus.tlsError = 0;
        Serial.printf("MQTT deconnecte (rc=%d)\n", mqtt.state());
        retryDelay = MQTT_RETRY_MIN_MS;
        lastAttempt = ms;
    }
    if (!credentialsOk) {
        // PEM invalides : inutile de tenter, mais le rappeler pour un moniteur ouvert tard.
        static uint32_t lastReport = 0;
        if (ms - lastReport >= MQTT_RETRY_MAX_MS) {
            lastReport = ms;
            checkCredentials();
        }
        return;
    }
    if (!wifiUp || !clockValid()) return;  // TLS exige l'heure
    if (ms - lastAttempt < retryDelay) return;

    mqttUp = mqttConnect();
    if (mqttUp) {
        netStatus.mqtt = net::MqttPhase::Connected;
        retryDelay = MQTT_RETRY_MIN_MS;
    } else {
        retryDelay = retryDelay ? retryDelay * 2 : MQTT_RETRY_MIN_MS;
        if (retryDelay > MQTT_RETRY_MAX_MS) retryDelay = MQTT_RETRY_MAX_MS;
        Serial.printf("MQTT : nouvelle tentative dans %lu s\n", (unsigned long)(retryDelay / 1000));
    }
    lastAttempt = millis();  // la tentative elle-meme peut durer plusieurs secondes
}

// Partie de l'etat reseau lue chaque seconde ; les phases de scan/connexion
// sont posees par startWifi()/pollWifiScan(), celles de MQTT par handleMqtt().
static void refreshNetStatus(uint32_t ms)
{
    static uint32_t wifiUpSince = 0;
    bool up = WiFi.status() == WL_CONNECTED;
    bool wasUp = netStatus.wifi == net::WifiPhase::Connected;
    if (up && !wasUp) {
        wifiUpSince = ms;
        wifiDisconnectReason = 0;
        netStatus.wifi = net::WifiPhase::Connected;
    }
    if (!up && wasUp) netStatus.wifi = net::WifiPhase::Connecting;  // reconnexion du driver
    if (up) {
        strlcpy(netStatus.ssid, WiFi.SSID().c_str(), sizeof netStatus.ssid);
        strlcpy(netStatus.ip, WiFi.localIP().toString().c_str(), sizeof netStatus.ip);
        netStatus.rssi = WiFi.RSSI();
    }
    int reason = wifiDisconnectReason;
    netStatus.disconnectReason = reason;
    strlcpy(netStatus.disconnectReasonName,
            reason ? WiFi.disconnectReasonName((wifi_err_reason_t)reason) : "",
            sizeof netStatus.disconnectReasonName);
    netStatus.clockOk = clockValid();
    if (ntpSynced) netStatus.clockSource = net::ClockSource::Ntp;
    netStatus.clockWaitS = up ? (ms - wifiUpSince) / 1000 : 0;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nClaude_Dashboard - JC3248W535C");

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    // Le TWDT est deja demarre par le framework (CONFIG_ESP_TASK_WDT_INIT, 5 s) :
    // esp_task_wdt_init() seul echouerait (ESP_ERR_INVALID_STATE) et laisserait 5 s.
    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_init(&wdt_config);
    if (err != ESP_OK) Serial.printf("WDT : configuration echouee (%d)\n", err);
    esp_task_wdt_add(NULL);

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();
    beep_begin();

    bsp_display_lock(0);
    ui_create();
    ui_render(dashboard, time(nullptr), netStatus);
    bsp_display_unlock();
    printHeap("UI prete");

    // WiFi et NTP non bloquants : loop() suit la connexion (repli HTTPS si NTP filtre).
    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
            // ASSOC_LEAVE vient de notre propre disconnect() : il masquerait la vraie cause
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason != WIFI_REASON_ASSOC_LEAVE) wifiDisconnectReason = reason;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    startWifi(millis());
    sntp_set_time_sync_notification_cb([](struct timeval *) { ntpSynced = true; });
    configTzTime(TZ_PARIS, "pool.ntp.org", "time.google.com");

    credentialsOk = checkCredentials();
    if (!credentialsOk) netStatus.mqtt = net::MqttPhase::BadCredentials;
    tls.setCACert(DASH_MQTT_CA_CERT);
    tls.setCertificate(DASH_MQTT_CLIENT_CERT);
    tls.setPrivateKey(DASH_MQTT_CLIENT_KEY);
    tls.setConnectionTimeout(TCP_CONNECT_TIMEOUT_MS);
    tls.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);

    mqtt.setServer(DASH_MQTT_SERVER, DASH_MQTT_PORT);
    if (!mqtt.setBufferSize(MQTT_BUFFER_SIZE))
        Serial.printf("ERREUR : tampon MQTT de %d o non alloue (snapshots ignores)\n", MQTT_BUFFER_SIZE);
    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
    mqtt.setCallback(onMessage);

    lastActivityMs = millis();
    Serial.println("Setup termine");
}

void loop()
{
    esp_task_wdt_reset();
    uint32_t ms = millis();

    handleWifi(ms);
    handleClockFallback(ms);

    static bool ntpShown = false;
    if (!ntpShown && ntpSynced) {
        ntpShown = true;
        time_t now = time(nullptr);
        struct tm tm;
        localtime_r(&now, &tm);
        Serial.printf("NTP synchronise : %02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
    }

    handleMqtt(ms);

    if (renderNeeded) {
        renderNeeded = false;
        bsp_display_lock(0);
        ui_render(dashboard, time(nullptr), netStatus);
        bsp_display_unlock();
    }
    if (pendingAlert != dash::Alert::None) {
        dash::Alert a = pendingAlert;
        pendingAlert = dash::Alert::None;
        alertTriggered(a);  // bip synchrone (~0,4 s), hors verrou LVGL
    }

    pollTouch();

    static uint32_t lastTick = 0;
    if (ms - lastTick >= 1000) {
        lastTick = ms;
        expireHosts();
        refreshNetStatus(ms);
        bsp_display_lock(0);
        ui_tick(dashboard, time(nullptr), netStatus);
        bsp_display_unlock();
        updateScreen();
    }
    delay(20);
}
