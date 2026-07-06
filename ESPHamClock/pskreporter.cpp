/* manage PSKReporter, WSPR and RBN records and drawing.
 */

#include "HamClock.h"



// global state for webserver
uint8_t psk_mask;                               // one of PSKModeBits
uint32_t psk_bands;                             // bitmask of HamBandSetting
uint16_t psk_maxage_mins;                       // query period, minutes
uint8_t psk_showdist;                           // show max distances, else count
uint8_t psk_showpath;                           // show paths, else not

// query urls
static const char psk_page[] PROGMEM = "/fetchPSKReporter.pl";
static const char wspr_page[] PROGMEM = "/fetchWSPR.pl";
static const char rbn_page[] PROGMEM = "/fetchRBN.pl";

// color config
#define LIVE_COLOR      RGB565(80,80,255)       // title color

// private state
static DXSpot *reports;                         // malloced list of all reports, not just TST_PSKBAND
static int n_reports;                           // count of reports used in psk_bands, might be < n_malloced
static int n_malloced;                          // total n malloced in reports[]
static int spot_maxrpt[HAMBAND_N];              // indices into reports[] for the farthest spot per band
static PSKBandStats bstats[HAMBAND_N];          // band stats

// PSK fetch scheduling with per-endpoint failure backoff. This is deliberately
// independent of the generic nextWiFiRetry() so that a slow or unresponsive PSK
// endpoint backs off exponentially instead of being retried every ~15 s forever.
#define PSK_TIMEOUT_MS  15000                   // socket timeout for the PSK endpoint, ms
#define PSK_RETRY_BASE  30                      // initial retry backoff after a failure, secs
#define PSK_RETRY_MAX   (20*60)                 // cap on the backoff, secs
#define PSK_RETRY_MULT  2                       // backoff multiplier per consecutive failure
static time_t psk_next_fetch;                   // wall time when the next network fetch is allowed
static time_t psk_backoff;                      // current failure backoff, secs (0 when healthy)

// layout
#define SUBHEAD_DYUP 15                         // distance up from bottom to subheading
#define TBLHGAP (PLOTBOX123_W/20)               // table horizontal gap
#define TBLCOLW (43*PLOTBOX123_W/100)           // table column width
#define TBLROWH ((PLOTBOX123_H-LISTING_Y0-SUBHEAD_DYUP)/(HAMBAND_N/2))      // table row height

// handy test and set whether a band is in use
#define SET_PSKBAND(b)  (psk_bands |= (1 << (b)))               // record that band b paths are displayed
#define TST_PSKBAND(b)  ((b) != HAMBAND_NONE && (psk_bands & (1 << (b))) != 0)  // test if band b displayed


/* draw a distance target marker at Raw s with the given fill color.
 */
static void drawDistanceTarget (const SCoord &s, ColorSelection id)
{
    // ignore if no dots
    if (getSpotLabelType() == LBL_NONE)
        return;

    // get radius
    uint16_t dot_r = getRawSpotRadius(id);

    // get colors
    uint16_t fill_color = getMapColor (id);
    uint16_t cross_color = getGoodTextColor (fill_color);

    // raw looks nicer

    tft.fillCircleRaw (s.x, s.y, dot_r, fill_color);
    tft.drawCircleRaw (s.x, s.y, dot_r, cross_color);
    tft.drawLineRaw (s.x-dot_r, s.y, s.x+dot_r, s.y, 1, cross_color);
    tft.drawLineRaw (s.x, s.y-dot_r, s.x, s.y+dot_r, 1, cross_color);
}

/* return whether the given age, in minutes, is allowed.
 */
bool maxPSKageOk (int m)
{
    return (m==15 || m==30 || m==60 || m==360 || m==1440);
}

/* get NV settings related to PSK
 */
void initPSKState()
{
    if (!NVReadUInt8 (NV_PSK_MODEBITS, &psk_mask)) {
        // default PSK of grid
        psk_mask = PSKMB_PSK | PSKMB_OFDE;
        NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    }
    if (!NVReadUInt32 (NV_PSK_BANDS, &psk_bands)) {
        // default all ham_bands
        psk_bands = 0;
        for (int i = 0; i < HAMBAND_N; i++)
            SET_PSKBAND(i);
        NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    }
    if (!NVReadUInt16 (NV_PSK_MAXAGE, &psk_maxage_mins) || !maxPSKageOk(psk_maxage_mins)) {
        // default 30 minutes
        psk_maxage_mins = 30;
        NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    }
    if (!NVReadUInt8 (NV_PSK_SHOWDIST, &psk_showdist)) {
        psk_showdist = 0;
        NVWriteUInt8 (NV_PSK_SHOWDIST, psk_showdist);
    }
    if (!NVReadUInt8 (NV_PSK_SHOWPATH, &psk_showpath)) {
        psk_showpath = 1;                                       // default on
        NVWriteUInt8 (NV_PSK_SHOWPATH, psk_showpath);
    }
}

