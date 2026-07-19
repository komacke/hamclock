/* manage the hams.at Satellite Activations Pane.
 *
 * Fetches https://hams.at/api/alerts/upcoming (optionally with a Bearer API key -- see
 * setup.cpp field NV_HAMSATKEY) and displays upcoming satellite activation alerts sorted by AOS.
 *
 * IMPORTANT: we always perform the query even if no API key is configured -- hams.at still
 * returns the full alert list, it just leaves match_percent/is_workable (etc) null/false since
 * it can't compute personalized visibility without knowing who's asking. We render those as
 * blank/'--' rather than treating a missing key as an error.
 *
 * No JSON library is linked into HamClock (ESP32 build has none available), so this file
 * includes a small dependency-free JSON scanner tailored to the flat hams.at alert schema.
 */

#include "HamClock.h"


// title/accent color
#define HAMSAT_COLOR    RGB565(150,250,255)

// match% badge colors
#define MATCH_HI_COLOR  RGB565(40,180,40)          // >= 70%
#define MATCH_MED_COLOR RGB565(200,160,20)         // 30-69%
#define MATCH_LO_COLOR  RGB565(120,120,120)         // < 30%
#define MATCH_UNK_COLOR RGB565(60,60,60)            // unknown (no key / null)

// layout
#define ALERT_DY        LISTING_DY                  // one alert per row

// remote endpoint
static const char hamsat_host[] = "hams.at";
static const char hamsat_page[] = "/api/alerts/upcoming";
static const char hamsat_file[] = "hamsat.json";
#define HAMSAT_MAXAGE   90                           // refresh cache no older than this, secs
#define HAMSAT_MINSIZ   10                           // min acceptable file size, bytes
#define HAMSAT_TO_MS    15000                        // curl timeout, ms


/* one alert, trimmed to what we display / act on
 */
typedef struct {
    char callsign[MAX_SPOTCALL_LEN];
    char satname[16];
    int  sat_number;
    char mode[8];
    char direction[6];                              // "up" or "down" or ""
    float mhz;                                       // 0 if null
    char grid[MAID_CHARLEN];                        // first grid only
    char comment[60];
    char url[64];                                    // https://hams.at/alerts/<uuid>
    time_t aos_at;
    time_t los_at;
    int  match_percent;                              // -1 if null/unknown
    bool is_workable;
    int  likes;
} HamsatAlert;

static HamsatAlert *hamsat_alerts;                   // malloced list, sorted by aos_at
static int n_hamsat;                                 // n entries
static ScrollState hamsat_ss;                        // scrolling context
static SBox hamsat_url_b;                             // tappable region over the subtitle text
static char hamsat_key[NV_HAMSATKEY_LEN];            // API key, may be empty


/* *********************************************************************************************
 * minimal JSON helpers -- tailored to hams.at's flat alert schema, not a general parser.
 * all functions operate on a NUL-terminated buffer and are read-only / non-destructive except
 * where noted.
 */

/* advance p past whitespace
 */
static const char *jsonSkipWS (const char *p)
{
    while (*p && isspace ((unsigned char)*p))
        p++;
    return (p);
}

/* given p pointing anywhere at or before a '{' or '[', return pointer just after the matching
 * closing '}' or ']', respecting quoted strings and backslash escapes. returns NULL if unbalanced.
 */
static const char *jsonSkipBalanced (const char *p)
{
    p = jsonSkipWS (p);
    char open = *p;
    char close = (open == '{') ? '}' : (open == '[') ? ']' : 0;
    if (!close)
        return (NULL);

    int depth = 0;
    bool in_str = false;
    for (; *p; p++) {
        char c = *p;
        if (in_str) {
            if (c == '\\' && p[1])
                p++;                                 // skip escaped char
            else if (c == '"')
                in_str = false;
        } else {
            if (c == '"')
                in_str = true;
            else if (c == open)
                depth++;
            else if (c == close) {
                depth--;
                if (depth == 0)
                    return (p+1);
            }
        }
    }
    return (NULL);
}

