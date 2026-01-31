/*
 * SD_Browser - JC3248W535C
 *
 * Explorateur de carte SD avec interface tactile LVGL.
 * Affiche les infos techniques et permet de naviguer dans les fichiers.
 *
 * Board: JC3248W535C (ESP32-S3 + LCD tactile 3.5")
 * FQBN: PlatformIO esp32-s3-devkitc-1
 *
 * SD Card pins (MMC 1-bit mode):
 *   D0  -> GPIO 13
 *   CLK -> GPIO 12
 *   CMD -> GPIO 11
 *
 * @dependencies LVGL 8.3.x, SD_MMC
 */

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <lvgl.h>
#include <JPEGDEC.h>
#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"

#define LVGL_PORT_ROTATION_DEGREE (90)

// SD MMC pins
#define SD_MMC_CLK  12
#define SD_MMC_CMD  11
#define SD_MMC_D0   13

// Colors
#define COLOR_BG        0x1a1a2e
#define COLOR_CARD      0x16213e
#define COLOR_TEXT      0xffffff
#define COLOR_ACCENT    0x00ff88
#define COLOR_FOLDER    0xfca311
#define COLOR_FILE      0x4cc9f0
#define COLOR_DIMMED    0x666666
#define COLOR_ERROR     0xf72585

// UI elements
static lv_obj_t *label_title;
static lv_obj_t *label_path;
static lv_obj_t *btn_back;
static lv_obj_t *btn_info;
static lv_obj_t *btn_refresh;
static lv_obj_t *list_files;
static lv_obj_t *img_overlay = nullptr;
static lv_obj_t *img_view = nullptr;

// State
static String currentPath = "/";
static bool sdCardOk = false;
static bool showingInfo = false;
static bool showingImage = false;

// Format size to human readable
static String formatSize(uint64_t bytes)
{
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return String(bytes / 1024.0, 1) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return String(bytes / (1024.0 * 1024.0), 1) + " MB";
    } else {
        return String(bytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    }
}

// Get SD card type string
static const char* getCardTypeString(sdcard_type_t type)
{
    switch (type) {
        case CARD_MMC: return "MMC";
        case CARD_SD: return "SD";
        case CARD_SDHC: return "SDHC";
        default: return "Inconnu";
    }
}

// Check if file is an image (JPEG only for now)
static bool isImageFile(const char* name)
{
    String filename = String(name);
    filename.toLowerCase();
    return filename.endsWith(".jpg") || filename.endsWith(".jpeg");
}

// JPEG decoder instance and canvas buffer
static JPEGDEC jpeg;
static lv_obj_t *img_canvas = nullptr;
static lv_color_t *canvas_buf = nullptr;
static int canvas_width = 0;
static int canvas_height = 0;
static int img_offset_x = 0;
static int img_offset_y = 0;

// Close image overlay callback
static void close_image_cb(lv_event_t *e)
{
    if (img_overlay) {
        lv_obj_del(img_overlay);
        img_overlay = nullptr;
        img_view = nullptr;
        img_canvas = nullptr;
        showingImage = false;

        // Free canvas buffer
        if (canvas_buf) {
            heap_caps_free(canvas_buf);
            canvas_buf = nullptr;
        }
    }
}

// JPEG draw callback - called for each decoded MCU block
static int jpegDrawCallback(JPEGDRAW *pDraw)
{
    if (!canvas_buf) return 0;

    // Draw pixels to canvas buffer
    uint16_t *src = pDraw->pPixels;
    for (int y = 0; y < pDraw->iHeight; y++) {
        int destY = pDraw->y + y + img_offset_y;
        if (destY < 0 || destY >= canvas_height) continue;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int destX = pDraw->x + x + img_offset_x;
            if (destX < 0 || destX >= canvas_width) continue;

            uint16_t pixel = src[y * pDraw->iWidth + x];
            // Convert RGB565 to lv_color
            canvas_buf[destY * canvas_width + destX] = lv_color_make(
                (pixel >> 8) & 0xF8,  // R
                (pixel >> 3) & 0xFC,  // G
                (pixel << 3) & 0xF8   // B
            );
        }
    }
    return 1;
}

