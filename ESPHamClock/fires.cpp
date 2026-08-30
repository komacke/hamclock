/* fires.cpp -- global active-fire hotspot overlay for HamClock
 *
 * Fetches pre-filtered active-fire detections from the OHB backend (fires/hotspots.txt), which
 * fetch_fires.py maintains by polling NASA FIRMS (VIIRS NOAA-20/21) and reducing it to a small
 * flat file -- same OHB-proxy pattern as lightning.cpp, hurricane.cpp and firewx.cpp, minus the
 * CGI hop: the poller writes hotspots.txt straight into the web server's static docroot, so
 * HamClock just GETs it directly like any other static file. The backend applies the
 * confidence/FRP threshold so HamClock only ever sees fires worth showing; a raw world VIIRS
 * query can otherwise return 30,000-100,000+ detections/day.
 *
 * Data format from /fires/hotspots.txt -- one line per hotspot, comma-separated, 3 fields:
 *   LAT,LON,FRP
 * where FRP is Fire Radiative Power in megawatts (backend already dropped anything below
 * FIRE_MIN_FRP_MW and anything below "nominal" confidence -- see OHB-side fetch_fires.py).
 *
 * On-map presence: a real badge button next to View/Borders (not buried in the map menu, which
 * is already full), shown only when it's meaningful to look for fires -- Countries, Terrain or
 * Clouds, and only in the Mercator projection, matching how bordersBadgeVisible() gates itself
 * to Clouds/Terrain + Mercator/Robinson. Toggling the badge only hides the badge and the glyphs;
 * fires_on is the actual on/off state and persists across projection/map changes, same contract
 * as borders_on.
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define FIRE_INTERVAL      (15*60)    // fetch interval, secs -- FIRMS itself only updates ~q 3 hrs

// Storing hotspots is cheap (12 bytes each); it's the per-pixel sprite blit that's expensive,
// and that cost is now bounded separately by the screen-space de-clutter grid in
// drawFiresOnMap() -- one glyph drawn per grid cell, regardless of how many raw hotspots
// fall in it. So this cap just bounds memory/fetch size, not frame time.
//
// Set generously: the underlying feed's response isn't sorted by region, so a low cap silently
// truncates by file order rather than by geography -- e.g. a cap of 4000 against a ~17000-line
// global response dropped most of a given day's Americas hotspots simply because they happened
// to appear later in the file than that day's African agricultural burns. This build targets
// desktop/RPi (see Makefile), not embedded ESP32, so a few hundred KB here is not a concern.
#define FIRE_MAXHOTSPOTS   25000

static const char fires_page[] = "/fires/hotspots.txt";   // static file, not a CGI
static const char fires_fn[]   = "hotspots.txt";                       // local cache filename

#define FIRE_MINSIZ        1          // min acceptable file size (empty file = 0 hotspots, still ok)

#define FIRE_COLOR_OUTER   RGB565(255,60,0)      // outer flame tongue -- red-orange
#define FIRE_COLOR_INNER   RGB565(255,200,0)      // inner flame tongue -- yellow highlight
#define FIRE_CELL_MULT     1                       // de-clutter cell = this * sprite width

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

typedef struct {
    float lat;      // decimal degrees N
    float lng;      // decimal degrees E
    float frp;      // Fire Radiative Power, MW -- already thresholded server-side
} FireHotspot;

static FireHotspot fire_spots[FIRE_MAXHOTSPOTS];
static int         n_fires;
static time_t      fires_next_fetch;

uint8_t fires_on;                       // extern; saved to NV_FIRES_ON
SBox    fires_btn_b;                    // extern; badge box, geometry set each draw (floats with Borders)

// ---------------------------------------------------------------------------
// badge visibility and geometry
// ---------------------------------------------------------------------------

/* return whether the on-map "Fires On/Off" badge should currently be shown.
 * Offered on Countries, Terrain or Clouds, and only in the Mercator projection -- unlike the
 * Borders badge this deliberately does NOT include Robinson, since that's the caller's spec.
 * Like bordersBadgeVisible(), this is only a UI convenience: hiding the badge never touches
 * fires_on, so the setting the user last chose keeps applying silently once they come back to
 * a qualifying map/projection.
 */
