/* marinewarnings.cpp -- US NWS Special Marine Warning overlay for HamClock
 *
 * Fetches active marine warnings from the OHB backend (fetch_marine_warnings.py) and displays
 * them on the map and in a scrollable pane, following the same OHB-proxy pattern as
 * hurricane.cpp and lightning.cpp: the backend does the real polling of api.weather.gov and
 * hands the client a small pre-reduced flat file.
 *
 * These are a US NWS product -- Special Marine Warning, Marine Weather Statement, Storm
 * Warning, etc. -- issued per marine zone (coastal waters, Great Lakes, territorial waters).
 * There is no equivalent feed for non-US waters, so this overlay is inherently US-only; that
 * is a property of the data source, not a restriction on which stations may use it.
 *
 * Data format from /marine/warnings.txt -- one line per warning, comma-separated, 8 fields:
 *   ID,OFFICE,ISSUED,EXPIRES,CENTER_LAT,CENTER_LON,HEADLINE,VERTS
 * where:
 *   ID       NWS product id, eg "KTBW-SMW-2026082815"
 *   OFFICE   3-4 char issuing WFO id, eg "TBW"
 *   ISSUED   unix epoch seconds
 *   EXPIRES  unix epoch seconds
 *   CENTER_LAT/LON   polygon centroid (or zone centroid if no polygon), decimal degrees
 *   HEADLINE short human-readable text; backend sanitizes commas out of this field
 *   VERTS    optional polygon, last field so it may itself contain commas:
 *            "lat1:lon1;lat2:lon2;...;latN:lonN" (empty if unavailable -- centroid-only warning)
 *
 * A warning is considered "local" (near the user's own station) if DE falls inside its polygon,
 * or -- when no polygon was supplied -- within MARINE_FALLBACK_MI of its centroid. The first
 * time a local warning is seen, if the feature's auto-popup setting is on, it forcibly takes
 * over its target pane (whatever was there) so it can't be missed, and restores whatever
 * previously showing after MARINE_AUTOPOP_HOLD seconds or once the warning expires, whichever
 * is first.
 *
 * Pane: PLOT_CH_MARINE   (scrollable list, follows Storms pane pattern)
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define MARINE_MAXAGE        (60*5)     // cache max age, secs -- SMWs are short-fuse, poll every 5 min
#define MARINE_MINSIZ        1          // min acceptable file size (empty file = 0 warnings, still ok)
#define MARINE_MAXWARN       20         // max simultaneous warnings
#define MARINE_MAXVERTS      20         // max polygon vertices kept per warning
#define MARINE_IDLEN         40         // NWS product id length including EOS
#define MARINE_OFFLEN        8          // issuing office id length including EOS
#define MARINE_HEADLEN       80         // headline text length including EOS
#define MARINE_AREALEN       96         // areaDesc (human-readable location) length including EOS
#define MARINE_FALLBACK_MI   50         // default geofence radius, statute miles, when no polygon
#define MARINE_AUTOPOP_HOLD  300        // secs to hold a forced popup before restoring

#define MARINE_TITLE_Y0      PANETITLE_H
#define MARINE_ENTRY_H       22
#define MARINE_START_DY      36         // y offset (from box top) to first warning row
#define MARINE_ROW_H         58         // total height of one row: icon + up to 3-line headline + detail
#define MARINE_ROW_PAD       8          // gap between rows
#define MARINE_ICON_SZ        30         // glyph bounding box, px -- deliberately not tiny
#define MARINE_ICON_MARGIN    8          // gap between icon and text column
#define MARINE_TEXT_LINE_H    12         // line pitch for wrapped FAST_FONT headline text (8px glyph + margin)
#define MARINE_HEAD_MAXLINES  3          // wrap headline to at most this many lines
#define MARINE_TITLE_RSV     28         // reserve at right so title clears the scroll-arrow control
#define MARINE_ACCENT_W       4          // width of the colored accent bar on the left of each row
#define MARINE_BADGE_PAD      3          // padding inside the LOCAL badge chip

static const char marine_page[] = "/marine/warnings.txt";
static const char marine_fn[]   = "warnings.txt";

#define MARINE_COLOR_WARN    RGB565(255,50,50)     // active local warning -- red
#define MARINE_COLOR_OTHER   RGB565(255,170,0)     // active but not local -- amber
#define MARINE_COLOR_TITLE   RA8875_WHITE
#define MARINE_COLOR_HINT    RGB565(110,110,110)

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

typedef struct {
    char     id[MARINE_IDLEN];
    char     office[MARINE_OFFLEN];
    time_t   issued;
    time_t   expires;
    LatLong  center;
    char     headline[MARINE_HEADLEN];
    char     area[MARINE_AREALEN];     // human-readable location, e.g. CAP alert's areaDesc
    LatLong  verts[MARINE_MAXVERTS];
    int      n_verts;                  // 0 == centroid-only, use radius fallback
    bool     is_local;                 // DE inside polygon or within fallback radius
} MarineWarning;

static MarineWarning marine_warn[MARINE_MAXWARN];
static int    n_marine = 0;
static uint32_t marine_prev_refresh;    // millis() of last fetch attempt; 0 == never fetched

static ScrollState marine_ss;

// whether the feature is on at all, and whether a local warning may force its target pane. Both persisted.
static uint8_t marine_on       = 1;     // NV_MARINE_ON, default on
static uint8_t marine_autopop  = 1;     // NV_MARINE_AUTOPOP, default on
static uint16_t marine_radius_mi = MARINE_FALLBACK_MI;   // NV_MARINE_RADIUS

// auto-popup state: remembers what the target pane was showing before we seized it, so we can put it back.
static bool       marine_forced_active = false;   // true while our forced popup is in control of the target pane
static PlotChoice marine_prev_pane_ch  = PLOT_CH_NONE;
static PlotMask   marine_prev_pane_rotset = 0;
static time_t     marine_autopop_until = 0;        // myNow() deadline to auto-restore

// IDs already popped this session, so a given warning only interrupts once. Small fixed ring is
// plenty -- SMWs are short-lived and MARINE_MAXWARN bounds how many can be active at once anyway.
static char marine_seen_ids[MARINE_MAXWARN][MARINE_IDLEN];
static int  n_marine_seen = 0;

// tap-to-view-full-text state: showing the NWS description for one warning in place of the list
#define MARINE_DETAIL_MAXLEN    700
#define MARINE_DETAIL_MAXLINES  40      // ~40 wrapped lines is comfortably more than
                                         // MARINE_DETAIL_MAXLEN's worth of text at any pane width
static bool     marine_detail_showing = false;
static char     marine_detail_text[MARINE_DETAIL_MAXLEN];
static uint16_t marine_detail_color;
static char     marine_detail_lines[MARINE_DETAIL_MAXLINES][64];
static int      marine_detail_n_lines = 0;
static ScrollState marine_detail_ss;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

/* standard ray-casting point-in-polygon test, good enough at marine-zone scale.
 * verts[] is a simple (possibly non-convex) closed ring; does not need to repeat first==last.
 */
