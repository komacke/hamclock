/* manage the PLOT_CH_ECLIPSE option -- next solar eclipse local circumstances for DE.
 */

#include "HamClock.h"


// cached result so touch handling doesn't have to re-run the search
static EclipseCir eclipse_cir;
static bool eclipse_cir_ok;

// how long the tap-for-detail popup stays up before auto-dismissing, ms
#define ECLIPSE_POPUP_MS   4000

// non-blocking popup state: drawn on tap, cleared either by another tap (see
// checkEclipseTouch) or by checkEclipsePopupTimeout(), which loop() calls every
// iteration so the rest of the app -- clocks, DX cluster, wifi, etc -- keeps running
// the entire time the popup is up. nothing in this file ever blocks.
static bool     popup_up;
static bool     popup_has_bs;                  // whether popup_bs is valid to restore
static uint8_t *popup_bs;
static uint16_t popup_px, popup_py, popup_pw, popup_ph;
static SBox     popup_pane_box;                 // for fallback redraw if no backing store
static uint32_t popup_expire_ms;

/* draw a two-disk glyph representing the eclipse, to exact relative scale: orange sun
 * outline, dark moon disk, using the actual angular radii and separation at max so
 * partial/annular/total all look correct, not just illustrative.
 */
static void drawEclipseGlyph (const SBox &b, const EclipseCir &ec, int16_t cy)
{
        const int16_t cx = b.x + b.w/2;

        // fix the sun's on-screen radius and scale everything else to it, in real proportion
        const int16_t sun_r_px = 18;
        const float px_per_rad = sun_r_px / ec.sun_r;
        int16_t moon_r_px = (int16_t)(ec.moon_r * px_per_rad + 0.5F);
        if (moon_r_px < 1) moon_r_px = 1;
        int16_t sep_px = (int16_t)(ec.sep * px_per_rad + 0.5F);

        // offset along a fixed diagonal direction purely for visual clarity
        int16_t dx = (int16_t)(sep_px * 0.7F);
        int16_t dy = (int16_t)(sep_px * 0.3F);

        tft.drawCircle (cx - dx/2, cy - dy/2, sun_r_px, RGB565(255,140,0));
        tft.fillCircle (cx - dx/2, cy - dy/2, sun_r_px-1, RGB565(40,25,0));
        tft.fillCircle (cx + dx/2, cy + dy/2, moon_r_px, RGB565(15,15,15));
        tft.drawCircle (cx + dx/2, cy + dy/2, moon_r_px, RGB565(90,90,90));
}

/* update eclipse pane: search for and display next solar eclipse visible from DE.
 */
void updateEclipsePane (const SBox &box)
{
        char str[50];

        prepPlotBox (box);

        // run the search fresh at DE's effective time; cheap enough to always redo.
        // require_visible (the trailing true, also the default) skips any eclipse whose
        // maximum occurs while the sun is below DE's horizon, so this only ever reports
        // one that could actually be seen from here.
        time_t t0 = nowWO();
        eclipse_cir_ok = getNextSolarEclipse (t0, de_ll, 36, eclipse_cir, true);

        if (!eclipse_cir_ok) {
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (DE_COLOR);
            tft.setCursor (box.x+1, box.y+2);
            tft.print ("No eclipse found");
            return;
        }

        int detz = getTZ (de_tz);
        time_t t_max_de = eclipse_cir.t_max + detz;

        // row 1: centered pane label so it's unambiguous what this pane shows
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RGB565(150,150,150));
        const char *title = "Next Eclipse";
        tft.setCursor (box.x+(box.w-getTextWidth(title))/2, box.y+2);
        tft.print (title);

        // row 2: date (incl 2-digit year, so there's never any "which year?" ambiguity) at
        // left, type at right
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);
        snprintf (str, sizeof(str), "%d/%d/%02d", month(t_max_de), day(t_max_de), year(t_max_de)%100);
        tft.setCursor (box.x+1, box.y+14);
        tft.print (str);

        static const char *tnames[] = {"None", "Partial", "Annular", "Total"};
        strcpy (str, tnames[eclipse_cir.type]);
        tft.setCursor (box.x+box.w-getTextWidth(str)-1, box.y+14);
        tft.print (str);

        // glyph, centered in the remaining space between row 2 and the hint/bottom rows
        int16_t glyph_cy = box.y + (box.h + 24)/2 - 6;
        drawEclipseGlyph (box, eclipse_cir, glyph_cy);

        // defensive only: with require_visible above this shouldn't normally trigger, but
        // keep it in case a caller ever passes false or a future edge case slips through
        int16_t hint_y = box.y + box.h - 20;
        if (!eclipse_cir.visible) {
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (RGB565(120,120,120));
            const char *msg = "(sun down)";
            tft.setCursor (box.x+(box.w-getTextWidth(msg))/2, hint_y - 12);
            tft.print (msg);
        }

        // always show a small hint that there's more detail available on tap
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RGB565(120,120,120));
        const char *hint = "Tap for details";
        tft.setCursor (box.x+(box.w-getTextWidth(hint))/2, hint_y);
        tft.print (hint);

        // time of maximum, bottom-left, DE local time
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);
        snprintf (str, sizeof(str), "%02d:%02dz", hour(eclipse_cir.t_max), minute(eclipse_cir.t_max));
        tft.setCursor (box.x+1, box.y+box.h-10);
        tft.print (str);

        // obscuration %, bottom-right
        snprintf (str, sizeof(str), "%.0f%%", eclipse_cir.obscuration*100);
        tft.setCursor (box.x+box.w-getTextWidth(str)-1, box.y+box.h-10);
        tft.print (str);

        // a fresh pane redraw always implicitly invalidates any popup that had been up
        // (its captured backing store no longer matches what's now on screen)
        popup_up = false;
}

