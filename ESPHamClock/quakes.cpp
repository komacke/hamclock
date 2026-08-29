/* quakes.cpp -- USGS earthquake overlay for HamClock
 *
 * Fetches recent earthquakes from the OHB backend (fetch_quakes.py) and displays them on the
 * map and in a scrollable pane, following the same OHB-proxy pattern as marinewarnings.cpp and
 * firewx.cpp: the backend does the real polling of earthquake.usgs.gov and hands the client a
 * small pre-reduced flat file (the client has no TLS stack, so it can't reach USGS directly).
 *
 * Structurally different from the Marine/Fire warning panes in three ways:
 *   - no polygon/zone -- just an epicenter point, so severity is continuous (magnitude) rather
 *     than a fixed color per product type
 *   - no expiry -- a quake is an instant, not a time-bounded hazard, so the list shows AGE
 *     ("8m ago") and rows get pruned once older than QUAKE_MAXAGE_SHOWN rather than at a
 *     per-event expires time
 *   - "local" combines distance from DE with magnitude: a small quake only counts nearby, but
 *     a big one is worth knowing about from much farther away
 *
 * Data format from /quakes/quakes.txt -- one line per event, comma-separated, 7 fields:
 *   ID,TIME,MAG,DEPTH_KM,LAT,LON,PLACE
 * where PLACE is the USGS feed's human-readable "place" field (e.g. "10km SW of Anza, CA");
 * backend sanitizes commas out of it.
 *
 * Auto-popup defaults OFF and, when enabled, only fires for QUAKE_AUTOPOP_MIN_MAG+ within
 * QUAKE_AUTOPOP_MI miles -- most quakes are FYI, not the kind of "can't miss this" event a
 * marine or fire warning is.
 *
 * Pane: PLOT_CH_QUAKES   (scrollable list, follows the Marine Wx / Fire Wx pane pattern)
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define QUAKE_MAXAGE          (60*5)     // cache max age, secs -- poll every 5 min
#define QUAKE_MINSIZ          1
#define QUAKE_MAXQ            30         // max simultaneous events kept
#define QUAKE_IDLEN           24
#define QUAKE_PLACELEN        80
#define QUAKE_MAXAGE_SHOWN    (24*3600)  // drop events older than this from the list entirely

#define QUAKE_LOCAL_MI_SMALL  100        // any quake at all counts local within this radius
#define QUAKE_LOCAL_MAG_MED   3.0f       // M3.0+ counts local within QUAKE_LOCAL_MI_MED
#define QUAKE_LOCAL_MI_MED    300
#define QUAKE_LOCAL_MAG_BIG   5.0f       // M5.0+ counts local within QUAKE_LOCAL_MI_BIG
#define QUAKE_LOCAL_MI_BIG    1000

#define QUAKE_AUTOPOP_HOLD    300        // secs to hold a forced popup before restoring
#define QUAKE_AUTOPOP_PANE    PANE_1     // same target as Marine/Fire Wx auto-popup
#define QUAKE_AUTOPOP_MIN_MAG 5.0f       // auto-popup only for genuinely notable quakes
#define QUAKE_AUTOPOP_MI      500

#define QUAKE_TITLE_Y0        PANETITLE_H
#define QUAKE_START_DY        36
#define QUAKE_ROW_H           58
#define QUAKE_ROW_PAD         8
#define QUAKE_MAP_MAXR         34        // outer ripple radius on the map -- generous, map has room
#define QUAKE_PANE_MAXR        24        // outer ripple radius in the pane -- capped so this plus
                                          // the icon's center placement can never reach the text
#define QUAKE_ICON_SZ          (QUAKE_PANE_MAXR*2 + 6)   // pane icon column, sized exactly to fit it
#define QUAKE_ICON_MARGIN     8
#define QUAKE_TEXT_LINE_H     12
#define QUAKE_PLACE_MAXLINES  3
#define QUAKE_TITLE_RSV       28
#define QUAKE_ACCENT_W        4
#define QUAKE_BADGE_PAD       3

static const char quake_page[] = "/quakes/quakes.txt";
static const char quake_fn[]   = "quakes.txt";

#define QUAKE_COLOR_BIG    RGB565(255,50,50)     // M5.0+  -- red
#define QUAKE_COLOR_MED    RGB565(255,170,0)     // M3.0-4.9 -- amber
#define QUAKE_COLOR_SMALL  RGB565(150,150,150)   // <M3.0 -- gray
#define QUAKE_COLOR_TITLE  RA8875_WHITE
#define QUAKE_COLOR_HINT   RGB565(110,110,110)

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

typedef struct {
    char     id[QUAKE_IDLEN];
    time_t   qtime;                    // event time (epoch)
    float    mag;
    float    depth_km;
    LatLong  center;
    char     place[QUAKE_PLACELEN];
    bool     is_local;
} QuakeEvent;

static QuakeEvent quake_ev[QUAKE_MAXQ];

// per-drawn-marker hit-test boxes for click-to-popup on the map, rebuilt every drawQuakesOnMap()
static SBox quake_map_btn[QUAKE_MAXQ];
static int  quake_map_idx[QUAKE_MAXQ];   // quake_map_btn[i] -> quake_ev[quake_map_idx[i]]
static int  n_quake_map_btn = 0;

// map-centering state for the "return to previous view" button -- tap a quake row to pan/zoom
// the map to its epicenter; tap it again (or its reset button) to restore the prior view.
// Same pattern as hurricane.cpp's storm_pz_saved/storm_saved_pz/storm_centered_idx.
static int      quake_centered_idx = -1;
static bool     quake_pz_saved = false;
static PanZoom  quake_saved_pz;
#define QUAKE_RESET_W   18

static SBox quakeResetBox (const SBox &box, uint16_t row_y)
{
    SBox b;
    b.w = QUAKE_RESET_W;
    b.h = QUAKE_RESET_W;
    b.x = box.x + box.w - QUAKE_RESET_W - 3;
    b.y = row_y + 3;
    return b;
}

/* small "restore previous map view" button, identical convention to hurricane.cpp's */
static void drawQuakeResetButton (const SBox &b)
{
    tft.fillRect (b.x, b.y, b.w, b.h, RA8875_BLACK);
    tft.drawRect (b.x, b.y, b.w, b.h, GRAY);

    uint16_t cx = b.x + b.w/2;
    uint16_t cy = b.y + b.h/2;
    int rr = b.w/2 - 4;
    if (rr < 2)
        rr = 2;
    tft.drawCircle (cx, cy, rr, RA8875_WHITE);
    tft.fillRect (cx+rr-1, cy-1, 3, 3, RA8875_BLACK);
    tft.fillTriangle (cx+rr-2, cy-rr+1, cx+rr+2, cy-rr+1,
                      cx+rr,   cy-rr+4, RA8875_WHITE);
}


