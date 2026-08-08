/* hurricane.cpp -- tropical cyclone tracking overlay for HamClock
 *
 * Fetches active storm data from OHB backend (fetch_cyclones.py) and
 * displays storm tracks on the map and in a scrollable pane.
 *
 * Data format from /storms/storms.txt (one line per track point) -- UNCHANGED, 9 fields:
 *   NAME,BASIN,TYPE,CATEGORY,LAT,LON,WIND_KT,FCST_HOUR,ADVISORY
 *
 * A companion, OPTIONAL sidecar file /storms/storm_ids.txt (one line per storm, not per
 * track point) may also be present:
 *   NAME,BASIN,ATCF_ID
 * eg "FAUSTO,EP,EP062026". It's joined to storms.txt by the (NAME,BASIN) pair -- the same
 * key storms.txt already uses to group a storm's track points -- to recover the storm's real
 * ATCF number for building a reliable Tropical Tidbits link. This file is deliberately kept
 * separate rather than added as a 10th storms.txt column so storms.txt's format never changes
 * for any other consumer; if storm_ids.txt is missing or a storm has no entry in it, the
 * Tropical Tidbits link simply falls back to other heuristics (see stormATCFNumber()) or is
 * omitted for that storm.
 *
 * FCST_HOUR == 0  → current position (drawn as bullseye)
 * FCST_HOUR >  0  → forecast track point (drawn as line + circle)
 *
 * Pane: PLOT_CH_STORMS   (scrollable list, follows DXpeds pattern)
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define STORM_MAXAGE        (60*10)     // cache max age, secs -- poll every 10 min for near-real-time updates
#define STORM_MINSIZ        10          // min acceptable file size (empty = 0 storms)
#define STORM_MAXSTORMS     20          // max simultaneous storms
#define STORM_MAXPTS        12          // max track points per storm (current + forecast)
#define STORM_NAMELEN       12          // max name length including EOS
#define STORM_IDLEN         12          // advisory or storm id length including EOS
#define STORM_TITLE_Y0      PANETITLE_H  // pane title baseline
#define STORM_ENTRY_H       22          // line spacing for the "no storms" message
#define STORM_START_DY      40          // y offset (from box top) to first storm row
#define STORM_ROW_H         50          // total height of one storm row (name + 2 detail lines)
#define STORM_NAME_DY       24          // baseline of large name line within a row (SMALL_FONT)
#define STORM_CAT_DY        28          // top of category/wind line within a row (FAST_FONT)
#define STORM_COORD_DY      40          // top of basin/coords line within a row (FAST_FONT)
#define STORM_TITLE_RSV     28          // reserve at right so title clears the scroll-arrow control
#define STORM_RESET_W       18          // size of the reset-view button on the centered storm's row
static const char storm_page[] = "/storms/storms.txt";
static const char storm_fn[]   = "storms.txt";

// optional sidecar file mapping (NAME,BASIN) -> ATCF_ID; safe to be missing entirely
#define STORMIDS_MAXAGE     (60*10)     // same refresh cadence as storms.txt
#define STORMIDS_MINSIZ     1           // just needs to be non-empty
static const char stormids_page[] = "/storms/storm_ids.txt";
static const char stormids_fn[]   = "storm_ids.txt";

// ---------------------------------------------------------------------------
// Colours (NWS standard tropical cyclone colour convention)
// ---------------------------------------------------------------------------

#define STORM_COLOR_TD      RGB565(120,120,120)   // Tropical Depression
#define STORM_COLOR_TS      RGB565(0,200,200)     // Tropical Storm - cyan
#define STORM_COLOR_CAT1    RGB565(255,220,0)     // Cat 1 - yellow
#define STORM_COLOR_CAT2    RGB565(255,140,0)     // Cat 2 - orange
#define STORM_COLOR_CAT3    RGB565(255,80,0)      // Cat 3 - red-orange
#define STORM_COLOR_CAT4    RGB565(220,40,40)     // Cat 4 - red
#define STORM_COLOR_CAT5    RGB565(200,0,200)     // Cat 5 - magenta
#define STORM_COLOR_TITLE   RA8875_WHITE

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

typedef struct {
    float    lat, lon;          // position decimal degrees
    uint16_t wind_kt;           // max sustained wind knots
    uint16_t fcst_hour;         // 0=current, 12,24,...=forecast
    uint8_t  category;          // 0=TD/TS, 1-5=Saffir-Simpson
    char     type[4];           // TD|TS|HU|TY|TC|EX
} StormPoint;

typedef struct {
    char       name[STORM_NAMELEN]; // e.g. "HELENE"
    char       basin[3];            // AL|EP|CP|WP|IO
    char       advisory[STORM_IDLEN]; // advisory/storm id, if supplied
    char       atcf_id[STORM_IDLEN];  // optional full ATCF id, eg "AL092026", if supplied (10th field)
    uint8_t    peak_cat;            // highest category in track
    uint16_t   peak_wind;           // highest wind speed in track
    LatLong    cur_ll;              // current position (fcst_hour==0)
    StormPoint pts[STORM_MAXPTS];   // track points sorted by fcst_hour
    int        n_pts;               // number of valid track points
} Storm;

static Storm  storms[STORM_MAXSTORMS];
static int    n_storms = 0;
static uint32_t storm_prev_refresh;             // millis() of last fetch attempt; 0 == never fetched.
                                                 // shared by checkStormsData() and updateStorms() so
                                                 // fetching stays on one schedule regardless of which
                                                 // one triggers it.

// map-centering state for the "reset view" button (feature: tap storm centers map, button restores)
static int      storm_centered_idx = -1;        // data index of storm the map is centered on, else -1
static bool     storm_pz_saved = false;         // whether storm_saved_pz holds a pre-focus map view
static PanZoom  storm_saved_pz;                 // map view to restore when the reset button is tapped
static ScrollState storm_ss;

// ---------------------------------------------------------------------------
// Colour lookup
// ---------------------------------------------------------------------------

uint16_t stormCategoryColor (uint8_t cat, uint16_t wind_kt)
{
    if (wind_kt < 34)  return STORM_COLOR_TD;
    if (wind_kt < 64)  return STORM_COLOR_TS;
    switch (cat) {
        case 1: return STORM_COLOR_CAT1;
        case 2: return STORM_COLOR_CAT2;
        case 3: return STORM_COLOR_CAT3;
        case 4: return STORM_COLOR_CAT4;
        case 5: return STORM_COLOR_CAT5;
        default: return STORM_COLOR_TS;
    }
}

static const char *stormCategoryLabel (uint8_t cat, uint16_t wind_kt, const char *type)
{
    static char buf[12];
    if (wind_kt < 34)
        strcpy (buf, "TD");
    else if (wind_kt < 64)
        strcpy (buf, "TS");
    else if (strcmp(type, "TY") == 0)
        snprintf (buf, sizeof(buf), "Ty%d", cat);
    else
        snprintf (buf, sizeof(buf), "Cat%d", cat);
    return buf;
}

// ---------------------------------------------------------------------------
// CSV parser
// ---------------------------------------------------------------------------

/* parse storms.txt into storms[] array. storms.txt format is unchanged -- 9 fields:
 * NAME,BASIN,TYPE,CATEGORY,LAT,LON,WIND_KT,FCST_HOUR,ADVISORY
 * (atcf_id is filled in separately, if available, by applyStormIds() from storm_ids.txt)
 */
