/* lightning.cpp  - Blitzortung lightning overlay for HamClock via OHB
 *
 * Adds a "Lightning" toggle to the Map View menu. When enabled, fetches
 * pre-filtered strike data from the OHB backend every LIGHTNING_INTERVAL
 * seconds and renders age-coloured bolt icons on the map.
 *
 * Also provides:
 *   drawLightningRings()        - concentric great-circle range rings from DE
 *   drawNCDXFLightningStats()   - NCDXF-style panel showing strike counts
 *
 * The OHB backend (blitzortung_daemon.py + lightning/strikes.pl) handles:
 *   - Subscribing to the Blitzortung global MQTT feed
 *   - Maintaining a rolling 10-minute global strike buffer
 *   - Filtering to DE lat/lon + radius on request
 *   - Computing age in seconds at request time
 *
 * Response format  - plain text, one strike per line:
 *   lat,lon,age_seconds
 *
 * Strikes age out naturally: OHB only returns strikes younger than MAX_AGE
 * and HamClock replaces strikes[] entirely on each fetch.
 */

#include "HamClock.h"

// ---- tunables ------------------------------------------------------------

#define LIGHTNING_INTERVAL      (5*60)  // fetch interval, seconds
#define LIGHTNING_RETRY_SECS    60      // retry after failed fetch
#define LIGHTNING_MAX_STRIKES   5000    // worldwide coverage  - ~3 min of global activity

// Ring distances in km  - frames the 500km search radius
static const int ltg_ring_km[] = { 100, 200, 300, 400, 500 };
#define LTG_N_RINGS  ((int)NARRAY(ltg_ring_km))

// OHB endpoint
static const char ltg_strikes[] = "/ham/HamClock/lightning/strikes.pl";

// ---- types ---------------------------------------------------------------

typedef struct {
    float lat;      // decimal degrees N
    float lng;      // decimal degrees E
    int   age_s;    // seconds old at time of last fetch
} LightningStrike;

// ---- module state --------------------------------------------------------

static LightningStrike  strikes[LIGHTNING_MAX_STRIKES];
static int              n_strikes;
static time_t           next_fetch;
static time_t           limit_msg_until;

uint8_t  lightning_on;      // extern; saved to NV_LIGHTNING_ON
uint8_t  ltg_worldwide;     // extern; 1=worldwide, 0=radius mode
uint16_t ltg_radius_km;     // extern; search radius in km when not worldwide

#define LTG_RADIUS_DEFAULT  500
#define LTG_RADIUS_MIN      10
#define LTG_RADIUS_MAX      20000

static void showLightningRadiusLimitMsg(void)
{
    limit_msg_until = myNow() + 7;
}

// ---- bolt icon -----------------------------------------------------------
//
// 13-pixel-tall lightning bolt shape, 2px thick lines:
//   cx+2,cy-6  to  cx-2,cy-1   top stroke
//   cx-4,cy    to  cx+4,cy     horizontal bar
//   cx+2,cy+1  to  cx-2,cy+6   bottom stroke
//   white centre pixel for visibility on any background

static void drawBolt (int16_t cx, int16_t cy, uint16_t color)
{
    // Guard: bolt extends 6px vertically and 4px horizontally from centre.
    // Use raw map bounds to ensure every pixel lands on screen.
    uint16_t mx = (uint16_t)(tft.SCALESZ * map_b.x);
    uint16_t my = (uint16_t)(tft.SCALESZ * map_b.y);
    uint16_t mw = (uint16_t)(tft.SCALESZ * map_b.w);
    uint16_t mh = (uint16_t)(tft.SCALESZ * map_b.h);
    if (cx-4 < (int16_t)mx || cx+4 >= (int16_t)(mx+mw) ||
        cy-6 < (int16_t)my || cy+6 >= (int16_t)(my+mh))
        return;

    tft.drawLineRaw (cx+2, cy-6,  cx-2, cy-1,  2, color);
    tft.drawLineRaw (cx-4, cy,    cx+4, cy,     2, color);
    tft.drawLineRaw (cx+2, cy+1,  cx-2, cy+6,  2, color);
    tft.drawPixelRaw (cx, cy, RA8875_WHITE);
}

