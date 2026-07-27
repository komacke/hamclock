/* draw most plotting areas.
 */

#include "HamClock.h"

#define BORDER_COLOR    GRAY
#define GRID_COLOR      RGB565(55,55,55)
#define TICKLEN         2                       // length of plot tickmarks, pixels
#define TGAP            10                      // top gap for title
#define BGAP            15                      // bottom gap for x labels
#define FONTW           6                       // fast font width
#define FONTH           8                       // fast font height


/* plot the given data within the given box.
 * if y_min == y_max: auto scale min and max from data
 * if y_min < y_max:  force min to y_min and max to y_max
 * if y_min > y_max:  force min to y_min but auto scale max from data
 * return whether had anything to plot.
 * N.B. if both [xy]labels are NULL, use same limits as previous call as an "overlay"
 * N.B. special y axis labeling hack when ylabel contains the string "Ray"
 * N.B. special plot format hack when ylabel contains "Kp"
 */
bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float y_min, float y_max, float label_value)
{
    char buf[32];
    snprintf (buf, sizeof(buf), "%.*f", fabsf(label_value) >= 100 ? 0 : 1, label_value);
    return (plotXYstr (box, x, y, nxy, xlabel, ylabel, color, y_min, y_max, buf));
}

/* same as plotXY but label is a string
 */
bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float y_min, float y_max, char *label_str)
{
    // no labels implies overlay previous plot
    bool overlay = xlabel == NULL && ylabel == NULL;

    // check for special kp plot. N.B. ylabel is NULL if this is an overlay plot
    bool kp_plot = ylabel && strstr (ylabel, "Kp") != NULL;

    // check for xray plot
    bool xray_plot = ylabel && strstr (ylabel, "Ray") != NULL;

    // persistent scale info in case of subsequent overlay
    #define MAXTICKS     10
    static float xticks[MAXTICKS+2], yticks[MAXTICKS+2];
    static uint8_t nxt, nyt;
    static float minx, maxx;
    static float miny, maxy;
    static float dx, dy;
    static uint16_t LGAP;

    char buf[32];

    // set initial font and color
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // report if no data
    if (nxy < 1 || !x || !y) {
        plotMessage (box, color, "No data");
        return (false);
    }

    // find new limits unless this is an overlay
    if (!overlay) {

        // find data extrema
        minx = x[0]; maxx = x[0];
        miny = y[0]; maxy = y[0];
        for (int i = 1; i < nxy; i++) {
            if (x[i] > maxx) maxx = x[i];
            if (x[i] < minx) minx = x[i];
            if (y[i] > maxy) maxy = y[i];
            if (y[i] < miny) miny = y[i];
        }
        minx = floor(minx);
        maxx = ceil(maxx);
        if (maxx < minx + 1)
            maxx = minx + 1;

        if (y_min < y_max) {
            // force miny and maxy to the given y range
            miny = y_min;
            maxy = y_max;
        } else {
            if (y_min == y_max) {
                // auto scale both miny and maxy
                miny = floor(miny);
            } else {
                // force miny, still autoscale maxy
                miny = y_min;
            }
            // autoscale maxy
            maxy = ceil(maxy);
            if (maxy < miny + 1)
                maxy = miny + 1;
        }

        // find tickmarks
        nxt = tickmarks (minx, maxx, MAXTICKS, xticks);
        nyt = tickmarks (miny, maxy, MAXTICKS, yticks);

        // find minimal LGAP that accommodates widest y label
        LGAP = 0;
        for (int i = 0; i < nyt; i++) {
            snprintf (buf, sizeof(buf), "%.0f", yticks[i]);   // N.B. use same format as label 
            uint16_t g = getTextWidth(buf) + TICKLEN + 5;
            if (g > LGAP)
                LGAP = g;
        }

        // handy extrema
        minx = xticks[0];
        maxx = xticks[nxt-1];
        miny = yticks[0];
        maxy = yticks[nyt-1];
        dx = maxx-minx;
        dy = maxy-miny;

        // erase -- don't use prepPlotBox because we prefer no border on these plots
        fillSBox (box, RA8875_BLACK);

        // y labels and tickmarks just to the left of the plot
        if (xray_plot) {
            // mark exponents and customary X ray levels, extend levels across in darker color
            uint16_t tx = box.x+2*FONTW+TICKLEN+5;
            uint16_t step_h = (box.h-BGAP-TGAP)/nyt;
            for (int i = 0; i < nyt; i++) {
                uint16_t ty = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (yticks[i]-miny)/dy) + 0.5F);
                tft.drawLine (tx-TICKLEN, ty, tx, ty, color);           // main tick
                tft.drawLine (tx, ty, box.x+box.w-1, ty, GRID_COLOR);   // level line across
                tft.setCursor (tx-FONTW-1, ty-step_h+(step_h-FONTH)/2-1);
                switch ((int)yticks[i]) {
                case -9: tft.setCursor (tx-TICKLEN-2*FONTW-1, ty-FONTH/2); tft.print(-9); break;
                case -8: tft.print ('A'); break;
                case -7: tft.print ('B'); break;
                case -6: tft.print ('C'); break;
                case -5: tft.print ('M'); break;
                case -4: tft.print ('X'); break;
                case -2: tft.setCursor (tx-TICKLEN-2*FONTW-1, ty-FONTH/2); tft.print(-2); break;
                }
            }
        } else {
            uint16_t tx = box.x+LGAP-TICKLEN;
            bool prev_tick = false;
            for (int i = 0; i < nyt; i++) {
                uint16_t ty = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (yticks[i]-miny)/dy) + 0.5F);
                tft.drawLine (tx, ty, tx+TICKLEN, ty, color);
                tft.drawLine (box.x+LGAP, ty, box.x+box.w-1, ty, GRID_COLOR);
                // label first, last or whole number change but never two adjacent or just before last
                if (i == 0 || i == nyt-1 || (!prev_tick && (int)yticks[i-1] != (int)yticks[i] && i != nyt-2)){
                    snprintf (buf, sizeof(buf), "%.0f", yticks[i]);
                    tft.setCursor (tx - getTextWidth(buf) - 1, ty - FONTH/2 + 1);
                    tft.print (buf);
                    prev_tick = true;
                } else
                    prev_tick = false;
            }
        }

        // y label is title over plot, beware spilling off right edge
        uint16_t tl = getTextWidth(ylabel);
        uint16_t ttx = box.x + LGAP + (box.w-LGAP-tl)/2;
        if (ttx + tl >= box.x + box.w)
            ttx = box.x + box.w - tl;
        tft.setCursor (ttx, box.y+(TGAP-FONTH)/2+1);
        tft.print (ylabel);

        // x labels and tickmarks just below plot
        uint16_t txty = box.y+box.h-FONTH-2;
        tft.setCursor (box.x+LGAP, txty);
        tft.print (minx,0);
        snprintf (buf, sizeof(buf), "%c%d", maxx > 0 ? '+' : ' ', (int)maxx);
        tft.setCursor (box.x+box.w-getTextWidth(buf)-1, txty);
        tft.print (buf);
        for (int i = 0; i < nxt; i++) {
            uint16_t tx = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(xticks[i]-minx)/dx + 0.5F);
            tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
            tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+TGAP, GRID_COLOR);
        }

        // always label 0 if within larger range
        if (minx < 0 && maxx > 0) {
            uint16_t zx = (uint16_t)(box.x+LGAP + (box.w-LGAP)*(0-minx)/dx + 0.5F);
            tft.setCursor (zx-FONTW/2, txty);
            tft.print (0);
        }

        // x label is centered about the plot across the bottom
        tft.setCursor (box.x + LGAP + (box.w-LGAP-getTextWidth (xlabel))/2, box.y+box.h-FONTH-2);
        tft.print (xlabel);

    }

    // finally the data
    uint16_t prev_px = 0, prev_py = 0;
    bool prev_lacuna = false;           // avoid adjacent lacunas, eg, env plots
    const float lacuna_dx = 5*dx/nxy;   // define as a gap at least 5x average spacing
    for (int i = 0; i < nxy; i++) {
        // Serial.printf ("kp %2d: %g %g\n", i, x[i], y[i]);
        if (kp_plot) {
            // plot Kp values vertical bars colored depending on strength
            uint16_t h = y[i]*(box.h-BGAP-TGAP)/maxy;
            if (h > 0) {
                uint16_t w = (box.w-LGAP-2)/nxy;
                uint16_t px = (uint16_t)(box.x+LGAP+1 + (box.w-LGAP-2-w)*(x[i]-minx)/dx);
                uint16_t py = (uint16_t)(box.y + TGAP + 1 + (box.h-BGAP-TGAP)*(1 - (y[i]-miny)/dy));
                uint16_t co = y[i] < 4.5 ? RGB565(0x91,0xd0,0x51) : 
                              y[i] < 5.5 ? RGB565(0xf6,0xeb,0x16) :
                              y[i] < 6.5 ? RGB565(0xfe,0xc8,0x04) :
                              y[i] < 7.5 ? RGB565(0xff,0x96,0x02) :
                              y[i] < 8.5 ? RGB565(0xff,0x00,0x00) :
                                           RGB565(0xc7,0x01,0x00);
                tft.fillRect (px, py, w, h, co);
            }
        } else {
            // other plots are connect-the-dots but watch for lacuna
            uint16_t px = (uint16_t)(box.x+LGAP+1 + (box.w-LGAP-3)*(x[i]-minx)/dx);   // stay inside border
            uint16_t py = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (y[i]-miny)/dy));
            if (nxy == 1) {
                tft.drawLine (box.x+LGAP, py, box.x+box.w-1, py, color);   // one value clear across
            } else if (i > 0) {
                bool is_lacuna = (x[i]-x[i-1]) > lacuna_dx;
                if ((prev_px != px || prev_py != py) && (!is_lacuna || !prev_lacuna))
                    tft.drawLine (prev_px, prev_py, px, py, color);        // avoid bug with 0-length lines
                prev_lacuna = is_lacuna;
            }
            prev_px = px;
            prev_py = py;
        }
    }

    // draw plot border
    tft.drawRect (box.x+LGAP, box.y+TGAP, box.w-LGAP, box.h-BGAP-TGAP+1, BORDER_COLOR);

    // overlay large center value on top in gray
    if (label_str) {
        tft.setTextColor(BRGRAY);
        selectFontStyle (BOLD_FONT, LARGE_FONT);
        uint16_t lw, lh;
        getTextBounds (label_str, &lw, &lh);
        uint16_t text_x = box.x+LGAP+(box.w-LGAP-lw)/2;
        uint16_t text_y = box.y+TGAP+(box.h-TGAP-BGAP)/25+lh;
        tft.setCursor (text_x, text_y);
        tft.print (label_str);
    }

    // printFreeHeap ("plotXYstr");

    // ok
    return (true);
}

