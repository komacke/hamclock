/* a generic mechanism to plot a server file or x/y table on map_b
 */

#include "HamClock.h"

// layout
#define TOPB            46                      // top border
#define BOTB            40                      // bottom border
#define LEFTB           40                      // left border
#define RIGHTB          15                      // right border
#define TICKL           5                       // tick mark length
#define PLOTW           (map_b.w-LEFTB-RIGHTB)  // plot width
#define PLOTH           (map_b.h-TOPB-BOTB)     // plot height
#define AXISXL          (map_b.x + LEFTB)       // axis x left
#define AXISXR          (AXISXL + PLOTW)        // axis x right
#define AXISYT          (map_b.y + TOPB)        // axis y top
#define AXISYB          (AXISYT + PLOTH)        // axis y bottom
#define XLBLY           (AXISYB+20)             // x axis label y
#define TTLY            (map_b.y + 30)          // title y
#define ERR_DWELL       2000                    // time to leave up err msg, msec

// colors
#define AXISC           BRGRAY                  // axis color
#define GRIDC           GRAY                    // grid color
#define LABELC          RA8875_WHITE            // label color
#define TITLEC          RA8875_WHITE            // title color
#define DATAC           RA8875_GREEN            // data color

// plot decoration
#define NXTICKS         20                      // nominal number of x tickmarks
#define NYTICKS         10                      // nominal number of y tickmarks
#define DX2GX(x)  (AXISXL + (float)PLOTW*((x)-xticks[0])/(xticks[n_xticks-1]-xticks[0])) // data to graphics x
#define DY2GY(y)  (AXISYB - (float)PLOTH*((y)-yticks[0])/(yticks[n_yticks-1]-yticks[0])) // data to graphics y


/* plot the 2d data on the earth map.
 */
void plotMapData (const char title[], const char x_label[], float x_data[], float y_data[], int n_data)
{
    // skip if not at least 2 points
    if (n_data < 2) {
        Serial.printf ("PMAP: only %d data points, require 2\n", n_data);
        return;
    }

    // erase
    fillSBox (map_b, RA8875_BLACK);

    // find ranges
    float min_x = x_data[0], max_x = x_data[0];
    float min_y = y_data[0], max_y = y_data[0];
    for (int i = 1; i < n_data; i++) {
        if (x_data[i] < min_x)
            min_x = x_data[i];
        if (x_data[i] > max_x)
            max_x = x_data[i];
        if (y_data[i] < min_y)
            min_y = y_data[i];
        if (y_data[i] > max_y)
            max_y = y_data[i];
    }

    // find tickmarks
    float xticks[NXTICKS+2], yticks[NYTICKS+2];
    int n_xticks = tickmarks (min_x, max_x, NXTICKS, xticks);
    int n_yticks = tickmarks (min_y, max_y, NYTICKS, yticks);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TITLEC);
    uint16_t tw = getTextWidth(title);
    tft.setCursor (map_b.x + (map_b.w - tw)/2, TTLY);
    tft.print (title);

    // draw axes
    tft.drawLine (AXISXL, AXISYT, AXISXL, AXISYB, AXISC);
    tft.drawLine (AXISXL, AXISYB, AXISXR, AXISYB, AXISC);

    // draw x label
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t xw = getTextWidth(x_label);
    tft.setCursor (map_b.x + (map_b.w - xw)/2, XLBLY);
    tft.print (x_label);

    // draw grids
    tft.setTextColor (LABELC);
    for (int i = 0; i < n_xticks; i++) {
        char lbl[20];
        uint16_t x = DX2GX(xticks[i]);
        tft.drawLine (x, AXISYT, x, AXISYB+TICKL, GRIDC);
        snprintf (lbl, sizeof(lbl), "%.4g", xticks[i]);
        tft.setCursor (x-getTextWidth(lbl)/2, AXISYB+TICKL+4);  // center lbl
        tft.print (lbl);
    }
    for (int i = 0; i < n_yticks; i++) {
        uint16_t y = DY2GY(yticks[i]);
        tft.drawLine (AXISXL-TICKL, y, AXISXR, y, GRIDC);
        tft.setCursor (AXISXL-LEFTB+1, y-4);
        tft.printf("%g", yticks[i]);
    }

    // finally the data -- looks nicer at raw resolition
    uint16_t prev_x = 0, prev_y = 0;
    for (int i = 0; i < n_data; i++) {
        uint16_t x = DX2GX(x_data[i]) * tft.SCALESZ;
        uint16_t y = DY2GY(y_data[i]) * tft.SCALESZ;
        if (i > 0)
            tft.drawLineRaw (prev_x, prev_y, x, y, 2, DATAC);
        prev_x = x;
        prev_y = y;
    }

    // create resume button box
    SBox resume_b;
    resume_b.w = 100;
    resume_b.x = map_b.x + map_b.w - resume_b.w - LEFTB;
    resume_b.h = 40;
    resume_b.y = map_b.y + 4;
    const char button_name[] = "Resume";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (button_name, resume_b, false, RA8875_GREEN);

    // see it all now
    tft.drawPR();

    // report info for tap times until time out or do anything
    UserInput ui = {
        map_b,
        UI_UFuncNone,
        UF_UNUSED,
        30000,
        UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };
    (void) waitForUser(ui);

    // ack
    drawStringInBox (button_name, resume_b, true, RA8875_GREEN);
    tft.drawPR();

    // restore
    initEarthMap();
}