/* find the array value for the given key within obj (a '{'..'}' buffer, NUL-terminated is fine
 * even if it continues beyond the matching brace -- we only look for the key before any '}').
 * returns pointer to the '[' or NULL if not found.
 */
static const char *jsonFindArray (const char *obj, const char *key)
{
    char pat[40];
    snprintf (pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr (obj, pat);
    if (!k)
        return (NULL);
    const char *p = jsonSkipWS (k + strlen(pat));
    if (*p != ':')
        return (NULL);
    p = jsonSkipWS (p+1);
    return (*p == '[' ? p : NULL);
}

/* find key's value start within the single object obj_end (exclusive).
 * returns pointer to first char of the value, or NULL if not found in this object.
 * only scans up to obj_end so nested objects/arrays for other keys don't leak in.
 */
static const char *jsonFindValue (const char *obj, const char *obj_end, const char *key)
{
    char pat[40];
    snprintf (pat, sizeof(pat), "\"%s\"", key);
    size_t patlen = strlen(pat);
    for (const char *s = obj; s < obj_end - patlen; s++) {
        if (memcmp (s, pat, patlen) == 0) {
            const char *p = jsonSkipWS (s + patlen);
            if (*p == ':')
                return (jsonSkipWS (p+1));
        }
    }
    return (NULL);
}

/* extract a string value (unquoted, escapes reduced) for key into buf; "" if absent or null.
 */
static void jsonGetString (const char *obj, const char *obj_end, const char *key,
                            char *buf, size_t buflen)
{
    buf[0] = '\0';
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v != '"')
        return;
    v++;
    size_t bi = 0;
    while (*v && *v != '"' && bi < buflen-1) {
        if (*v == '\\' && v[1]) {
            v++;
            char c = *v;
            buf[bi++] = (c == 'n') ? '\n' : (c == 't') ? '\t' : c;
        } else
            buf[bi++] = *v;
        v++;
    }
    buf[bi] = '\0';
}

/* extract a float for key, or dflt if null/absent/not-a-number
 */
static float jsonGetFloat (const char *obj, const char *obj_end, const char *key, float dflt)
{
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v == 'n' /* null */)
        return (dflt);
    return ((float) atof (v));
}

static int jsonGetInt (const char *obj, const char *obj_end, const char *key, int dflt)
{
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v == 'n' /* null */)
        return (dflt);
    return (atoi (v));
}

static bool jsonGetBool (const char *obj, const char *obj_end, const char *key, bool dflt)
{
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v)
        return (dflt);
    if (!strncmp (v, "true", 4))
        return (true);
    if (!strncmp (v, "false", 5))
        return (false);
    return (dflt);
}

/* extract first element of a string array value for key, e.g. "grids":["CN71","CN72"] -> CN71.
 * "" if absent, null, or empty array.
 */
static void jsonGetFirstArrayString (const char *obj, const char *obj_end, const char *key,
                                      char *buf, size_t buflen)
{
    buf[0] = '\0';
    const char *v = jsonFindValue (obj, obj_end, key);
    if (!v || *v != '[')
        return;
    const char *s = jsonSkipWS (v+1);
    if (*s != '"')
        return;
    s++;
    size_t bi = 0;
    while (*s && *s != '"' && bi < buflen-1)
        buf[bi++] = *s++;
    buf[bi] = '\0';
}

/* parse one alert object [obj, obj_end) into a
 */
