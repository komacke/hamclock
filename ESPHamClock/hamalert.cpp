/* handle the HamAlert pane.
 *
 * HamAlert (hamalert.org) is a third-party service that watches DX cluster, RBN, SOTAwatch, POTA,
 * WWFF and PSKReporter for stations matching alerts the user has configured on the HamAlert web site,
 * then republishes just those matches to us over a small telnet-like protocol at hamalert.org:7300.
 * All the interesting filtering (DXCC, CQ zone, continent, IOTA, SOTA ref, spotter, time-of-day etc)
 * happens server-side -- we are just a thin display client, so unlike dxcluster.cpp there is no local
 * watch list to compile nor any user commands to send once logged in: everything HamAlert sends us is
 * already something the user asked to be told about.
 *
 * the wire format HamAlert streams to us is the venerable DX cluster spot line:
 *   DX de SPOTTER:     14025.0  W1AW         CQ FT8                       1234Z
 * which is exactly what crackClusterSpot() in parsespot.cpp already parses, so we reuse it as-is.
 */

#include "HamClock.h"


// layout
#define HAC_COLOR       RGB565(255,110,180)             // title and spot text color -- pink
#define HA_HOST         "hamalert.org"                  // fixed HamAlert telnet host
#define HA_PORT         7300                            // fixed HamAlert telnet port

// connection
static WiFiClient ha_client;                            // persistent TCP connection while displayed
#define HA_MAX_LCN      10                               // max lost connections per HA_MAX_LCDT
#define HA_MAX_LCDT     1800                             // max lost connections period, seconds

// ages
#define HA_MAXKEEP_DT   (60*30)                         // max age to stay on ha_spots list, secs

// timing
#define HA_BGCHECK_DT   1000                             // background checkHamAlert period, millis
#define HA_HBEAT_MS     60000                            // heartbeat interval, millis

// state
static DXSpot *ha_spots;                                // malloced list of all spots, oldest first
static ScrollState ha_ss;                                // scrolling info, count in ha_ss.n_data
static bool ha_spots_changed;                            // set to rebuild display because ha_spots changed
static time_t ha_scrolledaway_tm;                         // time() when user scrolled away from top
static uint32_t ha_activity_ms;                           // millis() of last socket activity
static bool ha_showbio;                                  // whether click shows bio

// crude in-memory (non-persistent) lost-connection throttle -- doesn't need to survive reboot
static uint32_t ha_lc_t0;                                 // time() when current lost-conn window started
static uint8_t ha_lc_n;                                   // n lost connections in current window


static void haLog (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));

/* log with our own prefix
 */
static void haLog (const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start (ap, fmt);
    (void) vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);
    chompString (msg);
    Serial.printf ("HAM ALERT: %s\n", msg);
}

/* free spots list memory
 */
static void resetHAMem()
{
    if (ha_spots) {
        free (ha_spots);
        ha_spots = NULL;
    }
    ha_ss.n_data = 0;
}

/* qsort-style compare by spotted time, ascending (oldest first)
 */
static int qsHASpotted (const void *p1, const void *p2)
{
    const DXSpot *s1 = (const DXSpot *)p1;
    const DXSpot *s2 = (const DXSpot *)p2;
    return (s1->spotted < s2->spotted ? -1 : (s1->spotted > s2->spotted ? 1 : 0));
}

/* return whether the most recent spot arrived after we scrolled away from the top
 */
static bool showingNewHASpot(void)
{
    return (ha_scrolledaway_tm > 0 && ha_ss.n_data > 0
                        && ha_spots[ha_ss.n_data-1].spotted > ha_scrolledaway_tm);
}

/* draw one spot row. no watch-list highlighting here: everything HamAlert sends us already matched
 * something the user asked to be alerted on, so every row is inherently "already qualified".
 */
