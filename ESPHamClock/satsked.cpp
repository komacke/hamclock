/* Satellite Group Schedule -- an InstantTrack-style hour-by-hour visibility grid for a
 * user-configured group of satellites, viewed from DE.
 *
 * Takes over the full map area the same way sattool.cpp's drawSatTool() does: draw once,
 * then run a self-contained modal loop (waitForUser()) until the user taps Resume or types
 * Return/Esc. Controls: tap a satellite's checklist to change group membership, tap </> to
 * step the displayed day forward/backward, tap the TZ button to toggle UTC vs DE local time
 * labels, tap Resume (or Esc/Return) to leave.
 *
 * Visibility for each satellite/hour cell is sampled several times across that hour using the
 * same P13 Observer/Satellite primitives the rest of HamClock already relies on for pass
 * prediction; a cell is marked visible if elevation is above the horizon at any sample.
 */

#include "HamClock.h"


#define MAX_GRP_SATS    20                              // matches InstantTrack's 20-satellite limit
#define SK_HOURS        24                               // columns
#define SK_TB           60                               // top border for title/controls
#define SK_LB           110                              // left border for sat name labels
#define SK_RB           20                               // right border
#define SK_BB           20                               // bottom border
#define SK_ROW_H        20                               // row height, px
#define SK_GRIDTOP      18                               // y offset from grid box top to first row line
#define SK_TO           (60*1000)                        // time out, millis
#define SK_GRID_C       RGB565(60,60,60)                 // grid line color
#define SK_UP_C         RGB565(80,220,80)                 // visible cell color
#define SK_PARTIAL_C    RGB565(230,200,40)                 // footprint-only overlap (no freq match)
#define SK_TCA_C        RGB565(255,215,0)                 // TCA line color in the tap popup
#define SK_HDR_C        RGB565(150,250,255)               // header/title color

static char grp_names[MAX_GRP_SATS][NV_SATNAME_LEN];
static int n_grp;
static int day_offset;                                   // 0 = today, UTC day boundaries
static bool show_utc = true;
static ScrollState sk_ss;                                 // schedule grid row scrolling


/* *********************************************************************************************
 * NVRAM persistence -- newline-separated satellite names
 */

static void loadSatGroup (void)
{
    char buf[NV_SATGROUP_LEN];
    n_grp = 0;
    if (!NVReadString (NV_SATGROUP, buf))
        return;
    char *saveptr = NULL;
    char *tok = strtok_r (buf, "\n", &saveptr);
    while (tok && n_grp < MAX_GRP_SATS) {
        strncpy (grp_names[n_grp], tok, NV_SATNAME_LEN-1);
        grp_names[n_grp][NV_SATNAME_LEN-1] = '\0';
        if (grp_names[n_grp][0])
            n_grp++;
        tok = strtok_r (NULL, "\n", &saveptr);
    }
}

static void saveSatGroup (void)
{
    char buf[NV_SATGROUP_LEN];
    size_t bi = 0;
    buf[0] = '\0';
    for (int i = 0; i < n_grp; i++) {
        size_t nlen = strlen (grp_names[i]);
        if (bi + nlen + 2 >= sizeof(buf))
            break;
        if (i > 0)
            buf[bi++] = '\n';
        memcpy (buf+bi, grp_names[i], nlen);
        bi += nlen;
    }
    buf[bi] = '\0';
    NVWriteString (NV_SATGROUP, buf);
}

/* let the user pick up to MAX_GRP_SATS satellites from the full TLE list.
 * N.B. simple flat multi-select menu -- fine for the typical list sizes HamClock deals with.
 */
static void querySatGroup (const SBox &box)
{
    // N.B. getAllSatNames() returns triples back-to-back with no separator: name, TLE line 1,
    // TLE line 2, for every satellite -- not a flat list of names. Caller owns every string
    // plus the array itself.
    const char **raw = getAllSatNames();
    if (!raw)
        return;
    int n_raw = 0;
    while (raw[n_raw])
        n_raw++;
    int n_all = n_raw / 3;
    if (n_all == 0) {
        for (int i = 0; i < n_raw; i++)
            free ((void*)raw[i]);
        free (raw);
        return;
    }
    const char **all_names = (const char **) malloc (n_all * sizeof(const char*));
    for (int i = 0; i < n_all; i++)
        all_names[i] = raw[i*3];                        // just the name, skip the two TLE lines

    // cap each displayed label regardless of how long the underlying name actually is --
    // the menu auto-sizes its width from the widest label, and an unexpectedly long name
    // (this list isn't curated by us) can blow the computed menu width past the screen and
    // crash the pixel-capture routine underneath it.
    #define SK_MENU_NAME_W  16
    char (*trunc_names)[SK_MENU_NAME_W] = (char (*)[SK_MENU_NAME_W]) malloc (n_all * SK_MENU_NAME_W);
    if (!trunc_names)
        fatalError ("No room for %d sat group menu labels", n_all);

    MenuItem *mitems = (MenuItem *) malloc (n_all * sizeof(MenuItem));
    if (!mitems)
        fatalError ("No room for %d sat group menu items", n_all);

    for (int i = 0; i < n_all; i++) {
        bool is_set = false;
        for (int j = 0; j < n_grp; j++)
            if (strcmp (grp_names[j], all_names[i]) == 0)
                is_set = true;
        snprintf (trunc_names[i], SK_MENU_NAME_W, "%s", all_names[i]);
        mitems[i] = {MENU_0OFN, is_set, 1, 2, trunc_names[i], 0};
    }

    // measure the actual widest label in the same font menu.cpp uses internally (FAST_FONT),
    // rather than guessing a per-character pixel width -- an earlier guess turned out well off,
    // which is what caused the menu to overflow the canonical screen width and crash.
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    int widest = 0;
    for (int i = 0; i < n_all; i++) {
        int w = getTextWidth (trunc_names[i]);
        if (w > widest)
            widest = w;
    }
    const int col_overhead = 2 /*indent*/ + 9 /*checkbox*/ + 4 /*fudge*/;
    int col_w = widest + col_overhead;

    #define SK_MENU_SAFE_W   700                        // canonical screen is 800; leave margin
    int n_cols = col_w > 0 ? SK_MENU_SAFE_W / col_w : 1;
    if (n_cols < 1)
        n_cols = 1;

    SBox menu_b = {(uint16_t)(box.x + 60), (uint16_t)(box.y + 40), 0, 0};
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, n_cols, n_all, mitems};
    if (runMenu (menu)) {
        n_grp = 0;
        for (int i = 0; i < n_all && n_grp < MAX_GRP_SATS; i++) {
            if (mitems[i].set) {
                strncpy (grp_names[n_grp], all_names[i], NV_SATNAME_LEN-1);
                grp_names[n_grp][NV_SATNAME_LEN-1] = '\0';
                n_grp++;
            }
        }
        saveSatGroup ();
    }

    free (mitems);
    free (trunc_names);
    free (all_names);
    for (int i = 0; i < n_raw; i++)
        free ((void*)raw[i]);
    free (raw);
}


