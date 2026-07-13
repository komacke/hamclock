/* handle the DX Cluster display.
 *
 * support Spider, AR and CC clusters, plus several UDP packet formats.
 *
 * We actually keep two lists:
 *   dxc_spots: the complete raw list, not filtered nor sorted; length in n_dxspots.
 *   dxwl_spots: watchlist-filtered and time-sorted for display; length in dxc_ss.n_data.
 * 
 */

#include "HamClock.h"


// layout 
#define DXC_COLOR       RA8875_GREEN
#define CLRBOX_DX       6                       // clear control box left offset
#define CLRBOX_DY       6                       // " top offset
#define CLRBOX_W        20                      // " width
#define CLRBOX_H        11                      // " height

// connection info
static WiFiClient dxc_client;                   // persistent TCP connection while displayed ...
static WiFiUDP udp_server;                      // or persistent UDP "connection" to WSJT-X client program
static bool multi_cntn;                         // set when cluster has noticed multiple connections
#define MAX_LCN         10                      // max lost connections per MAX_LCDT
#define MAX_LCDT        1800                    // max lost connections period, seconds

// ages
static const uint8_t dxc_ages[] = {10, 20, 40, 60};   // menu selections in ascending order, minutes
static uint8_t dxc_age;                               // one of above, once set
#define N_DXCAGES       NARRAY(dxc_ages)              // handy count
#define MAXKEEP_DT      (60*dxc_ages[N_DXCAGES-1])    // max age to stay on dxc_spots list, secs

// timing
#define BGCHECK_DT      1000                    // background checkDXCluster period, millis
#define DXCMSG_DT       500                     // delay before sending each cluster message, millis
#define HBEAT_MS        60000                   // heatbeat interval, millis

// state
static DXSpot *dxc_spots;                       // malloced list of all spots
static int n_dxspots;                           // n spots in dxc_spots
static DXSpot *dxwl_spots;                      // malloced list, filtered for display, count in dxc_ss.n_data
static ScrollState dxc_ss;                      // scrolling info, and count of dxwl_spots
static bool dxc_showbio;                        // whether click shows bio
static bool dxc_spots_changed;                  // set to rebuild display because dxc_spots changed
static bool dxc_updateDE;                       // request to send DE location when possible
static bool new_dxc_cntn;                       // set to commence initial server handshake
static time_t scrolledaway_tm;                  // time() when user scrolled away from top of list
static uint32_t dxc_activity_ms;                // millis() of last socket activity
static SBox dxcclr_b;                           // Clear spots control box


// type
typedef enum {
    CT_UNKNOWN,
    CT_READONLY,                                // read spider spots but never send anything
    CT_ARCLUSTER,
    CT_DXSPIDER,
    CT_VE7CC,
    CT_UDP,
} DXClusterType;
static DXClusterType cl_type;

