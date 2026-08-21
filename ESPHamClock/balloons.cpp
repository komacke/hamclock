/* balloons.cpp  --  Amateur Radio High-Altitude Balloon (HAB) and WSPR/APRS PicoBalloon
 * tracking pane for HamClock.
 *
 * Data is fetched from the OHB backend (fetch_balloons.py) which itself polls two
 * upstream sources so HamClock never talks to either directly:
 *
 *   HAB   -- api.v2.sondehub.org "/amateur" latest-position feed (SondeHub-Amateur)
 *   PICO  -- knormoyle/knormoyle.github.io "mymaps.csv" (WSPR/U4B/Zachtek/APRS pico
 *            balloons, derived by that project from wsprlive; see mymaps.csv's own
 *            header for the gory detail of how flights are discovered)
 *
 * fetch_balloons.py merges both into one small CSV cache file served at balloons_page:
 *
 *   Line 1:  credit string
 *   One CSV line per balloon thereafter:
 *     TYPE,NAME,CALLSIGN,LASTHEARD,LAT,LON,ALT_M,SPEED_MPS,HDG_DEG,DETAIL,URL,TRACK,
 *     FREQ_HZ,BATT_V,TEMP_C
 *
 *   TYPE      "HAB" or "PICO"
 *   NAME      short flight/display name
 *   CALLSIGN  operator or payload callsign
 *   LASTHEARD unix timestamp of the most recent report used to build this row
 *   LAT, LON  signed decimal degrees (row omitted upstream if no fix is known)
 *   ALT_M     integer metres, or "NA" if unknown (common for un-instrumented pico flights)
 *   SPEED_MPS horizontal speed, or "NA"
 *   HDG_DEG   heading true, or "NA"
 *   DETAIL    short free-text second line for the pane (commas already stripped)
 *   URL       web page to open from the tap menu (tracker page)
 *   TRACK     older breadcrumb points, oldest-first, NOT including the current LAT,LON:
 *             "" (none) or "lat1:lon1|lat2:lon2|...". Neither source API hands back a
 *             pollable position history (SondeHub's amateur feed is latest-only, and
 *             mymaps.csv is likewise a single current row per flight), so this is a
 *             breadcrumb trail the backend accumulates itself across its own polling
 *             runs -- see fetch_balloons.py's local history state -- rather than the
 *             balloon's true continuous flight path. Good enough to show recent drift
 *             direction on the map; not a substitute for a real telemetry history.
 *   FREQ_HZ   transmit frequency in Hz, or "NA"
 *   BATT_V    battery voltage, or "NA"
 *   TEMP_C    payload temperature in Celsius, or "NA"
 *
 * Map icons come from a tiny 2-frame "sprite sheet" (HAB, PICO) -- see balloon_sprites[]
 * below -- drawn in the balloon's type colour, dimmed when its last report has aged out
 * past BALLOON_STALE_AGE so a scroll of the map still shows a flight's rough location
 * without implying it's still actively transmitting.
 *
 * Pane: PLOT_CH_BALLOONS (scrollable list, follows the Launches/Storms pattern)
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Path on OHB backend (httpHCGET prepends "/ham/HamClock") and local cache file name.
static const char balloons_page[] = "/balloons/balloons.txt";
static const char balloons_fn[]   = "balloons.txt";

#define BALLOONS_MAXAGE     600         // re-download when cache older than this, secs (10 min)
#define BALLOONS_MINSIZ     10          // accept even a credit-only (no active flights) file
// N.B. BALLOONS_INTERVAL (pane redraw interval, secs) is defined in HamClock.h since
// wifi.cpp's dispatcher needs it too -- same convention as LAUNCHES_INTERVAL.

#define BALLOON_STALE_AGE   (6*3600)    // dim an entry once its last report is this old, secs
#define BALLOON_DROP_AGE     (3*86400)  // backend already drops these, but be defensive locally too

#define BALN_MAX_TRACK       8          // max older breadcrumb points kept per balloon
#define BALN_TRACK_LW        1          // track line width, canonical px (grows with SCALESZ)

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define BALN_TITLE_DY     PANETITLE_H   // pane title baseline
#define BALN_TITLE_RSV    28            // reserve at right so title clears the scroll-arrow control
#define BALN_CREDITS_DY   SUBTITLE_Y0   // source credit line
#define BALN_HEADER_DY    (SUBTITLE_Y0 + 11)   // column-header row, just under the credit line
#define BALN_START_DY     (BALN_HEADER_DY + 13) // first data row, just under the header row
#define BALN_ROW_H        LISTING_DY    // single-line row spacing -- matches On The Air's list style
#define BALN_ROW_OS       LISTING_OS    // background-rect y offset above the text baseline
#define BALN_COL_N        4             // TYP, FREQ, CALL, ALT -- evenly spaced across the pane width
#define BALN_COL_GAP      2             // small gap reserved at the right edge of each column

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

#define BALN_TITLE_COLOR   RGB565(150,220,255)   // pane title
#define BALN_HAB_COLOR     RGB565(90,190,255)    // HAB tag/icon: sky blue
#define BALN_HAB_DIM       RGB565(60,90,110)     // HAB, stale
#define BALN_PICO_COLOR    RGB565(255,175,60)    // PicoBalloon tag/icon: amber
#define BALN_PICO_DIM      RGB565(110,85,50)     // PicoBalloon, stale
#define BALN_REST_COLOR    RA8875_WHITE          // callsign + altitude text

// ---------------------------------------------------------------------------
// Map icon "sprite sheet"
//
// Two 12x14px palette-indexed glyphs (HAB, PICO): 0=transparent, 1=outline(black),
// 2=body, 3=highlight. Deliberately the SAME bright color scheme for both types --
// map terrain varies too much (ocean, desert, forest) for a single flat fill color
// to stay visible everywhere, so this uses an actual small red/white "circus balloon"
// look with a black outline for contrast against any background, the way spots on
// this map are drawn with an outline ring rather than a bare dot. Only the *shape*
// still differs by type (HAB gets a small payload box; PICO doesn't). Staleness is
// conveyed by dimming the body/highlight at draw time (see dimRGB565()) rather than
// baking a second dim palette into the sprite sheet. Sized to match the visual weight
// of this app's other map markers (see hurricane.cpp's stormDotRadius()). Regenerate
// with integration/scripts/gen_balloon_sprites.py if the artwork ever needs to change.
// ---------------------------------------------------------------------------

#define BALN_SPR_W    12                        // sprite width, px
#define BALN_SPR_H    14                        // sprite height, px

#define BALN_PAL_TRANSP  0
#define BALN_PAL_OUTLINE 1
#define BALN_PAL_BODY    2
#define BALN_PAL_HILITE  3

#define BALN_COL_OUTLINE  RA8875_BLACK
#define BALN_COL_BODY     RGB565(230,40,40)     // bright red -- same for HAB and PICO
#define BALN_COL_HILITE   RGB565(255,210,210)   // light pink highlight, gives it a 3D look
#define BALN_STALE_DIM    0.45F                 // brightness factor once a report has gone stale

typedef enum {
    BALN_SPR_HAB,
    BALN_SPR_PICO,
    BALN_SPR_N
} BalnSpriteFrame;

static const uint8_t balloon_sprites[BALN_SPR_N][BALN_SPR_H*BALN_SPR_W] PROGMEM = {
{
    // HAB: balloon body + highlight + knot/string + small outlined payload box
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,1,1,2,1,1,0,0,0,0,
    0,0,1,2,3,2,2,2,1,0,0,0,
    0,0,1,3,2,2,2,2,1,0,0,0,
    0,1,2,2,2,2,2,2,2,1,0,0,
    0,0,1,2,2,2,2,2,1,0,0,0,
    0,0,1,2,2,2,2,2,1,0,0,0,
    0,0,0,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,0,
    0,0,0,0,1,0,1,0,0,0,0,0,
    0,0,0,0,1,1,1,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0
},
{
    // PICO: same balloon body + highlight, longer thin string, no payload box
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,1,1,2,1,1,0,0,0,0,
    0,0,1,2,3,2,2,2,1,0,0,0,
    0,0,1,3,2,2,2,2,1,0,0,0,
    0,1,2,2,2,2,2,2,2,1,0,0,
    0,0,1,2,2,2,2,2,1,0,0,0,
    0,0,1,2,2,2,2,2,1,0,0,0,
    0,0,0,1,1,1,1,1,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,1,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0
}
};

/* scale an RGB565 color's brightness by f (0..1), used to dim the sprite once a
 * balloon's last report has gone stale without needing a second baked-in palette.
 */