static bool parseStormsFile (FILE *fp)
{
    n_storms = 0;
    for (int i = 0; i < STORM_MAXSTORMS; i++)
        storms[i] = Storm{};
    storm_ss.init (STORM_MAXSTORMS, 0, 0, ScrollState::DIR_TOPDOWN);

    char line[200];
    while (fgets (line, sizeof(line), fp)) {

        // skip comment/header lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        // parse CSV fields
        char name[64];
        char basin[8];
        char type[8];
        int  cat;
        float lat, lon;
        int  wind;
        int  fcst_hour;
        char advisory[32];

        int n = sscanf (line, "%63[^,],%7[^,],%7[^,],%d,%f,%f,%d,%d,%31s",
                        name, basin, type, &cat, &lat, &lon,
                        &wind, &fcst_hour, advisory);
        if (n < 8)
            continue;

        // clamp category
        if (cat < 0) cat = 0;
        if (cat > 5) cat = 5;

        // find or create storm entry by name
        Storm *sp = NULL;
        for (int i = 0; i < n_storms; i++) {
            if (strncmp (storms[i].name, name, STORM_NAMELEN-1) == 0) {
                sp = &storms[i];
                break;
            }
        }
        if (!sp) {
            if (n_storms >= STORM_MAXSTORMS)
                continue;
            sp = &storms[n_storms++];
            quietStrncpy (sp->name, name, sizeof(sp->name));
            quietStrncpy (sp->basin, basin, sizeof(sp->basin));
            quietStrncpy (sp->advisory, advisory, sizeof(sp->advisory));
            sp->peak_cat  = 0;
            sp->peak_wind = 0;
            sp->n_pts     = 0;
        }

        // update peak intensity
        if ((uint16_t)wind > sp->peak_wind) {
            sp->peak_wind = (uint16_t)wind;
            sp->peak_cat  = (uint8_t)cat;
        }

        // store track point
        if (sp->n_pts < STORM_MAXPTS) {
            StormPoint &pt = sp->pts[sp->n_pts++];
            pt.lat      = lat;
            pt.lon      = lon;
            pt.wind_kt  = (uint16_t)wind;
            pt.fcst_hour = (uint16_t)fcst_hour;
            pt.category = (uint8_t)cat;
            quietStrncpy (pt.type, type, sizeof(pt.type));

            // record current position (fcst_hour==0)
            if (fcst_hour == 0) {
                sp->cur_ll = LatLong (lat, lon);
                if (advisory[0])
                    quietStrncpy (sp->advisory, advisory, sizeof(sp->advisory));
            }
        }
    }

    // Sort by peak wind ascending (weakest first). The ScrollState DIR_TOPDOWN model puts the
    // highest array index on the top display row, so ascending order shows the strongest storm on top.
    for (int i = 0; i < n_storms-1; i++) {
        for (int j = i+1; j < n_storms; j++) {
            if (storms[j].peak_wind < storms[i].peak_wind) {
                Storm tmp = storms[i];
                storms[i] = storms[j];
                storms[j] = tmp;
            }
        }
    }

    storm_ss.n_data = n_storms;
    storm_ss.scrollToNewest();          // position list so the first full window is shown from the top
    Serial.printf ("STORM: parsed %d storms\n", n_storms);
    return true;
}

// ---------------------------------------------------------------------------
// Pane drawing
// ---------------------------------------------------------------------------

/* geometry of the reset-view button within a storm row whose top is at row_y */
static SBox stormResetBox (const SBox &box, uint16_t row_y)
{
    SBox b;
    b.w = STORM_RESET_W;
    b.h = STORM_RESET_W;
    b.x = box.x + box.w - STORM_RESET_W - 3;
    b.y = row_y + 3;
    return b;
}

/* draw a small "restore previous map view" button (refresh-style icon) */
static void drawStormResetButton (const SBox &b)
{
    tft.fillRect (b.x, b.y, b.w, b.h, RA8875_BLACK);
    tft.drawRect (b.x, b.y, b.w, b.h, GRAY);

    uint16_t cx = b.x + b.w/2;
    uint16_t cy = b.y + b.h/2;
    int rr = b.w/2 - 4;
    if (rr < 2)
        rr = 2;
    tft.drawCircle (cx, cy, rr, RA8875_WHITE);                  // ring
    tft.fillRect (cx+rr-1, cy-1, 3, 3, RA8875_BLACK);           // gap in the ring
    tft.fillTriangle (cx+rr-2, cy-rr+1, cx+rr+2, cy-rr+1,
                      cx+rr,   cy-rr+4, RA8875_WHITE);           // arrowhead -> refresh/reset
}