static void parseOneAlert (const char *obj, const char *obj_end, HamsatAlert &a)
{
    memset (&a, 0, sizeof(a));

    jsonGetString (obj, obj_end, "callsign", a.callsign, sizeof(a.callsign));
    jsonGetString (obj, obj_end, "mode", a.mode, sizeof(a.mode));
    jsonGetString (obj, obj_end, "mhz_direction", a.direction, sizeof(a.direction));
    jsonGetString (obj, obj_end, "comment", a.comment, sizeof(a.comment));
    jsonGetString (obj, obj_end, "url", a.url, sizeof(a.url));
    jsonGetFirstArrayString (obj, obj_end, "grids", a.grid, sizeof(a.grid));
    a.mhz = jsonGetFloat (obj, obj_end, "mhz", 0);
    a.is_workable = jsonGetBool (obj, obj_end, "is_workable", false);
    a.likes = jsonGetInt (obj, obj_end, "likes", 0);

    // match_percent is a bare number 0-100, or JSON null when no API key was sent
    const char *mp = jsonFindValue (obj, obj_end, "match_percent");
    a.match_percent = (mp && *mp != 'n') ? atoi(mp) : -1;

    // times
    char ts[32];
    jsonGetString (obj, obj_end, "aos_at", ts, sizeof(ts));
    a.aos_at = ts[0] ? crackISO8601 (ts) : 0;
    jsonGetString (obj, obj_end, "los_at", ts, sizeof(ts));
    a.los_at = ts[0] ? crackISO8601 (ts) : 0;

    // nested "satellite": {"name":"RS-44","number":44909}
    const char *satkey = strstr (obj, "\"satellite\"");
    if (satkey && satkey < obj_end) {
        const char *sv = jsonSkipWS (strchr (satkey, ':') + 1);
        const char *sv_end = jsonSkipBalanced (sv);
        if (sv_end && sv_end <= obj_end) {
            jsonGetString (sv, sv_end, "name", a.satname, sizeof(a.satname));
            a.sat_number = jsonGetInt (sv, sv_end, "number", 0);
        }
    }
}

/* qsort comparator: descending aos_at -- combined with the pane's existing "newest at top"
 * display convention (scrollToNewest() + reversed getDisplayRow mapping), this puts the
 * soonest-upcoming alert at the top of the list and the furthest-out one at the bottom.
 */
static int qsAOS (const void *p1, const void *p2)
{
    const HamsatAlert *a1 = (const HamsatAlert *)p1;
    const HamsatAlert *a2 = (const HamsatAlert *)p2;
    return (a1->aos_at > a2->aos_at ? -1 : a1->aos_at < a2->aos_at ? 1 : 0);
}


/* *********************************************************************************************
 * networking + NVRAM
 */

/* load hamsat_key from NVRAM, ok if empty
 */
static void loadHamsatKey (void)
{
    if (!NVReadString (NV_HAMSATKEY, hamsat_key))
        hamsat_key[0] = '\0';
}

/* issue an HTTPS GET to hams.at, optionally with Authorization header if we have a key.
 * N.B. mirrors connecthttpsHCGET()'s curl-pipe technique in wifi.cpp but for an arbitrary
 * host/path/header rather than only HamClock's own backend. always attempted regardless of
 * whether hamsat_key is set -- hams.at answers either way, just without personalized fields.
 */
static bool connectHamsatHTTPS (WiFiClient &client)
{
    char cmd[512];
    if (hamsat_key[0])
        snprintf (cmd, sizeof(cmd),
            "curl -A \"%s/%s\" -H \"Authorization: Bearer %s\" --max-time 15 --silent "
            "https://%s%s",
            platform, hc_version, hamsat_key, hamsat_host, hamsat_page);
    else
        snprintf (cmd, sizeof(cmd),
            "curl -A \"%s/%s\" --max-time 15 --silent https://%s%s",
            platform, hc_version, hamsat_host, hamsat_page);

    Serial.printf ("HAMSAT: %s\n", cmd);
    return (client.connectCommand (cmd));
}

/* download the current alert list from hams.at into hamsat_alerts[]/n_hamsat.
 * return whether the network/parse step succeeded (an empty list is still success).
 */