static bool llInsidePolygon (const LatLong &pt, const LatLong verts[], int n_verts)
{
    if (n_verts < 3)
        return false;

    bool inside = false;
    for (int i = 0, j = n_verts - 1; i < n_verts; j = i++) {
        float yi = verts[i].lat_d, xi = verts[i].lng_d;
        float yj = verts[j].lat_d, xj = verts[j].lng_d;
        if (((yi > pt.lat_d) != (yj > pt.lat_d)) &&
            (pt.lng_d < (xj - xi) * (pt.lat_d - yi) / ((yj - yi) ? (yj - yi) : 1e-9f) + xi))
            inside = !inside;
    }
    return inside;
}

/* great-circle distance from DE to ll, statute miles */
static float milesFromDE (const LatLong &ll)
{
    LatLong de = de_ll;
    LatLong to = ll;
    return de.GSD(to) * ERAD_M;
}

/* decide and cache whether w is "local" -- inside its polygon if it has one, else within the
 * configured fallback radius of its centroid.
 */
static bool computeIsLocal (const MarineWarning &w)
{
    if (w.n_verts >= 3)
        return llInsidePolygon (de_ll, w.verts, w.n_verts);
    return milesFromDE (w.center) <= (float)marine_radius_mi;
}

// ---------------------------------------------------------------------------
// Text wrapping -- everything drawn in this pane MUST stay within the box: prepPlotBox()
// does not set any clip region, so text or graphics that overflow the box draw straight
// across the rest of the screen. maxStringW()/wrapText() are the only things standing
// between us and that -- never tft.print() a field-sourced string without going through one.
// ---------------------------------------------------------------------------

/* greedy word-wrap of src into at most max_lines lines, each hard-guaranteed (via maxStringW)
 * to be <= maxw pixels wide in the currently-selected font. Excess text is dropped, with the
 * last line getting a trailing "..." if anything was cut. Returns the number of lines used.
 */
static int wrapText (const char *src, uint16_t maxw, char lines[][48], int max_lines)
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
            maxStringW (lines[n_lines], maxw);          // hard guarantee even for one long word
            n_lines++;
            cur[0] = '\0';
        }
    }

    if (cur[0] && n_lines < max_lines) {
        quietStrncpy (lines[n_lines], cur, 48);
        maxStringW (lines[n_lines], maxw);
        n_lines++;
    } else if (*p && n_lines == max_lines) {
        // more text remains than we have room for -- mark the last line as truncated
        char *last = lines[n_lines-1];
        size_t l = strlen (last);
        if (l < 45) { last[l] = '.'; last[l+1] = '.'; last[l+2] = '.'; last[l+3] = '\0'; }
        maxStringW (last, maxw);
    }

    return n_lines;
}

/* like wrapText() but for the full scrollable detail view: wraps as many lines as fit within
 * max_lines (no "..." truncation marker -- the buffer is sized generously enough that running
 * out is not expected in normal use).
 */
static int wrapFullText (const char *src, uint16_t maxw, char lines[][64], int max_lines)
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

/* classic warning triangle + exclamation mark, entirely vector so it scales cleanly.
 * (cx,topy) is the apex; size is the bounding box width/height.
 */