static void drawStormsPane (const SBox &box)
{
    prepPlotBox (box);

    // title -- centered, but kept clear of the scroll-arrow control in the top-right corner.
    // That control always erases a band at the right edge (even when the arrows are inactive),
    // so if a full-width centered title would reach under it, shift the title left to clear it.
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (STORM_COLOR_TITLE);
    const char *title = "Trop Wx";
    uint16_t tw = getTextWidth (title);
    uint16_t avail_r = box.w > STORM_TITLE_RSV ? box.w - STORM_TITLE_RSV : box.w;  // right limit
    uint16_t tx = box.w > tw ? (box.w - tw)/2 : 2;                                 // ideal centered x
    if (tx + tw > avail_r)                                                         // would hit arrows
        tx = avail_r > tw ? avail_r - tw : 2;                                      // shift left to clear
    tft.setCursor (box.x + tx, box.y + STORM_TITLE_Y0);
    tft.print (title);

    // how many storm rows actually fit below the title; keep scroll state in sync so the
    // scroll arrows and getVisDataIndices() reflect the real visible count (not STORM_MAXSTORMS)
    int max_vis = (int)(box.h - STORM_START_DY) / STORM_ROW_H;
    if (max_vis < 1)
        max_vis = 1;
    storm_ss.max_vis = max_vis;
    storm_ss.n_data  = n_storms;

    // scroll arrows -- title is centered in the space left of this control
    storm_ss.drawScrollUpControl (box, STORM_COLOR_TITLE, STORM_COLOR_TITLE);
    storm_ss.drawScrollDownControl (box, STORM_COLOR_TITLE, STORM_COLOR_TITLE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);

    if (n_storms == 0) {
        // off season or no data
        tft.setTextColor (STORM_COLOR_TD);
        const char *msg = "No active";
        const char *msg2 = "storms";
        uint16_t w1 = getTextWidth (msg);
        uint16_t w2 = getTextWidth (msg2);
        uint16_t ymid = box.y + box.h/2 - STORM_ENTRY_H/2;
        tft.setCursor (box.x + (box.w-w1)/2, ymid);
        tft.print (msg);
        tft.setCursor (box.x + (box.w-w2)/2, ymid + STORM_ENTRY_H);
        tft.print (msg2);
        return;
    }

    // draw each visible storm
    uint16_t y0 = box.y + STORM_START_DY;
    int min_i, max_i;
    if (storm_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            const Storm &st = storms[i];
            int r = storm_ss.getDisplayRow (i);
            uint16_t y = y0 + r * STORM_ROW_H;          // top of this row
            uint16_t color = stormCategoryColor (st.peak_cat, st.peak_wind);

            // clear row and draw separator beneath it
            tft.fillRect (box.x+1, y, box.w-2, STORM_ROW_H, RA8875_BLACK);
            tft.drawLine (box.x+1, y + STORM_ROW_H - 1,
                          box.x + box.w-2, y + STORM_ROW_H - 1, 1, GRAY);

            // line 1: storm name, large for legibility (SMALL_FONT baseline)
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (color);
            uint16_t nw = getTextWidth (st.name);
            tft.setCursor (box.x + (box.w > nw ? (box.w-nw)/2 : 1), y + STORM_NAME_DY);
            tft.print (st.name);

            // line 2: category + peak wind (FAST_FONT top-left)
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            char line2[24];
            snprintf (line2, sizeof(line2), "%s  %d kt",
                      stormCategoryLabel (st.peak_cat, st.peak_wind, st.pts[0].type),
                      st.peak_wind);
            uint16_t w2 = getTextWidth (line2);
            tft.setCursor (box.x + (box.w > w2 ? (box.w-w2)/2 : 1), y + STORM_CAT_DY);
            tft.print (line2);

            // line 3: basin + current coordinates (FAST_FONT top-left)
            tft.setTextColor (GRAY);
            char line3[28];
            snprintf (line3, sizeof(line3), "%s %.1f%c %.1f%c",
                      st.basin,
                      fabsf(st.cur_ll.lat_d), st.cur_ll.lat_d >= 0 ? 'N' : 'S',
                      fabsf(st.cur_ll.lng_d), st.cur_ll.lng_d >= 0 ? 'E' : 'W');
            uint16_t w3 = getTextWidth (line3);
            tft.setCursor (box.x + (box.w > w3 ? (box.w-w3)/2 : 1), y + STORM_COORD_DY);
            tft.print (line3);

            // reset-view button on the storm the map is currently centered on
            if (storm_pz_saved && i == storm_centered_idx)
                drawStormResetButton (stormResetBox (box, y));
        }
    }
}

// ---------------------------------------------------------------------------
// Map drawing
// ---------------------------------------------------------------------------

/* Common size unit for all storm map markers, in raw (framebuffer) pixels.
 * Scales with display resolution (SCALESZ) and grows modestly with map zoom so the
 * storm stays easy to see when zoomed in (e.g. after tapping a storm zooms to MAX_ZOOM).
 */
static int stormSizeUnit()
{
    int scale = std::max (1, (int)tft.SCALESZ);
    return scale * (5 + 2 * (int)pan_zoom.zoom);    // zoom 1->7, 2->9, 3->11, 4->13 per SCALESZ
}

static int stormTrackWidth()
{
    return std::max (2, stormSizeUnit() / 3);
}

/* radius of the small category-colored dot drawn at each track position */
static int stormDotRadius()
{
    return std::max (2, stormSizeUnit() / 2);
}

/* draw a small category-colored dot at a track position; current==true marks the live position */
static void drawStormDot (const LatLong &ll, uint16_t color, bool current)
{
    SCoord s;
    ll2s (ll, s, 1);
    if (!overMap(s))
        return;

    ll2sRaw (ll, s, 1);
    int r = stormDotRadius();
    if (!rawPointClearOfMapEdge (s, r))          // dot's edge, not just its center, must stay on-map
        return;
    tft.fillCircleRaw (s.x, s.y, r, color);
    tft.drawCircleRaw (s.x, s.y, r, RA8875_BLACK);      // thin edge for contrast over the map
    if (current)
        tft.drawPixelRaw (s.x, s.y, RA8875_WHITE);      // mark the storm's current position
}

/* return destination point given start (lat1/lon1, radians), true bearing theta (radians,
 * standard compass convention: 0=north, clockwise positive, matching propPath()'s bearing),
 * and angular distance delta (radians). Standard spherical "direct geodesic" formula.
 */
static void destPoint (float lat1, float lon1, float theta, float delta, float *lat2, float *lon2)
{
    float s = CLAMPF (sinf(lat1)*cosf(delta) + cosf(lat1)*sinf(delta)*cosf(theta), -1, 1);
    *lat2 = asinf (s);
    *lon2 = lon1 + atan2f (sinf(theta)*sinf(delta)*cosf(lat1), cosf(delta) - sinf(lat1)*sinf(*lat2));
}

