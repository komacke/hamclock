/* handle Active Nets retrieval and display.
 *
 * Polls the local HamClock backend (OHB) once a minute for a CSV snapshot of
 * currently active NetLogger nets and shows them in a scrolling pane.
 *
 * The CSV is produced by a backend cron job and served at activenets_page.
 * Expected columns (a leading '#' comment line and a header line are skipped):
 *
 *     NetName,Frequency,Band,Mode,NetControl,Checkins,Logger,Started
 *
 * Each net is drawn as two lines: the net name in a large font with a smaller
 * supporting line beneath it (frequency, band, mode, net control, check-ins).
 * The Logger column is intentionally not displayed.
 */

#include "HamClock.h"


#define AN_COLOR        RGB565(80,200,120)      // pane accent (X11 ~ "emerald")
#define AN_NAME_COLOR   RA8875_WHITE            // net name color
#define AN_INFO_COLOR   BRGRAY                  // supporting info color

#define AN_START_DY     LISTING_Y0              // dy of first net's name line
#define AN_GAP_NI       2                       // gap: name bbox bottom -> info top
#define AN_GAP_BLK      4                       // gap: info bottom -> next net's name

// URL on the backend (OHB) and local cache file name.
// N.B. httpHCGET() automatically prepends "/ham/HamClock", so this path is
// relative to that root -- it must NOT repeat the prefix. The file is served at
// <backend>/ham/HamClock/activenets/activenets.txt
static const char activenets_page[] = "/activenets/activenets.txt";
static const char activenets_fn[] = "activenets.txt";
#define ACTIVENETS_MAXAGE   30                  // re-download when cache older than this, secs
#define ACTIVENETS_MINSIZ   1                   // accept even an empty (header-only) file

// info about each active net
typedef struct {
    char *name;                                 // malloced net name (large line, may be scrubbed to fit)
    char *info;                                 // malloced supporting info (small line)
    char *full_name;                            // malloced ORIGINAL net name, unscrubbed -- for web search URL
} ActiveNetEntry;

static ActiveNetEntry *anets;                   // malloced list, count in an_ss.n_data
static char subtitle[40];                       // count / freshness line under the title
static ScrollState an_ss;                       // scrolling context

/* free the anets[] list and reset count.
 */
static void freeActiveNets (void)
{
    for (int i = 0; i < an_ss.n_data; i++) {
        free (anets[i].name);
        free (anets[i].info);
        free (anets[i].full_name);
    }
    free (anets);
    anets = NULL;
    an_ss.n_data = 0;
}

/* measure the currently selected font's ascent (pixels above baseline) and full
 * glyph height, in logical app coordinates (independent of build resolution).
 */
static void fontVMetrics (int &ascent, int &height)
{
    const GFXfont *f = tft.getFont();
    int16_t miny = 0, maxy = 0;
    for (uint16_t c = f->first; c <= f->last; c++) {
        const GFXglyph *g = &f->glyph[c - f->first];
        if (g->yOffset < miny)
            miny = g->yOffset;
        if ((int16_t)(g->yOffset + g->height) > maxy)
            maxy = g->yOffset + g->height;
    }
    int sc = tft.SCALESZ > 0 ? tft.SCALESZ : 1;
    ascent = (-miny) / sc;
    height = (maxy - miny) / sc;
}

/* compute the pane's row geometry from the live font metrics.
 *   block_dy      total vertical pitch of one net entry
 *   name_base_off baseline of the (large) name line, measured down from block top
 *   info_base_off baseline of the (small) info line, measured down from block top
 *   max_vis       number of whole net entries that fit below the title
 * everything is in logical coordinates so it is correct at every build resolution.
 */
static void anGeometry (const SBox &box, int &block_dy, int &name_base_off,
    int &info_base_off, int &max_vis)
{
    int n_asc, n_h, i_asc, i_h;

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    fontVMetrics (n_asc, n_h);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    fontVMetrics (i_asc, i_h);

    name_base_off = n_asc;                              // name baseline within block
    info_base_off = n_h + AN_GAP_NI + i_asc;            // info baseline within block
    block_dy      = n_h + AN_GAP_NI + i_h + AN_GAP_BLK; // total per-net pitch

    max_vis = (box.h - AN_START_DY) / block_dy;
    if (max_vis < 1)
        max_vis = 1;
}