static void drawWarningGlyph (uint16_t cx, uint16_t topy, uint16_t size, uint16_t color, bool raw = false)
{
    int16_t x0 = cx,          y0 = topy;
    int16_t x1 = cx - size/2, y1 = topy + size;
    int16_t x2 = cx + size/2, y2 = topy + size;

    uint16_t stem_w = size/7 > 1 ? size/7 : 2;
    uint16_t stem_h = size*2/5;
    uint16_t stem_x = cx - stem_w/2;
    uint16_t stem_y = topy + size*3/10;
    uint16_t dot_r  = stem_w > 1 ? stem_w/2+1 : 1;
    uint16_t dot_y  = topy + size*8/10;

    if (raw) {
        tft.fillTriangleRaw (x0, y0, x1, y1, x2, y2, color);
        tft.drawTriangleRaw (x0, y0, x1, y1, x2, y2, RA8875_BLACK);
        tft.fillRectRaw (stem_x, stem_y, stem_w, stem_h, RA8875_BLACK);
        tft.fillCircleRaw (cx, dot_y, dot_r, RA8875_BLACK);
    } else {
        tft.fillTriangle (x0, y0, x1, y1, x2, y2, color);
        tft.drawTriangle (x0, y0, x1, y1, x2, y2, RA8875_BLACK);
        tft.fillRect (stem_x, stem_y, stem_w, stem_h, RA8875_BLACK);
        tft.fillCircle (cx, dot_y, dot_r, RA8875_BLACK);
    }
}

/* calm "all clear" badge for the empty-warnings state -- a filled circle with a checkmark */
static void drawAllClearGlyph (uint16_t cx, uint16_t cy, uint16_t r, uint16_t color)
{
    tft.fillCircle (cx, cy, r, color);
    tft.drawCircle (cx, cy, r, RA8875_BLACK);
    tft.drawLine (cx - r*3/5, cy,        cx - r/6,   cy + r*2/5, 3, RA8875_BLACK);
    tft.drawLine (cx - r/6,   cy + r*2/5, cx + r*3/5, cy - r*2/5, 3, RA8875_BLACK);
}

/* compact type abbreviation derived from the headline text -- NWS headlines are fairly
 * formulaic ("Special Marine Warning issued...", "Marine Weather Statement...", etc.) so a
 * simple substring match covers the common marine products without needing a separate field
 * from the backend.
 */
static const char *warningTypeAbbrev (const char *headline)
{
    if (strstr (headline, "Special Marine"))
        return "SMW";
    if (strstr (headline, "Marine Weather"))
        return "MWS";
    if (strstr (headline, "Hurricane"))
        return "HUR";
    if (strstr (headline, "Storm Warning") || strstr (headline, "Storm Wx"))
        return "STM";
    if (strstr (headline, "Gale"))
        return "GALE";
    if (strstr (headline, "Small Craft"))
        return "SCA";
    return "MRN";
}



/* parse one VERTS field "lat1:lon1;lat2:lon2;..." into w->verts[]/n_verts. tolerant of empty. */
static void parseVerts (MarineWarning *w, char *verts_str)
{
    w->n_verts = 0;
    if (!verts_str || !verts_str[0])
        return;

    char *save = NULL;
    for (char *tok = strtok_r (verts_str, ";", &save); tok && w->n_verts < MARINE_MAXVERTS;
                tok = strtok_r (NULL, ";", &save)) {
        float lat, lon;
        if (sscanf (tok, "%f:%f", &lat, &lon) == 2)
            w->verts[w->n_verts++] = LatLong (lat, lon);
    }
}

static bool parseMarineFile (FILE *fp)
{
    n_marine = 0;
    for (int i = 0; i < MARINE_MAXWARN; i++)
        marine_warn[i] = MarineWarning{};
    marine_ss.init (MARINE_MAXWARN, 0, 0, ScrollState::DIR_TOPDOWN);

    char line[400];
    while (fgets (line, sizeof(line), fp) && n_marine < MARINE_MAXWARN) {

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char id[MARINE_IDLEN], office[MARINE_OFFLEN], headline[MARINE_HEADLEN];
        char area[MARINE_AREALEN];
        long issued, expires;
        float lat, lon;
        char verts[300];
        area[0] = '\0';
        verts[0] = '\0';

        // headline/area/verts may be empty; verts is last so trailing content (incl any ';') is fine
        int n = sscanf (line, "%39[^,],%7[^,],%ld,%ld,%f,%f,%79[^,],%95[^,],%299[^\n\r]",
                        id, office, &issued, &expires, &lat, &lon, headline, area, verts);
        if (n < 7)
            continue;

        MarineWarning *w = &marine_warn[n_marine++];
        quietStrncpy (w->id, id, sizeof(w->id));
        quietStrncpy (w->office, office, sizeof(w->office));
        w->issued  = (time_t)issued;
        w->expires = (time_t)expires;
        w->center  = LatLong (lat, lon);
        quietStrncpy (w->headline, headline, sizeof(w->headline));
        quietStrncpy (w->area, n >= 8 ? area : "", sizeof(w->area));
        if (n == 9)
            parseVerts (w, verts);
        w->is_local = computeIsLocal (*w);
    }

    // strongest/most-imminent first isn't meaningful here the way peak wind is for storms, so
    // just sort local warnings to the top -- that's what the user actually needs to see first.
    for (int i = 0; i < n_marine-1; i++) {
        for (int j = i+1; j < n_marine; j++) {
            if (marine_warn[j].is_local && !marine_warn[i].is_local) {
                MarineWarning tmp = marine_warn[i];
                marine_warn[i] = marine_warn[j];
                marine_warn[j] = tmp;
            }
        }
    }

    marine_ss.n_data = n_marine;
    marine_ss.scrollToNewest();
    Serial.printf ("MARINE: parsed %d warnings\n", n_marine);
    return true;
}

// ---------------------------------------------------------------------------
// Auto-popup: force a newly-seen local warning onto a fixed target pane
// ---------------------------------------------------------------------------

// which pane the auto-popup seizes. PANE_0 (the small DE/DX overlay) was the original choice
// but it's the panel people are usually actively watching, so this now targets PANE_1 instead.
#define MARINE_AUTOPOP_PANE   PANE_1