#if defined(__GNUC__)
static void showDXClusterErr (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
static void dxcSendMsg (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void showDXClusterErr (const char *fmt, ...);
static void dxcSendMsg (const char *fmt, ...);
#endif


/* draw, else erase, the clear spots control
 */
static void drawClearListBtn (bool draw)
{
    uint16_t color = draw ? DXC_COLOR : RA8875_BLACK;

    drawSBox (dxcclr_b, color);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (dxcclr_b.x+1, dxcclr_b.y+2);
    tft.setTextColor (color);
    tft.print ("CLR");
}

/* handy check whether we are, or should, show the New spots symbol
 */
static bool showingNewSpot(void)
{
    return (scrolledaway_tm > 0 && n_dxspots > 0 && dxc_spots[n_dxspots-1].spotted > scrolledaway_tm);
}

/* rebuild dxwl_spots from dxc_spots
 */
static void rebuildDXWatchList(void)
{
    // update ADIF if in use in case our WL uses it
    freshenADIFFile();

    // extract qualifying spots
    time_t oldest = myNow() - 60*dxc_age;               // oldest time to display, seconds
    dxc_ss.n_data = 0;                                  // reset count, don't bother to resize dxwl_spots
    for (int i = 0; i < n_dxspots; i++) {
        DXSpot &spot = dxc_spots[i];
        if (spot.spotted >= oldest && checkWatchListSpot (WLID_DX, spot) != WLS_NO) {
            dxwl_spots = (DXSpot *) realloc (dxwl_spots, (dxc_ss.n_data+1) * sizeof(DXSpot));
            if (!dxwl_spots)
                fatalError ("No mem for %d watch list spots", dxc_ss.n_data+1);
            dxwl_spots[dxc_ss.n_data++] = spot;
        }
    }

    // resort and scroll to newest
    qsort (dxwl_spots, dxc_ss.n_data, sizeof(DXSpot), qsDXCSpotted);
    dxc_ss.scrollToNewest();
}

/* draw all currently visible spots in the pane then update scroll markers if more
 */
static void drawAllVisDXCSpots (const SBox &box)
{
    drawVisibleSpots (WLID_DX, dxwl_spots, dxc_ss, box, DXC_COLOR);
    drawClearListBtn (dxc_ss.n_data > 0);
}

/* handy check whether New Spot symbol needs changing on/off
 */
static void checkNewSpotSymbol (bool was_at_newest)
{
    if (was_at_newest && !dxc_ss.atNewest()) {
        scrolledaway_tm = myNow();                              // record when moved off top
        ROTHOLD_SET(PLOT_CH_DXCLUSTER);                         // disable rotation
    } else if (!was_at_newest && dxc_ss.atNewest()) {
        dxc_ss.drawNewSpotsSymbol (false, false);               // turn off entirely
        rebuildDXWatchList ();
        scrolledaway_tm = 0;
        ROTHOLD_CLR(PLOT_CH_DXCLUSTER);                         // resume rotation
    }
}

/* shift the visible list up, if possible.
 * if reach the end with the newest entry, turn off New spots and update dxwl_spots
 */
static void scrollDXCUp (const SBox &box)
{
    bool was_at_newest = dxc_ss.atNewest();
    if (dxc_ss.okToScrollUp()) {
        dxc_ss.scrollUp();
        drawAllVisDXCSpots(box);
    }
    checkNewSpotSymbol (was_at_newest);
}

/* shift the visible list down, if possible.
 * set scrolledaway_tm if scrolling away from newest entry
 */
static void scrollDXCDown (const SBox &box)
{
    bool was_at_newest = dxc_ss.atNewest();
    if (dxc_ss.okToScrollDown()) {
        dxc_ss.scrollDown ();
        drawAllVisDXCSpots (box);
    }
    checkNewSpotSymbol (was_at_newest);
}

/* set bio, radio and DX from given row, known to be defined
 */
static void engageDXCRow (DXSpot &s)
{
    newDX (s.tx_ll, NULL, s.tx_call);
    setRadioSpot(s.kHz);
    if (dxc_showbio)
        openQRZBio (s);
}

/* log the given spot roughly similar to how spider spots look.
 * this is intended for logging spots from UDP.
 * DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z
 */
static void logDXSpot (const char *label, const DXSpot &spot)
{
    const struct tm *spot_time = gmtime (&spot.spotted);
    dxcLog ("%s: DX de %-10s %8.1f  %-44s%02d%02dZ\n", label, spot.rx_call, spot.kHz, spot.tx_call,
                                spot_time->tm_hour, spot_time->tm_min);
}

/* add a potentially new spot to dxc_spots[].
 * set dxc_spots_changed if dxc_spots changed in either content or count.
 * set DX too if asked and desired.
 */
static void addDXClusterSpot (DXSpot &new_spot, bool set_dx)
{
    // just discard if already too old
    time_t now = myNow();
    time_t ancient = now - MAXKEEP_DT;
    if (new_spot.spotted < ancient) {
        dxcLog ("new %s %g dropped: age %ld already > %d mins\n", new_spot.tx_call, new_spot.kHz, 
            (long)((now - new_spot.spotted)/60), MAXKEEP_DT/60);
        return;
    }

    // nice to insure calls are upper case
    strtoupper (new_spot.rx_call);
    strtoupper (new_spot.tx_call);

    // handy
    HamBandSetting new_band = findHamBand(new_spot.kHz);

    // check for dup, and remove any ancient spots along the way
    bool same_spot = false;
    for (int i = 0; i < n_dxspots; i++) {
        DXSpot &spot = dxc_spots[i];
        if (spot.spotted < ancient) {
            dxcLog ("%s %g: aged out\n", spot.tx_call, spot.kHz);
            memmove (&dxc_spots[i], &dxc_spots[i+1], (--n_dxspots - i) * sizeof(DXSpot));
            i -= 1;                                     // examine new [i] again next loop
            dxc_spots_changed = true;                   // update GUI with updated list
        } else if (!strcmp (spot.tx_call, new_spot.tx_call) && findHamBand (spot.kHz) == new_band) {
            // consider dupe if same tx call and band
            int spt_hr = hour(spot.spotted);
            int spt_mn = minute(spot.spotted);
            int new_hr = hour(new_spot.spotted);
            int new_mn = minute(new_spot.spotted);
            same_spot = true;
            if (new_spot.spotted > spot.spotted) {
                dxcLog ("%s %g: updated %02d%02dZ > %02d%02dZ\n", new_spot.tx_call, new_spot.kHz,
                                                                new_hr, new_mn, spt_hr, spt_mn);
                spot = new_spot;                        // update info
                dxc_spots_changed = true;               // update GUI with new age
            } else if (new_spot.spotted == spot.spotted) {
                dxcLog ("%s %g: dup time %02d%02dZ\n", spot.tx_call, spot.kHz, spt_hr, spt_mn);
            } else {
                dxcLog ("%s %g: superseded %02d%02dZ < %02d%02dZ\n", spot.tx_call, spot.kHz,
                                                                new_hr, new_mn, spt_hr, spt_mn);
            }
        }
    }
    
    // that's it if already in dxc_spots
    if (same_spot)
        return;

    // tweak map location for unique picking
    ditherLL (new_spot.tx_ll);
    ditherLL (new_spot.rx_ll);

    // append to dxc_spots
    dxc_spots = (DXSpot *) realloc (dxc_spots, (n_dxspots+1) * sizeof(DXSpot));
    if (!dxc_spots)
        fatalError ("No memory for %d DX spots", n_dxspots+1);
    dxc_spots[n_dxspots++] = new_spot;

    // set new DX if desired
    if (set_dx) {
        newDX (new_spot.tx_ll, new_spot.tx_grid, new_spot.tx_call);

        // move mouse too to show in info box
        SCoord s;
        ll2s (new_spot.tx_ll, s, 5);
        tft.setMouse (s.x, s.y);
    }

    // update GUI with new spot
    dxc_spots_changed = true;

    // inform others who might care about a new spot
    tellDXPedsSpotChanged();
}


/* display the given error message and shut down the connection.
 */
static void showDXClusterErr (const char *fmt, ...)
{
    char buf[500];
    va_list ap;
    va_start (ap, fmt);
    size_t ml = snprintf (buf, sizeof(buf), "DX Cluster error: ");
    vsnprintf (buf+ml, sizeof(buf)-ml, fmt, ap);
    va_end (ap);
    mapMsg (3000, "%s", buf);

    // log
    dxcLog ("%s\n", buf);

    // shut down connection
    closeDXCluster();
}


/* increment NV_DXMAX_N
 */
static void incLostConn(void)
{
    uint8_t n_lostconn;
    if (!NVReadUInt8 (NV_DXMAX_N, &n_lostconn))
        n_lostconn = 0;
    n_lostconn += 1;
    NVWriteUInt8 (NV_DXMAX_N, n_lostconn);
    dxcLog ("lost connection: now %u\n", n_lostconn);
}

/* return whether max lost connection rate has been reached
 */
static bool checkLostConnRate()
{
    uint32_t t0 = (uint32_t)myNow();        // time now in same units as those saved
    uint32_t t_maxconn;                     // time when the limit was last reached
    uint8_t n_lostconn;                     // n connections lost so far since t_maxconn

    // get current state
    if (!NVReadUInt32 (NV_DXMAX_T, &t_maxconn)) {
        t_maxconn = t0;
        NVWriteUInt32 (NV_DXMAX_T, t_maxconn);
    }
    if (!NVReadUInt8 (NV_DXMAX_N, &n_lostconn)) {
        n_lostconn = 0;
        NVWriteUInt8 (NV_DXMAX_N, n_lostconn);
    }
    dxcLog ("%u lost connections since %u\n", n_lostconn, t_maxconn);

    // check if max lost connections have been hit
    bool hit_max = false;
    if (n_lostconn > MAX_LCN) {
        if (t0 < t_maxconn + MAX_LCDT) {
            // hit the max during the last MAX_LCDT 
            hit_max = true;
        } else {
            // record the time and start a new count
            NVWriteUInt32 (NV_DXMAX_T, t0);
            n_lostconn = 0;
            NVWriteUInt8 (NV_DXMAX_N, n_lostconn);
        }
    }

    return (hit_max);
}

/* given a cluster line, set multi_cntn if it seems to be telling us it has detected multiple connections
 *   from the same call-ssid.
 * not at all sure this works everywhere.
 * Only Spiders seem to care enough to dicsonnect, AR and CC clusters report but otherwise don't care.
 */
static void detectMultiConnection (const char *line)
{
    // first seems typical for spiders, second for AR
    if (strstr (line, "econnected") != NULL || strstr (line, "Dupe call") != NULL)
        multi_cntn = true;
}

/* send a message to dxc_client.
 * here for convenience of stdarg, logging and delay.
 * N.B. we assume fmt will include NL
 */
static void dxcSendMsg (const char *fmt, ...)
{
    // format
    char msg[400];
    va_list ap;
    va_start (ap, fmt);
    (void) vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);

    // friendly delay
    wdDelay (DXCMSG_DT);

    // send and log
    dxc_client.print (msg);
    dxcLog ("> %s", msg);

    // update activity timer
    dxc_activity_ms = millis();
}

/* send a request for recent spots such that they will arrive the same as normal
 */
static void requestRecentSpots (void)
{
    // silently ignored if RO
    if (cl_type == CT_READONLY)
        return;

    const char *msg = NULL;

    if (cl_type == CT_DXSPIDER)
        msg = "sh/dx filter real 30";
    else if (cl_type == CT_ARCLUSTER)
        msg = "show/dx/30 @";
    else if (cl_type == CT_VE7CC)
        msg = "show/myfdx";   // always 30, does not accept a count

    if (msg)
        dxcSendMsg ("%s\n", msg);
}

/* free both spots lists memory
 */
static void resetDXMem()
{
    if (dxc_spots) {
        free (dxc_spots);
        dxc_spots = NULL;
        n_dxspots = 0;
    }

    if (dxwl_spots) {
        free (dxwl_spots);
        dxwl_spots = NULL;
        dxc_ss.n_data = 0;
    }
}

/* return whether the given host appears to be a multicast address
 */
static bool isHostMulticast (const char *host)
{
    int first_octet = atoi (host);
    return (first_octet >= 224 && first_octet <= 239);  // will always be false if URL
}

/* send the next getDXClCommands() if more to send or restart.
 * return whether all have been sent.
 * N.B. although not implemented herein, it is expected we are not called in a tight loop in order
 *   that commands are spaced out.
 */
static bool sendNextUserCommand (bool restart)
{
    static int next_cmd;                            // next getDXClCommands() index to send

    // skip if RO
    if (cl_type == CT_READONLY)
        return (false);

    // collect
    const char *dx_cmds[N_DXCLCMDS];
    bool dx_on[N_DXCLCMDS];
    getDXClCommands (dx_cmds, dx_on);

    // restart if desired
    if (restart)
        next_cmd = 0;

    // send next until last
    while (next_cmd < N_DXCLCMDS && (!dx_on[next_cmd] || strlen(dx_cmds[next_cmd]) == 0))
        next_cmd++;
    if (next_cmd < N_DXCLCMDS)
        dxcSendMsg("%s\n", dx_cmds[next_cmd++]);

    // all?
    return (next_cmd == N_DXCLCMDS);
}

/* return whether the given line is the Spider query for setting location.
 * we get this prompt from a spider only when it does not already know our location.
 */
static bool queryForQRA (const char line[])
{
    return (strcistr (line, "Please enter your location with set/location or set/qra") != NULL);
}

/* display the current cluster host in the given color
 */
static void showDXCHost (const SBox &box, uint16_t c)
{
    const char *dxhost = getDXClusterHost();
    int dxport = getDXClusterPort();

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    char host[50];

    if (cl_type == CT_UDP && !isHostMulticast (dxhost)) {
        snprintf (host, sizeof(host), "UDP port %d", dxport);
    } else {
        snprintf (host, sizeof(host), "%s:%d", dxhost, dxport);
        uint16_t hw = getTextWidth (host);
        if (hw > box.w - 5)
            snprintf (host, sizeof(host), "%s", dxhost);
    }

    tft.setTextColor(c);
    uint16_t hw = getTextWidth (host);
    tft.setCursor (box.x + (box.w-hw)/2, box.y + SUBTITLE_Y0);
    tft.print (host);
}

/* send our lat/long and grid to dxc_client, depending on cluster type.
 */
static void sendDELLGrid()
{
    // silently succeeds if RO
    if (cl_type == CT_READONLY)
        return;

    // DE grid as string
    char maid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, maid);

    // command syntax depends on type

    switch (cl_type) {

    case CT_DXSPIDER:   // fallthru
    case CT_VE7CC:

        // set grid
        dxcSendMsg ("set/qra %s\n", maid);

        break;

    case CT_ARCLUSTER:

        // friendly turn off skimmer just avoid getting swamped
        dxcSendMsg ("set dx filter not skimmer\n");

        // set grid
        dxcSendMsg ("set station grid %s\n", maid);

        break;

    default:

        // other have no such command
        break;
    }
}

