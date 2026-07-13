/* handle DXPeditions retrieval and display.
 */

#include "HamClock.h"


#define DXPEDS_COLOR    RGB565(255,200,130)     // heading text color
#define NOW_COLOR       RGB565(40,140,40)       // background when dxpedition is happening now
#define SPOT_COLOR      RA8875_RED              // color indicating expedition has been spotted
#define HIDE_COLOR      RGB565(150,100,20)       // color indicating expedition is marked as hidden
#define CREDITS_Y0      SUBTITLE_Y0             // dy of credits row
#define DXPEDS_DY       12                      // dy of each successive row -- a bit tighter than LISTING_DY
#define START_DY        LISTING_Y0              // dy of first dxpedition row
#define TITLE_DY        PANETITLE_H             // dy to baseline of title text
#define MARKER_R0       4                       // map marker inner radius
#define MARKER_R1       5                       // map marker outer radius

// dxcluster ok box
#define DXCOKBOX_DX     6                       // cluster status box left offset
#define DXCOKBOX_DY     6                       // " top offset
#define DXCOKBOX_W      20                      // " width
#define DXCOKBOX_H      11                      // " height
#define DXCCHECK_DT     15000                   // period to check dxcluster if failing, millis
static SBox clok_b;


// URL and its local cache file name
static const char dxpeds_page[] = "/dxpeds/dxpeditions.txt";
static const char dxpeds_fn[] = "dxpeditions.txt";

#define DXPEDS_MAXAGE   (3600*24)               // update when cache older than this, secs
#define DXPEDS_MINSIZ   100                     // min acceptable file size is just attribution line

// attribution credits
typedef struct {
    char *name;                                 // malloced name to display
    char *url;                                  // malloc URL if click
} DXPCredit;


// ADIF dxcc/prefix worked list
typedef struct {
    int dxcc;
    char prefix[MAX_PREF_LEN];
    DXPedsWorked worked;
} ADIFWList;


// internal state
static DXPedEntry *dxpeds;                      // malloced list of DXPedEntry, count in dxp_ss.n_data
static bool show_date;                          // whether to show 2nd line with date
static bool show_current;                       // whether to show only the active expeditions
static bool show_hidden;                        // whether to show peds marked as hidden
static bool watch_cluster;                      // whether to watch cluster for spots
static ScrollState dxp_ss;                      // scrolling context, max_vis/2 if showing date
static DXPCredit *credits;                      // malloced list of each credit
static int n_credits;                           // n credits
static ADIFWList *adif_worked;                  // malloced list of ADIF worked band+mode
static int n_adif_worked;                       // n adif_worked[]


// NV_DXPEDS bits
enum {
    NVBIT_SHOWDATE = (1<<0),                    // show_date
    NVBIT_CURRENT  = (1<<1),                    // show_current
    NVBIT_WATCHDXC = (1<<2),                    // watch_cluster
    NVBIT_SHOWHIDE = (1<<3),                    // show_hidden
};


/* save NV_DXPEDS
 */
static void saveDXPedNV (void)
{
    uint8_t dxpeds_mask = 0;

    dxpeds_mask |= show_date      ? NVBIT_SHOWDATE : 0;
    dxpeds_mask |= show_current   ? NVBIT_CURRENT  : 0;
    dxpeds_mask |= watch_cluster  ? NVBIT_WATCHDXC : 0;
    dxpeds_mask |= show_hidden    ? NVBIT_SHOWHIDE : 0;

    NVWriteUInt8 (NV_DXPEDS, dxpeds_mask);
}

/* load NV_DXPEDS
 */
static void loadDXPedNV (void)
{
    uint8_t dxpeds_mask = 0;

    if (!NVReadUInt8 (NV_DXPEDS, &dxpeds_mask)) {
        dxpeds_mask = 0;
        NVWriteUInt8 (NV_DXPEDS, dxpeds_mask);
    }

    show_date     = (dxpeds_mask & NVBIT_SHOWDATE) != 0;
    show_current  = (dxpeds_mask & NVBIT_CURRENT)  != 0;
    watch_cluster = (dxpeds_mask & NVBIT_WATCHDXC) != 0;
    show_hidden   = (dxpeds_mask & NVBIT_SHOWHIDE) != 0;
}

/* draw marker at ll
 */
static void drawDXPedsMarker (const LatLong &ll, uint16_t color)
{
    SCoord s;

    // first check over map
    ll2s (ll, s, MARKER_R1);
    if (!overMap(s))
        return;

    // now full res if different
    if (tft.SCALESZ > 1)
        ll2sRaw (ll, s, MARKER_R1);

    const int r0_raw = MARKER_R0*tft.SCALESZ;
    const int r1_raw = MARKER_R1*tft.SCALESZ;
    tft.fillRectRaw (s.x - r1_raw, s.y - r1_raw, 2*r1_raw+1, 2*r1_raw+1, RA8875_WHITE);
    tft.fillRectRaw (s.x - r0_raw, s.y - r0_raw, 2*r0_raw+1, 2*r0_raw+1, color);
}



