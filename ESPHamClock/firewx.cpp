/* firewx.cpp -- US NWS Red Flag Warning / Fire Weather Watch overlay for HamClock
 *
 * Fetches active fire weather warnings from the OHB backend (fetch_firewx_warnings.py) and
 * displays them on the map and in a scrollable pane, following the same OHB-proxy pattern as
 * hurricane.cpp, lightning.cpp, and marinewarnings.cpp: the backend does the real polling of
 * api.weather.gov and hands the client a small pre-reduced flat file.
 *
 * These are a US NWS product -- Red Flag Warning (conditions favorable for rapid fire spread,
 * in effect now) and Fire Weather Watch (elevated risk, not yet in effect) -- issued per NWS
 * fire weather zone. There is no equivalent feed outside the US, so this overlay is inherently
 * US-only; that is a property of the data source, not a restriction on which stations may use it.
 *
 * Data format from /firewx/warnings.txt -- one line per warning, comma-separated, 9 fields:
 *   ID,OFFICE,ISSUED,EXPIRES,CENTER_LAT,CENTER_LON,HEADLINE,AREA,VERTS
 * where:
 *   ID       NWS product id
 *   OFFICE   3-4 char issuing WFO id
 *   ISSUED   unix epoch seconds
 *   EXPIRES  unix epoch seconds
 *   CENTER_LAT/LON   polygon centroid (or zone centroid if no polygon), decimal degrees
 *   HEADLINE short human-readable text; backend sanitizes commas out of this field
 *   AREA     CAP alert's areaDesc -- human-readable affected zone, eg "Coastal Plains of
 *            southeast GA and northeast FL"
 *   VERTS    optional polygon, last field so it may itself contain commas:
 *            "lat1:lon1;lat2:lon2;...;latN:lonN" (empty if unavailable -- centroid-only warning)
 *
 * A warning is considered "local" (near the user's own station) if DE falls inside its polygon,
 * or -- when no polygon was supplied -- within FIRE_FALLBACK_MI of its centroid. The first
 * time a local warning is seen, if the feature's auto-popup setting is on, it forcibly takes
 * over its target pane (whatever was there) so it can't be missed, and restores whatever was
 * previously showing after FIRE_AUTOPOP_HOLD seconds or once the warning expires, whichever
 * is first.
 *
 * Pane: PLOT_CH_FIREWX   (scrollable list, follows the Marine Wx pane pattern)
 */

#include "HamClock.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

#define FIRE_MAXAGE        (60*5)     // cache max age, secs -- poll every 5 min
#define FIRE_MINSIZ        1          // min acceptable file size (empty file = 0 warnings, still ok)
#define FIRE_MAXWARN       20         // max simultaneous warnings
#define FIRE_MAXVERTS      20         // max polygon vertices kept per warning
#define FIRE_IDLEN         40         // NWS product id length including EOS
#define FIRE_OFFLEN        8          // issuing office id length including EOS
#define FIRE_HEADLEN       80         // headline text length including EOS
#define FIRE_AREALEN       96         // areaDesc (human-readable location) length including EOS
#define FIRE_FALLBACK_MI   50         // default geofence radius, statute miles, when no polygon
#define FIRE_AUTOPOP_HOLD  300        // secs to hold a forced popup before restoring

#define FIRE_TITLE_Y0      PANETITLE_H
#define FIRE_ENTRY_H       22
#define FIRE_START_DY      36         // y offset (from box top) to first warning row
#define FIRE_ROW_H         58         // total height of one row: icon + up to 3-line headline + detail
#define FIRE_ROW_PAD       8          // gap between rows
#define FIRE_ICON_SZ        30         // glyph bounding box, px -- deliberately not tiny
#define FIRE_ICON_MARGIN    8          // gap between icon and text column
#define FIRE_TEXT_LINE_H    12         // line pitch for wrapped FAST_FONT headline text (8px glyph + margin)
#define FIRE_HEAD_MAXLINES  3          // wrap headline to at most this many lines
#define FIRE_TITLE_RSV     28         // reserve at right so title clears the scroll-arrow control
#define FIRE_ACCENT_W       4          // width of the colored accent bar on the left of each row
#define FIRE_BADGE_PAD      3          // padding inside the LOCAL badge chip

static const char firewx_page[] = "/firewx/warnings.txt";
static const char firewx_fn[]   = "firewx_warnings.txt";

#define FIRE_COLOR_WARN    RGB565(255,50,50)     // active local warning -- red
#define FIRE_COLOR_OTHER   RGB565(255,170,0)     // active but not local -- amber
#define FIRE_COLOR_TITLE   RA8875_WHITE
#define FIRE_COLOR_HINT    RGB565(110,110,110)

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

typedef struct {
    char     id[FIRE_IDLEN];
    char     office[FIRE_OFFLEN];
    time_t   issued;
    time_t   expires;
    LatLong  center;
    char     headline[FIRE_HEADLEN];
    char     area[FIRE_AREALEN];     // human-readable location, e.g. CAP alert's areaDesc
    LatLong  verts[FIRE_MAXVERTS];
    int      n_verts;                  // 0 == centroid-only, use radius fallback
    bool     is_local;                 // DE inside polygon or within fallback radius
} FireWarning;