static bool retrieveHamsat (void)
{
    free (hamsat_alerts);
    hamsat_alerts = NULL;
    n_hamsat = 0;

    WiFiClient client;
    if (!connectHamsatHTTPS (client)) {
        Serial.println ("HAMSAT: connect failed");
        return (false);
    }

    // slurp entire body -- curl output has no HTTP header to skip. read raw chars rather than
    // lines: the response is a single unbroken line of JSON with no trailing newline, and
    // getTCPLine() only delivers a line once it sees '\n', silently discarding everything
    // accumulated so far if EOF arrives first.
    StackMalloc buf_mem(20000);                      // generous; truncated below if ever exceeded
    char *buf = (char *) buf_mem.getMem();
    size_t buflen = buf_mem.getSize();
    size_t bi = 0;
    char c;
    while (bi < buflen-1 && getTCPChar (client, &c))
        buf[bi++] = c;
    buf[bi] = '\0';
    client.stop();

    if (bi < HAMSAT_MINSIZ) {
        Serial.println ("HAMSAT: response too short");
        return (false);
    }

    // find "data":[ ... ] and split into top-level objects
    const char *arr = jsonFindArray (buf, "data");
    if (!arr) {
        Serial.println ("HAMSAT: no data[] in response");
        return (false);
    }

    const char *p = jsonSkipWS (arr+1);
    while (*p && *p != ']') {
        if (*p != '{') {
            p++;
            continue;
        }
        const char *obj_end = jsonSkipBalanced (p);
        if (!obj_end)
            break;

        // parse into scratch storage first and sanity-check it before it ever reaches the
        // display array -- callsign/satname should never be empty, aos_at should always crack
        // successfully from a real alert, and match_percent (when not the -1 "unknown" sentinel)
        // must be a real 0-100 percentage. Anything failing this is rejected rather than risk
        // rendering as a garbage row (e.g. "999%" with junk text) if a response was ever malformed.
        HamsatAlert tmp;
        parseOneAlert (p, obj_end, tmp);
        bool sane = tmp.callsign[0] && tmp.satname[0] && tmp.aos_at != 0 &&
                    (tmp.match_percent == -1 || (tmp.match_percent >= 0 && tmp.match_percent <= 100));
        if (sane) {
            hamsat_alerts = (HamsatAlert *) realloc (hamsat_alerts, (n_hamsat+1)*sizeof(HamsatAlert));
            if (!hamsat_alerts)
                fatalError ("No room for %d hamsat alerts", n_hamsat+1);
            hamsat_alerts[n_hamsat++] = tmp;
        } else {
            Serial.printf ("HAMSAT: rejecting malformed alert entry: callsign='%s' sat='%s' "
                            "aos=%ld match=%d\n", tmp.callsign, tmp.satname, (long)tmp.aos_at,
                            tmp.match_percent);
        }

        p = jsonSkipWS (obj_end);
        if (*p == ',')
            p = jsonSkipWS (p+1);
    }

    // drop anything already past LOS, then sort by AOS ascending
    time_t now = myNow();
    int keep = 0;
    for (int i = 0; i < n_hamsat; i++)
        if (hamsat_alerts[i].los_at == 0 || hamsat_alerts[i].los_at > now)
            hamsat_alerts[keep++] = hamsat_alerts[i];
    n_hamsat = keep;
    qsort (hamsat_alerts, n_hamsat, sizeof(HamsatAlert), qsAOS);

    Serial.printf ("HAMSAT: %d upcoming alerts%s\n", n_hamsat, hamsat_key[0] ? "" : " (no API key set)");
    return (true);
}


/* *********************************************************************************************
 * drawing
 */

/* format "Hh MMm" or "NOW" until aos, relative to now
 */
static void formatCountdown (time_t aos, time_t now, char *buf, size_t buflen)
{
    if (aos <= now) {
        snprintf (buf, buflen, "NOW");
        return;
    }
    long dt = aos - now;
    int hr = dt / 3600;
    int mn = (dt % 3600) / 60;
    // clamp so %d always fits buf regardless of dt's theoretical range; also satisfies GCC's
    // value-range analysis to avoid -Wformat-truncation (same convention used in lightning.cpp)
    #define CD_CLAMP(n) ((n) > 99 ? 99 : (n))
    if (hr > 0)
        snprintf (buf, buflen, "%dh%02dm", CD_CLAMP(hr), mn);
    else
        snprintf (buf, buflen, "%dm", mn);
}

static uint16_t matchColor (int match_percent)
{
    if (match_percent < 0)
        return (MATCH_UNK_COLOR);
    if (match_percent >= 70)
        return (MATCH_HI_COLOR);
    if (match_percent >= 30)
        return (MATCH_MED_COLOR);
    return (MATCH_LO_COLOR);
}