/* return color to render the given expedition, depending on several factors.
 */
static uint16_t renderDXPedColor (const DXPedEntry &de)
{
    uint16_t color = RA8875_BLACK;

    if (findDXCCall(de.call) != NULL)
        color = SPOT_COLOR;
    else if (isDXPedsHidden(&de))
        color = HIDE_COLOR;
    else {
        time_t now = myNow();
        bool active_now = now > de.start_t && now < de.end_t;
        if (active_now)
            color = NOW_COLOR;
    }

    return (color);
}

/* draw the cluster ok status box as appropriate
 */
static void drawDXCStatusBox (void)
{
    uint16_t color = RA8875_BLACK;
    bool want_cl = watch_cluster && useDXCluster();
    bool cl_ok = isDXClusterConnected();
    if (want_cl)
        color = cl_ok ? RA8875_GREEN : RA8875_RED;

    drawSBox (clok_b, color);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (clok_b.x+1, clok_b.y+2);
    tft.setTextColor (color);
    tft.print ("DXC");
}

/* draw dxpeds[] in the given pane box
 */
static void drawDXPedsPane (const SBox &box)
{
    // skip if no credit yet
    if (n_credits == 0)
        return;

    // erase
    prepPlotBox (box);

    // cluster status
    clok_b.x = box.x + DXCOKBOX_DX;
    clok_b.y = box.y + DXCOKBOX_DY;
    clok_b.w = DXCOKBOX_W;
    clok_b.h = DXCOKBOX_H;
    drawDXCStatusBox();

    // title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(DXPEDS_COLOR);
    static const char *title = "DXPeds";
    uint16_t tw = getTextWidth(title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // credit
    char credit[100];
    int c_l = 0;
    for (int i = 0; i < n_credits; i++)
        c_l += snprintf (credit+c_l, sizeof(credit)-c_l, "%s ", credits[i].name);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(DXPEDS_COLOR);
    uint16_t c_w = getTextWidth (credit);
    tft.setCursor (box.x + (box.w-c_w)/2, box.y + CREDITS_Y0);
    tft.print (credit);

    // show each dxpedition starting with top_vis, up to max visible.
    // N.B. scroller doesn't know show_date entries occupy two rows.
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(RA8875_WHITE);
    uint16_t y0 = box.y + START_DY;
    int min_i, max_i;
    if (dxp_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            DXPedEntry &de = dxpeds[i];
            uint16_t bg_color = renderDXPedColor (de);
            int r = dxp_ss.getDisplayRow(i);
            if (show_date) {
                uint16_t y = y0 + r*2*DXPEDS_DY;
                tft.fillRect (box.x+1, y-4, box.w-2, 2*DXPEDS_DY+1, bg_color);
                tft.drawLine (box.x+1, y+2*DXPEDS_DY-3, box.x+box.w-2, y+2*DXPEDS_DY-3, 2, DXPEDS_COLOR);
                uint16_t w = getTextWidth (de.title);
                tft.setCursor (box.x + (box.w-w)/2, y);
                tft.print (de.title);
                y += DXPEDS_DY;
                w = getTextWidth (de.date_str);
                tft.setCursor (box.x + (box.w-w)/2, y);
                tft.print (de.date_str);
            } else {
                uint16_t y = y0 + r*DXPEDS_DY;
                tft.fillRect (box.x+1, y-2, box.w-2, DXPEDS_DY, bg_color);
                uint16_t w = getTextWidth (de.title);
                tft.setCursor (box.x + (box.w-w)/2, y);
                tft.print (de.title);
            }
        }
    }

    // draw scroll controls, if needed
    dxp_ss.drawScrollDownControl (box, DXPEDS_COLOR, DXPEDS_COLOR);
    dxp_ss.drawScrollUpControl (box, DXPEDS_COLOR, DXPEDS_COLOR);
}

/* scroll up, if appropriate to do so now.
 */
static void scrollDXPedUp (const SBox &box)
{
    if (dxp_ss.okToScrollUp()) {
        dxp_ss.scrollUp();
        drawDXPedsPane (box);
    }
}

/* scroll down, if appropriate to do so now.
 */
static void scrollDXPedDown (const SBox &box)
{
    if (dxp_ss.okToScrollDown()) {
        dxp_ss.scrollDown();
        drawDXPedsPane (box);
    }
}

/* malloc and format de.date_str and title from the given info to fit within box.
 */