static bool alreadySeen (const char *id)
{
    for (int i = 0; i < n_marine_seen; i++)
        if (strcmp (marine_seen_ids[i], id) == 0)
            return true;
    return false;
}

static void rememberSeen (const char *id)
{
    if (n_marine_seen >= MARINE_MAXWARN) {
        // ring: drop oldest to make room -- MARINE_MAXWARN active warnings is already the cap
        memmove (marine_seen_ids[0], marine_seen_ids[1], (MARINE_MAXWARN-1) * MARINE_IDLEN);
        n_marine_seen = MARINE_MAXWARN - 1;
    }
    quietStrncpy (marine_seen_ids[n_marine_seen++], id, MARINE_IDLEN);
}

/* restore whatever MARINE_AUTOPOP_PANE was showing before we forced Marine onto it, if we're
 * still the one in control of it (the user may have manually changed panes since, in which
 * case leave it alone).
 */
static void restoreForcedPane (void)
{
    if (!marine_forced_active)
        return;
    marine_forced_active = false;
    ROTHOLD_CLR (PLOT_CH_MARINE);
    if (plot_ch[MARINE_AUTOPOP_PANE] == PLOT_CH_MARINE) {
        PlotChoice restore_ch = marine_prev_pane_ch;
        PlotMask   restore_rs = marine_prev_pane_rotset;
        if (restore_ch == PLOT_CH_NONE) {
            // shouldn't normally happen for a non-PANE_0 target (only PANE_0 legitimately
            // sits "off"), but fall back to something rather than leave the pane stuck showing
            // Marine with an empty rotset
            restore_ch = getAnyAvailableChoice();
            restore_rs = restore_ch != PLOT_CH_NONE ? PLOTBIT (restore_ch) : 0;
        }
        if (restore_ch != PLOT_CH_NONE) {
            plot_rotset[MARINE_AUTOPOP_PANE] = restore_rs;
            setPlotChoice (MARINE_AUTOPOP_PANE, restore_ch);
        }
    }
    marine_prev_pane_ch = PLOT_CH_NONE;
    marine_prev_pane_rotset = 0;
}

/* scan for any newly-arrived local warning and, if auto-popup is enabled, seize the target pane.
 * called once per fetch, after parseMarineFile().
 */
