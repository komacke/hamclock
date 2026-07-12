/* hamqsl.cpp
 *
 * HamClock panes: HF Conditions and VHF Conditions.
 *
 * Both render from the single CSV that the OHB proxy (fetch_hamqsl.py) writes
 * from Paul Herrman N0NBH's HamQSL solar feed:
 *
 *     /ham/HamClock/hamqsl/hamqsl-cond.csv
 *
 * The file's first lines begin with '#': a comment/attribution header carrying
 * the HamQSL update time and OHB fetch time. We skip them. Remaining rows are
 *     section,name,qualifier,value
 * with sections SOLAR, HF, VHF.
 *
 * HF falls back to the locally-computed SFI+K model (validated against N0NBH's
 * own published examples) if the live HF cells are ever absent.
 *
 * Data courtesy HamQSL.com / Paul Herrman N0NBH, used with permission.
 * Credit is rendered in both pane footers.
 */

#include "HamClock.h"

// backend file written by the OHB proxy
static const char hq_page[] = "/hamqsl/hamqsl-cond.csv";

// poll a little behind the proxy's 10-min cadence
#define HQ_MAXLINE      120

// colours
#define HQ_SUN          RA8875_YELLOW
#define HQ_MOON         BRGRAY
#define HQ_ES2          RGB565(255,70,196)              // 144 MHz ES (magenta)
#define HQ_ES4          RGB565(154,212,0)               // 70 MHz ES (yellow-green)
#define HQ_TBD_BG       RGB565(45,45,45)                // neutral cell fill while awaiting data

// rating codes
enum { HQ_POOR = 0, HQ_FAIR = 1, HQ_GOOD = 2, HQ_UNK = -1 };

// one parsed snapshot
typedef struct {
    bool data_ok;                                       // at least the 8 HF cells valid
    time_t next_update;                                 // when to retrieve again
    bool from_model;                                    // HF came from local fallback model
    // solar drivers (display + fallback model)
    int sfi, sn, kp;
    // HF: [group 0..3 = 80-40,30-20,17-15,12-10][0=day,1=night]
    int8_t hf[4][2];
    // VHF categorical strings
    char aurora[16], es6[16], es4[16], es2eu[16], es2na[16];
    char geomag[16], signoise[12];
    char muf[12];                                       // ionosonde HF MUF, MHz text or "NoRpt"
} HamQSLData;

static HamQSLData hq_cache;

static const char *hf_group[4] = { "80-40", "30-20", "17-15", "12-10" };

static int8_t crackRating (const char *s)
{
    if (strcasestr (s, "Good")) return HQ_GOOD;
    if (strcasestr (s, "Fair")) return HQ_FAIR;
    if (strcasestr (s, "Poor")) return HQ_POOR;
    return HQ_UNK;
}

static int hfGroupIndex (const char *name)
{
    for (int i = 0; i < 4; i++) {
        // match the leading "80m-40m" against our short "80-40" by first 2 digits
        if (name[0] == hf_group[i][0] && name[1] == hf_group[i][1])
            return i;
    }
    return -1;
}

/* validated local fallback: Good/Fair/Poor from SFI and Kp.
 * Matches N0NBH's published examples 112/112 across solar-min..max.
 */
static int8_t hfModel (int grp, int tod, int sfi, int kp)
{
    bool day = (tod == 0);
    switch (grp) {
    case 0: // 80-40
        return day ? ((sfi >= 155 || kp >= 3) ? HQ_POOR : HQ_FAIR)
                   : ((kp >= 3) ? HQ_FAIR : HQ_GOOD);
    case 1: // 30-20
        return (sfi < 100 || kp >= 4) ? HQ_FAIR : HQ_GOOD;
    case 2: // 17-15
        if (sfi < 100) return HQ_POOR;
        return (sfi < 125 || kp >= 4) ? HQ_FAIR : HQ_GOOD;
    case 3: // 12-10
        return day ? (sfi >= 160 ? HQ_GOOD : (sfi >= 120 ? HQ_FAIR : HQ_POOR))
                   : HQ_POOR;
    }
    return HQ_UNK;
}

