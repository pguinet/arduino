/*
 * Transit_Tracker - JC3248W535C
 *
 * Prochains passages bus et trains via l'API PRIM Ile-de-France
 * Mobilites, regroupes dans une seule app a onglets. Chaque arret
 * peut etre un bus (StopPoint:Q) ou un train (StopArea:SP).
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

enum StopType { TYPE_BUS, TYPE_TRAIN };

struct StopConfig {
    StopType    type;
    const char* monitoringRef;   // URL-encoded (ex: STIF%3AStopPoint%3AQ%3A413248%3A)
    const char* name;
    bool        autoReturn;      // Si true, retourne au stop 0 apres AUTO_RETURN_DELAY
};

// Configuration des arrets (modifier ici pour ajouter/changer un arret)
static StopConfig stops[MAX_STOPS] = {
    {TYPE_BUS,   "STIF%3AStopPoint%3AQ%3A413248%3A", "Foch",   false},
    {TYPE_BUS,   "STIF%3AStopPoint%3AQ%3A14305%3A",  "Eglise", true},
    {TYPE_TRAIN, "STIF%3AStopArea%3ASP%3A43120%3A",  "Domont", true},
};

static int currentStop = 0;
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
    {"C01252", "269",   COLOR_BUS_BADGE, 0x000000},
    {"C02462", "1517",  COLOR_BUS_BADGE, 0x000000},
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

static WiFiClientSecure client;

static unsigned long getUpdateInterval()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour, minute = ti->tm_min;
    if ((hour == 6 && minute >= 30) || (hour >= 7 && hour < 9))  return INTERVAL_RUSH_HOUR;
    if (hour >= 17 && hour < 20)                                  return INTERVAL_RUSH_HOUR;
    return INTERVAL_NORMAL;
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

static void updateUI()
{
    bsp_display_lock(0);

    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);

    StopConfig& stop = stops[currentStop];

    // Header
    char buf[64];
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

static void updateStopButtons()
{
    bsp_display_lock(0);
    for (int i = 0; i < MAX_STOPS; i++) {
        if (i == currentStop) {
            lv_obj_set_style_bg_color(btn_stops[i], lv_color_hex(COLOR_ACCENT), 0);
        } else {
            lv_obj_set_style_bg_color(btn_stops[i], lv_color_hex(COLOR_CARD), 0);
        }
    }
    bsp_display_unlock();
}

static void btn_refresh_cb(lv_event_t *e)
{
    if (!fetching) manualRefreshRequested = true;
}

static void btn_stop_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == currentStop || fetching) return;

    currentStop = idx;
    stopSwitchTime = stops[idx].autoReturn ? millis() : 0;
    manualRefreshRequested = true;
    updateStopButtons();
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

    // Stop buttons (largeur 110, espacement 5)
    int btnW = 110, btnH = 35;
    for (int i = 0; i < MAX_STOPS; i++) {
        btn_stops[i] = lv_btn_create(title_bar);
        lv_obj_set_size(btn_stops[i], btnW, btnH);
        lv_obj_align(btn_stops[i], LV_ALIGN_LEFT_MID, i * (btnW + 5), 0);
        lv_obj_set_style_bg_color(btn_stops[i], lv_color_hex(i == 0 ? COLOR_ACCENT : COLOR_CARD), 0);
        lv_obj_add_event_cb(btn_stops[i], btn_stop_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn_stops[i]);
        lv_label_set_text(lbl, stops[i].name);
        lv_obj_set_style_text_color(lbl, lv_color_hex(i == 0 ? 0x000000 : COLOR_TEXT), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    }

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

    // Auto-return au stop 0
    if (currentStop != 0 && stopSwitchTime > 0) {
        if (millis() - stopSwitchTime >= AUTO_RETURN_DELAY) {
            currentStop = 0;
            stopSwitchTime = 0;
            updateStopButtons();
            manualRefreshRequested = true;
        }
    }

    if (manualRefreshRequested && !fetching) {
        manualRefreshRequested = false;
        fetchDepartures();
        updateUI();
    }

    // Periodic refresh (skip si stop bus en mode nuit)
    bool inBusNight = (stops[currentStop].type == TYPE_BUS) && busNightMode;
    if (!fetching && !inBusNight) {
        unsigned long interval = getUpdateInterval();
        if (millis() - lastUpdate >= interval) {
            fetchDepartures();
            updateUI();
        }
    }

    delay(100);
}
