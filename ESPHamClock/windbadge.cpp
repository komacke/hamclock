/* windbadge.cpp -- on-map "Wind" badge for HamClock
 *
 * Same idea as adsbbadge.cpp: no on/off state of its own, draws nothing onto the map, just a
 * link button. Tapping it opens Windy.com centered on DE via openURLPopup() -- a Chromium
 * app-mode popup on the X11 desktop build, or a plain new browser tab everywhere else -- see
 * qrz.cpp.
 *
 * On-map presence, not buried in the map menu: only shown when core_map == CM_WX (the "Weather"
 * core map style, ie the one people actually think to look for wind info on), same convention as
 * wefaxBadgeVisible(). It floats to the right of the WEFAX badge, which shares the row on CM_WX
 * and can be showing at the same time, or right of View when WEFAX isn't enabled.
 */

#include "HamClock.h"

SBox windmap_btn_b;                    // extern; badge box, geometry set each draw (floats with WEFAX)

/* return whether the on-map "Wind" badge should currently be shown.
 * Weather map only -- no projection restriction, matching WEFAX's own simplicity, since this is
 * just a link and draws nothing that would look wrong in an azimuthal projection.
 */
bool windBadgeVisible(void)
{
    return (core_map == CM_WX);
}

/* draw (or blank) the on-map "Wind" badge.
 * Floats to the right of the WEFAX badge, when showing, else right of View -- recomputed here
 * every draw, same convention as drawFiresButton()/drawADSBBadge() tracking their neighbors.
 */
void drawWindButton(void)
{
    windmap_btn_b.y = view_btn_b.y;

    if (!windBadgeVisible()) {
        // wrong core map -- leave the map pixels already painted here alone.
        return;
    }

    const int gap = 4;
    const int pad = 8;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t left_edge = wefaxBadgeVisible() ? (wefax_btn_b.x + wefax_btn_b.w)
                                              : (view_btn_b.x + view_btn_b.w);
    windmap_btn_b.x = left_edge + gap;
    windmap_btn_b.w = getTextWidth ("Wind") + pad;
    windmap_btn_b.h = view_btn_b.h;

    // always the same "unpressed" look -- black fill, white outline/text -- since it has no
    // on/off state, just Borders/Fires' normal (off) styling permanently.
    tft.fillRect (windmap_btn_b.x, windmap_btn_b.y, windmap_btn_b.w-1, windmap_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (windmap_btn_b.x, windmap_btn_b.y, windmap_btn_b.w-1, windmap_btn_b.h-1, RA8875_WHITE);

    const char *label = "Wind";
    uint16_t lbl_w = getTextWidth(label);
    tft.setCursor (windmap_btn_b.x+(windmap_btn_b.w-lbl_w)/2, windmap_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (label);
}

/* open Windy.com centered on DE.
 * uses openURLPopup(): on the X11 desktop build this is a chromeless Chromium app-mode popup
 * positioned near the HamClock window; everywhere else (Live Web, fb0, ESP32, Android, or no
 * Chromium found) it's a plain new browser tab -- see qrz.cpp.
 * call this from checkTouch() when windBadgeVisible() && inBox(s, windmap_btn_b).
 */
void windBadgeClicked(void)
{
    char url[100];
    snprintf (url, sizeof(url), "https://www.windy.com/?%.3f,%.3f,8", de_ll.lat_d, de_ll.lng_d);
    openURLPopup (url);
}