/* read and plot the given server file.
 */
void plotServerFile (const char *filename, const char *title, const char x_label[])
{
    // base of filename
    const char *file_slash = strrchr (filename, '/');
    const char *file_base = file_slash ? file_slash + 1 : filename;

    // read data
    float *x_data = NULL;
    float *y_data = NULL;
    int n_data = 0;
    WiFiClient map_client;
    bool ok = false;

    Serial.println (filename);
    if (map_client.connect (backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (map_client, backend_host, filename);

        // skip response header
        if (!httpSkipHeader (map_client)) {
            mapMsg (2000, "%s: Header is short", file_base);
            goto out;
        }

        // read lines, adding to x_data[] and y_data[]
        char line[100];
        while (getTCPLine (map_client, line, sizeof(line), NULL)) {

            // crack
            float x, y;
            if (sscanf (line, "%f %f", &x, &y) != 2) {
                Serial.printf ("PMAP: bad line: %s\n", line);
                mapMsg (2000, "%s: Data is corrupted", file_base);
                goto out;
            }

            // grow
            float *new_x = (float *) realloc (x_data, (n_data+1) * sizeof(float));
            float *new_y = (float *) realloc (y_data, (n_data+1) * sizeof(float));
            if (!new_x || !new_y) {
                mapMsg (2000, "%s: Insufficient memory", file_base);
                goto out;
            }

            // add
            x_data = new_x;
            y_data = new_y;
            x_data[n_data] = x;
            y_data[n_data] = y;
            n_data += 1;
        }

        Serial.printf ("PMAP: read %d points\n", n_data);

        // require at least a few points
        if (n_data < 10) {
            mapMsg (2000, "%s: File is short", file_base);
            goto out;
        }

        // ok!
        ok = true;
    }

out:

    if (ok)
        plotMapData (title, x_label, x_data, y_data, n_data);

    // clean up, any error is already reported
    free (x_data);
    free (y_data);
    map_client.stop();
}



/*******************************************************************************************
 *
 * NOAA-style "Recent R-S-G reports" stacked history graph, shown full-map when the
 * NOAA SpaceWx pane is tapped (see checkPlotTouch(), PLOT_CH_NOAASPW).
 *
 * Three stacked panels, top to bottom:
 *   - Solar X-ray flux (flare class A..X)           -> R (radio blackout) scale
 *   - Solar proton flux, log10(pfu)                 -> S (solar radiation storm) scale
 *   - Planetary Kp index                            -> G (geomagnetic storm) scale
 * Each panel has a color-coded severity legend along its right edge -- six solid blocks,
 * G0 (quiet, green) at the bottom up to G5 (extreme, dark red) at the top -- using the
 * same colors NOAA uses for all three R/S/G scales.
 *
 * Tapping anywhere inside a panel's plot area shows a small popup with the nearest data
 * point's value and its age (hours or days back); tapping again elsewhere, tapping the
 * Resume button, or letting it time out returns to the normal map.
 *
 *******************************************************************************************/

#define RSG_FONTW       6                       // FAST_FONT glyph width, pixels
#define RSG_FONTH       8                       // FAST_FONT glyph height, pixels
#define RSG_LEGW        30                      // legend strip width, pixels
#define RSG_LGAP        3                       // gap between plot and legend
#define RSG_LEFTM       26                      // left margin for y axis labels
#define RSG_BOTM        16                      // bottom margin for x axis labels
#define RSG_TOPM        12                      // top margin for panel title
#define RSG_PANEL_GAP   10                      // vertical gap between panels
#define RSG_TITLE_Y     40                      // title baseline, down from map_b.y
#define RSG_TOP_Y       58                      // first panel top, down from map_b.y

/* everything needed to hit-test a tap against one panel's plot area and look up the
 * nearest data point, filled in by rsgDrawPanel() and used afterward by plotRSGHistory().
 */
typedef struct {
    SBox area;                                  // plot area screen bounds (axes/frame, no legend)
    float *xd, *yd;                             // data arrays (not owned)
    int n;                                       // n points, 0 if no data
    float x_min, x_max, y_min, y_max;           // data range spanning area
    const char *x_unit;                          // eg "h" or "d", appended to age
    void (*fmtY)(float value, char *buf, int bufsz);  // format a y value for the popup
} RSGPanelInfo;

/* the six standard NOAA scale colors, level 0 (quiet) .. 5 (extreme)
 */
static uint16_t rsgLevelColor (int level)
{
    switch (level) {
    case 0:  return (RGB565(0x91,0xd0,0x51));           // green
    case 1:  return (RGB565(0xf6,0xeb,0x16));           // yellow
    case 2:  return (RGB565(0xfe,0xc8,0x04));           // gold
    case 3:  return (RGB565(0xff,0x96,0x02));           // orange
    case 4:  return (RGB565(0xff,0x00,0x00));           // red
    default: return (RGB565(0xc7,0x01,0x00));           // dark red
    }
}

/* R scale level from log10(W/m^2) GOES long-wavelength x-ray flux
 */
static int rsgRLevel (float log_flux)
{
    if (log_flux < -5.0F) return (0);                   // < M1
    if (log_flux < -4.301F) return (1);                 // M1 - M5
    if (log_flux < -4.0F) return (2);                   // M5 - X1
    if (log_flux < -3.0F) return (3);                   // X1 - X10
    if (log_flux < -2.699F) return (4);                 // X10 - X20
    return (5);                                          // >= X20
}

/* S scale level from log10(pfu) >= 10 MeV proton flux
 */
static int rsgSLevel (float log_pfu)
{
    if (log_pfu < 1.0F) return (0);
    if (log_pfu < 2.0F) return (1);
    if (log_pfu < 3.0F) return (2);
    if (log_pfu < 4.0F) return (3);
    if (log_pfu < 5.0F) return (4);
    return (5);
}

/* G scale level from Kp index, 0 - 9
 */
static int rsgGLevel (float kp)
{
    if (kp < 4.5F) return (0);
    if (kp < 5.5F) return (1);
    if (kp < 6.5F) return (2);
    if (kp < 7.5F) return (3);
    if (kp < 8.5F) return (4);
    return (5);
}

/* format an x-ray log10(W/m^2) value as a conventional flare class string, eg "M1.3"
 */
static void rsgFmtXray (float log_flux, char *buf, int bufsz)
{
    char cls;
    float base;
    if (log_flux < -7.0F)      { cls = 'A'; base = 1e-8F; }
    else if (log_flux < -6.0F) { cls = 'B'; base = 1e-7F; }
    else if (log_flux < -5.0F) { cls = 'C'; base = 1e-6F; }
    else if (log_flux < -4.0F) { cls = 'M'; base = 1e-5F; }
    else                       { cls = 'X'; base = 1e-4F; }
    snprintf (buf, bufsz, "%c%.1f", cls, powf(10,log_flux)/base);
}

/* format a proton log10(pfu) value, eg "S: 3.2 pfu"
 */
static void rsgFmtProton (float log_pfu, char *buf, int bufsz)
{
    snprintf (buf, bufsz, "%.3g pfu", powf(10,log_pfu));
}

/* format a Kp value, eg "Kp 4.3"
 */
static void rsgFmtKp (float kp, char *buf, int bufsz)
{
    snprintf (buf, bufsz, "Kp %.1f", kp);
}

/* draw the severity legend spanning the given box: six equal-height SOLID blocks,
 * level 0 (quiet) at the bottom to level 5 (extreme) at the top, each with a bold
 * white label -- eg "G0" .. "G5". Deliberately NOT proportional to the data's actual
 * value range so every level stays legible even though, physically, level 0 covers
 * by far the widest span of raw values.
 */
static void rsgDrawLegend (uint16_t lx, uint16_t ty, uint16_t lw, uint16_t lh, char scale_id)
{
    selectFontStyle (BOLD_FONT, FAST_FONT);

    const int N_LEVELS = 6;
    for (int i = 0; i < N_LEVELS; i++) {
        int level = N_LEVELS-1-i;                       // top row = 5 (extreme) .. bottom row = 0
        uint16_t by0 = ty + (uint16_t)((long)i*lh/N_LEVELS);
        uint16_t by1 = ty + (uint16_t)((long)(i+1)*lh/N_LEVELS);
        uint16_t bh = by1 - by0;

        tft.fillRect (lx, by0, lw, bh, rsgLevelColor(level));

        char buf[4];
        snprintf (buf, sizeof(buf), "%c%d", scale_id, level);
        uint16_t bw = getTextWidth (buf);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (lx + (lw-bw)/2, by0 + bh/2 - RSG_FONTH/2 + 1);
        tft.print (buf);
    }

    tft.drawRect (lx, ty, lw, lh, GRAY);
}

/* draw one connect-the-dots or bar-chart panel within box, with the RSG legend strip
 * along its right edge. If !data_ok just show "No data" and still draw the frame.
 * kp_bars: true to draw NOAA-Kp-style colored vertical bars instead of a connected line.
 * levelFromValue is used both for the connect-the-dots line coloring (so the line itself
 * turns yellow/orange/red as it crosses into those severity ranges) and the Kp bar coloring.
 * x_unit_label is a short word like "Hours" or "Days" centered under the x axis.
 * info, if non-NULL, is filled in with everything needed to hit-test a later tap against
 * this panel's plot area (see RSGPanelInfo).
 */
static void rsgDrawPanel (const SBox &box, const char *title,
                           float x[], float y[], int nxy, bool data_ok,
                           float y_min_in, float y_max_in, bool kp_bars,
                           char scale_id, int (*levelFromValue)(float), const char *x_unit_label,
                           const char *y_tick_labels[], int y_tick_values[], int n_y_ticklabels,
                           void (*fmtY)(float,char*,int), RSGPanelInfo *info)
{
    // frame
    fillSBox (box, RA8875_BLACK);

    // title, centered
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + 1);
    tft.print (title);

    // plot area, leaving room for legend strip at right
    uint16_t px = box.x + RSG_LEFTM;
    uint16_t py = box.y + RSG_TOPM;
    uint16_t pw = box.w - RSG_LEFTM - RSG_LGAP - RSG_LEGW - 2;
    uint16_t ph = box.h - RSG_TOPM - RSG_BOTM;
    if (info)
        info->n = 0;                                    // assume none until proven otherwise below
    if (pw < 10 || ph < 10)
        return;                                          // too small to bother

    // y range
    float y_min = y_min_in, y_max = y_max_in;
    if (data_ok && nxy > 0) {
        float dmin = y[0], dmax = y[0];
        for (int i = 1; i < nxy; i++) {
            if (y[i] < dmin) dmin = y[i];
            if (y[i] > dmax) dmax = y[i];
        }
        if (dmin < y_min) y_min = floorf(dmin);
        if (dmax > y_max) y_max = ceilf(dmax);
    }
    // clamp to a sane range around the nominal one -- protects against a single bogus or
    // missing-data sentinel value from the feed (eg an unpublished future Kp prediction)
    // blowing the axis out to an absurd magnitude
    {
        float nominal_span = y_max_in - y_min_in;
        float clamp_lo = y_min_in - 2*nominal_span;
        float clamp_hi = y_max_in + 2*nominal_span;
        if (y_min < clamp_lo) y_min = clamp_lo;
        if (y_max > clamp_hi) y_max = clamp_hi;
    }
    if (y_max <= y_min)
        y_max = y_min + 1;

    // y ticks / grid -- skip a label (but keep its gridline) if it would collide with
    // the previous one, so this degrades gracefully on any screen size
    tft.setTextColor (BRGRAY);
    int last_label_ty = -1000;
    if (n_y_ticklabels > 0) {
        // explicit labels (eg flare class letters) at fixed data values
        for (int i = 0; i < n_y_ticklabels; i++) {
            float v = y_tick_values[i];
            if (v < y_min || v > y_max)
                continue;
            int ty = py + (int)(ph * (1 - (v-y_min)/(y_max-y_min)));
            tft.drawLine (px, ty, px+pw-1, ty, GRIDC);
            if (abs(ty - last_label_ty) >= RSG_FONTH) {
                tft.setCursor (box.x + RSG_LEFTM - RSG_FONTW - 3, ty - RSG_FONTH/2);
                tft.print (y_tick_labels[i]);
                last_label_ty = ty;
            }
        }
    } else {
        int nyt = 5;
        for (int i = 0; i <= nyt; i++) {
            float v = y_min + i*(y_max-y_min)/nyt;
            int ty = py + (int)(ph * (1 - (float)i/nyt));
            tft.drawLine (px, ty, px+pw-1, ty, GRIDC);
            if (abs(ty - last_label_ty) >= RSG_FONTH) {
                char buf[16];
                snprintf (buf, sizeof(buf), "%.0f", v);
                uint16_t bw = getTextWidth (buf);
                // never let this land left of our own box, no matter how wide an
                // extreme/bogus value's text turns out to be
                int label_x = (int)px - (int)bw - 3;
                if (label_x < (int)box.x)
                    label_x = box.x;
                tft.setCursor (label_x, ty - RSG_FONTH/2);
                tft.print (buf);
                last_label_ty = ty;
            }
        }
    }

    // frame around plot area
    tft.drawRect (px, py, pw, ph, GRAY);

    if (!data_ok || nxy <= 0) {

        const char msg[] = "No data";
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        uint16_t mw = getTextWidth (msg);
        tft.setTextColor (RA8875_RED);
        tft.setCursor (px + (pw-mw)/2, py + ph/2 - RSG_FONTH/2);
        tft.print (msg);

    } else {

        // x range directly from data
        float x_min = x[0], x_max = x[0];
        for (int i = 1; i < nxy; i++) {
            if (x[i] < x_min) x_min = x[i];
            if (x[i] > x_max) x_max = x[i];
        }
        if (x_max <= x_min)
            x_max = x_min + 1;

        if (kp_bars) {
            // NOAA-style colored vertical bars, one per sample
            uint16_t bw = pw/nxy;
            if (bw < 1) bw = 1;
            for (int i = 0; i < nxy; i++) {
                if (y[i] <= y_min)
                    continue;
                uint16_t bx = px + (uint16_t)((pw-bw) * (x[i]-x_min)/(x_max-x_min));
                uint16_t bh = (uint16_t)(ph * (y[i]-y_min)/(y_max-y_min));
                uint16_t by = py + ph - bh;
                tft.fillRect (bx, by, bw, bh, rsgLevelColor((*levelFromValue)(y[i])));
            }
        } else {
            // connect-the-dots, colored by severity level of the trailing point of each segment
            uint16_t prev_px = 0, prev_py = 0;
            for (int i = 0; i < nxy; i++) {
                uint16_t dx = px + (uint16_t)((pw-1) * (x[i]-x_min)/(x_max-x_min));
                uint16_t dy = py + (uint16_t)(ph * (1 - (y[i]-y_min)/(y_max-y_min)));
                if (i > 0) {
                    uint16_t seg_color = rsgLevelColor ((*levelFromValue)(y[i]));
                    tft.drawLine (prev_px, prev_py, dx, dy, 2, seg_color);
                }
                prev_px = dx;
                prev_py = dy;
            }
        }

        // x axis end labels plus centered unit label, eg "Hours" or "Days"
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (BRGRAY);
        char lbuf[16], rbuf[16];
        snprintf (lbuf, sizeof(lbuf), "%+.0f", x_min);
        snprintf (rbuf, sizeof(rbuf), "%+.0f", x_max);
        tft.setCursor (px, py+ph+2);
        tft.print (lbuf);
        uint16_t rw = getTextWidth (rbuf);
        tft.setCursor (px+pw-rw, py+ph+2);
        tft.print (rbuf);
        if (x_unit_label) {
            uint16_t uw = getTextWidth (x_unit_label);
            tft.setCursor (px + (pw-uw)/2, py+ph+2);
            tft.print (x_unit_label);
        }

        // report hit-test info for the caller
        if (info) {
            info->xd = x;
            info->yd = y;
            info->n = nxy;
            info->x_min = x_min;
            info->x_max = x_max;
        }
    }

    if (info) {
        info->area = { px, py, pw, ph };
        info->y_min = y_min;
        info->y_max = y_max;
        info->fmtY = fmtY;
    }

    // legend strip -- fixed six-block scale, independent of this panel's actual data range
    rsgDrawLegend (px+pw+RSG_LGAP, py, RSG_LEGW, ph, scale_id);
}