/* prepare a fresh box but preserve any existing spots
 */
static void initDXGUI (const SBox &box)
{
    // prep box
    prepPlotBox (box);

    // locate the Clr box
    dxcclr_b = {(uint16_t)(box.x+CLRBOX_DX), (uint16_t)(box.y+CLRBOX_DY), CLRBOX_W, CLRBOX_H};

    // title
    const char *title = BOX_IS_PANE_0(box) ? "Cluster" : "DX Cluster";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(DXC_COLOR);
    uint16_t tw = getTextWidth(title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // init scroller for this box size but leave n_data
    dxc_ss.max_vis = (box.h - LISTING_Y0)/LISTING_DY;
    dxc_ss.initNewSpotsSymbol (box, DXC_COLOR);
    dxc_ss.dir = dxc_ss.DIR_FROMSETUP;
    dxc_ss.scrollToNewest();
}


/* run menu to allow editing watch list
 */
static void runDXClusterMenu (const SBox &box)
{
    // set up the MENU_TEXT field 
    MenuText mtext;                                             // menu text prompt context
    char wl_state[WLA_MAXLEN];                                  // wl state, menu may change
    setupWLMenuText (WLID_DX, mtext, wl_state);

    // build the possible age labels
    char dxages_str[N_DXCAGES][10];
    for (int i = 0; i < N_DXCAGES; i++)
        snprintf (dxages_str[i], sizeof(dxages_str[i]), "%d m", dxc_ages[i]);

    // whether to show bio on click, only show in menu at all if bio source has been set in Setup
    bool show_bio_enabled = getQRZId() != QRZ_NONE;
    MenuFieldType bio_lbl_mft = show_bio_enabled ? MENU_LABEL : MENU_IGNORE;
    MenuFieldType bio_yes_mft = show_bio_enabled ? MENU_1OFN : MENU_IGNORE;
    MenuFieldType bio_no_mft = show_bio_enabled ? MENU_1OFN : MENU_IGNORE;

    // optional bio and watch list
    #define MI_AGE_GRP  3                                       // MenuItem.group for the age items
    MenuItem mitems[10] = {
        // column 1
        {bio_lbl_mft, false,                 0, 2, "Bio:", NULL},                       // 0
        {MENU_LABEL, false,                  1, 2, "Age:", NULL},                       // 1
        {MENU_BLANK, false,                  2, 0, NULL, NULL},                         // 2

        // column 2
        {bio_yes_mft, dxc_showbio,           5, 2, "Yes", NULL},                        // 3
        {MENU_1OFN,  dxc_age == dxc_ages[0], MI_AGE_GRP, 2, dxages_str[0], NULL},       // 4
        {MENU_1OFN,  dxc_age == dxc_ages[2], MI_AGE_GRP, 2, dxages_str[2], NULL},       // 5

        // column 3
        {bio_no_mft, !dxc_showbio,           5, 2, "No", NULL},                         // 6
        {MENU_1OFN,  dxc_age == dxc_ages[1], MI_AGE_GRP, 2, dxages_str[1], NULL},       // 7
        {MENU_1OFN,  dxc_age == dxc_ages[3], MI_AGE_GRP, 2, dxages_str[3], NULL},       // 8

        // watch list
        {MENU_TEXT,  false,                  4, 2, wl_state, &mtext},                   // 9
    };


    SBox menu_b = box;                                  // copy, not ref!
    menu_b.x = box.x + 5;
    menu_b.y = box.y + SUBTITLE_Y0;
    menu_b.w = box.w-10;
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 3, NARRAY(mitems), mitems};
    if (runMenu (menu)) {

        // check bio
        if (show_bio_enabled) {
            dxc_showbio = mitems[3].set;
            NVWriteUInt8 (NV_DXCBIO, dxc_showbio);
        }

        // set desired age
        for (int i = 0; i < NARRAY(mitems); i++) {
            MenuItem &mi = mitems[i];
            if (mi.group == MI_AGE_GRP && mi.set) {
                dxc_age = atoi (mi.label);
                NVWriteUInt8 (NV_DXCAGE, dxc_age);
                break;
            }
        }

        // must recompile to update wl but runMenu already insured wl compiles ok
        Message ynot;
        if (lookupWatchListState (mtext.label) != WLA_OFF && !compileWatchList (WLID_DX, mtext.text, ynot))
            fatalError ("dxc failed recompling wl %s: %s", mtext.text, ynot.get());
        setWatchList (WLID_DX, mtext.label, mtext.text);
        dxcLog ("set WL to %s %s\n", mtext.label, mtext.text);

        // rebuild with new options
        rebuildDXWatchList();

        // full update to capture any/all changes
        dxc_spots_changed = true;
        scheduleNewPlot (PLOT_CH_DXCLUSTER);

    }


    // always free the working watch list text
    free (mtext.text);
}