/* print weather info in the given box
 */
void plotWX (const SBox &box, uint16_t color, const WXInfo &wi)
{
    // prep
    prepPlotBox (box);

    const uint8_t attr_w = FONTW+1;     // allow for attribution down right side
    uint16_t dy = box.h/3;
    uint16_t ddy = box.h/5;
    float f;
    char buf[32];
    uint16_t w;

    // large temperature with degree symbol and units
    tft.setTextColor(color);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    f = showTempC() ? wi.temperature_c : CEN2FAH(wi.temperature_c);
    snprintf (buf, sizeof(buf), "%.0f %c", f, showTempC() ? 'C' : 'F');
    w = maxStringW (buf, box.w-attr_w);
    tft.setCursor (box.x+(box.w-attr_w-w)/2, box.y+dy);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf+strlen(buf)-2, &bw, &bh);
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (tft.getCursorX()-bw, tft.getCursorY()-2*bh/3);
    tft.print('o');
    dy += ddy;


    // humidity and pressure and optional pressure change symbol

    if (wi.pressure_chg >= -1 && wi.pressure_chg <= 1) {

        // include pressure change symbol

        #define PCHG_H      20          // change arrow height
        #define PCHG_SH     3           // steady arrow half-height
        #define PCHG_LW     10          // small font label width
        #define PCHG_W      5           // half-width of change arrow
        #define PCHG_LG     3           // gap either side of label

        // main info line

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        if (showATMhPa())
            snprintf (buf, sizeof(buf), "%.0f%% %.0f", wi.humidity_percent, wi.pressure_hPa);
        else
            snprintf (buf, sizeof(buf), "%.0f%% %.2f", wi.humidity_percent, HPA2INHG(wi.pressure_hPa));
        w = maxStringW (buf, box.w-attr_w-PCHG_W-PCHG_LW-2*PCHG_LG);
        tft.setCursor (box.x+(box.w-attr_w-w-PCHG_W-PCHG_LW-2*PCHG_LG)/2, box.y+dy);
        tft.print (buf);
        dy += ddy;

        // add units after value
        uint16_t pchg_x = tft.getCursorX() + PCHG_LG;
        uint16_t pchg_y = tft.getCursorY() - PCHG_H - 1;
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        if (showATMhPa()) {
            tft.setCursor (pchg_x, pchg_y); tft.print ("h");
            tft.setCursor (pchg_x, pchg_y+8); tft.print ("P");
            tft.setCursor (pchg_x, pchg_y+14); tft.print ("a");
        } else {
            #ifdef _INHG
                tft.setCursor (pchg_x, pchg_y-4); tft.print ("i");
                tft.setCursor (pchg_x, pchg_y+3); tft.print ("n");
                tft.setCursor (pchg_x, pchg_y+10); tft.print ("H");
                tft.setCursor (pchg_x, pchg_y+17); tft.print ("g");
            #else
                tft.setCursor (pchg_x, pchg_y+4); tft.print ("i");
                tft.setCursor (pchg_x, pchg_y+12); tft.print ("n");
            #endif
        }
        pchg_x += PCHG_LW + PCHG_LG;

        // add pressure arrow after units
        if (wi.pressure_chg > 0)
            tft.fillTriangle(pchg_x+PCHG_W, pchg_y, pchg_x, pchg_y+PCHG_H,
                             pchg_x+2*PCHG_W, pchg_y+PCHG_H, color);
        else if (wi.pressure_chg < 0)
            tft.fillTriangle(pchg_x, pchg_y, pchg_x+2*PCHG_W, pchg_y, pchg_x+PCHG_W, pchg_y+PCHG_H, color);
        else
            tft.fillTriangle(pchg_x, pchg_y+10-PCHG_SH, pchg_x+2*PCHG_W, pchg_y+10,
                             pchg_x, pchg_y+10+PCHG_SH, color);

    } else {

        // no pressure change symbol

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        if (showATMhPa())
            snprintf (buf, sizeof(buf), "%.0f%% %.0f hPa", wi.humidity_percent, wi.pressure_hPa);
        else
            snprintf (buf, sizeof(buf), "%.0f%% %.2f in", wi.humidity_percent,HPA2INHG(wi.pressure_hPa));
        w = maxStringW (buf, box.w-attr_w);
        tft.setCursor (box.x+(box.w-attr_w-w)/2, box.y+dy);
        tft.print (buf);
        dy += ddy;

    }

    // wind
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    f = (showDistKm() ? 3.6 : 2.237) * wi.wind_speed_mps; // kph or mph
    snprintf (buf, sizeof(buf), "%s @ %.0f %s", wi.wind_dir_name, f, showDistKm() ? "kph" : "mph");
    w = maxStringW (buf, box.w-attr_w);
    if (buf[strlen(buf)-1] != 'h') {
        // try shorter string in case of huge speed
        snprintf (buf, sizeof(buf),"%s @ %.0f%s", wi.wind_dir_name, f, showDistKm() ? "k/h" : "m/h");
        w = maxStringW (buf, box.w-attr_w);
    }
    tft.setCursor (box.x+(box.w-attr_w-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // nominal conditions
    strcpy (buf, wi.conditions);
    w = maxStringW (buf, box.w-attr_w);
    tft.setCursor (box.x+(box.w-attr_w-w)/2, box.y+dy);
    tft.print(buf);

    // attribution very small down the right side
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint8_t ylen = strlen(wi.attribution);
    uint16_t ly0 = box.y + (box.h - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
        tft.setCursor (box.x+box.w-attr_w, ly0+i*FONTH);
        tft.print (wi.attribution[i]);
    }

    // printFreeHeap ("plotWX");
}


/* this function draws the Band Conditions pane. It can be called in two quite different ways:
 *   1. when called by updateBandConditions(), we are given a table containing relative propagation values
 *      for each band and a summary line to be drawn across the bottom.
 *   2. we can also be called just to update annotation as indicated by bmp or cfg_str being NULL. In this
 *      case we only draw the band indicators in toa/rel_band according to busy with the others normal
 *      and we redraw the time line according to bc_utc_tl.
 * bmp is a matrix of 24 rows of UTC 0 .. 23, 8 columns of bands 80-40-30-20-17-15-12-10.
 * we draw the matrix rotated so rows go up from 80 and cols start on the left at the current DE hour.
 * busy means <0 err, 0 idle, >0 active.
 * N.B. coordinate the layout geometry with checkBCTouch()
 */
void plotBandConditions (const SBox &box, int busy, const BandCdtnMatrix *bmp, char *cfg_str)
{
    // layout
    #define PFONT_H 7                                   // plot labels font height
    #define PFONT_W 7                                   // plot labels font width
    #define PLOT_ROWS BMTRX_COLS                        // plot rows
    #define PLOT_COLS BMTRX_ROWS                        // plot columns
    #define TOP_B PANETITLE_H                           // top border
    #define PBOT_B 20                                   // plot bottom border -- room for config and time
    #define PLEFT_B 27                                  // left border -- room for band
    #define PRIGHT_B 14                                 // right border
    #define PTOP_Y (box.y + SUBTITLE_Y0)                // plot top y
    #define PBOT_Y (box.y+box.h-PBOT_B)                 // plot bottom y
    #define PLEFT_X (box.x + PLEFT_B)                   // plot left x
    #define PRIGHT_X (box.x+box.w-PRIGHT_B)             // plot right x
    #define PLOT_W (PRIGHT_X - PLEFT_X + 1)             // plot width
    #define PLOT_H (PBOT_Y - PTOP_Y)                    // plot height
    #define PCOL_W (PLOT_W/PLOT_COLS)                 // plot column width
    #define PROW_H (PLOT_H/PLOT_ROWS)                 // plot row height
    #define RELTOA_COLOR RGB565(0x58,0xa0,0xc8)         // TOA/REL marker color

    // to help organize the matrix rotation, p_ variables refer to plot indices, m_ to matrix indices

    // detect whether full or just updating labels
    bool draw_all = bmp != NULL && cfg_str != NULL;

    // prep box if all
    if (draw_all)
        prepPlotBox (box);

    // label band names and indicate current voacap band, if any
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(GRAY);
    for (int p_row = 0; p_row < PLOT_ROWS; p_row++) {

        // find row and desired bg color
        uint16_t y = PBOT_Y - PLOT_H*(p_row+1)/PLOT_ROWS;
        bool toa_active = IS_CMROT(CM_PMTOA) && p_row == cm_info[CM_PMTOA].band;
        bool rel_active = IS_CMROT(CM_PMREL) && p_row == cm_info[CM_PMREL].band;
        uint16_t band_bg = (toa_active || rel_active)
                                ? (busy > 0 ? RA8875_YELLOW : (busy < 0 ? RA8875_RED : RA8875_WHITE))
                                : RA8875_BLACK;

        // show band
        tft.fillRect (box.x+1, y+1, 2*PFONT_W-1, PFONT_H+3, band_bg);
        tft.setCursor (box.x+1, y + LISTING_OS);
        tft.print (propBand2Band((PropMapBand)p_row));

        // show whether REL
        tft.fillCircle (PLEFT_X - PRIGHT_B/2, y+PROW_H/2, 3, rel_active ? RELTOA_COLOR : RA8875_BLACK);

        // show whether TOA
        tft.fillCircle (box.x + box.w - PRIGHT_B/2, y+PROW_H/2, 3, toa_active ? RELTOA_COLOR : RA8875_BLACK);
    }

    // find utc and DE hour now. these will be the matrix row in plot column 0.
    int utc_hour_now = hour (nowWO());
    int de_hour_now = hour (nowWO() + getTZ (de_tz));
    int hr_now = bc_utc_tl ? utc_hour_now : de_hour_now;

    // erase timeline if not drawing all (because prepPlotBox() already erased everything if draw_all)
    uint16_t timeline_y = PBOT_Y+2;
    if (!draw_all)
        tft.fillRect (box.x + 1, timeline_y-1, box.w-2, PFONT_H+1, RA8875_BLACK);

    // label timeline local or utc wth local DE now always on left end
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (box.x+2, timeline_y);
    if (bc_utc_tl) {
        tft.setTextColor(GRAY);
        tft.print ("UTC");
    } else {
        tft.setTextColor(DE_COLOR);
        tft.print ("DE");
    }
    for (int p_col = 0; p_col < BMTRX_ROWS; p_col++) {
        int hr = (hr_now + p_col) % 24;
        if ((hr%4) != 0)
            continue;
        uint16_t x = PLEFT_X + PLOT_W*p_col/PLOT_COLS;
        if (hr >= 10) {
            // close packing centered
            tft.setCursor (x-2, timeline_y);
            tft.print (hr/10);
            tft.setCursor (x+2, timeline_y);
            tft.print (hr%10);
        } else {
            tft.setCursor (x, timeline_y);
            tft.print (hr);
        }
    }

    // when hr_now doesn't fall on a printed label, draw a small upward arrow
    // at column 0 so the user can see where "now" is
    if ((hr_now % 4) != 0) {
        uint16_t ax = PLEFT_X + PCOL_W / 2;
        uint16_t ay = timeline_y + PFONT_H / 2;
        tft.drawLine (ax,   ay-3, ax,   ay-3, RELTOA_COLOR);
        tft.drawLine (ax-1, ay-2, ax+1, ay-2, RELTOA_COLOR);
        tft.drawLine (ax-2, ay-1, ax+2, ay-1, RELTOA_COLOR);
        tft.drawLine (ax-3, ay,   ax+3, ay,   RELTOA_COLOR);
    }

    // that's it unless drawing all
    if (!draw_all)
        return;

    // center title across the top
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    const char *title = "VOACAP DE-DX";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-tw)/2, box.y + TOP_B);
    tft.print ((char*)title);

    // center the config across the bottom
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(BRGRAY);
    uint16_t cw = maxStringW (cfg_str, box.w);
    tft.setCursor (box.x+(box.w-cw)/2, box.y + box.h - 10);
    tft.print ((char*)cfg_str);

    // scan matrix by rows but plot as columns to affect rotation
    for (int m_row = 0; m_row < BMTRX_ROWS; m_row++) {
        int p_col = (m_row - utc_hour_now + 48) % 24;
        uint16_t p_x = PLEFT_X + PLOT_W*p_col/PLOT_COLS;
        for (int m_col = 0; m_col < BMTRX_COLS; m_col++) {
            // get reliability
            uint8_t rel = bmp->m[m_row][m_col];

            // choose color similar to fetchVOACAPArea.pl
            // rel:    0     10         33         66          100
            // color: black   |   red    |  yellow  |   green
            // hue:      x         0          43          85
            uint8_t h, s = 250, v;
            v = rel < 10 ? 0 : 250;
            h = rel < 33 ? 0 : (rel < 66 ? 43 : 85);
            uint16_t color = HSV_2_RGB565 (h, s, v);

            // draw color box
            int p_row = m_col;
            uint16_t p_y = PBOT_Y - PLOT_H*(p_row+1)/PLOT_ROWS;
            tft.fillRect (p_x, p_y, PCOL_W, PROW_H, color);
        }
    }

    // grid lines
    for (int p_col = 0; p_col <= PLOT_COLS; p_col++) {
        uint16_t x = PLEFT_X + PLOT_W*p_col/PLOT_COLS;
        tft.drawLine (x, PBOT_Y, x, PTOP_Y, GRID_COLOR);
    }
    for (int p_row = 0; p_row <= PLOT_ROWS; p_row++) {
        uint16_t y = PTOP_Y + PLOT_H*p_row/PLOT_ROWS;
        tft.drawLine (PLEFT_X, y, PRIGHT_X+1, y, GRID_COLOR);   // same y as box so extend to box edge
    }

}