/* sun: filled disc + 8 rays. Same construction as wxDrawSun() in earthsat.cpp. */
static void drawSunGlyph (uint16_t cx, uint16_t cy, uint16_t r, uint16_t col)
{
    for (int a = 0; a < 8; a++) {
        float ang = a * M_PIF / 4;
        tft.drawLine (cx + (r+2)*cosf(ang), cy + (r+2)*sinf(ang),
                      cx + (r+5)*cosf(ang), cy + (r+5)*sinf(ang), col);
    }
    tft.fillCircle (cx, cy, r, col);
}

/* crescent moon: disc, then carve a bite with a bg-coloured disc. */
static void drawMoonGlyph (uint16_t cx, uint16_t cy, uint16_t r, uint16_t col, uint16_t bg)
{
    tft.fillCircle (cx, cy, r, col);
    tft.fillCircle (cx + 0.55F*r, cy - 0.30F*r, r, bg);
}

static bool retrieveHamQSL (HamQSLData &hq)
{
    // serve cache if still fresh
    if (myNow() < hq_cache.next_update && hq_cache.data_ok) {
        hq = hq_cache;
        return (true);
    }

    WiFiClient hq_client;
    char line[HQ_MAXLINE];
    bool ok = false;
    int n_hf = 0;

    // reset to unknown
    memset (&hq_cache, 0, sizeof(hq_cache));
    for (int i = 0; i < 4; i++) { hq_cache.hf[i][0] = HQ_UNK; hq_cache.hf[i][1] = HQ_UNK; }

    Serial.println (hq_page);
    if (hq_client.connect (backend_host, backend_port)) {
        updateClocks (false);
        httpHCGET (hq_client, backend_host, hq_page);
        if (!httpSkipHeader (hq_client)) {
            Serial.print ("HAMQSL: header short\n");
            goto out;
        }
        ok = true;                                      // transaction itself worked

        while (getTCPLine (hq_client, line, sizeof(line), NULL)) {
            if (line[0] == '#' || line[0] == '\0')      // skip comments / blanks
                continue;

            // split into up to 4 comma fields in place
            char *f[4] = { line, NULL, NULL, NULL };
            int nf = 1;
            for (char *p = line; *p && nf < 4; p++) {
                if (*p == ',') { *p = '\0'; f[nf++] = p + 1; }
            }
            if (nf < 4)
                continue;
            const char *sec = f[0], *name = f[1], *qual = f[2], *val = f[3];

            if (!strcmp (sec, "SOLAR")) {
                if      (!strcmp (name, "sfi")) hq_cache.sfi = atoi (val);
                else if (!strcmp (name, "sn"))  hq_cache.sn  = atoi (val);
                else if (!strcmp (name, "k"))   hq_cache.kp  = atoi (val);
                else if (!strcmp (name, "geomagfield"))
                    strncpy (hq_cache.geomag, val, sizeof(hq_cache.geomag)-1);
                else if (!strcmp (name, "signalnoise"))
                    strncpy (hq_cache.signoise, val, sizeof(hq_cache.signoise)-1);
                else if (!strcmp (name, "muf"))
                    strncpy (hq_cache.muf, val, sizeof(hq_cache.muf)-1);
            } else if (!strcmp (sec, "HF")) {
                int g = hfGroupIndex (name);
                int tod = strcmp (qual, "night") ? 0 : 1;
                int8_t r = crackRating (val);
                if (g >= 0 && r != HQ_UNK) { hq_cache.hf[g][tod] = r; n_hf++; }
            } else if (!strcmp (sec, "VHF")) {
                char *dst = NULL;
                if      (!strcmp (name, "vhf-aurora"))        dst = hq_cache.aurora;
                else if (!strcmp (qual, "europe_6m"))         dst = hq_cache.es6;
                else if (!strcmp (qual, "europe_4m"))         dst = hq_cache.es4;
                else if (!strcmp (qual, "europe"))            dst = hq_cache.es2eu;
                else if (!strcmp (qual, "north_america"))     dst = hq_cache.es2na;
                if (dst) strncpy (dst, val, 15);
            }
        }

        // HF valid if we got all 8 cells; else synthesise from the model if we
        // at least have SFI (keeps the pane alive if HF block is ever dropped).
        if (n_hf >= 8) {
            hq_cache.data_ok = true;
            hq_cache.from_model = false;
        } else if (hq_cache.sfi > 0) {
            for (int g = 0; g < 4; g++)
                for (int t = 0; t < 2; t++)
                    hq_cache.hf[g][t] = hfModel (g, t, hq_cache.sfi, hq_cache.kp);
            hq_cache.data_ok = true;
            hq_cache.from_model = true;
        }
    } else {
        Serial.println ("HAMQSL: connection failed");
    }

out:
    hq_cache.next_update = ok ? nextRetrieval (PLOT_CH_HFCOND, HQ_INTERVAL)
                              : nextWiFiRetry (PLOT_CH_HFCOND);
    hq = hq_cache;
    return (ok && hq_cache.data_ok);
}