static uint16_t dimRGB565 (uint16_t c, float f)
{
    uint16_t r = (uint16_t)(((c >> 11) & 0x1F) * f);
    uint16_t g = (uint16_t)(((c >> 5) & 0x3F) * f);
    uint16_t b = (uint16_t)((c & 0x1F) * f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* draw one sprite frame CENTERED on raw screen coord sr, scaled by tft.SCALESZ. The
 * outline stays full-strength black regardless of staleness so the shape stays crisp
 * and readable even when dimmed; only the body/highlight fill dims. Centering (rather
 * than anchoring the icon above the point) keeps the icon's footprint balanced around
 * the balloon's actual position, which matters for both hover accuracy (getNearestBalloon()
 * tests distance to the real lat/lon) and for not needlessly extending the icon's
 * bounding box toward whatever's above it on screen.
 */
static void drawBalloonSprite (const SCoord &sr, BalnSpriteFrame frame, bool stale)
{
    const int S = tft.SCALESZ;
    const int x0 = (int)sr.x - (BALN_SPR_W*S)/2;
    const int y0 = (int)sr.y - (BALN_SPR_H*S)/2;

    const uint16_t body_col  = stale ? dimRGB565 (BALN_COL_BODY, BALN_STALE_DIM) : BALN_COL_BODY;
    const uint16_t hilite_col = stale ? dimRGB565 (BALN_COL_HILITE, BALN_STALE_DIM) : BALN_COL_HILITE;

    for (int r = 0; r < BALN_SPR_H; r++) {
        for (int c = 0; c < BALN_SPR_W; c++) {
            uint8_t idx = pgm_read_byte (&balloon_sprites[frame][r*BALN_SPR_W + c]);
            if (idx == BALN_PAL_TRANSP)
                continue;
            uint16_t color;
            switch (idx) {
            case BALN_PAL_OUTLINE: color = BALN_COL_OUTLINE; break;
            case BALN_PAL_HILITE:  color = hilite_col; break;
            default:                color = body_col; break;         // BALN_PAL_BODY
            }
            int sx = x0 + c*S;
            int sy = y0 + r*S;
            for (int dy = 0; dy < S; dy++)
                for (int dx = 0; dx < S; dx++)
                    tft.drawPixelRaw (sx+dx, sy+dy, color);
        }
    }
}

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

typedef enum {
    BALN_HAB,
    BALN_PICO,
} BalnType;

typedef struct {
    BalnType type;
    char    *name;                      // malloced display name (flight name / payload callsign)
    char    *callsign;                  // malloced operator/payload callsign
    char    *detail;                    // malloced second-row free text
    char    *url;                       // malloced tracker page URL
    time_t   last_t;                    // unix time of most recent report
    float    lat, lng;                  // decimal degrees
    bool     has_alt;
    float    alt_m;
    bool     has_speed;
    float    speed_mps;
    bool     has_hdg;
    float    hdg_deg;
    int      n_track;                   // count of older breadcrumb points in track_lat/lng[]
    float    track_lat[BALN_MAX_TRACK]; // oldest-first, NOT including current lat/lng
    float    track_lng[BALN_MAX_TRACK];
    bool     has_freq;
    float    freq_hz;
    bool     has_batt;
    float    batt_v;
    bool     has_temp;
    float    temp_c;
} BalloonEntry;

static BalloonEntry *balloons;          // malloced array, count = bln_ss.n_data
static char         *balloons_credit;   // malloced credit / attribution line
static ScrollState   bln_ss;            // scroll state

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* split a CSV line in place into up to maxf field pointers; return field count.
 * handles double-quoted fields containing commas and doubled "" quote escapes.
 * (same algorithm as activenets.cpp's splitCSV(), duplicated locally per this
 * codebase's convention of small self-contained parsing helpers per file.)
 */
static int splitBalnCSV (char *line, char *fields[], int maxf)
{
    int n = 0;
    char *p = line;

    while (n < maxf) {
        char *out = p;
        char *start = out;
        bool inq = false;

        for (;;) {
            char c = *p;
            if (inq) {
                if (c == '"') {
                    if (p[1] == '"') { *out++ = '"'; p += 2; continue; }
                    inq = false; p++; continue;
                } else if (c == '\0') {
                    break;
                } else {
                    *out++ = c; p++; continue;
                }
            } else {
                if (c == '"') { inq = true; p++; continue; }
                if (c == ',' || c == '\0') break;
                *out++ = c; p++; continue;
            }
        }

        char sep = *p;
        *out = '\0';
        fields[n++] = start;
        if (sep == '\0')
            break;
        p++;
    }

    return (n);
}

/* current colour for a balloon's TYP badge in the pane list, factoring in type and
 * staleness -- NOT used for the map icon any more (see drawBalloonSprite()'s header
 * comment for why the map icon is deliberately the same bright color for both types).
 */
static uint16_t balloonColor (const BalloonEntry &be)
{
    bool stale = (myNow() - be.last_t) > BALLOON_STALE_AGE;
    if (be.type == BALN_HAB)
        return stale ? BALN_HAB_DIM : BALN_HAB_COLOR;
    else
        return stale ? BALN_PICO_DIM : BALN_PICO_COLOR;
}

/* whether a balloon's last report is old enough to be considered stale. */
static bool balloonIsStale (const BalloonEntry &be)
{
    return (myNow() - be.last_t) > BALLOON_STALE_AGE;
}

/* compact "age" string, eg "3m", "2h14m", "1d5h". */
static void formatAge (time_t last_t, char *buf, size_t buf_l)
{
    long secs = (long)(myNow() - last_t);
    if (secs < 0)
        secs = 0;
    if (secs < 60)
        snprintf (buf, buf_l, "%lds", secs);
    else if (secs < 3600)
        snprintf (buf, buf_l, "%ldm", secs/60);
    else if (secs < 86400)
        snprintf (buf, buf_l, "%ldh%02ldm", secs/3600, (secs%3600)/60);
    else
        snprintf (buf, buf_l, "%ldd%ldh", secs/86400, (secs%86400)/3600);
}

/* format altitude in the user's configured units (Settings > Units: Metric shows
 * meters; Imperial and British both show feet, matching showDistKm()'s existing
 * metric-vs-everything-else convention used throughout the rest of HamClock).
 */
static void formatBalnAlt (const BalloonEntry &be, char *buf, size_t buf_l)
{
    if (!be.has_alt) {
        snprintf (buf, buf_l, "?");
        return;
    }
    if (showDistKm())
        snprintf (buf, buf_l, "%.0fm", be.alt_m);
    else
        snprintf (buf, buf_l, "%.0fft", be.alt_m * FT_PER_M);
}

/* free all malloced balloon data and reset the scroll state. */
static void freeBalloons (void)
{
    for (int i = 0; i < bln_ss.n_data; i++) {
        free (balloons[i].name);
        free (balloons[i].callsign);
        free (balloons[i].detail);
        free (balloons[i].url);
    }
    free (balloons);
    balloons = NULL;
    free (balloons_credit);
    balloons_credit = NULL;
    bln_ss.n_data = 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

/* compute BALN_COL_N+1 evenly-spaced column boundary x coords spanning the box width;
 * column c's usable text region is [col_x[c], col_x[c+1] - BALN_COL_GAP).
 */
static void balnColX (const SBox &box, uint16_t col_x[BALN_COL_N+1])
{
    uint16_t avail = box.w - 4;
    for (int i = 0; i <= BALN_COL_N; i++)
        col_x[i] = box.x + 2 + (avail * i) / BALN_COL_N;
}

/* usable text width of the column spanning [x0,x1), clamped to avoid unsigned
 * underflow if a pane is ever narrower than BALN_COL_N columns can sanely hold.
 */
static uint16_t balnColW (uint16_t x0, uint16_t x1)
{
    return (x1 > x0 + BALN_COL_GAP) ? (x1 - x0 - BALN_COL_GAP) : 0;
}

static void drawBalloonsPane (const SBox &box)
{
    if (!balloons_credit)
        return;

    prepPlotBox (box);

    // ---- Title (kept clear of the scroll-arrow control, same technique as Launches) ----
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (BALN_TITLE_COLOR);
    static const char *title = "Balloons";
    uint16_t tw = getTextWidth (title);
    uint16_t avail_r = box.w > BALN_TITLE_RSV ? box.w - BALN_TITLE_RSV : box.w;
    uint16_t tx = box.w > tw ? (box.w - tw)/2 : 2;
    if (tx + tw > avail_r)
        tx = avail_r > tw ? avail_r - tw : 2;
    tft.setCursor (box.x + tx, box.y + BALN_TITLE_DY);
    tft.print (title);

    // ---- Credit ----
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (BALN_TITLE_COLOR);
    char credit_copy[80];
    quietStrncpy (credit_copy, balloons_credit, sizeof(credit_copy));
    maxStringW (credit_copy, box.w - 4);
    uint16_t cw = getTextWidth (credit_copy);
    tft.setCursor (box.x + (box.w - cw) / 2, box.y + BALN_CREDITS_DY);
    tft.print (credit_copy);

    // ---- Column headers: TYP / FREQ / CALL / ALT, evenly spaced ----
    uint16_t col_x[BALN_COL_N+1];
    balnColX (box, col_x);
    static const char *col_hdrs[BALN_COL_N] = {"TYP", "FREQ", "CALL", "ALT"};
    tft.setTextColor (BRGRAY);
    for (int c = 0; c < BALN_COL_N; c++) {
        tft.setCursor (col_x[c], box.y + BALN_HEADER_DY);
        tft.print (col_hdrs[c]);
    }

    // ---- "no active flights" placeholder ----
    if (bln_ss.n_data == 0) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (BRGRAY);
        static const char *none_msg = "No active balloons reported";
        uint16_t nw = getTextWidth (none_msg);
        tft.setCursor (box.x + (box.w - nw)/2, box.y + BALN_START_DY);
        tft.print (none_msg);
        return;
    }

    // ---- Entries: one line each, four evenly-spaced columns -- TYP/FREQ (band-colored
    // background, same technique as the On The Air pane)/CALL/ALT. Everything else
    // (battery, temperature, age, full flight name) is one hover away via
    // drawIB_Balloon() rather than crowding the list.
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t y0 = box.y + BALN_START_DY;

    int min_i, max_i;
    if (bln_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            BalloonEntry &be = balloons[i];
            int      r     = bln_ss.getDisplayRow (i);
            uint16_t row_y = y0 + r * BALN_ROW_H;
            uint16_t badge_col = balloonColor (be);
            const char *tag = (be.type == BALN_HAB) ? "H" : "P";

            // TYP column -- filled badge, white text, same fillRect+contrast-text
            // technique spots.cpp uses for its "I" (IOTA) marker in the DX pane.
            char typbuf[4];
            quietStrncpy (typbuf, tag, sizeof(typbuf));
            maxStringW (typbuf, balnColW (col_x[0], col_x[1]));
            uint16_t typ_w = getTextWidth (typbuf);
            tft.fillRect (col_x[0]-1, row_y-BALN_ROW_OS, typ_w+2, BALN_ROW_H-2, badge_col);
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (col_x[0], row_y);
            tft.print (typbuf);

            // FREQ column: band-colored background, same technique as ontheair.cpp's
            // drawONTAVisSpots(). Shows "?" if unknown. Renders with no visible
            // highlight (just plain text) if the band isn't one of the app's
            // configured ham bands (eg 70cm HAB telemetry) -- getBandColor() returns
            // black in that case, same graceful fallback ontheair.cpp gets.
            char freqbuf[12];
            if (be.has_freq) {
                float kHz = be.freq_hz / 1000.0f;
                snprintf (freqbuf, sizeof(freqbuf), "%.0f", kHz);
                maxStringW (freqbuf, balnColW (col_x[1], col_x[2]));
                uint16_t bg_col = getBandColor (kHz);
                uint16_t txt_col = getGoodTextColor (bg_col);
                uint16_t fw = getTextWidth (freqbuf);
                tft.fillRect (col_x[1]-1, row_y-BALN_ROW_OS, fw+2, BALN_ROW_H-2, bg_col);
                tft.setTextColor (txt_col);
                tft.setCursor (col_x[1], row_y);
                tft.print (freqbuf);
            } else {
                tft.setTextColor (BRGRAY);
                tft.setCursor (col_x[1], row_y);
                tft.print ('?');
            }

            // CALL column -- truncated to fit if too long
            char callbuf[MAX_SPOTCALL_LEN];
            quietStrncpy (callbuf, be.callsign, sizeof(callbuf));
            maxStringW (callbuf, balnColW (col_x[2], col_x[3]));
            tft.setTextColor (BALN_REST_COLOR);
            tft.setCursor (col_x[2], row_y);
            tft.print (callbuf);

            // ALT column
            char altbuf[16];
            formatBalnAlt (be, altbuf, sizeof(altbuf));
            maxStringW (altbuf, balnColW (col_x[3], col_x[4]));
            tft.setTextColor (BALN_REST_COLOR);
            tft.setCursor (col_x[3], row_y);
            tft.print (altbuf);
        }
    }

    // ---- Scroll controls ----
    bln_ss.drawScrollDownControl (box, BALN_TITLE_COLOR, BALN_TITLE_COLOR);
    bln_ss.drawScrollUpControl   (box, BALN_TITLE_COLOR, BALN_TITLE_COLOR);
}

// ---------------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------------

static void scrollBalloonsUp (const SBox &box)
{
    if (bln_ss.okToScrollUp()) {
        bln_ss.scrollUp();
        drawBalloonsPane (box);
    }
}

static void scrollBalloonsDown (const SBox &box)
{
    if (bln_ss.okToScrollDown()) {
        bln_ss.scrollDown();
        drawBalloonsPane (box);
    }
}

// ---------------------------------------------------------------------------
// Touch / menu
// ---------------------------------------------------------------------------

/* Show a popup menu for the tapped balloon entry.
 * Returns true if a full re-schedule is needed, false if just a redraw.
 */
static bool runBalloonsMenu (const SCoord &s, const SBox &box)
{
    BalloonEntry *bep = NULL;
    if (s.y >= box.y + BALN_START_DY && bln_ss.n_data > 0) {
        int display_row = (s.y - box.y - BALN_START_DY) / BALN_ROW_H;
        int data_i;
        if (bln_ss.findDataIndex (display_row, data_i))
            bep = &balloons[data_i];
    }

    const int indent = 2;

    char bname[50];
    quietStrncpy (bname, bep ? bep->name : "", sizeof(bname));
    const uint16_t menu_gap = 20;
    for (uint16_t t_l = getTextWidth(bname); t_l > box.w - 2*menu_gap; t_l = getTextWidth(bname)) {
        char *r_space = strrchr (bname, ' ');
        if (r_space)
            *r_space = '\0';
        else
            bname[strlen(bname) - 1] = '\0';
    }

    MenuFieldType bname_mft = bep ? MENU_LABEL  : MENU_IGNORE;
    MenuFieldType open_mft  = (bep && bep->url && bep->url[0]) ? MENU_TOGGLE : MENU_IGNORE;
    MenuFieldType credit_mft = bep ? MENU_IGNORE : MENU_TOGGLE;

    enum { BM_NAME, BM_OPEN, BM_CREDIT };

    MenuItem mitems[] = {
        {bname_mft,  false, 0, indent, bname,              0},
        {open_mft,   false, 1, indent, "Open tracker page", 0},
        {credit_mft, false, 2, indent, "SondeHub / wsprlive", 0},
    };
    const int n_mi = NARRAY(mitems);

    const uint16_t menu_x     = box.x + menu_gap;
    const uint16_t menu_h     = 60;
    const uint16_t menu_max_y = box.y + box.h - menu_h - 5;
    const uint16_t menu_y     = s.y < menu_max_y ? s.y : menu_max_y;
    SBox menu_b = {menu_x, menu_y, 0, 0};
    SBox ok_b;

    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n_mi, mitems};
    if (runMenu (menu)) {
        if (bep && mitems[BM_OPEN].set)
            openURL (bep->url);
        else if (!bep && mitems[BM_CREDIT].set)
            openURL ("https://amateur.sondehub.org/");
    }

    return (false);
}