/* draw a dramatic direction indicator at the last forecast track point:
 *   - an extension line starting at the last dot's edge
 *   - straight if only two track points are available
 *   - curved (continuing the arc of the last two segments) if three or more
 *     points are available; total curvature is capped at 90 degrees so the
 *     arrow never spirals back on itself
 *   - a filled arrowhead at the far end of the extension line
 *
 * Direction and curvature are derived from actual geographic bearings (great-circle, via
 * propPath()) rather than by differencing raw on-screen pixel positions of the real track
 * points. At low zoom, consecutive forecast points -- often just a few hours apart -- can
 * land on the very same raw pixel, or just 1-2 pixels apart; differencing such tiny, heavily
 * quantized pixel deltas produced noisy/wrong directions and made the arrow curl into a shape
 * that didn't match the real forecast track, visibly different from the same storm zoomed in
 * further. Bearings computed straight from lat/lon are exact regardless of zoom, so a fixed,
 * generously-sized synthetic probe point along that true bearing (still run back through
 * ll2sRaw(), so it still reflects this map's actual on-screen orientation) gives a reliable,
 * non-quantized screen direction. The rest of the geometry (extension line, arrowhead) is
 * still done in raw screen pixels after ll2sRaw() conversion.
 */
static void drawStormDirectionArrow (const Storm &st)
{
    if (st.n_pts < 2)
        return;

    // --- last two track points ---
    const StormPoint &p_prev = st.pts[st.n_pts - 2];
    const StormPoint &p_last = st.pts[st.n_pts - 1];

    LatLong ll_prev (p_prev.lat, p_prev.lon);
    LatLong ll_last (p_last.lat, p_last.lon);

    SCoord sc_last;
    ll2s (ll_last, sc_last, 1);
    if (!overMap(sc_last))
        return;
    ll2sRaw (ll_last, sc_last, 1);

    // true bearing of the last track segment
    float bear_last, dist_unused;
    propPath (false, ll_prev, sinf(ll_prev.lat), cosf(ll_prev.lat), ll_last, &dist_unused, &bear_last);

    // project a synthetic probe point a fixed, generous geographic distance along that
    // bearing, then convert *that* through ll2sRaw() to get a reliable on-screen unit vector
    #define DIR_PROBE_DEG 3.0F              // far enough to resolve cleanly even at min zoom
    float plat, plon;
    destPoint (ll_last.lat, ll_last.lng, bear_last, deg2rad(DIR_PROBE_DEG), &plat, &plon);
    LatLong ll_probe (rad2deg(plat), rad2deg(plon));
    SCoord sc_probe;
    ll2sRaw (ll_probe, sc_probe, 1);

    float dx = (float)((int)sc_probe.x - (int)sc_last.x);
    float dy = (float)((int)sc_probe.y - (int)sc_last.y);
    float seg_len = sqrtf (dx*dx + dy*dy);
    if (seg_len < 0.5f)
        return;
    dx /= seg_len;
    dy /= seg_len;

    // --- derive per-pixel angular turn rate from the penultimate segment, same bearing-based
    // approach as above; positive = clockwise in screen coords (Y-down), negative = ccw ---
    float turn_rate = 0.0f;
    if (st.n_pts >= 3) {
        const StormPoint &p_prev2 = st.pts[st.n_pts - 3];
        LatLong ll_prev2 (p_prev2.lat, p_prev2.lon);
        float bear_prev, dist_unused2;
        propPath (false, ll_prev2, sinf(ll_prev2.lat), cosf(ll_prev2.lat), ll_prev,
                                                                        &dist_unused2, &bear_prev);

        // signed turn between the two true bearings, wrapped to -pi..pi
        float angle_change = fmodf (bear_last - bear_prev + 3*M_PIF, 2*M_PIF) - M_PIF;

        turn_rate = angle_change / seg_len;    // radians per screen-pixel (of the probe segment)
        // cap total curvature at 90 degrees so the arrow cannot curl back
        float ext_f  = (float)(stormSizeUnit() * 3);
        float max_tr = 1.5707963f / ext_f;     // pi/2 spread over extension
        if (turn_rate >  max_tr) turn_rate =  max_tr;
        if (turn_rate < -max_tr) turn_rate = -max_tr;
    }
    #undef DIR_PROBE_DEG

    // --- draw the extension line from the last dot's edge ---
    int ext_len  = stormSizeUnit() * 3;     // extension length in screen pixels
    int lw       = stormTrackWidth();
    int ow       = lw + 2;                  // outline width, 1px black border each side
    int step_px  = std::max (2, lw);        // one draw-call per step_px pixels
    int r        = stormDotRadius();
    uint16_t color = stormCategoryColor (p_last.category, p_last.wind_kt);

    // precompute the per-step rotation (constant throughout the extension)
    float angle_step = turn_rate * (float)step_px;
    float cos_a = cosf (angle_step);
    float sin_a = sinf (angle_step);

    float cur_x  = sc_last.x + dx * (float)r;  // start just past the last dot
    float cur_y  = sc_last.y + dy * (float)r;
    float cur_dx = dx;
    float cur_dy = dy;

    // Walk the curve once to collect its points, then draw in two full passes below (all
    // black, then all color) rather than alternating black/color segment-by-segment. Each
    // straight segment has flat end caps at a slightly different angle than its neighbor
    // (the curve bends a little every step), so outlining segments individually left a
    // visible black seam/notch at every joint. Drawing the whole black path first -- with a
    // small round black "joint patch" at each interior point to fill those notches -- then
    // the whole color path on top the same way gives one smooth continuous outlined curve
    // instead of a chain of separately-outlined segments.
    #define MAX_EXT_PTS 64                  // ext_len is a small fixed multiple of stormSizeUnit()
    float pts_x[MAX_EXT_PTS], pts_y[MAX_EXT_PTS];
    int n_ext_pts = 0;
    pts_x[n_ext_pts] = cur_x;
    pts_y[n_ext_pts] = cur_y;
    n_ext_pts++;

    for (int drawn = 0; drawn < ext_len && n_ext_pts < MAX_EXT_PTS; drawn += step_px) {
        float nx = cur_x + cur_dx * (float)step_px;
        float ny = cur_y + cur_dy * (float)step_px;
        // stop extending once the next step would poke past the map's own edge -- this
        // extrapolates well beyond the last known (on-map) track point with no natural
        // limit otherwise, and was bleeding into whatever sits just outside the map
        // (e.g. the DE/DX info panes) when a storm's last point was near the map edge
        SCoord nend = {(uint16_t)(int32_t)nx, (uint16_t)(int32_t)ny};
        if (!rawPointClearOfMapEdge (nend, ow/2+1))
            break;
        pts_x[n_ext_pts] = nx;
        pts_y[n_ext_pts] = ny;
        n_ext_pts++;
        cur_x = nx;
        cur_y = ny;
        // rotate direction for the next step; renormalize to prevent float drift
        float ndx  =  cur_dx * cos_a - cur_dy * sin_a;
        float ndy  =  cur_dx * sin_a + cur_dy * cos_a;
        float nlen = sqrtf (ndx*ndx + ndy*ndy);
        if (nlen > 0.01f) { cur_dx = ndx / nlen;  cur_dy = ndy / nlen; }
    }

    // pass 1: black outline, wide, with round joints
    for (int i = 1; i < n_ext_pts; i++) {
        if (i > 1)
            tft.fillCircleRaw ((int16_t)pts_x[i-1], (int16_t)pts_y[i-1], ow/2, RA8875_BLACK);
        tft.drawLineRaw ((int16_t)pts_x[i-1], (int16_t)pts_y[i-1],
                         (int16_t)pts_x[i],   (int16_t)pts_y[i], ow, RA8875_BLACK);
    }
    // pass 2: category color on top, narrower, with round joints of its own
    for (int i = 1; i < n_ext_pts; i++) {
        if (i > 1)
            tft.fillCircleRaw ((int16_t)pts_x[i-1], (int16_t)pts_y[i-1], lw/2, color);
        tft.drawLineRaw ((int16_t)pts_x[i-1], (int16_t)pts_y[i-1],
                         (int16_t)pts_x[i],   (int16_t)pts_y[i], lw, color);
    }
    #undef MAX_EXT_PTS

    // --- arrowhead at the end of the extension, in the final travel direction ---
    int alen = r * 2 + lw;   // arrowhead height
    int awid = r + 1;        // arrowhead half-width at base
    float perp_x = -cur_dy;
    float perp_y =  cur_dx;

    SCoord a_tip   = {(uint16_t)(int32_t)(cur_x + cur_dx * alen), (uint16_t)(int32_t)(cur_y + cur_dy * alen)};
    SCoord a_left  = {(uint16_t)(int32_t)(cur_x + perp_x * awid), (uint16_t)(int32_t)(cur_y + perp_y * awid)};
    SCoord a_right = {(uint16_t)(int32_t)(cur_x - perp_x * awid), (uint16_t)(int32_t)(cur_y - perp_y * awid)};

    // skip the arrowhead entirely rather than draw a partial/clipped triangle if any vertex
    // would land off the map (e.g. the extension line above got cut short right at the edge)
    if (!rawPointClearOfMapEdge (a_tip, 0) || !rawPointClearOfMapEdge (a_left, 0)
                                            || !rawPointClearOfMapEdge (a_right, 0))
        return;

    tft.fillTriangleRaw (
        (int16_t)(cur_x + cur_dx * alen),              // tip
        (int16_t)(cur_y + cur_dy * alen),
        (int16_t)(cur_x + perp_x * awid),              // left base wing
        (int16_t)(cur_y + perp_y * awid),
        (int16_t)(cur_x - perp_x * awid),              // right base wing
        (int16_t)(cur_y - perp_y * awid),
        color);
    tft.drawTriangleRaw (
        (int16_t)(cur_x + cur_dx * alen),
        (int16_t)(cur_y + cur_dy * alen),
        (int16_t)(cur_x + perp_x * awid),
        (int16_t)(cur_y + perp_y * awid),
        (int16_t)(cur_x - perp_x * awid),
        (int16_t)(cur_y - perp_y * awid),
        RA8875_BLACK);
}