// ---- response parsing ----------------------------------------------------
//
// OHB returns plain text, one strike per line:   lat,lon,age_seconds

static int parseStrikes (const char *buf, int buf_len)
{
    int count = 0;
    const char *p   = buf;
    const char *end = buf + buf_len;

    while (p < end && count < LIGHTNING_MAX_STRIKES) {
        float lat, lon;
        int   age_s;
        if (sscanf (p, "%f,%f,%d", &lat, &lon, &age_s) == 3 && age_s >= 0) {
            strikes[count].lat   = lat;
            strikes[count].lng   = lon;
            strikes[count].age_s = age_s;
            count++;
        }
        const char *nl = (const char *) memchr (p, '\n', end - p);
        if (!nl) break;
        p = nl + 1;
    }
    return count;
}

// ---- fetch ---------------------------------------------------------------

static bool fetchLightning (void)
{
    if (ltg_worldwide)
        Serial.printf ("LTG: requesting worldwide\n");
    else
        Serial.printf ("LTG: requesting radius %dkm lat=%.4f lon=%.4f\n",
                       ltg_radius_km, (double)de_ll.lat_d, (double)de_ll.lng_d);

    WiFiClient client;
    bool ok = false;

    if (!client.connect (backend_host, backend_port)) {
        Serial.printf ("LTG: connect %s:%d failed\n", backend_host, backend_port);
        n_strikes = 0;   // reset on failure so panel shows clean zero
        return false;
    }

    // Build GET path  - add lat/lon/radius only when not worldwide
    client.print ("GET /ham/HamClock/lightning/strikes.pl");
    if (!ltg_worldwide) {
        client.print ("?lat=");
        client.print (de_ll.lat_d, 4);
        client.print ("&lon=");
        client.print (de_ll.lng_d, 4);
        client.print ("&radius=");
        client.print (ltg_radius_km);
    }
    client.print (" HTTP/1.0\r\n");
    client.print ("Host: ");
    client.println (backend_host);
    client.print ("Connection: close\r\n\r\n");

    if (!httpSkipHeader (client)) {
        Serial.printf ("LTG: no HTTP header\n");
        goto out;
    }

    {
        // 128KB handles ~5000 strikes at ~20 bytes each with headroom
        const int BUFSZ = 131072;
        char *buf = (char *) malloc (BUFSZ);
        if (!buf) {
            Serial.printf ("LTG: malloc %d failed\n", BUFSZ);
            goto out;
        }

        int  pos = 0;
        char line[128];
        while (getTCPLine (client, line, sizeof(line), NULL)) {
            int len = strlen (line);
            if (pos + len + 1 < BUFSZ) {
                memcpy (buf + pos, line, len);
                buf[pos + len] = '\n';
                pos += len + 1;
            }
        }
        buf[pos] = '\0';

        n_strikes = parseStrikes (buf, pos);
        free (buf);

        Serial.printf ("LTG: %d strikes %s\n", n_strikes,
                       ltg_worldwide ? "worldwide" : "in radius");

        // refresh NCDXF panel immediately if it's showing lightning stats
        if (brb_mode == BRB_SHOW_LIGHTNING)
            drawNCDXFLightningStats();

        ok = true;     // empty response is valid  - no storms nearby
    }

out:
    client.stop();
    if (!ok)
        n_strikes = 0;   // always reset on failure so panel shows clean zero
    return ok;
}

// ---- range rings ---------------------------------------------------------
//
// Draws concentric dashed great-circle rings centred on DE.
// Labels are staggered by using a slightly different azimuth for each ring
// so they fan out rather than stacking on top of each other.
// Each label gets a small black background for readability over any map.

