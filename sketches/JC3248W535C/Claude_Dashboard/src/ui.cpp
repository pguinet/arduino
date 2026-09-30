#include "ui.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

using namespace dash;

#define COLOR_BG      0x0f1419
#define COLOR_CARD    0x1c2733
#define COLOR_TRACK   0x2a3a4a
#define COLOR_TEXT    0xffffff
#define COLOR_DIM     0x888888
#define COLOR_WORKING 0x4cc9f0
#define COLOR_IDLE    0xfca311
#define COLOR_PERM    0xf72585
#define COLOR_OK      0x4caf50

// Au-dela de cette date (2024-01-01), l'horloge systeme est consideree synchronisee.
static const time_t CLOCK_VALID_AFTER = 1704067200;
// Au-dela, un libelle "+N autres sessions" : les lignes sont triees par urgence,
// rien d'important n'est coupe, et on borne la memoire LVGL (~12 objets par carte).
static const int MAX_CARDS = 16;

static lv_obj_t *lblHost, *dotMqtt, *lblClock, *banner, *list, *lblEmpty;
static lv_obj_t *bar5h, *pct5h, *reset5h, *bar7d, *pct7d, *reset7d;

// Chronos des cartes, mis a jour chaque seconde sans reconstruire la liste
static lv_obj_t *durLabels[MAX_CARDS];
static int64_t durSince[MAX_CARDS];
static int durCount = 0;

// Etat affiche par ui_tick : ne toucher aux objets que s'il change (sinon LVGL
// redessine chaque seconde meme sans session).
static int shownMqtt = -1;
static int shownStale = -1;

// N'invalide le label que si le texte change.
static void setText(lv_obj_t *l, const char *text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

static uint32_t stateColor(State s) {
    return s == State::Permission ? COLOR_PERM : s == State::Idle ? COLOR_IDLE : COLOR_WORKING;
}

static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, "");
    return l;
}

static lv_obj_t *mkBar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_bar_set_range(b, 0, 100);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
    return b;
}

// Colore et remplit une barre de pourcentage ; value < 0 = inconnu (barre vide, "--").
static void setPercent(lv_obj_t *bar, lv_obj_t *pct, int value) {
    if (value < 0) {
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(pct, "--");
        return;
    }
    if (value > 100) value = 100;
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(colorForPercent(value)), LV_PART_INDICATOR);
    lv_label_set_text_fmt(pct, "%d%%", value);
}