static void formatDXPed (DXPedEntry &de, const char *loc_line, const char *call_line, const SBox &box)
{
    char str[512];

    // find max box chars wide
    memset (str, 'x', sizeof(str));
    str[sizeof(str)-1] = '\0';
    (void) maxStringW (str, box.w-2);
    const size_t box_l = strlen(str);

    // break out time
    // N.B. gmtime() returns pointer to the same static array
    const struct tm tm1 = *gmtime (&de.start_t);
    const struct tm tm2 = *gmtime (&de.end_t);

    // get day and month names  -- N.B. dayShortStr() returns pointer to the same static array
    char wd1[10], wd2[10], mo1[10], mo2[10];
    strcpy (wd1, dayShortStr(tm1.tm_wday+1));
    strcpy (wd2, dayShortStr(tm2.tm_wday+1));
    strcpy (mo1, monthShortStr(tm1.tm_mon+1));
    strcpy (mo2, monthShortStr(tm2.tm_mon+1));

    // build title to spread edge-to-edge
    char str1[40], str2[40];
    const size_t s1_l = snprintf (str1, sizeof(str1), "%s %s %d", wd1, mo1, tm1.tm_mday);
    const size_t s2_l = snprintf (str2, sizeof(str2), "%s %s %d", wd2, mo2, tm2.tm_mday);
    const int gap = box_l - s1_l - s2_l;
    snprintf (str, sizeof(str), "%s%*s%s", str1, gap, "", str2);
    if (gap < 3)
        str[box_l - s2_l - 1] = '-';                    // just before call
    else
        str[s1_l + gap/2] = '-';                        // centered

    // save
    de.date_str = strdup (str);


    // now combine location and call

    // max location length is box width - call length - 1 for gap
    const size_t call_l = strlen (call_line);
    const size_t max_loc_l = box_l - call_l - 1;        // need at least 1 space between
    size_t loc_l = strlen (loc_line);                   // might get reduced to fit

    // build title string edge-to-edge
    if (loc_l > max_loc_l) {
        // loc is too long, try removing everything after " and " which seems to happen often
        const char *and_ptr = strstr (loc_line, " and ");
        if (and_ptr)
            loc_l = and_ptr - loc_line;
        if (loc_l > max_loc_l) {
            // still too long so remove words from the right until it fits
            for (const char *lp = loc_line+loc_l; lp > loc_line && loc_l > max_loc_l; --lp)
                if (*lp == ' ')
                    loc_l = lp - loc_line;
            if (loc_l > max_loc_l) {
                // STILL too long so no choice but to be ruthless
                loc_l = max_loc_l;
            }
        }
    }
    const int gap_l = max_loc_l - loc_l + 1;
    (void) snprintf (str, sizeof(str), "%.*s%*s%s", (int)loc_l, loc_line, gap_l, "", call_line);

    // save
    de.title = strdup (str);
}

/* show the main dxpedition menu in the given box.
 * return true if enough changed that a complete update is required, such as changing show_date,
 * else false if ok to just redraw pane without any changes.
 */
