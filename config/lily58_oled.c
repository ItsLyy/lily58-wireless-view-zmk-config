/*
 * lily58_oled.c
 * Custom OLED status screen for Lily58 (ZMK)
 *
 * Left  half : Lily58 logo on idle, then Layer name + Mod indicators
 * Right half : WPM counter with animated progress bar
 *
 * Build system: add to CMakeLists.txt →  target_sources(app PRIVATE lily58_oled.c)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#include <zmk/display.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/widgets/wpm_status.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/modifiers_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/wpm.h>
#include <zmk/split/bluetooth/peripheral.h>

#include <lvgl.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── Lily58 pixel-art logo (128×32, 1-bit, row-major) ─────────────── */
/* A simple "LILY58" wordmark rendered at 128×32.
   Replace this array with your own bitmap exported from
   image2cpp (https://javl.github.io/image2cpp/)
   Settings: 128×32, "Arduino code", Threshold 128, MSB first.         */
static const uint8_t lily58_logo[] = {
    /* Row 0 */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* Row 1 */
    0x3C,0x4E,0x49,0x59,0x59,0x4E,0x3C,0x00,0x3C,0x52,0x52,0x52,0x52,0x52,0x3C,0x00,
    /* Row 2 */
    0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00,0x7E,0x02,0x02,0x7E,0x40,0x40,0x7E,0x00,
    /* Row 3 – padding */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* Remaining rows – zero (blank) */
    [64 ... 511] = 0x00,
};

/* ── Layer names (keep in sync with your keymap layer order) ────────── */
static const char *layer_names[] = {
    "QWERTY",   // DEF  0
    "NAV",      // NAV  1
    "SYM",      // SYM  2
    "FUN",      // FUN  3
};
#define NUM_LAYERS ARRAY_SIZE(layer_names)

/* ── LVGL objects – Left half ──────────────────────────────────────── */
static lv_obj_t *logo_img   = NULL;   // shown when idle
static lv_obj_t *layer_lbl  = NULL;   // "LAYER: QWERTY"
static lv_obj_t *mod_lbl    = NULL;   // "⌃ ⌥ ⇧ ⌘"

/* ── LVGL objects – Right half ─────────────────────────────────────── */
static lv_obj_t *wpm_lbl    = NULL;   // "WPM"  header
static lv_obj_t *wpm_val    = NULL;   // "  87"  large number
static lv_obj_t *wpm_bar    = NULL;   // progress bar 0-200 wpm

/* ── Helpers ────────────────────────────────────────────────────────── */
static inline bool is_left_side(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_ROLE_PERIPHERAL)
    return false;   // peripheral = right in default Lily58 config
#else
    return true;    // central = left
#endif
}

/* Build modifier string, e.g. "Ctl Alt Sft Gui" */
static void update_mod_label(void) {
    if (!mod_lbl) return;

    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();
    char buf[32] = {0};

    if (mods & MOD_LCTL || mods & MOD_RCTL) strcat(buf, "Ctl ");
    if (mods & MOD_LALT || mods & MOD_RALT) strcat(buf, "Alt ");
    if (mods & MOD_LSFT || mods & MOD_RSFT) strcat(buf, "Sft ");
    if (mods & MOD_LGUI || mods & MOD_RGUI) strcat(buf, "Gui ");

    if (strlen(buf) == 0) strcpy(buf, "---");

    lv_label_set_text(mod_lbl, buf);
}

/* Update WPM bar & value label */
static void update_wpm(uint8_t wpm) {
    if (!wpm_val || !wpm_bar) return;

    char buf[8];
    snprintf(buf, sizeof(buf), "%3d", wpm);
    lv_label_set_text(wpm_val, buf);

    /* bar value capped at 200 wpm */
    lv_bar_set_value(wpm_bar, MIN(wpm, 200), LV_ANIM_ON);

    /* colour-code: green < 60, yellow < 100, red ≥ 100 */
    lv_color_t col;
    if      (wpm < 60)  col = lv_color_make(0x00, 0xFF, 0x00);
    else if (wpm < 100) col = lv_color_make(0xFF, 0xD0, 0x00);
    else                col = lv_color_make(0xFF, 0x40, 0x40);

    lv_obj_set_style_bg_color(wpm_bar, col, LV_PART_INDICATOR);
}