static lv_obj_t *mkQuota(lv_obj_t *parent, lv_coord_t y, const char *name, lv_obj_t **pct,
                         lv_obj_t **reset) {
    lv_obj_t *l = mkLabel(parent, &lv_font_montserrat_16, COLOR_TEXT);
    lv_label_set_text(l, name);
    lv_obj_set_pos(l, 10, y);
    lv_obj_t *b = mkBar(parent, 45, y + 3, 220, 14);
    *pct = mkLabel(parent, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_set_pos(*pct, 275, y);
    *reset = mkLabel(parent, &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_pos(*reset, 330, y + 1);
    setPercent(b, *pct, -1);
    return b;
}

void ui_create() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // En-tete (0-30)
    lv_obj_t *title = mkLabel(scr, &lv_font_montserrat_18, COLOR_TEXT);
    lv_label_set_text(title, "Claude Code");
    lv_obj_set_pos(title, 10, 6);
    lblHost = mkLabel(scr, &lv_font_montserrat_16, COLOR_DIM);
    lv_label_set_long_mode(lblHost, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lblHost, 190);
    lv_obj_set_pos(lblHost, 170, 7);
    dotMqtt = lv_obj_create(scr);
    lv_obj_set_size(dotMqtt, 12, 12);
    lv_obj_set_style_radius(dotMqtt, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dotMqtt, 0, 0);
    lv_obj_set_style_bg_color(dotMqtt, lv_color_hex(COLOR_PERM), 0);
    lv_obj_clear_flag(dotMqtt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(dotMqtt, 370, 10);
    lblClock = mkLabel(scr, &lv_font_montserrat_18, COLOR_TEXT);
    lv_label_set_text(lblClock, "--:--");
    lv_obj_set_pos(lblClock, 410, 6);

    // Quotas (34-80)
    bar5h = mkQuota(scr, 36, "5h", &pct5h, &reset5h);
    bar7d = mkQuota(scr, 60, "7j", &pct7d, &reset7d);

    // Liste des sessions (86-320), defilement vertical
    list = lv_obj_create(scr);
    lv_obj_set_pos(list, 0, 86);
    lv_obj_set_size(list, 480, 234);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    lblEmpty = mkLabel(scr, &lv_font_montserrat_18, COLOR_DIM);
    lv_label_set_text(lblEmpty, "En attente de donnees...");
    lv_obj_align(lblEmpty, LV_ALIGN_CENTER, 0, 60);

    // Bandeau d'alerte (serveur injoignable), cache par defaut
    banner = lv_label_create(scr);
    lv_obj_set_width(banner, 480);
    lv_obj_set_style_bg_color(banner, lv_color_hex(COLOR_PERM), 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(banner, lv_color_white(), 0);
    lv_obj_set_style_text_font(banner, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(banner, 4, 0);
    lv_obj_align(banner, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
}

static void renderLimit(lv_obj_t *bar, lv_obj_t *pct, lv_obj_t *reset, int value,
                        int64_t resetAt, bool withDay) {
    setPercent(bar, pct, value);
    if (value < 0 || resetAt <= 0) {
        lv_label_set_text(reset, "");
        return;
    }
    static const char *const DAYS[] = {"dim", "lun", "mar", "mer", "jeu", "ven", "sam"};
    time_t t = (time_t)resetAt;
    struct tm tm;
    localtime_r(&t, &tm);
    if (withDay)
        lv_label_set_text_fmt(reset, "reset %s %02d:%02d", DAYS[tm.tm_wday], tm.tm_hour, tm.tm_min);
    else
        lv_label_set_text_fmt(reset, "reset %02d:%02d", tm.tm_hour, tm.tm_min);
}

static lv_obj_t *mkCardLabel(lv_obj_t *card, const lv_font_t *font, uint32_t color, lv_coord_t x,
                             lv_coord_t y, lv_coord_t w) {
    lv_obj_t *l = mkLabel(card, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void addCard(const Row &r, bool showHost) {
    const Session &s = *r.session;
    uint32_t color = stateColor(s.state);

    lv_obj_t *card = lv_obj_create(list);
    lv_obj_set_size(card, lv_pct(100), 62);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(card, 5, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Ligne 1 : projet(@hote) | modele | etat | outil | chrono
    lv_obj_t *name = mkCardLabel(card, &lv_font_montserrat_18, COLOR_TEXT, 4, 0, 150);
    if (showHost) lv_label_set_text_fmt(name, "%s@%s", s.project, r.host);
    else lv_label_set_text(name, s.project);

    lv_obj_t *model = mkCardLabel(card, &lv_font_montserrat_14, COLOR_DIM, 160, 3, 85);
    lv_label_set_text(model, s.model);

    lv_obj_t *state = mkCardLabel(card, &lv_font_montserrat_14, color, 250, 3, 92);
    lv_label_set_text(state, stateLabel(s.state));

    lv_obj_t *tool = mkCardLabel(card, &lv_font_montserrat_14, COLOR_DIM, 345, 3, 55);
    lv_label_set_text(tool, s.state == State::Idle ? "" : s.tool);

    lv_obj_t *dur = mkLabel(card, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_align(dur, LV_ALIGN_TOP_RIGHT, 0, 1);
    if (durCount < MAX_CARDS) {
        durLabels[durCount] = dur;
        durSince[durCount] = s.since;
        durCount++;
    }

    // Ligne 2 : jauge de contexte
    lv_obj_t *ctxLbl = mkLabel(card, &lv_font_montserrat_14, COLOR_DIM);
    lv_label_set_text(ctxLbl, "ctx");
    lv_obj_set_pos(ctxLbl, 4, 28);
    lv_obj_t *bar = mkBar(card, 36, 32, 140, 10);
    lv_obj_t *pct = mkLabel(card, &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_pos(pct, 184, 28);
    setPercent(bar, pct, s.ctx);
}

void ui_render(const Dashboard &d, time_t now, bool mqttOk) {
    const Limits *lim = d.limits();
    renderLimit(bar5h, pct5h, reset5h, lim ? lim->h5 : -1, lim ? lim->h5Reset : 0, false);
    renderLimit(bar7d, pct7d, reset7d, lim ? lim->d7 : -1, lim ? lim->d7Reset : 0, true);

    lv_label_set_text(lblHost, d.hostCount() == 1 ? d.hostName(0) : "");

    lv_coord_t scroll = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    durCount = 0;
    static Row rows[MAX_ROWS];  // static : evite ~400 o sur la pile de l'appelant
    int n = d.rows(rows, MAX_ROWS);
    int shown = n < MAX_CARDS ? n : MAX_CARDS;
    for (int i = 0; i < shown; i++) addCard(rows[i], d.hostCount() > 1);
    if (n > shown) {
        lv_obj_t *more = mkLabel(list, &lv_font_montserrat_14, COLOR_DIM);
        lv_label_set_text_fmt(more, "+%d autres sessions", n - shown);
    }
    lv_obj_update_layout(list);
    lv_obj_scroll_to_y(list, scroll, LV_ANIM_OFF);

    lv_label_set_text(lblEmpty,
                      d.hostCount() == 0 ? "En attente de donnees..." : "Aucune session active");
    if (n == 0) lv_obj_clear_flag(lblEmpty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(lblEmpty, LV_OBJ_FLAG_HIDDEN);

    ui_tick(d, now, mqttOk);
}

void ui_tick(const Dashboard &d, time_t now, bool mqttOk) {
    char buf[48];
    if (now > CLOCK_VALID_AFTER) {
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
        setText(lblClock, buf);
    }

    if (shownMqtt != (int)mqttOk) {
        shownMqtt = mqttOk;
        lv_obj_set_style_bg_color(dotMqtt, lv_color_hex(mqttOk ? COLOR_OK : COLOR_PERM), 0);
    }

    for (int i = 0; i < durCount; i++) {
        if (durSince[i] <= 0 || now <= CLOCK_VALID_AFTER) {
            setText(durLabels[i], "");
            continue;
        }
        formatDuration(now - durSince[i], buf, sizeof buf);
        setText(durLabels[i], buf);
    }

    int64_t stale = d.hostCount() ? d.staleSeconds(now) : 0;
    bool isStale = stale > STALE_AFTER_S;
    if (isStale) {
        snprintf(buf, sizeof buf, "Serveur injoignable depuis %d min", (int)(stale / 60));
        setText(banner, buf);
    }
    if (shownStale != (int)isStale) {
        shownStale = isStale;
        if (isStale) lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(list, isStale ? LV_OPA_50 : LV_OPA_COVER, 0);
    }
}
