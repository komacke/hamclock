/* support for the ADIF file pane and mapping.
 * based on https://www.adif.org/314/ADIF_314.htm
 * pane layout and operations are similar to dxcluster.
 */

#include "HamClock.h"


#define ADIF_COLOR      RGB565 (255,228,225)            // misty rose


// tables of sort enum ids and corresponding qsort compare functions -- use X trick to insure in sync

#define ADIF_SORTS              \
    X(ADS_AGE,  qsDXCSpotted)   \
    X(ADS_DIST, qsDXCDist)      \
    X(ADS_CALL, qsDXCTXCall)    \
    X(ADS_BAND, qsDXCFreq)

#define X(a,b) a,                                       // expands X to each enum value and comma
typedef enum {
    ADIF_SORTS
    ADIF_SORTS_N
} ADIFSorts;                                            // enum of each sort function
#undef X

#define X(a,b) b,                                       // expands X to each function pointer and comma
static PQSF adif_pqsf[ADIF_SORTS_N] = {                 // sort functions, in order of ADIFSorts
    ADIF_SORTS
};
#undef X



// class to organize testing whether a file has changed
class FileSignature
{
    public:

        // init
        FileSignature(void) {
            reset();
        }

        // reset so any subsequent file will appear to have changed
        void reset() {
            mtime = 0;
            len = 0;
        }

        // return whether the given file appears to be different than the current state.
        // if so, save the new file info as the current state.
        bool fileChanged (const char *fn) {

            // get info for fn
            struct stat s;
            bool changed = true;
            if (::stat (fn, &s) == 0) {
                time_t fn_mtime = s.st_mtime;
                long fn_len = s.st_size;
                changed = fn_mtime != mtime || fn_len != len;
                mtime = fn_mtime;
                len = fn_len;
            } else {
                Serial.printf ("ADIF: stat(%s): %s\n", fn, strerror(errno));
                reset();
            }
            return (changed);
        }

    private:

        // current state
        time_t mtime;                                   // modification time
        long len;                                       // file length

};


// state
static ADIFSorts adif_sort;                             // current sort code index into adif_pqsf
static DXSpot *adif_spots;                              // malloced
static ScrollState adif_ss;                             // scroll controller, n_data is count
static bool showing_set_adif;                           // set when not checking for local file
static bool newfile_pending;                            // set when find new file while scrolled away
static FileSignature fsig;                              // used to decide whether to read file again
static int n_adif_bad;                                  // n bad spots found, global to maintain context


/* save sort and file name
 */
static void saveADIFSettings (const char *fn)
{
    NVWriteUInt8 (NV_ADIFSORT, (uint8_t) adif_sort);
    setADIFFilename (fn);                               // changes persistent value managed by setup
}

/* load sort
 */
static void loadADIFSettings (void)
{
    uint8_t sort;
    if (!NVReadUInt8 (NV_ADIFSORT, &sort))
        adif_sort = ADS_AGE;                            // default sort by age
    else
        adif_sort = (ADIFSorts) sort;
}

/* draw all currently visible spots then update scroll markers
 */
static void drawAllVisADIFSpots (const SBox &box)
{
    drawVisibleSpots (WLID_ADIF, adif_spots, adif_ss, box, ADIF_COLOR);
}

/* shift the visible list up, if appropriate
 */
static void scrollADIFUp (const SBox &box)
{
    if (adif_ss.okToScrollUp ()) {
        adif_ss.scrollUp ();
        drawAllVisADIFSpots (box);
    }
}

/* shift the visible list down, if appropriate
 */
static void scrollADIFDown (const SBox &box)
{
    if (adif_ss.okToScrollDown()) {
        adif_ss.scrollDown ();
        drawAllVisADIFSpots (box);
    }
}

static void resetADIFMem(void)
{
    free (adif_spots);
    adif_spots = NULL;
    adif_ss.n_data = 0;
}

/* draw complete ADIF pane in the given box.
 * also indicate if any were removed from the list based on n_adif_bad.
 * if only want to show new spots, such as when scrolling, just call drawAllVisADIFSpots()
 */
