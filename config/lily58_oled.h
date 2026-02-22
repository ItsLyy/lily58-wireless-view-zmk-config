/*
 * Lily58 – Custom OLED Configuration
 *
 * Left  half : Logo (idle) → Layer name + Modifier status
 * Right half : WPM counter + animated bar
 *
 * Drop this file next to your lily58.keymap and add
 *   #include "lily58_oled.h"
 * at the top of lily58.keymap, OR merge the display {} node
 * directly into your keymap's root node.
 *
 * Requires in lily58.conf:
 *   CONFIG_ZMK_DISPLAY=y
 *   CONFIG_ZMK_DISPLAY_INVERT=n        # flip if screen looks inverted
 *   CONFIG_ZMK_WPM=y
 */

/ {
    chosen {
        zephyr,display = &oled;
    };
};

/* ─────────────────────────────────────────────────────────────────────
   Custom OLED widget – implemented as a ZMK display status screen.

   ZMK renders the "status screen" by calling zmk_display_status_screen()
   which you override below.  The two halves share the same firmware but
   IS_LEFT / IS_RIGHT let us branch at runtime.
───────────────────────────────────────────────────────────────────── */