/* draw all active storm tracks on the map.
 * called from ESPHamClock.cpp after drawDXPedsOnMap().
 */
void drawStormsOnMap (void)
{
    if (n_storms == 0)
        return;

    // only draw if Storms pane is active
    PlotPane pp = findPaneForChoice (PLOT_CH_STORMS);
    if (pp == PANE_NONE)
        return;

    for (int i = 0; i < n_storms; i++) {
        const Storm &st = storms[i];
        if (st.n_pts == 0)
            continue;

        // 1. forecast track center line, category-colored
        for (int j = 1; j < st.n_pts; j++) {
            const StormPoint &p0 = st.pts[j-1];
            const StormPoint &p1 = st.pts[j];

            LatLong ll0 (p0.lat, p0.lon);
            LatLong ll1 (p1.lat, p1.lon);

            SCoord a, b;
            ll2sRaw (ll0, a, 1);
            ll2sRaw (ll1, b, 1);
            int tw = stormTrackWidth();
            int ow = tw + 2;                     // outline width, 1px black border each side
            // segmentSpanOkRaw() covers everything the old ad-hoc overMap()+edge check did,
            // plus the one it was missing: in azimuthal projection, consecutive track points
            // can legitimately land in *different* hemisphere lobes (a storm crossing to the
            // far side of the world from DE). Connecting those two points drew a straight
            // line straight across the void between the two lobes -- segmentSpanOkRaw()
            // rejects exactly that case, same as every other path/track drawn on this map.
            if (!segmentSpanOkRaw (a, b, ow/2+1))
                continue;
            uint16_t color = stormCategoryColor (p0.category, p0.wind_kt);
            tft.drawLineRaw (a.x, a.y, b.x, b.y, ow, RA8875_BLACK);  // black border first, wider
            tft.drawLineRaw (a.x, a.y, b.x, b.y, tw, color);         // category color on top
        }

        // 2. category-colored dot at each position; the current position is flagged
        for (int j = 0; j < st.n_pts; j++) {
            const StormPoint &pt = st.pts[j];
            LatLong ll (pt.lat, pt.lon);
            uint16_t color = stormCategoryColor (pt.category, pt.wind_kt);
            drawStormDot (ll, color, pt.fcst_hour == 0);
        }

        // 3. directional arrow at the last forecast track point
        drawStormDirectionArrow (st);
    }
}

// ---------------------------------------------------------------------------
// Hover detection
// ---------------------------------------------------------------------------

/* return storm name if cursor is within ~200km of a storm's current position,
 * else NULL. Sets *closest if found.
 * follows getNearestCity() pattern.
 */