/* send something end-to-end just to grease the connection for all intermediate routers
 */
static void sendHeartbeat(void)
{
    // can not find anything published for AR Cluster

    dxcSendMsg ("\r\n");
}

/* get next line from dxc_client.
 * return whether line is ready.
 */
static bool getNextDXCLine (char line[], size_t ll)
{
    bool ok = dxc_client.available() && getTCPLine (dxc_client, line, ll, NULL);
    if (ok) {
        if (queryForQRA (line))
            dxc_updateDE = true;
        dxc_activity_ms = millis();
    }
    return (ok);
}

/* get next UDP packet up to packet_size from udp_server.
 * return length or 0 if nothing or trouble..
 * N.B. we do not block, we return whether new packet is immediately ready.
 */
static int getUDPPacket (uint8_t packet[], int packet_size)
{
    // get expected size
    int psize = udp_server.parsePacket();

    // always read, even if have to ditch it
    if (psize > packet_size) {
        uint8_t big_buf[10000];
        dxcLog ("UDP size %d > available %d\n", udp_server.read (big_buf, sizeof(big_buf)), packet_size);
        return (0);
    }

    // ok, should fit in packet
    if (psize > 0) {
        int n_read = udp_server.read (packet, psize);
        if (n_read == psize)
            return (psize);
        fatalError ("bogus UDP packet size %d != %d", n_read, psize);
    }

    // nothing heard
    return (0);
}