static int   n_quake = 0;
static uint32_t quake_prev_refresh;

static ScrollState quake_ss;

static uint8_t  quake_on       = 1;
static uint8_t  quake_autopop  = 0;      // default OFF -- most quakes are FYI, not urgent
static uint16_t quake_radius_mi = QUAKE_LOCAL_MI_SMALL;   // small-quake fallback radius, user-tunable

static bool       quake_forced_active = false;
static PlotChoice quake_prev_pane_ch  = PLOT_CH_NONE;
static PlotMask   quake_prev_pane_rotset = 0;
static time_t     quake_autopop_until = 0;

static char quake_seen_ids[QUAKE_MAXQ][QUAKE_IDLEN];
static int  n_quake_seen = 0;

#define QUAKE_DETAIL_MAXLEN    400
#define QUAKE_DETAIL_MAXLINES  40
static bool     quake_detail_showing = false;
static char     quake_detail_text[QUAKE_DETAIL_MAXLEN];
static uint16_t quake_detail_color;
static char     quake_detail_lines[QUAKE_DETAIL_MAXLINES][64];
static int      quake_detail_n_lines = 0;
static ScrollState quake_detail_ss;

// ---------------------------------------------------------------------------
// Geometry / severity helpers
// ---------------------------------------------------------------------------

static float quakeMilesFromDE (const LatLong &ll)
{
    LatLong de = de_ll;
    LatLong to = ll;
    return de.GSD (to) * ERAD_M;
}

/* combines distance from DE with magnitude -- small quakes only count nearby, big ones count
 * from much farther away. quake_radius_mi is the user-tunable radius for the "any magnitude"
 * tier; the medium/big tiers scale up from there proportionally to keep them all relatable to
 * one adjustable setting.
 */
static bool computeQuakeIsLocal (const QuakeEvent &q)
{
    float mi = quakeMilesFromDE (q.center);
    if (q.mag >= QUAKE_LOCAL_MAG_BIG)
        return mi <= (quake_radius_mi * (QUAKE_LOCAL_MI_BIG / (float)QUAKE_LOCAL_MI_SMALL));
    if (q.mag >= QUAKE_LOCAL_MAG_MED)
        return mi <= (quake_radius_mi * (QUAKE_LOCAL_MI_MED / (float)QUAKE_LOCAL_MI_SMALL));
    return mi <= quake_radius_mi;
}

static uint16_t quakeSeverityColor (float mag)
{
    if (mag >= QUAKE_LOCAL_MAG_BIG)
        return QUAKE_COLOR_BIG;
    if (mag >= QUAKE_LOCAL_MAG_MED)
        return QUAKE_COLOR_MED;
    return QUAKE_COLOR_SMALL;
}

// ---------------------------------------------------------------------------
// Text wrapping (identical approach to marinewarnings.cpp/firewx.cpp)
// ---------------------------------------------------------------------------

static int quakeWrapText (const char *src, uint16_t maxw, char lines[][48], int max_lines)
{
    int n_lines = 0;
    char cur[48] = "";
    const char *p = src;

    while (*p && n_lines < max_lines) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *sp = strchr (p, ' ');
        size_t wlen = sp ? (size_t)(sp - p) : strlen (p);
        if (wlen > sizeof(cur) - 2)
            wlen = sizeof(cur) - 2;

        char trial[48];
        if (cur[0])
            snprintf (trial, sizeof(trial), "%s %.*s", cur, (int)wlen, p);
        else
            snprintf (trial, sizeof(trial), "%.*s", (int)wlen, p);

        if (!cur[0] || getTextWidth (trial) <= maxw) {
            quietStrncpy (cur, trial, sizeof(cur));
            p += wlen;
        } else {
            quietStrncpy (lines[n_lines], cur, 48);
            maxStringW (lines[n_lines], maxw);
            n_lines++;
            cur[0] = '\0';
        }
    }

    if (cur[0] && n_lines < max_lines) {
        quietStrncpy (lines[n_lines], cur, 48);
        maxStringW (lines[n_lines], maxw);
        n_lines++;
    } else if (*p && n_lines == max_lines) {
        char *last = lines[n_lines-1];
        size_t l = strlen (last);
        if (l < 45) { last[l] = '.'; last[l+1] = '.'; last[l+2] = '.'; last[l+3] = '\0'; }
        maxStringW (last, maxw);
    }

    return n_lines;
}