static FireWarning firewx_warn[FIRE_MAXWARN];
static int    n_firewx = 0;
static uint32_t firewx_prev_refresh;    // millis() of last fetch attempt; 0 == never fetched

static ScrollState firewx_ss;

// whether the feature is on at all, and whether a local warning may force its target pane. Both persisted.
static uint8_t firewx_on       = 1;     // NV_FIREWX_ON, default on
static uint8_t firewx_autopop  = 1;     // NV_FIREWX_AUTOPOP, default on
static uint16_t firewx_radius_mi = FIRE_FALLBACK_MI;   // NV_FIREWX_RADIUS

// auto-popup state: remembers what the target pane was showing before we seized it, so we can put it back.
static bool       firewx_forced_active = false;   // true while our forced popup is in control of the target pane
static PlotChoice firewx_prev_pane_ch  = PLOT_CH_NONE;
static PlotMask   firewx_prev_pane_rotset = 0;
static time_t     firewx_autopop_until = 0;        // myNow() deadline to auto-restore

// IDs already popped this session, so a given warning only interrupts once. Small fixed ring is
// plenty -- SMWs are short-lived and FIRE_MAXWARN bounds how many can be active at once anyway.
static char firewx_seen_ids[FIRE_MAXWARN][FIRE_IDLEN];
static int  n_firewx_seen = 0;

// tap-to-view-full-text state: showing the NWS description for one warning in place of the list
#define FIRE_DETAIL_MAXLEN    700
#define FIRE_DETAIL_MAXLINES  40      // ~40 wrapped lines is comfortably more than
                                         // FIRE_DETAIL_MAXLEN's worth of text at any pane width
static bool     firewx_detail_showing = false;
static char     firewx_detail_text[FIRE_DETAIL_MAXLEN];
static uint16_t firewx_detail_color;
static char     firewx_detail_lines[FIRE_DETAIL_MAXLINES][64];
static int      firewx_detail_n_lines = 0;
static ScrollState firewx_detail_ss;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

/* standard ray-casting point-in-polygon test, good enough at marine-zone scale.
 * verts[] is a simple (possibly non-convex) closed ring; does not need to repeat first==last.
 */
static bool fireLlInsidePolygon (const LatLong &pt, const LatLong verts[], int n_verts)
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
static float fireMilesFromDE (const LatLong &ll)
{
    LatLong de = de_ll;
    LatLong to = ll;
    return de.GSD(to) * ERAD_M;
}

/* decide and cache whether w is "local" -- inside its polygon if it has one, else within the
 * configured fallback radius of its centroid.
 */
static bool computeFireIsLocal (const FireWarning &w)
{
    if (w.n_verts >= 3)
        return fireLlInsidePolygon (de_ll, w.verts, w.n_verts);
    return fireMilesFromDE (w.center) <= (float)firewx_radius_mi;
}

// ---------------------------------------------------------------------------
// Text wrapping -- everything drawn in this pane MUST stay within the box: prepPlotBox()
// does not set any clip region, so text or graphics that overflow the box draw straight
// across the rest of the screen. maxStringW()/fireWrapText() are the only things standing
// between us and that -- never tft.print() a field-sourced string without going through one.
// ---------------------------------------------------------------------------

/* greedy word-wrap of src into at most max_lines lines, each hard-guaranteed (via maxStringW)
 * to be <= maxw pixels wide in the currently-selected font. Excess text is dropped, with the
 * last line getting a trailing "..." if anything was cut. Returns the number of lines used.
 */
static int fireWrapText (const char *src, uint16_t maxw, char lines[][48], int max_lines)
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

/* like fireWrapText() but for the full scrollable detail view: wraps as many lines as fit within
 * max_lines (no "..." truncation marker -- the buffer is sized generously enough that running
 * out is not expected in normal use).
 */
static int fireWrapFullText (const char *src, uint16_t maxw, char lines[][64], int max_lines)
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
/* two-tone flame silhouette: outer triangle in the row/severity color, smaller brighter
 * triangle inset toward the bottom as the "core" -- distinct from Marine Wx's warning-triangle
 * glyph so the two overlay types read apart from each other at a glance on a shared map.
 */
static void drawFireGlyph (uint16_t cx, uint16_t topy, uint16_t size, uint16_t color, bool raw = false)
{
    int16_t ox0 = cx,              oy0 = topy;
    int16_t ox1 = cx - size*3/8,   oy1 = topy + size;
    int16_t ox2 = cx + size*3/8,   oy2 = topy + size;

    uint16_t inner_top   = topy + size*3/8;
    int16_t  ix0 = cx,              iy0 = inner_top;
    int16_t  ix1 = cx - size*3/16,  iy1 = topy + size;
    int16_t  ix2 = cx + size*3/16,  iy2 = topy + size;
    uint16_t inner_color = RGB565(255,230,80);   // bright core, regardless of outer severity color

    if (raw) {
        tft.fillTriangleRaw (ox0, oy0, ox1, oy1, ox2, oy2, color);
        tft.drawTriangleRaw (ox0, oy0, ox1, oy1, ox2, oy2, RA8875_BLACK);
        tft.fillTriangleRaw (ix0, iy0, ix1, iy1, ix2, iy2, inner_color);
    } else {
        tft.fillTriangle (ox0, oy0, ox1, oy1, ox2, oy2, color);
        tft.drawTriangle (ox0, oy0, ox1, oy1, ox2, oy2, RA8875_BLACK);
        tft.fillTriangle (ix0, iy0, ix1, iy1, ix2, iy2, inner_color);
    }
}