/* print the NOAA RSG Space Weather Scales in the given box.
 * return whether at least the data transaction was valid.
 */
bool plotNOAASWx (const SBox &box)
{
    // prep
    prepPlotBox (box);

    // fetch
    NOAASpaceWxData noaa;
    if (!retrieveNOAASWx(noaa)) {
        plotMessage (box, RA8875_RED, "NOAA connection failed");
        return (false);
    } else if (!noaa.data_ok) {
        plotMessage (box, RA8875_RED, "NOAA data invalid");
        return (true);                                                  // transaction itself was ok
    }

    // title
    tft.setTextColor(NOAASPW_COLOR);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    const char title[] = "NOAA SpaceWx";
    uint16_t bw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-bw)/2, box.y+PANETITLE_H);
    tft.print (title);

    // scale
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    const char scale_msg[] = "Scale 0 - 5";
    uint16_t sw = getTextWidth (scale_msg);
    tft.setCursor (box.x+(box.w-sw)/2, box.y+SUBTITLE_Y0);
    tft.print (scale_msg);

    // print each line
    uint16_t table_y0 = box.y + PANETITLE_H + 20;
    uint16_t table_h = (box.y + box.h - 5) - table_y0;
    uint16_t label_x0 = box.x + 6;
    uint16_t value_x0 = label_x0 + 10 * FONTW;
    uint16_t value_w = (box.x + box.w - value_x0)/N_NOAASW_V;
    uint16_t footer_y = box.y + box.h - 12;

    for (int i = 0; i < N_NOAASW_C; i++) {

        // label
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        uint16_t label_y = table_y0 + i*table_h/N_NOAASW_C;             // font baseline is at top
        tft.setTextColor(NOAASPW_COLOR);
        tft.setCursor (label_x0, label_y);
        switch (noaa.cat[i]) {
        case 'R':
            tft.print ("Radio");
            tft.setCursor (label_x0, label_y += FONTH+2);
            tft.print ("blackout");
            break;
        case 'S':
            tft.print ("Solar");
            tft.setCursor (label_x0, label_y += FONTH+2);
            tft.print ("storm");
            break;
        case 'G':
            tft.print ("Magnetic");
            tft.setCursor (label_x0, label_y += FONTH+2);
            tft.print ("storm");
            break;
        default:
            fatalError ("bogus noaa sw category: %c %d", noaa.cat[i], noaa.cat[i]);
            break;                                                      // lint
        }

        // value and label below
        uint16_t value_y = table_y0 + i*table_h/N_NOAASW_C + 19;        // font baseline is at bottom
        for (int j = 0; j < N_NOAASW_V; j++) {
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            uint16_t value_x = value_x0 + j*value_w;
            int val = noaa.val[i][j];
            tft.setCursor (value_x, value_y);
            tft.setTextColor(val == 0 ? RA8875_GREEN : (val <= 3 ? RA8875_YELLOW : RA8875_RED));
            tft.print (val);
        }
    }

    // labels below
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(NOAASPW_COLOR);
    for (int i = 0; i < N_NOAASW_V; i++) {
        uint16_t value_x = value_x0 + i*value_w - 3;
        tft.setCursor (value_x, footer_y);
        if (i == 0)
            tft.print ("Now");
        else
            tft.printf ("D+%d", i);
    }

    // ok
    return (true);
}