bool checkBalloonsTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + PANETITLE_H) {
        if (bln_ss.checkScrollUpTouch (s, box)) {
            scrollBalloonsUp (box);
            return (true);
        }
        if (bln_ss.checkScrollDownTouch (s, box)) {
            scrollBalloonsDown (box);
            return (true);
        }
    } else {
        runBalloonsMenu (s, box);
        return (true);
    }

    return (false);
}

// ---------------------------------------------------------------------------
// Data retrieval
// ---------------------------------------------------------------------------

/* qsort comparator: most-recently-heard last (see launches.cpp's identical rationale --
 * ScrollState anchors scrollToNewest() on the *last* array entry).
 */
static int balnLastTAscCompar (const void *a, const void *b)
{
    const BalloonEntry *ba = (const BalloonEntry *) a;
    const BalloonEntry *bb = (const BalloonEntry *) b;
    if (ba->last_t < bb->last_t)
        return (-1);
    if (ba->last_t > bb->last_t)
        return (1);
    return (0);
}

/* Fetch and parse balloons.txt from the OHB cache. Returns true if successful. */
static bool retrieveBalloons (const SBox &box)
{
    bool ok = false;

    FILE *fp = openCachedFile (balloons_fn, balloons_page, BALLOONS_MAXAGE, BALLOONS_MINSIZ);
    if (!fp) {
        Serial.printf ("BALN: failed to open %s\n", balloons_fn);
        return (false);
    }

    updateClocks (false);

    freeBalloons();

    int max_vis = (box.h - BALN_START_DY) / BALN_ROW_H;
    if (max_vis < 1)
        max_vis = 1;
    bln_ss.init (max_vis, 0, 0, ScrollState::DIR_TOPDOWN);

    char line[300];

    // ---- Credit line ----
    if (!fgets (line, sizeof(line), fp)) {
        Serial.printf ("BALN: %s: no credit line\n", balloons_fn);
        goto out;
    }
    chompString (line);
    balloons_credit = strdup (line);
    ok = true;                          // credit line alone counts as a successful fetch

    // ---- One CSV row per balloon ----
    while (fgets (line, sizeof(line), fp)) {

        chompString (line);
        if (line[0] == '\0' || line[0] == '#')
            continue;

        char *f[15];
        int nf = splitBalnCSV (line, f, NARRAY(f));
        if (nf < 11)
            continue;

        // skip an accidental header row
        if (strcmp (f[0], "TYPE") == 0)
            continue;

        BalnType type;
        if (!strcasecmp (f[0], "HAB"))
            type = BALN_HAB;
        else if (!strcasecmp (f[0], "PICO"))
            type = BALN_PICO;
        else
            continue;                   // unknown type -- ignore defensively

        const char *name_s  = f[1];
        const char *call_s  = f[2];
        time_t last_t        = (time_t) atol (f[3]);
        const char *lat_s   = f[4];
        const char *lon_s   = f[5];
        const char *alt_s   = f[6];
        const char *spd_s   = f[7];
        const char *hdg_s   = f[8];
        const char *detail_s = f[9];
        const char *url_s   = f[10];
        const char *track_s = nf > 11 ? f[11] : "";    // TRACK is optional for backward compat
        const char *freq_s  = nf > 12 ? f[12] : "NA";  // FREQ_HZ/BATT_V/TEMP_C likewise
        const char *batt_s  = nf > 13 ? f[13] : "NA";
        const char *temp_s  = nf > 14 ? f[14] : "NA";

        if (name_s[0] == '\0' || strcmp (lat_s, "NA") == 0 || strcmp (lon_s, "NA") == 0)
            continue;                   // need at least a name and a fix

        if (myNow() - last_t > BALLOON_DROP_AGE)
            continue;                   // defensive: backend should already have dropped this

        balloons = (BalloonEntry*) realloc (balloons, (bln_ss.n_data + 1) * sizeof(BalloonEntry));
        if (!balloons)
            fatalError ("No memory for %d balloons", bln_ss.n_data + 1);

        BalloonEntry &be = balloons[bln_ss.n_data++];
        memset (&be, 0, sizeof(BalloonEntry));

        be.type    = type;
        be.last_t  = last_t;
        be.lat     = atof (lat_s);
        be.lng     = atof (lon_s);

        char nbuf[80];
        quietStrncpy (nbuf, name_s, sizeof(nbuf));
        be.name     = strdup (nbuf);
        be.callsign = strdup (call_s);
        be.detail   = strdup (detail_s);
        be.url      = strdup (url_s);

        if (strcmp (alt_s, "NA") != 0) {
            be.has_alt = true;
            be.alt_m = atof (alt_s);
        }
        if (strcmp (spd_s, "NA") != 0) {
            be.has_speed = true;
            be.speed_mps = atof (spd_s);
        }
        if (strcmp (hdg_s, "NA") != 0) {
            be.has_hdg = true;
            be.hdg_deg = atof (hdg_s);
        }

        // TRACK: "lat1:lon1|lat2:lon2|..." oldest-first, capped at BALN_MAX_TRACK points.
        // Parsed with a local, non-destructive scan (unlike splitBalnCSV, this field is
        // never quoted and uses its own '|'/':' sub-delimiters, so a simple strtok-style
        // walk over a scratch copy is enough).
        be.n_track = 0;
        if (track_s[0]) {
            char tbuf[200];
            quietStrncpy (tbuf, track_s, sizeof(tbuf));
            char *save = NULL;
            for (char *tok = strtok_r (tbuf, "|", &save); tok && be.n_track < BALN_MAX_TRACK;
                                                            tok = strtok_r (NULL, "|", &save)) {
                char *colon = strchr (tok, ':');
                if (!colon)
                    continue;
                *colon = '\0';
                be.track_lat[be.n_track] = atof (tok);
                be.track_lng[be.n_track] = atof (colon+1);
                be.n_track++;
            }
        }

        if (strcmp (freq_s, "NA") != 0) {
            be.has_freq = true;
            be.freq_hz = atof (freq_s);
        }
        if (strcmp (batt_s, "NA") != 0) {
            be.has_batt = true;
            be.batt_v = atof (batt_s);
        }
        if (strcmp (temp_s, "NA") != 0) {
            be.has_temp = true;
            be.temp_c = atof (temp_s);
        }

        Serial.printf ("BALN: [%d] %s %s alt=%s @ %.4f,%.4f (%d track pts)\n", bln_ss.n_data,
                       type == BALN_HAB ? "HAB" : "PICO", be.name, alt_s, be.lat, be.lng, be.n_track);
    }

    if (bln_ss.n_data > 1)
        qsort (balloons, bln_ss.n_data, sizeof(BalloonEntry), balnLastTAscCompar);

out:
    fclose (fp);
    Serial.printf ("BALN: loaded %d balloons from %s\n", bln_ss.n_data, balloons_fn);
    return (ok);
}