const char *getNearestStorm (const LatLong &ll, Storm **closest)
{
    if (n_storms == 0)
        return NULL;

    // threshold in degrees (~200km), tighter at higher zoom
    float thresh = 3.0f / pan_zoom.zoom;
    float best_dist = thresh;
    Storm *best = NULL;

    for (int i = 0; i < n_storms; i++) {
        float dlat = ll.lat_d - storms[i].cur_ll.lat_d;
        float dlng = ll.lng_d - storms[i].cur_ll.lng_d;
        float dist = sqrtf (dlat*dlat + dlng*dlng);
        if (dist < best_dist) {
            best_dist = dist;
            best = &storms[i];
        }
    }

    if (best) {
        if (closest) *closest = best;
        return best->name;
    }
    return NULL;
}

/* return hover label string for a storm, or NULL if no storm nearby.
 * caller uses this to draw a label near the cursor.
 */
const char *getStormHoverLabel (const LatLong &ll)
{
    static char buf[40];
    Storm *st = NULL;
    const char *name = getNearestStorm (ll, &st);
    if (!name || !st)
        return NULL;
    snprintf (buf, sizeof(buf), "%s %s %dkt",
              name,
              stormCategoryLabel (st->peak_cat, st->peak_wind, st->pts[0].type),
              st->peak_wind);
    return buf;
}

/* true if there is at least one active storm (used to skip an empty pane in rotation) */
bool stormsActive (void)
{
    return n_storms > 0;
}

/* If ms is hovering over a storm row in the Storms pane, return that storm's current position in
 * *ll plus a short label, and return true. Lets infobox highlight the storm on the map the same
 * way it does when hovering over a station listing.
 */
bool getStormPaneHover (const SCoord &ms, LatLong *ll, char *label, size_t label_len)
{
    if (n_storms == 0)
        return false;

    PlotPane pp = findPaneChoiceNow (PLOT_CH_STORMS);
    if (pp == PANE_NONE)
        return false;
    const SBox &box = plot_b[pp];
    if (!inBox (ms, box) || ms.y < box.y + STORM_START_DY)      // not over the storm-rows region
        return false;

    int item = (ms.y - box.y - STORM_START_DY) / STORM_ROW_H;
    int index;
    if (!storm_ss.findDataIndex (item, index) || index < 0 || index >= n_storms)
        return false;

    const Storm &st = storms[index];
    *ll = st.cur_ll;
    const StormPoint &cur = st.pts[0];
    snprintf (label, label_len, "%s  %s  %d kt", st.name,
              stormCategoryLabel (cur.category, cur.wind_kt, cur.type), cur.wind_kt);
    return true;
}


static const char *stormBasinName (const char *basin)
{
    if (!strcmp (basin, "AL")) return "Atlantic Basin";
    if (!strcmp (basin, "EP")) return "E Pacific Basin";
    if (!strcmp (basin, "CP")) return "C Pacific Basin";
    if (!strcmp (basin, "WP")) return "W Pacific Basin";
    if (!strcmp (basin, "IO")) return "Indian Ocean";
    if (!strcmp (basin, "SH")) return "S Hemisphere";
    return "Tropical Basin";
}