/* *********************************************************************************************
 * visibility computation
 */

/* return a DateTime for the given time -- same conversion earthsat.cpp's userDateTime() does,
 * duplicated here since that one is private to earthsat.cpp
 */
static DateTime toDateTime (time_t t)
{
    return (DateTime (year(t), month(t), day(t), hour(t), minute(t), second(t)));
}

/* return sat's azimuth and elevation, in degrees, at DE at time t
 */
static void satAzEl (Satellite *sat, Observer *de_obs, time_t t, float &az_deg, float &el_deg)
{
    DateTime dt = toDateTime (t);
    sat->predict (dt);
    float range, rate;
    // N.B. topo() returns az/el already in degrees, not radians -- do not rad2deg() these
    sat->topo (de_obs, el_deg, az_deg, range, rate);
}


/* *********************************************************************************************
 * satellite co-visibility -- "can satellite/DE/Sun A see satellite/DE/Sun B", i.e. is Earth NOT
 * blocking the line of sight between them. mirrors InstantTrack's "Multiple Satellite
 * Co-Visibility" screen.
 *
 * satellite positions (Satellite::S, set by predict()) are true geocentric Cartesian, in km, in
 * an Earth-fixed (rotating) frame -- confirmed by Satellite::geo() converting S straight to
 * lat/lng with no separate time/rotation correction. DE's position is built the same way from
 * its lat/lon.
 *
 * satellite-satellite cells mean something different from DE/Sun cells: two satellites are
 * marked GREEN if their ground footprints overlap (there's a point on Earth both could
 * potentially work through) AND their published frequencies overlap too (real interference/
 * relay risk, e.g. the TEVEL series sharing spectrum); YELLOW if footprints overlap but
 * frequencies don't (or aren't known). DE-satellite and Sun-satellite cells keep the original
 * meaning (DE within footprint / satellite sunlit) since footprint/frequency doesn't apply to
 * either of those.
 *
 * the Sun is different: Sun::SUN is a unit *direction* vector, not a true-scale position (the
 * Sun is effectively at infinity for shadow geometry). Satellite-Sun visibility uses the
 * library's own eclipsed() (shadow) test, and DE-Sun visibility is "is it daytime" via
 * getSolarCir().
 *
 * the Moon is deliberately NOT included: getLunarCir() only gives topocentric az/el/range from
 * one ground point, not the same Earth-fixed geocentric frame satellite positions use, and
 * approximating it would need a real RA/Dec+GHA -> Cartesian conversion that isn't done here.
 */

typedef enum { CV_DE, CV_SUN, CV_SAT } CVType;

// result of comparing two entities
typedef enum { CV_NONE, CV_PARTIAL, CV_FULL } CVResult;    // FULL: footprint+freq, or plain "visible"
                                                            // PARTIAL: footprint only, no freq data/match

typedef struct {
    CVType type;
    char label[10];
    Satellite *sat;                                       // only for CV_SAT; owned, must delete
    int norad;                                             // only for CV_SAT
    SatFreq *freqs;                                        // only for CV_SAT; owned, must free
    int n_freqs;
} CVEntity;

/* return whether a's and b's ground footprints overlap -- i.e. there's some point on Earth
 * both could potentially work through. uses the standard horizon-to-horizon (0 elevation)
 * footprint radius, same definition the map's own footprint circles use.
 */
static bool footprintsOverlap (CVEntity &a, CVEntity &b)
{
    float lat_a, lng_a, lat_b, lng_b;
    a.sat->geo (lat_a, lng_a);
    b.sat->geo (lat_b, lng_b);

    float cos_d = sinf(lat_a)*sinf(lat_b) + cosf(lat_a)*cosf(lat_b)*cosf(lng_a-lng_b);
    if (cos_d > 1) cos_d = 1; else if (cos_d < -1) cos_d = -1;
    float d = acosf (cos_d);                                // angular distance between subsat points

    float ra = a.sat->viewingRadius (0);
    float rb = b.sat->viewingRadius (0);

    return (d < ra + rb);
}

/* return whether any of a's uplink/downlink ranges overlap any of b's -- real interference/
 * relay risk, not just "both use the same band loosely"
 */
static bool freqsOverlap (const CVEntity &a, const CVEntity &b)
{
    if (!a.freqs || !b.freqs)
        return (false);

    for (int i = 0; i < a.n_freqs; i++) {
        for (int ap = 0; ap < 2; ap++) {
            long alo = ap==0 ? a.freqs[i].ul_lo : a.freqs[i].dl_lo;
            if (alo == 0)
                continue;
            long ahi_raw = ap==0 ? a.freqs[i].ul_hi : a.freqs[i].dl_hi;
            long ahi = ahi_raw ? ahi_raw : alo;

            for (int j = 0; j < b.n_freqs; j++) {
                for (int bp = 0; bp < 2; bp++) {
                    long blo = bp==0 ? b.freqs[j].ul_lo : b.freqs[j].dl_lo;
                    if (blo == 0)
                        continue;
                    long bhi_raw = bp==0 ? b.freqs[j].ul_hi : b.freqs[j].dl_hi;
                    long bhi = bhi_raw ? bhi_raw : blo;

                    if (alo <= bhi && blo <= ahi)
                        return (true);
                }
            }
        }
    }
    return (false);
}

/* return how entity a relates to entity b at time t (symmetric).
 * CV_FULL (green) is reserved exclusively for a genuine satellite pair whose footprints AND
 * frequencies both overlap -- real interference/relay risk. any pair involving DE or Sun uses
 * CV_PARTIAL (yellow) when visible/sunlit/up, since frequency isn't a meaningful concept for
 * those and showing green there would misleadingly imply a frequency match that was never
 * checked. a satellite pair with footprint overlap but no frequency match is also CV_PARTIAL.
 */