/* draw just the alert rows + scroll controls, box already has title/subtitle drawn
 */
static void drawHamsatVisAlerts (const SBox &box)
{
    uint16_t x = box.x + 1;
    uint16_t y0 = box.y + LISTING_Y0;
    tft.fillRect (box.x+1, y0-LISTING_OS, box.w-2, box.h - (LISTING_Y0-LISTING_OS+1), RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    time_t now = myNow();
    int min_i, max_i;
    if (hamsat_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            const HamsatAlert &a = hamsat_alerts[i];
            uint16_t y = y0 + hamsat_ss.getDisplayRow(i) * ALERT_DY;

            // match% badge, fixed width
            char mbuf[6];
            // clamp to satisfy GCC's -Wformat-truncation value-range analysis (same convention
            // as lightning.cpp) -- also just correct, since this is logically always 0-100
            #define MP_CLAMP(n) ((n) < 0 ? 0 : (n) > 999 ? 999 : (n))
            if (a.match_percent >= 0)
                snprintf (mbuf, sizeof(mbuf), "%3d%%", MP_CLAMP(a.match_percent));
            else
                snprintf (mbuf, sizeof(mbuf), " -- ");
            uint16_t bg = matchColor (a.match_percent);
            uint16_t badge_w = 4*6 + 2;               // ~4 chars at FAST_FONT width
            tft.fillRect (x, y-LISTING_OS, badge_w, ALERT_DY-2, bg);
            tft.setTextColor (getGoodTextColor(bg));
            tft.setCursor (x+1, y);
            tft.print (mbuf);

            // callsign, sat, countdown -- rest of the line. color signals status: green if the
            // pass is happening right now, yellow if it starts within 15 minutes, else fall back
            // to flagging workable passes yellow (as before) or plain white.
            char cd[8];
            formatCountdown (a.aos_at, now, cd, sizeof(cd));
            char line[24];
            if (box.w < PLOTBOX123_W)
                // narrow pane (pane 0): truncate callsign to 3 chars, then a literal 2-space
                // gap before the countdown -- NOT just field padding, since satnames like
                // AO-73/RS-44/FO-29 are exactly 5 chars and fill %-5.5s with no trailing pad,
                // which was swallowing the gap and running straight into the countdown.
                // full callsign is still shown in the tap-to-detail popup below, via a.callsign.
                snprintf (line, sizeof(line), "%-3.3s %-5.5s  %s", a.callsign, a.satname, cd);
            else
                // wide pane (1/2/3): same issue as above -- satnames like AO-123 fill the whole
                // %-6.6s field with no trailing pad, so force a literal 1-char gap before the
                // countdown instead of relying on padding.
                snprintf (line, sizeof(line), "%-7.7s%-6.6s %s", a.callsign, a.satname, cd);
            #define SOON_SECS       (15*60)            // "starting soon" threshold, seconds
            uint16_t line_color;
            if (a.aos_at && a.aos_at <= now && (a.los_at == 0 || a.los_at > now))
                line_color = RA8875_GREEN;             // in progress right now
            else if (a.aos_at && a.aos_at > now && a.aos_at - now <= SOON_SECS)
                line_color = RA8875_YELLOW;            // starting within 15 minutes
            else
                line_color = a.is_workable ? RA8875_YELLOW : RA8875_WHITE;
            tft.setTextColor (line_color);
            tft.setCursor (x + badge_w, y);
            tft.print (line);
        }
    }

    uint16_t up_color = HAMSAT_COLOR;
    uint16_t dw_color = HAMSAT_COLOR;
    hamsat_ss.drawScrollUpControl (box, up_color, HAMSAT_COLOR);
    hamsat_ss.drawScrollDownControl (box, dw_color, HAMSAT_COLOR);
}