static const char *stormCompass16 (float bearing_d)
{
    static const char *dirs[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int i = (int)floorf ((bearing_d + 11.25F)/22.5F) & 15;
    return dirs[i];
}

static bool stormMotionString (const Storm &st, char *buf, size_t buflen)
{
    if (st.n_pts < 2 || st.pts[0].fcst_hour != 0 || st.pts[1].fcst_hour == 0) {
        snprintf (buf, buflen, "Motion unavailable");
        return false;
    }

    float lat1 = deg2rad (st.pts[0].lat);
    float lat2 = deg2rad (st.pts[1].lat);
    float dlat = deg2rad (st.pts[1].lat - st.pts[0].lat);
    float dlon = deg2rad (st.pts[1].lon - st.pts[0].lon);

    float h = sinf(dlat/2)*sinf(dlat/2) + cosf(lat1)*cosf(lat2)*sinf(dlon/2)*sinf(dlon/2);
    float dist_rad = 2 * atan2f (sqrtf(h), sqrtf(1-h));
    float dist_nm = (dist_rad * ERAD_M) / 1.15078F;
    float hours = st.pts[1].fcst_hour - st.pts[0].fcst_hour;
    float speed_kt = hours > 0 ? dist_nm / hours : 0;
    float y = sinf (dlon) * cosf (lat2);
    float x = cosf (lat1) * sinf (lat2) - sinf (lat1) * cosf (lat2) * cosf (dlon);
    float bearing_d = fmodf (rad2deg (atan2f (y, x)) + 360.0F, 360.0F);

    snprintf (buf, buflen, "Moving %s @ %.0f kt", stormCompass16 (bearing_d), speed_kt);
    return true;
}

/* map a 2-letter NHC/JTWC basin code to the single-letter ATCF basin suffix used by Tropical
 * Tidbits region codes, e.g. AL -> L, EP -> E.
 */
static char stormATCFBasinLetter (const char *basin)
{
    if (!strcmp (basin, "AL")) return 'L';
    if (!strcmp (basin, "EP")) return 'E';
    if (!strcmp (basin, "CP")) return 'C';
    if (!strcmp (basin, "WP")) return 'W';
    if (!strcmp (basin, "IO")) return 'A';
    if (!strcmp (basin, "SH")) return 'S';
    return 0;
}

/* map a spelled-out placeholder name (used by NHC for still-unnamed Atlantic/EPac depressions,
 * eg "TWO") to its numeric ATCF storm number, or 0 if name isn't one of these.
 */
static int stormNumberFromName (const char *name)
{
    static const char *numwords[] = {
        "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE", "TEN",
        "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN", "SEVENTEEN", "EIGHTEEN",
        "NINETEEN", "TWENTY", "TWENTYONE", "TWENTYTWO", "TWENTYTHREE", "TWENTYFOUR", "TWENTYFIVE",
        "TWENTYSIX", "TWENTYSEVEN", "TWENTYEIGHT", "TWENTYNINE", "THIRTY"
    };
    for (unsigned i = 0; i < NARRAY(numwords); i++)
        if (!strcasecmp (name, numwords[i]))
            return (int)i;
    return 0;
}

/* try to determine st's 2-digit ATCF storm number, filling num_buf[3] with it zero-padded (eg "02")
 * and returning true, else returning false if no number could be reliably determined. Deliberately
 * conservative -- a wrong region code sends the user to the wrong storm's satellite loop, so this
 * only trusts sources that are unambiguous, checked in order of reliability:
 *   1. the storm's atcf_id field (the optional 10th CSV column), eg "AL092026" -- if present this
 *      is the authoritative ATCF id and its 2-digit storm number (chars 2-3) is used directly.
 *   2. the advisory id IS the number+year, ie exactly 6 digits "NNYYYY" with a plausible year --
 *      this intentionally excludes other advisory formats, eg a bare synoptic timestamp like
 *      "2026071918", which could otherwise be misread as a number.
 *   3. the advisory id is already a bare region code, eg "02L".
 *   4. the storm's name is one of the placeholder names ("ONE", "TWO", ...) NHC uses for a still
 *      unnamed depression -- for these the ATCF number is exactly the spelled-out value.
 * a storm with a proper name (eg "FAUSTO") and neither an atcf_id nor one of the advisory formats
 * above simply has no reliably-known ATCF number available to this parser and returns false.
 */
static bool stormATCFNumber (const Storm &st, char num_buf[3])
{
    // 1. explicit atcf_id field, eg "AL092026" -- basin (2 letters) + number (2 digits) + year
    size_t id_len = strlen (st.atcf_id);
    if (id_len >= 4 && isalpha ((unsigned char)st.atcf_id[0]) && isalpha ((unsigned char)st.atcf_id[1])
                     && isdigit ((unsigned char)st.atcf_id[2]) && isdigit ((unsigned char)st.atcf_id[3])) {
        num_buf[0] = st.atcf_id[2];
        num_buf[1] = st.atcf_id[3];
        num_buf[2] = '\0';
        return true;
    }

    const char *advisory = st.advisory;
    size_t len = strlen (advisory);

    // 2. advisory id looks unambiguously like NNYYYY
    if (len == 6) {
        bool all_digits = true;
        for (size_t i = 0; i < len; i++)
            if (!isdigit ((unsigned char)advisory[i])) { all_digits = false; break; }
        if (all_digits) {
            int year = atoi (advisory+2);
            if (year >= 1990 && year <= 2199) {
                num_buf[0] = advisory[0];
                num_buf[1] = advisory[1];
                num_buf[2] = '\0';
                return true;
            }
        }
    }

    // 3. advisory id is already a bare region code, eg "02L"
    if (len == 3 && isdigit ((unsigned char)advisory[0]) && isdigit ((unsigned char)advisory[1])
                 && isalpha ((unsigned char)advisory[2])) {
        num_buf[0] = advisory[0];
        num_buf[1] = advisory[1];
        num_buf[2] = '\0';
        return true;
    }

    // 4. placeholder numeric name for a still-unnamed depression, eg "TWO"
    int n = stormNumberFromName (st.name);
    if (n > 0) {
        snprintf (num_buf, 3, "%02d", n);
        return true;
    }

    return false;
}

/* build the Tropical Tidbits satlooper URL for the storm nearest ll, eg:
 *   https://www.tropicaltidbits.com/sat/satlooper.php?region=02L&product=truecolor
 * returns false (leaving url untouched) if no storm is nearby or no ATCF number can be reliably
 * determined for it -- see stormATCFNumber().
 */
bool getStormTropicalTidbitsURL (const LatLong &ll, char *url, size_t url_len)
{
    Storm *st = NULL;
    if (!getNearestStorm (ll, &st) || !st)
        return false;

    char basin_letter = stormATCFBasinLetter (st->basin);
    if (!basin_letter)
        return false;

    char num_buf[3];
    if (!stormATCFNumber (*st, num_buf))
        return false;

    snprintf (url, url_len, "https://www.tropicaltidbits.com/sat/satlooper.php?region=%s%c&product=truecolor",
               num_buf, basin_letter);
    return true;
}

bool getStormMapMenuInfo (const LatLong &ll, char *line1, size_t line1_len,
        char *line2, size_t line2_len, char *line3, size_t line3_len)
{
    Storm *st = NULL;
    if (!getNearestStorm (ll, &st) || !st || st->n_pts == 0)
        return false;

    const StormPoint &cur = st->pts[0];
    snprintf (line1, line1_len, "%s  %s  %d kt", st->name,
              stormCategoryLabel (cur.category, cur.wind_kt, cur.type), cur.wind_kt);

    if (st->advisory[0])
        snprintf (line2, line2_len, "%s  %s", stormBasinName (st->basin), st->advisory);
    else
        snprintf (line2, line2_len, "%s  %s", stormBasinName (st->basin), st->basin);

    stormMotionString (*st, line3, line3_len);
    return true;
}

// ---------------------------------------------------------------------------
// Data fetch and update
// ---------------------------------------------------------------------------

/* parse storm_ids.txt (one "NAME,BASIN,ATCF_ID" line per storm) and fill in atcf_id for any
 * already-parsed entry in storms[] whose name+basin matches. Tolerant of a missing, empty, or
 * malformed file -- storms[] atcf_id fields simply stay blank in that case and
 * stormATCFNumber() falls back to its other heuristics.
 */
static void applyStormIds (FILE *fp)
{
    char line[100];
    while (fgets (line, sizeof(line), fp)) {

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char name[64];
        char basin[8];
        char atcf_id[32];
        if (sscanf (line, "%63[^,],%7[^,],%31[^,\n\r]", name, basin, atcf_id) != 3)
            continue;

        for (int i = 0; i < n_storms; i++) {
            if (strncmp (storms[i].name, name, STORM_NAMELEN-1) == 0
                        && strncmp (storms[i].basin, basin, sizeof(storms[i].basin)-1) == 0) {
                quietStrncpy (storms[i].atcf_id, atcf_id, sizeof(storms[i].atcf_id));
                break;
            }
        }
    }
}

/* fetch and apply the optional storm_ids.txt sidecar file, if the backend provides one.
 * must be called AFTER storms[] has already been populated by parseStormsFile() since it
 * matches into existing entries by name+basin. Not finding this file is not an error --
 * plenty of backends won't have it -- so this never affects the return value of retrieveStorms().
 */
static void retrieveStormIds (void)
{
    FILE *fp = openCachedFile (stormids_fn, stormids_page, STORMIDS_MAXAGE, STORMIDS_MINSIZ);
    if (!fp)
        return;                          // fine -- backend doesn't provide this file (yet)
    applyStormIds (fp);
    fclose (fp);
}

/* retrieve storms file from OHB backend and parse it into storms[]/n_storms.
 * N.B. does not touch the display -- safe to call whether or not the Storms pane is shown.
 */
static bool retrieveStorms (void)
{
    FILE *fp = openCachedFile (storm_fn, storm_page, STORM_MAXAGE, STORM_MINSIZ);
    if (!fp) {
        Serial.printf ("STORM: failed to open %s\n", storm_fn);
        return false;
    }

    bool ok = parseStormsFile (fp);
    fclose (fp);

    if (!ok)
        Serial.printf ("STORM: parse failed\n");
    else
        retrieveStormIds ();            // best-effort; fills in atcf_id where available

    return ok;
}


/* fetch/parse storm data if due, independent of whether the Storms pane is currently displayed
 * anywhere. This must run whenever Storms is selected in ANY pane's rotation set -- not just when
 * it happens to be the pane actively shown -- otherwise getNextRotationChoice()'s stormsActive()
 * gate can never be satisfied (n_storms starts at 0 and nothing else ever fetches it), permanently
 * locking Storms out of a multi-choice rotation. Called unconditionally from wifi.cpp updateWiFi(),
 * same as updateLightning() -- self-guards below like updateLightning() guards on lightning_on.
 * return whether the current storm data is valid (true also when not in use, or a fetch wasn't due).
 */
bool checkStormsData (void)
{
    if (findPaneForChoice (PLOT_CH_STORMS) == PANE_NONE)
        return true;                                     // not selected in any pane -- nothing to do

    if (storm_prev_refresh != 0 && !timesUp (&storm_prev_refresh, (uint32_t)STORM_MAXAGE * 1000))
        return true;                                     // not due yet, existing data still good

    storm_prev_refresh = millis();
    return retrieveStorms ();
}

/* update the storms pane.
 * called from wifi.cpp updateWiFi() PLOT_CH_STORMS case.
 */
bool updateStorms (const SBox &box, bool fresh)
{
    bool ok;

    if (fresh) {
        // pane was just rotated/switched into view -- fetch now regardless of schedule
        storm_prev_refresh = millis();
        ok = retrieveStorms ();
    } else {
        ok = checkStormsData ();
    }

    if (ok) {
        if (fresh)
            drawStormsPane (box);
    } else {
        plotMessage (box, STORM_COLOR_CAT1, "Storm data error");
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Touch handling
// ---------------------------------------------------------------------------

/* run the storms pane-level popup menu (triggered by tapping the subtitle
 * area above the storm list).  Mirrors the runDXPedPaneMenu() pattern.
 */
static void runStormsPaneMenu (const SBox &box)
{
    enum {
        STM_TITLE,          // non-interactive label
        STM_NHC,            // open NHC web page
        STM_N
    };

    MenuItem mitems[STM_N];
    mitems[STM_TITLE] = {MENU_LABEL,  false, 0, 2, "Trop Wx", 0};
    mitems[STM_NHC]   = {MENU_TOGGLE, false, 1, 2, "Open NHC page", 0};

    uint16_t menu_x = BOX_IS_PANE_0(box) ? box.x + 3 : box.x + 10;
    SBox menu_b = {menu_x, (uint16_t)(box.y + STORM_START_DY), 0, 0};
    SBox ok_b;

    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, STM_N, mitems};
    if (runMenu (menu)) {
        if (mitems[STM_NHC].set) {
            openURL ("https://www.nhc.noaa.gov/");
            Serial.printf ("STORM: opened NHC web page\n");
        }
    }
}

/* handle touch within the storms pane.
 * returns true if touch was consumed.
 */
bool checkStormsTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + STORM_TITLE_Y0 + 5) {

        // scroll controls in title area
        if (storm_ss.checkScrollUpTouch (s, box)) {
            if (storm_ss.okToScrollUp()) {
                storm_ss.scrollUp();
                drawStormsPane (box);
            }
            return true;
        }
        if (storm_ss.checkScrollDownTouch (s, box)) {
            if (storm_ss.okToScrollDown()) {
                storm_ss.scrollDown();
                drawStormsPane (box);
            }
            return true;
        }
        return false;

    } else if (s.y < box.y + STORM_START_DY) {

        // tapped subtitle area -- show pane options including NHC web page
        runStormsPaneMenu (box);
        return true;

    } else {

        // tapped a storm row -- map the display row to the right storm
        int item = (s.y - box.y - STORM_START_DY) / STORM_ROW_H;
        int index;
        if (storm_ss.findDataIndex (item, index) && index >= 0 && index < n_storms) {

            // if this is the centered storm and the tap is on its reset button, restore the prior view
            if (storm_pz_saved && index == storm_centered_idx) {
                uint16_t row_y = (box.y + STORM_START_DY) + item * STORM_ROW_H;
                if (inBox (s, stormResetBox (box, row_y))) {
                    restorePanZoom (storm_saved_pz);
                    storm_pz_saved = false;
                    storm_centered_idx = -1;
                    drawStormsPane (box);
                    Serial.printf ("STORM: restored previous map view\n");
                    return true;
                }
            }

            // otherwise center & max-zoom the map on this storm
            const Storm &st = storms[index];
            LatLong ll = st.cur_ll;
            PanZoom before = pan_zoom;
            if (panZoomToLocation (ll, MAX_ZOOM)) {
                if (!storm_pz_saved) {              // remember the view we had before focusing any storm
                    storm_saved_pz = before;
                    storm_pz_saved = true;
                }
                storm_centered_idx = index;
            } else {
                Serial.printf ("STORM: %s tapped; map pan/zoom unavailable in this projection\n",
                               st.name);
            }
            drawStormsPane (box);                  // redraw so the reset button appears on this row
            Serial.printf ("STORM: panned to %s at %.2f,%.2f\n", st.name, ll.lat_d, ll.lng_d);
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

/* called at startup from ESPHamClock.cpp.
 * reads NV_STORMS_ON and initialises state.
 */
void initStorms (void)
{
    uint8_t storms_on;
    if (!NVReadUInt8 (NV_STORMS_ON, &storms_on)) {
        storms_on = 0;
        NVWriteUInt8 (NV_STORMS_ON, storms_on);
    }

    n_storms = 0;
    storm_ss.init (STORM_MAXSTORMS, 0, 0, ScrollState::DIR_TOPDOWN);

    Serial.printf ("STORM: init, overlay %s\n", storms_on ? "ON" : "OFF");
}