static bool runDXPedPaneMenu (const SBox &box)
{
    // handy mitems[] offset names, sans credit lines
    enum {
        DEM_SHOW_DATES,                                 // whether to show dates
        DEM_SHOW_CURRENT,                               // whether to show only current exp
        DEM_SHOW_HIDDEN,                                // whether to show dxpeds markde as hidden
        DEM_USE_DXC,                                    // whether to check DX cluster for new spot
        DEM_N
    };

    // whether caller must redo everything
    bool full_redo = false;

    // decide whether to even ask about for DX Cluster spots
    MenuFieldType dxc_mft = useDXCluster() ? MENU_TOGGLE : MENU_IGNORE;

    // get memory for menu items; must be dynamic to accommodate credits
    const int indent = 2;
    const int n_mi = DEM_N + n_credits;
    MenuItem *mitems = (MenuItem *) calloc (n_mi, sizeof(MenuItem));                    // N.B. free!
    if (!mitems)
        fatalError ("No memory for %d DXP menu items", n_mi);
    #define _MAX_CIL 50
    typedef char CredItem_t[_MAX_CIL];
    CredItem_t *citems = (CredItem_t *) calloc (n_credits, sizeof(CredItem_t));         // N.B. free!
    if (!citems)
        fatalError ("No memory for %d DXP credit items", n_credits);

    // init fixed menu items
    mitems[DEM_SHOW_DATES]   = {MENU_TOGGLE, show_date,             1, indent, "Show dates", 0};
    mitems[DEM_SHOW_CURRENT] = {MENU_TOGGLE, show_current,          2, indent, "Show only current", 0};
    mitems[DEM_SHOW_HIDDEN]  = {MENU_TOGGLE, show_hidden,           3, indent, "Show hidden in brown", 0};
    mitems[DEM_USE_DXC]      = {dxc_mft,     watch_cluster,         4, indent, "Show spotted in red", 0};

    // add variable number of credits
    for (int i = 0; i < n_credits; i++) {
        snprintf (citems[i], _MAX_CIL, "Open %s web page", credits[i].name);
        mitems[DEM_N+i] = {MENU_TOGGLE, false, DEM_N+1, indent, citems[i], 0};
    }

    // boxes -- very tight fit especially in pane 0
    uint16_t menu_x = BOX_IS_PANE_0(box) ? box.x + 3 : box.x + 10;
    SBox menu_b = {menu_x, (uint16_t)(box.y + CREDITS_Y0), 0, 0};
    SBox ok_b;

    // run
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n_mi, mitems};
    if (runMenu (menu)) {

        // update show_date?
        if (show_date != mitems[DEM_SHOW_DATES].set) {
            show_date = mitems[DEM_SHOW_DATES].set;
            Serial.printf ("DXP: show_date changed to %d\n", show_date);
            full_redo = true;
        }

        // update show_current?
        if (show_current != mitems[DEM_SHOW_CURRENT].set) {
            show_current = mitems[DEM_SHOW_CURRENT].set;
            Serial.printf ("DXP: show_current changed to %d\n", show_current);
            full_redo = true;
        }

        // update show_hidden?
        if (show_hidden != mitems[DEM_SHOW_HIDDEN].set) {
            show_hidden = mitems[DEM_SHOW_HIDDEN].set;
            Serial.printf ("DXP: show_hidden changed to %d\n", show_hidden);
            full_redo = true;
        }

        // udate watch_cluster?
        if (watch_cluster) {
            if (!mitems[DEM_USE_DXC].set) {
                // we no longer want to watch dx cluster, rely on checkDXCluster() to close
                watch_cluster = false;
                full_redo = true;
            }
        } else {
            if (mitems[DEM_USE_DXC].set) {
                // we are first to want dx cluster, rely on updateDXPeds() to open
                watch_cluster = true;
                full_redo = true;
            }
        }

        // open a web page?
        for (int i = 0; i < n_credits; i++) {
            if (mitems[DEM_N+i].set)
                openURL (credits[i].url);
        }

        // save any persistent state changes
        if (full_redo)
            saveDXPedNV();
    }

    // clean up
    free (mitems);
    free (citems);

    // return whether redo is required
    return (full_redo);
}

/* show menu for the given expedition clicked in the give box.
 * return true if enough changed that a complete update is required.
 * else false if ok to just redraw pane without any changes.
 */
static bool runOneDXPedMenu (const SCoord &s, const SBox &box, DXPedEntry *dep)
{
    // handy mitems[] offset names
    enum {
        DEX_NAME,                                       // expedition name
        DEX_HIDE,                                       // whether to hide this exped
        DEX_ALARM,                                      // whether to set alarm
        DEX_DX,                                         // whether to set DX to this location
        DEX_PAGE,                                       // whether to show this exp web page
        DEX_N
    };

    // whether caller must redo everything
    bool full_redo = false;

    // decide whether/how to show alarm control
    AlarmState a_s;
    time_t a_t;
    bool a_utc;
    getOneTimeAlarmState (a_s, a_t, a_utc);
    bool starts_in_future = dep->start_t > nowWO();
    bool alarm_is_set = a_s == ALMS_ARMED && a_t == dep->start_t && starts_in_future;
    MenuFieldType alarm_mft = starts_in_future ? MENU_TOGGLE : MENU_IGNORE;

    // decide hidden
    bool hidden = isDXPedsHidden (dep);

    // build title roughly centered
    char title[50];
    snprintf (title, sizeof(title), "%10s", dep->call);

    // build menu
    const int indent = 2;
    MenuItem mitems[DEX_N];
    mitems[DEX_NAME]  = {MENU_LABEL,   false,             0, indent, title, 0};
    mitems[DEX_HIDE]  = {MENU_TOGGLE,  hidden,            1, indent, "Hide", 0};
    mitems[DEX_ALARM] = {alarm_mft,    alarm_is_set,      2, indent, "Set alarm", 0};
    mitems[DEX_DX]    = {MENU_TOGGLE,  false,             3, indent, "Set DX", 0};
    mitems[DEX_PAGE]  = {MENU_TOGGLE,  false,             4, indent, "Show web page", 0};

    // boxes -- avoid spilling out the bottom
    const uint16_t menu_x = box.x + 20;
    const uint16_t menu_h = 80;
    const uint16_t menu_max_y = box.y + box.h - menu_h - 5;
    const uint16_t menu_y = s.y < menu_max_y ? s.y : menu_max_y;
    SBox menu_b = {menu_x, menu_y, 0, 0};
    SBox ok_b;

    // run
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, DEX_N, mitems};
    if (runMenu (menu)) {

        // alarm state change?
        if (mitems[DEX_ALARM].set != alarm_is_set)
            setOneTimeAlarmState (mitems[DEX_ALARM].set ? ALMS_ARMED : ALMS_OFF, true,
                                            dep->start_t, dep->loc);

        // set dx?
        if (mitems[DEX_DX].set) {
            Serial.printf ("DXP: newDX %s @ %g %g\n", dep->call, dep->ll.lat_d, dep->ll.lng_d);
            newDX (dep->ll, NULL, dep->call);
        }

        // open dxpedition web page?
        if (mitems[DEX_PAGE].set)
            openURL (dep->url);

        // change hidden?
        if (hidden != mitems[DEX_HIDE].set) {
            if (mitems[DEX_HIDE].set)
                addDXPedsHidden (dep);
            else
                rmDXPedsHidden (dep);
            Serial.printf ("DXP: %s hide changed to %d\n", dep->call, mitems[DEX_HIDE].set);
            full_redo = true;
        }
    }

    // return whether redo is required
    return (full_redo);
}