static void drawHamsatPane (const SBox &box)
{
    prepPlotBox (box);

    const char *title = "Sat Alerts";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (HAMSAT_COLOR);
    uint16_t pw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-pw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // subtitle: source + whether personalized. maxStringW() truncates its argument in place,
    // so this must be writable storage, never a cast-away-const string literal. Pane 0 is also
    // narrower than the normal panes, so use a compact form there.
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    char sub[40];
    if (box.w < PLOTBOX123_W)
        snprintf (sub, sizeof(sub), "hams.at (%s)", hamsat_key[0] ? "key" : "no key");
    else
        snprintf (sub, sizeof(sub), "https://hams.at (%s)", hamsat_key[0] ? "has key" : "no key");
    uint16_t sw = maxStringW (sub, box.w-2);
    tft.setTextColor (hamsat_key[0] ? RA8875_WHITE : RGB565(180,140,60));
    tft.setCursor (box.x + (box.w-sw)/2, box.y + SUBTITLE_Y0);
    tft.print (sub);

    // remember where this was drawn so a tap can offer to open the web page
    hamsat_url_b.x = box.x + (box.w-sw)/2;
    hamsat_url_b.y = box.y + SUBTITLE_Y0 - 2;
    hamsat_url_b.w = sw;
    hamsat_url_b.h = 12;

    drawHamsatVisAlerts (box);
}


/* *********************************************************************************************
 * public entry points -- declared extern in HamClock.h, dispatched from wifi.cpp / plotmgmnt.cpp
 * exactly like updateOnTheAir()/checkOnTheAirTouch().
 */

/* (re)init storage/scroll state for a freshly-selected pane in box
 */
static void resetHamsatStorage (const SBox &box)
{
    free (hamsat_alerts);
    hamsat_alerts = NULL;
    n_hamsat = 0;
    hamsat_ss.init ((box.h - LISTING_Y0)/ALERT_DY, 0, 0, hamsat_ss.DIR_TOPDOWN);
}

/* called occasionally (and whenever fresh) to update and draw the Hamsat pane in box.
 * return whether io was ok.
 */
bool updateHamsat (const SBox &box, bool fresh)
{
    if (fresh) {
        ROTHOLD_CLR(PLOT_CH_SATACT);
        loadHamsatKey ();
        resetHamsatStorage (box);
    }

    bool ok = retrieveHamsat ();
    if (ok) {
        hamsat_ss.init ((box.h - LISTING_Y0)/ALERT_DY, hamsat_ss.top_vis, n_hamsat, hamsat_ss.DIR_TOPDOWN);
        if (fresh)
            hamsat_ss.scrollToNewest();
        drawHamsatPane (box);
    } else {
        plotMessage (box, RA8875_RED, "https://hams.at download error");
    }

    return (ok);
}

/* handle a tap at s known to be within box for our Pane.
 * return true if we handled it, else false to let caller change the Pane option.
 */