bool firesBadgeVisible(void)
{
    return ((core_map == CM_COUNTRIES || core_map == CM_TERRAIN || core_map == CM_CLOUDS)
                        && map_proj == MAPP_MERCATOR);
}

/* draw (or blank) the on-map "Fires On/Off" badge.
 * Sits immediately right of the Borders badge when Borders is also showing (Terrain/Clouds),
 * or immediately right of the View button when it isn't (Countries has no Borders badge) --
 * recomputed here every draw, same convention as drawBordersButton() tracking view_btn_b.y.
 */
void drawFiresButton(void)
{
    fires_btn_b.y = view_btn_b.y;

    if (!firesBadgeVisible()) {
        // wrong core map or projection -- leave the map pixels already painted here alone.
        return;
    }

    const int gap = 4;
    const int pad = 8;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t left_edge = bordersBadgeVisible() ? (borders_btn_b.x + borders_btn_b.w)
                                                : (view_btn_b.x + view_btn_b.w);
    fires_btn_b.x = left_edge + gap;
    fires_btn_b.w = getTextWidth ("Fires Off") + pad;
    fires_btn_b.h = view_btn_b.h;

    uint16_t fill_clr = fires_on ? RA8875_WHITE : RA8875_BLACK;
    uint16_t text_clr = fires_on ? RA8875_BLACK : RA8875_WHITE;
    const char *label = fires_on ? "Fires On" : "Fires Off";

    tft.fillRect (fires_btn_b.x, fires_btn_b.y, fires_btn_b.w-1, fires_btn_b.h-1, fill_clr);
    tft.drawRect (fires_btn_b.x, fires_btn_b.y, fires_btn_b.w-1, fires_btn_b.h-1, RA8875_WHITE);

    uint16_t lbl_w = getTextWidth(label);
    tft.setCursor (fires_btn_b.x+(fires_btn_b.w-lbl_w)/2, fires_btn_b.y+2);
    tft.setTextColor (text_clr);
    tft.print (label);
}

// ---------------------------------------------------------------------------
// flame glyph -- real hand-authored bitmap sprite, same family as aprsicons.h
// ---------------------------------------------------------------------------
//
// Unlike a list-row sprite (APRS icons blit onto a fixed black row background), this glyph sits
// on top of arbitrary map pixels -- ocean, land, cloud -- so a plain rectangular blit would stamp
// a visible box behind it. FIRE_SPRITE_TRANSPARENT is a sentinel value no real flame pixel uses;
// the blit loop below tests each pixel and skips drawing it entirely rather than painting it, so
// the map shows through everywhere outside the flame's actual silhouette.
//
// N.B. per-pixel blit is materially more expensive than the primitive-based glyphs used elsewhere
// in this file's siblings (drawBolt() in lightning.cpp fills with RA8875's hardware circle/triangle
// routines). At FIRE_MAXHOTSPOTS worth of glyphs a frame this adds up -- see the reduced cap below.

#define FIRE_SPRITE_SZ 16
#define FIRE_SPRITE_TRANSPARENT 0xF81F   // magenta sentinel -- never drawn, just means "skip"