/* handle all incoming UDP -- called by checkDXCluster()
 */
static void incomingUDP()
{
    // drain ALL pending UDP packets

    uint8_t packet[2000];
    int p_len;
    while ((p_len = getUDPPacket (packet, sizeof(packet)-1)) > 0) {         // allow for adding EOS

        // add EOS
        if (debugLevel (DEBUG_DXC, 1))
            dxcLog ("UDP: read packet containing %d bytes\n", p_len);
        packet[p_len] = '\0';

        // auto-check several popular formats
        DXSpot spot;
        uint8_t *bp = packet;
        if (wsjtxIsStatusMsg (&bp)) {
            if (wsjtxParseStatusMsg (bp, spot)) {
                if (useUDPSpot (spot)) {
                    logDXSpot ("WSJT-X", spot);
                    addDXClusterSpot (spot, UDPSetsDX());
                }
            }
        } else if (crackXMLSpot ((char *)packet, spot)) {
            if (useUDPSpot (spot)) {
                logDXSpot ("XML", spot);
                addDXClusterSpot (spot, UDPSetsDX());
            }
        } else if (crackADIFSpot ((char *)packet, spot)) {
            if (useUDPSpot (spot)) {
                logDXSpot ("ADIF", spot);
                addDXClusterSpot (spot, UDPSetsDX());
            }
        } else {
            dxcLog ("received unrecognized UDP packet\n");
        }
    }
}

/* handle all incoming DX Cluster -- called by checkDXCluster()
 */
static void incomingDXC()
{
    // roll all pending new spots into list as fast as possible but don't block if nothing waiting
    char line[120];
    while (getNextDXCLine (line, sizeof(line))) {

        // note incoming message and time
        dxcLog ("< %s\n", line);
        detectMultiConnection (line);

        // look alive
        updateClocks(false);

        // crack and add
        DXSpot new_spot;
        if (crackClusterSpot (line, new_spot))
            addDXClusterSpot (new_spot, false);        // already logged above
    }
    
    // send fresh location whenever requested
    if (dxc_updateDE) {
        sendDELLGrid();
        dxc_updateDE = false;
    }

    // new connections first send all user commands then request recent spots subject to those cmds.
    if (new_dxc_cntn) {
        static bool first_user_sent;
        if (sendNextUserCommand (new_dxc_cntn && !first_user_sent)) {
            requestRecentSpots();
            first_user_sent = false;
            new_dxc_cntn = false;
        } else
            first_user_sent = true;
    }

    // send heartbeat if idle too long
    if (cl_type != CT_READONLY && timesUp (&dxc_activity_ms, HBEAT_MS))
        sendHeartbeat();

    // check connection still ok
    if (!dxc_client) {
        dxcLog ("bg lost connection\n");
        incLostConn();
        closeDXCluster();
    }
}

/* insure cluster connection is closed
 */
void closeDXCluster()
{
    // make sure either/both connection is/are closed
    if (dxc_client) {
        dxc_client.stop();
        dxcLog ("disconnect %s\n", dxc_client ? "failed" : "ok");
    }
    if (udp_server) {
        udp_server.stop();
        dxcLog ("WSTJ-X disconnect %s\n", udp_server ?"failed":"ok");
    }

    // reset mem and multi flag
    resetDXMem();
    multi_cntn = false;
}

/* try to connect to the cluster.
 * if success: dxc_client or udp_server is live, perform other prep and return true,
 *       else: both are closed, display mapMsg, return false.
 * use mapMsg to display progress or errors.
 * N.B. inforce MAX_LCN
 */
