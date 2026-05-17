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

#define STORM_MAXAGE        (3600*6)    // cache max age, secs
#define STORM_MINSIZ        10          // min acceptable file size (empty = 0 storms)
#define STORM_MAXSTORMS     20          // max simultaneous storms
#define STORM_MAXPTS        12          // max track points per storm (current + forecast)
#define STORM_NAMELEN       12          // max name length including EOS
#define STORM_IDLEN         12          // advisory or storm id length including EOS
#define STORM_TITLE_Y0      PANETITLE_H  // pane title baseline
#define STORM_ENTRY_H       22          // height of each storm entry in pane
#define STORM_START_DY      40          // y offset for first storm entry
#define STORM_TITLE_RSV     24          // reserve room at right for scroll arrow
#define STORM_CONE_COLOR    RGB565(80,80,80)      // no-alpha uncertainty outline

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

    // sort storms by peak wind speed descending (most intense first)
    for (int i = 0; i < n_storms-1; i++) {
        for (int j = i+1; j < n_storms; j++) {
            if (storms[j].peak_wind > storms[i].peak_wind) {
                Storm tmp = storms[i];
                storms[i] = storms[j];
                storms[j] = tmp;
            }
        }
    }

    storm_ss.n_data = n_storms;
    Serial.printf ("STORM: parsed %d storms\n", n_storms);
    return true;
}

// ---------------------------------------------------------------------------
// Pane drawing
// ---------------------------------------------------------------------------

static void drawStormsPane (const SBox &box)
{
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (STORM_COLOR_TITLE);
    const char *title = "Tropical Wx";
    uint16_t tw = getTextWidth (title);
    uint16_t title_w = box.w > STORM_TITLE_RSV ? box.w - STORM_TITLE_RSV : box.w;
    tft.setCursor (box.x + (title_w > tw ? (title_w - tw)/2 : 2), box.y + STORM_TITLE_Y0);
    tft.print (title);

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
            uint16_t y = y0 + r * STORM_ENTRY_H * 2;
            uint16_t color = stormCategoryColor (st.peak_cat, st.peak_wind);

            // highlight row background
            tft.fillRect (box.x+1, y-2, box.w-2, STORM_ENTRY_H*2, RA8875_BLACK);
            tft.drawLine (box.x+1, y + STORM_ENTRY_H*2 - 2,
                          box.x + box.w-2, y + STORM_ENTRY_H*2 - 2, 1, GRAY);

            // row 1: name + category + wind
            tft.setTextColor (color);
            char row1[30];
            snprintf (row1, sizeof(row1), "%s %s %dkt",
                      st.name,
                      stormCategoryLabel (st.peak_cat, st.peak_wind, st.pts[0].type),
                      st.peak_wind);
            uint16_t w1 = getTextWidth (row1);
            tft.setCursor (box.x + (box.w-w1)/2, y);
            tft.print (row1);

            // row 2: basin + current coords
            tft.setTextColor (GRAY);
            char row2[30];
            snprintf (row2, sizeof(row2), "%s %.1f%c %.1f%c",
                      st.basin,
                      fabsf(st.cur_ll.lat_d), st.cur_ll.lat_d >= 0 ? 'N' : 'S',
                      fabsf(st.cur_ll.lng_d), st.cur_ll.lng_d >= 0 ? 'E' : 'W');
            uint16_t w2 = getTextWidth (row2);
            tft.setCursor (box.x + (box.w-w2)/2, y + STORM_ENTRY_H);
            tft.print (row2);
        }
    }
}

// ---------------------------------------------------------------------------
// Map drawing
// ---------------------------------------------------------------------------

static int stormTrackWidth()
{
    return std::max (2, 2 * (int)tft.SCALESZ);
}

static int stormPointRadius (uint16_t fcst_hour)
{
    int scale = std::max (1, (int)tft.SCALESZ);
    if (fcst_hour == 0)
        return 3 * scale;
    return std::max (2, (3 * scale) - (int)(fcst_hour / 72));
}

/* Approximate forecast uncertainty from forecast hour only.
 * This is intentionally just an outline because HamClock drawing has no alpha;
 * a filled cone would obscure too much of the map.
 */
static int stormConeRadius (uint16_t fcst_hour)
{
    int scale = std::max (1, (int)tft.SCALESZ);
    int r = (2 + (int)(fcst_hour / 12)) * scale;
    int max_r = 18 * scale;
    if (r < 2 * scale)
        r = 2 * scale;
    if (r > max_r)
        r = max_r;
    return r;
}