/* qsort-style comparison to sort by in decreasing start time.
 * this puts the first (smallest time) dxpedition to start at the end of the array, as expected by SrollState.
 */
static int qsDXPedStart (const void *v1, const void *v2)
{
    return (((DXPedEntry*)v2)->start_t - ((DXPedEntry*)v1)->start_t);
}


/* free all heap memory used by dxpeds
 */
static void freeDXPeds (void)
{
    if (dxp_ss.n_data > 0) {
        for (int i = 0; i < dxp_ss.n_data; i++) {
            DXPedEntry &de = dxpeds[i];
            free (de.date_str);
            free (de.title);
            free (de.url);
            free (de.call);
            free (de.loc);
            dxp_ss.n_data = 0;
        }
        free (dxpeds);
        dxpeds = NULL;
        dxp_ss.n_data = 0;
    }

    if (n_credits > 0) {
        for (int i = 0; i < n_credits; i++) {
            free (credits[i].name);
            free (credits[i].url);
        }
        free (credits);
        credits = NULL;
        n_credits = 0;
    }
}

/* first line is number of credits, followed by one line for name and for url.
 * return whether io ok.
 * N.B. we assume credits[] has already been free()d
 */
static bool retrieveDXPCredits (FILE *fp)
{
    char line[256];

    // read count
    if (!fgets (line, sizeof(line), fp)) {
        Serial.printf ("DXP: no credits count\n");
        return (false);
    }
    n_credits = atoi (line);
    if (n_credits > 0) {
        // malloc credits array
        credits = (DXPCredit *) malloc (n_credits * sizeof(DXPCredit));
        if (!credits)
            fatalError ("No memory for %d DXPeds credits", n_credits);
    } else
        Serial.printf ("DXP: no credits??\n");

    // read each pair
    for (int i = 0; i < n_credits; i++) {

        // read name
        if (!fgets (line, sizeof(line), fp)) {
            Serial.printf ("DXP: no credits[%d] name\n", i);
            return (false);
        }
        chompString (line);
        credits[i].name = strdup (line);

        // read URL
        if (!fgets (line, sizeof(line), fp)) {
            Serial.printf ("DXP: no credits[%d] url\n", i);
            return (false);
        }
        chompString (line);
        credits[i].url = strdup (line);

        if (debugLevel (DEBUG_DXPEDS, 1))
            Serial.printf ("DXP: credit[%d] %s %s\n", i, credits[i].name, credits[i].url);
    }

    // ok
    return (true);
}

/* collect DXPed info into the dxpeds[] array.
 * return whether io ok.
 */