/* colour-code the S-meter noise level: S0-2 green, S3-4 yellow, S5-6 orange, S7+ red */
static uint16_t noiseColor (const char *sn)
{
    const char *p = strchr (sn, 'S');
    int n = (p && p[1]) ? atoi (p + 1) : 0;             // first S-number, e.g. "S3-S4" -> 3
    if (n <= 2) return RA8875_GREEN;
    if (n <= 4) return RA8875_YELLOW;
    if (n <= 6) return RGB565(255,140,0);               // orange
    return RA8875_RED;
}

/* parse the <muf> field. Returns MHz (>0) when N0NBH reports a numeric ionosonde
 * MUF (e.g. "11.01"), or -1 for "NoRpt"/blank/non-numeric. */
static float parseMuf (void)
{
    const char *s = hq_cache.muf;
    while (*s == ' ') s++;
    if (!*s || (s[0]|0x20) == 'n')                      // "NoRpt"/blank
        return -1.0f;
    char *end = NULL;
    float v = strtof (s, &end);
    return (end != s && v > 0) ? v : -1.0f;
}
#define HQ_MUF_FS   35.0f                               // MUF RT legend full-scale, MHz

/* Map MHz to the MUF RT map legend supplied by HamQSL: purple -> blue ->
 * cyan -> green -> yellow -> orange -> red over 0..35 MHz. */
static uint16_t mufColor (float mhz)
{
    static const float F[8] = { 0, 5, 10, 15, 20, 25, 30, 35 };
    static const uint8_t R[8] = { 45, 45,   0,  70,  90, 210, 255, 220 };
    static const uint8_t G[8] = {  0,  0,  70, 220, 245, 255, 145,  30 };
    static const uint8_t B[8] = { 85,170, 255, 190,  80,  30,   0,   0 };

    if (mhz <= F[0])
        return RGB565 (R[0], G[0], B[0]);
    if (mhz >= F[7])
        return RGB565 (R[7], G[7], B[7]);

    int i = 0;
    while (i < 7 && mhz > F[i+1])
        i++;

    float u = (mhz - F[i]) / (F[i+1] - F[i]);
    uint8_t r = (uint8_t)(R[i] + ((int)R[i+1] - (int)R[i]) * u);
    uint8_t g = (uint8_t)(G[i] + ((int)G[i+1] - (int)G[i]) * u);
    uint8_t b = (uint8_t)(B[i] + ((int)B[i+1] - (int)B[i]) * u);
    return RGB565 (r, g, b);
}

