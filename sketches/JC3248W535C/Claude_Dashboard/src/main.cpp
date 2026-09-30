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
 * @dependencies LVGL 8.3.x, ArduinoJson, PubSubClient
 */
#include <Arduino.h>
#include <lvgl.h>
#include <sys/time.h>
#include <esp_heap_caps.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "dash_model.h"
#include "ui.h"

// ---------------------------------------------------------------------------
// Demo temporaire (remplacee par WiFi/NTP/MQTT en Task 13)
// 0 : 1 hote, 3 sessions ; 1 : 2 hotes, 6 sessions (defilement, "projet@hote")
#define DEMO_MULTI 0
static const time_t DEMO_NOW = 1790771520;  // 2026-09-30 14:32 heure de Paris
// ---------------------------------------------------------------------------

static dash::Dashboard dashboard;

static void printHeap(const char *tag)
{
    Serial.printf("[%s] heap interne libre=%u min=%u | psram libre=%u\n", tag,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void applyDemoJson(const char *json, time_t now)
{
    static dash::HostSnapshot snap;  // ~1 Ko : hors pile
    if (dash::parseSnapshot(json, strlen(json), snap)) dashboard.apply(snap, now);
    else Serial.println("Demo: snapshot invalide");
}

static void loadDemo()
{
    struct timeval tv = {DEMO_NOW, 0};
    settimeofday(&tv, nullptr);
    const long n = (long)DEMO_NOW;
    char json[1024];

    snprintf(json, sizeof json,
             "{\"host\":\"srv-dev\",\"ts\":%ld,"
             "\"limits\":{\"h5\":42,\"h5_reset\":%ld,\"d7\":18,\"d7_reset\":1791010800},"
             "\"sessions\":["
             "{\"id\":\"a1\",\"project\":\"arduino\",\"model\":\"Opus 5.5\",\"state\":\"permission\","
             "\"since\":%ld,\"ctx\":37,\"tool\":\"Bash\"},"
             "{\"id\":\"b2\",\"project\":\"api-backend\",\"model\":\"Sonnet 4.6\",\"state\":\"idle\","
             "\"since\":%ld},"
             "{\"id\":\"c3\",\"project\":\"docs-site\",\"model\":\"Haiku 4.5\",\"state\":\"working\","
             "\"since\":%ld,\"ctx\":72,\"tool\":\"Edit\"}"
             "]}",
             n, n + 5880, n - 42, n - 310, n - 3900);
    applyDemoJson(json, DEMO_NOW);

#if DEMO_MULTI
    snprintf(json, sizeof json,
             "{\"host\":\"laptop\",\"ts\":%ld,"
             "\"limits\":{\"h5\":55,\"h5_reset\":%ld,\"d7\":20,\"d7_reset\":1791010800},"
             "\"sessions\":["
             "{\"id\":\"d4\",\"project\":\"frontend-application\",\"model\":\"Opus 5.5\","
             "\"state\":\"permission\",\"since\":%ld,\"ctx\":91,\"tool\":\"WebFetch\"},"
             "{\"id\":\"e5\",\"project\":\"scripts\",\"model\":\"Sonnet 4.6\",\"state\":\"working\","
             "\"since\":%ld,\"ctx\":12,\"tool\":\"Grep\"},"
             "{\"id\":\"f6\",\"project\":\"notes\",\"model\":\"Haiku 4.5\",\"state\":\"idle\","
             "\"since\":%ld,\"ctx\":55}"
             "]}",
             n - 10, n + 3600, n - 5, n - 75, n - 7200);
    applyDemoJson(json, DEMO_NOW);
#endif
}

void setup()
{
    Serial.begin(115200);
    // Laisse au moniteur serie le temps de se reconnecter apres un reset (demo)
    while (!Serial && millis() < 5000) delay(10);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    loadDemo();

    printHeap("avant UI");
    bsp_display_lock(0);
    ui_create();
    ui_render(dashboard, time(nullptr), false);
    bsp_display_unlock();
    printHeap("apres UI");
    Serial.println("UI ready");
}

void loop()
{
    static uint32_t lastTick = 0, lastRender = 0;
    uint32_t ms = millis();
    if (ms - lastTick >= 1000) {
        lastTick = ms;
        bsp_display_lock(0);
        ui_tick(dashboard, time(nullptr), false);
        bsp_display_unlock();
    }
    // Reconstruction periodique : verifie que lv_obj_clean ne fuit pas
    if (ms - lastRender >= 5000) {
        lastRender = ms;
        bsp_display_lock(0);
        ui_render(dashboard, time(nullptr), false);
        bsp_display_unlock();
        printHeap("render");
    }
    delay(50);
}