bool connectDXCluster (void)
{
    // check max lost connection rate
    if (checkLostConnRate()) {
        showDXClusterErr ("Dropped %d times. Check cluster config. Lockout: 30 mins", MAX_LCN);
        return (false);
    }

    // reset list and view
    resetDXMem();

    // get cluster connection info
    const char *dxhost = getDXClusterHost();
    int dxport = getDXClusterPort();

    if (useWSJTX()) {


        // create fresh UDP for WSJT-X
        udp_server.stop();

        // open normal or multicast
        if (isHostMulticast (dxhost)) {

            mapMsg (0, "Connecting to multicat UDP %s:%d", dxhost, dxport);

            // reformat as IPAddress
            unsigned o1, o2, o3, o4;
            if (sscanf (dxhost, "%u.%u.%u.%u", &o1, &o2, &o3, &o4) != 4) {
                showDXClusterErr ("Bad Multicast format: %s", dxhost);
                return (false);
            }
            IPAddress ifIP(0,0,0,0);                        // ignored
            IPAddress mcIP(o1,o2,o3,o4);

            if (udp_server.beginMulticast (ifIP, mcIP, dxport)) {
                dxcLog ("multicast %s:%d ok\n", dxhost, dxport);
                cl_type = CT_UDP;
            } else {
                showDXClusterErr ("multicast %s:%d failed", dxhost, dxport);
                return (false);
            }

            mapMsg (0, "Listening to UDP %s:%d", dxhost, dxport);

        } else {

            mapMsg (0, "Connecting to UDP port %d", dxport);

            if (udp_server.begin(dxport)) {
                dxcLog ("Listening to UDP port %d\n", dxport);
                cl_type = CT_UDP;
            } else {
                showDXClusterErr ("Failed UDP port %d connection", dxport);
                return (false);
            }

            mapMsg (0, "Listening to UDP port %d", dxport);
        }

    } else {

        mapMsg (0, "Connecting to %s:%d", dxhost, dxport);

        // open fresh socket
        dxc_client.stop();
        if (dxc_client.connect(dxhost, dxport)) {

            // valid connection -- keep an eye out for lost connection

            // look alive
            updateClocks(false);
            dxcLog ("connect %s:%d ok\n", dxhost, dxport);

            // assume first question is asking for call.
            // don't try to read with getTCPLine because first line is "login: " without trailing nl
            const char *login = getDXClusterLogin();
            dxcLog ("logging in as %s\n", login);
            dxcSendMsg ("%s\n", login);

            // look for first prompt and watch for clue about type of cluster along the way
            uint16_t bl;
            char buf[200];
            const size_t bufl = sizeof(buf);
            cl_type = CT_UNKNOWN;
            bool rx_gt = false;
            while (cl_type == CT_UNKNOWN && rx_gt == false && getTCPLine (dxc_client, buf, bufl, &bl)) {
                dxcLog ("< %s\n", buf);

                strtolower(buf);
                detectMultiConnection (buf);

                if (queryForQRA (buf)) {
                    dxc_updateDE = true;
                    cl_type = CT_DXSPIDER;
                } else if (strstr (buf, "dx") && strstr (buf, "spider"))
                    cl_type = CT_DXSPIDER;
                else if (strstr (buf, " cc "))
                    cl_type = CT_VE7CC;
                else if (strstr (buf, "ar-cluster"))
                    cl_type = CT_ARCLUSTER;

                // could just wait for timeout but usually ok to stop if find what looks like a prompt
                if (buf[bl-1] == '>')
                    rx_gt = true;
            }

            // if no id string but do see > assume it's M0CKE's fire hose
            if (cl_type == CT_UNKNOWN && rx_gt)
                cl_type = CT_READONLY;

            // what is it?
            if (cl_type == CT_READONLY)
                dxcLog ("Cluster type is unknown but did see \">\" so assuming read-only\n");
            else if (cl_type == CT_DXSPIDER)
                dxcLog ("Cluster is Spider\n");
            else if (cl_type == CT_ARCLUSTER)
                dxcLog ("Cluster is AR\n");
            else if (cl_type == CT_VE7CC)
                dxcLog ("Cluster is CC\n");
            else {
                incLostConn();
                showDXClusterErr ("Unknown cluster type");
                return (false);
            }

            // confirm still ok
            if (!dxc_client) {
                incLostConn();
                if (multi_cntn)
                    showDXClusterErr ("Multiple logins");
                else
                    showDXClusterErr ("Login failed");
                return (false);
            }

            // all ok
            dxcLog ("Cluster connection now operational\n");
            mapMsg (1000, "Connected to %s:%d", dxhost, dxport);

        } else {

            showDXClusterErr ("%s:%d Connection failed", dxhost, dxport);
            return (false);
        }
    }


    // if get here the connection is ready, finish remaining prep

    // get max age
    if (!NVReadUInt8 (NV_DXCAGE, &dxc_age)) {
        dxc_age = dxc_ages[1];
        NVWriteUInt8 (NV_DXCAGE, dxc_age);
    }

    // determine dxc_showbio
    uint8_t bio = 0;
    if (getQRZId() != QRZ_NONE) {
        if (!NVReadUInt8 (NV_DXCBIO, &bio)) {
            bio = 0;
            NVWriteUInt8 (NV_DXCBIO, bio);
        }
    }
    dxc_showbio = (bio != 0);

    // note for background
    new_dxc_cntn = true;

    // fresh heartbeat
    dxc_activity_ms = myNow();

    // ok
    return (true);
}

/* called often while pane is visible, fresh is set when newly so.
 * connect if not already then show list.
 * return whether connection is open.
 * N.B. we never read new spots here, that is done by checkDXCluster()
 */