static bool retrieveDXPeds (const SBox &box)
{
    WiFiClient ctst_client;
    bool ok = false;

    // stats
    int found_n_broken = 0;
    int found_n = 0;

    // fresh
    loadDXPedNV();
    freeDXPeds();

    // download and load dxpeds[]
    FILE *fp = openCachedFile (dxpeds_fn, dxpeds_page, DXPEDS_MAXAGE, DXPEDS_MINSIZ);

    if (fp) {

        // look alive
        updateClocks(false);

        // handy UTC
        time_t now = myNow();

        // init scroller and max list size. max_vis is half the number of rows if showing date too.
        dxp_ss.init ((box.h - START_DY)/DXPEDS_DY, 0, 0, dxp_ss.DIR_TOPDOWN);
        if (show_date)
            dxp_ss.max_vis /= 2;

        // first line is number of credits, followed by one line for name and for url
        if (!retrieveDXPCredits (fp))
            goto out;           // already logged why

        // consider transaction is ok if get at least the credit messages
        ok = true;

        // set font for formatDXPed()
        selectFontStyle (LIGHT_FONT, FAST_FONT);

        // line buffer
        char line[256];

        // read each line, add to dxpeds if ok
        while (fgets (line, sizeof(line), fp)) {

            // rm nl
            chompString (line);

            if (debugLevel (DEBUG_DXPEDS, 2))
                Serial.printf ("DXP: datum %d: %s\n", dxp_ss.n_data, line);

            // skip if want to hide
            if (!show_hidden && isDXPedsHidden (line)) {
                if (debugLevel (DEBUG_DXPEDS, 2))
                    Serial.printf ("DXP: hiding %s\n", line);
                continue;
            }


            // find each CSV field -- formatted as per fetchDXPeds.pl
            char *start_f = line;
            char *end_f = strchr (start_f, ',');
            char *loc_f = end_f ? (*end_f++ = '\0', strchr (end_f, ',')) : NULL;
            char *call_f = loc_f ? (*loc_f++ = '\0', strchr (loc_f, ',')) : NULL;
            char *url_f = call_f ? (*call_f++ = '\0', strchr (call_f, ',')) : NULL;
            if (url_f)
                *url_f++ = '\0';
            if (!end_f || !loc_f || !call_f || !url_f) {
                Serial.printf ("DXP: missing fields: %s\n", line);
                found_n_broken++;
                continue;
            }
            if (debugLevel (DEBUG_DXPEDS, 2))
                Serial.printf ("DXP: parsed: %s, %s, %s, %s, %s\n", start_f, end_f, loc_f, call_f, url_f);

            // extract unix times
            long start_t = atol(start_f);
            long end_t = atol(end_f);
            if (start_t < 1735689600L || end_t < 1735689600L) {         // 2025/01/01
                Serial.printf ("DXP: bogus time: %s,%s\n", start_f, end_f);
                found_n_broken++;
                continue;
            }

            // skip if dxpedition is already over
            if (end_t < now) {
                Serial.printf ("DXP: %s already passed: end %d/%d < %d/%d\n", call_f,
                                    month(end_t), day(end_t), month(now), day(now));
                continue;
            }

            // skip if not active and don't want
            if (show_current && (now < start_t || now > end_t)) {
                if (debugLevel (DEBUG_DXPEDS, 2))
                    Serial.printf ("DXP: skipping not active: %s\n", call_f);
                continue;
            }

            // skip if call can not be turned into ll
            LatLong ll;
            if (!call2LL (call_f, ll)) {
                Serial.printf ("DXP: can not find LL for %s\n", call_f);
                found_n_broken++;
                continue;
            }

            // dither in case of overlap
            ditherLL (ll);

            // skip if call can not be turned into dxcc
            int dxcc;
            if (!call2DXCC (call_f, dxcc)) {
                Serial.printf ("DXP: can not find DXCC for %s\n", call_f);
                found_n_broken++;
                continue;
            }

            // set prefix
            char prefix[MAX_PREF_LEN];
            findCallPrefix (call_f, prefix);

            // ok! add to dxpeds[]
            dxpeds = (DXPedEntry*) realloc (dxpeds, (dxp_ss.n_data+1) * sizeof(DXPedEntry));
            if (!dxpeds)
                fatalError ("No memory for %d dxpeds", dxp_ss.n_data+1);
            DXPedEntry &de = dxpeds[dxp_ss.n_data++];

            // set fields
            de = {};
            de.call = strdup (call_f);
            de.url = strdup (url_f);
            de.loc = strdup (loc_f);
            de.start_t = start_t;
            de.end_t = end_t;
            de.ll = ll;
            de.dxcc = dxcc;
            strcpy (de.prefix, prefix);
            formatDXPed (de, loc_f, call_f, box);         // uses start/end_t, sets date_str, title

            found_n++;
        }
    }

out:

    // sort by start time
    if (ok) {
        qsort (dxpeds, dxp_ss.n_data, sizeof(DXPedEntry), qsDXPedStart);
        Serial.printf ("DXP: %s: %d listing, %d found, %d broken\n", dxpeds_fn,
                                            dxp_ss.n_data, found_n, found_n_broken);
    }

    fclose (fp);
    return (ok);
}

/* note dxpeds that are newly active or remove if passed.
 * return whether any such or any have just become active.
 */
static bool checkActiveDXPeds (void)
{
    bool any_past = false;
    bool newly_active = false;
    time_t now = myNow();

    for (int i = 0; i < dxp_ss.n_data; i++) {
        DXPedEntry *cp = &dxpeds[i];
        if (cp->end_t <= now) {
            memmove (cp, cp+1, (--dxp_ss.n_data - i) * sizeof(DXPedEntry));
            i -= 1;                             // examine new [i] again next loop
            any_past = true;
        } else if (cp->start_t <= now && !cp->was_active) {
            cp->was_active = true;
            newly_active = true;
        }
    }

    if (any_past)
        dxp_ss.scrollToNewest();

    return (any_past || newly_active);
}