static CVResult cvCompare (CVEntity &a, CVEntity &b, Sun &sun, Observer &de_obs, time_t t)
{
    if (a.type == CV_SUN && b.type == CV_SUN)
        return (CV_NONE);
    if (a.type == CV_DE && b.type == CV_DE)
        return (CV_NONE);

    if (a.type == CV_SUN || b.type == CV_SUN) {
        CVEntity &other = (a.type == CV_SUN) ? b : a;
        if (other.type == CV_DE) {
            AstroCir cir;
            getSolarCir (t, de_ll, cir);
            return (cir.el > 0 ? CV_PARTIAL : CV_NONE);
        } else {
            return (!other.sat->eclipsed (&sun) ? CV_PARTIAL : CV_NONE);
        }
    }

    if (a.type == CV_DE || b.type == CV_DE) {
        CVEntity &sate = (a.type == CV_DE) ? b : a;
        float az_deg, el_deg;
        satAzEl (sate.sat, &de_obs, t, az_deg, el_deg);
        return (el_deg > 0 ? CV_PARTIAL : CV_NONE);
    }

    // both CV_SAT
    if (!footprintsOverlap (a, b))
        return (CV_NONE);
    return (freqsOverlap (a, b) ? CV_FULL : CV_PARTIAL);
}

#define SK_STEP_S       120                              // pass-detection step, secs (2 min)
#define SK_MAX_PASSES   16                               // per satellite per day

typedef struct {
    time_t aos_t, tca_t, los_t;
    float aos_az, tca_el, los_az;
} SKPass;

/* find all passes of sat at DE within [day_t0, day_t0+24h), stepping every SK_STEP_S seconds.
 * AOS/LOS times are approximate to within one step (2 min); TCA (max elevation) likewise.
 * a pass already in progress at day_t0, or still in progress at day_t0+24h, is included with
 * its AOS or LOS clipped to the day boundary.
 * returns the number of passes found, up to SK_MAX_PASSES.
 */
static int findDayPasses (Satellite *sat, Observer *de_obs, time_t day_t0, SKPass passes[SK_MAX_PASSES])
{
    int n = 0;
    time_t day_end = day_t0 + 86400;

    float az0, el0;
    satAzEl (sat, de_obs, day_t0, az0, el0);
    bool in_pass = el0 > 0;

    SKPass cur;
    if (in_pass) {
        cur.aos_t = day_t0;
        cur.aos_az = az0;
        cur.tca_t = day_t0;
        cur.tca_el = el0;
    }

    for (time_t t = day_t0 + SK_STEP_S; t < day_end && n < SK_MAX_PASSES; t += SK_STEP_S) {
        float az, el;
        satAzEl (sat, de_obs, t, az, el);
        bool up = el > 0;

        if (up && !in_pass) {
            // AOS
            in_pass = true;
            cur.aos_t = t;
            cur.aos_az = az;
            cur.tca_t = t;
            cur.tca_el = el;
        } else if (up && in_pass) {
            if (el > cur.tca_el) {
                cur.tca_el = el;
                cur.tca_t = t;
            }
        } else if (!up && in_pass) {
            // LOS
            cur.los_t = t;
            cur.los_az = az;
            passes[n++] = cur;
            in_pass = false;
        }
    }

    if (in_pass && n < SK_MAX_PASSES) {
        // still up at end of day -- close it out at the boundary
        float az, el;
        satAzEl (sat, de_obs, day_end, az, el);
        cur.los_t = day_end;
        cur.los_az = az;
        passes[n++] = cur;
    }

    return (n);
}

/* fill hour_el[0..23] with the peak elevation (degrees) sat reaches at DE during each hour of
 * the day starting at grid_t0, sampled every SK_STEP_S seconds. a value <= 0 means never above
 * the horizon during that hour.
 */
static void buildHourElevations (Satellite *sat, Observer *de_obs, time_t grid_t0, float hour_el[SK_HOURS])
{
    for (int h = 0; h < SK_HOURS; h++)
        hour_el[h] = -99;

    for (time_t t = grid_t0; t < grid_t0 + 86400; t += SK_STEP_S) {
        int h = (t - grid_t0) / 3600;
        if (h < 0 || h >= SK_HOURS)
            continue;
        float az_deg, el_deg;
        satAzEl (sat, de_obs, t, az_deg, el_deg);
        if (el_deg > hour_el[h])
            hour_el[h] = el_deg;
    }
}


/* *********************************************************************************************
 * drawing
 */

/* return start of the UTC day containing t0, shifted by day_offset days
 */
static time_t dayStart (time_t t0)
{
    time_t ds = t0 - (t0 % 86400) + (time_t)day_offset*86400;
    return (ds);
}

/* return the UTC instant that grid column 0 actually represents: UTC midnight of the selected
 * day when showing UTC, or the UTC instant of LOCAL midnight of the corresponding local day
 * when showing local time. this makes column h simply "hour h" of whichever day/timezone is
 * being shown -- no separate label-rotation is needed, and critically, the underlying data
 * queried for each column is for the CORRECT local hour, not just relabeled UTC hours.
 */
static time_t gridT0 (time_t day_t0)
{
    return (show_utc ? day_t0 : day_t0 - getTZ (de_tz));
}

/* draw str centered both ways within b, in FAST_FONT. drawStringInBox() only approximates
 * vertical centering (b.h/5, tuned for its own callers' box proportions elsewhere in the app);
 * this does real centering using the string's actual measured height.
 */
static void drawCenteredButton (const char *str, const SBox &b, uint16_t color)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t w, h;
    getTextBounds (str, &w, &h);
    tft.setTextColor (color);
    tft.setCursor (b.x + (b.w > w ? (b.w-w)/2 : 0), b.y + (b.h > h ? (b.h-h)/2 : 0));
    tft.print (str);
}

/* draw a small filled up- or down-pointing triangle centered within b -- there's no arrow glyph
 * in this font, so these mirror the < / > text buttons using the same drawing primitive
 * ScrollState's own arrows use.
 */