/* show a small popup with the data point nearest the tapped x position within info's
 * plot area. Does nothing if info has no data.
 */
static void rsgShowPopup (const RSGPanelInfo &info, const SCoord &tap, const char *x_unit)
{
    if (info.n <= 0 || !info.fmtY)
        return;

    // map tapped pixel x back to a data x value, then find the nearest sample
    float frac = info.area.w > 1 ? (float)(tap.x - info.area.x)/(info.area.w-1) : 0;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float xval = info.x_min + frac*(info.x_max - info.x_min);
    int best = 0;
    float best_d = fabsf (info.xd[0] - xval);
    for (int i = 1; i < info.n; i++) {
        float d = fabsf (info.xd[i] - xval);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }

    char vbuf[24], xbuf[24];
    (*info.fmtY) (info.yd[best], vbuf, sizeof(vbuf));
    snprintf (xbuf, sizeof(xbuf), "%+.1f%s", info.xd[best], x_unit);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t w1 = getTextWidth (vbuf), w2 = getTextWidth (xbuf);
    uint16_t pw = (w1 > w2 ? w1 : w2) + 10;
    uint16_t ph = 2*RSG_FONTH + 10;

    // place near the tap but keep fully inside this panel's plot area
    int bx = tap.x + 8;
    int by = tap.y - ph - 4;
    if (bx + pw > info.area.x + info.area.w)
        bx = info.area.x + info.area.w - pw;
    if (bx < info.area.x)
        bx = info.area.x;
    if (by < info.area.y)
        by = tap.y + 8;
    if (by + ph > info.area.y + info.area.h)
        by = info.area.y + info.area.h - ph;

    tft.fillRect (bx, by, pw, ph, RGB565(25,25,25));
    tft.drawRect (bx, by, pw, ph, RA8875_WHITE);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (bx+5, by+4);
    tft.print (vbuf);
    tft.setCursor (bx+5, by+4+RSG_FONTH+2);
    tft.print (xbuf);

    tft.drawPR();
}

