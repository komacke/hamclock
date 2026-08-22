/* manage the PLOT_CH_ECLIPSE option -- next solar eclipse local circumstances for DE.
 */

#include "HamClock.h"


// cached result so touch handling doesn't have to re-run the search
static EclipseCir eclipse_cir;
static bool eclipse_cir_ok;

/* draw the moon disk using the real baked-in lunar texture (moon_image[], the same bitmap
 * the Moon pane itself uses) downsampled into a small circle of radius r_px, rather than a
 * flat fill. eclipses only happen at new moon, when the hemisphere facing Earth is the
 * unlit one, so the texture is drawn uniformly darkened -- which conveniently also matches
 * how an eclipsed moon actually looks (dim, but with real texture, not pure flat black).
 */
static void drawMoonTexture (int16_t cx, int16_t cy, int16_t r_px)
{
        // drawPixelRaw writes directly to raw framebuffer coordinates and does NOT
        // auto-apply tft.SCALESZ the way drawCircle/drawLine/etc do, so both the center
        // and the radius have to be converted to raw pixels ourselves here (same
        // convention moonpane.cpp uses for its own drawPixelRaw calls) or this ends up
        // drawing a tiny, mis-positioned patch on any build where SCALESZ > 1.
        const int16_t src_r = HC_MOON_W/2;                  // moon_image is HC_MOON_W square
        const int16_t raw_r = r_px * tft.SCALESZ;            // radius in raw framebuffer pixels
        const int16_t raw_cx = cx * tft.SCALESZ;
        const int16_t raw_cy = cy * tft.SCALESZ;
        const float src_per_dst = (float)HC_MOON_W / (2*raw_r);

        for (int16_t dy = -raw_r; dy <= raw_r; dy++) {
            for (int16_t dx = -raw_r; dx <= raw_r; dx++) {
                if (dx*dx + dy*dy > raw_r*raw_r)
                    continue;                                // outside our small disk

                int sx = src_r + (int)(dx*src_per_dst);
                int sy = src_r + (int)(dy*src_per_dst);
                if (sx < 0) sx = 0; else if (sx >= HC_MOON_W) sx = HC_MOON_W-1;
                if (sy < 0) sy = 0; else if (sy >= HC_MOON_W) sy = HC_MOON_W-1;

                uint16_t pix = moon_image[sy*HC_MOON_W + sx];
                pix = RGB565 (RGB565_R(pix)/3, RGB565_G(pix)/3, RGB565_B(pix)/3);   // dim, unlit side
                tft.drawPixelRaw (raw_cx+dx, raw_cy+dy, pix);
            }
        }
}

/* draw a two-disk glyph representing the eclipse, to exact relative scale, styled by type
 * and magnitude:
 *   - partial: sun saturation/brightness scales with magnitude -- a marginal partial looks
 *     dim and unremarkable, a deep one looks bold
 *   - annular: the visible ring is emphasized with a bright gold glow
 *   - total: sun is replaced with a soft white corona + a few rays, no orange disk at all
 * the moon disk always uses the real lunar texture (see drawMoonTexture), darkened.
 */