// ---------------------------------------------------------------------------
// Map overlay, hover and POI menu
// ---------------------------------------------------------------------------

static void balnLL (const BalloonEntry &be, LatLong &ll)
{
    ll.lat_d = be.lat;
    ll.lng_d = be.lng;
    ll.normalize();
}

/* fill a BalloonHoverInfo from a BalloonEntry -- shared by both the map-hover and
 * pane-row-hover paths below, same idea as ActiveNetInfo/MeshInfo's fill helpers.
 */
static void fillBalnHoverInfo (const BalloonEntry &be, BalloonHoverInfo *info)
{
    quietStrncpy (info->name, be.name, sizeof(info->name));
    info->is_hab = (be.type == BALN_HAB);
    info->has_alt = be.has_alt;
    info->alt_m = be.alt_m;
    info->has_freq = be.has_freq;
    info->freq_hz = be.freq_hz;
    info->has_batt = be.has_batt;
    info->batt_v = be.batt_v;
    info->has_temp = be.has_temp;
    info->temp_c = be.temp_c;
    formatAge (be.last_t, info->age, sizeof(info->age));
    balnLL (be, info->ll);
}

/* draw the breadcrumb track (older points -> current position) for one balloon, same
 * black-outline-then-color technique as drawStormsOnMap()'s track segments, including
 * segmentSpanOkRaw() so a segment isn't drawn straight across the map in azimuthal
 * projections when consecutive points land in different hemisphere lobes.
 */