/* fetch and display the stacked "Recent R-S-G reports" history graph over the whole map
 * area, styled after the NOAA SWPC product of the same name. Tap inside any of the three
 * plots to see the nearest value and its age; tap the Resume button, tap anywhere else,
 * or wait for the timeout to restore the normal map.
 */
void plotRSGHistory (void)
{
    Serial.println ("RSG: pane opening");

    // fetch all three sources -- draw whatever is available, note what is not.
    // N.B. static, not stack-local: these are sizeable fixed arrays and this function
    //      may be reached with a fair bit already on the stack via the touch handler
    static XRayData xray;
    bool xray_ok = retrieveXRay (xray) && xray.data_ok;

    static ProtonData proton;
    bool proton_ok = retrieveProtonFlux (proton) && proton.data_ok;

    static KpData kp;
    bool kp_ok = retrieveKp (kp) && kp.data_ok;

    Serial.printf ("RSG: xray_ok=%d proton_ok=%d kp_ok=%d\n", xray_ok, proton_ok, kp_ok);

    // title text; actual drawing happens in redrawAll() below
    const char title[] = "Recent R-S-G reports";

    // resume button, upper right, same convention as plotMapData() but smaller and using
    // FAST_FONT explicitly both times it is drawn so it doesn't change size on tap
    SBox resume_b;
    resume_b.w = 70;
    resume_b.h = 22;
    resume_b.x = map_b.x + map_b.w - resume_b.w - 15;
    resume_b.y = map_b.y + 8;
    const char button_name[] = "Resume";

    // lay out three panels top to bottom, evenly spaced
    uint16_t top_y = map_b.y + RSG_TOP_Y;
    uint16_t avail_h = map_b.h - RSG_TOP_Y - 10;
    uint16_t panel_h = (avail_h - 2*RSG_PANEL_GAP) / 3;

    SBox xray_box   = { (uint16_t)(map_b.x+10), (uint16_t)(top_y),                             (uint16_t)(map_b.w-20), panel_h };
    SBox proton_box = { (uint16_t)(map_b.x+10), (uint16_t)(xray_box.y + panel_h+RSG_PANEL_GAP), (uint16_t)(map_b.w-20), panel_h };
    SBox kp_box     = { (uint16_t)(map_b.x+10), (uint16_t)(proton_box.y + panel_h+RSG_PANEL_GAP),(uint16_t)(map_b.w-20), panel_h };

    // X-ray flux: fixed flare-class ticks, A(-8) .. X(-4), tight range so labels don't crowd
    static const char *xray_lbls[] = {"X", "M", "C", "B", "A"};
    static int xray_vals[] = {-4, -5, -6, -7, -8};
    RSGPanelInfo xray_info = {}, proton_info = {}, kp_info = {};

    // (re)draws everything from scratch -- used for the initial draw and again to clear
    // any popup before the next one is shown
    auto redrawAll = [&]() {
        fillSBox (map_b, RA8875_BLACK);

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);
        uint16_t tw = getTextWidth (title);
        tft.setCursor (map_b.x + (map_b.w-tw)/2, map_b.y + RSG_TITLE_Y);
        tft.print (title);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        drawStringInBox (button_name, resume_b, false, RA8875_GREEN);

        rsgDrawPanel (xray_box, "Solar X-ray Flux",
                      xray.x, xray.l, xray_ok ? XRAY_NV : 0, xray_ok,
                      -8.5F, -3.5F, false, 'R', rsgRLevel, "Hours", xray_lbls, xray_vals, 5,
                      rsgFmtXray, &xray_info);

        rsgDrawPanel (proton_box, "Solar Proton Flux",
                      proton.x, proton.p, proton_ok ? PROTON_NV : 0, proton_ok,
                      0, 5, false, 'S', rsgSLevel, "Hours", NULL, NULL, 0,
                      rsgFmtProton, &proton_info);

        rsgDrawPanel (kp_box, "Geomagnetic Activity",
                      kp.x, kp.p, kp_ok ? KP_NV : 0, kp_ok,
                      0, 9, true, 'G', rsgGLevel, "Days", NULL, NULL, 0,
                      rsgFmtKp, &kp_info);

        // data source credit, bottom right
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (GRAY);
        const char credit[] = "Data: NOAA SWPC";
        uint16_t cw = getTextWidth (credit);
        tft.setCursor (map_b.x + map_b.w - cw - 10, map_b.y + map_b.h - RSG_FONTH - 2);
        tft.print (credit);

        tft.drawPR();
    };

    redrawAll();

    // interactive loop: taps inside a plot show a popup and keep the pane open; taps
    // anywhere else (including Resume), a keypress, or the timeout close it
    bool need_redraw = false;
    for (;;) {

        UserInput ui = {
            map_b,
            UI_UFuncNone,
            UF_UNUSED,
            60000,
            UF_CLOCKSOK,
            {0, 0}, TT_NONE, '\0', false, false
        };
        if (!waitForUser (ui))
            break;                                       // timed out
        if (ui.kb_char != CHAR_NONE)
            break;                                        // any keypress closes it

        if (need_redraw) {
            redrawAll();
            need_redraw = false;
        }

        if (inBox (ui.tap, xray_info.area) && xray_info.n > 0) {
            rsgShowPopup (xray_info, ui.tap, "h");
            need_redraw = true;
            continue;
        }
        if (inBox (ui.tap, proton_info.area) && proton_info.n > 0) {
            rsgShowPopup (proton_info, ui.tap, "h");
            need_redraw = true;
            continue;
        }
        if (inBox (ui.tap, kp_info.area) && kp_info.n > 0) {
            rsgShowPopup (kp_info, ui.tap, "d");
            need_redraw = true;
            continue;
        }

        // tap outside all three plot areas (eg Resume, or the margins) -- close
        break;
    }

    // ack and restore
    Serial.println ("RSG: pane closing, restoring map");
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    drawStringInBox (button_name, resume_b, true, RA8875_GREEN);
    tft.drawPR();

    initEarthMap();
    Serial.println ("RSG: map restored");
}