static void drawEclipseGlyph (const SBox &b, const EclipseCir &ec, int16_t cy)
{
        const int16_t cx = b.x + b.w/2;

        const int16_t sun_r_px = 26;
        const float px_per_rad = sun_r_px / ec.sun_r;
        int16_t moon_r_px = (int16_t)(ec.moon_r * px_per_rad + 0.5F);
        if (moon_r_px < 1) moon_r_px = 1;
        int16_t sep_px = (int16_t)(ec.sep * px_per_rad + 0.5F);

        // offset along a fixed diagonal direction purely for visual clarity
        int16_t dx = (int16_t)(sep_px * 0.7F);
        int16_t dy = (int16_t)(sep_px * 0.3F);
        int16_t sun_cx = cx - dx/2, sun_cy = cy - dy/2;
        int16_t moon_cx = cx + dx/2, moon_cy = cy + dy/2;

        if (ec.type == ECL_TOTAL) {

            // soft corona: a few concentric rings fading outward, then short rays
            tft.fillCircle (sun_cx, sun_cy, sun_r_px+6, RGB565(10,12,25));       // faint halo backdrop
            tft.drawCircle (sun_cx, sun_cy, sun_r_px+4, RGB565(120,140,200));
            tft.drawCircle (sun_cx, sun_cy, sun_r_px+2, RGB565(200,215,255));
            tft.drawCircle (sun_cx, sun_cy, sun_r_px,   RGB565(255,255,255));
            static const float ray_a[] = { 0, 45, 90, 135, 180, 225, 270, 315 };
            for (unsigned i = 0; i < sizeof(ray_a)/sizeof(ray_a[0]); i++) {
                float a = deg2rad(ray_a[i]);
                int16_t x0 = sun_cx + (int16_t)((sun_r_px+3)*cosf(a));
                int16_t y0 = sun_cy + (int16_t)((sun_r_px+3)*sinf(a));
                int16_t x1 = sun_cx + (int16_t)((sun_r_px+9)*cosf(a));
                int16_t y1 = sun_cy + (int16_t)((sun_r_px+9)*sinf(a));
                tft.drawLine (x0, y0, x1, y1, RGB565(210,225,255));
            }
            tft.fillCircle (sun_cx, sun_cy, sun_r_px-1, RGB565(5,5,10));         // dark disk itself

        } else if (ec.type == ECL_ANNULAR) {

            // bright gold glow ring, then the normal disk underneath
            tft.drawCircle (sun_cx, sun_cy, sun_r_px+2, RGB565(255,210,120));
            tft.drawCircle (sun_cx, sun_cy, sun_r_px+1, RGB565(255,190,60));
            tft.drawCircle (sun_cx, sun_cy, sun_r_px,   RGB565(255,160,20));
            tft.fillCircle (sun_cx, sun_cy, sun_r_px-1, RGB565(60,35,0));

        } else {

            // partial: saturation/brightness scales with magnitude, dim and muted when
            // barely partial, bold and warm when deep
            float m = ec.magnitude;
            if (m < 0) m = 0; else if (m > 1) m = 1;
            uint8_t rim_r = (uint8_t)(140 + 115*m), rim_g = (uint8_t)(90 + 60*m);
            uint8_t fill_r = (uint8_t)(25 + 45*m),  fill_g = (uint8_t)(15 + 20*m);
            tft.drawCircle (sun_cx, sun_cy, sun_r_px, RGB565(rim_r, rim_g, 10));
            tft.fillCircle (sun_cx, sun_cy, sun_r_px-1, RGB565(fill_r, fill_g, 0));

        }

        drawMoonTexture (moon_cx, moon_cy, moon_r_px);
        tft.drawCircle (moon_cx, moon_cy, moon_r_px, RGB565(90,90,90));
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

        // total eclipses darken the sky -- tint the pane background to suggest that,
        // drawn now so all the text/glyph below lands on top of it
        if (eclipse_cir.type == ECL_TOTAL)
            tft.fillRect (box.x+1, box.y+1, box.w-2, box.h-2, RGB565(8,9,20));

        // title: bigger, same font/placement convention as the On The Air pane's title
        // (SMALL_FONT, baseline sitting right at PANETITLE_H). Says "Solar Eclipse" and
        // not just "Eclipse" since this pane only ever covers solar events, never lunar.
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RGB565(170,170,170));
        const char *title = "Solar Eclipse";
        tft.setCursor (box.x+(box.w-getTextWidth(title))/2, box.y+PANETITLE_H);
        tft.print (title);

        // date/type row, pushed down below the now-taller title (SUBTITLE_Y0 is the same
        // convention On The Air uses for its own row right under its title). date respects
        // the user's DE/DX date format setting from Setup, same as the rest of HamClock --
        // and always with a 2-digit year, so there's never any "which year?" ambiguity.
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);
        int yy = year(t_max_de)%100, mo = month(t_max_de), dy = day(t_max_de);
        switch (getDateFormat()) {
        case DF_DMY:
            snprintf (str, sizeof(str), "%d/%d/%02d", dy, mo, yy);
            break;
        case DF_YMD:
            snprintf (str, sizeof(str), "%02d/%d/%d", yy, mo, dy);
            break;
        case DF_MDY:
        default:
            snprintf (str, sizeof(str), "%d/%d/%02d", mo, dy, yy);
            break;
        }
        tft.setCursor (box.x+1, box.y+SUBTITLE_Y0);
        tft.print (str);

        static const char *tnames[] = {"None", "Partial", "Annular", "Total"};
        strcpy (str, tnames[eclipse_cir.type]);
        uint16_t type_color;
        switch (eclipse_cir.type) {
        case ECL_TOTAL:   type_color = RGB565(230,235,255); break;   // pale corona white
        case ECL_ANNULAR: type_color = RGB565(255,180,40);  break;   // gold
        default:          type_color = DE_COLOR;             break;   // partial: normal
        }
        tft.setTextColor (type_color);
        tft.setCursor (box.x+box.w-getTextWidth(str)-1, box.y+SUBTITLE_Y0);
        tft.print (str);

        // glyph, centered in the remaining space between the date/type row and the
        // hint/bottom rows
        int16_t glyph_cy = box.y + SUBTITLE_Y0 + (box.h - SUBTITLE_Y0 - 24)/2;
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
}