/* dismiss the detail popup, if up: restore exactly what was underneath it, or fall back
 * to a full pane redraw if we don't have a valid backing store for some reason.
 */
static void dismissEclipsePopup (void)
{
        if (!popup_up)
            return;

        if (popup_has_bs)
            tft.setBackingStore (popup_bs, popup_px, popup_py, popup_pw, popup_ph);
        else
            updateEclipsePane (popup_pane_box);

        tft.drawPR();
        popup_up = false;
}

/* called once per loop() iteration -- cheap no-op unless the popup is actually up and its
 * time has expired, so it's safe to call unconditionally alongside the app's other
 * always-on per-iteration housekeeping.
 */
void checkEclipsePopupTimeout (void)
{
        if (popup_up && millis() >= popup_expire_ms)
            dismissEclipsePopup ();
}

/* check for touch at s, assumed to be within box.
 * shows a temporary, non-blocking detail popup: drawn here and returned from immediately,
 * so the rest of the app keeps running while it's up. a second tap anywhere dismisses it
 * right away; otherwise checkEclipsePopupTimeout() (called every loop() iteration) clears
 * it automatically after ECLIPSE_POPUP_MS.
 */
bool checkEclipseTouch (const SCoord &s, const SBox &box)
{
        // not ours if in title area -- let the caller open the pane-selection menu there,
        // same convention moonpane.cpp uses
        if (s.y < box.y + PANETITLE_H)
            return (false);

        // if the popup is already showing, this tap just dismisses it early
        if (popup_up) {
            dismissEclipsePopup ();
            return (true);
        }

        if (!eclipse_cir_ok)
            return (true);

        int detz = getTZ (de_tz);
        time_t c1 = eclipse_cir.t_c1 ? eclipse_cir.t_c1 + detz : 0;
        time_t c4 = eclipse_cir.t_c4 ? eclipse_cir.t_c4 + detz : 0;
        time_t tmax = eclipse_cir.t_max + detz;

        // short lines, one fact each, so nothing has to be crammed onto one row --
        // keeps every line comfortably inside a standard 160px-wide pane
        char lines[5][40];
        int nlines = 0;
        if (c1)
            snprintf (lines[nlines++], sizeof(lines[0]), "Start  %02d:%02d", hour(c1), minute(c1));
        snprintf (lines[nlines++], sizeof(lines[0]), "Max    %02d:%02d", hour(tmax), minute(tmax));
        if (c4)
            snprintf (lines[nlines++], sizeof(lines[0]), "End    %02d:%02d", hour(c4), minute(c4));
        snprintf (lines[nlines++], sizeof(lines[0]), "Magnitude    %.2f", eclipse_cir.magnitude);
        snprintf (lines[nlines++], sizeof(lines[0]), "Obscuration   %.0f%%", eclipse_cir.obscuration*100);

        // size and clamp the popup to fit entirely within this pane's own box so it never
        // bleeds into a neighboring pane
        const int16_t line_h = 13;
        uint16_t pw = box.w - 10;
        uint16_t ph = nlines*line_h + 8;
        uint16_t px = box.x + 5;
        uint16_t py = box.y + PANETITLE_H + 4;
        if (py + ph > box.y + box.h)
            ph = box.y + box.h - py;               // clamp, just in case

        // capture what's underneath so it can be restored exactly, then draw the popup
        popup_has_bs = tft.getBackingStore (popup_bs, px, py, pw, ph);
        popup_px = px; popup_py = py; popup_pw = pw; popup_ph = ph;
        popup_pane_box = box;

        tft.fillRect (px, py, pw, ph, RA8875_BLACK);
        tft.drawRect (px, py, pw, ph, RA8875_WHITE);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);
        for (int i = 0; i < nlines; i++) {
            tft.setCursor (px+4, py+4+i*line_h);
            tft.print (lines[i]);
        }
        tft.drawPR();

        popup_up = true;
        popup_expire_ms = millis() + ECLIPSE_POPUP_MS;

        return (true);
}