/* save NV settings related to PSK
 */
void savePSKState()
{
    NVWriteUInt8 (NV_PSK_MODEBITS, psk_mask);
    NVWriteUInt32 (NV_PSK_BANDS, psk_bands);
    NVWriteUInt16 (NV_PSK_MAXAGE, psk_maxage_mins);
    NVWriteUInt8 (NV_PSK_SHOWDIST, psk_showdist);
    NVWriteUInt8 (NV_PSK_SHOWPATH, psk_showpath);
}

/* draw a target at the farthest spot in each active band as needed.
 */
void drawFarthestPSKSpots ()
{
    // proceed unless not wanted or not in use`
    if (getSpotLabelType() == LBL_NONE || findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    // draw each that are enabled
    for (int i = 0; i < HAMBAND_N; i++) {
        PSKBandStats &pbs = bstats[i];
        if (pbs.maxkm > 0 && TST_PSKBAND(i)) {
            int tw = getRawSpotRadius (findColSel((HamBandSetting)i));
            SCoord s;
            ll2s (pbs.maxll, s, tw);
            if (overMap(s)) {
                ll2sRaw (pbs.maxll, s, tw);
                drawDistanceTarget (s, findColSel((HamBandSetting)i));
            }
        }
    }
}

/* draw the PSK pane in the given box
 */
static void drawPSKPane (const SBox &box)
{
    // clear
    prepPlotBox (box);

    // handy
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;

    // title
    static const char *title = "Live Spots";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t tw = getTextWidth(title);
    tft.setTextColor (LIVE_COLOR);
    tft.setCursor (box.x + (box.w - tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // set name to call or 4x grid
    char name[20];
    if (use_call) {
        strcpy (name, getCallsign());
    } else {
        char de_maid[MAID_CHARLEN];
        getNVMaidenhead (NV_DE_GRID, de_maid);
        snprintf (name, sizeof(name), "%.4s", de_maid);
    }

    // show how and when
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    char where_how[100];
    snprintf (where_how, sizeof(where_how), "%s %s - %s %d %s",
                of_de ? "of" : "by", name,
                (ispsk ? "PSK" : (iswspr ? "WSPR" : "RBN")),
                psk_maxage_mins < 60 ? psk_maxage_mins : psk_maxage_mins/60,
                psk_maxage_mins < 60 ? "mins" : (psk_maxage_mins == 60 ? "hour" : "hrs"));
    uint16_t whw = getTextWidth(where_how);
    tft.setCursor (box.x + (box.w-whw)/2, box.y + SUBTITLE_Y0);
    tft.print (where_how);

    // table
    for (int i = 0; i < HAMBAND_N; i++) {
        int row = i % (HAMBAND_N/2);
        int col = i / (HAMBAND_N/2);
        uint16_t x = box.x + TBLHGAP + col*(TBLCOLW+TBLHGAP);
        uint16_t y = box.y + LISTING_Y0 + row*TBLROWH;
        char report[30];
        if (psk_showdist) {
            float d = bstats[i].maxkm;
            if (!showDistKm())
                d *= MI_PER_KM;
            snprintf (report, sizeof(report), "%3sm %5.0f", findBandName((HamBandSetting)i), d);
        } else
            snprintf (report, sizeof(report), "%3sm %5d", findBandName((HamBandSetting)i), bstats[i].count);
        if (TST_PSKBAND(i)) {
            uint16_t map_col = getMapColor(findColSel((HamBandSetting)i));
            uint16_t txt_col = getGoodTextColor(map_col);
            tft.fillRect (x, y-LISTING_OS+1, TBLCOLW, TBLROWH-3, map_col);      // leave black below
            tft.setTextColor (txt_col);
            tft.setCursor (x+2, y);
            tft.print (report);
        } else {
            // disabled, always show but diminished
            tft.fillRect (x, y-LISTING_OS+1, TBLCOLW, TBLROWH-3, RA8875_BLACK);
            tft.setTextColor (GRAY);
            tft.setCursor (x+2, y);
            tft.print (report);
        }
    }

    // caption
    const char *label = psk_showdist ? (showDistKm() ? "Max distance (km)" : "Max distance (mi)")
                                     : "Counts";
    uint16_t lw = getTextWidth (label);
    uint16_t lx = box.x + (box.w-lw)/2;
    uint16_t ly = box.y + box.h - SUBHEAD_DYUP;
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (lx, ly);
    tft.print (label);
}

/* retrieve spots into reports[] according to current settings.
 * return whether io ok.
 */
static bool retrievePSK (void)
{
    // get fresh
    WiFiClient psk_client;
    psk_client.setTimeout (PSK_TIMEOUT_MS);             // bound the wait for a slow endpoint
    bool ok = false;

    // query type
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
    bool isrbn = (psk_mask & PSKMB_SRCMASK) == PSKMB_RBN;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;

    // handy 4x DE maid if needed
    char de_maid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_maid);
    de_maid[4] = '\0';

    // build query
    char query[100];
    if (ispsk)
        strcpy_P (query, psk_page);
    else if (iswspr)
        strcpy_P (query, wspr_page);
    else
        strcpy_P (query, rbn_page);
    int qlen = strlen (query);
    snprintf (query+qlen, sizeof(query)-qlen, "?%s%s=%s&maxage=%d",
                                        of_de ? "of" : "by",
                                        use_call ? "call" : "grid",
                                        use_call ? getCallsign() : de_maid,
                                        psk_maxage_mins*60 /* wants seconds */);
    Serial.printf ("PSK: query: %s\n", query);

    // fetch and fill reports[]
    if (psk_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (psk_client, backend_host, query);

        // skip header
        if (!httpSkipHeader (psk_client)) {
            Serial.print ("PSK: no header\n");
            goto out;
        }

        // N.B. ok is set only after we confirm the whole body arrived; see below.
        // A malformed line below still goes to out with ok == false (a failure),
        // which lets the backoff engage instead of caching a bad/partial response.

        // reset lists
        n_reports = 0;
        for (int i = 0; i < HAMBAND_N; i++)
            bstats[i] = {};

        // read lines -- anything unexpected is considered an error message
        char line[100];
        while (getTCPLine (psk_client, line, sizeof(line), NULL)) {

            // Serial.printf ("PSK: fetched %s\n", line);

            // parse.
            // N.B. match sscanf sizes with array sizes
            // N.B. first grid/call pair is always TX, second always RX; which is DE depends on PSKMB_OFDE
            DXSpot new_sp = {};
            long posting_temp;
            long Hz_temp;
            char txcall_temp[64], rxcall_temp[64]; // Large enough to hold long calls prior to trim
            int count = sscanf(line, "%ld,%6[^,],%63[^,],%6[^,],%63[^,],%7[^,],%ld,%f",
                            &posting_temp,
                            new_sp.tx_grid,
                            txcall_temp,
                            new_sp.rx_grid,
                            rxcall_temp,
                            new_sp.mode,
                            &Hz_temp,
                            &new_sp.snr);
            if (count == 8) {
                strncpy(new_sp.tx_call, txcall_temp, 10);
                new_sp.tx_call[10] = '\0';
                strncpy(new_sp.rx_call, rxcall_temp, 10);
                new_sp.rx_call[10] = '\0';
            } else {
                Serial.printf ("PSK: %s\n", line);
                goto out;
            }
            new_sp.spotted = posting_temp;
            new_sp.kHz = Hz_temp * 1e-3F;

            // RBN does not provide tx_grid but it must be us. N.B. this will be blank from rbndaemon
            if (isrbn)
                strcpy (new_sp.tx_grid, de_maid);

            // convert grids to ll
            if (!maidenhead2ll (new_sp.tx_ll, new_sp.tx_grid)) {
                Serial.printf ("PSK: RX grid? %s\n", line);
                continue;
            }
            if (!maidenhead2ll (new_sp.rx_ll, new_sp.rx_grid)) {
                Serial.printf ("PSK: RX grid? %s\n", line);
                continue;
            }

            // check for unknown or unsupported band
            const HamBandSetting band = findHamBand (new_sp.kHz);
            if (band == HAMBAND_NONE) {
                Serial.printf ("PSK: band? %s\n", line);
                continue;
            }

            // DXCC
            if (!call2DXCC (new_sp.tx_call, new_sp.tx_dxcc)) {
                Serial.printf ("PSK: no DXCC for %s\n", new_sp.tx_call);
                continue;
            }
            if (!call2DXCC (new_sp.rx_call, new_sp.rx_dxcc)) {
                Serial.printf ("PSK: no DXCC for %s\n", new_sp.rx_call);
                continue;
            }

            // update stats for this band
            PSKBandStats &pbs = bstats[band];

            // update count of this band
            pbs.count++;

            // dither ll for unique selection
            ditherLL (new_sp.tx_ll);
            ditherLL (new_sp.rx_ll);

            // finally! save new report, grow array if out of room
            if ( !(n_reports < n_malloced) ) {
                reports = (DXSpot *) realloc (reports, (n_malloced += 100) * sizeof(DXSpot));
                if (!reports)
                    fatalError ("Live Spots: no mem %d", n_malloced);
            }
            reports[n_reports] = new_sp;         // N.B. do not inc yet, used last

            // check each end for farthest from DE
            float tx_dist, rx_dist, bearing;        
            propDEPath (false, new_sp.tx_ll, &tx_dist, &bearing);
            propDEPath (false, new_sp.rx_ll, &rx_dist, &bearing);
            tx_dist *= KM_PER_MI * ERAD_M;                         // convert core angle to surface km
            rx_dist *= KM_PER_MI * ERAD_M;                         // convert core angle to surface km
            bool tx_gt_rx = (tx_dist > rx_dist);
            float max_dist = tx_gt_rx ? tx_dist : rx_dist;
            if (max_dist > pbs.maxkm) {

                // update pbs for this band with farther spot
                LatLong max_ll = tx_gt_rx ? new_sp.tx_ll : new_sp.rx_ll;
                const char *call = tx_gt_rx ? new_sp.tx_call : new_sp.rx_call;
                pbs.maxkm = max_dist;
                pbs.maxll = max_ll;
                if (getSpotLabelType() == LBL_PREFIX)
                    findCallPrefix (call, pbs.maxcall);
                else
                    strcpy (pbs.maxcall, call);

                // newest spot is now farthest for this band
                spot_maxrpt[band] = n_reports;
            }

            // ok, another report
            n_reports++;
        }

        // We asked for "Connection: close", so a complete response ends with the
        // server closing the socket (connected() now false). If it is still open
        // the read loop above ended on a timeout -- the endpoint didn't respond in
        // time -- so treat that as a failure rather than caching a partial result.
        // N.B. an empty-but-complete response (0 reports) still counts as success.
        ok = !psk_client.connected();
        if (!ok)
            Serial.print ("PSK: response stalled, endpoint too slow\n");

    } else
        Serial.print ("PSK: Spots connection failed\n");

out:
    // reset counts if trouble
    if (!ok) {
        n_reports = 0;
        for (int i = 0; i < HAMBAND_N; i++) {
            bstats[i].count = -1;
            bstats[i].maxkm = -1;
        }
    }

    // finish up
    psk_client.stop();
    Serial.printf ("PSK: found %d %s reports %s %s\n",
                        n_reports,
                        (ispsk ? "PSK" : (iswspr ? "WSPR" : "RBN")),
                        of_de ? "of" : "by",
                        use_call ? getCallsign() : de_maid);

    // already logged any problems
    return (ok);
}

/* call after a successful fetch: clear any backoff and resume the normal cadence.
 */
static void resetPSKRetry (void)
{
    if (psk_backoff)
        Serial.printf ("PSK: recovered, backoff cleared\n");
    psk_backoff = 0;
    psk_next_fetch = myNow() + PSK_INTERVAL;
}

/* call after a failed fetch: schedule the next attempt using exponential backoff
 * with +/-25% jitter so a fleet of clients doesn't resynchronize on the backend.
 */
static void schedulePSKRetry (void)
{
    if (psk_backoff < PSK_RETRY_BASE)
        psk_backoff = PSK_RETRY_BASE;

    long jitter = psk_backoff / 4;
    long delay  = psk_backoff + (random(2*jitter + 1) - jitter);
    if (delay < PSK_RETRY_BASE)
        delay = PSK_RETRY_BASE;
    psk_next_fetch = myNow() + delay;

    Serial.printf ("PSK: fetch failed, backing off %ld s (cur %ld, max %d)\n",
                        delay, (long)psk_backoff, PSK_RETRY_MAX);

    psk_backoff *= PSK_RETRY_MULT;
    if (psk_backoff > PSK_RETRY_MAX)
        psk_backoff = PSK_RETRY_MAX;
}

/* query PSK reporter etc for new reports, draw results and return whether all ok
 */
bool updatePSKReporter (const SBox &box, bool force)
{
    // settings used for the reports[] we currently hold
    static uint8_t my_psk_mask;                         // setting used for reports[]
    static uint32_t my_psk_bands;                       // setting used for reports[]
    static uint16_t my_psk_maxage_mins;                 // setting used for reports[]
    static bool last_ok;                                // result of last real fetch

    // A change of query settings invalidates our cached reports[] and means the
    // user is actively asking for new data, so fetch now and cancel any backoff.
    // N.B. keyed on settings, NOT on force: force can stay set across failures, so
    // resetting on force would let it defeat the backoff and hammer the backend.
    if (my_psk_mask != psk_mask || my_psk_maxage_mins != psk_maxage_mins
                                || my_psk_bands != psk_bands) {
        my_psk_mask = psk_mask;
        my_psk_maxage_mins = psk_maxage_mins;
        my_psk_bands = psk_bands;
        psk_backoff = 0;                                // healthy again
        psk_next_fetch = 0;                             // allow a fetch right now
    }

    // Decide whether to fetch now. An active failure backoff (psk_backoff > 0) is
    // ALWAYS honored so a slow or down endpoint can't be hammered -- in particular
    // force does not override it, since force can stay set across the dispatcher's
    // ~15 s retries. When healthy we still honor the normal PSK_INTERVAL unless the
    // caller forced a refresh (e.g. the pane was just selected). Otherwise just
    // redraw what we have and report the last real result.
    if (myNow() < psk_next_fetch && (psk_backoff > 0 || !force)) {
        drawPSKPane (box);
        return (last_ok);
    }

    // time to fetch fresh
    last_ok = retrievePSK();

    // reschedule: normal cadence on success, exponential backoff on failure
    if (last_ok)
        resetPSKRetry();
    else
        schedulePSKRetry();

    // display whatever we got regardless
    drawPSKPane (box);

    // reply
    return (last_ok);
}

/* check for tap at s known to be within a PLOT_CH_PSK box.
 * return whether it was ours.
 */
bool checkPSKTouch (const SCoord &s, const SBox &box)
{
    // done if tap title
    if (s.y < box.y + PANETITLE_H)
        return (false);

    // handy menu entry indices
    // N.B. must be in column-major order
    // N.B. keep in sync!
    enum {
        _M_RBN,  _M_SPOT, _M_WHAT, _M_SHOW, _M_PATH, _M_AGE, _M_1HR, _M_160, _M_80, _M_60, _M_40,
        _M_PSK,  _M_OFDE, _M_CALL, _M_DIST, _M_PON,  _M_15M, _M_6HR, _M_30,  _M_20, _M_17, _M_15,
        _M_WSPR, _M_BYDE, _M_GRID, _M_CNT,  _M_POFF, _M_30M, _M_24H, _M_12,  _M_10, _M_6,  _M_2,
        _M_N
    };

    // handy current state
    bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
    bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
    bool isrbn = (psk_mask & PSKMB_SRCMASK) == PSKMB_RBN;
    bool use_call = (psk_mask & PSKMB_CALL) != 0;
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;
    bool show_dist = psk_showdist != 0;
    bool show_path = psk_showpath != 0;

    // menu
    #define PRI_INDENT 2
    #define SEC_INDENT 12
    #define MI_N (HAMBAND_N + 21)                                // ham_bands + controls
    MenuItem mitems[MI_N];

    if (MI_N != _M_N)
        fatalError ("busted live spots menu size: %d != %d", MI_N, _M_N);

    // runMenu() expects column-major entries

    mitems[_M_RBN]  = {MENU_1OFN,  isrbn,    1, PRI_INDENT, "RBN", 0};
    mitems[_M_SPOT] = {MENU_LABEL, false,    0, PRI_INDENT, "Spot:", 0};
    mitems[_M_WHAT] = {MENU_LABEL, false,    0, PRI_INDENT, "What:", 0};
    mitems[_M_SHOW] = {MENU_LABEL, false,    0, PRI_INDENT, "Show:", 0};
    mitems[_M_PATH] = {MENU_LABEL, false,    0, PRI_INDENT, "Path:", 0};
    mitems[_M_AGE]  = {MENU_LABEL, false,    0, PRI_INDENT, "Age:", 0};
    mitems[_M_1HR]  = {MENU_1OFN,  false,    6, 5, "1 hr", 0};
    mitems[_M_160]  = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_160M), 4, SEC_INDENT, findBandName(HAMBAND_160M), 0};
    mitems[_M_80]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_80M),  4, SEC_INDENT, findBandName(HAMBAND_80M), 0};
    mitems[_M_60]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_60M),  4, SEC_INDENT, findBandName(HAMBAND_60M), 0};
    mitems[_M_40]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_40M),  4, SEC_INDENT, findBandName(HAMBAND_40M), 0};

    mitems[_M_PSK]  = {MENU_1OFN, ispsk,     1, PRI_INDENT, "PSK", 0};
    mitems[_M_OFDE] = {MENU_1OFN, of_de,     2, PRI_INDENT, "of DE", 0};
    mitems[_M_CALL] = {MENU_1OFN, use_call,  3, PRI_INDENT, "Call", 0};
    mitems[_M_DIST] = {MENU_1OFN, show_dist, 7, PRI_INDENT, "MaxDst", 0};
    mitems[_M_PON]  = {MENU_1OFN, show_path, 8, PRI_INDENT, "On", 0};
    mitems[_M_15M]  = {MENU_1OFN, false,     6, PRI_INDENT, "15 min", 0};
    mitems[_M_6HR]  = {MENU_1OFN, false,     6, PRI_INDENT, "6 hrs", 0};
    mitems[_M_30]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_30M),  4, SEC_INDENT, findBandName(HAMBAND_30M), 0};
    mitems[_M_20]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_20M),  4, SEC_INDENT, findBandName(HAMBAND_20M), 0};
    mitems[_M_17]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_17M),  4, SEC_INDENT, findBandName(HAMBAND_17M), 0};
    mitems[_M_15]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_15M),  4, SEC_INDENT, findBandName(HAMBAND_15M), 0};

    mitems[_M_WSPR] = {MENU_1OFN, iswspr,    1, PRI_INDENT, "WSPR", 0};
    mitems[_M_BYDE] = {MENU_1OFN, !of_de,    2, PRI_INDENT, "by DE", 0};
    mitems[_M_GRID] = {MENU_1OFN, !use_call, 3, PRI_INDENT, "Grid", 0};
    mitems[_M_CNT]  = {MENU_1OFN, !show_dist,7, PRI_INDENT, "Count", 0};
    mitems[_M_POFF] = {MENU_1OFN, !show_path,8, PRI_INDENT, "Off", 0};
    mitems[_M_30M]  = {MENU_1OFN, false,     6, PRI_INDENT, "30 min", 0};
    mitems[_M_24H]  = {MENU_1OFN, false,     6, PRI_INDENT, "24 hrs", 0};
    mitems[_M_12]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_12M),  4, SEC_INDENT, findBandName(HAMBAND_12M), 0};
    mitems[_M_10]   = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_10M),  4, SEC_INDENT, findBandName(HAMBAND_10M), 0};
    mitems[_M_6]    = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_6M),   4, SEC_INDENT, findBandName(HAMBAND_6M), 0};
    mitems[_M_2]    = {MENU_AL1OFN, TST_PSKBAND(HAMBAND_2M),   4, SEC_INDENT, findBandName(HAMBAND_2M), 0};

    // set age
    switch (psk_maxage_mins) {
    case 15:   mitems[_M_15M].set = true; break;
    case 30:   mitems[_M_30M].set = true; break;
    case 60:   mitems[_M_1HR].set  = true; break;
    case 360:  mitems[_M_6HR].set = true; break;
    case 1440: mitems[_M_24H].set = true; break;
    default:   fatalError ("Bad psk_maxage_mins: %d", psk_maxage_mins);
    }

    // create a box for the menu
    SBox menu_b;
    menu_b.x = box.x+9;
    menu_b.y = box.y + 5;
    menu_b.w = 0;               // shrink to fit

    // run
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 3, MI_N, mitems};
    if (runMenu (menu)) {

        // handy
        bool psk_set  = mitems[_M_PSK].set;
        bool wspr_set = mitems[_M_WSPR].set;
        bool rbn_set  = mitems[_M_RBN].set;
        bool ofDE_set = mitems[_M_OFDE].set;
        bool call_set = mitems[_M_CALL].set;

        // RBN only works with ofcall
        if (rbn_set && (!ofDE_set || !call_set)) {

            // show error briefly then restore existing settings
            plotMessage (box, RA8875_RED, "RBN requires \"of DE\" and \"Call\"");
            wdDelay (5000);
            drawPSKPane(box);

        } else {

            // set new mode mask;
            psk_mask = psk_set ? PSKMB_PSK : (wspr_set ? PSKMB_WSPR : PSKMB_RBN);
            if (ofDE_set)
                psk_mask |= PSKMB_OFDE;
            if (call_set)
                psk_mask |= PSKMB_CALL;

            // set new ham_bands
            psk_bands = 0;
            if (mitems[_M_160].set) SET_PSKBAND(HAMBAND_160M);
            if (mitems[_M_80].set)  SET_PSKBAND(HAMBAND_80M);
            if (mitems[_M_60].set)  SET_PSKBAND(HAMBAND_60M);
            if (mitems[_M_40].set)  SET_PSKBAND(HAMBAND_40M);
            if (mitems[_M_30].set)  SET_PSKBAND(HAMBAND_30M);
            if (mitems[_M_20].set)  SET_PSKBAND(HAMBAND_20M);
            if (mitems[_M_17].set)  SET_PSKBAND(HAMBAND_17M);
            if (mitems[_M_15].set)  SET_PSKBAND(HAMBAND_15M);
            if (mitems[_M_12].set)  SET_PSKBAND(HAMBAND_12M);
            if (mitems[_M_10].set)  SET_PSKBAND(HAMBAND_10M);
            if (mitems[_M_6].set)   SET_PSKBAND(HAMBAND_6M);
            if (mitems[_M_2].set)   SET_PSKBAND(HAMBAND_2M);

            // get new age
            if (mitems[_M_15M].set)
                psk_maxage_mins = 15;
            else if (mitems[_M_30M].set)
                psk_maxage_mins = 30;
            else if (mitems[_M_1HR].set)
                psk_maxage_mins = 60;
            else if (mitems[_M_6HR].set)
                psk_maxage_mins = 360;
            else if (mitems[_M_24H].set)
                psk_maxage_mins = 1440;
            else
                fatalError ("PSK: No menu age");

            // get how to show
            psk_showdist = mitems[_M_DIST].set;

            // get whether to show paths
            psk_showpath = mitems[_M_PON].set;

            // persist
            savePSKState();

            // refresh with new criteria
            updatePSKReporter (box, true);
        }
    }

    // ours alright
    return (true);
}