static void drawHASpotRow (const SBox &box, const DXSpot &spot, int row)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    char line[50];

    const uint16_t x = box.x+1;
    const uint16_t y = box.y + LISTING_Y0 + row*LISTING_DY;
    const uint16_t h = LISTING_DY - 2;
    tft.fillRect (x, y-LISTING_OS, box.w-2, h, RA8875_BLACK);
    uint16_t cx = x;

    // freq, fixed width, bg matching band color assignment
    const char *f_fmt = spot.kHz < 1e6F ? "%8.1f" : "%8.0f";
    snprintf (line, sizeof(line), f_fmt, spot.kHz);
    const uint16_t fbg_col = getBandColor (spot.kHz);
    const uint16_t ffg_col = getGoodTextColor (fbg_col);
    tft.setTextColor (ffg_col);
    tft.fillRect (cx, y-LISTING_OS, 50, h, fbg_col);
    tft.setCursor (cx, y);
    tft.print (line);
    cx += 50;

    // DXCC entity number as a small badge, same idea as the little green pill on HB9DQM's
    // own HamAlert ESP32 client -- only when there's room to spare and we actually resolved one
    bool show_dxcc = box.w > 200 && spot.tx_dxcc > 0;
    if (show_dxcc) {
        snprintf (line, sizeof(line), " %d ", spot.tx_dxcc);
        uint16_t dxcc_w = getTextWidth (line) + 4;
        tft.fillRect (cx, y-LISTING_OS, dxcc_w, h, RA8875_GREEN);
        tft.setTextColor (RA8875_BLACK);
        tft.setCursor (cx+2, y);
        tft.print (line);
        cx += dxcc_w + 4;
    }

    // call
    const int max_call = BOX_IS_PANE_0(box) ? MAX_SPOTCALL_LEN-3 : MAX_SPOTCALL_LEN-1;
    tft.setTextColor (HAC_COLOR);
    snprintf (line, sizeof(line), " %-*.*s ", max_call, max_call, spot.tx_call);
    tft.setCursor (cx, y);
    tft.print (line);
    cx += getTextWidth (line);

    // mode, dimmer than the call so the callsign still reads as the primary element. nudged a
    // touch left of where it'd naturally fall right after the call, so it doesn't crowd the age
    // field over on the right edge.
    if (spot.mode[0]) {
        tft.setTextColor (RA8875_YELLOW);
        snprintf (line, sizeof(line), "%-4.4s", spot.mode);
        tft.setCursor (cx > x+8 ? cx-8 : cx, y);
        tft.print (line);
        cx += getTextWidth (line);
    }

    // age, right-justified against the box edge
    time_t age = myNow() - spot.spotted;
    char age_str[10];
    formatAge (age, age_str, sizeof(age_str), BOX_IS_PANE_0(box) ? 3 : 4);
    uint16_t age_w = getTextWidth (age_str);
    tft.setTextColor (HAC_COLOR);
    tft.setCursor (box.x + box.w - age_w - 3, y);
    tft.print (age_str);
}

/* draw all currently visible spots then update scroll markers
 */
static void drawAllVisHASpots (const SBox &box)
{
    uint16_t y0 = box.y + LISTING_Y0;
    tft.fillRect (box.x+1, y0-LISTING_OS, box.w-2, box.h - (LISTING_Y0-LISTING_OS+1), RA8875_BLACK);

    int min_i, max_i;
    if (ha_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++)
            drawHASpotRow (box, ha_spots[i], ha_ss.getDisplayRow(i));
    }

    ha_ss.drawScrollUpControl (box, HAC_COLOR, HAC_COLOR);
    ha_ss.drawScrollDownControl (box, HAC_COLOR, HAC_COLOR);
}

/* handy check whether New Spot symbol needs changing on/off
 */
static void checkNewHASpotSymbol (bool was_at_newest)
{
    if (was_at_newest && !ha_ss.atNewest()) {
        ha_scrolledaway_tm = myNow();
        ROTHOLD_SET(PLOT_CH_HAMALERT);
    } else if (!was_at_newest && ha_ss.atNewest()) {
        ha_ss.drawNewSpotsSymbol (false, false);
        ha_scrolledaway_tm = 0;
        ROTHOLD_CLR(PLOT_CH_HAMALERT);
    }
}

static void scrollHAUp (const SBox &box)
{
    bool was_at_newest = ha_ss.atNewest();
    if (ha_ss.okToScrollUp()) {
        ha_ss.scrollUp();
        drawAllVisHASpots (box);
    }
    checkNewHASpotSymbol (was_at_newest);
}

static void scrollHADown (const SBox &box)
{
    bool was_at_newest = ha_ss.atNewest();
    if (ha_ss.okToScrollDown()) {
        ha_ss.scrollDown();
        drawAllVisHASpots (box);
    }
    checkNewHASpotSymbol (was_at_newest);
}

/* set DX, radio and optional bio from the given row
 */