static void drawBalloonTrack (const BalloonEntry &be, uint16_t color)
{
    if (be.n_track == 0)
        return;

    const int tw = BALN_TRACK_LW * tft.SCALESZ;
    const int ow = tw + 2;

    // walk oldest -> newest, ending with a final segment into the current position
    LatLong ll_prev (be.track_lat[0], be.track_lng[0]);
    for (int j = 1; j <= be.n_track; j++) {
        LatLong ll_next = (j < be.n_track) ? LatLong (be.track_lat[j], be.track_lng[j])
                                            : LatLong (be.lat, be.lng);
        SCoord a, b;
        ll2sRaw (ll_prev, a, 1);
        ll2sRaw (ll_next, b, 1);
        if (segmentSpanOkRaw (a, b, ow/2+1)) {
            tft.drawLineRaw (a.x, a.y, b.x, b.y, ow, RA8875_BLACK);
            tft.drawLineRaw (a.x, a.y, b.x, b.y, tw, color);
        }
        ll_prev = ll_next;
    }
}

static void drawBalloonMarker (const BalloonEntry &be)
{
    LatLong ll;
    balnLL (be, ll);

    SCoord s;
    ll2s (ll, s, BALN_SPR_H);
    if (!overMap (s))
        return;

    bool stale = balloonIsStale (be);
    uint16_t track_col = stale ? dimRGB565 (BALN_COL_BODY, BALN_STALE_DIM) : BALN_COL_BODY;
    drawBalloonTrack (be, track_col);

    SCoord sr;
    ll2sRaw (ll, sr, BALN_SPR_H);

    // the icon is centered on sr and extends roughly half its size in every direction --
    // skip it (rather than let it spill past the map's edge into whatever's drawn just
    // outside, eg a corner info box) if that full footprint wouldn't stay on-map. Same
    // technique hurricane.cpp uses for its dots/tracks via rawPointClearOfMapEdge().
    int half_extent = (std::max (BALN_SPR_W, BALN_SPR_H) * tft.SCALESZ) / 2 + 1;
    if (!rawPointClearOfMapEdge (sr, half_extent))
        return;

    BalnSpriteFrame frame = (be.type == BALN_HAB) ? BALN_SPR_HAB : BALN_SPR_PICO;
    drawBalloonSprite (sr, frame, stale);
}