static void drawCenteredTriangle (const SBox &b, bool up, uint16_t color)
{
    uint16_t cx = b.x + b.w/2;
    uint16_t cy = b.y + b.h/2;
    const uint16_t tw = 8, th = 8;
    if (up)
        tft.fillTriangle (cx, cy-th/2, cx-tw/2, cy+th/2, cx+tw/2, cy+th/2, color);
    else
        tft.fillTriangle (cx, cy+th/2, cx-tw/2, cy-th/2, cx+tw/2, cy-th/2, color);
}

/* draw a number followed by a small circle (the standard "no degree glyph in this font" stand-in
 * already used elsewhere in HamClock, e.g. earthsat.cpp's DX info panel) at the cursor position
 */
static void printDeg (int deg, uint16_t color)
{
    tft.print (deg);
    tft.drawCircle (tft.getCursorX()+2, tft.getCursorY()+2, 1, color);
}

static void drawSKHeader (const SBox &box, time_t day_t0, const SBox &resume_b, const SBox &prev_b,
                            const SBox &next_b, const SBox &tz_b, const SBox &grp_b,
                            const SBox &up_b, const SBox &dn_b)
{
    fillSBox (box, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (SK_HDR_C);
    tft.setCursor (box.x+5, box.y+30);
    tft.print ("Satellite Group Schedule");

    // day label
    time_t local_t = day_t0;
    int tz_secs = show_utc ? 0 : getTZ (de_tz);
    local_t += tz_secs;
    char day_str[32];
    struct tm tm_v;
    time_t tm_t = local_t;
    gmtime_r (&tm_t, &tm_v);
    strftime (day_str, sizeof(day_str), "%a %Y-%m-%d", &tm_v);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (box.x+5, box.y+44);
    tft.print (day_str);
    tft.print (show_utc ? " UTC" : " Local");

    // legend, right-justified on the same row as the day/time string. green cells show the
    // peak elevation reached during that hour (e.g. "39" plus a small circle standing in for the
    // degree glyph, which this compact font doesn't have -- same convention used elsewhere in
    // HamClock, e.g. the DX info panel).
    {
        const char *ltxt1 = "visible, showing peak elevation that hour (e.g. 39";
        const char *ltxt2 = ")";
        const uint16_t swatch_w = 10, swatch_gap = 4, circle_w = 6, right_margin = 10;
        uint16_t t1w = getTextWidth (ltxt1);
        uint16_t t2w = getTextWidth (ltxt2);
        uint16_t total_w = swatch_w + swatch_gap + t1w + circle_w + t2w;
        uint16_t lx = box.x + box.w > right_margin+total_w ? box.x+box.w-right_margin-total_w : box.x+5;

        tft.fillRect (lx, box.y+38, swatch_w, swatch_w, SK_UP_C);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (lx+swatch_w+swatch_gap, box.y+44);
        tft.print (ltxt1);
        printDeg (39, RA8875_WHITE);
        tft.print (ltxt2);
    }

    // day nav, tz toggle, group-edit, scroll and resume buttons -- all one right-aligned cluster
    // so they can never collide with the title text on the left. N.B. FAST_FONT, not SMALL_FONT
    // (which is actually a 16pt font and far too large for these compact boxes).
    drawCenteredButton ("Resume", resume_b, RA8875_GREEN);
    drawCenteredButton ("<", prev_b, RA8875_WHITE);
    drawCenteredButton (">", next_b, RA8875_WHITE);
    drawCenteredButton (show_utc ? "UTC" : "Local", tz_b, RA8875_YELLOW);
    drawCenteredButton ("Edit Group", grp_b, RA8875_GREEN);
    drawCenteredTriangle (up_b, true, sk_ss.okToScrollUp() ? RA8875_WHITE : RA8875_BLACK);
    drawCenteredTriangle (dn_b, false, sk_ss.okToScrollDown() ? RA8875_WHITE : RA8875_BLACK);
}

/* draw the grid itself: one row per grp_names[], columns = hours.
 * N.B. box is the full available area below the header.
 */
/* show a small popup with satellite name, time, elevation and azimuth for the cell at the given
 * satellite row and time t (the exact time is interpolated from where within the hour column was
 * tapped, not just the hour boundary). modeled directly on sattool.cpp's drawSTPopup() -- HamClock
 * has no true mouse-hover, only taps, so this is the standard "inspect a data point" convention
 * used elsewhere in the app.
 */
/* compute the grid's column width and left edge x, from the grid box -- shared by drawSKGrid()
 * and the touch handler so hit-testing always matches what's actually drawn.
 */
static void gridGeom (const SBox &box, uint16_t &col_w, uint16_t &x0)
{
    col_w = (box.w - SK_LB - SK_RB) / SK_HOURS;
    x0 = box.x + SK_LB;
}

/* build the entity list: DE, Sun, then every satellite currently in the group.
 * caller must call freeCVEntities() when done (owns each entity's Satellite*).
 * returns the number of entities built.
 */
static int buildCVEntities (CVEntity ents[MAX_GRP_SATS+2])
{
    int n = 0;

    strncpy (ents[n].label, "DE", sizeof(ents[n].label));
    ents[n].type = CV_DE;
    ents[n].sat = NULL;
    ents[n].freqs = NULL;
    ents[n].n_freqs = 0;
    n++;

    strncpy (ents[n].label, "Sun", sizeof(ents[n].label));
    ents[n].type = CV_SUN;
    ents[n].sat = NULL;
    ents[n].freqs = NULL;
    ents[n].n_freqs = 0;
    n++;

    for (int i = 0; i < n_grp; i++) {
        int norad = 0;
        Satellite *sat = lookupSatByName (grp_names[i], &norad);
        if (!sat)
            continue;
        strncpySubChar (ents[n].label, grp_names[i], ' ', '_', sizeof(ents[n].label));
        ents[n].type = CV_SAT;
        ents[n].sat = sat;
        ents[n].norad = norad;
        ents[n].freqs = NULL;
        ents[n].n_freqs = norad ? getSatFreqs (norad, &ents[n].freqs) : 0;
        if (ents[n].n_freqs <= 0)
            ents[n].freqs = NULL;                           // getSatFreqs may leave it unset if 0
        n++;
    }

    return (n);
}

static void freeCVEntities (CVEntity ents[], int n)
{
    for (int i = 0; i < n; i++) {
        if (ents[i].sat)
            delete ents[i].sat;
        if (ents[i].freqs)
            free (ents[i].freqs);
    }
}

/* draw the NxN co-visibility matrix within box for the given entity list at time t.
 * a filled dot means the row entity can see the column entity; the diagonal is left blank.
 */
/* compute the co-vis matrix's cell size and origin -- shared by drawCVMatrix() and the tap
 * handler so hit-testing always matches what's actually drawn.
 */
static void cvGeom (const SBox &box, int n, uint16_t &lb, uint16_t &tb, uint16_t &cell,
                     uint16_t &x0, uint16_t &y0)
{
    lb = 50;                                                // room for row labels
    tb = 34;                                                // room for column labels
    cell = (box.w - lb) / n;
    if (cell > (box.h - tb) / n)
        cell = (box.h - tb) / n;
    if (cell < 6)
        cell = 6;                                           // never shrink to nothing
    x0 = box.x + lb;
    y0 = box.y + tb;
}

typedef struct {
    time_t start_t, end_t;
    bool freq_ovl;
} CVWindow;

#define CV_MAX_WINDOWS  8

/* find the time windows within [day_t0, day_t0+24h) during which a's and b's footprints
 * overlap (only meaningful for two satellites). freq_ovl is the same for every window since
 * frequency assignments don't change through the day -- computed once, not per-sample.
 */
static int findCVWindows (CVEntity &a, CVEntity &b, time_t day_t0, CVWindow windows[CV_MAX_WINDOWS])
{
    if (a.type != CV_SAT || b.type != CV_SAT)
        return (0);

    bool freq_ovl = freqsOverlap (a, b);

    int n = 0;
    time_t day_end = day_t0 + 86400;

    DateTime dt0 = toDateTime (day_t0);
    a.sat->predict (dt0);
    b.sat->predict (dt0);
    bool in_window = footprintsOverlap (a, b);

    CVWindow cur;
    if (in_window) {
        cur.start_t = day_t0;
        cur.freq_ovl = freq_ovl;
    }

    for (time_t t = day_t0 + SK_STEP_S; t < day_end && n < CV_MAX_WINDOWS; t += SK_STEP_S) {
        DateTime dt = toDateTime (t);
        a.sat->predict (dt);
        b.sat->predict (dt);
        bool over = footprintsOverlap (a, b);

        if (over && !in_window) {
            in_window = true;
            cur.start_t = t;
            cur.freq_ovl = freq_ovl;
        } else if (!over && in_window) {
            cur.end_t = t;
            windows[n++] = cur;
            in_window = false;
        }
    }

    if (in_window && n < CV_MAX_WINDOWS) {
        cur.end_t = day_end;
        windows[n++] = cur;
    }

    return (n);
}

/* show today's footprint-overlap windows for satellites row/col, near popup_b's location
 */
static void drawCVPopup (CVEntity &row_e, CVEntity &col_e, time_t t, const SBox &popup_b)
{
    fillSBox (popup_b, RA8875_BLACK);
    drawSBox (popup_b, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    char nbuf[24];
    snprintf (nbuf, sizeof(nbuf), "%.8s - %.8s", row_e.label, col_e.label);
    tft.setCursor (popup_b.x+4, popup_b.y+2);
    tft.print (nbuf);

    time_t day_t0 = t - (t % 86400);
    CVWindow windows[CV_MAX_WINDOWS];
    int n_win = findCVWindows (row_e, col_e, day_t0, windows);

    if (n_win == 0) {
        tft.setCursor (popup_b.x+4, popup_b.y+14);
        tft.print ("no overlap today");
    } else {
        for (int i = 0; i < n_win && i < 6; i++) {
            char buf[24];
            snprintf (buf, sizeof(buf), "%02d:%02d-%02d:%02d %s",
                        hour(windows[i].start_t), minute(windows[i].start_t),
                        hour(windows[i].end_t), minute(windows[i].end_t),
                        windows[i].freq_ovl ? "freq" : "");
            tft.setTextColor (windows[i].freq_ovl ? SK_UP_C : SK_PARTIAL_C);
            tft.setCursor (popup_b.x+4, popup_b.y+14+i*10);
            tft.print (buf);
        }
    }

    tft.drawPR();
}

static void drawCVMatrix (const SBox &box, CVEntity ents[], int n, time_t t)
{
    fillSBox (box, RA8875_BLACK);
    if (n == 0)
        return;

    Sun sun;
    sun.predict (toDateTime (t));
    Observer de_obs (de_ll.lat_d, de_ll.lng_d, 0);

    // update each satellite's position for time t once, up front, rather than per pair
    DateTime dt = toDateTime (t);
    for (int i = 0; i < n; i++)
        if (ents[i].type == CV_SAT)
            ents[i].sat->predict (dt);

    uint16_t lb, tb, cell, x0, y0;
    cvGeom (box, n, lb, tb, cell, x0, y0);

    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // column labels, rotated isn't available, so just abbreviate to first couple chars --
    // tap a column header for the full name (see drawSatCoVis()'s tap handler)
    tft.setTextColor (RA8875_WHITE);
    for (int c = 0; c < n; c++) {
        char cbuf[3];
        snprintf (cbuf, sizeof(cbuf), "%.2s", ents[c].label);
        tft.setCursor (x0 + c*cell + 1, box.y + 10);
        tft.print (cbuf);
    }

    for (int r = 0; r < n; r++) {

        char rbuf[10];
        snprintf (rbuf, sizeof(rbuf), "%.8s", ents[r].label);
        tft.setTextColor (RA8875_WHITE);
        int yoff = cell/2 - 4;
        if (yoff < 0)
            yoff = 0;
        tft.setCursor (box.x+2, y0 + r*cell + yoff);
        tft.print (rbuf);

        for (int c = 0; c < n; c++) {
            if (r == c)
                continue;                                   // self, leave blank
            CVResult res = cvCompare (ents[r], ents[c], sun, de_obs, t);
            if (res == CV_NONE)
                continue;
            uint16_t cell_c = (res == CV_FULL) ? SK_UP_C : SK_PARTIAL_C;
            tft.fillRect (x0 + c*cell + 1, y0 + r*cell + 1, cell-1, cell-1, cell_c);
        }
    }

    // grid lines
    for (int i = 0; i <= n; i++) {
        tft.drawLine (x0, y0+i*cell, x0+n*cell, y0+i*cell, SK_GRID_C);
        tft.drawLine (x0+i*cell, y0, x0+i*cell, y0+n*cell, SK_GRID_C);
    }
}

static void drawSKPopup (int row, time_t grid_t0, time_t tap_t, const SBox &popup_b)
{
    fillSBox (popup_b, RA8875_BLACK);
    drawSBox (popup_b, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    char nbuf[12];
    strncpySubChar (nbuf, grp_names[row], ' ', '_', sizeof(nbuf));
    tft.setCursor (popup_b.x+4, popup_b.y+2);
    tft.print (nbuf);

    Satellite *sat = lookupSatByName (grp_names[row]);
    if (!sat) {
        tft.setCursor (popup_b.x+4, popup_b.y+14);
        tft.print ("no TLE");
        tft.drawPR();
        return;
    }

    Observer de_obs (de_ll.lat_d, de_ll.lng_d, 0);
    SKPass passes[SK_MAX_PASSES];
    int n_passes = findDayPasses (sat, &de_obs, grid_t0, passes);
    delete sat;

    // find the pass overlapping the tapped time, else fall back to the closest one that day
    int best = -1;
    time_t best_dist = 0;
    for (int p = 0; p < n_passes; p++) {
        if (tap_t >= passes[p].aos_t && tap_t <= passes[p].los_t) {
            best = p;
            break;
        }
        time_t dist = tap_t < passes[p].aos_t ? passes[p].aos_t - tap_t : tap_t - passes[p].los_t;
        if (best < 0 || dist < best_dist) {
            best = p;
            best_dist = dist;
        }
    }

    if (best < 0) {
        tft.setCursor (popup_b.x+4, popup_b.y+14);
        tft.print ("no pass today");
        tft.drawPR();
        return;
    }

    const SKPass &ps = passes[best];
    int tz_secs = show_utc ? 0 : getTZ (de_tz);

    char buf[40];
    time_t la = ps.aos_t + tz_secs;
    snprintf (buf, sizeof(buf), "AOS %02d:%02d  Az %.0f", hour(la), minute(la), ps.aos_az);
    tft.setCursor (popup_b.x+4, popup_b.y+14);
    tft.print (buf);

    time_t lt = ps.tca_t + tz_secs;
    snprintf (buf, sizeof(buf), "TCA %02d:%02d  El %.0f", hour(lt), minute(lt), ps.tca_el);
    tft.setCursor (popup_b.x+4, popup_b.y+24);
    tft.setTextColor (SK_TCA_C);
    tft.print (buf);
    tft.setTextColor (RA8875_WHITE);

    time_t ll = ps.los_t + tz_secs;
    snprintf (buf, sizeof(buf), "LOS %02d:%02d  Az %.0f", hour(ll), minute(ll), ps.los_az);
    tft.setCursor (popup_b.x+4, popup_b.y+34);
    tft.print (buf);

    tft.setTextColor (RGB565(160,160,160));
    tft.setCursor (popup_b.x+4, popup_b.y+44);
    tft.print (show_utc ? "UTC" : "Local");

    tft.drawPR();
}

static void drawSKGrid (const SBox &box, time_t grid_t0)
{
    fillSBox (box, RA8875_BLACK);

    Observer de_obs (de_ll.lat_d, de_ll.lng_d, 0);

    uint16_t col_w, x0;
    gridGeom (box, col_w, x0);

    int min_i, max_i;
    int n_vis = n_grp > 0 ? sk_ss.getVisDataIndices (min_i, max_i) : 0;

    // hour ticks across top -- centered in each column, not left-anchored against the line.
    // N.B. column h always literally IS hour h of the displayed timezone -- the caller passes
    // in a day origin already shifted for UTC vs local, so no relabeling/rotation is done here.
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    for (int h = 0; h < SK_HOURS; h++) {
        char hbuf[4];
        snprintf (hbuf, sizeof(hbuf), "%d", h);
        uint16_t hw = getTextWidth (hbuf);
        tft.setCursor (x0 + h*col_w + (col_w-hw)/2, box.y + 6);
        tft.print (hbuf);
        tft.drawLine (x0 + h*col_w, box.y+SK_GRIDTOP, x0 + h*col_w, box.y+SK_GRIDTOP+n_vis*SK_ROW_H, SK_GRID_C);
    }
    tft.drawLine (x0 + SK_HOURS*col_w, box.y+SK_GRIDTOP, x0 + SK_HOURS*col_w, box.y+SK_GRIDTOP+n_vis*SK_ROW_H, SK_GRID_C);

    if (n_grp == 0) {
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (x0, box.y+40);
        tft.print ("No satellites in group -- tap Edit Group to add some");
        return;
    }

    for (int i = min_i; i <= max_i; i++) {

        int disp_row = sk_ss.getDisplayRow (i);
        uint16_t row_y = box.y + SK_GRIDTOP + disp_row*SK_ROW_H;

        // name label -- vertically centered in the row
        tft.setTextColor (RA8875_WHITE);
        char nbuf[12];
        strncpySubChar (nbuf, grp_names[i], ' ', '_', sizeof(nbuf));
        uint16_t nw, nh;
        getTextBounds (nbuf, &nw, &nh);
        tft.setCursor (box.x+2, row_y + (SK_ROW_H > nh ? (SK_ROW_H-nh)/2 : 0));
        tft.print (nbuf);

        // look up TLE fresh each draw -- cheap, and keeps this decoupled from sat_state[]
        Satellite *sat = lookupSatByName (grp_names[i]);
        if (!sat) {
            tft.setTextColor (RA8875_RED);
            tft.setCursor (x0+2, row_y + (SK_ROW_H > nh ? (SK_ROW_H-nh)/2 : 0));
            tft.print ("no TLE");
            delete sat;
            continue;
        }

        float hour_el[SK_HOURS];
        buildHourElevations (sat, &de_obs, grid_t0, hour_el);
        delete sat;

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        for (int h = 0; h < SK_HOURS; h++) {
            if (hour_el[h] <= 0)
                continue;
            tft.fillRect (x0 + h*col_w + 1, row_y+1, col_w-1, SK_ROW_H-2, SK_UP_C);

            int el_i = (int)(hour_el[h] + 0.5F);
            char ebuf[5];
            snprintf (ebuf, sizeof(ebuf), "%d", el_i);
            uint16_t ew = getTextWidth (ebuf) + 4;          // + small degree circle
            uint16_t ex = x0 + h*col_w + (col_w > ew ? (col_w-ew)/2 : 1);
            tft.setTextColor (RA8875_BLACK);
            tft.setCursor (ex, row_y + (SK_ROW_H-8)/2);
            printDeg (el_i, RA8875_BLACK);
        }
    }

    // horizontal separators
    for (int i = 0; i <= n_vis; i++) {
        uint16_t y = box.y + SK_GRIDTOP + i*SK_ROW_H;
        tft.drawLine (x0, y, x0 + SK_HOURS*col_w, y, SK_GRID_C);
    }
}


/* *********************************************************************************************
 * public entry point
 */

/* show the group schedule, taking over map_b, until the user leaves.
 * N.B. modeled directly on sattool.cpp's drawSatTool().
 */
void drawSatGroupSchedule (void)
{
    loadSatGroup();
    day_offset = 0;

    // layout: header band, grid band, all within map_b
    SBox hdr_b = {map_b.x, map_b.y, map_b.w, SK_TB};
    SBox grid_b = {map_b.x, (uint16_t)(map_b.y+SK_TB), map_b.w, (uint16_t)(map_b.h-SK_TB-SK_BB)};

    // right-aligned button cluster, sized for FAST_FONT (~6px/char), not SMALL_FONT (16pt --
    // far too big for boxes this size, which was the earlier overflow bug). laid out leftward
    // from the right margin so nothing can ever collide with the title text on the left.
    const uint16_t bt_y = map_b.y + 6, bt_h = 22, bt_gap = 8, right_margin = 15;
    uint16_t grp_w = 85, tz_w = 55, arrow_w = 26, resume_w = 60, updn_w = 30;
    uint16_t x = map_b.x + map_b.w - right_margin;

    x -= grp_w;    SBox grp_b    = {x, bt_y, grp_w, bt_h};
    x -= bt_gap + tz_w;    SBox tz_b     = {x, bt_y, tz_w, bt_h};
    x -= bt_gap + arrow_w; SBox next_b   = {x, bt_y, arrow_w, bt_h};
    x -= bt_gap + arrow_w; SBox prev_b   = {x, bt_y, arrow_w, bt_h};
    x -= bt_gap + updn_w;  SBox dn_b     = {x, bt_y, updn_w, bt_h};
    x -= bt_gap + updn_w;  SBox up_b     = {x, bt_y, updn_w, bt_h};
    x -= bt_gap + resume_w; SBox resume_b = {x, bt_y, resume_w, bt_h};

    // row scrolling for groups larger than fit on screen at once
    int max_vis_rows = (grid_b.h - 16) / SK_ROW_H;
    if (max_vis_rows < 1)
        max_vis_rows = 1;
    sk_ss.init (max_vis_rows, max_vis_rows-1, n_grp, ScrollState::DIR_BOTUP);

    fillSBox (map_b, RA8875_BLACK);

    time_t day_t0 = dayStart (nowWO());
    drawSKHeader (hdr_b, day_t0, resume_b, prev_b, next_b, tz_b, grp_b, up_b, dn_b);
    drawSKGrid (grid_b, gridT0 (day_t0));

    tft.drawPR();

    // popup history for erasing
    bool popup_is_up = false;
    bool popup_was_up = false;
    SBox popup_b = {0,0,0,0};

    UserInput ui = {
        map_b,
        UI_UFuncNone,
        UF_UNUSED,
        SK_TO,
        UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };

    while (waitForUser (ui)) {

        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC
                    || inBox (ui.tap, resume_b)
                    || (ui.kb_char == CHAR_NONE && !inBox (ui.tap, map_b)))
            break;

        bool changed = false;

        // first erase any previous popup
        if (popup_is_up) {
            drawSKGrid (grid_b, gridT0 (day_t0));
            popup_is_up = false;
            popup_was_up = true;
        }

        if (inBox (ui.tap, prev_b)) {
            day_offset--;
            changed = true;
        } else if (inBox (ui.tap, next_b)) {
            day_offset++;
            changed = true;
        } else if (inBox (ui.tap, tz_b)) {
            show_utc = !show_utc;
            changed = true;
        } else if (inBox (ui.tap, grp_b)) {
            querySatGroup (map_b);
            sk_ss.init (sk_ss.max_vis, sk_ss.max_vis-1, n_grp, ScrollState::DIR_BOTUP);
            changed = true;
        } else if (inBox (ui.tap, up_b)) {
            if (sk_ss.okToScrollUp()) {
                sk_ss.scrollUp ();
                drawSKGrid (grid_b, gridT0 (day_t0));
                drawSKHeader (hdr_b, day_t0, resume_b, prev_b, next_b, tz_b, grp_b, up_b, dn_b);
                tft.drawPR();
            }
        } else if (inBox (ui.tap, dn_b)) {
            if (sk_ss.okToScrollDown()) {
                sk_ss.scrollDown ();
                drawSKGrid (grid_b, gridT0 (day_t0));
                drawSKHeader (hdr_b, day_t0, resume_b, prev_b, next_b, tz_b, grp_b, up_b, dn_b);
                tft.drawPR();
            }
        } else if (n_grp > 0 && ui.tt != TT_NONE) {

            // tap within a grid cell? show elevation/azimuth popup for that satellite/time.
            // time is interpolated from where within the hour column was tapped, not just
            // the hour boundary -- matches sattool.cpp's ST_X2T convention.
            uint16_t col_w, x0;
            gridGeom (grid_b, col_w, x0);
            int disp_row = (ui.tap.y - grid_b.y - SK_GRIDTOP) / SK_ROW_H;
            int col = (ui.tap.x - x0) / col_w;
            int row;                                        // actual grp_names[] index, once known

            if (ui.tap.x >= x0 && disp_row >= 0 && sk_ss.findDataIndex (disp_row, row)
                        && col >= 0 && col < SK_HOURS
                        && (!popup_was_up || !inBox (ui.tap, popup_b))) {

                float frac = (float)(ui.tap.x - (x0 + col*col_w)) / col_w;
                time_t t = gridT0 (day_t0) + col*3600 + (time_t)(frac*3600);

                popup_b.w = 155;
                popup_b.h = 56;
                popup_b.x = ui.tap.x;
                popup_b.y = ui.tap.y;
                if (popup_b.x + popup_b.w > grid_b.x + grid_b.w)
                    popup_b.x = grid_b.x + grid_b.w - popup_b.w;
                if (popup_b.y + popup_b.h > grid_b.y + grid_b.h)
                    popup_b.y = grid_b.y + grid_b.h - popup_b.h;

                drawSKPopup (row, gridT0 (day_t0), t, popup_b);
                popup_was_up = popup_is_up;
                popup_is_up = true;
            }
        }

        if (changed) {
            day_t0 = dayStart (nowWO());
            drawSKHeader (hdr_b, day_t0, resume_b, prev_b, next_b, tz_b, grp_b, up_b, dn_b);
            drawSKGrid (grid_b, gridT0 (day_t0));
            tft.drawPR();
        } else if (popup_was_up && !popup_is_up) {
            // erased a popup but didn't open a new one or change anything else -- just show
            // the now-clean grid
            tft.drawPR();
        }
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    drawStringInBox ("Resume", resume_b, true, RA8875_GREEN);
    tft.drawPR();
}

/* show the satellite co-visibility matrix (DE, Sun, and the satellite group), live-refreshing
 * every few seconds, until the user leaves. same full-map-takeover / modal-loop convention as
 * drawSatGroupSchedule() and sattool.cpp's drawSatTool().
 */
void drawSatCoVis (void)
{
    loadSatGroup();

    fillSBox (map_b, RA8875_BLACK);

    SBox hdr_b = {map_b.x, map_b.y, map_b.w, SK_TB};
    SBox grid_b = {map_b.x, (uint16_t)(map_b.y+SK_TB), map_b.w, (uint16_t)(map_b.h-SK_TB)};

    SBox resume_b = {(uint16_t)(map_b.x+map_b.w-15-60), (uint16_t)(map_b.y+6), 60, 22};

    #define CV_REFRESH_MS   5000                            // live refresh interval

    // build the entity list once -- labels and which satellites are actually present (some may
    // be skipped for a missing TLE) must stay fixed across refreshes so tap hit-testing always
    // matches what's actually drawn, even though each entity's position is refreshed every redraw
    CVEntity ents[MAX_GRP_SATS+2];
    int n_ents = buildCVEntities (ents);

    auto drawAll = [&](time_t t) {
        fillSBox (hdr_b, RA8875_BLACK);

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (SK_HDR_C);
        tft.setCursor (hdr_b.x+5, hdr_b.y+30);
        tft.print ("Satellite Co-Visibility");

        char buf[40];
        struct tm tm_v;
        time_t tm_t = t;
        gmtime_r (&tm_t, &tm_v);
        strftime (buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_v);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (hdr_b.x+5, hdr_b.y+44);
        tft.print (buf);

        tft.print ("  ");
        tft.fillRect (tft.getCursorX(), hdr_b.y+38, 9, 9, SK_UP_C);
        tft.setCursor (tft.getCursorX()+12, hdr_b.y+44);
        tft.print ("footprint+freq");
        tft.print ("  ");
        tft.fillRect (tft.getCursorX(), hdr_b.y+38, 9, 9, SK_PARTIAL_C);
        tft.setCursor (tft.getCursorX()+12, hdr_b.y+44);
        tft.print ("footprint only");
        tft.print ("  (tap header=name, tap cell=times)");

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        drawStringInBox ("Resume", resume_b, false, RA8875_GREEN);

        drawCVMatrix (grid_b, ents, n_ents, t);

        tft.drawPR();
    };

    drawAll (nowWO());

    UserInput ui = {
        map_b,
        UI_UFuncNone,
        UF_UNUSED,
        CV_REFRESH_MS,
        UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };

    for (;;) {

        bool got_event = waitForUser (ui);

        if (!got_event) {
            // timeout -- just refresh with the current time and keep going
            drawAll (nowWO());
            continue;
        }

        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC
                    || inBox (ui.tap, resume_b)
                    || (ui.kb_char == CHAR_NONE && ui.tt != TT_NONE && !inBox (ui.tap, map_b)))
            break;

        // tap on a column header? show its full name as a tooltip, same convention used
        // elsewhere in HamClock, rather than immediately refreshing the whole screen
        if (n_ents > 0 && ui.tt != TT_NONE) {
            uint16_t lb, tb, cell, x0, y0;
            cvGeom (grid_b, n_ents, lb, tb, cell, x0, y0);
            if (ui.tap.x >= x0 && ui.tap.y >= grid_b.y && ui.tap.y < y0) {
                int col = (ui.tap.x - x0) / cell;
                if (col >= 0 && col < n_ents) {
                    tooltip (ui.tap, ents[col].label);
                    drawAll (nowWO());                      // tooltip erased itself; redraw fresh
                    continue;
                }
            }

            // tap on a matrix cell? show today's overlap time windows for that pair (only
            // meaningful for two satellites -- DE/Sun cells don't have "windows" in this sense)
            if (ui.tap.x >= x0 && ui.tap.y >= y0) {
                int row = (ui.tap.y - y0) / cell;
                int col = (ui.tap.x - x0) / cell;
                if (row >= 0 && row < n_ents && col >= 0 && col < n_ents && row != col
                            && ents[row].type == CV_SAT && ents[col].type == CV_SAT) {
                    SBox popup_b = {ui.tap.x, ui.tap.y, 160, 20+CV_MAX_WINDOWS*10};
                    if (popup_b.x + popup_b.w > grid_b.x + grid_b.w)
                        popup_b.x = grid_b.x + grid_b.w - popup_b.w;
                    if (popup_b.y + popup_b.h > grid_b.y + grid_b.h)
                        popup_b.y = grid_b.y + grid_b.h - popup_b.h;
                    drawCVPopup (ents[row], ents[col], nowWO(), popup_b);
                    drainTouch ();                          // avoid self-dismissing on a leftover
                                                              // event from the tap that opened this
                    UserInput dismiss_ui = {
                        map_b, UI_UFuncNone, UF_UNUSED, UI_NOTIMEOUT, UF_CLOCKSOK,
                        {0, 0}, TT_NONE, '\0', false, false
                    };
                    waitForUser (dismiss_ui);                // block until any tap/key dismisses it
                    drawAll (nowWO());
                    continue;
                }
            }
        }

        // any other tap: just refresh with the current time
        drawAll (nowWO());
    }

    freeCVEntities (ents, n_ents);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    drawStringInBox ("Resume", resume_b, true, RA8875_GREEN);
    tft.drawPR();
}