void drawADIFPane (const SBox &box, const char *filename)
{
    // prep
    prepPlotBox(box);

    // title
    const char *title = "ADIF";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(ADIF_COLOR);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // show only the base filename
    const char *fn_basename = strrchr (filename, '/');
    if (fn_basename)
        fn_basename += 1;                       // skip past /
    else
        fn_basename = filename;                 // no change

    // show fn with counts if they fit 
    int max_chw = (box.w-4)/6;                  // font is 6 pixels wide
    char info[200];
    int info_l;
    if (n_adif_bad > 0)
        info_l = snprintf (info, sizeof(info), "%s %d-%d", fn_basename, adif_ss.n_data+n_adif_bad,n_adif_bad);
    else
        info_l = snprintf (info, sizeof(info), "%s %d", fn_basename, adif_ss.n_data);
    if (info_l > max_chw)
        info_l = snprintf (info, max_chw, "%s", fn_basename);

    // center
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t iw = getTextWidth(info);
    tft.setCursor (box.x + (box.w-iw)/2, box.y + SUBTITLE_Y0);
    tft.print (info);

    // draw spots
    drawAllVisADIFSpots (box);
}

/* test whether the given file name is suitable for ADIF, else return brief reason.
 */
bool checkADIFFilename (const char *fn, Message &ynot)
{
    // at all?
    if (!fn) {
        ynot.set ("no file");
        return (false);
    }
    // edge tests
    if (strchr (fn, ' ')) {
        ynot.set("no blanks");
        return (false);
    }
    size_t fn_l = strlen (fn);
    if (fn_l == 0) {
        ynot.set("empty");
        return (false);
    }
    if (fn_l > NV_ADIFFN_LEN-1) {
        ynot.set("too long");
        return (false);
    }

    // worth trying for real
    char fn_exp[1000];
    if (!expandENV (fn, fn_exp, sizeof(fn_exp))) {
        ynot.set("env lookup failed");
        Serial.printf ("ADIF: %s env expand failed\n", fn);
        return (false);
    }

    FILE *fp = fopen (fn_exp, "r");
    if (fp) {
        fclose (fp);
        return (true);
    } else {
        ynot.set("open failed");
        Serial.printf ("ADIF: %s %s\n", fn_exp, strerror(errno));
        return (false);
    }
}

/* callback for testing adif file name in the given MenuText->text
 */
static bool testADIFFilename (struct _menu_text *tfp, Message &ynot)
{
    return (checkADIFFilename (tfp->text, ynot));
}

/* run the ADIF menu
 */
static void runADIFMenu (const SBox &box)
{
    // set up the MENU_TEXT watch list field -- N.B. must free wl_mt.text
    MenuText wl_mt;                                             // watch list field
    char wl_state[WLA_MAXLEN];                                  // wl state, menu may change
    setupWLMenuText (WLID_ADIF, wl_mt, wl_state);

    // set up the MENU_TEXT file name field -- N.B. must free fn_mt.text
    MenuText fn_mt;                                             // file name field
    memset (&fn_mt, 0, sizeof(fn_mt));
    fn_mt.text = (char *) calloc (NV_ADIFFN_LEN, 1);            // full length text - N.B. must free!
    fn_mt.t_mem = NV_ADIFFN_LEN;                                // total text memory available
    snprintf (fn_mt.text, NV_ADIFFN_LEN,"%s",getADIFilename()); // init displayed file name
    fn_mt.label = strdup("File:");                              // field label -- N.B. must free!
    fn_mt.l_mem = strlen(fn_mt.label) + 1;                      // max label len
    fn_mt.text_fp = testADIFFilename;                           // file name test
    fn_mt.c_pos = fn_mt.w_pos = 0;                              // start at left

    // fill column-wise

    #define AM_INDENT  1

    // fill column-wise
    MenuItem mitems[] = {
        {MENU_LABEL,                false, 0, AM_INDENT, "Sort:", 0},                      // 0
        {MENU_BLANK,                false, 0, AM_INDENT, NULL, 0},                         // 1
        {MENU_1OFN, adif_sort == ADS_AGE,  1, AM_INDENT, "Age", 0},                        // 2
        {MENU_1OFN, adif_sort == ADS_BAND, 1, AM_INDENT, "Band", 0},                       // 3
        {MENU_1OFN, adif_sort == ADS_CALL, 1, AM_INDENT, "Call", 0},                       // 4
        {MENU_1OFN, adif_sort == ADS_DIST, 1, AM_INDENT, "Dist", 0},                       // 5
        {MENU_TEXT,                 false, 3, AM_INDENT, wl_state, &wl_mt},                // 6
        {MENU_TEXT,                 false, 4, AM_INDENT, fn_mt.label, &fn_mt},             // 7
    };
    #define MI_N NARRAY(mitems)

    SBox menu_b;
    menu_b.x = box.x + 3;
    menu_b.y = box.y + 40;
    menu_b.w = box.w - 6;

    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 3, MI_N, mitems};
    if (runMenu (menu)) {

        // new sort
        if (mitems[2].set)
            adif_sort = ADS_AGE;
        else if (mitems[5].set)
            adif_sort = ADS_DIST;
        else if (mitems[4].set)
            adif_sort = ADS_CALL;
        else if (mitems[3].set)
            adif_sort = ADS_BAND;
        else
            fatalError ("runADIFMenu no sort set");

        // must recompile to update WL but runMenu already insured wl compiles ok
        Message ynot;
        if (lookupWatchListState (wl_mt.label) != WLA_OFF && !compileWatchList (WLID_ADIF, wl_mt.text, ynot))
            fatalError ("ADIF failed recompling wl %s: %s", wl_mt.text, ynot.get());
        setWatchList (WLID_ADIF, wl_mt.label, wl_mt.text);
        Serial.printf ("ADIF: set WL to %s %s\n", wl_mt.label, wl_mt.text);

        // save
        Serial.printf ("ADIF: new file name from menu: %s\n", fn_mt.text);
        saveADIFSettings (fn_mt.text);

        // refresh pane to engage choices
        scheduleNewPlot (PLOT_CH_ADIF);
    }

    // clean up 
    free (wl_mt.text);
    free (fn_mt.text);
    free (fn_mt.label);

}