/* draw all balloon markers if the Balloons pane is in use; otherwise free the data
 * the same way the Launches/Storms panes do when not shown.
 */
void drawBalloonsOnMap (void)
{
    if (bln_ss.n_data == 0)
        return;

    if (findPaneForChoice (PLOT_CH_BALLOONS) == PANE_NONE) {
        freeBalloons();
        return;
    }

    for (int i = 0; i < bln_ss.n_data; i++)
        drawBalloonMarker (balloons[i]);
}

/* find the balloon nearest ll within a zoom-scaled threshold. */
static bool getNearestBalloon (const LatLong &ll, int *idx)
{
    if (bln_ss.n_data == 0)
        return false;

    float thresh = 3.0f / pan_zoom.zoom;
    float best   = thresh;
    int   best_i = -1;
    float clat   = cosf (ll.lat);

    for (int i = 0; i < bln_ss.n_data; i++) {
        float dlat = ll.lat_d - balloons[i].lat;
        float dlng = (ll.lng_d - balloons[i].lng) * clat;
        float d = sqrtf (dlat*dlat + dlng*dlng);
        if (d < best) {
            best = d;
            best_i = i;
        }
    }

    if (best_i >= 0) {
        if (idx)
            *idx = best_i;
        return true;
    }
    return false;
}