/* draw anets[] in the given pane box.
 */
static void drawActiveNetsPane (const SBox &box)
{
    // erase
    prepPlotBox (box);

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (AN_COLOR);
    static const char *title = "Nets";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // subtitle (count / freshness)
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (AN_COLOR);
    uint16_t sw = getTextWidth (subtitle);
    tft.setCursor (box.x + (box.w-sw)/2, box.y + SUBTITLE_Y0);
    tft.print (subtitle);

    // compute row geometry from live font metrics
    int block_dy, name_base_off, info_base_off, max_vis;
    anGeometry (box, block_dy, name_base_off, info_base_off, max_vis);

    // nothing more if no nets
    if (an_ss.n_data == 0) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (AN_INFO_COLOR);
        static const char *none = "No active nets";
        uint16_t nw = getTextWidth (none);
        tft.setCursor (box.x + (box.w-nw)/2, box.y + AN_START_DY + name_base_off);
        tft.print (none);
        return;
    }

    // show each visible net: name in large font, info beneath in small font
    uint16_t y0 = box.y + AN_START_DY;
    int min_i, max_i;
    if (an_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            ActiveNetEntry &an = anets[i];
            int r = an_ss.getDisplayRow (i);
            uint16_t bt = y0 + r*block_dy;              // top of this net's block

            // net name -- the largest text
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (AN_NAME_COLOR);
            uint16_t w = getTextWidth (an.name);
            tft.setCursor (box.x + (box.w-w)/2, bt + name_base_off);
            tft.print (an.name);

            // supporting info beneath
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            tft.setTextColor (AN_INFO_COLOR);
            w = getTextWidth (an.info);
            tft.setCursor (box.x + (box.w-w)/2, bt + info_base_off);
            tft.print (an.info);
        }
    }

    // draw scroll controls, if needed
    an_ss.drawScrollDownControl (box, AN_COLOR, AN_COLOR);
    an_ss.drawScrollUpControl (box, AN_COLOR, AN_COLOR);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollActiveNetsUp (const SBox &box)
{
    if (an_ss.okToScrollUp()) {
        an_ss.scrollUp();
        drawActiveNetsPane (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollActiveNetsDown (const SBox &box)
{
    if (an_ss.okToScrollDown()) {
        an_ss.scrollDown();
        drawActiveNetsPane (box);
    }
}

/* split a CSV line in place into up to maxf field pointers; return field count.
 * handles double-quoted fields containing commas and doubled "" quote escapes.
 */
static int splitCSV (char *line, char *fields[], int maxf)
{
    int n = 0;
    char *p = line;

    while (n < maxf) {
        char *out = p;                          // unescape write position
        char *start = out;
        bool inq = false;

        for (;;) {
            char c = *p;
            if (inq) {
                if (c == '"') {
                    if (p[1] == '"') { *out++ = '"'; p += 2; continue; }  // escaped quote
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
        p++;                                    // step over the comma
    }

    return (n);
}

/* shorten line IN PLACE to fit within box width, assuming the desired font is selected.
 */
static void scrubToFit (char *line, const SBox &box)
{
    while (getTextWidth (line) >= box.w - 2) {
        char *right_space = strrchr (line, ' ');
        if (right_space)
            *right_space = '\0';                // chop at right-most space
        else if (strlen(line) > 0)
            line[strlen(line)-1] = '\0';        // no space: chop last char
        else
            break;
    }
}

/* build the supporting info line for one net from its CSV fields.
 * format: "<freq> <band> <mode> - <n> ckins", omitting empty pieces.
 * uses only ASCII so it renders correctly in the small CP437-based font.
 */
static void buildInfoLine (char *out, size_t out_l, const char *freq, const char *band,
    const char *mode, const char *ncs, const char *chk)
{
    out[0] = '\0';
    size_t used = 0;

    // frequency / band / mode are space-separated as a first group
    #define AN_APP_SP(str)                                                      \
        do {                                                                    \
            if ((str) && (str)[0] && used < out_l)                             \
                used += snprintf (out+used, out_l-used, "%s%s",                 \
                                  used ? " " : "", (str));                      \
        } while (0)

    AN_APP_SP (freq);
    AN_APP_SP (band);
    AN_APP_SP (mode);

    // net control, set off with a dash
    //if (ncs && ncs[0] && used < out_l)
    //    used += snprintf (out+used, out_l-used, "%sNCS %s", used ? " - " : "", ncs);

    // check-in count
    if (chk && chk[0] && used < out_l)
        used += snprintf (out+used, out_l-used, "%s%s ckins", used ? " - " : "", chk);

    #undef AN_APP_SP
}

/* (re)load anets[] from the cached backend file and draw into box.
 * return whether io ok.
 */
static bool retrieveActiveNets (const SBox &box)
{
    bool ok = false;

    FILE *fp = openCachedFile (activenets_fn, activenets_page, ACTIVENETS_MAXAGE, ACTIVENETS_MINSIZ);
    if (!fp) {
        Serial.printf ("ANET: %s not available\n", activenets_fn);
        return (false);
    }

    // look alive
    updateClocks (false);

    // start fresh
    freeActiveNets();

    // one net per displayed block; size max_vis from live font metrics
    int block_dy, name_base_off, info_base_off, max_vis;
    anGeometry (box, block_dy, name_base_off, info_base_off, max_vis);
    an_ss.init (max_vis, 0, 0, an_ss.DIR_TOPDOWN);

    // set the font used for width scrubbing of the info line
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // getting this far counts as a successful transaction (file may legitimately be empty)
    ok = true;

    char line[300];
    while (fgets (line, sizeof(line), fp)) {

        chompString (line);

        // skip blank lines and the '#' comment line
        if (line[0] == '\0' || line[0] == '#')
            continue;

        // split into fields
        char *f[8];
        int nf = splitCSV (line, f, NARRAY(f));
        if (nf < 5)
            continue;                           // not a usable data row

        // skip the column header row
        if (strcmp (f[0], "NetName") == 0)
            continue;

        // map columns: 0 name, 1 freq, 2 band, 3 mode, 4 ncs, 5 checkins (6 logger, 7 started)
        const char *name = f[0];
        const char *freq = nf > 1 ? f[1] : "";
        const char *band = nf > 2 ? f[2] : "";
        const char *mode = nf > 3 ? f[3] : "";
        const char *ncs  = nf > 4 ? f[4] : "";
        const char *chk  = nf > 5 ? f[5] : "";

        if (name[0] == '\0')
            continue;                           // need at least a name

        if (debugLevel (DEBUG_CONTESTS, 1))
            Serial.printf ("ANET %d: %s | %s %s %s %s %s\n", an_ss.n_data,
                                name, freq, band, mode, ncs, chk);

        // build supporting info line
        char info[200];
        buildInfoLine (info, sizeof(info), freq, band, mode, ncs, chk);

        // scrub each to fit
        char nbuf[200];
        quietStrncpy (nbuf, name, sizeof(nbuf));
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        scrubToFit (nbuf, box);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        scrubToFit (info, box);

        // append to anets[]
        anets = (ActiveNetEntry *) realloc (anets, (an_ss.n_data+1) * sizeof(ActiveNetEntry));
        if (!anets)
            fatalError ("No memory for %d active nets", an_ss.n_data+1);
        ActiveNetEntry &an = anets[an_ss.n_data++];
        an.name = strdup (nbuf);
        an.info = strdup (info);
        an.full_name = strdup (name);            // keep unscrubbed original for the URL
    }

    fclose (fp);

    // build subtitle
    snprintf (subtitle, sizeof(subtitle), "%d active net%s", an_ss.n_data,
                                                an_ss.n_data == 1 ? "" : "s");

    an_ss.scrollToNewest();

    Serial.printf ("ANET: found %d in %s\n", an_ss.n_data, activenets_fn);
    return (ok);
}

/* poll the backend for active nets and show in the given pane box.
 * called about once a minute via ACTIVENETS_INTERVAL; openCachedFile() rate-limits
 * the actual network fetch to no more than once per ACTIVENETS_MAXAGE seconds.
 */
bool updateActiveNets (const SBox &box, bool fresh)
{
    (void) fresh;       // we rebuild from cache every call; nothing extra needed when freshly exposed

    bool ok = retrieveActiveNets (box);
    if (ok)
        drawActiveNetsPane (box);
    else
        plotMessage (box, AN_COLOR, "Active Nets error");

    return (ok);
}

/* build a Google search URL for the given net name: spaces -> '+', everything
 * else URL-unsafe percent-encoded, "amateur radio" appended -- same scheme
 * NetLogger's own website uses for its (www) links.
 */
static void buildNetSearchURL (const char *netname, char *out, size_t out_l)
{
    size_t used = snprintf (out, out_l, "%s", "https://www.google.com/search?q=");

    for (const char *p = netname; *p && used + 4 < out_l; p++) {
        char c = *p;
        if (c == ' ')
            out[used++] = '+';
        else if (isalnum((unsigned char)c) || c=='-' || c=='_' || c=='.' || c=='~')
            out[used++] = c;
        else
            used += snprintf (out+used, out_l-used, "%%%02X", (unsigned char)c);
    }

    used += snprintf (out+used, out_l-used, "+amateur+radio");
    out[out_l-1] = '\0';
}

/* ask "Open webpage?" return whether user confirmed.
 * mirrors the static RUSure() pattern in ESPHamClock.cpp, adapted locally
 * since that one isn't exposed outside its file. kept deliberately short and
 * fixed-size (no net name) so it never depends on, or is sized by, pane width.
 */
static bool confirmOpenNetWeb (const SBox &box)
{
    static const char *q = "Open webpage?";
    #define AN_CONFIRM_INDENT  3                 // left-justified, matches other small menu indents

    MenuItem mitems[] = {
        {MENU_BLANK, false, 0, 0, NULL, 0},
        {MENU_LABEL, false, 0, AN_CONFIRM_INDENT, q, 0},
        {MENU_BLANK, false, 0, 0, NULL, 0},
    };
    const int n_m = NARRAY(mitems);

    SBox menu_b = {box.x, box.y, 0, 0};         // shrink-wrap, anchored at pane origin
    SBox ok_b;

    MenuInfo menu = {menu_b, ok_b, UF_NOCLOCKS, M_CANCELOK, 1, n_m, mitems};
    return (runMenu (menu));
}

/* return true if user is interacting with the active nets pane (scroll), false to let
 * the caller bring up the pane-choice menu.
 * N.B. we assume s is within box.
 */
bool checkActiveNetsTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + PANETITLE_H) {
        if (an_ss.checkScrollUpTouch (s, box)) {
            scrollActiveNetsUp (box);
            return (true);
        }
        if (an_ss.checkScrollDownTouch (s, box)) {
            scrollActiveNetsDown (box);
            return (true);
        }
        // not ours -- caller will offer the pane-choice menu
        return (false);
    }

    // below the title -- check whether a net row was tapped
    int block_dy, name_base_off, info_base_off, max_vis;
    anGeometry (box, block_dy, name_base_off, info_base_off, max_vis);
    (void) name_base_off;
    (void) info_base_off;
    (void) max_vis;

    if (s.y < box.y + AN_START_DY)
        return (false);                          // subtitle strip, not a row

    int vis_row = (s.y - (box.y + AN_START_DY)) / block_dy;
    int net_i;
    if (an_ss.findDataIndex (vis_row, net_i) && net_i < an_ss.n_data) {
        if (confirmOpenNetWeb (box)) {
            char url[300];
            buildNetSearchURL (anets[net_i].full_name, url, sizeof(url));
            Serial.printf ("ANET: opening %s\n", url);
            openURL (url);
        }
        return (true);
    }

    return (false);
}