/* freshen the ADIF file if used and necessary then update pane if in use.
 * leave with newfile_pending set if file changed but we're currently not in a position to show new entries.
 * N.B. io errors are fatal.
 */
void freshenADIFFile (void)
{
    // web files just sit until menu is run again
    if (showing_set_adif)
        return;

    // get full filename
    const char *fn = getADIFilename();
    if (!fn)
        return;                                                         // not interested
    char fn_exp[1000];
    if (!expandENV (fn, fn_exp, sizeof(fn_exp)))
        fatalError ("ADIF %s failed ENV expansion", fn);

    // check for new/changed file
    if (fsig.fileChanged (fn_exp))
        newfile_pending = true;

    // read file if newer and not scrolled away
    if (newfile_pending && adif_ss.atNewest()) {

        // open
        FILE *fp = fopen (fn_exp, "r");
        if (!fp)
            fatalError ("ADIF %s: %s", fn_exp, strerror(errno));        // never returns

        // ingest
        GenReader gr(fp);
        int n_good;
        loadADIFFile (gr, n_good, n_adif_bad);
        fclose (fp);

        // update list if showing
        PlotPane pp = findPaneChoiceNow (PLOT_CH_ADIF);
        if (pp != PANE_NONE)
            drawADIFPane (plot_b[pp], getADIFilename());

        // caught up
        newfile_pending = false;
        showing_set_adif = false;
    }
}

/* replace our adif_spots with those found in the given GenReader.
 * pass back number of qualifying spots and bad spots found.
 * N.B. caller must close gr
 * N.B. we set n_adif_bad for drawADIFPane()
 * N.B. unless loading directly from the network, you probably want freshenADIFFile().
 */
void loadADIFFile (GenReader &gr, int &n_good, int &n_bad)
{
    // announce but no waiting, message will remain until this function returns
    mapMsg (0, "Loading ADIF file");

    // restart list and insure settings are loaded
    resetADIFMem();
    loadADIFSettings();

    // crack file, adds good entries to adif_spots[]
    adif_ss.n_data = readADIFFile (gr, adif_spots, true, n_bad);

    // report
    n_good = adif_ss.n_data;
    n_adif_bad = n_bad;
    Serial.printf ("ADIF: loaded %d qualifying %d busted spots\n", n_good, n_bad);

    // sort spots and prep for display
    qsort (adif_spots, adif_ss.n_data, sizeof(DXSpot), adif_pqsf[adif_sort]);
    adif_ss.scrollToNewest();

    // note new source type ready
    showing_set_adif = gr.isClient();
    
    // final message
    mapMsg (1000, "Loaded ADIF file");
}

/* called frequently to check for new ADIF records.
 * refresh pane if requested or file changes.
 */
void updateADIF (const SBox &box, bool refresh)
{
    // restart if fresh
    if (refresh) {
        resetADIFMem();
        adif_ss.init ((box.h - LISTING_Y0)/LISTING_DY, 0, 0, adif_ss.DIR_FROMSETUP);
        adif_ss.initNewSpotsSymbol (box, ADIF_COLOR);
        adif_ss.scrollToNewest();
        showing_set_adif = false;
        newfile_pending = true;
    }

    // check for changed file
    freshenADIFFile();

    // update symbol and hold
    adif_ss.drawNewSpotsSymbol (newfile_pending, false);
    if (adif_ss.atNewest())
        ROTHOLD_CLR(PLOT_CH_ADIF);
    else
        ROTHOLD_SET(PLOT_CH_ADIF);
}

/* draw each entry, if enabled
 */
