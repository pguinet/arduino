/*
 * Bus_Tracker - JC3248W535C
 *
 * Affiche les prochains passages de bus via l'API PRIM
 * Île-de-France Mobilités. Interface tactile LVGL.
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
#include <lvgl.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "credentials.h"
#include "prim_config.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

// Colors
#define COLOR_BG        0x1a1a2e
#define COLOR_CARD      0x16213e
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00ff88
#define COLOR_IMMINENT  0xf72585
#define COLOR_SOON      0xfca311
#define COLOR_NORMAL    0x4cc9f0
#define COLOR_DIMMED    0x666666

// Update intervals (ms)
#define INTERVAL_RUSH_HOUR  300000  // 5 min heures de pointe
#define INTERVAL_NORMAL     600000  // 10 min heures creuses
#define NIGHT_START_HOUR    20
#define NIGHT_END_HOUR      6

// Config
#define MAX_DEPARTURES 5
#define MAX_STOPS 3

// Line code to name mapping (PRIM API uses internal codes)
struct LineMapping {
    const char* code;   // Ex: "C01252"
    const char* name;   // Ex: "269"
};

static const LineMapping lineMappings[] = {
    {"C01252", "269"},
    {"C02462", "1517"},
    // Ajouter d'autres lignes ici si besoin
    {nullptr, nullptr}
};

// Get line name from LineRef (ex: "STIF:Line::C01252:" -> "269")
static const char* getLineName(const char* lineRef)
{
    if (!lineRef) return "?";

    // Extract code from "STIF:Line::C01252:"
    const char* start = strstr(lineRef, "::");
    if (!start) return "?";
    start += 2;  // Skip "::"

    // Find in mapping
    for (int i = 0; lineMappings[i].code != nullptr; i++) {
        if (strstr(start, lineMappings[i].code) == start) {
            return lineMappings[i].name;
        }
    }

    // Not found - return the code itself (truncated)
    static char buf[8];
    strncpy(buf, start, 6);
    buf[6] = '\0';
    // Remove trailing ':'
    char* colon = strchr(buf, ':');
    if (colon) *colon = '\0';
    return buf;
}

// Stop configuration
struct StopConfig {
    const char* stopId;
    const char* stopName;
};

// Liste des arrêts configurés
static StopConfig stops[MAX_STOPS] = {
    {"413248", "Marechal Foch"},
    {"", ""},  // Slot libre
    {"", ""}   // Slot libre
};
static int currentStop = 0;

// Departure data
struct Departure {
    int minutesLeft;
    char lineName[10];
    char destination[40];
    bool atStop;
};

static Departure departures[MAX_DEPARTURES];
static int departureCount = 0;
static bool dataValid = false;
static unsigned long lastUpdate = 0;
static char lastUpdateTime[10] = "--:--";
static char errorMsg[50] = "";
static bool nightMode = false;
static bool fetching = false;
static bool manualRefreshRequested = false;

// UI elements
static lv_obj_t *label_title;
static lv_obj_t *label_stop;
static lv_obj_t *label_status;
static lv_obj_t *label_update_time;
static lv_obj_t *btn_refresh;
static lv_obj_t *spinner;
static lv_obj_t *cont_departures;
static lv_obj_t *labels_time[MAX_DEPARTURES];
static lv_obj_t *labels_dest[MAX_DEPARTURES];
static lv_obj_t *night_overlay;

// WiFi client
static WiFiClientSecure client;

// Get update interval based on time
static unsigned long getUpdateInterval()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    int minute = ti->tm_min;

    // Heures de pointe matin (06h30-09h00)
    if ((hour == 6 && minute >= 30) || (hour >= 7 && hour < 9)) {
        return INTERVAL_RUSH_HOUR;
    }
    // Heures de pointe soir (16h00-18h00)
    if (hour >= 16 && hour < 18) {
        return INTERVAL_RUSH_HOUR;
    }
    return INTERVAL_NORMAL;
}

// Check if night mode
static bool isNightMode()
{
    time_t now = time(nullptr);
    struct tm* ti = localtime(&now);
    int hour = ti->tm_hour;
    return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
}

// Fetch departures from API
static void fetchDepartures()
{
    if (fetching || strlen(stops[currentStop].stopId) == 0) return;

    fetching = true;

    bsp_display_lock(0);
    lv_label_set_text(label_status, "Chargement...");
    lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();

    client.setInsecure();

    HTTPClient https;
    String url = "https://prim.iledefrance-mobilites.fr/marketplace/stop-monitoring?MonitoringRef=STIF%3AStopPoint%3AQ%3A";
    url += stops[currentStop].stopId;
    url += "%3A";

    Serial.printf("Fetching: %s\n", url.c_str());
    https.setTimeout(15000);

    if (https.begin(client, url)) {
        https.addHeader("Accept", "application/json");
        https.addHeader("apikey", PRIM_API_KEY);

        int httpCode = https.GET();
        Serial.printf("HTTP code: %d\n", httpCode);

        if (httpCode == HTTP_CODE_OK) {
            String payload = https.getString();
            Serial.printf("Payload length: %d\n", payload.length());

            int start = payload.indexOf('{');
            if (start >= 0) {
                // Filter JSON
                JsonDocument filter;
                JsonObject filterVisit = filter["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"].add<JsonObject>();
                filterVisit["MonitoredVehicleJourney"]["LineRef"]["value"] = true;
                filterVisit["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"] = true;
                filterVisit["MonitoredVehicleJourney"]["MonitoredCall"]["VehicleAtStop"] = true;
                filterVisit["MonitoredVehicleJourney"]["DestinationName"][0]["value"] = true;

                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload.c_str() + start,
                    DeserializationOption::Filter(filter),
                    DeserializationOption::NestingLimit(15));

                payload = "";  // Free memory

                if (!error) {
                    JsonArray visits = doc["Siri"]["ServiceDelivery"]["StopMonitoringDelivery"][0]["MonitoredStopVisit"];

                    departureCount = 0;
                    time_t now = time(nullptr);

                    for (JsonObject visit : visits) {
                        if (departureCount >= MAX_DEPARTURES) break;

                        const char* lineRef = visit["MonitoredVehicleJourney"]["LineRef"]["value"];
                        const char* lineName = getLineName(lineRef);
                        const char* expectedTime = visit["MonitoredVehicleJourney"]["MonitoredCall"]["ExpectedDepartureTime"];
                        const char* destName = visit["MonitoredVehicleJourney"]["DestinationName"][0]["value"];
                        bool atStop = visit["MonitoredVehicleJourney"]["MonitoredCall"]["VehicleAtStop"] | false;

                        if (expectedTime) {
                            // Parse ISO 8601 UTC time
                            int year, month, day, hour, minute, second;
                            sscanf(expectedTime, "%d-%d-%dT%d:%d:%d",
                                   &year, &month, &day, &hour, &minute, &second);

                            // Calculate epoch (simplified for 2020-2030)
                            int days = (year - 1970) * 365 + (year - 1969) / 4;
                            int monthDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
                            days += monthDays[month - 1] + day - 1;
                            if (month > 2 && (year % 4 == 0)) days++;

                            time_t departureTime = (time_t)days * 86400 + hour * 3600 + minute * 60 + second;
                            int minutes = (departureTime - now) / 60;

                            if (minutes >= 0) {
                                departures[departureCount].minutesLeft = minutes;
                                departures[departureCount].atStop = atStop;
                                strncpy(departures[departureCount].lineName,
                                       lineName ? lineName : "?", 9);
                                departures[departureCount].lineName[9] = '\0';
                                strncpy(departures[departureCount].destination,
                                       destName ? destName : "", 39);
                                departures[departureCount].destination[39] = '\0';
                                departureCount++;
                            }
                        }
                    }

                    dataValid = true;
                    strcpy(errorMsg, "");

                    struct tm* ti = localtime(&now);
                    sprintf(lastUpdateTime, "%02d:%02d", ti->tm_hour, ti->tm_min);

                } else {
                    sprintf(errorMsg, "JSON: %s", error.c_str());
                    dataValid = false;
                }
            } else {
                strcpy(errorMsg, "No JSON");
                dataValid = false;
            }
        } else {
            sprintf(errorMsg, "HTTP %d", httpCode);
            dataValid = false;
        }

        https.end();
    } else {
        strcpy(errorMsg, "Connexion impossible");
        dataValid = false;
    }

    lastUpdate = millis();
    fetching = false;
}

// Update UI with departure data
static void updateUI()
{
    bsp_display_lock(0);

    // Hide spinner, show button
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_HIDDEN);

    // Night mode overlay
    if (nightMode) {
        lv_obj_clear_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label_status, "Mode veille (06h-20h)");
        bsp_display_unlock();
        return;
    }
    lv_obj_add_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);

    // Update header
    char buf[64];
    snprintf(buf, sizeof(buf), LV_SYMBOL_GPS " %s", stops[currentStop].stopName);
    lv_label_set_text(label_stop, buf);

    // Update status
    if (!dataValid) {
        if (strlen(errorMsg) > 0) {
            lv_label_set_text(label_status, errorMsg);
        }
    } else if (departureCount == 0) {
        lv_label_set_text(label_status, "Aucun bus prevu");
    } else {
        snprintf(buf, sizeof(buf), "%d passage%s", departureCount, departureCount > 1 ? "s" : "");
        lv_label_set_text(label_status, buf);
    }

    // Update time
    snprintf(buf, sizeof(buf), "MAJ: %s", lastUpdateTime);
    lv_label_set_text(label_update_time, buf);

    // Update departures
    for (int i = 0; i < MAX_DEPARTURES; i++) {
        if (i < departureCount && dataValid) {
            // Time
            char timeStr[20];
            lv_color_t color;

            if (departures[i].atStop) {
                strcpy(timeStr, "A L'ARRET");
                color = lv_color_hex(COLOR_IMMINENT);
            } else if (departures[i].minutesLeft == 0) {
                strcpy(timeStr, "Imminent");
                color = lv_color_hex(COLOR_IMMINENT);
            } else if (departures[i].minutesLeft < 3) {
                snprintf(timeStr, sizeof(timeStr), "%d min", departures[i].minutesLeft);
                color = lv_color_hex(COLOR_IMMINENT);
            } else if (departures[i].minutesLeft < 10) {
                snprintf(timeStr, sizeof(timeStr), "%d min", departures[i].minutesLeft);
                color = lv_color_hex(COLOR_SOON);
            } else if (departures[i].minutesLeft < 60) {
                snprintf(timeStr, sizeof(timeStr), "%d min", departures[i].minutesLeft);
                color = lv_color_hex(COLOR_NORMAL);
            } else {
                int h = departures[i].minutesLeft / 60;
                int m = departures[i].minutesLeft % 60;
                snprintf(timeStr, sizeof(timeStr), "%dh%02d", h, m);
                color = lv_color_hex(COLOR_NORMAL);
            }

            lv_label_set_text(labels_time[i], timeStr);
            lv_obj_set_style_text_color(labels_time[i], color, 0);

            // Line + Destination
            char destBuf[60];
            snprintf(destBuf, sizeof(destBuf), "[%s] %s", departures[i].lineName, departures[i].destination);
            lv_label_set_text(labels_dest[i], destBuf);
            lv_obj_clear_flag(lv_obj_get_parent(labels_time[i]), LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lv_obj_get_parent(labels_time[i]), LV_OBJ_FLAG_HIDDEN);
        }
    }

    bsp_display_unlock();
}

// Button callbacks - just set flag, fetch will happen in loop()
static void btn_refresh_cb(lv_event_t *e)
{
    if (!fetching) {
        manualRefreshRequested = true;
    }
}

// Create UI
static void createUI()
{
    bsp_display_lock(0);

    // Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BG), 0);

    // Title bar
    lv_obj_t *title_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(title_bar);
    lv_obj_set_size(title_bar, 480, 50);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(title_bar, 10, 0);

    label_title = lv_label_create(title_bar);
    lv_label_set_text(label_title, LV_SYMBOL_HOME " Bus Tracker");
    lv_obj_set_style_text_color(label_title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_22, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 0, 0);

    // Refresh button
    btn_refresh = lv_btn_create(title_bar);
    lv_obj_set_size(btn_refresh, 90, 35);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_refresh, btn_refresh_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(btn_refresh);
    lv_label_set_text(btn_label, LV_SYMBOL_REFRESH " MAJ");
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0x000000), 0);
    lv_obj_center(btn_label);

    // Spinner (hidden by default)
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

    // Departures container
    cont_departures = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(cont_departures);
    lv_obj_set_size(cont_departures, 460, 200);
    lv_obj_align(cont_departures, LV_ALIGN_TOP_MID, 0, 85);
    lv_obj_set_flex_flow(cont_departures, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont_departures, 8, 0);

    // Create departure rows
    for (int i = 0; i < MAX_DEPARTURES; i++) {
        lv_obj_t *row = lv_obj_create(cont_departures);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 460, 30);
        lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_hor(row, 15, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        labels_time[i] = lv_label_create(row);
        lv_label_set_text(labels_time[i], "--");
        lv_obj_set_style_text_font(labels_time[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(labels_time[i], lv_color_hex(COLOR_NORMAL), 0);
        lv_obj_set_width(labels_time[i], 100);

        labels_dest[i] = lv_label_create(row);
        lv_label_set_text(labels_dest[i], "---");
        lv_obj_set_style_text_font(labels_dest[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(labels_dest[i], lv_color_hex(COLOR_TEXT), 0);
        lv_label_set_long_mode(labels_dest[i], LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(labels_dest[i], 1);

        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    // Status bar
    lv_obj_t *status_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, 460, 25);
    lv_obj_align(status_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
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

    // Night mode overlay (hidden by default)
    night_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(night_overlay, 480, 320);
    lv_obj_center(night_overlay);
    lv_obj_set_style_bg_color(night_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(night_overlay, LV_OPA_90, 0);
    lv_obj_add_flag(night_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *night_label = lv_label_create(night_overlay);
    lv_label_set_text(night_label, LV_SYMBOL_EYE_CLOSE "\nMode veille\n\nService 06h00 - 20h00");
    lv_obj_set_style_text_align(night_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(night_label, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_set_style_text_font(night_label, &lv_font_montserrat_18, 0);
    lv_obj_center(night_label);

    bsp_display_unlock();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nBus_Tracker - JC3248W535C");

    // Initialize display
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

    // Create UI
    createUI();

    // Connect WiFi
    bsp_display_lock(0);
    lv_label_set_text(label_status, "Connexion WiFi...");
    bsp_display_unlock();

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());

        // Config NTP
        configTime(0, 0, "pool.ntp.org");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();

        bsp_display_lock(0);
        lv_label_set_text(label_status, "Synchro NTP...");
        bsp_display_unlock();

        // Wait for NTP sync (time > year 2024)
        int ntpAttempts = 0;
        while (time(nullptr) < 1704067200 && ntpAttempts < 20) {  // 1704067200 = 2024-01-01
            delay(500);
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

            // First fetch
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
    // Check night mode
    bool wasNightMode = nightMode;
    nightMode = isNightMode();

    if (nightMode != wasNightMode) {
        updateUI();
    }

    // Manual refresh requested (from button)
    if (manualRefreshRequested && !fetching) {
        manualRefreshRequested = false;
        fetchDepartures();
        updateUI();
    }

    // Periodic update (only if not night mode)
    if (!nightMode && !fetching) {
        unsigned long interval = getUpdateInterval();
        if (millis() - lastUpdate >= interval) {
            fetchDepartures();
            updateUI();
        }
    }

    delay(100);
}
