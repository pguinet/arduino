/*
 * Transit_Tracker - JC3248W535C
 *
 * Prochains passages bus et trains via l'API PRIM Ile-de-France
 * Mobilites, regroupes dans une seule app a onglets. Chaque arret
 * peut etre un bus (StopPoint:Q) ou un train (StopArea:SP).
 *
 * Un onglet Meteo (Open-Meteo, sans cle API) complete l'ensemble, avec
 * un bandeau temperature permanent en haut des onglets transport. L'ecran
 * revient de lui-meme aux departs aux heures de pointe et a la meteo le
 * reste du temps.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * @dependencies LVGL 8.3.x, ArduinoJson, WiFi
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "credentials.h"
#include "prim_config.h"

#define LVGL_PORT_ROTATION_DEGREE (270)

// Colors
#define COLOR_BG        0x0f1419
#define COLOR_CARD      0x1c2733
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00d4ff
#define COLOR_LATE      0xf72585
#define COLOR_SOON      0xfca311
#define COLOR_NORMAL    0x4cc9f0
#define COLOR_DIMMED    0x888888
#define COLOR_BUS_BADGE 0x4cc9f0

// Meteo (Open-Meteo : API publique, aucune cle requise)
#define WEATHER_LAT       "49.021"
#define WEATHER_LON       "2.363"
#define WEATHER_CITY      "Ezanville"
#define WEATHER_INTERVAL  900000UL   // 15 min
#define WEATHER_RETRY     60000UL    // nouvel essai apres echec
#define WEATHER_SLOTS     4          // creneaux horaires affiches
#define WEATHER_SLOT_STEP 3          // heures entre deux creneaux

// Watchdog
#define WDT_TIMEOUT_SEC     30

// Update intervals
#define INTERVAL_RUSH_HOUR  30000
#define INTERVAL_NORMAL     120000

// Screen off (23h-5h, OK pour bus et trains)
#define SCREEN_OFF_START    23
#define SCREEN_OFF_END      5

// Bus : pas de service la nuit
#define BUS_NIGHT_START_HOUR 20
#define BUS_NIGHT_END_HOUR   6

#define MAX_DEPARTURES 5
#define MAX_STOPS      3
#define AUTO_RETURN_DELAY 120000

// Onglets : 0..MAX_STOPS-1 = arrets, MAX_STOPS = meteo
#define TAB_WEATHER MAX_STOPS

enum StopType { TYPE_BUS, TYPE_TRAIN };

struct StopConfig {
    StopType    type;
    const char* monitoringRef;   // URL-encoded (ex: STIF%3AStopPoint%3AQ%3A413248%3A)
    const char* name;
};

// Configuration des arrets (modifier ici pour ajouter/changer un arret)
static StopConfig stops[MAX_STOPS] = {
    {TYPE_BUS,   "STIF%3AStopPoint%3AQ%3A413248%3A", "Foch"},
    {TYPE_BUS,   "STIF%3AStopPoint%3AQ%3A14305%3A",  "Eglise"},
    {TYPE_TRAIN, "STIF%3AStopArea%3ASP%3A43073%3A",  "Ecouen"},
};

static int currentStop = 0;   // arret transport affiche (onglets 0..MAX_STOPS-1)
static int currentTab = 0;    // onglet actif, TAB_WEATHER pour la meteo
static unsigned long stopSwitchTime = 0;

// Mapping lignes : codes PRIM -> nom affiche + couleur badge
struct LineInfo {
    const char* code;
    const char* name;
    uint32_t    color;
    uint32_t    textColor;
};

static const LineInfo lineInfos[] = {
    // Bus locaux
    {"C01252", "269",   0xFF5A00, 0x000000},  // orange
    {"C02462", "1517",  0x8D653D, 0xFFFFFF},  // marron
    // Transilien
    {"C01737", "H",     0x6E1E78, 0xFFFFFF},
    {"C01739", "J",     0xCD8B00, 0xFFFFFF},
    {"C01738", "K",     0xA0006E, 0xFFFFFF},
    {"C01740", "L",     0x8D5E2A, 0xFFFFFF},
    {"C01741", "N",     0x00A88F, 0xFFFFFF},
    {"C01736", "P",     0xF3D03E, 0x000000},
    {"C01735", "R",     0xF49FB6, 0x000000},
    {"C01744", "U",     0xD41367, 0xFFFFFF},
    // RER
    {"C01742", "RER A", 0xE2231A, 0xFFFFFF},
    {"C01743", "RER B", 0x4296D2, 0xFFFFFF},
    {"C01727", "RER C", 0xF99D1C, 0x000000},
    {"C01728", "RER D", 0x008B5B, 0xFFFFFF},
    {"C01729", "RER E", 0xB94E9A, 0xFFFFFF},
    {nullptr, nullptr, 0, 0}
};

static const LineInfo* getLineInfo(const char* lineRef)
{
    if (!lineRef) return nullptr;
    const char* start = strstr(lineRef, "::");
    if (!start) return nullptr;
    start += 2;
    for (int i = 0; lineInfos[i].code != nullptr; i++) {
        if (strstr(start, lineInfos[i].code) == start) {
            return &lineInfos[i];
        }
    }
    return nullptr;
}


// ---------------------------------------------------------------------------
// Meteo (Open-Meteo)
// ---------------------------------------------------------------------------

enum WeatherIconKind {
    ICON_SUN, ICON_SUN_CLOUD, ICON_CLOUD, ICON_FOG, ICON_RAIN, ICON_SNOW, ICON_STORM
};

struct WeatherSlot {
    int             hour;
    int             temp;
    WeatherIconKind icon;
};

static struct {
    bool            valid;
    int             temp;
    int             feels;
    int             wind;         // km/h
    int             humidity;     // %
    int             tempMin;
    int             tempMax;
    float           precipSum;    // mm cumules sur la journee
    WeatherIconKind icon;
    const char*     desc;
    WeatherSlot     slots[WEATHER_SLOTS];
    int             slotCount;
    char            updateTime[8];
    char            errorMsg[40];
} weather = {};

static unsigned long lastWeatherUpdate = 0;
static bool weatherRequested = false;

// Codes WMO -> pictogramme + libelle. Les libelles sont sans accents : les
// fontes Montserrat integrees a LVGL ne couvrent que l'ASCII (+ le degre).
static void weatherFromCode(int code, WeatherIconKind* icon, const char** desc)
{
    switch (code) {
        case 0:  *icon = ICON_SUN;       *desc = "Ensoleille";          break;
        case 1:  *icon = ICON_SUN_CLOUD; *desc = "Peu nuageux";         break;
        case 2:  *icon = ICON_SUN_CLOUD; *desc = "Nuages epars";        break;
        case 3:  *icon = ICON_CLOUD;     *desc = "Couvert";             break;
        case 45:
        case 48: *icon = ICON_FOG;       *desc = "Brouillard";          break;
        case 51:
        case 53:
        case 55: *icon = ICON_RAIN;      *desc = "Bruine";              break;
        case 56:
        case 57: *icon = ICON_RAIN;      *desc = "Bruine verglacante";  break;
        case 61: *icon = ICON_RAIN;      *desc = "Pluie faible";        break;
        case 63: *icon = ICON_RAIN;      *desc = "Pluie";               break;
        case 65: *icon = ICON_RAIN;      *desc = "Pluie forte";         break;
        case 66:
        case 67: *icon = ICON_RAIN;      *desc = "Pluie verglacante";   break;
        case 71: *icon = ICON_SNOW;      *desc = "Neige faible";        break;
        case 73: *icon = ICON_SNOW;      *desc = "Neige";               break;
        case 75: *icon = ICON_SNOW;      *desc = "Neige forte";         break;
        case 77: *icon = ICON_SNOW;      *desc = "Grains de neige";     break;
        case 80:
        case 81: *icon = ICON_RAIN;      *desc = "Averses";             break;
        case 82: *icon = ICON_RAIN;      *desc = "Fortes averses";      break;
        case 85:
        case 86: *icon = ICON_SNOW;      *desc = "Averses de neige";    break;
        case 95: *icon = ICON_STORM;     *desc = "Orage";               break;
        case 96:
        case 99: *icon = ICON_STORM;     *desc = "Orage et grele";      break;
        default: *icon = ICON_CLOUD;     *desc = "---";                 break;
    }
}

// Pictogramme compose de primitives LVGL : aucune fonte d'icones meteo n'est
// disponible, on assemble donc un soleil, un nuage et 3 gouttes / barres.
struct WeatherIcon {
    lv_obj_t* cont;
    lv_obj_t* sun;
    lv_obj_t* body;
    lv_obj_t* puffL;
    lv_obj_t* puffR;
    lv_obj_t* bits[3];
    int       size;
};

static lv_obj_t* weatherIconPart(lv_obj_t* parent)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void weatherIconCreate(WeatherIcon* ic, lv_obj_t* parent, int size)
{
    ic->size = size;

    ic->cont = lv_obj_create(parent);
    lv_obj_remove_style_all(ic->cont);
    lv_obj_set_size(ic->cont, size, size);
    lv_obj_clear_flag(ic->cont, LV_OBJ_FLAG_SCROLLABLE);

    // Ordre de creation = ordre d'empilement : le soleil passe derriere le nuage
    ic->sun   = weatherIconPart(ic->cont);
    ic->body  = weatherIconPart(ic->cont);
    ic->puffL = weatherIconPart(ic->cont);
    ic->puffR = weatherIconPart(ic->cont);
    for (int i = 0; i < 3; i++) ic->bits[i] = weatherIconPart(ic->cont);
}

static void weatherIconSet(WeatherIcon* ic, WeatherIconKind kind)
{
    const int S = ic->size;

    lv_obj_t* parts[] = {ic->sun, ic->body, ic->puffL, ic->puffR,
                         ic->bits[0], ic->bits[1], ic->bits[2]};
    for (unsigned i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        lv_obj_add_flag(parts[i], LV_OBJ_FLAG_HIDDEN);
    }

    bool wet = (kind == ICON_RAIN || kind == ICON_SNOW || kind == ICON_STORM);

    if (kind == ICON_SUN || kind == ICON_SUN_CLOUD) {
        bool alone = (kind == ICON_SUN);
        int d = alone ? (S * 62) / 100 : (S * 42) / 100;
        lv_obj_set_size(ic->sun, d, d);
        lv_obj_set_style_bg_color(ic->sun, lv_color_hex(0xFFC93C), 0);
        lv_obj_align(ic->sun, LV_ALIGN_CENTER,
                     alone ? 0 : (S * 22) / 100,
                     alone ? 0 : -(S * 24) / 100);
        lv_obj_clear_flag(ic->sun, LV_OBJ_FLAG_HIDDEN);
    }

    if (kind != ICON_SUN && kind != ICON_FOG) {
        uint32_t col = 0xD8DEE9;
        if (kind == ICON_CLOUD || kind == ICON_RAIN || kind == ICON_SNOW) col = 0xAEB8C4;
        if (kind == ICON_STORM) col = 0x6B7683;

        // Les variantes avec precipitations remontent le nuage
        int cy = wet ? -(S * 12) / 100 : (S * 2) / 100;

        lv_obj_set_size(ic->body, (S * 80) / 100, (S * 30) / 100);
        lv_obj_align(ic->body, LV_ALIGN_CENTER, 0, cy);

        lv_obj_set_size(ic->puffL, (S * 40) / 100, (S * 40) / 100);
        lv_obj_align(ic->puffL, LV_ALIGN_CENTER, -(S * 14) / 100, cy - (S * 14) / 100);

        lv_obj_set_size(ic->puffR, (S * 28) / 100, (S * 28) / 100);
        lv_obj_align(ic->puffR, LV_ALIGN_CENTER, (S * 17) / 100, cy - (S * 13) / 100);

        lv_obj_t* cloud[] = {ic->body, ic->puffL, ic->puffR};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_bg_color(cloud[i], lv_color_hex(col), 0);
            lv_obj_clear_flag(cloud[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (wet) {
        bool snow = (kind == ICON_SNOW);
        int w = snow ? LV_MAX(3, (S * 13) / 100) : LV_MAX(2, (S * 8) / 100);
        int h = snow ? w : LV_MAX(5, (S * 20) / 100);
        uint32_t col = snow                   ? 0xFFFFFF
                     : (kind == ICON_STORM)   ? 0xFCA311
                                              : 0x4CC9F0;
        const int dx[3] = {-(S * 22) / 100, 0, (S * 22) / 100};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_size(ic->bits[i], w, h);
            lv_obj_set_style_bg_color(ic->bits[i], lv_color_hex(col), 0);
            lv_obj_align(ic->bits[i], LV_ALIGN_CENTER, dx[i], (S * 28) / 100);
            lv_obj_clear_flag(ic->bits[i], LV_OBJ_FLAG_HIDDEN);
        }
    } else if (kind == ICON_FOG) {
        int w = (S * 66) / 100;
        int h = LV_MAX(2, (S * 8) / 100);
        const int dy[3] = {-(S * 20) / 100, 0, (S * 20) / 100};
        for (int i = 0; i < 3; i++) {
            lv_obj_set_size(ic->bits[i], (i == 1) ? w : (w * 80) / 100, h);
            lv_obj_set_style_bg_color(ic->bits[i], lv_color_hex(0xAEB8C4), 0);
            lv_obj_align(ic->bits[i], LV_ALIGN_CENTER, 0, dy[i]);
            lv_obj_clear_flag(ic->bits[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Departure data (champs train ignores pour bus)
struct Departure {
    int       minutesLeft;
    int       delayMinutes;        // train only
    char      lineName[8];
    uint32_t  lineColor;
    uint32_t  lineTextColor;
    char      mission[6];          // train only ("" si bus)
    char      destination[40];
    char      platform[6];         // train only ("" si bus)
    bool      atStop;
};

static Departure departures[MAX_DEPARTURES];
static int departureCount = 0;
static bool dataValid = false;
static unsigned long lastUpdate = 0;
static char lastUpdateTime[10] = "--:--";
static char errorMsg[60] = "";
static bool busNightMode = false;
static bool screenOff = false;
static bool fetching = false;
static unsigned long fetchStartTime = 0;
#define FETCH_TIMEOUT_MS 20000
static bool manualRefreshRequested = false;
static bool uiRefreshRequested = false;
static int consecutiveErrors = 0;

// UI elements
static lv_obj_t *label_stop;
static lv_obj_t *label_status;
static lv_obj_t *label_update_time;
static lv_obj_t *btn_refresh;
static lv_obj_t *btn_stops[MAX_STOPS];
static lv_obj_t *spinner;
static lv_obj_t *cont_departures;
static lv_obj_t *rows[MAX_DEPARTURES];
static lv_obj_t *line_badges[MAX_DEPARTURES];
static lv_obj_t *labels_line[MAX_DEPARTURES];
static lv_obj_t *labels_time[MAX_DEPARTURES];
static lv_obj_t *labels_mission[MAX_DEPARTURES];
static lv_obj_t *labels_dest[MAX_DEPARTURES];
static lv_obj_t *labels_right[MAX_DEPARTURES];   // platform (train)
static lv_obj_t *night_overlay;

// UI meteo
static lv_obj_t *btn_weather;
static lv_obj_t *hdr_weather;            // bandeau compact de l'en-tete
static WeatherIcon hdr_icon;
static lv_obj_t *label_hdr_temp;
static lv_obj_t *label_hdr_feels;
static lv_obj_t *cont_weather;           // panneau de l'onglet dedie
static WeatherIcon big_icon;
static lv_obj_t *label_w_desc;
static lv_obj_t *label_w_temp;
static lv_obj_t *label_w_feels;
static lv_obj_t *label_w_minmax;
static lv_obj_t *label_w_wind;
static lv_obj_t *label_w_precip;
static lv_obj_t *label_w_hum;
static lv_obj_t *slot_cards[WEATHER_SLOTS];
static WeatherIcon slot_icons[WEATHER_SLOTS];
static lv_obj_t *label_slot_hour[WEATHER_SLOTS];
static lv_obj_t *label_slot_temp[WEATHER_SLOTS];

static WiFiClientSecure client;
static WiFiClientSecure clientWeather;

static bool isRushHour()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour, minute = ti->tm_min;
    if ((hour == 6 && minute >= 30) || (hour >= 7 && hour < 9))  return true;
    if (hour >= 17 && hour < 20)                                  return true;
    return false;
}

static unsigned long getUpdateInterval()
{
    return isRushHour() ? INTERVAL_RUSH_HOUR : INTERVAL_NORMAL;
}

// Onglet de repli : les departs aux heures de pointe, la meteo le reste du
// temps. L'affichage y revient seul apres AUTO_RETURN_DELAY, et bascule aussi
// quand l'heure change si personne n'a touche l'ecran entre-temps.
static int defaultTab()
{
    return isRushHour() ? 0 : TAB_WEATHER;
}

static bool isBusNightMode()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    return (hour >= BUS_NIGHT_START_HOUR || hour < BUS_NIGHT_END_HOUR);
}

static bool isScreenOffTime()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    return (hour >= SCREEN_OFF_START || hour < SCREEN_OFF_END);
}

static time_t parseIso8601(const char* str)
{
    if (!str) return 0;
    int year, month, day, hour, minute, second;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) return 0;
    int days = (year - 1970) * 365 + (year - 1969) / 4;
    int monthDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += monthDays[month - 1] + day - 1;
    if (month > 2 && (year % 4 == 0)) days++;
    return (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
}

static void fetchDepartures()
{
    if (fetching) return;

    fetching = true;
    fetchStartTime = millis();

    bsp_display_lock(0);
    lv_label_set_text(label_status, "Chargement...");
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, skipping fetch");
        strcpy(errorMsg, "WiFi deconnecte");
        dataValid = false;
        fetching = false;
        return;
    }

    StopConfig& stop = stops[currentStop];

    client.setInsecure();
    client.setTimeout(10);

    HTTPClient https;
    String url = "https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=";
    url += stop.monitoringRef;

    Serial.printf("Fetching %s (%s): %s\n",
        stop.name, stop.type == TYPE_TRAIN ? "train" : "bus", url.c_str());
    https.setTimeout(10000);
    esp_task_wdt_reset();

    if (https.begin(client, url)) {
        https.addHeader("Accept", "application/json");
        https.addHeader("apikey", PRIM_API_KEY);

        int httpCode = https.GET();
        esp_task_wdt_reset();
        Serial.printf("HTTP code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();

            int start = payload.indexOf('{');
            if (start >= 0) {
                JsonDocument filter;
                JsonObject fv = filter["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"].add<JsonObject>();
                fv["MonitoredVehicleJourney"]["LineRef"]["value"] = true;
                fv["MonitoredVehicleJourney"]["DestinationName"][0]["value"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["VehicleAtStop"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["DestinationDisplay"][0]["value"] = true;
                if (stop.type == TYPE_TRAIN) {
                    fv["MonitoredVehicleJourney"]["JourneyNote"][0]["value"] = true;
                    fv["MonitoredVehicleJourney"]["MonitoredCall"]["AimedDepartureTime"] = true;
                    fv["MonitoredVehicleJourney"]["MonitoredCall"]["DeparturePlatformName"]["value"] = true;
                    fv["MonitoredVehicleJourney"]["MonitoredCall"]["ArrivalPlatformName"]["value"] = true;
                }

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload.c_str() + start,
                    DeserializationOption::Filter(filter),
                    DeserializationOption::NestingLimit(15));

                payload = "";

                if (!error) {
                    JsonArray visits = doc["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"];

                    departureCount = 0;
                    time_t now = time(nullptr);

                    for (JsonObject visit : visits) {
                        if (departureCount >= MAX_DEPARTURES) break;

                        JsonObject mvj = visit["MonitoredVehicleJourney"];
                        JsonObject call = mvj["MonitoredCall"];

                        const char* lineRef = mvj["LineRef"]["value"];
                        const char* expectedTime = call["ExpectedDepartureTime"];
                        const char* destName = mvj["DestinationName"][0]["value"];
                        const char* destDisplay = call["DestinationDisplay"][0]["value"];
                        bool atStop = call["VehicleAtStop"] | false;

                        if (!expectedTime) continue;

                        time_t depTime = parseIso8601(expectedTime);
                        if (depTime == 0) continue;

                        int minutes = (depTime - now) / 60;
                        if (minutes < -1) continue;

                        Departure& d = departures[departureCount];
                        d.minutesLeft = minutes;
                        d.atStop = atStop;
                        d.delayMinutes = 0;
                        d.mission[0] = '\0';
                        d.platform[0] = '\0';

                        // Ligne
                        const LineInfo* li = getLineInfo(lineRef);
                        if (li) {
                            strncpy(d.lineName, li->name, sizeof(d.lineName) - 1);
                            d.lineName[sizeof(d.lineName) - 1] = '\0';
                            d.lineColor = li->color;
                            d.lineTextColor = li->textColor;
                        } else {
                            // Fallback : code tronque
                            d.lineName[0] = '?'; d.lineName[1] = '\0';
                            d.lineColor = 0x444444;
                            d.lineTextColor = 0xFFFFFF;
                        }

                        // Destination
                        const char* dest = (destDisplay && strlen(destDisplay) > 0) ? destDisplay : destName;
                        if (dest) {
                            strncpy(d.destination, dest, sizeof(d.destination) - 1);
                            d.destination[sizeof(d.destination) - 1] = '\0';
                        } else {
                            d.destination[0] = '\0';
                        }

                        // Specifique train : mission, quai, retard
                        if (stop.type == TYPE_TRAIN) {
                            const char* journeyNote = mvj["JourneyNote"][0]["value"];
                            const char* platform = call["DeparturePlatformName"]["value"]
                                                 | call["ArrivalPlatformName"]["value"];
                            const char* aimedTime = call["AimedDepartureTime"];

                            if (journeyNote && strlen(journeyNote) > 0 && strlen(journeyNote) <= 5) {
                                strncpy(d.mission, journeyNote, sizeof(d.mission) - 1);
                                d.mission[sizeof(d.mission) - 1] = '\0';
                            }
                            if (platform && strlen(platform) > 0) {
                                if (platform[0] >= '0' && platform[0] <= '9') {
                                    snprintf(d.platform, sizeof(d.platform), "V%s", platform);
                                } else {
                                    strncpy(d.platform, platform, sizeof(d.platform) - 1);
                                    d.platform[sizeof(d.platform) - 1] = '\0';
                                }
                            }
                            if (aimedTime) {
                                time_t aimedT = parseIso8601(aimedTime);
                                if (aimedT > 0) {
                                    d.delayMinutes = (depTime - aimedT) / 60;
                                }
                            }
                        }

                        departureCount++;
                    }

                    dataValid = true;
                    strcpy(errorMsg, "");
                    consecutiveErrors = 0;

                    struct tm* ti = localtime(&now);
                    sprintf(lastUpdateTime, "%02d:%02d", ti->tm_hour, ti->tm_min);
                } else {
                    sprintf(errorMsg, "JSON: %s", error.c_str());
                    dataValid = false;
                    consecutiveErrors++;
                }
            } else {
                strcpy(errorMsg, "No JSON");
                dataValid = false;
                consecutiveErrors++;
            }
        } else {
            sprintf(errorMsg, "HTTP %d", httpCode);
            dataValid = false;
            consecutiveErrors++;
        }
        https.end();
    } else {
        strcpy(errorMsg, "Connexion impossible");
        dataValid = false;
        consecutiveErrors++;
    }

    lastUpdate = millis();
    fetching = false;
}

static void fetchWeather()
{
    if (fetching) return;
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(weather.errorMsg, "WiFi deconnecte", sizeof(weather.errorMsg) - 1);
        return;
    }

    fetching = true;
    fetchStartTime = millis();

    if (currentTab == TAB_WEATHER) {
        bsp_display_lock(0);
        lv_label_set_text(label_status, "Chargement meteo...");
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }

    clientWeather.setInsecure();
    clientWeather.setTimeout(10);

    HTTPClient https;
    String url = "https://api.open-meteo.com/v1/forecast"
                 "?latitude=" WEATHER_LAT
                 "&longitude=" WEATHER_LON
                 "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
                 "weather_code,wind_speed_10m"
                 "&hourly=temperature_2m,weather_code"
                 "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum"
                 "&timezone=Europe%2FParis&forecast_days=2";

    Serial.printf("Fetching meteo: %s\n", url.c_str());
    https.setTimeout(10000);
    esp_task_wdt_reset();

    if (https.begin(clientWeather, url)) {
        https.addHeader("Accept", "application/json");
        https.setReuse(false);

        int httpCode = https.GET();
        esp_task_wdt_reset();
        Serial.printf("Meteo HTTP code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();

            JsonDocument filter;
            filter["current"]["temperature_2m"] = true;
            filter["current"]["apparent_temperature"] = true;
            filter["current"]["relative_humidity_2m"] = true;
            filter["current"]["weather_code"] = true;
            filter["current"]["wind_speed_10m"] = true;
            filter["hourly"]["temperature_2m"][0] = true;
            filter["hourly"]["weather_code"][0] = true;
            filter["daily"]["temperature_2m_max"][0] = true;
            filter["daily"]["temperature_2m_min"][0] = true;
            filter["daily"]["precipitation_sum"][0] = true;

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload,
                DeserializationOption::Filter(filter));
            payload = "";

            if (!error) {
                JsonObject cur = doc["current"];
                weather.temp     = lroundf(cur["temperature_2m"] | 0.0f);
                weather.feels    = lroundf(cur["apparent_temperature"] | 0.0f);
                weather.humidity = cur["relative_humidity_2m"] | 0;
                weather.wind     = lroundf(cur["wind_speed_10m"] | 0.0f);
                weatherFromCode(cur["weather_code"] | -1, &weather.icon, &weather.desc);

                JsonObject day = doc["daily"];
                weather.tempMax   = lroundf(day["temperature_2m_max"][0] | 0.0f);
                weather.tempMin   = lroundf(day["temperature_2m_min"][0] | 0.0f);
                weather.precipSum = day["precipitation_sum"][0] | 0.0f;

                // L'API renvoie les heures locales a partir de 00h00 aujourd'hui
                // (timezone=Europe/Paris) : l'index vaut donc directement l'heure.
                JsonArray hTemp = doc["hourly"]["temperature_2m"];
                JsonArray hCode = doc["hourly"]["weather_code"];

                time_t now = time(nullptr);
                struct tm* ti = localtime(&now);

                weather.slotCount = 0;
                // Sans heure fiable, l'index horaire n'aurait aucun sens
                for (int k = 0; k < WEATHER_SLOTS && now >= 1704067200; k++) {
                    int idx = ti->tm_hour + 1 + k * WEATHER_SLOT_STEP;
                    if (idx >= (int)hTemp.size()) break;

                    WeatherSlot& sl = weather.slots[weather.slotCount];
                    const char* ignored;
                    sl.hour = idx % 24;
                    sl.temp = lroundf(hTemp[idx] | 0.0f);
                    weatherFromCode(hCode[idx] | -1, &sl.icon, &ignored);
                    weather.slotCount++;
                }

                weather.valid = true;
                weather.errorMsg[0] = '\0';
                sprintf(weather.updateTime, "%02d:%02d", ti->tm_hour, ti->tm_min);
            } else {
                snprintf(weather.errorMsg, sizeof(weather.errorMsg), "Meteo JSON: %s", error.c_str());
                weather.valid = false;
            }
        } else {
            snprintf(weather.errorMsg, sizeof(weather.errorMsg), "Meteo HTTP %d", httpCode);
            weather.valid = false;
        }
        https.end();
    } else {
        strncpy(weather.errorMsg, "Meteo injoignable", sizeof(weather.errorMsg) - 1);
        weather.valid = false;
    }

    // Liberer le contexte TLS : garder la socket ouverte (keep-alive par
    // defaut dans HTTPClient) laisserait deux sessions mbedTLS vivantes en
    // RAM interne, et le handshake PRIM echouerait faute de heap.
    clientWeather.stop();

    lastWeatherUpdate = millis();
    fetching = false;
}

static void formatTimeLeft(int minutes, bool atStop, char* out, size_t outSize, lv_color_t* color)
{
    if (atStop) {
        strncpy(out, "A QUAI", outSize);
        *color = lv_color_hex(COLOR_LATE);
    } else if (minutes <= 0) {
        strncpy(out, "Imminent", outSize);
        *color = lv_color_hex(COLOR_LATE);
    } else if (minutes <= 6) {
        snprintf(out, outSize, "%d min", minutes);
        *color = lv_color_hex(COLOR_LATE);
    } else if (minutes <= 10) {
        snprintf(out, outSize, "%d min", minutes);
        *color = lv_color_hex(COLOR_SOON);
    } else if (minutes < 60) {
        snprintf(out, outSize, "%d min", minutes);
        *color = lv_color_hex(COLOR_NORMAL);
    } else {
        int h = minutes / 60, m = minutes % 60;
        snprintf(out, outSize, "%dh%02d", h, m);
        *color = lv_color_hex(COLOR_NORMAL);
    }
}

static void updateHeaderWeather()
{
    char buf[24];

    if (weather.valid) {
        weatherIconSet(&hdr_icon, weather.icon);
        lv_obj_clear_flag(hdr_icon.cont, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%d°", weather.temp);
        lv_label_set_text(label_hdr_temp, buf);
        snprintf(buf, sizeof(buf), "ress. %d°", weather.feels);
        lv_label_set_text(label_hdr_feels, buf);
    } else {
        lv_obj_add_flag(hdr_icon.cont, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_hdr_temp, "--°");
        lv_label_set_text(label_hdr_feels, "");
    }
}

static void updateWeatherPanel()
{
    char buf[32];

    if (!weather.valid) {
        weatherIconSet(&big_icon, ICON_CLOUD);
        lv_label_set_text(label_w_desc, weather.errorMsg[0] ? weather.errorMsg : "Indisponible");
        lv_label_set_text(label_w_temp, "--°C");
        lv_label_set_text(label_w_feels, "");
        lv_label_set_text(label_w_minmax, "");
        lv_label_set_text(label_w_wind, "");
        lv_label_set_text(label_w_precip, "");
        lv_label_set_text(label_w_hum, "");
        for (int i = 0; i < WEATHER_SLOTS; i++) {
            lv_obj_add_flag(slot_cards[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    weatherIconSet(&big_icon, weather.icon);
    lv_label_set_text(label_w_desc, weather.desc ? weather.desc : "---");

    snprintf(buf, sizeof(buf), "%d°C", weather.temp);
    lv_label_set_text(label_w_temp, buf);
    snprintf(buf, sizeof(buf), "ressenti %d°", weather.feels);
    lv_label_set_text(label_w_feels, buf);
    snprintf(buf, sizeof(buf), "min %d°   max %d°", weather.tempMin, weather.tempMax);
    lv_label_set_text(label_w_minmax, buf);

    snprintf(buf, sizeof(buf), "Vent  %d km/h", weather.wind);
    lv_label_set_text(label_w_wind, buf);
    snprintf(buf, sizeof(buf), "Pluie  %.1f mm", weather.precipSum);
    lv_label_set_text(label_w_precip, buf);
    snprintf(buf, sizeof(buf), "Humidite  %d %%", weather.humidity);
    lv_label_set_text(label_w_hum, buf);

    for (int i = 0; i < WEATHER_SLOTS; i++) {
        if (i < weather.slotCount) {
            snprintf(buf, sizeof(buf), "%dh", weather.slots[i].hour);
            lv_label_set_text(label_slot_hour[i], buf);
            snprintf(buf, sizeof(buf), "%d°", weather.slots[i].temp);
            lv_label_set_text(label_slot_temp[i], buf);
            weatherIconSet(&slot_icons[i], weather.slots[i].icon);
            lv_obj_clear_flag(slot_cards[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(slot_cards[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void updateUI()
{
    bsp_display_lock(0);

    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);

    char buf[64];
    updateHeaderWeather();

    // Onglet meteo : on masque la liste des departs et le bandeau compact
    if (currentTab == TAB_WEATHER) {
        lv_obj_add_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cont_departures, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hdr_weather, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(cont_weather, LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(label_stop, LV_SYMBOL_GPS " " WEATHER_CITY);
        updateWeatherPanel();
        lv_label_set_text(label_status, weather.valid ? "Open-Meteo" : "Meteo indisponible");
        snprintf(buf, sizeof(buf), "MAJ: %s",
                 weather.updateTime[0] ? weather.updateTime : "--:--");
        lv_label_set_text(label_update_time, buf);

        bsp_display_unlock();
        return;
    }

    lv_obj_add_flag(cont_weather, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cont_departures, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hdr_weather, LV_OBJ_FLAG_HIDDEN);

    StopConfig& stop = stops[currentStop];

    // Header
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %s", stop.name);
    lv_label_set_text(label_stop, buf);

    // Night mode overlay : seulement pour les bus
    bool showNight = (stop.type == TYPE_BUS) && busNightMode;
    if (showNight) {
        lv_obj_clear_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_status, "Mode veille bus (06h-20h)");
        bsp_display_unlock();
        return;
    }
    lv_obj_add_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);

    // Status
    if (!dataValid) {
        if (strlen(errorMsg) > 0) lv_label_set_text(label_status, errorMsg);
    } else if (departureCount == 0) {
        lv_label_set_text(label_status, stop.type == TYPE_TRAIN ? "Aucun train prevu" : "Aucun bus prevu");
    } else {
        snprintf(buf, sizeof(buf), "%d %s%s", departureCount,
            stop.type == TYPE_TRAIN ? "train" : "passage",
            departureCount > 1 ? "s" : "");
        lv_label_set_text(label_status, buf);
    }

    snprintf(buf, sizeof(buf), "MAJ: %s", lastUpdateTime);
    lv_label_set_text(label_update_time, buf);

    // Rows
    for (int i = 0; i < MAX_DEPARTURES; i++) {
        if (i < departureCount && dataValid) {
            Departure& d = departures[i];

            // Badge ligne
            lv_obj_set_style_bg_color(line_badges[i], lv_color_hex(d.lineColor), 0);
            lv_label_set_text(labels_line[i], d.lineName);
            lv_obj_set_style_text_color(labels_line[i], lv_color_hex(d.lineTextColor), 0);

            // Temps restant (commun bus + train)
            char timeBuf[16];
            lv_color_t timeColor;
            formatTimeLeft(d.minutesLeft, d.atStop, timeBuf, sizeof(timeBuf), &timeColor);
            lv_label_set_text(labels_time[i], timeBuf);
            lv_obj_set_style_text_color(labels_time[i], timeColor, 0);

            // Mission (train only)
            if (stop.type == TYPE_TRAIN && d.mission[0]) {
                lv_label_set_text(labels_mission[i], d.mission);
                lv_obj_clear_flag(labels_mission[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(labels_mission[i], LV_OBJ_FLAG_HIDDEN);
            }

            // Destination
            lv_label_set_text(labels_dest[i], d.destination);

            // Slot droite : voie + retard pour train, vide pour bus
            if (stop.type == TYPE_TRAIN) {
                char rightBuf[16];
                if (d.delayMinutes > 0 && d.platform[0]) {
                    snprintf(rightBuf, sizeof(rightBuf), "+%d %s", d.delayMinutes, d.platform);
                } else if (d.delayMinutes > 0) {
                    snprintf(rightBuf, sizeof(rightBuf), "+%d", d.delayMinutes);
                } else {
                    strncpy(rightBuf, d.platform, sizeof(rightBuf));
                    rightBuf[sizeof(rightBuf) - 1] = '\0';
                }
                lv_label_set_text(labels_right[i], rightBuf);
                lv_obj_clear_flag(labels_right[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(labels_right[i], LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    bsp_display_unlock();
}

static void setTabStyle(lv_obj_t* btn, bool active)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(active ? COLOR_ACCENT : COLOR_CARD), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0),
                                lv_color_hex(active ? 0x000000 : COLOR_TEXT), 0);
}

static void updateStopButtons()
{
    bsp_display_lock(0);
    for (int i = 0; i < MAX_STOPS; i++) {
        setTabStyle(btn_stops[i], currentTab == i);
    }
    setTabStyle(btn_weather, currentTab == TAB_WEATHER);
    bsp_display_unlock();
}

static void btn_refresh_cb(lv_event_t *e)
{
    if (fetching) return;
    if (currentTab == TAB_WEATHER) weatherRequested = true;
    else                           manualRefreshRequested = true;
}

static void btn_stop_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == currentTab || fetching) return;

    currentTab = idx;
    if (idx != TAB_WEATHER) {
        currentStop = idx;
        manualRefreshRequested = true;
    }
    stopSwitchTime = (idx != defaultTab()) ? millis() : 0;
    updateStopButtons();
    uiRefreshRequested = true;
}

static void createUI()
{
    bsp_display_lock(0);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BG), 0);

    // Title bar
    lv_obj_t *title_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, 480, 50);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(title_bar, 8, 0);

    // Onglets : 3 arrets + meteo (4 x 95 + 3 x 5 = 395, le refresh en prend 60)
    int btnW = 95, btnH = 35;
    for (int i = 0; i < MAX_STOPS; i++) {
        btn_stops[i] = lv_btn_create(title_bar);
        lv_obj_set_size(btn_stops[i], btnW, btnH);
        lv_obj_align(btn_stops[i], LV_ALIGN_LEFT_MID, i * (btnW + 5), 0);
        lv_obj_add_event_cb(btn_stops[i], btn_stop_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn_stops[i]);
        lv_label_set_text(lbl, stops[i].name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    }

    btn_weather = lv_btn_create(title_bar);
    lv_obj_set_size(btn_weather, btnW, btnH);
    lv_obj_align(btn_weather, LV_ALIGN_LEFT_MID, MAX_STOPS * (btnW + 5), 0);
    lv_obj_add_event_cb(btn_weather, btn_stop_cb, LV_EVENT_CLICKED, (void*)(intptr_t)TAB_WEATHER);

    lv_obj_t *lbl_weather = lv_label_create(btn_weather);
    lv_label_set_text(lbl_weather, "Meteo");
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_weather);

    // Refresh button
    btn_refresh = lv_btn_create(title_bar);
    lv_obj_set_size(btn_refresh, 60, btnH);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(btn_refresh, btn_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_label = lv_label_create(btn_refresh);
    lv_label_set_text(btn_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(btn_label);

    spinner = lv_spinner_create(title_bar, 1000, 60);
    lv_obj_set_size(spinner, 35, 35);
    lv_obj_align(spinner, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    // Stop name
    label_stop = lv_label_create(lv_scr_act());
    lv_label_set_text(label_stop, LV_SYMBOL_GPS " ---");
    lv_obj_set_style_text_color(label_stop, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_stop, &lv_font_montserrat_18, 0);
    lv_obj_align(label_stop, LV_ALIGN_TOP_LEFT, 15, 55);

    // Bandeau meteo compact, a droite du nom d'arret
    hdr_weather = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(hdr_weather);
    lv_obj_set_size(hdr_weather, 220, 26);
    lv_obj_align(hdr_weather, LV_ALIGN_TOP_RIGHT, -12, 53);
    lv_obj_set_flex_flow(hdr_weather, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr_weather, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr_weather, 8, 0);
    lv_obj_clear_flag(hdr_weather, LV_OBJ_FLAG_SCROLLABLE);

    weatherIconCreate(&hdr_icon, hdr_weather, 26);

    label_hdr_temp = lv_label_create(hdr_weather);
    lv_label_set_text(label_hdr_temp, "--\u00b0");
    lv_obj_set_style_text_color(label_hdr_temp, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_hdr_temp, &lv_font_montserrat_18, 0);

    label_hdr_feels = lv_label_create(hdr_weather);
    lv_label_set_text(label_hdr_feels, "");
    lv_obj_set_style_text_color(label_hdr_feels, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(label_hdr_feels, &lv_font_montserrat_12, 0);

    // Container des rows
    cont_departures = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cont_departures);
    lv_obj_set_size(cont_departures, 470, 215);
    lv_obj_align(cont_departures, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_flex_flow(cont_departures, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont_departures, 4, 0);

    for (int i = 0; i < MAX_DEPARTURES; i++) {
        lv_obj_t *row = lv_obj_create(cont_departures);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 470, 38);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        rows[i] = row;

        // Badge ligne
        line_badges[i] = lv_obj_create(row);
        lv_obj_remove_style_all(line_badges[i]);
        lv_obj_set_size(line_badges[i], 50, 28);
        lv_obj_align(line_badges[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(line_badges[i], lv_color_hex(COLOR_BUS_BADGE), 0);
        lv_obj_set_style_bg_opa(line_badges[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(line_badges[i], 5, 0);
        lv_obj_clear_flag(line_badges[i], LV_OBJ_FLAG_SCROLLABLE);

        labels_line[i] = lv_label_create(line_badges[i]);
        lv_label_set_text(labels_line[i], "?");
        lv_obj_set_style_text_color(labels_line[i], lv_color_hex(0x000000), 0);
        lv_obj_set_style_text_font(labels_line[i], &lv_font_montserrat_16, 0);
        lv_obj_center(labels_line[i]);

        // Temps restant
        labels_time[i] = lv_label_create(row);
        lv_label_set_text(labels_time[i], "--");
        lv_obj_set_style_text_color(labels_time[i], lv_color_hex(COLOR_NORMAL), 0);
        lv_obj_set_style_text_font(labels_time[i], &lv_font_montserrat_18, 0);
        lv_obj_align(labels_time[i], LV_ALIGN_LEFT_MID, 58, 0);
        lv_obj_set_width(labels_time[i], 85);

        // Mission (train)
        labels_mission[i] = lv_label_create(row);
        lv_label_set_text(labels_mission[i], "");
        lv_obj_set_style_text_color(labels_mission[i], lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_text_font(labels_mission[i], &lv_font_montserrat_14, 0);
        lv_obj_align(labels_mission[i], LV_ALIGN_LEFT_MID, 145, 0);
        lv_obj_add_flag(labels_mission[i], LV_OBJ_FLAG_HIDDEN);

        // Destination
        labels_dest[i] = lv_label_create(row);
        lv_label_set_text(labels_dest[i], "");
        lv_obj_set_style_text_color(labels_dest[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(labels_dest[i], &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(labels_dest[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(labels_dest[i], 210);
        lv_obj_align(labels_dest[i], LV_ALIGN_LEFT_MID, 195, 0);

        // Droite : voie + retard
        labels_right[i] = lv_label_create(row);
        lv_label_set_text(labels_right[i], "");
        lv_obj_set_style_text_color(labels_right[i], lv_color_hex(COLOR_SOON), 0);
        lv_obj_set_style_text_font(labels_right[i], &lv_font_montserrat_14, 0);
        lv_obj_align(labels_right[i], LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_add_flag(labels_right[i], LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    // Panneau meteo (onglet dedie), meme emprise que la liste des departs
    cont_weather = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cont_weather);
    lv_obj_set_size(cont_weather, 470, 215);
    lv_obj_align(cont_weather, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_clear_flag(cont_weather, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cont_weather, LV_OBJ_FLAG_HIDDEN);

    weatherIconCreate(&big_icon, cont_weather, 78);
    lv_obj_align(big_icon.cont, LV_ALIGN_TOP_LEFT, 36, 4);

    label_w_desc = lv_label_create(cont_weather);
    lv_label_set_text(label_w_desc, "---");
    lv_obj_set_style_text_color(label_w_desc, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_w_desc, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label_w_desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label_w_desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_w_desc, 150);
    lv_obj_align(label_w_desc, LV_ALIGN_TOP_LEFT, 0, 90);

    label_w_temp = lv_label_create(cont_weather);
    lv_label_set_text(label_w_temp, "--°C");
    lv_obj_set_style_text_color(label_w_temp, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label_w_temp, &lv_font_montserrat_40, 0);
    lv_obj_align(label_w_temp, LV_ALIGN_TOP_LEFT, 170, 6);

    label_w_feels = lv_label_create(cont_weather);
    lv_label_set_text(label_w_feels, "");
    lv_obj_set_style_text_color(label_w_feels, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(label_w_feels, &lv_font_montserrat_14, 0);
    lv_obj_align(label_w_feels, LV_ALIGN_TOP_LEFT, 173, 58);

    label_w_minmax = lv_label_create(cont_weather);
    lv_label_set_text(label_w_minmax, "");
    lv_obj_set_style_text_color(label_w_minmax, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_w_minmax, &lv_font_montserrat_14, 0);
    lv_obj_align(label_w_minmax, LV_ALIGN_TOP_LEFT, 173, 80);

    lv_obj_t *detail_labels[3];
    for (int i = 0; i < 3; i++) {
        detail_labels[i] = lv_label_create(cont_weather);
        lv_label_set_text(detail_labels[i], "");
        lv_obj_set_style_text_color(detail_labels[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(detail_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(detail_labels[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(detail_labels[i], LV_ALIGN_TOP_RIGHT, -6, 10 + i * 28);
    }
    label_w_wind   = detail_labels[0];
    label_w_precip = detail_labels[1];
    label_w_hum    = detail_labels[2];

    // Creneaux horaires a venir
    lv_obj_t *slots_row = lv_obj_create(cont_weather);
    lv_obj_remove_style_all(slots_row);
    lv_obj_set_size(slots_row, 470, 66);
    lv_obj_align(slots_row, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_flex_flow(slots_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slots_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(slots_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < WEATHER_SLOTS; i++) {
        lv_obj_t *card = lv_obj_create(slots_row);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 104, 64);
        lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        slot_cards[i] = card;

        label_slot_hour[i] = lv_label_create(card);
        lv_label_set_text(label_slot_hour[i], "--h");
        lv_obj_set_style_text_color(label_slot_hour[i], lv_color_hex(COLOR_DIMMED), 0);
        lv_obj_set_style_text_font(label_slot_hour[i], &lv_font_montserrat_12, 0);
        lv_obj_align(label_slot_hour[i], LV_ALIGN_TOP_MID, 0, 3);

        weatherIconCreate(&slot_icons[i], card, 24);
        lv_obj_align(slot_icons[i].cont, LV_ALIGN_TOP_MID, 0, 17);

        label_slot_temp[i] = lv_label_create(card);
        lv_label_set_text(label_slot_temp[i], "--°");
        lv_obj_set_style_text_color(label_slot_temp[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(label_slot_temp[i], &lv_font_montserrat_16, 0);
        lv_obj_align(label_slot_temp[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    // Status bar
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, 460, 22);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    label_status = lv_label_create(status_bar);
    lv_label_set_text(label_status, "Demarrage...");
    lv_obj_set_style_text_color(label_status, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(label_status, &lv_font_montserrat_12, 0);

    label_update_time = lv_label_create(status_bar);
    lv_label_set_text(label_update_time, "MAJ: --:--");
    lv_obj_set_style_text_color(label_update_time, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(label_update_time, &lv_font_montserrat_12, 0);

    // Night overlay (bus only)
    night_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(night_overlay, 480, 320);
    lv_obj_center(night_overlay);
    lv_obj_set_style_bg_color(night_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(night_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *night_label = lv_label_create(night_overlay);
    lv_label_set_text(night_label, LV_SYMBOL_EYE_CLOSE "\nBus en veille\n\nService 06h - 20h");
    lv_obj_set_style_text_align(night_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(night_label, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(night_label, &lv_font_montserrat_18, 0);
    lv_obj_center(night_label);

    bsp_display_unlock();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nTransit_Tracker - JC3248W535C");

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
#if LVGL_PORT_ROTATION_DEGREE == 90
        .rotate = LV_DISP_ROT_90,
#elif LVGL_PORT_ROTATION_DEGREE == 270
        .rotate = LV_DISP_ROT_270,
#elif LVGL_PORT_ROTATION_DEGREE == 180
        .rotate = LV_DISP_ROT_180,
#else
        .rotate = LV_DISP_ROT_NONE,
#endif
    };

    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    createUI();
    updateStopButtons();

    bsp_display_lock(0);
    lv_label_set_text(label_status, "Connexion WiFi...");
    bsp_display_unlock();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        esp_task_wdt_reset();
        attempts++;
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());

        configTime(0, 0, "pool.ntp.org");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        bsp_display_lock(0);
        lv_label_set_text(label_status, "Synchro NTP...");
        bsp_display_unlock();

        int ntpAttempts = 0;
        while (time(nullptr) < 1704067200 && ntpAttempts < 20) {
            delay(500);
            esp_task_wdt_reset();
            ntpAttempts++;
        }

        if (time(nullptr) >= 1704067200) {
            Serial.println("NTP synced!");
            currentTab = defaultTab();
            if (currentTab != TAB_WEATHER) currentStop = currentTab;
            updateStopButtons();
            fetchWeather();
            fetchDepartures();
            updateUI();
        } else {
            bsp_display_lock(0);
            lv_label_set_text(label_status, "NTP: echec synchro");
            bsp_display_unlock();
        }
    } else {
        bsp_display_lock(0);
        lv_label_set_text(label_status, "WiFi: echec connexion");
        bsp_display_unlock();
    }

    Serial.println("Setup complete!");
}

void loop()
{
    esp_task_wdt_reset();

    if (fetching && (millis() - fetchStartTime > FETCH_TIMEOUT_MS)) {
        Serial.println("WARN: fetch timeout, forcing reset flag");
        fetching = false;
        strcpy(errorMsg, "Timeout fetch");
        dataValid = false;
        consecutiveErrors++;
        updateUI();
    }

    if (consecutiveErrors >= 10) {
        Serial.println("ERROR: 10 erreurs consecutives, reboot...");
        delay(100);
        ESP.restart();
    }

    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.println("WiFi lost, reconnecting...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            bsp_display_lock(0);
            lv_label_set_text(label_status, "Reconnexion WiFi...");
            bsp_display_unlock();
        }
    }

    // Screen off
    bool wasScreenOff = screenOff;
    screenOff = isScreenOffTime();
    if (screenOff != wasScreenOff) {
        if (screenOff) bsp_display_backlight_off();
        else           bsp_display_backlight_on();
    }

    // Bus night mode (overlay sur stop bus seulement)
    bool wasBusNight = busNightMode;
    busNightMode = isBusNightMode();
    if (busNightMode != wasBusNight) updateUI();

    // Retour automatique a l'onglet de repli apres une selection manuelle
    int fallbackTab = defaultTab();
    if (stopSwitchTime > 0 && millis() - stopSwitchTime >= AUTO_RETURN_DELAY) {
        stopSwitchTime = 0;
    }

    // Hors selection manuelle, l'affichage suit l'onglet de repli : il bascule
    // donc tout seul a l'entree et a la sortie des heures de pointe.
    if (stopSwitchTime == 0 && currentTab != fallbackTab) {
        currentTab = fallbackTab;
        if (fallbackTab != TAB_WEATHER) {
            currentStop = fallbackTab;
            manualRefreshRequested = true;
        }
        updateStopButtons();
        uiRefreshRequested = true;
    }

    if (uiRefreshRequested) {
        uiRefreshRequested = false;
        updateUI();
    }

    // Meteo : cycle propre, independant du transport. Une seule requete
    // par tour de boucle pour ne pas cumuler deux timeouts sous le watchdog.
    bool didFetch = false;
    unsigned long weatherPeriod = weather.valid ? WEATHER_INTERVAL : WEATHER_RETRY;
    bool weatherDue = weatherRequested || lastWeatherUpdate == 0
                      || (millis() - lastWeatherUpdate >= weatherPeriod);
    if (!fetching && weatherDue && WiFi.status() == WL_CONNECTED) {
        weatherRequested = false;
        fetchWeather();
        updateUI();
        didFetch = true;
    }

    if (!didFetch && manualRefreshRequested && !fetching) {
        manualRefreshRequested = false;
        fetchDepartures();
        updateUI();
        didFetch = true;
    }

    // Periodic refresh (skip si stop bus en mode nuit)
    bool inBusNight = (stops[currentStop].type == TYPE_BUS) && busNightMode;
    if (!didFetch && !fetching && !inBusNight) {
        unsigned long interval = getUpdateInterval();
        if (millis() - lastUpdate >= interval) {
            fetchDepartures();
            updateUI();
        }
    }

    delay(100);
}
