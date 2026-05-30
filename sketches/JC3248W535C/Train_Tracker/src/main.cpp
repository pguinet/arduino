/*
 * Train_Tracker - JC3248W535C
 *
 * Prochains departs d'une gare Transilien/RER via l'API PRIM
 * Ile-de-France Mobilites. Affiche ligne, mission, heure (avec retard),
 * destination et voie sur ecran tactile LVGL.
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

// Station configuree (StopArea ID PRIM - voir jeu de donnees "zones-d-arrets")
// Domont (ligne H Transilien) : zdaid 43120
#define STATION_ID    "43120"
#define STATION_NAME  "Domont"

// Colors
#define COLOR_BG        0x0f1419
#define COLOR_CARD      0x1c2733
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00d4ff
#define COLOR_LATE      0xf72585
#define COLOR_SOON      0xfca311
#define COLOR_NORMAL    0x4cc9f0
#define COLOR_DIMMED    0x888888

// Watchdog timeout (seconds)
#define WDT_TIMEOUT_SEC     30

// Update intervals (ms)
#define INTERVAL_RUSH_HOUR  30000   // 30 sec heures de pointe
#define INTERVAL_NORMAL     120000  // 2 min heures creuses

// Screen off hours (backlight off) - trains tot le matin
#define SCREEN_OFF_START    23
#define SCREEN_OFF_END      5

// Config
#define MAX_DEPARTURES 5

// Line mapping (codes PRIM -> nom, couleur Transilien)
struct LineInfo {
    const char* code;       // Ex: "C01737"
    const char* name;       // Ex: "H"
    uint32_t    color;      // Couleur officielle de la ligne
    uint32_t    textColor;  // Couleur du texte sur le badge
};

static const LineInfo lineInfos[] = {
    // Transilien
    {"C01737", "H",     0x6E1E78, 0xFFFFFF},  // violet
    {"C01739", "J",     0xCD8B00, 0xFFFFFF},  // jaune ocre
    {"C01738", "K",     0xA0006E, 0xFFFFFF},  // mauve
    {"C01740", "L",     0x8D5E2A, 0xFFFFFF},  // marron clair
    {"C01741", "N",     0x00A88F, 0xFFFFFF},  // vert
    {"C01736", "P",     0xF3D03E, 0x000000},  // jaune
    {"C01735", "R",     0xF49FB6, 0x000000},  // rose
    {"C01744", "U",     0xD41367, 0xFFFFFF},  // magenta
    // RER
    {"C01742", "RER A", 0xE2231A, 0xFFFFFF},  // rouge
    {"C01743", "RER B", 0x4296D2, 0xFFFFFF},  // bleu
    {"C01727", "RER C", 0xF99D1C, 0x000000},  // orange
    {"C01728", "RER D", 0x008B5B, 0xFFFFFF},  // vert
    {"C01729", "RER E", 0xB94E9A, 0xFFFFFF},  // mauve
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

// Departure data
struct Departure {
    int minutesLeft;            // Minutes jusqu'au depart reel (Expected)
    int delayMinutes;           // Retard en minutes (Expected - Aimed)
    char lineName[8];           // Ex: "H", "RER A"
    uint32_t lineColor;
    uint32_t lineTextColor;
    char mission[6];            // Ex: "SARA"
    char destination[40];
    char platform[6];           // Ex: "V2"
    char timeStr[8];            // Ex: "18:42"
    bool atStop;
};

static Departure departures[MAX_DEPARTURES];
static int departureCount = 0;
static bool dataValid = false;
static unsigned long lastUpdate = 0;
static char lastUpdateTime[10] = "--:--";
static char errorMsg[60] = "";
static bool screenOff = false;
static bool fetching = false;
static unsigned long fetchStartTime = 0;
#define FETCH_TIMEOUT_MS 20000
static bool manualRefreshRequested = false;
static int consecutiveErrors = 0;

// UI elements
static lv_obj_t *label_station;
static lv_obj_t *label_status;
static lv_obj_t *label_update_time;
static lv_obj_t *btn_refresh;
static lv_obj_t *spinner;
static lv_obj_t *cont_departures;
static lv_obj_t *rows[MAX_DEPARTURES];
static lv_obj_t *line_badges[MAX_DEPARTURES];
static lv_obj_t *labels_line[MAX_DEPARTURES];
static lv_obj_t *labels_mission[MAX_DEPARTURES];
static lv_obj_t *labels_time[MAX_DEPARTURES];
static lv_obj_t *labels_delay[MAX_DEPARTURES];
static lv_obj_t *labels_dest[MAX_DEPARTURES];
static lv_obj_t *labels_platform[MAX_DEPARTURES];

static WiFiClientSecure client;

static unsigned long getUpdateInterval()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    int minute = ti->tm_min;
    if ((hour == 6 && minute >= 30) || (hour >= 7 && hour < 9)) return INTERVAL_RUSH_HOUR;
    if (hour >= 17 && hour < 20) return INTERVAL_RUSH_HOUR;
    return INTERVAL_NORMAL;
}

static bool isScreenOffTime()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    return (hour >= SCREEN_OFF_START || hour < SCREEN_OFF_END);
}

// Parse ISO 8601 UTC time string into epoch (simplifie pour 2020-2099)
static time_t parseIso8601(const char* str)
{
    if (!str) return 0;
    int year, month, day, hour, minute, second;
    if (sscanf(str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return 0;
    }
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

    client.setInsecure();
    client.setTimeout(10);

    HTTPClient https;
    String url = "https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF%3AStopArea%3ASP%3A";
    url += STATION_ID;
    url += "%3A";

    Serial.printf("Fetching: %s\n", url.c_str());
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
            Serial.printf("Payload length: %d\n", payload.length());

            int start = payload.indexOf('{');
            if (start >= 0) {
                JsonDocument filter;
                JsonObject fv = filter["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"].add<JsonObject>();
                fv["MonitoredVehicleJourney"]["LineRef"]["value"] = true;
                fv["MonitoredVehicleJourney"]["DestinationName"][0]["value"] = true;
                fv["MonitoredVehicleJourney"]["JourneyNote"][0]["value"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["AimedDepartureTime"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["VehicleAtStop"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["DeparturePlatformName"]["value"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["ArrivalPlatformName"]["value"] = true;
                fv["MonitoredVehicleJourney"]["MonitoredCall"]["DestinationDisplay"][0]["value"] = true;

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
                        const char* aimedTime = call["AimedDepartureTime"];
                        const char* destName = mvj["DestinationName"][0]["value"];
                        const char* destDisplay = call["DestinationDisplay"][0]["value"];
                        const char* journeyNote = mvj["JourneyNote"][0]["value"];
                        // DeparturePlatformName en priorite (train sortant), sinon ArrivalPlatformName
                        const char* platform = call["DeparturePlatformName"]["value"]
                                             | call["ArrivalPlatformName"]["value"];
                        bool atStop = call["VehicleAtStop"] | false;

                        if (!expectedTime) continue;

                        time_t depTime = parseIso8601(expectedTime);
                        if (depTime == 0) continue;

                        int minutes = (depTime - now) / 60;
                        if (minutes < -1) continue;  // Trains depasses

                        Departure& d = departures[departureCount];
                        d.minutesLeft = minutes;
                        d.atStop = atStop;

                        // Retard
                        d.delayMinutes = 0;
                        if (aimedTime) {
                            time_t aimedT = parseIso8601(aimedTime);
                            if (aimedT > 0) {
                                d.delayMinutes = (depTime - aimedT) / 60;
                            }
                        }

                        // Heure au format HH:MM (locale)
                        struct tm* dt = localtime(&depTime);
                        snprintf(d.timeStr, sizeof(d.timeStr), "%02d:%02d", dt->tm_hour, dt->tm_min);

                        // Ligne
                        const LineInfo* li = getLineInfo(lineRef);
                        if (li) {
                            strncpy(d.lineName, li->name, sizeof(d.lineName) - 1);
                            d.lineName[sizeof(d.lineName) - 1] = '\0';
                            d.lineColor = li->color;
                            d.lineTextColor = li->textColor;
                        } else {
                            strcpy(d.lineName, "?");
                            d.lineColor = 0x444444;
                            d.lineTextColor = 0xFFFFFF;
                        }

                        // Mission : JourneyNote contient le code 4 lettres (ex: "AOLA")
                        d.mission[0] = '\0';
                        if (journeyNote && strlen(journeyNote) > 0 && strlen(journeyNote) <= 5) {
                            strncpy(d.mission, journeyNote, sizeof(d.mission) - 1);
                            d.mission[sizeof(d.mission) - 1] = '\0';
                        }

                        // Destination : DestinationDisplay (plus court) sinon DestinationName
                        const char* dest = destDisplay && strlen(destDisplay) > 0 ? destDisplay : destName;
                        if (dest) {
                            strncpy(d.destination, dest, sizeof(d.destination) - 1);
                            d.destination[sizeof(d.destination) - 1] = '\0';
                        } else {
                            d.destination[0] = '\0';
                        }

                        // Quai
                        if (platform && strlen(platform) > 0) {
                            // Ajouter "V" si numerique pur
                            if (platform[0] >= '0' && platform[0] <= '9') {
                                snprintf(d.platform, sizeof(d.platform), "V%s", platform);
                            } else {
                                strncpy(d.platform, platform, sizeof(d.platform) - 1);
                                d.platform[sizeof(d.platform) - 1] = '\0';
                            }
                        } else {
                            d.platform[0] = '\0';
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

static void updateUI()
{
    bsp_display_lock(0);

    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);

    char buf[64];
    if (!dataValid) {
        if (strlen(errorMsg) > 0) {
            lv_label_set_text(label_status, errorMsg);
        }
    } else if (departureCount == 0) {
        lv_label_set_text(label_status, "Aucun train prevu");
    } else {
        snprintf(buf, sizeof(buf), "%d train%s", departureCount, departureCount > 1 ? "s" : "");
        lv_label_set_text(label_status, buf);
    }

    snprintf(buf, sizeof(buf), "MAJ: %s", lastUpdateTime);
    lv_label_set_text(label_update_time, buf);

    for (int i = 0; i < MAX_DEPARTURES; i++) {
        if (i < departureCount && dataValid) {
            Departure& d = departures[i];

            // Badge ligne
            lv_obj_set_style_bg_color(line_badges[i], lv_color_hex(d.lineColor), 0);
            lv_label_set_text(labels_line[i], d.lineName);
            lv_obj_set_style_text_color(labels_line[i], lv_color_hex(d.lineTextColor), 0);

            // Mission
            lv_label_set_text(labels_mission[i], d.mission[0] ? d.mission : "----");

            // Heure
            if (d.atStop) {
                lv_label_set_text(labels_time[i], "QUAI");
                lv_obj_set_style_text_color(labels_time[i], lv_color_hex(COLOR_LATE), 0);
            } else {
                lv_label_set_text(labels_time[i], d.timeStr);
                lv_color_t timeColor;
                if (d.minutesLeft <= 2)      timeColor = lv_color_hex(COLOR_LATE);
                else if (d.minutesLeft <= 5) timeColor = lv_color_hex(COLOR_SOON);
                else                          timeColor = lv_color_hex(COLOR_TEXT);
                lv_obj_set_style_text_color(labels_time[i], timeColor, 0);
            }

            // Retard
            if (d.delayMinutes > 0) {
                char delayBuf[12];
                snprintf(delayBuf, sizeof(delayBuf), "+%d", d.delayMinutes);
                lv_label_set_text(labels_delay[i], delayBuf);
                lv_obj_clear_flag(labels_delay[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(labels_delay[i], LV_OBJ_FLAG_HIDDEN);
            }

            // Destination
            lv_label_set_text(labels_dest[i], d.destination);

            // Quai
            lv_label_set_text(labels_platform[i], d.platform);

            lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    bsp_display_unlock();
}

static void btn_refresh_cb(lv_event_t *e)
{
    if (!fetching) {
        manualRefreshRequested = true;
    }
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
    lv_obj_set_style_pad_all(title_bar, 10, 0);

    // Station name
    label_station = lv_label_create(title_bar);
    char buf[64];
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %s", STATION_NAME);
    lv_label_set_text(label_station, buf);
    lv_obj_set_style_text_color(label_station, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label_station, &lv_font_montserrat_22, 0);
    lv_obj_align(label_station, LV_ALIGN_LEFT_MID, 5, 0);

    // Refresh button
    btn_refresh = lv_btn_create(title_bar);
    lv_obj_set_size(btn_refresh, 60, 35);
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

    // Departures container
    cont_departures = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cont_departures);
    lv_obj_set_size(cont_departures, 470, 235);
    lv_obj_align(cont_departures, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_set_flex_flow(cont_departures, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont_departures, 5, 0);

    for (int i = 0; i < MAX_DEPARTURES; i++) {
        lv_obj_t *row = lv_obj_create(cont_departures);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 470, 42);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        rows[i] = row;

        // Badge ligne (cercle colore)
        line_badges[i] = lv_obj_create(row);
        lv_obj_remove_style_all(line_badges[i]);
        lv_obj_set_size(line_badges[i], 50, 30);
        lv_obj_align(line_badges[i], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(line_badges[i], lv_color_hex(0x6E1E78), 0);
        lv_obj_set_style_bg_opa(line_badges[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(line_badges[i], 6, 0);
        lv_obj_clear_flag(line_badges[i], LV_OBJ_FLAG_SCROLLABLE);

        labels_line[i] = lv_label_create(line_badges[i]);
        lv_label_set_text(labels_line[i], "?");
        lv_obj_set_style_text_color(labels_line[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(labels_line[i], &lv_font_montserrat_18, 0);
        lv_obj_center(labels_line[i]);

        // Mission (4 lettres)
        labels_mission[i] = lv_label_create(row);
        lv_label_set_text(labels_mission[i], "----");
        lv_obj_set_style_text_color(labels_mission[i], lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_text_font(labels_mission[i], &lv_font_montserrat_16, 0);
        lv_obj_align(labels_mission[i], LV_ALIGN_LEFT_MID, 58, 0);

        // Heure
        labels_time[i] = lv_label_create(row);
        lv_label_set_text(labels_time[i], "--:--");
        lv_obj_set_style_text_color(labels_time[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(labels_time[i], &lv_font_montserrat_18, 0);
        lv_obj_align(labels_time[i], LV_ALIGN_LEFT_MID, 115, -6);

        // Retard (+Xmin) sous l'heure
        labels_delay[i] = lv_label_create(row);
        lv_label_set_text(labels_delay[i], "");
        lv_obj_set_style_text_color(labels_delay[i], lv_color_hex(COLOR_LATE), 0);
        lv_obj_set_style_text_font(labels_delay[i], &lv_font_montserrat_12, 0);
        lv_obj_align(labels_delay[i], LV_ALIGN_LEFT_MID, 115, 12);

        // Destination
        labels_dest[i] = lv_label_create(row);
        lv_label_set_text(labels_dest[i], "");
        lv_obj_set_style_text_color(labels_dest[i], lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(labels_dest[i], &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(labels_dest[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(labels_dest[i], 235);
        lv_obj_align(labels_dest[i], LV_ALIGN_LEFT_MID, 180, 0);

        // Quai
        labels_platform[i] = lv_label_create(row);
        lv_label_set_text(labels_platform[i], "");
        lv_obj_set_style_text_color(labels_platform[i], lv_color_hex(COLOR_SOON), 0);
        lv_obj_set_style_text_font(labels_platform[i], &lv_font_montserrat_16, 0);
        lv_obj_align(labels_platform[i], LV_ALIGN_RIGHT_MID, -5, 0);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    // Status bar
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, 460, 25);
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

    bsp_display_unlock();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nTrain_Tracker - JC3248W535C");

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(NULL);

    Serial.println("Initializing display...");
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
            Serial.print(".");
        }
        Serial.println();

        if (time(nullptr) >= 1704067200) {
            Serial.println("NTP synced!");
            bsp_display_lock(0);
            char buf[64];
            snprintf(buf, sizeof(buf), "WiFi OK: %s", WiFi.localIP().toString().c_str());
            lv_label_set_text(label_status, buf);
            bsp_display_unlock();

            fetchDepartures();
            updateUI();
        } else {
            Serial.println("NTP sync failed");
            bsp_display_lock(0);
            lv_label_set_text(label_status, "NTP: echec synchro");
            bsp_display_unlock();
        }
    } else {
        Serial.println("WiFi connection failed!");
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

    bool wasScreenOff = screenOff;
    screenOff = isScreenOffTime();
    if (screenOff != wasScreenOff) {
        if (screenOff) bsp_display_backlight_off();
        else           bsp_display_backlight_on();
    }

    if (manualRefreshRequested && !fetching) {
        manualRefreshRequested = false;
        fetchDepartures();
        updateUI();
    }

    if (!fetching) {
        unsigned long interval = getUpdateInterval();
        if (millis() - lastUpdate >= interval) {
            fetchDepartures();
            updateUI();
        }
    }

    delay(100);
}