bool updateDXCluster (const SBox &box, bool fresh)
{
    // insure connected
    if (!isDXClusterConnected()) {
        if (!connectDXCluster()) {                              // shows mapMsg if trouble
            initDXGUI (box);
            showDXCHost (box, RA8875_RED);
            return (false);
        }
        fresh = true;
    }

    // prep fresh box
    if (fresh) {
        initDXGUI (box);
        showDXCHost (box, RA8875_GREEN);
    }

    if (dxc_ss.atNewest()) {
        // rebuild displayed spots list when master list changes or oldest spot ages out
        if (dxc_spots_changed || (n_dxspots > 0 && dxc_spots[0].spotted < myNow() - MAXKEEP_DT)) {
            rebuildDXWatchList();
            dxc_spots_changed = false;
            dxc_ss.drawNewSpotsSymbol (false, false);           // insure off
            scrolledaway_tm = 0;
        }
        ROTHOLD_CLR(PLOT_CH_DXCLUSTER);                         // resume rotation
    } else {
        // show "new spots" symbol if added more spots since scrolled away
        if (showingNewSpot())
            dxc_ss.drawNewSpotsSymbol (true, false);            // show passively
        ROTHOLD_SET(PLOT_CH_DXCLUSTER);                         // disable rotation
    }

    // update list, if only to show aging
    drawAllVisDXCSpots (box);

    // ok
    return (true);

}

/* called often to add any new spots to list IFF connection is already open.
 * N.B. this is not a thread but can be thought of as a "background" function, no GUI.
 * N.B. we never open the cluster connection, that is done by updateDXCluster() but we will close it
 *      if nothing is using it.
 */
void checkDXCluster()
{
    // out fast if no connection
    if (!isDXClusterConnected())
        return;

    // not crazy fast
    static uint32_t prev_check;
    if (!timesUp (&prev_check, BGCHECK_DT))
        return;

    // close if not selected in any pane or by dxpeds
    if (findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE && !dxpedsWatchingCluster()) {
        dxcLog ("closing because no longer in any pane or used by DXPeds\n");
        closeDXCluster();
        return;
    }

    // check for more depending on type
    switch (cl_type) {
    case CT_UDP:
        if (udp_server)
            incomingUDP();
        break;
    case CT_DXSPIDER:   // fallthru
    case CT_ARCLUSTER:  // fallthru
    case CT_VE7CC:      // fallthru
    case CT_READONLY:   // fallthru
        if (dxc_client)
            incomingDXC();
        break;
    case CT_UNKNOWN:
        break;
    }
}

/* determine and engage a dx cluster pane touch.
 * return true if looks like user is interacting with the cluster pane, false if wants to change pane.
 * N.B. we assume s is within box
 */
bool checkDXClusterTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + PANETITLE_H) {

        // somewhere in the title bar

        // scroll up?
        if (dxc_ss.checkScrollUpTouch (s, box)) {
            scrollDXCUp (box);
            return (true);
        }

        // scroll down?
        if (dxc_ss.checkScrollDownTouch (s, box)) {
            scrollDXCDown (box);
            return (true);
        }

        // clear control?
        if (inBox (s, dxcclr_b)) {
            dxcLog ("User erased list of %d spots, %d qualified\n", n_dxspots, dxc_ss.n_data);
            resetDXMem();
            initDXGUI(box);
            showDXCHost (box, RA8875_GREEN);
            return (true);
        }

        // New spots?
        if (dxc_ss.checkNewSpotsTouch (s, box)) {
            if (!dxc_ss.atNewest() && showingNewSpot()) {
                // scroll to newest, let updateDXCluster() do the rest
                dxc_ss.scrollToNewest();
            }
            return (true);                      // claim our even if not showing
        }

        // on hold?
        if (ROTHOLD_TST(PLOT_CH_DXCLUSTER))
            return (true);

        // none of those, so we return indicating user can choose another pane
        return (false);

    }

    // check tapping host to edit watch list
    if (s.y < box.y + LISTING_Y0) {
        runDXClusterMenu (box);
        return (true);
    }

    // everything else below may be a tapped spot
    int vis_row = (s.y - (box.y + LISTING_Y0)) / LISTING_DY;
    int spot_row;
    if (dxc_ss.findDataIndex (vis_row, spot_row)
                        && dxwl_spots[spot_row].tx_call[0] != '\0' && isDXClusterConnected())
        engageDXCRow (dxwl_spots[spot_row]);

    // ours 
    return (true);
}

/* pass back current spots list, and return whether enabled at all.
 * ok to pass back if not displayed because spot list is still intact.
 * N.B. caller should not modify the list
 */
bool getDXClusterSpots (DXSpot **spp, uint8_t *nspotsp)
{
    if (useDXCluster()) {
        *spp = dxc_spots;
        *nspotsp = n_dxspots;
        return (true);
    }

    return (false);
}

/* return whether cluster is currently connected
 */
bool isDXClusterConnected()
{
    return (useDXCluster() && (dxc_client || udp_server));
}

/* draw all qualiying paths and spots on map, as desired
 */
void drawDXClusterSpotsOnMap ()
{
    // skip if we are not running
    if (!isDXClusterConnected())
        return;

    // in use by dxpeds?
    bool dxpeds_using = dxpedsWatchingCluster();

    // in use by us?
    bool dxc_using = findPaneForChoice(PLOT_CH_DXCLUSTER) != PANE_NONE;

    // skip if neither of these are running
    if (!dxpeds_using && !dxc_using)
        return;

    // in use just by DXPeds?
    bool just_dxpeds = dxpeds_using && !dxc_using;

    // must use full list if not using DXC pane
    DXSpot *spots          = just_dxpeds ? dxc_spots : dxwl_spots;
    int n_spots            = just_dxpeds ? n_dxspots : dxc_ss.n_data;
    LabelOnMapDot tx_label = just_dxpeds ? LOMD_JUSTDOT : LOMD_ALL;

    // draw paths then overlay labels
    for (int i = 0; i < n_spots; i++) {
        DXSpot &si = spots[i];
        if (dxc_using || (dxpeds_using && findDXPedsCall (&si))) {
            drawSpotPathOnMap (si);
        }
    }
    for (int i = 0; i < n_spots; i++) {
        DXSpot &si = spots[i];
        if (dxc_using || (dxpeds_using && findDXPedsCall (&si))) {
            drawSpotLabelOnMap (si, LOME_TXEND, tx_label);
            drawSpotLabelOnMap (si, LOME_RXEND, LOMD_JUSTDOT);
        }
    }
}