static void drawLightningRings (void)
{
    const uint16_t ring_color = RGB565(100, 140, 180);  // blue-grey
    const float    step       = deg2rad (1.5F);          // step between ring points

    for (int r = 0; r < LTG_N_RINGS; r++) {

        // Angular radius in radians for this ring
        float ang_r = ltg_ring_km[r] / (ERAD_M * KM_PER_MI);

        // Draw dashed great circle  - matches drawDXPath() pattern
        SCoord s0 = {0, 0}, s1;
        int dot = 0;

        for (float az = 0; az < 2*M_PIF; az += step) {
            float ca, B;
            solveSphere (az, ang_r, sdelat, cdelat, &ca, &B);
            ll2sRaw (asinf(ca),
                     fmodf(de_ll.lng + B + 5*M_PIF, 2*M_PIF) - M_PIF,
                     s1, 2);

            if (s0.x) {
                if (segmentSpanOkRaw (s0, s1, tft.SCALESZ)) {
                    if (dot % 2 == 0) {
                        // guard both endpoints within raw map bounds before drawing
                        uint16_t mx = tft.SCALESZ * map_b.x;
                        uint16_t my = tft.SCALESZ * map_b.y;
                        uint16_t mw = tft.SCALESZ * map_b.w;
                        uint16_t mh = tft.SCALESZ * map_b.h;
                        if (s0.x >= mx && s0.x < mx+mw && s0.y >= my && s0.y < my+mh &&
                            s1.x >= mx && s1.x < mx+mw && s1.y >= my && s1.y < my+mh)
                            tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, 1, ring_color);
                    }
                } else {
                    s1.x = 0;
                }
            }

            s0 = s1;
            dot++;
        }
    }
}

// ---- NCDXF stats panel ---------------------------------------------------
//
// Draws strike counts into NCDXF_b using drawNCDXFStats()  - the same
// template used by the Space Weather and Weather panels.
//
// Four rows:
//   Total strikes   white
//   < 2 min         yellow  (fresh, RGB565(255,220,0))
//   2 - 5 min       orange  (RGB565(255,140,0))
//   5 - 10 min      red     (RGB565(220,40,40))

void drawNCDXFLightningStats (void)
{
    if (limit_msg_until != 0 && myNow() < limit_msg_until) {
        prepPlotBox (NCDXF_b);
        selectFontStyle (BOLD_FONT, FAST_FONT);
        tft.setTextColor (RA8875_RED);
        static const char * const lines[] = {
            "Max Earth",
            "grt circl",
            "distance",
            "20000 km",
        };
        uint16_t y = NCDXF_b.y + NCDXF_b.h/5;
        const uint16_t line_step = 16;
        for (int i = 0; i < (int)NARRAY(lines); i++) {
            uint16_t w = getTextWidth (lines[i]);
            tft.setCursor (NCDXF_b.x + (NCDXF_b.w - w)/2, y);
            tft.print (lines[i]);
            y += line_step;
        }
        return;
    }

    // Erase panel
    fillSBox (NCDXF_b, RA8875_BLACK);

    // Count by age band
    int fresh = 0, recent = 0, old = 0;
    for (int i = 0; i < n_strikes; i++) {
        if      (strikes[i].age_s < 120) fresh++;
        else if (strikes[i].age_s < 300) recent++;
        else                             old++;
    }

    // Clamp counts to 0..9999 so %u always fits in NCDXF_B_MAXLEN (max 4 digits + NUL).
    // The clamp also satisfies GCC's value-range analysis to avoid -Wformat-truncation.
    #define LTG_CLAMP(n) ((unsigned)((n) < 0 ? 0 : (n) > 9999 ? 9999 : (n)))

    char     titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    char     values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];

    // Defensive: zero-init the stack arrays so any unfilled slot renders
    // as an empty string / black instead of whatever was on the stack.
    memset (titles, 0, sizeof(titles));
    memset (values, 0, sizeof(values));
    memset (colors, 0, sizeof(colors));

    snprintf (titles[0], NCDXF_B_MAXLEN, "%s", ltg_worldwide ? "Wldwide" : "Radius");
    snprintf (values[0], NCDXF_B_MAXLEN, "%u", LTG_CLAMP(n_strikes));
    colors[0] = RA8875_WHITE;

    snprintf (titles[1], NCDXF_B_MAXLEN, "%s", "< 2 min");
    snprintf (values[1], NCDXF_B_MAXLEN, "%u", LTG_CLAMP(fresh));
    colors[1] = RGB565(255, 220, 0);

    snprintf (titles[2], NCDXF_B_MAXLEN, "%s", "2-5 min");
    snprintf (values[2], NCDXF_B_MAXLEN, "%u", LTG_CLAMP(recent));
    colors[2] = RGB565(255, 140, 0);

    snprintf (titles[3], NCDXF_B_MAXLEN, "%s", "5-10min");
    snprintf (values[3], NCDXF_B_MAXLEN, "%u", LTG_CLAMP(old));
    colors[3] = RGB565(220, 40, 40);

    #undef LTG_CLAMP

    drawNCDXFStats (RA8875_BLACK, titles, values, colors);
}