static void checkAutoPopup (void)
{
    // first, auto-restore an expired or timed-out forced popup regardless of new arrivals
    if (marine_forced_active && myNow() >= marine_autopop_until)
        restoreForcedPane();

    if (!marine_on || !marine_autopop)
        return;

    for (int i = 0; i < n_marine; i++) {
        const MarineWarning &w = marine_warn[i];
        if (!w.is_local || alreadySeen (w.id))
            continue;

        rememberSeen (w.id);

        // don't clobber our own already-forced popup with another local warning mid-hold;
        // just extend the hold so the newer one's expiry still governs the restore
        if (!marine_forced_active) {
            marine_prev_pane_ch     = plot_ch[MARINE_AUTOPOP_PANE];
            marine_prev_pane_rotset = plot_rotset[MARINE_AUTOPOP_PANE];
            marine_forced_active = true;
        }
        // findPaneForChoice() (and therefore nextPaneUpdate(), isPaneRotating(), etc.) key off
        // plot_rotset, NOT plot_ch -- setPlotChoice() alone never touches plot_rotset, so this
        // must be set first or the pane will be genuinely showing Marine while every consistency
        // check in the rest of the app still thinks no pane has it (fatal crash otherwise).
        plot_rotset[MARINE_AUTOPOP_PANE] = PLOTBIT (PLOT_CH_MARINE);
        ROTHOLD_SET (PLOT_CH_MARINE);
        setPlotChoice (MARINE_AUTOPOP_PANE, PLOT_CH_MARINE);
        time_t hold_deadline = myNow() + MARINE_AUTOPOP_HOLD;
        time_t expire_deadline = w.expires > 0 ? w.expires : hold_deadline;
        marine_autopop_until = hold_deadline < expire_deadline ? hold_deadline : expire_deadline;

        Serial.printf ("MARINE: local warning %s forced onto pane %d\n", w.id, (int)MARINE_AUTOPOP_PANE);
    }
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

static bool retrieveMarineWarnings (void)
{
    FILE *fp = openCachedFile (marine_fn, marine_page, MARINE_MAXAGE, MARINE_MINSIZ);
    if (!fp) {
        Serial.printf ("MARINE: failed to open %s\n", marine_fn);
        return false;
    }

    bool ok = parseMarineFile (fp);
    fclose (fp);

    if (ok)
        checkAutoPopup();
    else
        Serial.printf ("MARINE: parse failed\n");

    return ok;
}

/* fetch/parse marine data if due. UNLIKE checkStormsData(), this runs regardless of whether the
 * Marine pane is selected anywhere -- the geofence/auto-popup logic depends on it running even
 * when the user has never put the pane on screen, otherwise a local warning could never surface.
 * Self-guards on the master on/off setting instead. Called unconditionally from wifi.cpp
 * updateWiFi(), same call site as checkStormsData()/updateLightning().
 */
bool checkMarineWarningsData (void)
{
    if (!marine_on)
        return true;

    if (marine_prev_refresh != 0 && !timesUp (&marine_prev_refresh, (uint32_t)MARINE_MAXAGE * 1000))
        return true;

    marine_prev_refresh = millis();
    return retrieveMarineWarnings();
}

/* update the marine warnings pane. called from wifi.cpp updateWiFi() PLOT_CH_MARINE case. */
bool updateMarineWarnings (const SBox &box, bool fresh)
{
    bool ok;

    if (fresh) {
        marine_prev_refresh = millis();
        ok = retrieveMarineWarnings();
    } else {
        ok = checkMarineWarningsData();
    }

    // don't redraw over an open detail view -- the fetch/parse above still ran and stays
    // current in the background, it just shouldn't yank the screen back to the list (or an
    // error message) out from under someone actively reading/scrolling the full text
    if (marine_detail_showing)
        return ok;

    if (ok)
        drawMarineWarningsPane (box);
    else
        plotMessage (box, MARINE_COLOR_WARN, "Marine warnings unavailable");

    return ok;
}

// ---------------------------------------------------------------------------
// Tap-to-view full text
// ---------------------------------------------------------------------------

/* mirror of the backend script's filename sanitizer -- CAP alert ids look like
 * "urn:oid:2.49.0.1.840.0.936f066bc47a75db" and need to become a safe single path component.
 * Both sides MUST use the same rule or the client will request a filename the backend never wrote.
 */
static void sanitizeIdForPath (const char *id, char *out, size_t out_len)
{
    size_t n = 0;
    for (const char *p = id; *p && n < out_len-1; p++)
        out[n++] = (isalnum ((unsigned char)*p) || *p=='.' || *p=='_' || *p=='-') ? *p : '_';
    out[n] = '\0';
}

#define MARINE_DETAIL_BOTTOM_MARGIN  10   // keep the last line clear of the box's bottom edge

/* how many detail-view lines fit in box -- shared so the initial scroll position (set once,
 * in showMarineDetail()) and the per-draw control sizing (in drawMarineDetailView()) can't
 * drift out of sync with each other.
 */
static int computeDetailMaxVis (const SBox &box)
{
    uint16_t usable_h = box.h > MARINE_START_DY + MARINE_DETAIL_BOTTOM_MARGIN
                       ? box.h - MARINE_START_DY - MARINE_DETAIL_BOTTOM_MARGIN : MARINE_TEXT_LINE_H;
    int max_vis = (int)usable_h / MARINE_TEXT_LINE_H;
    return max_vis < 1 ? 1 : max_vis;
}

/* fetch (through the usual backend-proxy cache, same as everything else in this file -- the
 * client has no TLS stack so it still can't reach api.weather.gov on its own) and display the
 * full NWS description text for one warning, replacing the list view until dismissed.
 */
static void showMarineDetail (const SBox &box, const MarineWarning &w)
{
    char safe_id[MARINE_IDLEN];
    sanitizeIdForPath (w.id, safe_id, sizeof(safe_id));

    char det_page[100];
    snprintf (det_page, sizeof(det_page), "/marine/detail/%s.txt", safe_id);
    char det_fn[60];
    snprintf (det_fn, sizeof(det_fn), "mdet_%s.txt", safe_id);

    marine_detail_color = w.is_local ? MARINE_COLOR_WARN : MARINE_COLOR_OTHER;

    FILE *fp = openCachedFile (det_fn, det_page, CACHE_FOREVER, 1);
    if (fp) {
        size_t n = fread (marine_detail_text, 1, sizeof(marine_detail_text)-1, fp);
        marine_detail_text[n] = '\0';
        fclose (fp);
    } else {
        quietStrncpy (marine_detail_text, "Full text unavailable for this warning.",
                      sizeof(marine_detail_text));
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t wrap_w = box.w > 8 ? box.w - 8 : box.w;
    marine_detail_n_lines = wrapFullText (marine_detail_text, wrap_w,
                                           marine_detail_lines, MARINE_DETAIL_MAXLINES);

    // DIR_BOTUP keeps display order matching array order (line 0 first, downward) -- DIR_TOPDOWN
    // is for "newest at top" feeds where the top row shows the *highest* index, which is why
    // only line 0 was ever visible until scrolling accidentally bumped top_vis via clamping.
    int max_vis = computeDetailMaxVis (box);
    marine_detail_ss.init (MARINE_DETAIL_MAXLINES, max_vis - 1, marine_detail_n_lines,
                           ScrollState::DIR_BOTUP);

    marine_detail_showing = true;
    drawMarineWarningsPane (box);
}

// ---------------------------------------------------------------------------
// Pane drawing
// ---------------------------------------------------------------------------

/* scrollable full-text detail view -- shown in place of the list while marine_detail_showing.
 * reuses the exact same ScrollState-driven scroll-arrow controls as the list view.
 */
static void drawMarineDetailView (const SBox &box)
{
    prepPlotBox (box);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (marine_detail_color);
    char title[20] = "Full Text";
    uint16_t avail_r = box.w > MARINE_TITLE_RSV ? box.w - MARINE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w > tw ? (box.w - tw)/2 : 2), box.y + MARINE_TITLE_Y0);
    tft.print (title);

    marine_detail_ss.max_vis = computeDetailMaxVis (box);

    marine_detail_ss.drawScrollUpControl (box, MARINE_COLOR_TITLE, MARINE_COLOR_TITLE);
    marine_detail_ss.drawScrollDownControl (box, MARINE_COLOR_TITLE, MARINE_COLOR_TITLE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (marine_detail_color);
    int min_i, max_i;
    marine_detail_ss.getVisDataIndices (min_i, max_i);
    uint16_t y = box.y + MARINE_START_DY + MARINE_TEXT_LINE_H;
    for (int i = min_i; i <= max_i && i < marine_detail_n_lines; i++) {
        tft.setCursor (box.x + 4, y);
        tft.print (marine_detail_lines[i]);
        y += MARINE_TEXT_LINE_H;
    }
}

void drawMarineWarningsPane (const SBox &box)
{
    if (marine_detail_showing) {
        drawMarineDetailView (box);
        return;
    }

    prepPlotBox (box);

    bool has_local = false;
    for (int i = 0; i < n_marine; i++)
        if (marine_warn[i].is_local) { has_local = true; break; }

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (has_local ? MARINE_COLOR_WARN : MARINE_COLOR_TITLE);
    char title[20] = "Marine Wx";
    uint16_t avail_r = box.w > MARINE_TITLE_RSV ? box.w - MARINE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    uint16_t tx = box.w > tw ? (box.w - tw)/2 : 2;
    if (tx + tw > avail_r)
        tx = avail_r > tw ? avail_r - tw : 2;
    tft.setCursor (box.x + tx, box.y + MARINE_TITLE_Y0);
    tft.print (title);

    int max_vis = (int)(box.h - MARINE_START_DY) / (MARINE_ROW_H + MARINE_ROW_PAD);
    if (max_vis < 1)
        max_vis = 1;
    marine_ss.max_vis = max_vis;
    marine_ss.n_data  = n_marine;

    marine_ss.drawScrollUpControl (box, MARINE_COLOR_TITLE, MARINE_COLOR_TITLE);
    marine_ss.drawScrollDownControl (box, MARINE_COLOR_TITLE, MARINE_COLOR_TITLE);

    if (n_marine == 0) {
        // decorative all-clear badge, sized to whatever headroom the box actually has
        uint16_t r = box.w/6;
        if (r > (box.h - MARINE_START_DY)/4) r = (box.h - MARINE_START_DY)/4;
        if (r < 10) r = 10;
        uint16_t cx = box.x + box.w/2;
        uint16_t cy = box.y + MARINE_START_DY + r + 4;
        drawAllClearGlyph (cx, cy, r, RGB565(60,190,100));

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (MARINE_COLOR_OTHER);
        char msg[20] = "No active";
        char msg2[20] = "warnings";
        maxStringW (msg, box.w - 4);
        maxStringW (msg2, box.w - 4);
        uint16_t w1 = getTextWidth (msg), w2 = getTextWidth (msg2);
        uint16_t ty = cy + r + 14;
        tft.setCursor (box.x + (box.w > w1 ? (box.w-w1)/2 : 2), ty);
        tft.print (msg);
        tft.setCursor (box.x + (box.w > w2 ? (box.w-w2)/2 : 2), ty + MARINE_ENTRY_H);
        tft.print (msg2);
        return;
    }

    int min_i, max_i;
    marine_ss.getVisDataIndices (min_i, max_i);
    uint16_t row_y = box.y + MARINE_START_DY;
    uint16_t text_x = box.x + MARINE_ACCENT_W + MARINE_ICON_SZ + MARINE_ICON_MARGIN + 2;
    uint16_t text_w = box.w > (MARINE_ACCENT_W + MARINE_ICON_SZ + MARINE_ICON_MARGIN + 8)
                        ? box.w - (MARINE_ACCENT_W + MARINE_ICON_SZ + MARINE_ICON_MARGIN + 8) : box.w/2;

    for (int i = min_i; i <= max_i && i < n_marine; i++) {
        const MarineWarning &w = marine_warn[i];
        uint16_t color = w.is_local ? MARINE_COLOR_WARN : MARINE_COLOR_OTHER;
        time_t left = w.expires > myNow() ? w.expires - myNow() : 0;
        bool expired = w.expires > 0 && left == 0;

        // subtle alternating row shading so entries read as distinct cards
        if ((i - min_i) % 2 == 1)
            tft.fillRect (box.x + MARINE_ACCENT_W, row_y - 2, box.w - MARINE_ACCENT_W,
                          MARINE_ROW_H, RGB565(18,18,18));

        // colored accent bar -- the fastest-scanning severity cue in the row
        tft.fillRect (box.x, row_y - 2, MARINE_ACCENT_W, MARINE_ROW_H, color);

        // wrap the headline first so we know the text block's actual height, then vertically
        // center the glyph against *that* rather than pinning it to the row top -- otherwise
        // a short 1-line headline leaves the icon looking stranded above a lot of empty space
        selectFontStyle (BOLD_FONT, FAST_FONT);
        char hl_lines[MARINE_HEAD_MAXLINES][48];
        int n_hl = wrapText (w.area[0] ? w.area : w.headline, text_w, hl_lines, MARINE_HEAD_MAXLINES);
        uint16_t content_h = MARINE_TEXT_LINE_H * n_hl + 14;    // headline lines + detail line
        uint16_t icon_top = row_y + (content_h > MARINE_ICON_SZ ? (content_h - MARINE_ICON_SZ)/2 : 0);

        drawWarningGlyph (box.x + MARINE_ACCENT_W + MARINE_ICON_SZ/2 + 2, icon_top,
                          MARINE_ICON_SZ, expired ? MARINE_COLOR_HINT : color);

        tft.setTextColor (expired ? MARINE_COLOR_HINT : color);
        uint16_t line_y = row_y + MARINE_TEXT_LINE_H;
        for (int l = 0; l < n_hl; l++) {
            tft.setCursor (text_x, line_y);
            tft.print (hl_lines[l]);
            line_y += MARINE_TEXT_LINE_H;
        }

        // measure the LOCAL badge *before* building/truncating the detail text so we always
        // reserve real room for it -- the previous version only reserved space when the row
        // happened to already be wide enough, so on a narrow pane the badge just overlapped
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        const char *badge = "LOCAL";
        uint16_t badge_w = getTextWidth (badge) + 2*MARINE_BADGE_PAD;
        uint16_t badge_reserve = (w.is_local && !expired) ? badge_w + 6 : 0;

        char det[32];
        const char *tabbr = warningTypeAbbrev (w.headline);
        if (expired) {
            tft.setTextColor (MARINE_COLOR_HINT);
            snprintf (det, sizeof(det), "EXPIRED  %s %s", tabbr, w.office);
        } else {
            struct tm exp_tm = *gmtime (&w.expires);
            uint16_t left_color = left <= 600 ? MARINE_COLOR_WARN
                                 : left <= 1800 ? MARINE_COLOR_OTHER : MARINE_COLOR_HINT;
            tft.setTextColor (left_color);
            snprintf (det, sizeof(det), "until %02d:%02dZ  %s %s",
                      exp_tm.tm_hour, exp_tm.tm_min, tabbr, w.office);
        }
        uint16_t det_w = text_w > badge_reserve ? text_w - badge_reserve : (text_w > 10 ? text_w : 10);
        maxStringW (det, det_w);
        uint16_t det_y = line_y + 2;
        tft.setCursor (text_x, det_y);
        tft.print (det);

        if (w.is_local && !expired) {
            uint16_t bx = box.x + box.w - badge_w - 3;
            uint16_t by = det_y - 9;
            tft.fillRect (bx, by, badge_w, 12, MARINE_COLOR_WARN);
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (bx + MARINE_BADGE_PAD, by + 9);
            tft.print (badge);
        }

        row_y += MARINE_ROW_H + MARINE_ROW_PAD;

        // divider between rows
        if (i < max_i && i < n_marine - 1)
            tft.drawLine (box.x + MARINE_ACCENT_W + 2, row_y - MARINE_ROW_PAD/2,
                          box.x + box.w - 2, row_y - MARINE_ROW_PAD/2, RGB565(45,45,45));
    }

    // redraw the local-warning glow last so nothing drawn above (accent bars, shading) can
    // ever paint over it -- 2px thick to be unmistakable against the pane's normal border
    if (has_local) {
        tft.drawRect (box.x, box.y, box.w, box.h, MARINE_COLOR_WARN);
        tft.drawRect (box.x+1, box.y+1, box.w-2, box.h-2, MARINE_COLOR_WARN);
    }
}

// ---------------------------------------------------------------------------
// Map overlay
// ---------------------------------------------------------------------------

/* draw all active warnings on the map: polygon outline if we have one, else a simple marker
 * at the centroid. local warnings drawn in the warning color, others dimmer.
 * called from ESPHamClock.cpp after drawStormsOnMap().
 */
void drawMarineWarningsOnMap (void)
{
    if (n_marine == 0 || !marine_on)
        return;

    // only draw while the Marine Wx pane is actually shown somewhere -- background tracking
    // (fetch/parse/geofence/auto-popup) still runs regardless, so a genuinely local warning
    // will force itself onto a pane and appear here anyway; this just stops a warning's map
    // marker from lingering after the user switches away from the pane.
    if (findPaneForChoice (PLOT_CH_MARINE) == PANE_NONE)
        return;

    for (int i = 0; i < n_marine; i++) {
        const MarineWarning &w = marine_warn[i];
        uint16_t color = w.is_local ? MARINE_COLOR_WARN : MARINE_COLOR_OTHER;

        // icon glyph only -- polygon outlines removed per request
        // ll2sRaw() + rawPointClearOfMapEdge() must be paired -- rawPointClearOfMapEdge()
        // compares against map_b scaled by tft.SCALESZ, and ll2sRaw() is the one that returns
        // coordinates already in that scaled space (plain ll2s() does not). Same pairing
        // hurricane.cpp/dxpeds.cpp/launches.cpp use for their own map markers.
        SCoord s;
        ll2sRaw (w.center, s, 1);
        if (rawPointClearOfMapEdge (s, 12))
            drawWarningGlyph (s.x, s.y - 9*tft.SCALESZ, 18*tft.SCALESZ, color, true);
    }
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

/* ScrollState::scrollDown()/scrollUp() page by (max_vis-1) rows (leaving one row of context),
 * which becomes a permanent no-op when max_vis==1 -- a real case here since a narrow pane can
 * easily only fit one row/line at a time. Step by exactly one row ourselves in that situation.
 * Assumes DIR_TOPDOWN (scrollT2B()==true), the only mode this file uses.
 */
static void scrollStepDown (ScrollState &ss)
{
    if (ss.max_vis > 1) {
        ss.scrollDown();
        return;
    }
    int new_top = ss.top_vis - 1;
    if (new_top < ss.max_vis - 1)
        new_top = ss.max_vis - 1;
    ss.top_vis = new_top;
}

static void scrollStepUp (ScrollState &ss)
{
    if (ss.max_vis > 1) {
        ss.scrollUp();
        return;
    }
    int new_top = ss.top_vis + 1;
    if (new_top > ss.n_data - 1)
        new_top = ss.n_data - 1;
    ss.top_vis = new_top;
}

bool checkMarineWarningsTouch (const SCoord &s, const SBox &box)
{
    if (marine_detail_showing) {
        if (marine_detail_ss.checkScrollUpTouch (s, box)) {
            if (marine_detail_ss.okToScrollUp()) {
                scrollStepUp (marine_detail_ss);
                drawMarineDetailView (box);
            }
            return true;
        }
        if (marine_detail_ss.checkScrollDownTouch (s, box)) {
            if (marine_detail_ss.okToScrollDown()) {
                scrollStepDown (marine_detail_ss);
                drawMarineDetailView (box);
            }
            return true;
        }
        // any other tap dismisses the detail view back to the list
        marine_detail_showing = false;
        drawMarineWarningsPane (box);
        return true;
    }

    if (s.y < box.y + MARINE_TITLE_Y0 + 5) {

        if (marine_ss.checkScrollUpTouch (s, box)) {
            if (marine_ss.okToScrollUp()) {
                scrollStepUp (marine_ss);
                drawMarineWarningsPane (box);
            }
            return true;
        }
        if (marine_ss.checkScrollDownTouch (s, box)) {
            if (marine_ss.okToScrollDown()) {
                scrollStepDown (marine_ss);
                drawMarineWarningsPane (box);
            }
            return true;
        }

        // tapping the title area of a forced popup is how the user dismisses it early
        if (marine_forced_active) {
            restoreForcedPane();
            return true;
        }
        return false;

    } else if (s.y < box.y + MARINE_START_DY) {

        // header padding area below the title -- dismiss a forced popup here too, otherwise no-op
        if (marine_forced_active)
            restoreForcedPane();
        return true;

    } else {

        int item = (s.y - box.y - MARINE_START_DY) / (MARINE_ROW_H + MARINE_ROW_PAD);
        int index;
        if (marine_ss.findDataIndex (item, index) && index >= 0 && index < n_marine) {
            Serial.printf ("MARINE: %s tapped\n", marine_warn[index].id);
            showMarineDetail (box, marine_warn[index]);
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// Misc accessors used by plotmgmnt.cpp / wifi.cpp wiring
// ---------------------------------------------------------------------------

bool marineWarningsActive (void)
{
    return n_marine > 0;
}

/* init from NV, called once at startup from ESPHamClock.cpp, same as initStorms() */
void initMarineWarnings (void)
{
    if (!NVReadUInt8 (NV_MARINE_ON, &marine_on)) {
        marine_on = 1;
        NVWriteUInt8 (NV_MARINE_ON, marine_on);
    }
    if (!NVReadUInt8 (NV_MARINE_AUTOPOP, &marine_autopop)) {
        marine_autopop = 1;
        NVWriteUInt8 (NV_MARINE_AUTOPOP, marine_autopop);
    }
    if (!NVReadUInt16 (NV_MARINE_RADIUS, &marine_radius_mi) || marine_radius_mi == 0) {
        marine_radius_mi = MARINE_FALLBACK_MI;
        NVWriteUInt16 (NV_MARINE_RADIUS, marine_radius_mi);
    }
    Serial.printf ("MARINE: init on=%d autopop=%d radius=%umi\n",
                   marine_on, marine_autopop, marine_radius_mi);
}

// ---------------------------------------------------------------------------
// REST test injection -- lets set_marine_test? on the web server drive a synthetic warning
// through the exact same geofence + auto-popup path real backend data would, without needing
// a live NWS alert or hand-editing the local cache file.
// ---------------------------------------------------------------------------

/* replace the current warning list with a single synthetic one and run it through the normal
 * geofence + auto-popup logic. minutes <= 0 defaults to 15. returns false only if id is empty.
 */
bool injectTestMarineWarning (const char *id, const char *office, float lat, float lng,
                               int minutes, const char *headline)
{
    if (!id || !id[0])
        return false;

    n_marine = 1;
    MarineWarning &w = marine_warn[0];
    w = MarineWarning{};
    quietStrncpy (w.id, id, sizeof(w.id));
    quietStrncpy (w.office, (office && office[0]) ? office : "TEST", sizeof(w.office));
    w.issued  = myNow();
    w.expires = myNow() + (minutes > 0 ? minutes : 15) * 60;
    w.center  = LatLong (lat, lng);
    quietStrncpy (w.headline, (headline && headline[0]) ? headline : "Test Special Marine Warning",
                  sizeof(w.headline));
    w.n_verts = 0;                      // centroid + radius fallback geofence, same as a real
                                         // warning that arrived with no CAP polygon
    w.is_local = computeIsLocal (w);

    marine_ss.init (MARINE_MAXWARN, 0, 0, ScrollState::DIR_TOPDOWN);
    marine_ss.n_data = n_marine;
    marine_ss.scrollToNewest();

    // drop this id from the "already popped" list so re-running the same test id always
    // retriggers the auto-popup, instead of only firing once per session like a real warning
    for (int i = 0; i < n_marine_seen; i++) {
        if (strcmp (marine_seen_ids[i], w.id) == 0) {
            memmove (marine_seen_ids[i], marine_seen_ids[i+1], (n_marine_seen-i-1) * MARINE_IDLEN);
            n_marine_seen--;
            break;
        }
    }

    checkAutoPopup();
    Serial.printf ("MARINE: test warning %s injected, local=%d\n", w.id, w.is_local);
    return true;
}

/* clear all warnings (including any injected test ones) and restore the target pane if a forced
 * popup was in control of it.
 */
void clearTestMarineWarnings (void)
{
    n_marine = 0;
    marine_ss.n_data = 0;
    restoreForcedPane();
    Serial.printf ("MARINE: test warnings cleared\n");
}