void plotHFConditions (const SBox &box)
{
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    const char *title = "HF Bands";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w - tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // geometry
    const uint16_t lblw = 34 * box.w / 160;             // band-label column
    const uint16_t gx   = box.x + 2;
    const uint16_t gw   = box.w - 4;
    const uint16_t colw = (gw - lblw) / 2;
    const uint16_t head_y = box.y + SUBTITLE_Y0;
    const uint16_t gy   = head_y + 14;
    const uint16_t gh   = box.h - (gy - box.y) - 34;    // room for MUF + noise rows + credit
    const uint16_t rowh = gh / 4;
    const uint16_t col0 = gx + lblw;
    const uint16_t col1 = col0 + colw;
    const uint16_t rs   = box.w / 53;                   // sun radius scales with build
    const uint16_t r_sun = rs < 3 ? 3 : rs;
    const uint16_t r_moon = r_sun + 2;

    // column headers: sun centered over the Day column, moon centered over the Night column
    drawSunGlyph  (col0 + colw/2, head_y + 4, r_sun,  HQ_SUN);
    drawMoonGlyph (col1 + colw/2, head_y + 4, r_moon, HQ_MOON, RA8875_BLACK);

    // rows  (use the small font for band labels + cell text; title above used SMALL_FONT)
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    for (int g = 0; g < 4; g++) {
        uint16_t y = gy + g*rowh;

        // band-group label
        tft.setTextColor (GRAY);
        tft.setCursor (gx, y + rowh/2 - 3);
        tft.print (hf_group[g]);

        for (int t = 0; t < 2; t++) {
            uint16_t x = (t == 0 ? col0 : col1);

            if (!hq_cache.data_ok) {
                // no backend data yet -> neutral "TBD" placeholder
                tft.fillRect (x, y, colw - 2, rowh - 2, HQ_TBD_BG);
                tft.setTextColor (GRAY);
                uint16_t lw = getTextWidth ("TBD");
                tft.setCursor (x + (colw - 2 - lw)/2, y + rowh/2 - 3);
                tft.print ("TBD");
                continue;
            }

            int8_t r = hq_cache.hf[g][t];
            uint16_t col = (r == HQ_GOOD) ? RA8875_GREEN
                         : (r == HQ_FAIR) ? RA8875_YELLOW
                         : (r == HQ_POOR) ? RA8875_RED : RA8875_BLACK;
            tft.fillRect (x, y, colw - 2, rowh - 2, col);

            const char *txt = (r == HQ_GOOD) ? "Good" : (r == HQ_FAIR) ? "Fair"
                            : (r == HQ_POOR) ? "Poor" : "?";
            tft.setTextColor (RA8875_BLACK);
            uint16_t lw = getTextWidth (txt);
            tft.setCursor (x + (colw - 2 - lw)/2, y + rowh/2 - 3);
            tft.print (txt);
        }
    }

    // two status rows below the grid:
    // MUF  [gradient----|-----] value
    // Noise                    S3-S4
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    const uint16_t lx = box.x + 4;
    const uint16_t rx = box.x + box.w - 4;

    // MUF row: HamQSL's station-specific ionosonde MUF, Boulder by default.
    {
        const uint16_t by = box.y + box.h - 29;
        const uint16_t bh = 7;
        float muf = hq_cache.data_ok ? parseMuf() : -1.0f;

        tft.setTextColor (BRGRAY);
        tft.setCursor (lx, by);
        tft.print ("MUF");

        char rd[12];
        if (muf > 0)
            snprintf (rd, sizeof(rd), "%.0f", muf);
        else
            strcpy (rd, "NoRpt");
        uint16_t rw = getTextWidth (rd);
        tft.setTextColor (muf > 0 ? BRGRAY : GRAY);
        tft.setCursor (rx - rw, by);
        tft.print (rd);

        uint16_t bx = lx + getTextWidth ("MUF") + 4;
        uint16_t bw = (rx - rw - 4) - bx;
        if (bw > 1) {
            if (muf <= 0) {
                tft.fillRect (bx, by, bw, bh, HQ_TBD_BG);
            } else {
                // Draw the same 0..35 MHz colour progression used by the MUF RT map.
                for (uint16_t x = 0; x < bw; x++) {
                    float mhz = HQ_MUF_FS * x / (bw - 1);
                    tft.drawLine (bx + x, by, bx + x, by + bh - 1, mufColor (mhz));
                }

                // White marker at the current MUF; values above 35 MHz pin at the right edge.
                float f = muf / HQ_MUF_FS;
                if (f > 1) f = 1;
                uint16_t mx = bx + (uint16_t)(f * (bw - 1));
                tft.fillRect (mx > bx ? mx - 1 : mx, by - 1, 2, bh + 2, RA8875_WHITE);
            }
        }
    }

    // Noise row: retain HamQSL's colour-coded S-meter noise report.
    {
        const uint16_t ny = box.y + box.h - 19;
        tft.setTextColor (BRGRAY);
        tft.setCursor (lx, ny);
        tft.print ("Noise");

        const char *sn = hq_cache.data_ok && hq_cache.signoise[0]
                       ? hq_cache.signoise : "TBD";
        uint16_t nw = getTextWidth (sn);
        tft.setTextColor (hq_cache.data_ok && hq_cache.signoise[0]
                         ? noiseColor (sn) : GRAY);
        tft.setCursor (rx - nw, ny);
        tft.print (sn);
    }

    // credit footer (Paul's one condition) + model flag if applicable
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (hq_cache.from_model ? RA8875_YELLOW : GRAY);
    const char *cr = hq_cache.from_model ? "OHB model (SFI/K)" : "HamQSL.com  N0NBH";
    uint16_t cw = getTextWidth (cr);
    tft.setCursor (box.x + (box.w - cw)/2, box.y + box.h - 9);
    tft.print (cr);
}