/* calm "all clear" badge for the empty-warnings state -- a filled circle with a checkmark */
static void drawFireAllClearGlyph (uint16_t cx, uint16_t cy, uint16_t r, uint16_t color)
{
    tft.fillCircle (cx, cy, r, color);
    tft.drawCircle (cx, cy, r, RA8875_BLACK);
    tft.drawLine (cx - r*3/5, cy,        cx - r/6,   cy + r*2/5, 3, RA8875_BLACK);
    tft.drawLine (cx - r/6,   cy + r*2/5, cx + r*3/5, cy - r*2/5, 3, RA8875_BLACK);
}

/* compact type abbreviation derived from the headline text -- NWS headlines are fairly
 * formulaic ("Red Flag Warning issued...", "Fire Weather Watch...") so a simple substring
 * match covers the fire weather products without needing a separate field from the backend.
 */
static const char *fireTypeAbbrev (const char *headline)
{
    if (strstr (headline, "Red Flag"))
        return "RFW";
    if (strstr (headline, "Fire Weather Watch"))
        return "FWW";
    if (strstr (headline, "Extreme Fire"))
        return "EXT";
    return "FIRE";
}



/* parse one VERTS field "lat1:lon1;lat2:lon2;..." into w->verts[]/n_verts. tolerant of empty. */
static void parseFireVerts (FireWarning *w, char *verts_str)
{
    w->n_verts = 0;
    if (!verts_str || !verts_str[0])
        return;

    char *save = NULL;
    for (char *tok = strtok_r (verts_str, ";", &save); tok && w->n_verts < FIRE_MAXVERTS;
                tok = strtok_r (NULL, ";", &save)) {
        float lat, lon;
        if (sscanf (tok, "%f:%f", &lat, &lon) == 2)
            w->verts[w->n_verts++] = LatLong (lat, lon);
    }
}

static bool parseFireWxFile (FILE *fp)
{
    n_firewx = 0;
    for (int i = 0; i < FIRE_MAXWARN; i++)
        firewx_warn[i] = FireWarning{};
    firewx_ss.init (FIRE_MAXWARN, 0, 0, ScrollState::DIR_TOPDOWN);

    char line[400];
    while (fgets (line, sizeof(line), fp) && n_firewx < FIRE_MAXWARN) {

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        char id[FIRE_IDLEN], office[FIRE_OFFLEN], headline[FIRE_HEADLEN];
        char area[FIRE_AREALEN];
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

        FireWarning *w = &firewx_warn[n_firewx++];
        quietStrncpy (w->id, id, sizeof(w->id));
        quietStrncpy (w->office, office, sizeof(w->office));
        w->issued  = (time_t)issued;
        w->expires = (time_t)expires;
        w->center  = LatLong (lat, lon);
        quietStrncpy (w->headline, headline, sizeof(w->headline));
        quietStrncpy (w->area, n >= 8 ? area : "", sizeof(w->area));
        if (n == 9)
            parseFireVerts (w, verts);
        w->is_local = computeFireIsLocal (*w);
    }

    // strongest/most-imminent first isn't meaningful here the way peak wind is for storms, so
    // just sort local warnings to the top -- that's what the user actually needs to see first.
    for (int i = 0; i < n_firewx-1; i++) {
        for (int j = i+1; j < n_firewx; j++) {
            if (firewx_warn[j].is_local && !firewx_warn[i].is_local) {
                FireWarning tmp = firewx_warn[i];
                firewx_warn[i] = firewx_warn[j];
                firewx_warn[j] = tmp;
            }
        }
    }

    firewx_ss.n_data = n_firewx;
    firewx_ss.scrollToNewest();
    Serial.printf ("MARINE: parsed %d warnings\n", n_firewx);
    return true;
}

// ---------------------------------------------------------------------------
// Auto-popup: force a newly-seen local warning onto a fixed target pane
// ---------------------------------------------------------------------------

// which pane the auto-popup seizes. PANE_0 (the small DE/DX overlay) was the original choice
// but it's the panel people are usually actively watching, so this now targets PANE_1 instead.
#define FIRE_AUTOPOP_PANE   PANE_1

static bool fireAlreadySeen (const char *id)
{
    for (int i = 0; i < n_firewx_seen; i++)
        if (strcmp (firewx_seen_ids[i], id) == 0)
            return true;
    return false;
}

