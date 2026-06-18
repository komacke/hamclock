/* hurricane.cpp -- tropical cyclone tracking overlay for HamClock
 *
 * Fetches active storm data from OHB backend (fetch_hurricane.py) and
 * displays storm tracks on the map and in a scrollable pane.
 *
 * Data format from /storms/storms.txt (one line per track point):
 *   NAME,BASIN,TYPE,CATEGORY,LAT,LON,WIND_KT,FCST_HOUR,ADVISORY
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
    uint8_t    peak_cat;            // highest category in track
    uint16_t   peak_wind;           // highest wind speed in track
    LatLong    cur_ll;              // current position (fcst_hour==0)
    StormPoint pts[STORM_MAXPTS];   // track points sorted by fcst_hour
    int        n_pts;               // number of valid track points
} Storm;

static Storm  storms[STORM_MAXSTORMS];
static int    n_storms = 0;

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

/* parse storms.txt into storms[] array.
 * format: NAME,BASIN,TYPE,CATEGORY,LAT,LON,WIND_KT,FCST_HOUR,ADVISORY
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
                sp->cur_ll.lat_d = lat;
                sp->cur_ll.lng_d = lon;
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
    const char *title = "Tropical Wx";
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
    tft.fillCircleRaw (s.x, s.y, r, color);
    tft.drawCircleRaw (s.x, s.y, r, RA8875_BLACK);      // thin edge for contrast over the map
    if (current)
        tft.drawPixelRaw (s.x, s.y, RA8875_WHITE);      // mark the storm's current position
}

/* draw a dramatic direction indicator at the last forecast track point:
 *   - an extension line starting at the last dot's edge
 *   - straight if only two track points are available
 *   - curved (continuing the arc of the last two segments) if three or more
 *     points are available; total curvature is capped at 90 degrees so the
 *     arrow never spirals back on itself
 *   - a filled arrowhead at the far end of the extension line
 *
 * All geometry is done in raw screen pixels after ll2sRaw() conversion.
 */
static void drawStormDirectionArrow (const Storm &st)
{
    if (st.n_pts < 2)
        return;

    // --- screen coords of the last two track points ---
    const StormPoint &p_prev = st.pts[st.n_pts - 2];
    const StormPoint &p_last = st.pts[st.n_pts - 1];

    LatLong ll_prev, ll_last;
    ll_prev.lat_d = p_prev.lat;  ll_prev.lng_d = p_prev.lon;
    ll_last.lat_d = p_last.lat;  ll_last.lng_d = p_last.lon;

    SCoord sc_prev, sc_last;
    ll2s (ll_prev, sc_prev, 1);
    ll2s (ll_last, sc_last, 1);
    if (!overMap(sc_prev) || !overMap(sc_last))
        return;
    ll2sRaw (ll_prev, sc_prev, 1);
    ll2sRaw (ll_last, sc_last, 1);

    // unit direction vector of the last track segment
    float dx = (float)(sc_last.x - sc_prev.x);
    float dy = (float)(sc_last.y - sc_prev.y);
    float seg_len = sqrtf (dx*dx + dy*dy);
    if (seg_len < 0.5f)
        return;
    dx /= seg_len;
    dy /= seg_len;

    // --- derive per-pixel angular turn rate from the penultimate segment ---
    // positive = clockwise in screen coords (Y-down), negative = counter-clockwise
    float turn_rate = 0.0f;
    if (st.n_pts >= 3) {
        const StormPoint &p_prev2 = st.pts[st.n_pts - 3];
        LatLong ll_prev2;
        ll_prev2.lat_d = p_prev2.lat;  ll_prev2.lng_d = p_prev2.lon;
        SCoord sc_prev2;
        ll2s (ll_prev2, sc_prev2, 1);
        if (overMap(sc_prev2)) {
            ll2sRaw (ll_prev2, sc_prev2, 1);
            float dx1 = (float)(sc_prev.x - sc_prev2.x);
            float dy1 = (float)(sc_prev.y - sc_prev2.y);
            float len1 = sqrtf (dx1*dx1 + dy1*dy1);
            if (len1 > 0.5f) {
                dx1 /= len1;
                dy1 /= len1;
                // signed angle between consecutive segment directions
                float angle_change = atan2f (dx1*dy - dy1*dx, dx1*dx + dy1*dy);
                turn_rate = angle_change / seg_len;   // radians per screen-pixel
                // cap total curvature at 90 degrees so the arrow cannot curl back
                float ext_f  = (float)(stormSizeUnit() * 3);
                float max_tr = 1.5707963f / ext_f;    // pi/2 spread over extension
                if (turn_rate >  max_tr) turn_rate =  max_tr;
                if (turn_rate < -max_tr) turn_rate = -max_tr;
            }
        }
    }

    // --- draw the extension line from the last dot's edge ---
    int ext_len  = stormSizeUnit() * 3;     // extension length in screen pixels
    int lw       = stormTrackWidth();
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

    for (int drawn = 0; drawn < ext_len; drawn += step_px) {
        float nx = cur_x + cur_dx * (float)step_px;
        float ny = cur_y + cur_dy * (float)step_px;
        tft.drawLineRaw ((int16_t)cur_x, (int16_t)cur_y,
                         (int16_t)nx,    (int16_t)ny, lw, color);
        cur_x = nx;
        cur_y = ny;
        // rotate direction for the next step; renormalize to prevent float drift
        float ndx  =  cur_dx * cos_a - cur_dy * sin_a;
        float ndy  =  cur_dx * sin_a + cur_dy * cos_a;
        float nlen = sqrtf (ndx*ndx + ndy*ndy);
        if (nlen > 0.01f) { cur_dx = ndx / nlen;  cur_dy = ndy / nlen; }
    }

    // --- arrowhead at the end of the extension, in the final travel direction ---
    int alen = r * 2 + lw;   // arrowhead height
    int awid = r + 1;        // arrowhead half-width at base
    float perp_x = -cur_dy;
    float perp_y =  cur_dx;

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

            LatLong ll0, ll1;
            ll0.lat_d = p0.lat;  ll0.lng_d = p0.lon;
            ll1.lat_d = p1.lat;  ll1.lng_d = p1.lon;

            SCoord a, b;
            ll2s (ll0, a, 1);
            ll2s (ll1, b, 1);
            if (!overMap(a) || !overMap(b))
                continue;

            ll2sRaw (ll0, a, 1);
            ll2sRaw (ll1, b, 1);
            uint16_t color = stormCategoryColor (p0.category, p0.wind_kt);
            tft.drawLineRaw (a.x, a.y, b.x, b.y, stormTrackWidth(), color);
        }

        // 2. category-colored dot at each position; the current position is flagged
        for (int j = 0; j < st.n_pts; j++) {
            const StormPoint &pt = st.pts[j];
            LatLong ll;
            ll.lat_d = pt.lat;
            ll.lng_d = pt.lon;
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

/* retrieve storms file from OHB backend and parse it */
static bool retrieveStorms (const SBox &box)
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

    return ok;
}

/* update the storms pane.
 * called from wifi.cpp updateWiFi() PLOT_CH_STORMS case.
 */
bool updateStorms (const SBox &box, bool fresh)
{
    bool ok = true;
    static uint32_t prev_refresh = 0;

    if (fresh || timesUp (&prev_refresh, (uint32_t)STORM_MAXAGE * 1000)) {
        prev_refresh = millis();
        ok = retrieveStorms (box);
        if (ok)
            fresh = true;
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
    mitems[STM_TITLE] = {MENU_LABEL,  false, 0, 2, "Tropical Wx", 0};
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