/* called once per loop() iteration -- cheap no-op most of the time, so it's safe to call
 * unconditionally alongside the app's other always-on per-iteration housekeeping. notices
 * the displayed eclipse has already fully happened and immediately searches for the next
 * one, rather than leaving a stale already-passed event on screen for up to an hour
 * waiting for the normal periodic refresh.
 */
void checkEclipsePopupTimeout (void)
{
        if (eclipse_cir_ok && nowWO() > eclipse_cir.t_max + 3600) {
            PlotPane pp = findPaneChoiceNow (PLOT_CH_ECLIPSE);
            if (pp != PANE_NONE)
                scheduleNewPlot (PLOT_CH_ECLIPSE);
        }
}

/* check for touch at s, assumed to be within box.
 * shows a standard HamClock menu with the full local circumstances plus an option to open
 * the NASA eclipse predictions site, same runMenu() mechanism every other pane (Moon,
 * Storm, SDO, ...) already uses for this -- so it gets proper tap-outside-to-cancel and
 * doesn't need any of our own popup/backing-store/timer bookkeeping.
 */
bool checkEclipseTouch (const SCoord &s, const SBox &box)
{
        // not ours if in title area -- let the caller open the pane-selection menu there,
        // same convention moonpane.cpp uses
        if (s.y < box.y + PANETITLE_H)
            return (false);

        if (!eclipse_cir_ok)
            return (true);

        int detz = getTZ (de_tz);
        time_t c1 = eclipse_cir.t_c1 ? eclipse_cir.t_c1 + detz : 0;
        time_t c4 = eclipse_cir.t_c4 ? eclipse_cir.t_c4 + detz : 0;
        time_t tmax = eclipse_cir.t_max + detz;

        // one MENU_LABEL row per fact, plus a MENU_TOGGLE to open the NASA page
        char l_start[30], l_max[30], l_end[30], l_mag[30], l_obs[30];
        MenuItem mitems[6];
        int n = 0;
        const int indent = 3;

        if (c1) {
            snprintf (l_start, sizeof(l_start), "Start  %02d:%02d", hour(c1), minute(c1));
            mitems[n++] = {MENU_LABEL, false, 0, indent, l_start, 0};
        }
        snprintf (l_max, sizeof(l_max), "Max    %02d:%02d", hour(tmax), minute(tmax));
        mitems[n++] = {MENU_LABEL, false, 0, indent, l_max, 0};
        if (c4) {
            snprintf (l_end, sizeof(l_end), "End    %02d:%02d", hour(c4), minute(c4));
            mitems[n++] = {MENU_LABEL, false, 0, indent, l_end, 0};
        }
        snprintf (l_mag, sizeof(l_mag), "Magnitude    %.2f", eclipse_cir.magnitude);
        mitems[n++] = {MENU_LABEL, false, 0, indent, l_mag, 0};
        snprintf (l_obs, sizeof(l_obs), "Obscuration  %.0f%%", eclipse_cir.obscuration*100);
        mitems[n++] = {MENU_LABEL, false, 0, indent, l_obs, 0};
        int nasa_i = n;
        mitems[n++] = {MENU_TOGGLE, false, 1, indent, "Open NASA eclipse page", 0};

        SBox menu_b = box;          // copy, not ref
        menu_b.w = 0;                // shrink to fit
        menu_b.x += 8;
        menu_b.y += PANETITLE_H + 4;
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n, mitems};
        if (runMenu (menu)) {
            if (mitems[nasa_i].set)
                openURL ("https://eclipse.gsfc.nasa.gov/eclipse.html");
        }

        return (true);
}