/* return current stats, if active
 */
bool getPSKBandStats (PSKBandStats stats[HAMBAND_N], const char *names[HAMBAND_N])
{
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return (false);

    // copy but zero out entries with 0 count
    memcpy (stats, bstats, sizeof(PSKBandStats) * HAMBAND_N);
    for (int i = 0; i < HAMBAND_N; i++) {
        if (bstats[i].count == 0) {
            stats[i].maxkm = 0;
            stats[i].maxll = {};
        }
        names[i] = findBandName((HamBandSetting)i);
    }

    return (true);
}



/* draw the current set of spot paths in reports[] if enabled
 */
void drawPSKPaths ()
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return;

    // which end to mark
    LabelOnMapEnd lom = (psk_mask & PSKMB_OFDE) ? LOME_RXEND : LOME_TXEND;

    if (psk_showdist) {

        // just show the longest path in each band
        for (int i = 0; i < HAMBAND_N; i++) {
            if (bstats[i].maxkm > 0 && TST_PSKBAND(i)) {
                if (psk_showpath)
                    drawSpotPathOnMap (reports[spot_maxrpt[i]]);
                drawSpotLabelOnMap (reports[spot_maxrpt[i]], lom, LOMD_ALL);
            }
        }

    } else {

        // show all paths first
        if (psk_showpath) {
            for (int i = 0; i < n_reports; i++) {
                DXSpot &s = reports[i];
                if (TST_PSKBAND(findHamBand(s.kHz)))
                    drawSpotPathOnMap (s);
            }
        }

        // then label all without text
        for (int i = 0; i < n_reports; i++) {
            // N.B. we know band in all reports[] are ok
            DXSpot &s = reports[i];
            if (TST_PSKBAND(findHamBand(s.kHz)))




                drawSpotLabelOnMap (s, LOME_BOTH, LOMD_JUSTDOT);
        }

        // then finally label only the farthest with text
        for (int i = 0; i < HAMBAND_N; i++)
            if (bstats[i].maxkm > 0 && TST_PSKBAND(i))
                drawSpotLabelOnMap (reports[spot_maxrpt[i]], lom, LOMD_ALL);
    }
}

