/* adsbbadge.cpp -- on-map "ADS-B" badge for HamClock
 *
 * Unlike the Borders/Fires badges this has no on/off state of its own and draws nothing onto the
 * map itself -- it's just a link button, same idea as the WEFAX/Wind badges (which launch/link
 * out instead of toggling an overlay). Tapping it opens ADS-B Exchange's live globe map, centered
 * on DE, via openURLPopupEmbed() -- see qrz.cpp.
 *
 * On-map presence, not buried in the map menu: shown on Countries, Terrain, Clouds (the maps
 * Borders/Fires can appear on) and Weather (the map WEFAX/Wind appear on), in any projection.
 * Borders/Fires restrict themselves to the Mercator/Robinson projections because they toggle a
 * map overlay that only renders sensibly in those -- that restriction doesn't apply here, since
 * this badge doesn't draw anything on the map itself, just floats along the always-present View
 * button's row (which exists regardless of projection) and opens a link when tapped.
 */

#include "HamClock.h"

SBox adsbmap_btn_b;                    // extern; badge box, geometry set each draw (floats with Borders/Fires)

/* return whether the on-map "ADS-B" badge should currently be shown.
 * Countries, Terrain, Clouds, or Weather -- any projection, since this badge draws nothing onto
 * the map and so has none of the rendering reasons Borders/Fires restrict themselves to Mercator/
 * Robinson. Hiding it has no other side effect: there's no persisted state to preserve, unlike
 * Borders/Fires, since this badge doesn't toggle anything -- it's just a link.
 */
bool adsbBadgeVisible(void)
{
    return (core_map == CM_COUNTRIES || core_map == CM_TERRAIN
                || core_map == CM_CLOUDS || core_map == CM_WX);
}

/* draw (or blank) the on-map "ADS-B" badge.
 * Floats to the right of whichever badge is currently rightmost in its row: on
 * Countries/Terrain/Clouds that's Fires, else Borders, else View; on Weather that's Wind, else
 * WEFAX, else View. Recomputed here every draw, same convention as drawFiresButton()/
 * drawWindButton() tracking their own neighbors.
 */
void drawADSBBadge(void)
{
    adsbmap_btn_b.y = view_btn_b.y;

    if (!adsbBadgeVisible()) {
        // wrong core map -- leave the map pixels already painted here alone.
        return;
    }

    const int gap = 4;
    const int pad = 8;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t left_edge = view_btn_b.x + view_btn_b.w;
    if (bordersBadgeVisible())
        left_edge = borders_btn_b.x + borders_btn_b.w;
    if (firesBadgeVisible())
        left_edge = fires_btn_b.x + fires_btn_b.w;
    if (wefaxBadgeVisible())
        left_edge = wefax_btn_b.x + wefax_btn_b.w;
    if (windBadgeVisible())
        left_edge = windmap_btn_b.x + windmap_btn_b.w;
    adsbmap_btn_b.x = left_edge + gap;
    adsbmap_btn_b.w = getTextWidth ("ADS-B") + pad;
    adsbmap_btn_b.h = view_btn_b.h;

    // always the same "unpressed" look -- black fill, white outline/text -- since it has no
    // on/off state, just Borders/Fires' normal (off) styling permanently.
    tft.fillRect (adsbmap_btn_b.x, adsbmap_btn_b.y, adsbmap_btn_b.w-1, adsbmap_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (adsbmap_btn_b.x, adsbmap_btn_b.y, adsbmap_btn_b.w-1, adsbmap_btn_b.h-1, RA8875_WHITE);

    const char *label = "ADS-B";
    uint16_t lbl_w = getTextWidth(label);
    tft.setCursor (adsbmap_btn_b.x+(adsbmap_btn_b.w-lbl_w)/2, adsbmap_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (label);
}

/* open ADS-B Exchange's live globe map, centered on DE, or the user's PiAware receiver if one is
 * configured in Setup -- same source and fallback as the ADS-B icon in the clock area (clocks.cpp).
 * uses openURLPopupEmbed(): on the X11 desktop build this is a chromeless Chromium app-mode popup
 * positioned near the HamClock window; under Live Web it's the in-page <iframe> overlay, since
 * ADS-B Exchange's globe map is confirmed to allow being framed; anywhere else (fb0, ESP32,
 * Android, or no Chromium found) it's a plain new browser tab -- see qrz.cpp.
 * call this from checkTouch() when adsbBadgeVisible() && inBox(s, adsbmap_btn_b).
 */
void adsbBadgeClicked(void)
{
    char url[100];
    const char *piaware_host = getPiAwareHost();
    if (piaware_host[0])
        snprintf (url, sizeof(url), "http://%s/skyaware/", piaware_host);
    else
        snprintf (url, sizeof(url), "https://globe.adsbexchange.com/?lat=%.3f&lon=%.3f&zoom=10",
                    de_ll.lat_d, de_ll.lng_d);
    openURLPopupEmbed (url);
}