/* find closest spot and location on either end to given ll, if any.
 */
bool getClosestDXCluster (LatLong &ll, DXSpot *sp, LatLong *llp)
{
    // skip if we are not running
    if (!isDXClusterConnected())
        return (false);

    // in use by dxpeds?
    bool dxpeds_using = dxpedsWatchingCluster();

    // in use by us?
    bool dxc_using = findPaneForChoice(PLOT_CH_DXCLUSTER) != PANE_NONE;

    // skip if neither of these are running
    if (!dxpeds_using && !dxc_using)
        return (false);

    // in use just by DXPeds?
    bool just_dxpeds = dxpeds_using && !dxc_using;

    // must use full list if not using DXC pane, else limit to just peds
    DXSpot *spots  = just_dxpeds ? dxc_spots : dxwl_spots;
    int n_spots    = just_dxpeds ? n_dxspots : dxc_ss.n_data;
    SpotFilter sfp = just_dxpeds ? findDXPedsCall : NULL;

    // find closest spot, if any
    bool found = getClosestSpot (spots, n_spots, sfp, LOME_BOTH, ll, sp, llp);
    if (!found)
        return (false);

    // ok, whatever
    return (found);
}

/* return spot in our pane if under ms
 */
bool getDXCPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll)
{
    // done if ms not showing our pane or not in our box
    PlotPane pp = findPaneChoiceNow (PLOT_CH_DXCLUSTER);
    if (pp == PANE_NONE)
        return (false);
    if (!inBox (ms, plot_b[pp]))
        return (false);

    // create box that will be placed over each listing entry
    SBox listrow_b;
    listrow_b.x = plot_b[pp].x;
    listrow_b.w = plot_b[pp].w;
    listrow_b.h = LISTING_DY;

    // scan listed spots for one located at ms
    uint16_t y0 = plot_b[pp].y + LISTING_Y0;
    int min_i, max_i;
    if (dxc_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            listrow_b.y = y0 + dxc_ss.getDisplayRow(i) * LISTING_DY;
            if (inBox (ms, listrow_b)) {
                // ms is over this spot
                *dxs = dxwl_spots[i];
                *ll = dxs->tx_ll;
                return (true);
            }
        }
    }

    // none
    return (false);
}

/* log the given cluster error message.
 */
void dxcLog (const char *fmt, ...)
{
    // format
    char msg[512];
    va_list ap;
    va_start (ap, fmt);
    (void) vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);

    // we add our own line ending
    chompString (msg);

    // and remove all beeping \a
    for (char *bell = msg; (bell = strchr (bell, '\a')) != NULL; bell++)
        *bell = ' ';

    // print with prefix
    Serial.printf ("DXC: %s\n", msg);
}

/* public interface to request sending DE location when possible (cluster need not be connected now)
 */
void sendDXClusterDELLGrid (void)
{
    dxc_updateDE = true;
}

/* look through _all_ spots for the given tx_call.
 * N.B. using just dxwl_spots won't work because that is never updated if the pane is not active.
 */
const DXSpot *findDXCCall (const char *call)
{
    // only if dxpeds wants it and we are actually running
    if (dxpedsWatchingCluster() && isDXClusterConnected()) {
        for (int i = 0; i < n_dxspots; i++)
            if (strcasecmp (dxc_spots[i].tx_call, call) == 0)
                return (&dxc_spots[i]);
    }

    return (NULL);
}

/* inject a fake DXSpot, typically from RESTful command.
 * return true else short reason why not.
 */
bool injectDXClusterSpot (const char *tx_call, const char *rx_call, const char *kHz, Message &ynot)
{
    DXSpot fake = {};

    quietStrncpy (fake.tx_call, tx_call, sizeof(fake.tx_call));
    quietStrncpy (fake.rx_call, rx_call, sizeof(fake.rx_call));

    char *endptr;
    fake.kHz = strtod (kHz, &endptr);
    if (fake.kHz == 0 || endptr == kHz) {
        ynot.printf ("bogus kHz %s for %s", kHz, tx_call);
        return (false);
    }

    if (findHamBand (fake.kHz) == HAMBAND_NONE) {
        ynot.printf ("no band for %s %g kHz", tx_call, fake.kHz);
        return (false);
    }

    if (!call2LL (tx_call, fake.tx_ll)) {
        ynot.printf ("no LL for %s", tx_call);
        return (false);
    }
    if (!call2LL (rx_call, fake.rx_ll)) {
        ynot.printf ("no LL for %s", rx_call);
        return (false);
    }

    ll2maidenhead (fake.tx_grid, fake.tx_ll);
    ll2maidenhead (fake.rx_grid, fake.rx_ll);

    if (!call2DXCC (tx_call, fake.tx_dxcc)) {
        ynot.printf ("no DXCC for %s", tx_call);
        return (false);
    }
    if (!call2DXCC (rx_call, fake.rx_dxcc)) {
        ynot.printf ("no DXCC for %s", rx_call);
        return (false);
    }

    quietStrncpy (fake.mode, findHamMode (fake.kHz), sizeof(fake.mode));

    fake.spotted = myNow();

    // inject
    logDXSpot ("set_spot", fake);
    addDXClusterSpot (fake, UDPSetsDX());

    // ok
    return (true);
}