// Show image in fullscreen overlay
static void showImage(const char* filepath)
{
    Serial.printf("Opening image: %s\n", filepath);

    // Open and read file to PSRAM
    File f = SD_MMC.open(filepath);
    if (!f) {
        Serial.printf("File not found: %s\n", filepath);
        return;
    }
    size_t fileSize = f.size();
    Serial.printf("File size: %u bytes\n", fileSize);

    // Limit file size (2MB max)
    if (fileSize > 2 * 1024 * 1024) {
        Serial.println("File too large");
        f.close();
        return;
    }

    // Allocate buffer for JPEG data in PSRAM
    uint8_t *jpegBuf = (uint8_t *)heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM);
    if (!jpegBuf) {
        Serial.println("Failed to allocate JPEG buffer");
        f.close();
        return;
    }

    // Read file
    size_t bytesRead = f.read(jpegBuf, fileSize);
    f.close();
    if (bytesRead != fileSize) {
        Serial.println("Failed to read file");
        heap_caps_free(jpegBuf);
        return;
    }

    // Open JPEG decoder with RAM buffer
    if (!jpeg.openRAM(jpegBuf, fileSize, jpegDrawCallback)) {
        Serial.println("Failed to open JPEG");
        heap_caps_free(jpegBuf);
        return;
    }

    int imgW = jpeg.getWidth();
    int imgH = jpeg.getHeight();
    Serial.printf("JPEG: %dx%d\n", imgW, imgH);

    // Calculate display size (fit to screen 480x320)
    canvas_width = 480;
    canvas_height = 320;

    // Center image
    img_offset_x = (canvas_width - imgW) / 2;
    img_offset_y = (canvas_height - imgH) / 2;
    if (img_offset_x < 0) img_offset_x = 0;
    if (img_offset_y < 0) img_offset_y = 0;

    // Allocate canvas buffer in PSRAM
    canvas_buf = (lv_color_t *)heap_caps_malloc(canvas_width * canvas_height * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!canvas_buf) {
        Serial.println("Failed to allocate canvas buffer");
        jpeg.close();
        heap_caps_free(jpegBuf);
        return;
    }

    // Fill with black
    for (int i = 0; i < canvas_width * canvas_height; i++) {
        canvas_buf[i] = lv_color_black();
    }

    // Decode JPEG
    Serial.println("Decoding JPEG...");
    jpeg.decode(0, 0, 0);  // No scaling
    jpeg.close();
    heap_caps_free(jpegBuf);
    Serial.println("Decode done");

    bsp_display_lock(0);

    // Create fullscreen overlay
    img_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(img_overlay);
    lv_obj_set_size(img_overlay, 480, 320);
    lv_obj_align(img_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(img_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(img_overlay, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(img_overlay, close_image_cb, LV_EVENT_CLICKED, NULL);

    // Create canvas with decoded image
    img_canvas = lv_canvas_create(img_overlay);
    lv_canvas_set_buffer(img_canvas, canvas_buf, canvas_width, canvas_height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_center(img_canvas);

    // Add close hint label
    lv_obj_t *hint = lv_label_create(img_overlay);
    lv_label_set_text(hint, "Tap pour fermer");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x888888), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    showingImage = true;

    bsp_display_unlock();
}

// Image click callback
static void image_click_cb(lv_event_t *e)
{
    const char *filepath = (const char *)lv_event_get_user_data(e);
    if (filepath) {
        showImage(filepath);
    }
}

// Initialize SD card
static bool initSDCard()
{
    Serial.println("Setting SD_MMC pins...");
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

    Serial.println("Calling SD_MMC.begin()...");
    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 5)) {
        Serial.println("SD Card mount failed!");
        return false;
    }

    sdcard_type_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached!");
        SD_MMC.end();
        return false;
    }

    Serial.printf("SD Card Type: %s\n", getCardTypeString(cardType));
    Serial.printf("SD Card Size: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
    return true;
}

// Forward declaration
static void updateFileList();