static void engageHARow (DXSpot &s)
{
    newDX (s.tx_ll, NULL, s.tx_call);
    setRadioSpot (s.kHz);
    if (ha_showbio)
        openQRZBio (s);
}

/* add a potentially new spot to ha_spots[], trimming anything too old along the way.
 * set ha_spots_changed if the list changed in content or count.
 */
static void addHASpot (DXSpot &new_spot)
{
    time_t now = myNow();
    time_t ancient = now - HA_MAXKEEP_DT;
    if (new_spot.spotted < ancient) {
        haLog ("%s %g dropped: too old\n", new_spot.tx_call, new_spot.kHz);
        return;
    }

    strtoupper (new_spot.rx_call);
    strtoupper (new_spot.tx_call);

    // drop anything that has aged out, and look for an exact dup while at it
    bool dup = false;
    for (int i = 0; i < ha_ss.n_data; i++) {
        DXSpot &spot = ha_spots[i];
        if (spot.spotted < ancient) {
            memmove (&ha_spots[i], &ha_spots[i+1], (--ha_ss.n_data - i) * sizeof(DXSpot));
            i -= 1;
            ha_spots_changed = true;
        } else if (!strcmp (spot.tx_call, new_spot.tx_call) && spot.kHz == new_spot.kHz
                                                             && spot.spotted == new_spot.spotted) {
            dup = true;
        }
    }
    if (dup)
        return;

    // tweak map location for unique picking
    ditherLL (new_spot.tx_ll);
    ditherLL (new_spot.rx_ll);

    ha_spots = (DXSpot *) realloc (ha_spots, (ha_ss.n_data+1) * sizeof(DXSpot));
    if (!ha_spots)
        fatalError ("No memory for %d HamAlert spots", ha_ss.n_data+1);
    ha_spots[ha_ss.n_data++] = new_spot;

    qsort (ha_spots, ha_ss.n_data, sizeof(DXSpot), qsHASpotted);

    ha_spots_changed = true;
}

static void incLostConn(void)
{
    ha_lc_n += 1;
    haLog ("lost connection: now %u\n", ha_lc_n);
}

static bool checkLostConnRate()
{
    time_t t0 = myNow();
    if (ha_lc_t0 == 0)
        ha_lc_t0 = t0;
    bool hit_max = false;
    if (ha_lc_n > HA_MAX_LCN) {
        if (t0 < ha_lc_t0 + HA_MAX_LCDT) {
            hit_max = true;
        } else {
            ha_lc_t0 = t0;
            ha_lc_n = 0;
        }
    }
    return (hit_max);
}

/* display the given error message and shut down the connection
 */