/* collect DXPed info into the dxpeds[] array and show in the given pane box
 */
bool updateDXPeds (const SBox &box, bool fresh)
{

    // retrieve once a day or fresh
    bool ok = true;
    static uint32_t prev_refresh;
    if (fresh || timesUp (&prev_refresh, DXPEDS_MAXAGE*1000)) {
        prev_refresh = millis();
        ok = retrieveDXPeds (box);
        if (ok) {
            dxp_ss.scrollToNewest();
            fresh = true;
        }
    }

    if (ok) {

        // insure dx cluster is running if checking spots -- N.B. rely on checkDXCluster() to close
        if (dxpedsWatchingCluster() && !isDXClusterConnected()) {
            static uint32_t connect_ms;
            if (connect_ms == 0 || timesUp (&connect_ms, DXCCHECK_DT)) {
                if (!connectDXCluster()) {              // shows it's own mapMsgs
                    fresh = true;                       // in case a red spot should be removed
                }
            }
        }

        if (checkActiveDXPeds() || fresh)
            drawDXPedsPane (box);

    } else {

        plotMessage (box, DXPEDS_COLOR, "DXPeds error");
    }

    return (ok);
}

/* draw all expeditions on map if active in any pane
 */
void drawDXPedsOnMap (void)
{
    // ignore and free if not in use
    if (dxp_ss.n_data == 0)
        return;
    PlotPane pp = findPaneForChoice (PLOT_CH_DXPEDS);
    if (pp == PANE_NONE) {

        // free all memory
        freeDXPeds();                   // sets dxp_ss.n_data = 0

    } else {

        // pane is in use somewhere so draw marks, draw SPOTS last to overlay
        for (int i = 0; i < dxp_ss.n_data; i++) {
            const DXPedEntry &de = dxpeds[i];
            uint16_t color = renderDXPedColor (de);
            if (color != SPOT_COLOR)
                drawDXPedsMarker (de.ll, color);
        }
        for (int i = 0; i < dxp_ss.n_data; i++) {
            const DXPedEntry &de = dxpeds[i];
            uint16_t color = renderDXPedColor (de);
            if (color == SPOT_COLOR)
                drawDXPedsMarker (de.ll, color);
        }

    }
}

/* return true if user is interacting with the dxpedition pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkDXPedsTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + TITLE_DY) {

        // scroll control? else just clicking title so return false

        if (dxp_ss.checkScrollUpTouch (s, box)) {
            scrollDXPedUp (box);
            return (true);
        }
        if (dxp_ss.checkScrollDownTouch (s, box)) {
            scrollDXPedDown (box);
            return (true);
        }

    } else if (s.y < box.y + START_DY) {

        if (runDXPedPaneMenu (box))
            scheduleNewPlot (PLOT_CH_DXPEDS);

        return (true);

    } else {

        // over an expedition?
        DXPedEntry *dep = NULL;
        int item = (s.y - box.y - START_DY)/DXPEDS_DY;
        if (show_date)
            item /= 2;
        int index;
        if (dxp_ss.findDataIndex (item, index))
            dep = &dxpeds[index];

        // redo if needed
        if (dep && runOneDXPedMenu (s, box, dep))
            scheduleNewPlot (PLOT_CH_DXPEDS);

        // ours in any case
        return (true);
    }

    // none of the above so not ours
    return (false);
}


/* return expeditions to caller
 * N.B. caller must free both lists and their contents iff we return > 0.
 */
int getDXPeds (char **&titles, char **&dates)
{
    if (dxp_ss.n_data > 0) {
        titles = (char **) malloc (dxp_ss.n_data * sizeof(const char *));
        dates = (char **) malloc (dxp_ss.n_data * sizeof(const char *));
        for (int i = 0; i < dxp_ss.n_data; i++) {
            titles[i] = strdup (dxpeds[i].title);
            dates[i] = strdup (dxpeds[i].date_str);
        }
    }

    return (dxp_ss.n_data);
}

/* return expedition info if s is over one of our pane entries
 */