/* print a message in a (plot?) box, take care not to go outside
 */
void plotMessage (const SBox &box, uint16_t color, const char *message)
{
    // log
    Serial.printf ("PlotMsg: %s\n", message);

    // prep font
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // prep box
    prepPlotBox (box);

    // make a copy so we can use make destructive line breaks
    char *msg_cpy = strdup (message);
    size_t msg_len = strlen (message);
    uint16_t msg_printed = 0;
    uint16_t y = box.y + box.h/4;

    // show up to at least a few lines
    for (int n_lines = 0; n_lines < 5 && msg_printed < msg_len; n_lines++) {

        // chop at max width -- maxStringW overwrites all beyond with 0's
        size_t l_before = strlen(msg_cpy);
        (void) maxStringW (msg_cpy, box.w-2);
        size_t l_after = strlen(msg_cpy);

        // unless finished, look for a closer blank but still print it so it's counted in msg_printed
        if (l_after < l_before) {
            char *blank = strrchr (msg_cpy, ' ');
            if (blank)
                blank[1] = '\0';
        }

        // draw what remains
        uint16_t msgw = getTextWidth (msg_cpy);
        tft.setCursor (box.x+(box.w-msgw)/2, y);
        tft.print(msg_cpy);

        // advance
        msg_printed += strlen (msg_cpy);
        strcpy (msg_cpy, message+msg_printed+(message[msg_printed] == ' ' ? 1 : 0));
        y += 2*FONTH;
    }

    // done
    free (msg_cpy);
}