/* return a BalloonHoverInfo (+ position) if the map cursor at ll is over a balloon.
 * mirrors getClosestActiveNet()/getClosestMeshtasticNode().
 */
bool getClosestBalloon (const LatLong &ll, LatLong *mark_ll, BalloonHoverInfo *info)
{
    int i;
    if (!getNearestBalloon (ll, &i))
        return false;
    fillBalnHoverInfo (balloons[i], info);
    *mark_ll = info->ll;
    return true;
}

/* if ms is hovering a balloon row in the Balloons pane, hand back that balloon's
 * position and a full BalloonHoverInfo. mirrors getActiveNetsPaneInfo()/
 * getMeshtasticPaneInfo() -- same structured-info-box treatment as Nets/Mesh, rather
 * than the simpler ring+one-line-label style Storms/Launches use, since a balloon's
 * frequency/battery/temperature are worth their own labeled lines.
 */
bool getBalloonPaneInfo (const SCoord &ms, LatLong *mark_ll, BalloonHoverInfo *info)
{
    if (bln_ss.n_data == 0)
        return false;

    PlotPane pp = findPaneChoiceNow (PLOT_CH_BALLOONS);
    if (pp == PANE_NONE)
        return false;
    const SBox &box = plot_b[pp];
    if (!inBox (ms, box) || ms.y < box.y + BALN_START_DY)      // not over the balloon-rows region
        return false;

    // same technique as hurricane.cpp's getStormPaneHover(): convert the mouse y to a
    // display row, then let ScrollState invert that back to a data array index, rather
    // than looping and re-deriving each row's box the other way around -- one fewer
    // place for the two directions of that math to quietly drift apart.
    int item = (ms.y - box.y - BALN_START_DY) / BALN_ROW_H;
    int index;
    if (!bln_ss.findDataIndex (item, index) || index < 0 || index >= bln_ss.n_data)
        return false;

    fillBalnHoverInfo (balloons[index], info);
    *mark_ll = info->ll;
    return true;
}