static void fireRememberSeen (const char *id)
{
    if (n_firewx_seen >= FIRE_MAXWARN) {
        // ring: drop oldest to make room -- FIRE_MAXWARN active warnings is already the cap
        memmove (firewx_seen_ids[0], firewx_seen_ids[1], (FIRE_MAXWARN-1) * FIRE_IDLEN);
        n_firewx_seen = FIRE_MAXWARN - 1;
    }
    quietStrncpy (firewx_seen_ids[n_firewx_seen++], id, FIRE_IDLEN);
}

/* restore whatever FIRE_AUTOPOP_PANE was showing before we forced Marine onto it, if we're
 * still the one in control of it (the user may have manually changed panes since, in which
 * case leave it alone).
 */
static void restoreFireForcedPane (void)
{
    if (!firewx_forced_active)
        return;
    firewx_forced_active = false;
    ROTHOLD_CLR (PLOT_CH_FIREWX);
    if (plot_ch[FIRE_AUTOPOP_PANE] == PLOT_CH_FIREWX) {
        PlotChoice restore_ch = firewx_prev_pane_ch;
        PlotMask   restore_rs = firewx_prev_pane_rotset;
        if (restore_ch == PLOT_CH_NONE) {
            // shouldn't normally happen for a non-PANE_0 target (only PANE_0 legitimately
            // sits "off"), but fall back to something rather than leave the pane stuck showing
            // Marine with an empty rotset
            restore_ch = getAnyAvailableChoice();
            restore_rs = restore_ch != PLOT_CH_NONE ? PLOTBIT (restore_ch) : 0;
        }
        if (restore_ch != PLOT_CH_NONE) {
            plot_rotset[FIRE_AUTOPOP_PANE] = restore_rs;
            setPlotChoice (FIRE_AUTOPOP_PANE, restore_ch);
        }
    }
    firewx_prev_pane_ch = PLOT_CH_NONE;
    firewx_prev_pane_rotset = 0;
}

/* scan for any newly-arrived local warning and, if auto-popup is enabled, seize the target pane.
 * called once per fetch, after parseFireWxFile().
 */