// Navigate to folder
static void navigateTo(const char* folder)
{
    if (strcmp(folder, "..") == 0) {
        // Go up
        int lastSlash = currentPath.lastIndexOf('/', currentPath.length() - 2);
        if (lastSlash > 0) {
            currentPath = currentPath.substring(0, lastSlash + 1);
        } else {
            currentPath = "/";
        }
    } else {
        // Go into folder
        if (!currentPath.endsWith("/")) currentPath += "/";
        currentPath += folder;
        if (!currentPath.endsWith("/")) currentPath += "/";
    }
    updateFileList();
}

// File item click callback
static void file_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name) {
        navigateTo(name);
    }
}

// Forward declarations
static void showSDInfo();
static void updateFileList();

// Back button callback
static void btn_back_cb(lv_event_t *e)
{
    if (showingInfo) {
        showingInfo = false;
        updateFileList();
    } else {
        navigateTo("..");
    }
}

// Info button callback
static void btn_info_cb(lv_event_t *e)
{
    showingInfo = !showingInfo;
    if (showingInfo) {
        showSDInfo();
    } else {
        updateFileList();
    }
}

// Refresh button callback
static void btn_refresh_cb(lv_event_t *e)
{
    if (showingInfo) {
        showSDInfo();
    } else {
        updateFileList();
    }
}

// Update file list
static void updateFileList()
{
    bsp_display_lock(0);

    // Update path label
    lv_label_set_text(label_path, currentPath.c_str());

    // Clear list
    lv_obj_clean(list_files);

    if (!sdCardOk) {
        lv_obj_t *btn = lv_list_add_btn(list_files, LV_SYMBOL_WARNING, "Carte SD non detectee");
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), lv_color_hex(COLOR_ERROR), 0);
        bsp_display_unlock();
        return;
    }

    File root = SD_MMC.open(currentPath);
    if (!root || !root.isDirectory()) {
        lv_obj_t *btn = lv_list_add_btn(list_files, LV_SYMBOL_WARNING, "Erreur ouverture dossier");
        bsp_display_unlock();
        return;
    }

    int count = 0;
    File entry = root.openNextFile();

    while (entry && count < 50) {
        const char* name = entry.name();
        bool isDir = entry.isDirectory();
        size_t size = entry.size();

        // Skip hidden files
        if (name[0] != '.') {
            // Build display text
            char displayText[80];
            if (isDir) {
                snprintf(displayText, sizeof(displayText), "%s/", name);
            } else {
                snprintf(displayText, sizeof(displayText), "%s  (%s)", name, formatSize(size).c_str());
            }

            // Add to list
            const char* icon = isDir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE;
            lv_obj_t *btn = lv_list_add_btn(list_files, icon, displayText);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);

            // Color icon (child 0) and text (child 1)
            lv_obj_t *img = lv_obj_get_child(btn, 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 1);
            if (img) {
                lv_obj_set_style_text_color(img, lv_color_hex(isDir ? COLOR_FOLDER : COLOR_FILE), 0);
            }
            if (lbl) {
                lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
            }

            // Add click handler for directories and images
            if (isDir) {
                // Store name in button's user data
                static char storedNames[50][64];
                strncpy(storedNames[count], name, 63);
                storedNames[count][63] = '\0';
                lv_obj_add_event_cb(btn, file_click_cb, LV_EVENT_CLICKED, storedNames[count]);
            } else if (isImageFile(name)) {
                // Store full path for images
                static char storedPaths[50][128];
                String fullPath = currentPath;
                if (!fullPath.endsWith("/")) fullPath += "/";
                fullPath += name;
                strncpy(storedPaths[count], fullPath.c_str(), 127);
                storedPaths[count][127] = '\0';
                lv_obj_add_event_cb(btn, image_click_cb, LV_EVENT_CLICKED, storedPaths[count]);
                // Highlight images
                lv_obj_set_style_text_color(img, lv_color_hex(COLOR_ACCENT), 0);
            }

            count++;
        }

        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    // Update title
    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "%d element%s", count, count > 1 ? "s" : "");
    lv_label_set_text(label_title, titleBuf);

    bsp_display_unlock();
}