static const uint16_t fire_sprite_px[] PROGMEM = {  // 16x16 flame, hand-authored
    0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xFBC0,0xD941,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xFBC0,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xD941,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xFBC0,0xD941,0xD941,0xD941,0xD941,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xFE60,0xFE60,0xD941,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0x0841,0xFBC0,0xFE60,0xFE60,0xFE60,0xD941,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0x0841,0xFBC0,0xFE60,0xFE60,0xFFFF,0xFE60,0xD941,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0x0841,0xFBC0,0xFE60,0xFE60,0xFFFF,0xFFFF,0xFE60,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0x0841,0xFBC0,0xD941,0xD941,0xFE60,0xFE60,0xFE60,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0x0841,0xFBC0,0xD941,0xD941,0xD941,0xFE60,0xFE60,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0x0841,0xFBC0,0xD941,0xD941,0xD941,0xD941,0xD941,0xFBC0,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0x0841,0x0841,0xFBC0,0xD941,0xD941,0xD941,0xFBC0,0x0841,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,
    0xF81F,0xF81F,0xF81F,0xF81F,0x0841,0x0841,0x0841,0xFBC0,0xFBC0,0x0841,0x0841,0x0841,0xF81F,0xF81F,0xF81F,0xF81F,
};

static void drawFlame (int16_t cx, int16_t cy)
{
    int s = tft.SCALESZ;
    if (s < 1) s = 1;

    // replicate each sprite pixel into an sxs block so apparent on-screen size holds steady
    // across build resolutions, same convention as LTG_BOLT_PX * s in drawBolt().
    int16_t half = (int16_t)((FIRE_SPRITE_SZ * s) / 2);

    // Guard: ensure the whole glyph lands within raw map bounds, same pattern as drawBolt().
    uint16_t mx = (uint16_t)(tft.SCALESZ * map_b.x);
    uint16_t my = (uint16_t)(tft.SCALESZ * map_b.y);
    uint16_t mw = (uint16_t)(tft.SCALESZ * map_b.w);
    uint16_t mh = (uint16_t)(tft.SCALESZ * map_b.h);
    if (cx-half < (int16_t)mx || cx+half >= (int16_t)(mx+mw) ||
        cy-half < (int16_t)my || cy+half >= (int16_t)(my+mh))
        return;

    int16_t x0 = cx - half;
    int16_t y0 = cy - half;

    for (int r = 0; r < FIRE_SPRITE_SZ; r++) {
        for (int c = 0; c < FIRE_SPRITE_SZ; c++) {
            uint16_t px = fire_sprite_px[r*FIRE_SPRITE_SZ + c];
            if (px == FIRE_SPRITE_TRANSPARENT)
                continue;                            // skip -- let the map show through here
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++)
                    tft.drawPixelRaw (x0 + c*s + dx, y0 + r*s + dy, px);
        }
    }
}

// ---------------------------------------------------------------------------
// fetch
// ---------------------------------------------------------------------------