static void checkFireAutoPopup (void)
{
    // first, auto-restore an expired or timed-out forced popup regardless of new arrivals
    if (firewx_forced_active && myNow() >= firewx_autopop_until)
        restoreFireForcedPane();

    if (!firewx_on || !firewx_autopop)
        return;

    for (int i = 0; i < n_firewx; i++) {
        const FireWarning &w = firewx_warn[i];
        if (!w.is_local || fireAlreadySeen (w.id))
            continue;

        fireRememberSeen (w.id);

        // don't clobber our own already-forced popup with another local warning mid-hold;
        // just extend the hold so the newer one's expiry still governs the restore
        if (!firewx_forced_active) {
            firewx_prev_pane_ch     = plot_ch[FIRE_AUTOPOP_PANE];
            firewx_prev_pane_rotset = plot_rotset[FIRE_AUTOPOP_PANE];
            firewx_forced_active = true;
        }
        // findPaneForChoice() (and therefore nextPaneUpdate(), isPaneRotating(), etc.) key off
        // plot_rotset, NOT plot_ch -- setPlotChoice() alone never touches plot_rotset, so this
        // must be set first or the pane will be genuinely showing Marine while every consistency
        // check in the rest of the app still thinks no pane has it (fatal crash otherwise).
        plot_rotset[FIRE_AUTOPOP_PANE] = PLOTBIT (PLOT_CH_FIREWX);
        ROTHOLD_SET (PLOT_CH_FIREWX);
        setPlotChoice (FIRE_AUTOPOP_PANE, PLOT_CH_FIREWX);
        time_t hold_deadline = myNow() + FIRE_AUTOPOP_HOLD;
        time_t expire_deadline = w.expires > 0 ? w.expires : hold_deadline;
        firewx_autopop_until = hold_deadline < expire_deadline ? hold_deadline : expire_deadline;

        Serial.printf ("MARINE: local warning %s forced onto pane %d\n", w.id, (int)FIRE_AUTOPOP_PANE);
    }
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

static bool retrieveFireWx (void)
{
    FILE *fp = openCachedFile (firewx_fn, firewx_page, FIRE_MAXAGE, FIRE_MINSIZ);
    if (!fp) {
        Serial.printf ("MARINE: failed to open %s\n", firewx_fn);
        return false;
    }

    bool ok = parseFireWxFile (fp);
    fclose (fp);

    if (ok)
        checkFireAutoPopup();
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
bool checkFireWxData (void)
{
    if (!firewx_on)
        return true;

    if (firewx_prev_refresh != 0 && !timesUp (&firewx_prev_refresh, (uint32_t)FIRE_MAXAGE * 1000))
        return true;

    firewx_prev_refresh = millis();
    return retrieveFireWx();
}

/* update the marine warnings pane. called from wifi.cpp updateWiFi() PLOT_CH_FIREWX case. */
bool updateFireWx (const SBox &box, bool fresh)
{
    bool ok;

    if (fresh) {
        firewx_prev_refresh = millis();
        ok = retrieveFireWx();
    } else {
        ok = checkFireWxData();
    }

    // don't redraw over an open detail view -- the fetch/parse above still ran and stays
    // current in the background, it just shouldn't yank the screen back to the list (or an
    // error message) out from under someone actively reading/scrolling the full text
    if (firewx_detail_showing)
        return ok;

    if (ok)
        drawFireWxPane (box);
    else
        plotMessage (box, FIRE_COLOR_WARN, "Fire warnings unavailable");

    return ok;
}

// ---------------------------------------------------------------------------
// Tap-to-view full text
// ---------------------------------------------------------------------------

/* mirror of the backend script's filename sanitizer -- CAP alert ids look like
 * "urn:oid:2.49.0.1.840.0.936f066bc47a75db" and need to become a safe single path component.
 * Both sides MUST use the same rule or the client will request a filename the backend never wrote.
 */
static void fireSanitizeIdForPath (const char *id, char *out, size_t out_len)
{
    size_t n = 0;
    for (const char *p = id; *p && n < out_len-1; p++)
        out[n++] = (isalnum ((unsigned char)*p) || *p=='.' || *p=='_' || *p=='-') ? *p : '_';
    out[n] = '\0';
}

#define FIRE_DETAIL_BOTTOM_MARGIN  10   // keep the last line clear of the box's bottom edge

/* how many detail-view lines fit in box -- shared so the initial scroll position (set once,
 * in showFireDetail()) and the per-draw control sizing (in drawFireDetailView()) can't
 * drift out of sync with each other.
 */
static int computeFireDetailMaxVis (const SBox &box)
{
    uint16_t usable_h = box.h > FIRE_START_DY + FIRE_DETAIL_BOTTOM_MARGIN
                       ? box.h - FIRE_START_DY - FIRE_DETAIL_BOTTOM_MARGIN : FIRE_TEXT_LINE_H;
    int max_vis = (int)usable_h / FIRE_TEXT_LINE_H;
    return max_vis < 1 ? 1 : max_vis;
}

/* fetch (through the usual backend-proxy cache, same as everything else in this file -- the
 * client has no TLS stack so it still can't reach api.weather.gov on its own) and display the
 * full NWS description text for one warning, replacing the list view until dismissed.
 */
static void showFireDetail (const SBox &box, const FireWarning &w)
{
    char safe_id[FIRE_IDLEN];
    fireSanitizeIdForPath (w.id, safe_id, sizeof(safe_id));

    char det_page[100];
    snprintf (det_page, sizeof(det_page), "/firewx/detail/%s.txt", safe_id);
    char det_fn[60];
    snprintf (det_fn, sizeof(det_fn), "fdet_%s.txt", safe_id);

    firewx_detail_color = w.is_local ? FIRE_COLOR_WARN : FIRE_COLOR_OTHER;

    FILE *fp = openCachedFile (det_fn, det_page, CACHE_FOREVER, 1);
    if (fp) {
        size_t n = fread (firewx_detail_text, 1, sizeof(firewx_detail_text)-1, fp);
        firewx_detail_text[n] = '\0';
        fclose (fp);
    } else {
        quietStrncpy (firewx_detail_text, "Full text unavailable for this warning.",
                      sizeof(firewx_detail_text));
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t wrap_w = box.w > 8 ? box.w - 8 : box.w;
    firewx_detail_n_lines = fireWrapFullText (firewx_detail_text, wrap_w,
                                           firewx_detail_lines, FIRE_DETAIL_MAXLINES);

    // DIR_BOTUP keeps display order matching array order (line 0 first, downward) -- DIR_TOPDOWN
    // is for "newest at top" feeds where the top row shows the *highest* index, which is why
    // only line 0 was ever visible until scrolling accidentally bumped top_vis via clamping.
    int max_vis = computeFireDetailMaxVis (box);
    firewx_detail_ss.init (FIRE_DETAIL_MAXLINES, max_vis - 1, firewx_detail_n_lines,
                           ScrollState::DIR_BOTUP);

    firewx_detail_showing = true;
    drawFireWxPane (box);
}

// ---------------------------------------------------------------------------
// Pane drawing
// ---------------------------------------------------------------------------

/* scrollable full-text detail view -- shown in place of the list while firewx_detail_showing.
 * reuses the exact same ScrollState-driven scroll-arrow controls as the list view.
 */
static void drawFireDetailView (const SBox &box)
{
    prepPlotBox (box);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (firewx_detail_color);
    char title[20] = "Full Text";
    uint16_t avail_r = box.w > FIRE_TITLE_RSV ? box.w - FIRE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w > tw ? (box.w - tw)/2 : 2), box.y + FIRE_TITLE_Y0);
    tft.print (title);

    firewx_detail_ss.max_vis = computeFireDetailMaxVis (box);

    firewx_detail_ss.drawScrollUpControl (box, FIRE_COLOR_TITLE, FIRE_COLOR_TITLE);
    firewx_detail_ss.drawScrollDownControl (box, FIRE_COLOR_TITLE, FIRE_COLOR_TITLE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (firewx_detail_color);
    int min_i, max_i;
    firewx_detail_ss.getVisDataIndices (min_i, max_i);
    uint16_t y = box.y + FIRE_START_DY + FIRE_TEXT_LINE_H;
    for (int i = min_i; i <= max_i && i < firewx_detail_n_lines; i++) {
        tft.setCursor (box.x + 4, y);
        tft.print (firewx_detail_lines[i]);
        y += FIRE_TEXT_LINE_H;
    }
}

void drawFireWxPane (const SBox &box)
{
    if (firewx_detail_showing) {
        drawFireDetailView (box);
        return;
    }

    prepPlotBox (box);

    bool has_local = false;
    for (int i = 0; i < n_firewx; i++)
        if (firewx_warn[i].is_local) { has_local = true; break; }

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (has_local ? FIRE_COLOR_WARN : FIRE_COLOR_TITLE);
    char title[20] = "Fire Wx";
    uint16_t avail_r = box.w > FIRE_TITLE_RSV ? box.w - FIRE_TITLE_RSV : box.w;
    maxStringW (title, avail_r > 4 ? avail_r - 4 : avail_r);
    uint16_t tw = getTextWidth (title);
    uint16_t tx = box.w > tw ? (box.w - tw)/2 : 2;
    if (tx + tw > avail_r)
        tx = avail_r > tw ? avail_r - tw : 2;
    tft.setCursor (box.x + tx, box.y + FIRE_TITLE_Y0);
    tft.print (title);

    int max_vis = (int)(box.h - FIRE_START_DY) / (FIRE_ROW_H + FIRE_ROW_PAD);
    if (max_vis < 1)
        max_vis = 1;
    firewx_ss.max_vis = max_vis;
    firewx_ss.n_data  = n_firewx;

    firewx_ss.drawScrollUpControl (box, FIRE_COLOR_TITLE, FIRE_COLOR_TITLE);
    firewx_ss.drawScrollDownControl (box, FIRE_COLOR_TITLE, FIRE_COLOR_TITLE);

    if (n_firewx == 0) {
        // decorative all-clear badge, sized to whatever headroom the box actually has
        uint16_t r = box.w/6;
        if (r > (box.h - FIRE_START_DY)/4) r = (box.h - FIRE_START_DY)/4;
        if (r < 10) r = 10;
        uint16_t cx = box.x + box.w/2;
        uint16_t cy = box.y + FIRE_START_DY + r + 4;
        drawFireAllClearGlyph (cx, cy, r, RGB565(60,190,100));

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (FIRE_COLOR_OTHER);
        char msg[20] = "No active";
        char msg2[20] = "fire wx";
        maxStringW (msg, box.w - 4);
        maxStringW (msg2, box.w - 4);
        uint16_t w1 = getTextWidth (msg), w2 = getTextWidth (msg2);
        uint16_t ty = cy + r + 14;
        tft.setCursor (box.x + (box.w > w1 ? (box.w-w1)/2 : 2), ty);
        tft.print (msg);
        tft.setCursor (box.x + (box.w > w2 ? (box.w-w2)/2 : 2), ty + FIRE_ENTRY_H);
        tft.print (msg2);
        return;
    }

    int min_i, max_i;
    firewx_ss.getVisDataIndices (min_i, max_i);
    uint16_t row_y = box.y + FIRE_START_DY;
    uint16_t text_x = box.x + FIRE_ACCENT_W + FIRE_ICON_SZ + FIRE_ICON_MARGIN + 2;
    uint16_t text_w = box.w > (FIRE_ACCENT_W + FIRE_ICON_SZ + FIRE_ICON_MARGIN + 8)
                        ? box.w - (FIRE_ACCENT_W + FIRE_ICON_SZ + FIRE_ICON_MARGIN + 8) : box.w/2;

    for (int i = min_i; i <= max_i && i < n_firewx; i++) {
        const FireWarning &w = firewx_warn[i];
        uint16_t color = w.is_local ? FIRE_COLOR_WARN : FIRE_COLOR_OTHER;
        time_t left = w.expires > myNow() ? w.expires - myNow() : 0;
        bool expired = w.expires > 0 && left == 0;

        // subtle alternating row shading so entries read as distinct cards
        if ((i - min_i) % 2 == 1)
            tft.fillRect (box.x + FIRE_ACCENT_W, row_y - 2, box.w - FIRE_ACCENT_W,
                          FIRE_ROW_H, RGB565(18,18,18));

        // colored accent bar -- the fastest-scanning severity cue in the row
        tft.fillRect (box.x, row_y - 2, FIRE_ACCENT_W, FIRE_ROW_H, color);

        // wrap the headline first so we know the text block's actual height, then vertically
        // center the glyph against *that* rather than pinning it to the row top -- otherwise
        // a short 1-line headline leaves the icon looking stranded above a lot of empty space
        selectFontStyle (BOLD_FONT, FAST_FONT);
        char hl_lines[FIRE_HEAD_MAXLINES][48];
        int n_hl = fireWrapText (w.area[0] ? w.area : w.headline, text_w, hl_lines, FIRE_HEAD_MAXLINES);
        uint16_t content_h = FIRE_TEXT_LINE_H * n_hl + 14;    // headline lines + detail line
        uint16_t icon_top = row_y + (content_h > FIRE_ICON_SZ ? (content_h - FIRE_ICON_SZ)/2 : 0);

        drawFireGlyph (box.x + FIRE_ACCENT_W + FIRE_ICON_SZ/2 + 2, icon_top,
                          FIRE_ICON_SZ, expired ? FIRE_COLOR_HINT : color);

        tft.setTextColor (expired ? FIRE_COLOR_HINT : color);
        uint16_t line_y = row_y + FIRE_TEXT_LINE_H;
        for (int l = 0; l < n_hl; l++) {
            tft.setCursor (text_x, line_y);
            tft.print (hl_lines[l]);
            line_y += FIRE_TEXT_LINE_H;
        }

        // measure the LOCAL badge *before* building/truncating the detail text so we always
        // reserve real room for it -- the previous version only reserved space when the row
        // happened to already be wide enough, so on a narrow pane the badge just overlapped
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        const char *badge = "LOCAL";
        uint16_t badge_w = getTextWidth (badge) + 2*FIRE_BADGE_PAD;
        uint16_t badge_reserve = (w.is_local && !expired) ? badge_w + 6 : 0;

        char det[32];
        const char *tabbr = fireTypeAbbrev (w.headline);
        if (expired) {
            tft.setTextColor (FIRE_COLOR_HINT);
            snprintf (det, sizeof(det), "EXPIRED  %s %s", tabbr, w.office);
        } else {
            struct tm exp_tm = *gmtime (&w.expires);
            uint16_t left_color = left <= 600 ? FIRE_COLOR_WARN
                                 : left <= 1800 ? FIRE_COLOR_OTHER : FIRE_COLOR_HINT;
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
            tft.fillRect (bx, by, badge_w, 12, FIRE_COLOR_WARN);
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (bx + FIRE_BADGE_PAD, by + 9);
            tft.print (badge);
        }

        row_y += FIRE_ROW_H + FIRE_ROW_PAD;

        // divider between rows
        if (i < max_i && i < n_firewx - 1)
            tft.drawLine (box.x + FIRE_ACCENT_W + 2, row_y - FIRE_ROW_PAD/2,
                          box.x + box.w - 2, row_y - FIRE_ROW_PAD/2, RGB565(45,45,45));
    }

    // redraw the local-warning glow last so nothing drawn above (accent bars, shading) can
    // ever paint over it -- 2px thick to be unmistakable against the pane's normal border
    if (has_local) {
        tft.drawRect (box.x, box.y, box.w, box.h, FIRE_COLOR_WARN);
        tft.drawRect (box.x+1, box.y+1, box.w-2, box.h-2, FIRE_COLOR_WARN);
    }
}

// ---------------------------------------------------------------------------
// Map overlay
// ---------------------------------------------------------------------------

/* draw all active warnings on the map: polygon outline if we have one, else a simple marker
 * at the centroid. local warnings drawn in the warning color, others dimmer.
 * called from ESPHamClock.cpp after drawStormsOnMap().
 */
void drawFireWxOnMap (void)
{
    if (n_firewx == 0 || !firewx_on)
        return;

    // only draw while the Fire Wx pane is actually shown somewhere -- background tracking
    // still runs regardless, so a genuinely local warning will force itself onto a pane and
    // appear here anyway; this just stops a warning's map marker from lingering after the
    // user switches away from the pane.
    if (findPaneForChoice (PLOT_CH_FIREWX) == PANE_NONE)
        return;

    for (int i = 0; i < n_firewx; i++) {
        const FireWarning &w = firewx_warn[i];
        uint16_t color = w.is_local ? FIRE_COLOR_WARN : FIRE_COLOR_OTHER;

        // icon glyph only -- polygon outlines removed per request
        // ll2sRaw() + rawPointClearOfMapEdge() must be paired -- rawPointClearOfMapEdge()
        // compares against map_b scaled by tft.SCALESZ, and ll2sRaw() is the one that returns
        // coordinates already in that scaled space (plain ll2s() does not). Same pairing
        // hurricane.cpp/dxpeds.cpp/launches.cpp use for their own map markers.
        SCoord s;
        ll2sRaw (w.center, s, 1);
        if (rawPointClearOfMapEdge (s, 12))
            drawFireGlyph (s.x, s.y - 9*tft.SCALESZ, 18*tft.SCALESZ, color, true);
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
static void fireScrollStepDown (ScrollState &ss)
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

static void fireScrollStepUp (ScrollState &ss)
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

bool checkFireWxTouch (const SCoord &s, const SBox &box)
{
    if (firewx_detail_showing) {
        if (firewx_detail_ss.checkScrollUpTouch (s, box)) {
            if (firewx_detail_ss.okToScrollUp()) {
                fireScrollStepUp (firewx_detail_ss);
                drawFireDetailView (box);
            }
            return true;
        }
        if (firewx_detail_ss.checkScrollDownTouch (s, box)) {
            if (firewx_detail_ss.okToScrollDown()) {
                fireScrollStepDown (firewx_detail_ss);
                drawFireDetailView (box);
            }
            return true;
        }
        // any other tap dismisses the detail view back to the list
        firewx_detail_showing = false;
        drawFireWxPane (box);
        return true;
    }

    if (s.y < box.y + FIRE_TITLE_Y0 + 5) {

        if (firewx_ss.checkScrollUpTouch (s, box)) {
            if (firewx_ss.okToScrollUp()) {
                fireScrollStepUp (firewx_ss);
                drawFireWxPane (box);
            }
            return true;
        }
        if (firewx_ss.checkScrollDownTouch (s, box)) {
            if (firewx_ss.okToScrollDown()) {
                fireScrollStepDown (firewx_ss);
                drawFireWxPane (box);
            }
            return true;
        }

        // tapping the title area of a forced popup is how the user dismisses it early
        if (firewx_forced_active) {
            restoreFireForcedPane();
            return true;
        }
        return false;

    } else if (s.y < box.y + FIRE_START_DY) {

        // header padding area below the title -- dismiss a forced popup here too, otherwise no-op
        if (firewx_forced_active)
            restoreFireForcedPane();
        return true;

    } else {

        int item = (s.y - box.y - FIRE_START_DY) / (FIRE_ROW_H + FIRE_ROW_PAD);
        int index;
        if (firewx_ss.findDataIndex (item, index) && index >= 0 && index < n_firewx) {
            Serial.printf ("MARINE: %s tapped\n", firewx_warn[index].id);
            showFireDetail (box, firewx_warn[index]);
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// Misc accessors used by plotmgmnt.cpp / wifi.cpp wiring
// ---------------------------------------------------------------------------

bool fireWxActive (void)
{
    return n_firewx > 0;
}

/* init from NV, called once at startup from ESPHamClock.cpp, same as initStorms() */
void initFireWx (void)
{
    if (!NVReadUInt8 (NV_FIREWX_ON, &firewx_on)) {
        firewx_on = 1;
        NVWriteUInt8 (NV_FIREWX_ON, firewx_on);
    }
    if (!NVReadUInt8 (NV_FIREWX_AUTOPOP, &firewx_autopop)) {
        firewx_autopop = 1;
        NVWriteUInt8 (NV_FIREWX_AUTOPOP, firewx_autopop);
    }
    if (!NVReadUInt16 (NV_FIREWX_RADIUS, &firewx_radius_mi) || firewx_radius_mi == 0) {
        firewx_radius_mi = FIRE_FALLBACK_MI;
        NVWriteUInt16 (NV_FIREWX_RADIUS, firewx_radius_mi);
    }
    Serial.printf ("MARINE: init on=%d autopop=%d radius=%umi\n",
                   firewx_on, firewx_autopop, firewx_radius_mi);
}

// ---------------------------------------------------------------------------
// REST test injection -- lets set_marine_test? on the web server drive a synthetic warning
// through the exact same geofence + auto-popup path real backend data would, without needing
// a live NWS alert or hand-editing the local cache file.
// ---------------------------------------------------------------------------

/* replace the current warning list with a single synthetic one and run it through the normal
 * geofence + auto-popup logic. minutes <= 0 defaults to 15. returns false only if id is empty.
 */
bool injectTestFireWx (const char *id, const char *office, float lat, float lng,
                               int minutes, const char *headline)
{
    if (!id || !id[0])
        return false;

    n_firewx = 1;
    FireWarning &w = firewx_warn[0];
    w = FireWarning{};
    quietStrncpy (w.id, id, sizeof(w.id));
    quietStrncpy (w.office, (office && office[0]) ? office : "TEST", sizeof(w.office));
    w.issued  = myNow();
    w.expires = myNow() + (minutes > 0 ? minutes : 15) * 60;
    w.center  = LatLong (lat, lng);
    quietStrncpy (w.headline, (headline && headline[0]) ? headline : "Test Red Flag Warning",
                  sizeof(w.headline));
    w.n_verts = 0;                      // centroid + radius fallback geofence, same as a real
                                         // warning that arrived with no CAP polygon
    w.is_local = computeFireIsLocal (w);

    firewx_ss.init (FIRE_MAXWARN, 0, 0, ScrollState::DIR_TOPDOWN);
    firewx_ss.n_data = n_firewx;
    firewx_ss.scrollToNewest();

    // drop this id from the "already popped" list so re-running the same test id always
    // retriggers the auto-popup, instead of only firing once per session like a real warning
    for (int i = 0; i < n_firewx_seen; i++) {
        if (strcmp (firewx_seen_ids[i], w.id) == 0) {
            memmove (firewx_seen_ids[i], firewx_seen_ids[i+1], (n_firewx_seen-i-1) * FIRE_IDLEN);
            n_firewx_seen--;
            break;
        }
    }

    checkFireAutoPopup();
    Serial.printf ("MARINE: test warning %s injected, local=%d\n", w.id, w.is_local);
    return true;
}

/* clear all warnings (including any injected test ones) and restore the target pane if a forced
 * popup was in control of it.
 */
void clearTestFireWx (void)
{
    n_firewx = 0;
    firewx_ss.n_data = 0;
    restoreFireForcedPane();
    Serial.printf ("MARINE: test warnings cleared\n");
}