// Show SD info
static void showSDInfo()
{
    bsp_display_lock(0);

    lv_label_set_text(label_title, LV_SYMBOL_SD_CARD " Infos SD");
    lv_label_set_text(label_path, "Informations techniques");
    lv_obj_clean(list_files);

    if (!sdCardOk) {
        lv_obj_t *btn = lv_list_add_btn(list_files, LV_SYMBOL_WARNING, "Carte SD non detectee!");
        lv_obj_set_style_text_color(btn, lv_color_hex(COLOR_ERROR), 0);
    } else {
        uint64_t cardSize = SD_MMC.cardSize();
        uint64_t totalBytes = SD_MMC.totalBytes();
        uint64_t usedBytes = SD_MMC.usedBytes();
        sdcard_type_t cardType = SD_MMC.cardType();
        float usagePercent = totalBytes > 0 ? (usedBytes * 100.0 / totalBytes) : 0;

        char buf[64];

        snprintf(buf, sizeof(buf), "Type: %s", getCardTypeString(cardType));
        lv_list_add_btn(list_files, LV_SYMBOL_SD_CARD, buf);

        snprintf(buf, sizeof(buf), "Capacite: %s", formatSize(cardSize).c_str());
        lv_list_add_btn(list_files, LV_SYMBOL_DRIVE, buf);

        snprintf(buf, sizeof(buf), "Espace total: %s", formatSize(totalBytes).c_str());
        lv_list_add_btn(list_files, LV_SYMBOL_DIRECTORY, buf);

        snprintf(buf, sizeof(buf), "Utilise: %s (%.1f%%)", formatSize(usedBytes).c_str(), usagePercent);
        lv_list_add_btn(list_files, LV_SYMBOL_FILE, buf);

        snprintf(buf, sizeof(buf), "Libre: %s", formatSize(totalBytes - usedBytes).c_str());
        lv_list_add_btn(list_files, LV_SYMBOL_OK, buf);
    }

    bsp_display_unlock();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("\nSD_Browser - JC3248W535C");

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
    Serial.println("Display OK");

    // Create UI
    bsp_display_lock(0);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(COLOR_BG), 0);

    // Title bar
    lv_obj_t *bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 480, 45);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    // Back button
    btn_back = lv_btn_create(bar);
    lv_obj_set_size(btn_back, 45, 35);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(btn_back, btn_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn_back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    // Title
    label_title = lv_label_create(bar);
    lv_label_set_text(label_title, LV_SYMBOL_SD_CARD " SD Browser");
    lv_obj_set_style_text_color(label_title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_18, 0);
    lv_obj_align(label_title, LV_ALIGN_LEFT_MID, 60, 0);

    // Info button
    btn_info = lv_btn_create(bar);
    lv_obj_set_size(btn_info, 45, 35);
    lv_obj_align(btn_info, LV_ALIGN_RIGHT_MID, -55, 0);
    lv_obj_set_style_bg_color(btn_info, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(btn_info, btn_info_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *il = lv_label_create(btn_info);
    lv_label_set_text(il, LV_SYMBOL_LIST);
    lv_obj_center(il);

    // Refresh button
    btn_refresh = lv_btn_create(bar);
    lv_obj_set_size(btn_refresh, 45, 35);
    lv_obj_align(btn_refresh, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_refresh, btn_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(btn_refresh);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(rl, lv_color_hex(0x000000), 0);
    lv_obj_center(rl);

    // Path label
    label_path = lv_label_create(lv_scr_act());
    lv_label_set_text(label_path, "/");
    lv_obj_set_style_text_color(label_path, lv_color_hex(COLOR_DIMMED), 0);
    lv_obj_align(label_path, LV_ALIGN_TOP_LEFT, 10, 50);

    // File list
    list_files = lv_list_create(lv_scr_act());
    lv_obj_set_size(list_files, 460, 230);
    lv_obj_align(list_files, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(list_files, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(list_files, 0, 0);
    lv_obj_set_style_text_color(list_files, lv_color_hex(COLOR_TEXT), 0);

    bsp_display_unlock();
    Serial.println("UI created");

    // Initialize SD
    delay(100);
    sdCardOk = initSDCard();
    Serial.printf("SD init: %s\n", sdCardOk ? "OK" : "FAILED");

    // Show content
    if (sdCardOk) {
        updateFileList();
    } else {
        showSDInfo();
    }

    Serial.println("Setup complete!");
}

void loop()
{
    delay(100);
}