static int quakeWrapFullText (const char *src, uint16_t maxw, char lines[][64], int max_lines)
{
    int n_lines = 0;
    char cur[64] = "";
    const char *p = src;

    while (*p && n_lines < max_lines) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        const char *sp = strchr (p, ' ');
        size_t wlen = sp ? (size_t)(sp - p) : strlen (p);
        if (wlen > sizeof(cur) - 2)
            wlen = sizeof(cur) - 2;

        char trial[64];
        if (cur[0])
            snprintf (trial, sizeof(trial), "%s %.*s", cur, (int)wlen, p);
        else
            snprintf (trial, sizeof(trial), "%.*s", (int)wlen, p);

        if (!cur[0] || getTextWidth (trial) <= maxw) {
            quietStrncpy (cur, trial, sizeof(cur));
            p += wlen;
        } else {
            quietStrncpy (lines[n_lines], cur, 64);
            maxStringW (lines[n_lines], maxw);
            n_lines++;
            cur[0] = '\0';
        }
    }

    if (cur[0] && n_lines < max_lines) {
        quietStrncpy (lines[n_lines], cur, 64);
        maxStringW (lines[n_lines], maxw);
        n_lines++;
    }

    return n_lines;
}

// ---------------------------------------------------------------------------
// Glyph -- concentric "ripple" sized by magnitude, distinct from the warning-triangle
// (Marine) and flame (Fire) glyphs since severity here is continuous, not a fixed product type
// ---------------------------------------------------------------------------