/* draw an outlined uncertainty cone by connecting forecast-hour error envelopes */
static void drawStormConeSegment (const SCoord &s0, uint16_t fcst0, const SCoord &s1, uint16_t fcst1)
{
    float dx = s1.x - s0.x;
    float dy = s1.y - s0.y;
    float d = sqrtf (dx*dx + dy*dy);
    if (d < 1)
        return;

    float nx = -dy/d;
    float ny =  dx/d;
    int r0 = stormConeRadius (fcst0);
    int r1 = stormConeRadius (fcst1);

    int x0a = (int)roundf (s0.x + nx*r0);
    int y0a = (int)roundf (s0.y + ny*r0);
    int x1a = (int)roundf (s1.x + nx*r1);
    int y1a = (int)roundf (s1.y + ny*r1);
    int x0b = (int)roundf (s0.x - nx*r0);
    int y0b = (int)roundf (s0.y - ny*r0);
    int x1b = (int)roundf (s1.x - nx*r1);
    int y1b = (int)roundf (s1.y - ny*r1);

    tft.drawLineRaw (x0a, y0a, x1a, y1a, 1, STORM_CONE_COLOR);
    tft.drawLineRaw (x0b, y0b, x1b, y1b, 1, STORM_CONE_COLOR);
    tft.drawCircleRaw (s1.x, s1.y, r1, STORM_CONE_COLOR);
}

/* draw a storm bullseye at the current position */
static void drawStormBullseye (const LatLong &ll, uint16_t color)
{
    SCoord s;
    ll2s (ll, s, 1);
    if (!overMap(s))
        return;

    ll2sRaw (ll, s, 1);
    int r1 = 4 * tft.SCALESZ;
    int r2 = std::max (1, 2 * (int)tft.SCALESZ);

    tft.drawCircleRaw  (s.x, s.y, r1, color);
    tft.fillCircleRaw  (s.x, s.y, r2, color);
    tft.drawPixelRaw   (s.x, s.y, RA8875_WHITE);
}

/* draw a small circle at a forecast track point */
static void drawStormFcstPoint (const LatLong &ll, uint16_t color, int radius)
{
    SCoord s;
    ll2s (ll, s, 1);
    if (!overMap(s))
        return;

    ll2sRaw (ll, s, 1);
    tft.drawCircleRaw (s.x, s.y, radius, color);
    tft.fillCircleRaw (s.x, s.y, std::max (1, radius/2), color);
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

        // draw forecast uncertainty outline first, underneath the track
        for (int j = 1; j < st.n_pts; j++) {
            const StormPoint &p0 = st.pts[j-1];
            const StormPoint &p1 = st.pts[j];

            LatLong ll0, ll1;
            ll0.lat_d = p0.lat;  ll0.lng_d = p0.lon;
            ll1.lat_d = p1.lat;  ll1.lng_d = p1.lon;

            SCoord s0, s1;
            ll2sRaw (ll0, s0, 1);
            ll2sRaw (ll1, s1, 1);

            if (!overMap(s0) || !overMap(s1))
                continue;

            drawStormConeSegment (s0, p0.fcst_hour, s1, p1.fcst_hour);
        }

        // draw forecast track lines on top of the cone
        for (int j = 1; j < st.n_pts; j++) {
            const StormPoint &p0 = st.pts[j-1];
            const StormPoint &p1 = st.pts[j];

            LatLong ll0, ll1;
            ll0.lat_d = p0.lat;  ll0.lng_d = p0.lon;
            ll1.lat_d = p1.lat;  ll1.lng_d = p1.lon;

            SCoord s0, s1;
            ll2sRaw (ll0, s0, 1);
            ll2sRaw (ll1, s1, 1);

            if (!overMap(s0) || !overMap(s1))
                continue;

            uint16_t color = stormCategoryColor (p0.category, p0.wind_kt);
            tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, stormTrackWidth(), color);
        }

        // draw forecast track circles
        for (int j = 1; j < st.n_pts; j++) {
            const StormPoint &pt = st.pts[j];
            LatLong ll;
            ll.lat_d = pt.lat;
            ll.lng_d = pt.lon;
            uint16_t color = stormCategoryColor (pt.category, pt.wind_kt);
            int r = stormPointRadius (pt.fcst_hour);
            drawStormFcstPoint (ll, color, r);
        }

        // draw current position bullseye on top
        if (st.n_pts > 0 && st.pts[0].fcst_hour == 0) {
            uint16_t color = stormCategoryColor (st.pts[0].category, st.pts[0].wind_kt);
            drawStormBullseye (st.cur_ll, color);
        }
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

        // tapped subtitle/menu area -- nothing for now
        return true;

    } else {

        // tapped a storm entry -- set DX to that storm's current position
        int item = (s.y - box.y - STORM_START_DY) / (STORM_ENTRY_H * 2);
        int min_i, max_i;
        storm_ss.getVisDataIndices (min_i, max_i);
        int index = min_i + item;

        if (index >= 0 && index < n_storms) {
            const Storm &st = storms[index];
            LatLong ll = st.cur_ll;
            newDX (ll, NULL, NULL);
            Serial.printf ("STORM: set DX to %s at %.2f,%.2f\n",
                           st.name, ll.lat_d, ll.lng_d);
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