/* if ll is over a balloon, fill the three menu label lines and hand back its tracker
 * URL for the map popup menu. mirrors getLaunchMapMenuInfo()/getStormMapMenuInfo().
 */
bool getBalloonMapMenuInfo (const LatLong &ll, char *l1, size_t l1n, char *l2, size_t l2n,
        char *l3, size_t l3n, const char **url)
{
    int i;
    if (!getNearestBalloon (ll, &i))
        return false;
    const BalloonEntry &be = balloons[i];

    snprintf (l1, l1n, "%s (%s)", be.name, be.type == BALN_HAB ? "HAB" : "Pico");
    if (be.has_alt)
        snprintf (l2, l2n, "Alt %.0fm", be.alt_m);
    else
        quietStrncpy (l2, be.callsign, l2n);

    char agebuf[24];
    formatAge (be.last_t, agebuf, sizeof(agebuf));
    snprintf (l3, l3n, "Heard %s ago", agebuf);

    if (url)
        *url = (be.url && be.url[0]) ? be.url : NULL;
    return true;
}

// ---------------------------------------------------------------------------
// Public update function (called from wifi.cpp)
// ---------------------------------------------------------------------------

bool updateBalloons (const SBox &box, bool fresh)
{
    static time_t next_fetch;

    bool ok = true;

    if (fresh || myNow() >= next_fetch) {
        ok = retrieveBalloons (box);
        if (ok) {
            next_fetch = myNow() + BALLOONS_MAXAGE;
            bln_ss.scrollToNewest();
            fresh = true;
        }
    }

    if (ok) {
        if (fresh)
            drawBalloonsPane (box);
    } else {
        plotMessage (box, BALN_TITLE_COLOR, "Balloons error");
    }

    return (ok);
}