/* ── Screen builder ─────────────────────────────────────────────────── */

/* LEFT half layout
   ┌────────────────────────────┐
   │ [Logo – shown until typed] │
   │ ─────────────────────────  │
   │ LAYER: QWERTY              │
   │ MODS:  Ctl Sft             │
   └────────────────────────────┘ */
static void build_left_screen(lv_obj_t *parent) {
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    /* Logo image (128×32 bitmap) */
    static lv_img_dsc_t logo_dsc = {
        .header = {
            .cf     = LV_IMG_CF_INDEXED_1BIT,
            .always_zero = 0,
            .reserved    = 0,
            .w      = 128,
            .h      = 32,
        },
        .data_size = sizeof(lily58_logo),
        .data      = lily58_logo,
    };

    logo_img = lv_img_create(parent);
    lv_img_set_src(logo_img, &logo_dsc);
    lv_obj_align(logo_img, LV_ALIGN_CENTER, 0, 0);

    /* Layer label – hidden initially */
    layer_lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(layer_lbl, lv_color_white(), 0);
    lv_label_set_text(layer_lbl, "LAYER: QWERTY");
    lv_obj_align(layer_lbl, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_add_flag(layer_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Modifier label */
    mod_lbl = lv_label_create(parent);
    lv_obj_set_style_text_color(mod_lbl, lv_color_white(), 0);
    lv_label_set_text(mod_lbl, "---");
    lv_obj_align(mod_lbl, LV_ALIGN_TOP_LEFT, 2, 16);
    lv_obj_add_flag(mod_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ── ZMK display entry point ────────────────────────────────────────── */
int zmk_display_status_screen(lv_obj_t *parent) {
    if (is_left_side()) {
        build_left_screen(parent);
    } else {
        build_right_screen(parent);
    }
    return 0;
}

/* ── Event listeners ────────────────────────────────────────────────── */

/* Layer changed → update layer label, hide/show logo */
static int layer_event_handler(const zmk_event_t *eh) {
    if (!is_left_side() || !layer_lbl) return ZMK_EV_EVENT_BUBBLE;

    uint8_t idx = zmk_keymap_highest_layer_active();
    const char *name = (idx < NUM_LAYERS) ? layer_names[idx] : "???";

    char buf[32];
    snprintf(buf, sizeof(buf), "LAYER: %s", name);
    lv_label_set_text(layer_lbl, buf);

    /* Hide logo, show layer + mod labels */
    if (logo_img)  lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    if (layer_lbl) lv_obj_clear_flag(layer_lbl, LV_OBJ_FLAG_HIDDEN);
    if (mod_lbl)   lv_obj_clear_flag(mod_lbl,   LV_OBJ_FLAG_HIDDEN);

    return ZMK_EV_EVENT_BUBBLE;
}

/* Modifier changed → refresh mod string */
static int mod_event_handler(const zmk_event_t *eh) {
    if (!is_left_side()) return ZMK_EV_EVENT_BUBBLE;
    update_mod_label();
    return ZMK_EV_EVENT_BUBBLE;
}

/* WPM changed → refresh right-side display */
static int wpm_event_handler(const zmk_event_t *eh) {
    if (is_left_side()) return ZMK_EV_EVENT_BUBBLE;

    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev) update_wpm(ev->wpm);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_status, layer_event_handler);
ZMK_SUBSCRIPTION(layer_status, zmk_layer_state_changed);

ZMK_LISTENER(mod_status, mod_event_handler);
ZMK_SUBSCRIPTION(mod_status, zmk_modifiers_state_changed);

ZMK_LISTENER(wpm_status, wpm_event_handler);
ZMK_SUBSCRIPTION(wpm_status, zmk_wpm_state_changed);