/* report spot closest to ll and which end to mark on map, if any within MAX_CSR_DIST.
 */
bool getClosestPSK (LatLong &ll, DXSpot *sp, LatLong *mark_ll)
{
    // ignore if not in any rotation set
    if (findPaneForChoice(PLOT_CH_PSK) == PANE_NONE)
        return (false);

    // which way?
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;

    if (psk_showdist) {

        // just check bstats if only showing farthest spots

        float min_d = 0;
        int min_i = -1;
        for (int i = 0; i < HAMBAND_N; i++) {
            if (TST_PSKBAND(i)) {
                float d = ll.GSD(bstats[i].maxll);
                if (min_i < 0 || d < min_d) {
                    min_d = d;
                    min_i = i;
                }
            }
        }

        if (min_i >= 0 && min_d*ERAD_M < MAX_CSR_DIST) {
            *sp = reports[spot_maxrpt[min_i]];
            *mark_ll = of_de ? sp->rx_ll : sp->tx_ll;
            return (true);
        }
    
    } else {

        // check all spots in displayed ham_bands.
        // N.B. can't use getClosestSpot() because of TST_PSKBAND

        float min_d = 0;
        int min_i = -1;
        for (int i = 0; i < n_reports; i++) {
            DXSpot &s = reports[i];
            if (TST_PSKBAND(findHamBand(s.kHz))) {
                float d = ll.GSD(s.rx_ll);
                if (min_i < 0 || d < min_d) {
                    min_d = d;
                    min_i = i;
                }
                d = ll.GSD(s.tx_ll);
                if (min_i < 0 || d < min_d) {
                    min_d = d;
                    min_i = i;
                }
            }
        }

        if (min_i >= 0 && min_d*ERAD_M < MAX_CSR_DIST) {
            *sp = reports[min_i];
            *mark_ll = of_de ? sp->rx_ll : sp->tx_ll;
            return (true);
        }
    }

    // none
    return (false);
}