bool getPaneDXPed (const SCoord &s, DXPedEntry *&dxp)
{
    // done if s not showing our pane or not in our box
    PlotPane pp = findPaneChoiceNow (PLOT_CH_DXPEDS);
    if (pp == PANE_NONE)
        return (false);
    const SBox &list_b = plot_b[pp];
    if (!inBox (s, list_b))
        return (false);

    // create box that will be placed over each listing entry
    SBox listrow_b;
    listrow_b.x = list_b.x;
    listrow_b.w = list_b.w;
    listrow_b.h = show_date ? 2*DXPEDS_DY : DXPEDS_DY;

    // scan listed spots for one located at s
    uint16_t y0 = list_b.y + START_DY;
    int min_i, max_i;
    if (dxp_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            listrow_b.y = y0 + dxp_ss.getDisplayRow(i) * listrow_b.h;
            if (inBox (s, listrow_b)) {
                // s is over this ped
                dxp = &dxpeds[i];
                return (true);
            }
        }
    }

    // none
    return (false);
}

/* return pointer to dxpeds closest to ll, within reason
 */
bool getClosestDXPed (LatLong &ll, DXPedEntry *&dxp)
{
    int min_i = -1;
    float min_d = 1e10;
    for (int i = 0; i < dxp_ss.n_data; i++) {
        float d = ll.GSD(dxpeds[i].ll);
        if (d < min_d) {
            min_d = d;
            min_i = i;
        }
    }

    if (min_i >= 0 && min_d*ERAD_M < MAX_CSR_DIST) {
        dxp = &dxpeds[min_i];
        return (true);
    }

    return (false);
}


/* we are being told a new spot has arrived or one has drifted off.
 * update if open unless scrolled away.
 */
void tellDXPedsSpotChanged (void)
{
    if (findPaneForChoice (PLOT_CH_DXPEDS) != PANE_NONE && dxp_ss.atNewest())
        scheduleNewPlot (PLOT_CH_DXPEDS);
}


/* qsort-style comparison of two DXPedsWorked
 */
static int qsWorkedCmp (const void *v1, const void *v2)
{
    DXPedsWorked *w1 = (DXPedsWorked*)v1;
    DXPedsWorked *w2 = (DXPedsWorked*)v2;

    // sort first by band then by mode
    int diff = (int)w1->hb - (int)w2->hb;
    if (diff == 0)
        diff = strcasecmp (w1->mode, w2->mode);
    return (diff);
}

/* add the given spot to adif_worked.
 * N.B. call for each ADIF spot, regardless of watch list.
 */
void addDXPedsWorked (const DXSpot &s)
{
    adif_worked = (ADIFWList *) realloc (adif_worked, (n_adif_worked+1) * sizeof(ADIFWList));
    ADIFWList &w = adif_worked[n_adif_worked++];
    w.dxcc = s.tx_dxcc;
    findCallPrefix (s.tx_call, w.prefix);
    w.worked.hb = findHamBand (s.kHz);
    strcpy (w.worked.mode, s.mode);
}

/* reset the list of ADIF worked
 */
void resetDXPedsWorked (void)
{
    if (adif_worked) {
        free ((void*)adif_worked);
        adif_worked = NULL;
        n_adif_worked = 0;
    }
}

/* pass back malloced list of sorted unique bands and modes that have worked the given expedition,
 * N.B. caller must free() if we return > 0
 */
int findDXPedsWorked (const DXPedEntry *dxp, DXPedsWorked *&worked)
{
    // get latest ADIF
    freshenADIFFile();

    // init list
    worked = NULL;
    int n_worked = 0;

    // scan list to build worked[]
    for (int i = 0; i < n_adif_worked; i++) {
        ADIFWList &wl = adif_worked[i];
        if (wl.dxcc == dxp->dxcc || strcasecmp (wl.prefix, dxp->prefix) == 0) {
            // add to worked[]
            worked = (DXPedsWorked *) realloc (worked, (n_worked + 1) * sizeof(DXPedsWorked));
            if (!worked)
                fatalError ("no memory for %d DXPedsWorked", n_worked+1);
            worked[n_worked++] = wl.worked;
        }
    }

    // sort
    qsort (worked, n_worked, sizeof(DXPedsWorked), qsWorkedCmp);

    // remove dups
    for (int i = 1; i < n_worked; i++) {
        if (memcmp (&worked[i-1], &worked[i], sizeof(DXPedsWorked)) == 0) {
            memmove (&worked[i], &worked[i+1], (--n_worked - i) * sizeof(DXPedsWorked));
            i -= 1;                                     // examine new [i] again next loop
        }
    }

    // done
    return (n_worked);
}

/* return whether any expedition hails from the given spot.
 */
bool findDXPedsCall (const DXSpot *sp)
{
    for (int i = 0; i < dxp_ss.n_data; i++)
        if (strcasecmp (sp->tx_call, dxpeds[i].call) == 0)
            return (true);
    return (false);
}

/* return whether we are watching the cluster for spots
 */
bool dxpedsWatchingCluster()
{
    return (watch_cluster && useDXCluster() && findPaneForChoice (PLOT_CH_DXPEDS) != PANE_NONE);
}
