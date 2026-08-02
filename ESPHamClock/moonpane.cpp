/* manage the PLOT_CH_MOON option
 */

#include "HamClock.h"



/* draw the moon image in the given box
 */
static void drawMoonImage (const SBox &b)
{
        // prep
        prepPlotBox (b);

        float phase = lunar_cir.phase;
        // Serial.printf ("Phase %g deg\n", rad2deg(phase));

        // s hemi flips both top/bot because upside-down and left/right because person rotates 180 to look N

        const int16_t mr = HC_MOON_W/2;                             // moon radius on output device
        const int16_t mcx = tft.SCALESZ*(b.x+b.w/2);                // moon center x "
        const int16_t mcy = tft.SCALESZ*(b.y+b.h/2);                // moon center y "
        const int16_t flip = de_ll.lat < 0 ? -1 : 1;                // flip in s hemi
        int pix_i = 0;                                              // moon_image index
        for (int16_t dy = -mr; dy < mr; dy++) {                     // scan top-to-bot, matching image
            float Ry = sqrtf(mr*mr-dy*dy);                          // moon circle half-width at y
            int16_t Ryi = floorf(Ry+0.5F);                          // " as int
            for (int16_t dx = -mr; dx < mr; dx++) {                 // scan left-to-right, matching image
                uint16_t pix = moon_image[pix_i++];                 // next pixel
                if (dx > -Ryi && dx < Ryi) {                        // if inside moon circle
                    float a = acosf((float)dx/Ryi);                 // looking down from NP CW from right limb
                    if (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                        pix = RGB565(RGB565_R(pix)/3, RGB565_G(pix)/3, RGB565_B(pix)/3); // unlit side
                    tft.drawPixelRaw (mcx+dx*flip, mcy+dy*flip, pix);
                }
            }
        }
}

/* update moon pane info and image.
 * image is in moon_image[HC_MOON_W*HC_MOON_H].
 */
void updateMoonPane (const SBox &box)
{
        char str[50];

        // fresh info at user's effective time
        time_t t0 = nowWO();
        getLunarCir (t0, de_ll, lunar_cir);

        // full image
        drawMoonImage(box);

        // info, layout similar to SDO

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (DE_COLOR);

        snprintf (str, sizeof(str), "Az:%.0f", rad2deg(lunar_cir.az));
        tft.setCursor (box.x+1, box.y+2);
        tft.print (str);

        snprintf (str, sizeof(str), "El:%.0f", rad2deg(lunar_cir.el));
        tft.setCursor (box.x+box.w-getTextWidth(str)-1, box.y+2);
        tft.print (str);

        // show which ever local rise or set event comes next
        time_t rise, set;
        int detz = getTZ (de_tz);
        getLunarRS (t0, de_ll, &rise, &set);
        if (rise > t0 && (set < t0 || rise - t0 < set - t0))
            snprintf (str, sizeof(str), "R@%02d:%02d", hour(rise+detz), minute (rise+detz));
        else if (set > t0 && (rise < t0 || set - t0 < rise - t0))
            snprintf (str, sizeof(str), "S@%02d:%02d", hour(set+detz), minute (set+detz));
        else 
            strcpy (str, "No R/S");
        tft.setCursor (box.x+1, box.y+box.h-10);
        tft.print (str);

        snprintf (str, sizeof(str), "%.0fm/s", lunar_cir.vel);;
        tft.setCursor (box.x+box.w-getTextWidth(str)-1, box.y+box.h-10);
        tft.print (str);
}

/* check for touch at s, assumed to be within b.
 * return whether ours
 */
bool checkMoonTouch (const SCoord &s, const SBox &box)
{
    // not ours if in title area
    if (s.y < box.y + PANETITLE_H)
        return (false);

    // show menu
    const int indent = 3;
    MenuItem mitems[2] = {
        {MENU_TOGGLE, false, 1, indent, "Show EME tool", 0},
        {MENU_TOGGLE, false, 2, indent, "Show movie", 0},
    };
    const int n_mitems = NARRAY(mitems);

    SBox menu_b = box;          // copy, not ref
    menu_b.w = 0;               // shrink to fit
    menu_b.x += 20;
    menu_b.y += 60;
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n_mitems, mitems};
    if (runMenu(menu)) {
        if (mitems[0].set) {
            // need immediate refresh because EME tool blocks
            updateMoonPane (box);
            drawEMETool();
        }
        if (mitems[1].set)
            openMovieURL ("/moon/movies/ap240602.html", "https://apod.nasa.gov/apod/ap240602.html");

        // refresh
        scheduleNewPlot (PLOT_CH_MOON);
    }


    return (true);
}