/* if ms is over one of the bands in our pane report its info and where to mark on map.
 * return whether ms is really over any of our bands.
 */
bool getMaxDistPSK (const SCoord &ms, DXSpot *sp, LatLong *mark_ll)
{
    // ignore if not currently up
    PlotPane pp = findPaneChoiceNow(PLOT_CH_PSK);
    if (pp == PANE_NONE)
        return (false);

    // which way?
    bool of_de = (psk_mask & PSKMB_OFDE) != 0;

    // find band where ms is located
    const SBox &box = plot_b[pp];
    SBox band_box;
    band_box.w = TBLCOLW;
    band_box.h = TBLROWH;
    for (int i = 0; i < HAMBAND_N; i++) {
        int row = i % (HAMBAND_N/2);
        int col = i / (HAMBAND_N/2);
        band_box.x = box.x + TBLHGAP + col*(TBLCOLW+TBLHGAP);
        band_box.y = box.y + LISTING_Y0 + row*TBLROWH;
        if (TST_PSKBAND(i) && inBox (ms, band_box) && bstats[i].maxkm > 0) {
            // report farthest spot on this band
            *sp = reports[spot_maxrpt[i]];
            *mark_ll = of_de ? sp->rx_ll : sp->tx_ll;
            return (true);
        }
    }

    return (false);
}