// ---- public API ----------------------------------------------------------

/* Clear strikes and reset fetch timer.
 * Toggling on:  next updateLightning() fetches immediately.
 * Toggling off: n_strikes=0 means drawLightningOnMap() draws nothing.
 */
void resetLightning (void)
{
    n_strikes  = 0;
    next_fetch = 0;
}

/* Restore NV state at startup.
 * N.B. follows the HamClock pattern of writing the default if no cookie found.
 */
void initLightning (void)
{
    if (!NVReadUInt8 (NV_LIGHTNING_ON, &lightning_on)) {
        lightning_on = 0;
        NVWriteUInt8 (NV_LIGHTNING_ON, lightning_on);
    }

    if (!NVReadUInt8 (NV_LTG_WORLDWIDE, &ltg_worldwide)) {
        ltg_worldwide = 1;                              // default worldwide
        NVWriteUInt8 (NV_LTG_WORLDWIDE, ltg_worldwide);
    }

    if (!NVReadUInt16 (NV_LTG_RADIUS, &ltg_radius_km) ||
            ltg_radius_km < LTG_RADIUS_MIN || ltg_radius_km > LTG_RADIUS_MAX) {
        ltg_radius_km = LTG_RADIUS_DEFAULT;
        NVWriteUInt16 (NV_LTG_RADIUS, ltg_radius_km);
    }

    n_strikes  = 0;
    next_fetch = 0;
    limit_msg_until = 0;
    Serial.printf ("LTG: init, overlay %s mode %s radius %dkm\n",
                   lightning_on  ? "ON" : "OFF",
                   ltg_worldwide ? "worldwide" : "radius",
                   ltg_radius_km);
}

/* Fetch fresh data if interval elapsed. Called from updateWiFi(). */
void updateLightning (void)
{
    if (!lightning_on)
        return;

    time_t now = myNow();
    if (limit_msg_until != 0 && now >= limit_msg_until) {
        limit_msg_until = 0;
        if (brb_mode == BRB_SHOW_LIGHTNING)
            drawNCDXFLightningStats();
    }
    if (now < next_fetch)
        return;

    if (fetchLightning())
        next_fetch = now + LIGHTNING_INTERVAL;
    else
        next_fetch = now + LIGHTNING_RETRY_SECS;
}

/* Handle a tap on the lightning NCDXF panel.
 * Shows a menu to choose worldwide vs radius mode.
 * MENU_TEXT field holds the radius in km.
 */