/* prep a box for plotting
 */
void prepPlotBox (const SBox &box)
{
    // erase all
    fillSBox (box, RA8875_BLACK);

    // not bottom so it appears to connect with map top...
    uint16_t rx = box.x+box.w-1;
    uint16_t by = box.y+box.h-1;
    tft.drawLine (box.x, box.y, box.x, by, BORDER_COLOR);               // left
    tft.drawLine (box.x, box.y, rx, box.y, BORDER_COLOR);               // top
    tft.drawLine (rx, box.y, rx, by, BORDER_COLOR);                     // right

    // ... unless showing de/dx pane
    if (memcmp (&box, &plot_b[PANE_0], sizeof(SBox)) == 0)
        tft.drawLine (box.x, by, rx, by, BORDER_COLOR);                 // bottom
}

/* install the given BMP image into the given box, resizing and padding with black as necessary.
 * return whether all ok else false with brief reason in ynot
 */
bool installBMPBox (GenReader &gr, const SBox &box, ImageRefit fit, Message &ynot)
{
    // prep box with black
    fillSBox (box, RA8875_BLACK);

    // draw to full raw resolution
    SBox raw_b;
    raw_b.x = box.x * tft.SCALESZ;
    raw_b.y = box.y * tft.SCALESZ;
    raw_b.w = box.w * tft.SCALESZ;
    raw_b.h = box.h * tft.SCALESZ;

    // read image fit to raw box dimensions
    uint16_t *pix_565;
    if (!readBMPImage (gr, raw_b, pix_565, fit, ynot))
        return (false);

    // fill raw_b with image
    uint16_t *pix = pix_565;
    for (int dy = 0; dy < raw_b.h; dy++)
        for (int dx = 0; dx < raw_b.w; dx++)
            tft.drawPixelRaw (raw_b.x+dx, raw_b.y+dy, *pix++);

    // ok
    free (pix_565);
    return (true);
}