/* return PSKReports list
 */
void getPSKSpots (const DXSpot* &rp, int &n_rep)
{
    rp = reports;
    n_rep = n_reports;
}

/* return drawing color for the given frequency, or black if not found.
 */
uint16_t getBandColor (float kHz)
{
    HamBandSetting b = findHamBand (kHz);
    return (b != HAMBAND_NONE ? getMapColor(findColSel(b)) : RA8875_BLACK);
}

/* return whether the path for the given freq should be drawn dashed
 */
bool getBandPathDashed (float kHz)
{
    HamBandSetting b = findHamBand (kHz);
    return (b != HAMBAND_NONE ? getPathDashed(findColSel(b)) : false);
}

/* return width to draw a map path for the given frequency.
 * returns 0 if band is turned off.
 */
int getRawBandPathWidth (float kHz)
{
    HamBandSetting b = findHamBand (kHz);
    return (b != HAMBAND_NONE ? getRawPathWidth(findColSel(b)) : false);
}

/* return width to draw a map spot for the given frequency.
 * always returns the size even if the path color is turned off.
 */
int getRawBandSpotRadius (float kHz)
{
    HamBandSetting b = findHamBand (kHz);
    return (b != HAMBAND_NONE ? getRawSpotRadius (findColSel(b)) : RAWWIDEPATHSZ);
}