static bool fetchFires (void)
{
    FILE *fp = openCachedFile (fires_fn, fires_page, FIRE_INTERVAL, FIRE_MINSIZ);
    if (!fp) {
        Serial.printf ("FIRE: failed to open %s\n", fires_fn);
        n_fires = 0;
        return false;
    }

    n_fires = 0;
    char line[64];
    while (fgets (line, sizeof(line), fp)) {
        float lat, lon, frp;
        if (sscanf (line, "%f,%f,%f", &lat, &lon, &frp) == 3) {
            if (n_fires < FIRE_MAXHOTSPOTS) {
                fire_spots[n_fires].lat = lat;
                fire_spots[n_fires].lng = lon;
                fire_spots[n_fires].frp = frp;
                n_fires++;
            }
        }
    }
    fclose (fp);

    Serial.printf ("FIRE: %d hotspots\n", n_fires);
    return true;    // empty file is valid -- no qualifying fires right now
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

/* restore NV state at startup
 */
void initFires (void)
{
    if (!NVReadUInt8 (NV_FIRES_ON, &fires_on)) {
        fires_on = 0;
        NVWriteUInt8 (NV_FIRES_ON, fires_on);
    }
    fires_next_fetch = 0;
    n_fires = 0;
}

/* call from updateWiFi(), same call site as updateLightning()/checkFireWxData().
 * self-guards on fires_on so it's a no-op whenever the overlay is off, same as updateLightning().
 */
void updateFires (void)
{
    if (!fires_on)
        return;

    time_t t0 = myNow();
    if (t0 < fires_next_fetch)
        return;

    fetchFires();
    fires_next_fetch = t0 + FIRE_INTERVAL;
    scheduleMapRedraw();
}

/* render flame glyphs; call from drawAllSymbols().
 * Only actually draws when firesBadgeVisible() -- same off-map-styles/projections restraint as
 * the badge itself, so glyphs never appear on, say, an azimuthal or DRAP map.
 *
 * Screen-space de-clutter grid, same approach as drawLightningOnMap(): bins projected hotspots
 * into cells sized to the sprite itself, keeps only the highest-FRP hotspot per cell, and draws
 * just the survivors. This is what makes it safe to fetch thousands of hotspots (FIRE_MAXHOTSPOTS)
 * without the per-pixel sprite blit cost scaling with them -- draw count is bounded by how many
 * cells fit on screen, not by how many fires the backend returned.
 */
void drawFiresOnMap (void)
{
    if (!fires_on || !firesBadgeVisible() || n_fires == 0)
        return;

    int s = tft.SCALESZ;
    if (s < 1) s = 1;
    uint16_t mx = (uint16_t)(tft.SCALESZ * map_b.x);
    uint16_t my = (uint16_t)(tft.SCALESZ * map_b.y);
    uint16_t mw = (uint16_t)(tft.SCALESZ * map_b.w);
    uint16_t mh = (uint16_t)(tft.SCALESZ * map_b.h);

    int cell = FIRE_CELL_MULT * FIRE_SPRITE_SZ * s;
    if (cell < 1) cell = 1;
    int  gw    = (int)(mw / cell) + 1;
    int  gh    = (int)(mh / cell) + 1;
    long ncell = (long)gw * (long)gh;

    int    *best = ncell >= 1 ? (int *)   malloc ((size_t)ncell * sizeof(int))    : NULL;
    SCoord *sc   = best      ? (SCoord *) malloc ((size_t)n_fires * sizeof(SCoord)) : NULL;

    if (!best || !sc) {
        // Allocation failed: fall back to drawing every hotspot directly. Rare, and only
        // means a busier-than-ideal frame rather than a crash or missing data.
        free (best); free (sc);
        for (int i = 0; i < n_fires; i++) {
            SCoord s2;
            ll2sRaw (fire_spots[i].lat * (M_PIF / 180.0F), fire_spots[i].lng * (M_PIF / 180.0F), s2, 8);
            if (s2.x == 0 && s2.y == 0) continue;
            if (!overMap (raw2appSCoord (s2))) continue;
            drawFlame ((int16_t)s2.x, (int16_t)s2.y);
        }
        return;
    }

    for (long c = 0; c < ncell; c++) best[c] = -1;

    // Project once; keep the most intense (highest FRP) hotspot per cell.
    for (int i = 0; i < n_fires; i++) {
        ll2sRaw (fire_spots[i].lat * (M_PIF / 180.0F), fire_spots[i].lng * (M_PIF / 180.0F), sc[i], 8);
        if (sc[i].x == 0 && sc[i].y == 0) continue;
        if (sc[i].x < mx || sc[i].x >= mx+mw ||
            sc[i].y < my || sc[i].y >= my+mh) continue;
        if (!overMap (raw2appSCoord (sc[i]))) continue;

        long c = (long)((sc[i].y - my) / cell) * (long)gw
               + (long)((sc[i].x - mx) / cell);
        if (c < 0 || c >= ncell) continue;
        if (best[c] < 0 || fire_spots[i].frp > fire_spots[best[c]].frp)
            best[c] = i;
    }

    for (long c = 0; c < ncell; c++) {
        int i = best[c];
        if (i >= 0)
            drawFlame ((int16_t)sc[i].x, (int16_t)sc[i].y);
    }

    free (best);
    free (sc);
}