static uint16_t vhfColor (const char *v)
{
    if (strstr (v, "144"))         return HQ_ES2;
    if (strstr (v, "70"))          return HQ_ES4;
    if (strstr (v, "50"))          return RA8875_GREEN;
    if (strcasestr (v, "High MUF"))return RA8875_YELLOW;
    if (strcasestr (v, "MID LAT")) return RA8875_RED;
    if (strcasestr (v, "High LAT"))return RA8875_YELLOW;
    return GRAY;                                         // Band Closed / NoRpt / blank
}

/* compress "Band Closed" -> "Closed", "50MHz ES" -> "50 ES" for the small pane */
static void vhfShort (const char *in, char *out, size_t out_len)
{
    if (!in[0] || strcasestr (in, "Band Closed")) {
        strncpy (out, "Closed", out_len); out[out_len-1] = 0; return;
    }
    // "<freq>MHz ES" -> "<freq> ES"  (e.g. "50MHz ES" -> "50 ES", "50/70/144MHz ES" -> "50/70/144 ES")
    const char *m = strstr (in, "MHz");
    if (m) {
        size_t n = (size_t)(m - in);
        if (n > out_len - 5) n = out_len - 5;
        memcpy (out, in, n);
        snprintf (out + n, out_len - n, " ES");
        return;
    }
    strncpy (out, in, out_len); out[out_len-1] = 0;
}

void plotVHFConditions (const SBox &box)
{
    prepPlotBox (box);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    const char *title = "VHF Conditions";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w - tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // rows: label + value from the feed (5 rows; geomag/noise moved to the HF pane)
    struct { const char *lab; const char *val; } row[5] = {
        { "Aurora", hq_cache.aurora },
        { "6m EsEU",  hq_cache.es6    },
        { "4m EsEU",  hq_cache.es4    },
        { "2m EsEU",  hq_cache.es2eu  },
        { "2m EsNA",  hq_cache.es2na  },
    };

    const uint16_t gy   = box.y + SUBTITLE_Y0;
    const uint16_t gh   = box.h - (gy - box.y) - 10;    // MUF moved to HF pane
    const uint16_t rowh = gh / 5;
    const uint16_t lx   = box.x + 4;                     // labels: left-justified
    const uint16_t rx   = box.x + box.w - 4;             // values: right-justified to this edge

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    for (int i = 0; i < 5; i++) {
        uint16_t y = gy + i*rowh + rowh/2 - 3;
        tft.setTextColor (BRGRAY);
        tft.setCursor (lx, y);
        tft.print (row[i].lab);

        char shrt[16];
        uint16_t vcol;
        if (!hq_cache.data_ok) {                        // no backend data yet
            strcpy (shrt, "TBD");
            vcol = GRAY;
        } else {
            vhfShort (row[i].val, shrt, sizeof(shrt));
            vcol = vhfColor (row[i].val);
        }
        uint16_t vw = getTextWidth (shrt);
        tft.setTextColor (vcol);
        tft.setCursor (rx - vw, y);                     // right-justify the value
        tft.print (shrt);
    }

    // credit footer
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    const char *cr = "HamQSL.com  N0NBH";
    uint16_t cw = getTextWidth (cr);
    tft.setCursor (box.x + (box.w - cw)/2, box.y + box.h - 9);
    tft.print (cr);
}

bool updateHFConditions (const SBox &box)
{
    // Paint immediately so a freshly-selected pane shows "TBD" during the
    // (blocking) backend call, rather than a blank box.
    if (!hq_cache.data_ok)
        plotHFConditions (box);

    HamQSLData hq;
    bool ok = retrieveHamQSL (hq);

    // Redraw with whatever we have now: live data, model fallback, or still TBD.
    plotHFConditions (box);
    return (ok);
}

bool updateVHFConditions (const SBox &box)
{
    if (!hq_cache.data_ok)
        plotVHFConditions (box);

    HamQSLData hq;
    bool ok = retrieveHamQSL (hq);                      // shared cache; one fetch feeds both

    plotVHFConditions (box);
    return (ok);
}