void doLightningTouch (void)
{
    // text field buffer for radius value  - must persist through runMenu
    char rad_buf[8];
    char rad_lbl[] = "";           // no prefix label -- units shown in radio button

    // initialise text field with current radius
    snprintf (rad_buf, sizeof(rad_buf), "%u", ltg_radius_km);

    MenuText mt;
    memset (&mt, 0, sizeof(mt));
    mt.text    = rad_buf;
    mt.t_mem   = sizeof(rad_buf);
    mt.label   = rad_lbl;
    mt.l_mem   = sizeof(rad_lbl);
    mt.to_upper = false;

    #define LTG_MI_WORLD  0
    #define LTG_MI_RADIUS 1
    #define LTG_MI_TEXT   2
    #define LTG_MI_N      3

    MenuItem mitems[LTG_MI_N] = {
        {MENU_1OFN,  (bool) ltg_worldwide,  1, 2, "Wldwide",  NULL},
        {MENU_1OFN,  (bool)!ltg_worldwide,  1, 2, "r (km):",  NULL},
        {MENU_TEXT,  false,                 2, 4, NULL,        &mt},
    };

    SBox menu_b, ok_b;
    menu_b.x = NCDXF_b.x + 2;
    menu_b.y = NCDXF_b.y + 2;
    menu_b.w = 0;
    menu_b.h = 0;

    // erase panel so its borders don't show through the menu background
    fillSBox (NCDXF_b, RA8875_BLACK);
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, LTG_MI_N, mitems};

    if (runMenu (menu)) {
        // read radio button choice
        ltg_worldwide = mitems[LTG_MI_WORLD].set ? 1 : 0;
        NVWriteUInt8 (NV_LTG_WORLDWIDE, ltg_worldwide);

        // parse radius from text field if radius mode selected
        if (!ltg_worldwide) {
            int r = atoi (rad_buf);
            Serial.printf ("LTG: radius text '%s' parsed as %d\n", rad_buf, r);
            if (r > LTG_RADIUS_MAX) {
                ltg_radius_km = LTG_RADIUS_MAX;
                showLightningRadiusLimitMsg();
            } else if (r >= LTG_RADIUS_MIN && r <= LTG_RADIUS_MAX) {
                ltg_radius_km = (uint16_t) r;
            } else {
                ltg_radius_km = LTG_RADIUS_DEFAULT;
            }
            NVWriteUInt16 (NV_LTG_RADIUS, ltg_radius_km);
            Serial.printf ("LTG: radius set to %dkm\n", ltg_radius_km);
        }

        // force immediate refetch with new settings
        resetLightning();
    }

    // redraw panel
    drawNCDXFLightningStats();

    #undef LTG_MI_WORLD
    #undef LTG_MI_RADIUS
    #undef LTG_MI_TEXT
    #undef LTG_MI_N
}

/* Draw lightning overlay on the map. Called from drawAllSymbols().
 *
 * Rings and attribution: shown whenever overlay is enabled, Mercator only.
 * Strike bolts: drawn on all projections when strikes are present.
 *
 * Colour encodes age at time of last fetch:
 *   < 2 min  - bright yellow
 *   < 5 min  - orange
 *   older    - red
 */
void drawLightningOnMap (void)
{
    if (!lightning_on)
        return;

    // Rings and attribution  - Mercator only, always on when overlay enabled
    if (map_proj == MAPP_MERCATOR) {
        drawLightningRings();

        static const char credit[] = "Blitzortung.org";
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        uint16_t cw = getTextWidth (credit);
        // place above RSS banner if it's showing, otherwise at map bottom
        uint16_t cy = rss_on
                    ? rss_bnr_b.y - 10
                    : map_b.y + map_b.h - 10;
        tft.setCursor (map_b.x + (map_b.w - cw)/2, cy);
        tft.setTextColor (RGB565(180, 180, 180));
        tft.print (credit);
    }

    if (n_strikes == 0)
        return;

    for (int i = 0; i < n_strikes; i++) {

        SCoord s;
        ll2sRaw (strikes[i].lat * (M_PIF / 180.0F),
                 strikes[i].lng * (M_PIF / 180.0F),
                 s, 8);

        if (s.x == 0 && s.y == 0)
            continue;

        // Don't draw over the RSS banner
        if (overRSS (raw2appSCoord (s)))
            continue;

        uint16_t color;
        if      (strikes[i].age_s < 120)  color = RGB565(255, 220,   0);
        else if (strikes[i].age_s < 300)  color = RGB565(255, 140,   0);
        else                              color = RGB565(220,  40,  40);

        drawBolt ((int16_t)s.x, (int16_t)s.y, color);
    }
}