void drawADIFSpotsOnMap()
{
    if (findPaneForChoice(PLOT_CH_ADIF) == PANE_NONE)
        return;

    // paths first then labels looks better
    for (int i = 0; i < adif_ss.n_data; i++) {
        DXSpot &si = adif_spots[i];
        drawSpotPathOnMap (si);
        if ((i % 1000) == 0)
            updateClocks(true);
    }
    for (int i = 0; i < adif_ss.n_data; i++) {
        DXSpot &si = adif_spots[i];
        drawSpotLabelOnMap (si, LOME_TXEND, LOMD_ALL);
        drawSpotLabelOnMap (si, LOME_RXEND, LOMD_ALL);
        if ((i % 1000) == 0)
            updateClocks(true);
    }
}

/* check for touch at s in the ADIF pane located in the given box.
 * return true if touch is for us else false so mean user wants to change pane selection.
 */
bool checkADIFTouch (const SCoord &s, const SBox &box)
{
    if (s.y < box.y + PANETITLE_H) {

        // scroll up?
        if (adif_ss.checkScrollUpTouch (s, box)) {
            scrollADIFUp (box);
            return (true);
        }

        // scroll down?
        if (adif_ss.checkScrollDownTouch (s, box)) {
            scrollADIFDown (box);
            return (true);
        }

        // New spots symbol?
        if (adif_ss.checkNewSpotsTouch (s, box)) {
            if (!adif_ss.atNewest() && newfile_pending) {
                // scroll to newest, let updateADIF do the rest
                adif_ss.drawNewSpotsSymbol (true, true);   // immediate feedback
                adif_ss.scrollToNewest();
            }
            return (true);                                      // claim our even if not showing
        }

        // on hold?
        if (ROTHOLD_TST(PLOT_CH_ADIF))
            return (true);

        // else in title
        return (false);
    }

    else if (s.y < box.y + SUBTITLE_Y0 + 10) {

        runADIFMenu(box);
        return (true);
    }

    else {

        // tapped entry to set DX

        int row_i = (s.y - box.y - LISTING_Y0)/LISTING_DY;
        int data_i;
        if (adif_ss.findDataIndex (row_i, data_i))
            newDX (adif_spots[data_i].tx_ll, NULL, adif_spots[data_i].tx_call);

        // our touch regardless of whether row was occupied
        return (true);
    }
}

/* find closest spot and location on either end to given ll, if any.
 */
bool getClosestADIFSpot (LatLong &ll, DXSpot *sp, LatLong *llp)
{
    return (adif_spots && findPaneForChoice(PLOT_CH_ADIF) != PANE_NONE
                && getClosestSpot (adif_spots, adif_ss.n_data, NULL, LOME_BOTH, ll, sp, llp));
}


/* return spot in our pane if under ms 
 */
bool getADIFPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll)
{
    // done if ms not showing our pane or not in our box
    PlotPane pp = findPaneChoiceNow (PLOT_CH_ADIF);
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
    if (adif_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            listrow_b.y = y0 + adif_ss.getDisplayRow(i) * LISTING_DY;
            if (inBox (ms, listrow_b)) {
                // ms is over this spot
                *dxs = adif_spots[i];
                *ll = dxs->tx_ll;
                return (true);
            }
        }
    }

    // none
    return (false);
}

/* return whether the given spot matches all the given tests for any ADIF spot.
 * N.B. check is limited to spots on ADIF watchlist if any.
 * N.B. if no tests are specified we always return true.
 * N.B. do NOT call freshenADIFFile() here -- it causes an inf loop
 */
bool onADIFList (const DXSpot &spot, bool chk_dxcc, bool chk_grid, bool chk_pref, bool chk_band)
{
    // prep spot
    HamBandSetting spot_band = findHamBand (spot.kHz);
    char spot_pref[MAX_PREF_LEN];
    findCallPrefix (spot.tx_call, spot_pref);

    // compare with each 
    for (int i = 0; i < adif_ss.n_data; i++) {
        DXSpot &adif_spot = adif_spots[i];
        bool pref_match = false;
        if (chk_pref) {
            char adif_pref[MAX_PREF_LEN];
            findCallPrefix (adif_spot.tx_call, adif_pref);
            pref_match = strcasecmp (spot_pref, adif_pref) == 0;
        }
        if (       (!chk_pref || pref_match)
                && (!chk_dxcc || spot.tx_dxcc == adif_spot.tx_dxcc)
                && (!chk_band || spot_band == findHamBand (adif_spot.kHz))
                && (!chk_grid || strncasecmp (spot.tx_grid, adif_spot.tx_grid, 4) == 0))
            return (true);
    }

    return (false);
}