static void showHamAlertErr (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
static void showHamAlertErr (const char *fmt, ...)
{
    char buf[500];
    va_list ap;
    va_start (ap, fmt);
    size_t ml = snprintf (buf, sizeof(buf), "HamAlert error: ");
    vsnprintf (buf+ml, sizeof(buf)-ml, fmt, ap);
    va_end (ap);
    mapMsg (3000, "%s", buf);

    haLog ("%s\n", buf);

    closeHamAlert();
}

static void initHAGUI (const SBox &box)
{
    prepPlotBox (box);

    const char *title = BOX_IS_PANE_0(box) ? "HamAlert" : "HamAlert";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(HAC_COLOR);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    ha_ss.max_vis = (box.h - LISTING_Y0)/LISTING_DY;
    ha_ss.initNewSpotsSymbol (box, HAC_COLOR);
    ha_ss.dir = ha_ss.DIR_FROMSETUP;
    ha_ss.scrollToNewest();
}

/* insure connection is closed
 */
void closeHamAlert()
{
    if (ha_client) {
        ha_client.stop();
        haLog ("disconnect %s\n", ha_client ? "failed" : "ok");
    }
    resetHAMem();
}

/* try to connect and log in to HamAlert.
 * if success: ha_client is live, return true, else: closed, mapMsg shown, return false.
 */
bool connectHamAlert (void)
{
    if (checkLostConnRate()) {
        showHamAlertErr ("Dropped %d times, pausing", HA_MAX_LCN);
        return (false);
    }

    resetHAMem();

    const char *login = getHamAlertLogin();
    const char *passwd = getHamAlertPasswd();

    mapMsg (0, "Connecting to HamAlert");

    ha_client.stop();
    if (!ha_client.connect (HA_HOST, HA_PORT)) {
        showHamAlertErr ("%s:%d Connection failed", HA_HOST, HA_PORT);
        return (false);
    }

    updateClocks(false);
    haLog ("connect %s:%d ok\n", HA_HOST, HA_PORT);

    // the "login: " and "password: " prompts the server sends have no trailing newline so
    // getTCPLine() can never reliably return them as distinct lines -- same situation dxcluster.cpp
    // notes for the analogous DX Spider prompt. rather than try to detect them, just send both
    // credentials blindly, back to back, exactly as a real telnet client's line-buffered input would
    // arrive regardless of whether the prompt text itself has been read yet.
    haLog ("logging in as %s\n", login);
    wdDelay (200);
    ha_client.print (login);
    ha_client.print ("\n");

    wdDelay (500);
    ha_client.print (passwd);
    ha_client.print ("\n");
    haLog ("> [password]\n");

    // now drain and log whatever the server sends back for a few seconds -- a bad login typically
    // either closes the connection or sends an explicit rejection message; a good login may say
    // nothing further at all until the first alert arrives, so silence alone is not a failure.
    char buf[200];
    const size_t bufl = sizeof(buf);
    uint16_t bl;
    bool ok = true;
    uint32_t t0 = millis();
    while (millis() - t0 < 3000) {
        if (getTCPLine (ha_client, buf, bufl, &bl)) {
            haLog ("< %s\n", buf);
            char lc[200];
            quietStrncpy (lc, buf, sizeof(lc));
            strtolower (lc);
            if (strstr (lc, "invalid") || strstr (lc, "incorrect") || strstr (lc, "denied")
                                        || strstr (lc, "failed")) {
                ok = false;
                break;
            }
        }
        if (!ha_client) {
            ok = false;
            break;
        }
        wdDelay (50);
    }

    if (!ok || !ha_client) {
        incLostConn();
        showHamAlertErr ("Login failed");
        return (false);
    }

    haLog ("HamAlert connection now operational\n");
    mapMsg (1000, "Connected to HamAlert");

    // determine bio setting, mirrors dxcluster's NV_DXCBIO convention but its own flag
    uint8_t bio = 0;
    if (getQRZId() != QRZ_NONE) {
        if (!NVReadUInt8 (NV_DXCBIO, &bio))
            bio = 0;
    }
    ha_showbio = (bio != 0);

    ha_activity_ms = millis();

    return (true);
}

/* called often while pane is visible, fresh set when newly so.
 * connect if not already then show list. return whether connection is open.
 */
bool updateHamAlert (const SBox &box, bool fresh)
{
    if (!useHamAlert()) {
        // selectable in the menu regardless of configuration, but never attempt a connection,
        // and never even try, until a password (and optionally login) has actually been entered
        if (fresh)
            initHAGUI (box);
        plotMessage (box, HAC_COLOR, "Set HamAlert User/Password in Setup pg 7");
        return (false);
    }

    if (!isHamAlertConnected()) {
        if (!connectHamAlert()) {
            initHAGUI (box);
            return (false);
        }
        fresh = true;
    }

    if (fresh)
        initHAGUI (box);

    // always jump to show the newest alert as it arrives -- this pane exists specifically so
    // alerts aren't missed, so unlike DX Cluster/Live Spots it does not hold position while
    // scrolled away and wait for a manual tap on the new-spot indicator.
    if (ha_spots_changed) {
        ha_ss.scrollToNewest();
        // trim any spots that aged out since last check
        time_t ancient = myNow() - HA_MAXKEEP_DT;
        while (ha_ss.n_data > 0 && ha_spots[0].spotted < ancient)
            memmove (&ha_spots[0], &ha_spots[1], (--ha_ss.n_data) * sizeof(DXSpot));
        ha_spots_changed = false;
        ha_ss.drawNewSpotsSymbol (false, false);
        ha_scrolledaway_tm = 0;
        if (findPaneForChoice (PLOT_CH_HAMALERT) != PANE_NONE)
            scheduleMapRedraw();
    }
    ROTHOLD_CLR(PLOT_CH_HAMALERT);

    drawAllVisHASpots (box);

    return (true);
}

/* called often to add any new spots to list IFF connection already open.
 * N.B. this is not a thread but can be thought of as a "background" function, no GUI.
 * N.B. we never open the connection, that is done by updateHamAlert(), but we close it
 *      if no longer selected in any pane.
 */
void checkHamAlert()
{
    if (!isHamAlertConnected())
        return;

    static uint32_t prev_check;
    if (!timesUp (&prev_check, HA_BGCHECK_DT))
        return;

    if (findPaneForChoice (PLOT_CH_HAMALERT) == PANE_NONE) {
        haLog ("closing because no longer in any pane\n");
        closeHamAlert();
        return;
    }

    char line[120];
    while (ha_client.available() && getTCPLine (ha_client, line, sizeof(line), NULL)) {
        haLog ("< %s\n", line);
        updateClocks(false);

        DXSpot new_spot;
        if (crackClusterSpot (line, new_spot))
            addHASpot (new_spot);
    }

    // send a harmless heartbeat if idle too long, per HB9DQM's documented "echo" command
    if (timesUp (&ha_activity_ms, HA_HBEAT_MS)) {
        ha_client.print ("echo\n");
        ha_activity_ms = millis();
    }

    if (!ha_client) {
        haLog ("bg lost connection\n");
        incLostConn();
        closeHamAlert();
    }
}

/* determine and engage a HamAlert pane touch.
 * return true if user is interacting with the pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkHamAlertTouch (TouchType tt, const SCoord &s, const SBox &box)
{
    (void)tt;

    if (s.y < box.y + PANETITLE_H) {

        if (ha_ss.checkScrollUpTouch (s, box)) {
            scrollHAUp (box);
            return (true);
        }
        if (ha_ss.checkScrollDownTouch (s, box)) {
            scrollHADown (box);
            return (true);
        }
        if (ha_ss.checkNewSpotsTouch (s, box)) {
            if (!ha_ss.atNewest() && showingNewHASpot())
                ha_ss.scrollToNewest();
            return (true);
        }
        if (ROTHOLD_TST(PLOT_CH_HAMALERT))
            return (true);

        return (false);
    }

    // everything else may be a tapped spot
    int vis_row = (s.y - (box.y + LISTING_Y0)) / LISTING_DY;
    int spot_row;
    if (ha_ss.findDataIndex (vis_row, spot_row) && ha_spots[spot_row].tx_call[0] != '\0'
                                                 && isHamAlertConnected())
        engageHARow (ha_spots[spot_row]);

    return (true);
}

/* return whether HamAlert is currently connected
 */
bool isHamAlertConnected()
{
    return (useHamAlert() && (bool)ha_client);
}

/* draw all qualifying paths and spots on map
 */
void drawHamAlertSpotsOnMap ()
{
    if (!isHamAlertConnected())
        return;
    if (findPaneForChoice (PLOT_CH_HAMALERT) == PANE_NONE)
        return;

    for (int i = 0; i < ha_ss.n_data; i++)
        drawSpotPathOnMap (ha_spots[i]);
    for (int i = 0; i < ha_ss.n_data; i++) {
        drawSpotLabelOnMap (ha_spots[i], LOME_TXEND, LOMD_ALL);
        drawSpotLabelOnMap (ha_spots[i], LOME_RXEND, LOMD_JUSTDOT);
    }
}

/* find closest spot and location on either end to given ll, if any
 */
bool getClosestHamAlert (LatLong &ll, DXSpot *sp, LatLong *llp)
{
    if (!isHamAlertConnected())
        return (false);
    if (findPaneForChoice (PLOT_CH_HAMALERT) == PANE_NONE)
        return (false);

    return (getClosestSpot (ha_spots, ha_ss.n_data, NULL, LOME_BOTH, ll, sp, llp));
}

/* return spot in our pane if under ms
 */
bool getHamAlertPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll)
{
    PlotPane pp = findPaneChoiceNow (PLOT_CH_HAMALERT);
    if (pp == PANE_NONE)
        return (false);
    if (!inBox (ms, plot_b[pp]))
        return (false);

    SBox listrow_b;
    listrow_b.x = plot_b[pp].x;
    listrow_b.w = plot_b[pp].w;
    listrow_b.h = LISTING_DY;

    uint16_t y0 = plot_b[pp].y + LISTING_Y0;
    int min_i, max_i;
    if (ha_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            listrow_b.y = y0 + ha_ss.getDisplayRow(i) * LISTING_DY;
            if (inBox (ms, listrow_b)) {
                *dxs = ha_spots[i];
                *ll = dxs->tx_ll;
                return (true);
            }
        }
    }

    return (false);
}