bool checkHamsatTouch (const SCoord &s, const SBox &box)
{
    // check for title or scroll controls -- both scroll arrows sit within the title band
    if (s.y < box.y + PANETITLE_H) {

        if (hamsat_ss.checkScrollUpTouch (s, box)) {
            if (hamsat_ss.okToScrollUp()) {
                hamsat_ss.scrollUp ();
                drawHamsatVisAlerts (box);
            }
            return (true);
        }

        if (hamsat_ss.checkScrollDownTouch (s, box)) {
            if (hamsat_ss.okToScrollDown()) {
                hamsat_ss.scrollDown ();
                drawHamsatVisAlerts (box);
            }
            return (true);
        }

        // on hold?
        if (ROTHOLD_TST(PLOT_CH_SATACT))
            return (true);

        // else tapping title always leaves this pane
        return (false);
    }

    // tap on the "https://hams.at" subtitle: offer to open the web page
    if (inBox (s, hamsat_url_b)) {

        typedef enum {
            UEX_PAGE,
            UEX_N
        } UrlExInfo;

        MenuItem mitems[UEX_N];
        mitems[UEX_PAGE] = {MENU_TOGGLE, false, 1, 2, "Open webpage?", 0};

        SBox menu_b = {s.x, s.y, 0, 0};
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, UEX_N, mitems};
        if (runMenu (menu) && mitems[UEX_PAGE].set)
            openURL ("https://hams.at");

        return (true);
    }

    // row tap: offer to set DX (and track this satellite) and/or open the alert's web page
    int row = (s.y - (box.y + LISTING_Y0)) / ALERT_DY;
    int array_index;
    if (hamsat_ss.findDataIndex (row, array_index) && array_index < n_hamsat) {
        const HamsatAlert &a = hamsat_alerts[array_index];

        typedef enum {
            HEX_NAME,
            HEX_DX,
            HEX_PAGE,
            HEX_N
        } HamsatExInfo;

        char title[40];
        snprintf (title, sizeof(title), "%s on %s", a.callsign, a.satname);

        MenuItem mitems[HEX_N];
        mitems[HEX_NAME] = {MENU_LABEL,  false, 0, 2, title,              0};
        mitems[HEX_DX]   = {MENU_TOGGLE, false, 1, 2, "Set DX",           0};
        mitems[HEX_PAGE] = {MENU_TOGGLE, false, 2, 2, "Show web page",    0};

        const uint16_t menu_x = box.x + 20;
        const uint16_t menu_h = 70;
        const uint16_t menu_max_y = box.y + box.h - menu_h - 5;
        const uint16_t menu_y = s.y < menu_max_y ? s.y : menu_max_y;
        SBox menu_b = {menu_x, menu_y, 0, 0};
        SBox ok_b;

        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, HEX_N, mitems};
        if (runMenu (menu)) {

            if (mitems[HEX_DX].set && a.grid[0]) {
                LatLong ll;
                if (maidenhead2ll (ll, a.grid))
                    newDX (ll, a.grid, a.callsign);
                if (a.satname[0])
                    setSatFromName (a.satname);
            }

            if (mitems[HEX_PAGE].set && a.url[0])
                openURL (a.url);
        }

        return (true);
    }

    return (false);
}

/* If ms is hovering over an alert row in the Sat Alerts pane, return that station's grid location
 * in *ll plus a short label, and return true. Lets infobox ring/highlight it on the map the same
 * way hovering a Storms or Launches row does (and the way ONTA highlights a station on the map,
 * though ONTA's version goes through the DXSpot-based over_pane path instead since it's a spot
 * type shared with other panes).
 */
bool getHamsatPaneHover (const SCoord &ms, LatLong *ll, char *label, size_t label_len)
{
    if (n_hamsat == 0)
        return false;

    PlotPane pp = findPaneChoiceNow (PLOT_CH_SATACT);
    if (pp == PANE_NONE)
        return false;
    const SBox &box = plot_b[pp];
    if (!inBox (ms, box) || ms.y < box.y + LISTING_Y0)          // not over the alert-rows region
        return false;

    int row = (ms.y - (box.y + LISTING_Y0)) / ALERT_DY;
    int index;
    if (!hamsat_ss.findDataIndex (row, index) || index < 0 || index >= n_hamsat)
        return false;

    const HamsatAlert &a = hamsat_alerts[index];
    if (!a.grid[0] || !maidenhead2ll (*ll, a.grid))
        return false;

    snprintf (label, label_len, "%s on %s", a.callsign, a.satname);
    return true;
}

/* draw each currently-loaded alert's callsign on the map at its grid location, same convention as
 * drawDXClusterSpotsOnMap()/drawDXPedsOnMap(). called once per main map refresh cycle.
 */
void drawHamsatOnMap (void)
{
    if (n_hamsat == 0)
        return;

    // only bother if the pane is actually in use somewhere
    if (findPaneForChoice (PLOT_CH_SATACT) == PANE_NONE)
        return;

    for (int i = 0; i < n_hamsat; i++) {
        const HamsatAlert &a = hamsat_alerts[i];
        if (!a.grid[0] || !a.callsign[0])
            continue;

        LatLong ll;
        if (!maidenhead2ll (ll, a.grid))
            continue;

        DXSpot spot{};
        quietStrncpy (spot.tx_call, a.callsign, sizeof(spot.tx_call));
        spot.tx_ll = ll;
        spot.kHz = a.mhz * 1000.0F;             // DXSpot wants kHz, we store MHz

        drawSpotLabelOnMap (spot, LOME_TXEND, LOMD_ALL);
    }
}