static void drawQuakeGlyph (uint16_t cx, uint16_t cy, float mag, uint16_t color, uint16_t max_outer,
                            bool raw = false)
{
    float clamped = mag < 1 ? 1 : (mag > 7 ? 7 : mag);
    uint16_t max_base = max_outer > 8 ? max_outer - 8 : 3;
    uint16_t r = (uint16_t)(4 + (clamped - 1) * (max_base > 4 ? max_base - 4 : 1) / 6);

    if (raw) {
        tft.fillCircleRaw (cx, cy, r, color);
        tft.drawCircleRaw (cx, cy, r, RA8875_BLACK);
        if (r > 6) {
            tft.drawCircleRaw (cx, cy, r + 4, color);
            tft.drawCircleRaw (cx, cy, r + 8, color);
        }
    } else {
        tft.fillCircle (cx, cy, r, color);
        tft.drawCircle (cx, cy, r, RA8875_BLACK);
        if (r > 6) {
            tft.drawCircle (cx, cy, r + 4, color);
            tft.drawCircle (cx, cy, r + 8, color);
        }
    }
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

static bool parseQuakesFile (FILE *fp)
{
    n_quake = 0;
    for (int i = 0; i < QUAKE_MAXQ; i++)
        quake_ev[i] = QuakeEvent{};
    quake_ss.init (QUAKE_MAXQ, 0, 0, ScrollState::DIR_TOPDOWN);

    // the array is rebuilt (and re-sorted) on every fetch, so a previously-centered index could
    // now point at a different quake -- clear it rather than risk the return button misattaching
    // to the wrong row. Deliberately NOT restoring the map view here: yanking the user's pan/zoom
    // back mid-read on a routine 5-min refresh would be worse than a return button that's
    // temporarily gone until they tap a row again.
    quake_centered_idx = -1;

    time_t cutoff = myNow() - QUAKE_MAXAGE_SHOWN;
    char line[300];
    while (fgets (line, sizeof(line), fp) && n_quake < QUAKE_MAXQ) {

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char id[QUAKE_IDLEN], place[QUAKE_PLACELEN];
        long qtime;
        float mag, depth, lat, lon;

        int n = sscanf (line, "%23[^,],%ld,%f,%f,%f,%f,%79[^\n\r]",
                        id, &qtime, &mag, &depth, &lat, &lon, place);
        if (n < 6)
            continue;
        if ((time_t)qtime < cutoff)
            continue;                          // too old, drop from the list entirely

        QuakeEvent *q = &quake_ev[n_quake++];
        quietStrncpy (q->id, id, sizeof(q->id));
        q->qtime    = (time_t)qtime;
        q->mag      = mag;
        q->depth_km = depth;
        q->center   = LatLong (lat, lon);
        quietStrncpy (q->place, n == 7 ? place : "", sizeof(q->place));
        q->is_local = computeQuakeIsLocal (*q);
    }

    // local first, then by magnitude descending within each group
    for (int i = 0; i < n_quake-1; i++) {
        for (int j = i+1; j < n_quake; j++) {
            bool swap = false;
            if (quake_ev[j].is_local && !quake_ev[i].is_local)
                swap = true;
            else if (quake_ev[j].is_local == quake_ev[i].is_local && quake_ev[j].mag > quake_ev[i].mag)
                swap = true;
            if (swap) {
                QuakeEvent tmp = quake_ev[i];
                quake_ev[i] = quake_ev[j];
                quake_ev[j] = tmp;
            }
        }
    }

    quake_ss.n_data = n_quake;
    quake_ss.scrollToNewest();
    Serial.printf ("QUAKE: parsed %d events\n", n_quake);
    return true;
}

// ---------------------------------------------------------------------------
// Auto-popup
// ---------------------------------------------------------------------------

static bool quakeAlreadySeen (const char *id)
{
    for (int i = 0; i < n_quake_seen; i++)
        if (strcmp (quake_seen_ids[i], id) == 0)
            return true;
    return false;
}

static void quakeRememberSeen (const char *id)
{
    if (n_quake_seen >= QUAKE_MAXQ) {
        memmove (quake_seen_ids[0], quake_seen_ids[1], (QUAKE_MAXQ-1) * QUAKE_IDLEN);
        n_quake_seen = QUAKE_MAXQ - 1;
    }
    quietStrncpy (quake_seen_ids[n_quake_seen++], id, QUAKE_IDLEN);
}

static void restoreQuakeForcedPane (void)
{
    if (!quake_forced_active)
        return;
    quake_forced_active = false;
    ROTHOLD_CLR (PLOT_CH_QUAKES);
    if (plot_ch[QUAKE_AUTOPOP_PANE] == PLOT_CH_QUAKES) {
        PlotChoice restore_ch = quake_prev_pane_ch;
        PlotMask   restore_rs = quake_prev_pane_rotset;
        if (restore_ch == PLOT_CH_NONE) {
            restore_ch = getAnyAvailableChoice();
            restore_rs = restore_ch != PLOT_CH_NONE ? PLOTBIT (restore_ch) : 0;
        }
        if (restore_ch != PLOT_CH_NONE) {
            plot_rotset[QUAKE_AUTOPOP_PANE] = restore_rs;
            setPlotChoice (QUAKE_AUTOPOP_PANE, restore_ch);
        }
    }
    quake_prev_pane_ch = PLOT_CH_NONE;
    quake_prev_pane_rotset = 0;
}

static void checkQuakeAutoPopup (void)
{
    if (quake_forced_active && myNow() >= quake_autopop_until)
        restoreQuakeForcedPane();

    if (!quake_on || !quake_autopop)
        return;

    for (int i = 0; i < n_quake; i++) {
        const QuakeEvent &q = quake_ev[i];
        if (quakeAlreadySeen (q.id))
            continue;
        if (q.mag < QUAKE_AUTOPOP_MIN_MAG || quakeMilesFromDE (q.center) > QUAKE_AUTOPOP_MI)
            continue;

        quakeRememberSeen (q.id);

        if (!quake_forced_active) {
            quake_prev_pane_ch     = plot_ch[QUAKE_AUTOPOP_PANE];
            quake_prev_pane_rotset = plot_rotset[QUAKE_AUTOPOP_PANE];
            quake_forced_active = true;
        }
        plot_rotset[QUAKE_AUTOPOP_PANE] = PLOTBIT (PLOT_CH_QUAKES);
        ROTHOLD_SET (PLOT_CH_QUAKES);
        setPlotChoice (QUAKE_AUTOPOP_PANE, PLOT_CH_QUAKES);
        quake_autopop_until = myNow() + QUAKE_AUTOPOP_HOLD;

        Serial.printf ("QUAKE: M%.1f event %s forced onto pane %d\n", q.mag, q.id,
                       (int)QUAKE_AUTOPOP_PANE);
    }
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

static bool retrieveQuakes (void)
{
    FILE *fp = openCachedFile (quake_fn, quake_page, QUAKE_MAXAGE, QUAKE_MINSIZ);
    if (!fp) {
        Serial.printf ("QUAKE: failed to open %s\n", quake_fn);
        return false;
    }
    bool ok = parseQuakesFile (fp);
    fclose (fp);
    if (ok)
        checkQuakeAutoPopup();
    else
        Serial.printf ("QUAKE: parse failed\n");
    return ok;
}

bool checkQuakesData (void)
{
    if (!quake_on)
        return true;
    if (quake_prev_refresh != 0 && !timesUp (&quake_prev_refresh, (uint32_t)QUAKE_MAXAGE * 1000))
        return true;
    quake_prev_refresh = millis();
    return retrieveQuakes();
}

void drawQuakesPane (const SBox &box);   // fwd decl, used by updateQuakes() below

bool updateQuakes (const SBox &box, bool fresh)
{
    bool ok;
    if (fresh) {
        quake_prev_refresh = millis();
        ok = retrieveQuakes();
    } else {
        ok = checkQuakesData();
    }

    if (quake_detail_showing)
        return ok;

    if (ok)
        drawQuakesPane (box);
    else
        plotMessage (box, QUAKE_COLOR_BIG, "Quake data unavailable");

    return ok;
}

// ---------------------------------------------------------------------------
// Tap-to-view full text
// ---------------------------------------------------------------------------

static void quakeSanitizeIdForPath (const char *id, char *out, size_t out_len)
{
    size_t n = 0;
    for (const char *p = id; *p && n < out_len-1; p++)
        out[n++] = (isalnum ((unsigned char)*p) || *p=='.' || *p=='_' || *p=='-') ? *p : '_';
    out[n] = '\0';
}

#define QUAKE_DETAIL_BOTTOM_MARGIN  10

static int computeQuakeDetailMaxVis (const SBox &box)
{
    uint16_t usable_h = box.h > QUAKE_START_DY + QUAKE_DETAIL_BOTTOM_MARGIN
                       ? box.h - QUAKE_START_DY - QUAKE_DETAIL_BOTTOM_MARGIN : QUAKE_TEXT_LINE_H;
    int max_vis = (int)usable_h / QUAKE_TEXT_LINE_H;
    return max_vis < 1 ? 1 : max_vis;
}

static void showQuakeDetail (const SBox &box, const QuakeEvent &q)
{
    char safe_id[QUAKE_IDLEN];
    quakeSanitizeIdForPath (q.id, safe_id, sizeof(safe_id));

    char det_page[100];
    snprintf (det_page, sizeof(det_page), "/quakes/detail/%s.txt", safe_id);
    char det_fn[60];
    snprintf (det_fn, sizeof(det_fn), "qdet_%s.txt", safe_id);

    quake_detail_color = quakeSeverityColor (q.mag);

    FILE *fp = openCachedFile (det_fn, det_page, CACHE_FOREVER, 1);
    if (fp) {
        size_t n = fread (quake_detail_text, 1, sizeof(quake_detail_text)-1, fp);
        quake_detail_text[n] = '\0';
        fclose (fp);
    } else {
        quietStrncpy (quake_detail_text, "Full text unavailable for this event.",
                      sizeof(quake_detail_text));
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t wrap_w = box.w > 8 ? box.w - 8 : box.w;
    quake_detail_n_lines = quakeWrapFullText (quake_detail_text, wrap_w,
                                              quake_detail_lines, QUAKE_DETAIL_MAXLINES);
    int max_vis = computeQuakeDetailMaxVis (box);
    quake_detail_ss.init (QUAKE_DETAIL_MAXLINES, max_vis - 1, quake_detail_n_lines,
                          ScrollState::DIR_BOTUP);

    quake_detail_showing = true;
    drawQuakesPane (box);
}

// ---------------------------------------------------------------------------
// Pane drawing
// ---------------------------------------------------------------------------

static void quakeScrollStepDown (ScrollState &ss)
{
    if (ss.max_vis > 1) { ss.scrollDown(); return; }
    int new_top = ss.top_vis - 1;
    if (new_top < ss.max_vis - 1) new_top = ss.max_vis - 1;
    ss.top_vis = new_top;
}

static void quakeScrollStepUp (ScrollState &ss)
{
    if (ss.max_vis > 1) { ss.scrollUp(); return; }
    int new_top = ss.top_vis + 1;
    if (new_top > ss.n_data - 1) new_top = ss.n_data - 1;
    ss.top_vis = new_top;
}

static void drawQuakeDetailView (const SBox &box)
{
    prepPlotBox (box);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (quake_detail_color);
    char title[20] = "Full Text";
    uint16_t avail_r = box.w > QUAKE_TITLE_RSV ? box.w - QUAKE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w > tw ? (box.w - tw)/2 : 2), box.y + QUAKE_TITLE_Y0);
    tft.print (title);

    quake_detail_ss.max_vis = computeQuakeDetailMaxVis (box);
    quake_detail_ss.drawScrollUpControl (box, QUAKE_COLOR_TITLE, QUAKE_COLOR_TITLE);
    quake_detail_ss.drawScrollDownControl (box, QUAKE_COLOR_TITLE, QUAKE_COLOR_TITLE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (quake_detail_color);
    int min_i, max_i;
    quake_detail_ss.getVisDataIndices (min_i, max_i);
    uint16_t y = box.y + QUAKE_START_DY + QUAKE_TEXT_LINE_H;
    for (int i = min_i; i <= max_i && i < quake_detail_n_lines; i++) {
        tft.setCursor (box.x + 4, y);
        tft.print (quake_detail_lines[i]);
        y += QUAKE_TEXT_LINE_H;
    }
}

void drawQuakesPane (const SBox &box)
{
    if (quake_detail_showing) {
        drawQuakeDetailView (box);
        return;
    }

    prepPlotBox (box);

    bool has_local = false;
    for (int i = 0; i < n_quake; i++)
        if (quake_ev[i].is_local) { has_local = true; break; }

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (has_local ? QUAKE_COLOR_BIG : QUAKE_COLOR_TITLE);
    char title[20] = "Quakes";
    uint16_t avail_r = box.w > QUAKE_TITLE_RSV ? box.w - QUAKE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    uint16_t tx = box.w > tw ? (box.w - tw)/2 : 2;
    if (tx + tw > avail_r)
        tx = avail_r > tw ? avail_r - tw : 2;
    tft.setCursor (box.x + tx, box.y + QUAKE_TITLE_Y0);
    tft.print (title);

    int max_vis = (int)(box.h - QUAKE_START_DY) / (QUAKE_ROW_H + QUAKE_ROW_PAD);
    if (max_vis < 1)
        max_vis = 1;
    quake_ss.max_vis = max_vis;
    quake_ss.n_data  = n_quake;

    quake_ss.drawScrollUpControl (box, QUAKE_COLOR_TITLE, QUAKE_COLOR_TITLE);
    quake_ss.drawScrollDownControl (box, QUAKE_COLOR_TITLE, QUAKE_COLOR_TITLE);

    if (n_quake == 0) {
        uint16_t r = box.w/8;
        if (r > (box.h - QUAKE_START_DY)/4) r = (box.h - QUAKE_START_DY)/4;
        if (r < 8) r = 8;
        uint16_t cx = box.x + box.w/2;
        uint16_t cy = box.y + QUAKE_START_DY + r + 4;
        drawQuakeGlyph (cx, cy, 2.0f, RGB565(60,190,100), r);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (QUAKE_COLOR_HINT);
        char msg[20] = "No recent";
        char msg2[20] = "quakes";
        maxStringW (msg, box.w - 4);
        maxStringW (msg2, box.w - 4);
        uint16_t w1 = getTextWidth (msg), w2 = getTextWidth (msg2);
        uint16_t ty = cy + r + 14;
        tft.setCursor (box.x + (box.w > w1 ? (box.w-w1)/2 : 2), ty);
        tft.print (msg);
        tft.setCursor (box.x + (box.w > w2 ? (box.w-w2)/2 : 2), ty + 22);
        tft.print (msg2);
        return;
    }

    int min_i, max_i;
    quake_ss.getVisDataIndices (min_i, max_i);
    uint16_t row_y = box.y + QUAKE_START_DY;
    uint16_t text_x = box.x + QUAKE_ACCENT_W + QUAKE_ICON_SZ + QUAKE_ICON_MARGIN + 2;
    uint16_t text_w = box.w > (QUAKE_ACCENT_W + QUAKE_ICON_SZ + QUAKE_ICON_MARGIN + 8)
                        ? box.w - (QUAKE_ACCENT_W + QUAKE_ICON_SZ + QUAKE_ICON_MARGIN + 8) : box.w/2;

    for (int i = min_i; i <= max_i && i < n_quake; i++) {
        const QuakeEvent &q = quake_ev[i];
        uint16_t color = quakeSeverityColor (q.mag);

        if ((i - min_i) % 2 == 1)
            tft.fillRect (box.x + QUAKE_ACCENT_W, row_y - 2, box.w - QUAKE_ACCENT_W,
                          QUAKE_ROW_H, RGB565(18,18,18));
        tft.fillRect (box.x, row_y - 2, QUAKE_ACCENT_W, QUAKE_ROW_H, color);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        char pl_lines[QUAKE_PLACE_MAXLINES][48];
        int n_pl = quakeWrapText (q.place[0] ? q.place : "Unknown location", text_w,
                                  pl_lines, QUAKE_PLACE_MAXLINES);
        uint16_t content_h = QUAKE_TEXT_LINE_H * n_pl + 14;
        uint16_t icon_cy = row_y + (content_h > QUAKE_ICON_SZ*2 ? content_h/2 : QUAKE_ICON_SZ);

        drawQuakeGlyph (box.x + QUAKE_ACCENT_W + QUAKE_ICON_SZ/2, icon_cy, q.mag, color, QUAKE_PANE_MAXR);

        selectFontStyle (BOLD_FONT, FAST_FONT);
        tft.setTextColor (color);
        uint16_t line_y = row_y + QUAKE_TEXT_LINE_H;
        for (int l = 0; l < n_pl; l++) {
            tft.setCursor (text_x, line_y);
            tft.print (pl_lines[l]);
            line_y += QUAKE_TEXT_LINE_H;
        }

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        time_t age = myNow() > q.qtime ? myNow() - q.qtime : 0;
        const char *badge = "LOCAL";
        uint16_t badge_w = getTextWidth (badge) + 2*QUAKE_BADGE_PAD;
        uint16_t badge_reserve = q.is_local ? badge_w + 6 : 0;

        char det[56];
        if (age < 3600)
            snprintf (det, sizeof(det), "M%.1f  %.0fkm  %ldm", q.mag, q.depth_km, (long)(age/60));
        else if (age < 86400)
            snprintf (det, sizeof(det), "M%.1f  %.0fkm  %ldh", q.mag, q.depth_km, (long)(age/3600));
        else
            snprintf (det, sizeof(det), "M%.1f  %.0fkm  %ldd", q.mag, q.depth_km, (long)(age/86400));
        tft.setTextColor (QUAKE_COLOR_HINT);
        uint16_t det_w = text_w > badge_reserve ? text_w - badge_reserve : (text_w > 10 ? text_w : 10);
        maxStringW (det, det_w);
        uint16_t det_y = line_y + 2;
        tft.setCursor (text_x, det_y);
        tft.print (det);

        if (q.is_local) {
            uint16_t bx = box.x + box.w - badge_w - 3;
            uint16_t by = det_y - 9;
            tft.fillRect (bx, by, badge_w, 12, QUAKE_COLOR_BIG);
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (bx + QUAKE_BADGE_PAD, by + 9);
            tft.print (badge);
        }

        if (quake_pz_saved && i == quake_centered_idx)
            drawQuakeResetButton (quakeResetBox (box, row_y));

        row_y += QUAKE_ROW_H + QUAKE_ROW_PAD;

        if (i < max_i && i < n_quake - 1)
            tft.drawLine (box.x + QUAKE_ACCENT_W + 2, row_y - QUAKE_ROW_PAD/2,
                          box.x + box.w - 2, row_y - QUAKE_ROW_PAD/2, RGB565(45,45,45));
    }

    if (has_local) {
        tft.drawRect (box.x, box.y, box.w, box.h, QUAKE_COLOR_BIG);
        tft.drawRect (box.x+1, box.y+1, box.w-2, box.h-2, QUAKE_COLOR_BIG);
    }
}

// ---------------------------------------------------------------------------
// Map overlay
// ---------------------------------------------------------------------------

/* small blocking info popup for a tapped quake marker, same convention as planets.cpp's
 * showPlanetPopup(): draw box, flush, block until the user taps again to dismiss, redraw.
 */
static void showQuakePopup (int idx, const SCoord &s)
{
    if (idx < 0 || idx >= n_quake)
        return;
    const QuakeEvent &q = quake_ev[idx];
    uint16_t color = quakeSeverityColor (q.mag);

    finishMapSweepNow();

    SBox popup_b;
    popup_b.w = 160;
    popup_b.h = 74;
    popup_b.x = s.x;
    popup_b.y = s.y;
    if (popup_b.x + popup_b.w > tft.width())
        popup_b.x = tft.width() - popup_b.w;
    if (popup_b.y + popup_b.h > tft.height())
        popup_b.y = tft.height() - popup_b.h;

    fillSBox (popup_b, RA8875_BLACK);
    drawSBox (popup_b, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (color);
    char buf[48];
    snprintf (buf, sizeof(buf), "M%.1f  %.0fkm deep", q.mag, q.depth_km);
    tft.setCursor (popup_b.x+4, popup_b.y+2);
    tft.print (buf);

    tft.setTextColor (RA8875_WHITE);
    char pl_lines[2][48];
    int n_pl = quakeWrapText (q.place[0] ? q.place : "Unknown location", popup_b.w-8, pl_lines, 2);
    uint16_t y = popup_b.y + 16;
    for (int l = 0; l < n_pl; l++) {
        tft.setCursor (popup_b.x+4, y);
        tft.print (pl_lines[l]);
        y += 12;
    }

    time_t age = myNow() > q.qtime ? myNow() - q.qtime : 0;
    tft.setTextColor (QUAKE_COLOR_HINT);
    tft.setCursor (popup_b.x+4, y+4);
    if (age < 3600)
        snprintf (buf, sizeof(buf), "%ldm ago", (long)(age/60));
    else if (age < 86400)
        snprintf (buf, sizeof(buf), "%ldh ago", (long)(age/3600));
    else
        snprintf (buf, sizeof(buf), "%ldd ago", (long)(age/86400));
    tft.print (buf);

    tft.drawPR();

    drainTouch();
    UserInput dismiss_ui = {
        map_b, UI_UFuncNone, UF_UNUSED, UI_NOTIMEOUT, UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };
    waitForUser (dismiss_ui);

    redrawMapBox (popup_b);
}

/* check for a tap on a quake marker on the map. return whether handled. called from the same
 * dispatch chain as checkSatMapTouch()/checkPlanetMapTouch() in ESPHamClock.cpp.
 */
bool checkQuakeMapTouch (const SCoord &s)
{
    if (!quake_on)
        return false;

    for (int i = 0; i < n_quake_map_btn; i++) {
        if (inBox (s, quake_map_btn[i])) {
            showQuakePopup (quake_map_idx[i], s);
            return true;
        }
    }
    return false;
}

void drawQuakesOnMap (void)
{
    n_quake_map_btn = 0;

    if (n_quake == 0 || !quake_on)
        return;

    // only draw while the Quakes pane is actually shown somewhere -- background tracking
    // still runs regardless, so a genuinely local/notable quake will force itself onto a pane
    // (if auto-popup is on) and appear here anyway; this just stops markers from lingering
    // after the user switches away from the pane.
    if (findPaneForChoice (PLOT_CH_QUAKES) == PANE_NONE)
        return;

    for (int i = 0; i < n_quake; i++) {
        const QuakeEvent &q = quake_ev[i];
        uint16_t color = quakeSeverityColor (q.mag);

        SCoord s_raw;
        ll2sRaw (q.center, s_raw, 1);
        if (!rawPointClearOfMapEdge (s_raw, 12))
            continue;

        drawQuakeGlyph (s_raw.x, s_raw.y, q.mag, color, QUAKE_MAP_MAXR, true);

        // build a logical-space (not raw/scaled) hit box for click-to-popup -- touch events
        // arrive in logical coordinates, same convention checkPlanetMapTouch() uses
        if (n_quake_map_btn < QUAKE_MAXQ) {
            SCoord s_log;
            ll2s (q.center, s_log, 1);
            const int hit_r = 14;
            SBox &b = quake_map_btn[n_quake_map_btn];
            b.x = s_log.x - hit_r;
            b.y = s_log.y - hit_r;
            b.w = hit_r * 2;
            b.h = hit_r * 2;
            quake_map_idx[n_quake_map_btn] = i;
            n_quake_map_btn++;
        }
    }
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

bool checkQuakesTouch (const SCoord &s, const SBox &box)
{
    if (quake_detail_showing) {
        if (quake_detail_ss.checkScrollUpTouch (s, box)) {
            if (quake_detail_ss.okToScrollUp()) {
                quakeScrollStepUp (quake_detail_ss);
                drawQuakeDetailView (box);
            }
            return true;
        }
        if (quake_detail_ss.checkScrollDownTouch (s, box)) {
            if (quake_detail_ss.okToScrollDown()) {
                quakeScrollStepDown (quake_detail_ss);
                drawQuakeDetailView (box);
            }
            return true;
        }
        quake_detail_showing = false;
        drawQuakesPane (box);
        return true;
    }

    if (s.y < box.y + QUAKE_TITLE_Y0 + 5) {
        if (quake_ss.checkScrollUpTouch (s, box)) {
            if (quake_ss.okToScrollUp()) {
                quakeScrollStepUp (quake_ss);
                drawQuakesPane (box);
            }
            return true;
        }
        if (quake_ss.checkScrollDownTouch (s, box)) {
            if (quake_ss.okToScrollDown()) {
                quakeScrollStepDown (quake_ss);
                drawQuakesPane (box);
            }
            return true;
        }
        if (quake_forced_active) {
            restoreQuakeForcedPane();
            return true;
        }
        return false;
    } else if (s.y < box.y + QUAKE_START_DY) {
        if (quake_forced_active)
            restoreQuakeForcedPane();
        return true;
    } else {
        int item = (s.y - box.y - QUAKE_START_DY) / (QUAKE_ROW_H + QUAKE_ROW_PAD);
        int index;
        if (quake_ss.findDataIndex (item, index) && index >= 0 && index < n_quake) {

            if (quake_pz_saved && index == quake_centered_idx) {
                uint16_t row_y = (box.y + QUAKE_START_DY) + item * (QUAKE_ROW_H + QUAKE_ROW_PAD);
                if (inBox (s, quakeResetBox (box, row_y))) {
                    // tapped the return button on the already-centered row -- restore prior view
                    restorePanZoom (quake_saved_pz);
                    quake_pz_saved = false;
                    quake_centered_idx = -1;
                    drawQuakesPane (box);
                    Serial.printf ("QUAKE: restored previous map view\n");
                    return true;
                }
                // tapped elsewhere on the already-centered row -- show the full text as a
                // secondary action, now that "where is it" has already been answered
                showQuakeDetail (box, quake_ev[index]);
                return true;
            }

            // not yet centered -- pan/zoom the map to this quake's epicenter
            const QuakeEvent &q = quake_ev[index];
            LatLong ll = q.center;
            PanZoom before = pan_zoom;
            if (panZoomToLocation (ll, MAX_ZOOM)) {
                if (!quake_pz_saved) {          // remember the view from before focusing any quake
                    quake_saved_pz = before;
                    quake_pz_saved = true;
                }
                quake_centered_idx = index;
                Serial.printf ("QUAKE: panned to %s at %.2f,%.2f\n", q.id, ll.lat_d, ll.lng_d);
            } else {
                Serial.printf ("QUAKE: %s tapped; map pan/zoom unavailable in this projection\n", q.id);
            }
            drawQuakesPane (box);               // redraw so the return button appears on this row
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

bool quakesActive (void)
{
    return n_quake > 0;
}

void initQuakes (void)
{
    if (!NVReadUInt8 (NV_QUAKE_ON, &quake_on)) {
        quake_on = 1;
        NVWriteUInt8 (NV_QUAKE_ON, quake_on);
    }
    if (!NVReadUInt8 (NV_QUAKE_AUTOPOP, &quake_autopop)) {
        quake_autopop = 0;                         // default OFF, unlike Marine/Fire
        NVWriteUInt8 (NV_QUAKE_AUTOPOP, quake_autopop);
    }
    if (!NVReadUInt16 (NV_QUAKE_RADIUS, &quake_radius_mi) || quake_radius_mi == 0) {
        quake_radius_mi = QUAKE_LOCAL_MI_SMALL;
        NVWriteUInt16 (NV_QUAKE_RADIUS, quake_radius_mi);
    }
    Serial.printf ("QUAKE: init on=%d autopop=%d radius=%umi\n",
                   quake_on, quake_autopop, quake_radius_mi);
}

// ---------------------------------------------------------------------------
// REST test injection
// ---------------------------------------------------------------------------

bool injectTestQuake (const char *id, float mag, float depth_km, float lat, float lng,
                      const char *place)
{
    if (!id || !id[0])
        return false;

    n_quake = 1;
    QuakeEvent &q = quake_ev[0];
    q = QuakeEvent{};
    quietStrncpy (q.id, id, sizeof(q.id));
    q.qtime    = myNow();
    q.mag      = mag;
    q.depth_km = depth_km;
    q.center   = LatLong (lat, lng);
    quietStrncpy (q.place, (place && place[0]) ? place : "Test Location", sizeof(q.place));
    q.is_local = computeQuakeIsLocal (q);

    quake_ss.init (QUAKE_MAXQ, 0, 0, ScrollState::DIR_TOPDOWN);
    quake_ss.n_data = n_quake;
    quake_ss.scrollToNewest();

    for (int i = 0; i < n_quake_seen; i++) {
        if (strcmp (quake_seen_ids[i], q.id) == 0) {
            memmove (quake_seen_ids[i], quake_seen_ids[i+1], (n_quake_seen-i-1) * QUAKE_IDLEN);
            n_quake_seen--;
            break;
        }
    }

    checkQuakeAutoPopup();
    Serial.printf ("QUAKE: test event %s injected, M%.1f local=%d\n", q.id, q.mag, q.is_local);
    return true;
}

void clearTestQuakes (void)
{
    n_quake = 0;
    quake_ss.n_data = 0;
    restoreQuakeForcedPane();
    Serial.printf ("QUAKE: test events cleared\n");
}
