/* manage the On The Air activation Panes for any "On the Air" organization.
 * server collects using fetchONTA.pl.
 *
 * We actually keep two lists:
 *   onta_spots: the complete raw list, not sorted; length in n_ontaspots, simple hash to detect change.
 *   ontawl_spots: watchlist-filterd and sorted for display; length in onta_ss.n_data
 */

#include "HamClock.h"


// config
static const char onta_page[] = "/ONTA/onta.txt";       // query page
static const char onta_file[] = "onta.txt";             // local cache file
#define MAX_ONTAORGS    10                              // max organizations
#define ONTA_COLOR      RGB565(150,250,255)             // title and spot text color


// names and functions for each sort type
typedef enum {
    ONTAS_BAND, 
    ONTAS_CALL,
    ONTAS_ORG,
    ONTAS_AGE,
    ONTAS_N,
} ONTASort;

typedef struct {
    const char *menu_name;                              // menu name for this sort
    PQSF qsf;                                           // matching qsort compare func
} ONTASortInfo;
static const ONTASortInfo onta_sorts[ONTAS_N] = {
    {"Band", qsDXCFreq},
    {"Call", qsDXCTXCall},
    {"Org",  qsDXCRXGrid},
    {"Age",  qsDXCSpotted},
};

// organization filter and each component
static char onta_orgfilter[NV_ONTAORG_LEN];             // original orgs as one string
static char onta_orgtokens[NV_ONTAORG_LEN];             // all orgs each with EOS for onta_orgs
static char *onta_orgs[MAX_ONTAORGS];                   // ptr to each token within onta_orgtokens
static int onta_norgs;                                  // n used in onta_orgs[]
static int next_ontaorg;                                // used to rotate org unless onta_merge
static bool onta_merge;                                 // whether + was in list to cancel rotation


// ages
static const uint8_t onta_ages[] = {10, 20, 40, 60};    // possible ages, minutes
static uint8_t onta_age;                                // one of above, once set
#define N_ONTAAGES NARRAY(onta_ages)                    // handy count


// state
static DXSpot *onta_spots;                              // malloced list, complete
static int n_ontaspots;                                 // n spots in onta_spots
static DXSpot *ontawl_spots;                            // filtered malloced list, count in onta_ss.n_data
static ScrollState onta_ss;                             // scrolling state
static uint8_t onta_sortby;                             // one of ONTASort
static bool onta_showbio;                               // whether click shows bio
static uint32_t spots_hash, hash_atscroll;              // hash of onta_spots, value when scrolled away
#define NEW_SPOTS()     (spots_hash != hash_atscroll)       // handy test for new spots pending

/* return a simple hash of the given spots array
 */
static uint32_t spotsHash (const DXSpot *spots, int n_spots)
{
    // https://stackoverflow.com/questions/1579721/why-are-5381-and-33-so-important-in-the-djb2-algorithm
    uint8_t *bytes = (uint8_t *) spots;
    uint32_t hash = 5381;
    for (uint8_t *bytes_end = bytes + n_spots * sizeof(DXSpot); bytes < bytes_end; bytes++)
        hash = (hash << 5) + hash + *bytes;            // hash*33 + c
    return (hash);
}

/* split onta_orgfilter into onta_orgs via onta_orgtokens.
 * also check for '+' so set onta_merge.
 */
static void parseONTAOrgs(void)
{
    // copy to onta_orgtokens for splitting
    memcpy (onta_orgtokens, onta_orgfilter, NV_ONTAORG_LEN);

    // remove but note all +
    onta_merge = false;
    for (char *plus = strchr (onta_orgtokens, '+'); plus != NULL; plus = strchr (onta_orgtokens, '+')) {
        onta_merge = true;
        *plus = ' ';
    }

    // split and reset rotation index
    onta_norgs = strtokens (onta_orgtokens, onta_orgs, MAX_ONTAORGS);
    if (next_ontaorg >= onta_norgs)
        next_ontaorg = 0;

    Serial.printf ("ONTA: parsed orgs '%s' into %d tokens with%s merging\n", onta_orgfilter, onta_norgs,                onta_merge ? "" : "out");
}


/* return whether the given spot's org is allowed
 */
static bool isSpotOrgOk (const DXSpot &s)
{
    // remember: rx_grid is repurposed for program name

    // all if no orgs
    if (onta_norgs == 0)
        return (true);

    // just next_ontaorg unless merging
    if (!onta_merge)
        return (strcasecmp (onta_orgs[next_ontaorg], s.rx_grid) == 0);

    // check for any org if merging
    if (onta_merge) {
        for (int i = 0; i < onta_norgs; i++) {
            if (strcasecmp (onta_orgs[i], s.rx_grid) == 0) {
                return (true);
            }
        }
    }

    // nope
    return (false);
}


/* insure our settings are loaded
 */
static void loadONTASettings (void)
{
    if (!NVReadUInt8 (NV_ONTASORTBY, &onta_sortby) || onta_sortby >= ONTAS_N) {
        onta_sortby = ONTAS_AGE;
        NVWriteUInt8 (NV_ONTASORTBY, onta_sortby);
    }
    if (!NVReadString (NV_ONTAORG, onta_orgfilter)) {
        memset (onta_orgfilter, 0, sizeof(onta_orgfilter));
        NVWriteString (NV_ONTAORG, onta_orgfilter);
    }
    if (!NVReadUInt8 (NV_ONTA_MAXAGE, &onta_age)) {
        onta_age = onta_ages[1];
        NVWriteUInt8 (NV_ONTA_MAXAGE, onta_age);
    }

    // parse onta_orgfilter
    parseONTAOrgs();

    // determine onta_showbio
    uint8_t bio = 0;
    if (getQRZId() != QRZ_NONE) {
        if (!NVReadUInt8 (NV_ONTABIO, &bio))
            bio = 0;
    }
    onta_showbio = (bio != 0);
}

/* save our settings
 */
static void saveONTASettings (void)
{
    NVWriteUInt8 (NV_ONTASORTBY, onta_sortby);
    NVWriteString (NV_ONTAORG, onta_orgfilter);
    NVWriteUInt8 (NV_ONTA_MAXAGE, onta_age);
    NVWriteUInt8 (NV_ONTABIO, onta_showbio);
}

/* create a line of text for the given spot that fits within the box known to be for PANE_0 or PANE_123.
 * pass back n chars assigned to frequency.
 */
static void formatONTASpot (const DXSpot &spot, const SBox &box, char *line, size_t l_len, int &freq_len)
{
    const char *id = spot.rx_call;                      // repurposed
    int age = myNow() - spot.spotted;                   // seconds old

    if (BOX_IS_PANE_0(box)) {

        // box is 22 wide

        freq_len     = 5;
        int call_len = 6;
                    // 1 blank
        int id_len   = 9;
                    // 1 age code

        // 5 chars for freq requires MHz when > 99999
        if (spot.kHz > 99999) {
            size_t ns = snprintf (line, l_len, "%4.0fM%*.*s %*.*s",
                    spot.kHz * 1e-3,
                    call_len, call_len, spot.tx_call,
                    id_len, id_len, id);
            formatAge (age, line+ns, l_len-ns, 1);
        } else {
            size_t ns = snprintf (line, l_len, "%*.0f%*.*s %*.*s",
                    freq_len, spot.kHz,
                    call_len, call_len, spot.tx_call,
                    id_len, id_len, id);
            formatAge (age, line+ns, l_len-ns, 1);
        }

    } else {

        // box is 26 wide

        freq_len     = 6;
        int call_len = 8;
                    // 1 blank
        int id_len   = 9;
                    // 1 blank
                    // 1 age mark

        size_t ns = snprintf (line, l_len, "%*.0f%*.*s %*.*s ",
                freq_len, spot.kHz,
                call_len, call_len, spot.tx_call,
                id_len, id_len, id);

        formatAge (age, line+ns, l_len-ns, 1);
    }
}

/* redraw all visible ontawl_spots in the given pane box.
 * N.B. this just draws the ontawl_spots, use drawONTAPane to start from scratch.
 */
static void drawONTAVisSpots (const SBox &box)
{
    // can't quite use drawVisibleSpots() because of unique formatting :-(

    // update ADIF in case our WL uses it
    freshenADIFFile();

    // init and reset to black
    uint16_t x = box.x + 1;
    uint16_t y0 = box.y + LISTING_Y0;
    tft.fillRect (box.x+1, y0-LISTING_OS, box.w-2, box.h - (LISTING_Y0-LISTING_OS+1), RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // show vis spots and note if any would be red above and below
    bool any_older = false;
    bool any_newer = false;
    int min_i, max_i;
    if (onta_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = 0; i < onta_ss.n_data; i++) {
            const DXSpot &spot = ontawl_spots[i];
            if (i < min_i) {
                if (!any_older)
                    any_older = checkWatchListSpot (WLID_ONTA, spot) == WLS_HILITE;
            } else if (i > max_i) {
                if (!any_newer)
                    any_newer = checkWatchListSpot (WLID_ONTA, spot) == WLS_HILITE;
            } else {
                // build info line
                char line[50];
                int freq_len;
                formatONTASpot (spot, box, line, sizeof(line), freq_len);

                // set y location
                uint16_t y = y0 + onta_ss.getDisplayRow(i) * LISTING_DY;

                // highlight overall bg if on watch list
                if (checkWatchListSpot (WLID_ONTA, spot) == WLS_HILITE)
                    tft.fillRect (x, y-LISTING_OS, box.w-2, LISTING_DY-2, RA8875_RED);

                // show freq with proper band map color background
                uint16_t bg_col = getBandColor (spot.kHz);
                uint16_t txt_col = getGoodTextColor (bg_col);
                tft.setTextColor(txt_col);
                tft.fillRect (x, y-LISTING_OS, freq_len*6, LISTING_DY-2, bg_col);
                tft.setCursor (x, y);
                tft.printf ("%*.*s", freq_len, freq_len, line);

                // show remainder of line in white
                tft.setTextColor(RA8875_WHITE);
                tft.printf (line+freq_len);
            }
        }
    }

    // scroll controls red if any more red spots in their directions
    uint16_t up_color = ONTA_COLOR;
    uint16_t dw_color = ONTA_COLOR;
    if (onta_ss.okToScrollDown() &&
                ((scrollTopToBottom() && any_older) || (!scrollTopToBottom() && any_newer)))
        dw_color = RA8875_RED;
    if (onta_ss.okToScrollUp() &&
                ((scrollTopToBottom() && any_newer) || (!scrollTopToBottom() && any_older)))
        up_color = RA8875_RED;

    onta_ss.drawScrollUpControl (box, up_color, ONTA_COLOR);
    onta_ss.drawScrollDownControl (box, dw_color, ONTA_COLOR);
}

/* draw spots in the given pane box from scratch.
 * use drawONTAVisSpots() if want to redraw just the spots.
 */
static void drawONTAPane (const SBox &box)
{
    // prep
    prepPlotBox (box);

    // title
    const char *title = BOX_IS_PANE_0(box) ? "On Air" : "On The Air";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(ONTA_COLOR);
    uint16_t pw = getTextWidth(title);
    tft.setCursor (box.x + (box.w-pw)/2, box.y + PANETITLE_H);
    tft.print (title);

    // show current org or All
    char f[NV_ONTAORG_LEN];
    if (onta_merge)
        quietStrncpy (f, onta_orgfilter, NV_ONTAORG_LEN);               // show original including +
    else if (onta_norgs > 0)
        quietStrncpy (f, onta_orgs[next_ontaorg], NV_ONTAORG_LEN);      // show current
    else
        quietStrncpy (f, "All", NV_ONTAORG_LEN);                        // show "All"
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t f_l = maxStringW (f, box.w-2);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (box.x + (box.w-f_l)/2, box.y + SUBTITLE_Y0);
    tft.print (f);

    // show each spot
    drawONTAVisSpots (box);
}

/* handy check whether New Spot symbol needs changing on/off
 */
static void checkONTANewSpotSymbol (bool was_at_newest)
{
    if (was_at_newest && !onta_ss.atNewest()) {
        // record hash when scrolling away and hold rotation
        hash_atscroll = spotsHash (onta_spots, n_ontaspots);
        ROTHOLD_SET(PLOT_CH_ONTA);
    } else if (!was_at_newest && onta_ss.atNewest()) {
        // fresh view from the beginning and release rotation hold
        scheduleNewPlot (PLOT_CH_ONTA);
        ROTHOLD_CLR(PLOT_CH_ONTA);
    }
}


/* scroll up, if appropriate to do so now.
 */
static void scrollONTAUp (const SBox &box)
{
    bool was_at_newest = onta_ss.atNewest();
    if (onta_ss.okToScrollUp ()) {
        onta_ss.scrollUp ();
        drawONTAVisSpots (box);
    }
    checkONTANewSpotSymbol (was_at_newest);
}

/* scroll down, if appropriate to do so now.
 */
static void scrollONTADown (const SBox &box)
{
    bool was_at_newest = onta_ss.atNewest();
    if (onta_ss.okToScrollDown()) {
        onta_ss.scrollDown ();
        drawONTAVisSpots (box);
    }
    checkONTANewSpotSymbol (was_at_newest);
}

/* set bio, radio and new DX from given spot
 */
static void engageONTARow (DXSpot &s)
{
    if (onta_showbio)
        openQRZBio (s);
    setRadioSpot(s.kHz);
    newDX (s.tx_ll, NULL, s.tx_call);
}

/* rebuild ontawl_spots from onta_spots
 */
static void rebuildONTAWatchList(void)
{
    // update ADIF in case our WL uses it
    freshenADIFFile();

    // extract qualifying spots from onta_spots into ontawl_spots
    time_t oldest = myNow() - 60*onta_age;               // minutes to seconds
    onta_ss.n_data = 0;                                  // reset count, don't bother to resize ontawl_spots
    int n_old = 0, n_no_org = 0, n_no_wl = 0;
    for (int i = 0; i < n_ontaspots; i++) {
        DXSpot &spot = onta_spots[i];
        if (spot.spotted < oldest)
            n_old++;
        else if (!isSpotOrgOk (spot))
            n_no_org++;
        else if (checkWatchListSpot (WLID_ONTA, spot) == WLS_NO)
            n_no_wl++;
        else {
            // ok!
            ontawl_spots = (DXSpot *) realloc (ontawl_spots, (onta_ss.n_data+1) * sizeof(DXSpot));
            if (!ontawl_spots)
                fatalError ("No mem for %d watch list spots", onta_ss.n_data+1);
            ontawl_spots[onta_ss.n_data++] = spot;
        }
    }

    Serial.printf ("ONTA: %d total - %d too-old - %d not-org - %d not-WL = %d showing\n",
                    n_ontaspots, n_old, n_no_org, n_no_wl, onta_ss.n_data);

    // sort as desired and scroll to newest with new n_data
    qsort (ontawl_spots, onta_ss.n_data, sizeof(DXSpot), onta_sorts[onta_sortby].qsf);
    onta_ss.scrollToNewest();
}


/* show menu to let op select sort and edit watch list
 * Age:
 *   ( ) 10 m   ( ) 40 m
 *   ( ) 20 m   ( ) 60 m
 * Sort by:
 *   ( ) Age    ( ) Call
 *   ( ) Band   ( ) Org
 * Org:
 * Watch:
 */
static void runONTASortMenu (const SBox &box)
{
    // insure defaults are set in case retrieval failed
    loadONTASettings();

    // set up the watch list MENU_TEXT field
    MenuText wl_mt;                                             // menu text prompt context
    char wl_state[WLA_MAXLEN];                                  // wl state, menu may change
    setupWLMenuText (WLID_ONTA, wl_mt, wl_state);               // N.B. we must free wl_mt.text

    // set up the org name field
    MenuText org_mt;                                            // file name field
    memset (&org_mt, 0, sizeof(org_mt));
    char org_text[NV_ONTAORG_LEN];
    quietStrncpy (org_text, onta_orgfilter, NV_ONTAORG_LEN);    // init with current
    org_mt.text = org_text;                                     // working mem
    org_mt.t_mem = NV_ONTAORG_LEN;                              // total text memory available
    char org_label[] = "Org: ";                                 // prompt
    org_mt.label = org_label;
    org_mt.l_mem = sizeof(org_label);                           // including EOS
    org_mt.c_pos = org_mt.w_pos = 0;                            // start at left
    org_mt.to_upper = true;

    // build the possible age labels
    char onta_ages_str[N_ONTAAGES][10];
    for (int i = 0; i < N_ONTAAGES; i++)
        snprintf (onta_ages_str[i], sizeof(onta_ages_str[i]), "%d m", onta_ages[i]);

    // whether to show bio on click, only show in menu at all if bio source has been set in Setup
    bool show_bio_enabled = getQRZId() != QRZ_NONE;
    MenuFieldType bio_lbl_mft = show_bio_enabled ? MENU_LABEL : MENU_IGNORE;
    MenuFieldType bio_yes_mft = show_bio_enabled ? MENU_1OFN : MENU_IGNORE;
    MenuFieldType bio_no_mft = show_bio_enabled ? MENU_1OFN : MENU_IGNORE;


    MenuItem mitems[] = {
        // column 1
        {bio_lbl_mft, false,                      0, 2, "Bio:", NULL},                          // 0
        {MENU_LABEL, false,                       0, 2, "Age:", NULL},                          // 1
        {MENU_BLANK, false,                       0, 2, NULL, NULL},                            // 2
        {MENU_LABEL, false,                       0, 2, "Sort:", NULL},                         // 3
        {MENU_BLANK, false,                       0, 2, NULL, NULL},                            // 4

        // column 2
        {bio_yes_mft, onta_showbio,               1, 2, "Yes", NULL},                           // 5
        {MENU_1OFN, onta_ages[0] == onta_age,     2, 2, onta_ages_str[0], NULL},                // 6
        {MENU_1OFN, onta_ages[2] == onta_age,     2, 2, onta_ages_str[2], NULL},                // 7
        {MENU_1OFN, onta_sortby == ONTAS_AGE,     3, 2, onta_sorts[ONTAS_AGE].menu_name, NULL}, // 8
        {MENU_1OFN, onta_sortby == ONTAS_BAND,    3, 2, onta_sorts[ONTAS_BAND].menu_name,NULL}, // 9

        // columns 3
        {bio_no_mft, !onta_showbio,               1, 2, "No", NULL},                            // 10
        {MENU_1OFN, onta_ages[1] == onta_age,     2, 2, onta_ages_str[1], NULL},                // 11
        {MENU_1OFN, onta_ages[3] == onta_age,     2, 2, onta_ages_str[3], NULL},                // 12
        {MENU_1OFN, onta_sortby == ONTAS_CALL,    3, 2, onta_sorts[ONTAS_CALL].menu_name,NULL}, // 13
        {MENU_1OFN, onta_sortby == ONTAS_ORG,     3, 2, onta_sorts[ONTAS_ORG].menu_name, NULL}, // 14

        // text fields across the bottom
        {MENU_TEXT, false,                        4, 2, org_mt.label, &org_mt},                 // 15
        {MENU_TEXT, false,                        5, 2, wl_mt.label, &wl_mt},                   // 16
    };
    #define ONTAMENU_N   NARRAY(mitems)

    SBox menu_b = box;                          // copy, not ref!
    menu_b.x = box.x + 5;
    menu_b.y = box.y + SUBTITLE_Y0;
    menu_b.w = box.w - 10;                      // pretty much full widt:
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 3, ONTAMENU_N, mitems};
    if (runMenu (menu)) {

        // check bio
        onta_showbio = mitems[5].set;

        // set desired age
        if (mitems[6].set)
            onta_age = onta_ages[0];
        else if (mitems[7].set)
            onta_age = onta_ages[2];
        else if (mitems[11].set)
            onta_age = onta_ages[1];
        else if (mitems[12].set)
            onta_age = onta_ages[3];
        else
            fatalError ("runONTASortMenu no age set");

        // set desired sort
        if (mitems[8].set)
            onta_sortby = ONTAS_AGE;
        else if (mitems[9].set)
            onta_sortby = ONTAS_BAND;
        else if (mitems[13].set)
            onta_sortby = ONTAS_CALL;
        else if (mitems[14].set)
            onta_sortby = ONTAS_ORG;
        else
            fatalError ("runONTASortMenu no sort set");

        // must recompile to update wl but runMenu already insured wl compiles ok
        Message ynot;
        if (lookupWatchListState (wl_mt.label) != WLA_OFF && !compileWatchList (WLID_ONTA, wl_mt.text, ynot))
            fatalError ("onair failed recompling wl %s: %s", wl_mt.text, ynot.get());
        setWatchList (WLID_ONTA, wl_mt.label, wl_mt.text);
        Serial.printf ("ONTA: set WL to %s %s\n", wl_mt.label, wl_mt.text);

        // save potentially new org filter
        quietStrncpy (onta_orgfilter, strTrimAll (org_text), NV_ONTAORG_LEN);
        parseONTAOrgs();

        // save
        saveONTASettings();

        // full refresh
        onta_ss.scrollToNewest();
        scheduleNewPlot (PLOT_CH_ONTA);
    }

    // always free the working text
    free (wl_mt.text);
}

/* reset storage and prep for box
 */
static void resetONTAStorage (const SBox &box)
{
    free (onta_spots);
    onta_spots = NULL;
    n_ontaspots = 0;
    free (ontawl_spots);
    ontawl_spots = NULL;
    onta_ss.init ((box.h - LISTING_Y0)/LISTING_DY, 0, 0, onta_ss.DIR_FROMSETUP);
    onta_ss.scrollToNewest();
    onta_ss.initNewSpotsSymbol (box, ONTA_COLOR);
}

/* download all spots into onta_spots, regardless of watch etc.
 * return whether io ok, even if no data.
 */
static bool retrieveONTA (void)
{
    // reset
    free (onta_spots);
    onta_spots = NULL;
    n_ontaspots = 0;

    // go
    FILE *fp = openCachedFile (onta_file, onta_page, ONTA_INTERVAL, 0);
    bool ok = false;

    if (fp) {

        // look alive
        updateClocks(false);

        // add each spot
        char line[100];
        while (fgets (line, sizeof(line), fp)) {

            // rm trailing \n
            chompString (line);

            // skip comments
            if (line[0] == '#')
                continue;

            // prep next spot but don't count until known good
            onta_spots = (DXSpot*) realloc (onta_spots, (n_ontaspots+1)*sizeof(DXSpot));
            if (!onta_spots)
                fatalError ("No room for %d ONTA spots", n_ontaspots+1);
            DXSpot &new_sp = onta_spots[n_ontaspots];
            new_sp = {};

            // parse
            char dxcall[20], dxgrid[20], mode[20], id[20], prog[20];    // N.B. match sscanf fields
            unsigned long hz, unx;
            float lat_d, lng_d;
            // JI1ORE,430510000,1728012018,CW,QM05,35.7566,140.189,JA-1234,SOTA
            if (sscanf (line, "%19[^,],%lu,%lu,%19[^,],%19[^,],%f,%f,%19[^,],%19s",
                                dxcall, &hz, &unx, mode, dxgrid, &lat_d, &lng_d, id, prog) != 9) {

                // maybe a blank mode?
                if (sscanf (line, "%19[^,],%lu,%lu,,%19[^,],%f,%f,%19[^,],%19s",
                                dxcall, &hz, &unx, dxgrid, &lat_d, &lng_d, id, prog) != 8) {
                    // .. nope, something else
                    Serial.printf ("ONTA: bogus %s\n", line);
                    continue;
                }

                // .. yup that was it
                mode[0] = '\0';
            }

            // ignore long calls
            if (strlen (dxcall) >= MAX_SPOTCALL_LEN) {
                Serial.printf ("ONTA: ignoring long call: %s\n", line);
                continue;
            }

            // check valid freq
            float kHz = hz * 1e-3F;
            if (findHamBand (kHz) == HAMBAND_NONE) {
                Serial.printf ("ONTA: ignoring freq: %s\n", line);
                continue;
            }

            // DXCC
            if (!call2DXCC (dxcall, new_sp.tx_dxcc)) {
                Serial.printf ("ONTA: no DXCC for %s\n", dxcall);
                continue;
            }

            // fill new_sp, repurpose rx_call for id and rx_grid for program name
            quietStrncpy (new_sp.tx_call, dxcall, sizeof(new_sp.tx_call));
            quietStrncpy (new_sp.tx_grid, dxgrid, sizeof(new_sp.tx_grid));
            quietStrncpy (new_sp.rx_call, id, sizeof(new_sp.rx_call));
            quietStrncpy (new_sp.rx_grid, prog, sizeof(new_sp.rx_grid));
            quietStrncpy (new_sp.mode, mode, sizeof(new_sp.mode));
            new_sp.rx_ll = de_ll;                      // us?
            new_sp.tx_ll.lat_d = lat_d;
            new_sp.tx_ll.lng_d = lng_d;
            new_sp.tx_ll.normalize();
            new_sp.kHz = kHz;
            new_sp.spotted = unx;

            // ok! append to spots[]
            n_ontaspots += 1;
        }

        // io ok, even if none found
        ok = true;
    }

    // done
    Serial.printf ("ONTA: read %d spots\n", n_ontaspots);
    fclose (fp);

    // result
    return (ok);
}

/* called occsionally to draw ONTA pane in box.
 * return whether io ok.
 */
bool updateOnTheAir (const SBox &box, bool fresh)
{
    // init all if new
    if (fresh) {
        ROTHOLD_CLR(PLOT_CH_ONTA);
        resetONTAStorage (box);
        loadONTASettings();
    }

    // always update raw onta_spots, but don't transfer to ontawl_spots if scrolled away

    bool ok = retrieveONTA();
    if (ok) {
        spots_hash = spotsHash (onta_spots, n_ontaspots);
        if (onta_ss.atNewest()) {
            if (onta_norgs > 0 && !onta_merge) {
                // rotate to next org
                next_ontaorg = (next_ontaorg + 1) % onta_norgs; // rotate org
                Serial.printf ("ONTA: now showing %s\n", onta_orgs[next_ontaorg]);
            }
            rebuildONTAWatchList();
            onta_ss.drawNewSpotsSymbol (false, false);                  // New symbol off
            ROTHOLD_CLR(PLOT_CH_ONTA);                                  // release rotation hold
            drawONTAPane (box);
        } else {
            onta_ss.drawNewSpotsSymbol (NEW_SPOTS(), false);            // on if different
            ROTHOLD_SET(PLOT_CH_ONTA);                                  // hold rotation
        }
    } else {
        onta_ss.drawNewSpotsSymbol (false, false);                      // insure off either way
        onta_ss.scrollToNewest();
        ROTHOLD_CLR(PLOT_CH_ONTA);                                      // release any rotation hold
        plotMessage (box, RA8875_RED, "ONTA download error");
    }


    return (ok);
}

/* implement a tap at s known to be within the given box for our Pane.
 * return if something for us, else false to mean op wants to change the Pane option.
 */
bool checkOnTheAirTouch (const SCoord &s, const SBox &box)
{
    // check for title or scroll
    if (s.y < box.y + PANETITLE_H) {

        if (onta_ss.checkScrollUpTouch (s, box)) {
            scrollONTAUp (box);
            return (true);
        }

        if (onta_ss.checkScrollDownTouch (s, box)) {
            scrollONTADown (box);
            return (true);
        }

        if (onta_ss.checkNewSpotsTouch (s, box)) {
            if (!onta_ss.atNewest() && NEW_SPOTS()) {
                // scroll to newest, let updateOnTheAir() do the rest
                onta_ss.drawNewSpotsSymbol (true, true);                // immediate feedback 
                onta_ss.scrollToNewest();
                scheduleNewPlot (PLOT_CH_ONTA);
            }
            return (true);                      // claim our even if not showing
        }

        // on hold?
        if (ROTHOLD_TST(PLOT_CH_ONTA))
            return (true);

        // else tapping title always leaves this pane
        return (false);
    }

    // check for tapping count to run menu
    if (s.y < box.y + LISTING_Y0) {
        runONTASortMenu (box);
        return (true);
    }

    // tapped a row, engage if defined
    int spot_row;
    int vis_row = (s.y - (box.y + LISTING_Y0))/LISTING_DY;
    if (onta_ss.findDataIndex (vis_row, spot_row))
        engageONTARow (ontawl_spots[spot_row]);

    // ours even if row is empty
    return (true);

}

/* pass back the ONTA spots list, and whether there are any at all.
 * N.B. caller must not modify the list
 */
bool getOnTheAirSpots (DXSpot **spp, uint8_t *nspotsp)
{
    // none if no spots or not showing
    if (!onta_spots || findPaneForChoice (PLOT_CH_ONTA) == PANE_NONE)
        return (false);

    // pass back
    *spp = onta_spots;
    *nspotsp = onta_ss.n_data;

    // ok
    return (true);
}

/* draw all filtered ONTA spots on the map
 */
void drawOnTheAirSpotsOnMap (void)
{
    if (ontawl_spots && findPaneForChoice (PLOT_CH_ONTA) != PANE_NONE) {
        for (int j = 0; j < onta_ss.n_data; j++) {
            drawSpotLabelOnMap (ontawl_spots[j], LOME_TXEND, LOMD_ALL);
        }
    }
}

/* find closest ontawl_spot and location on tx end to given ll (we don't use rx_ll), if any.
 */
bool getClosestOnTheAirSpot (LatLong &ll, DXSpot *onta_closest, LatLong *ll_closest)
{
    return (ontawl_spots && findPaneForChoice (PLOT_CH_ONTA) != PANE_NONE && getSpotLabelType() != LBL_NONE
            && getClosestSpot (ontawl_spots, onta_ss.n_data, NULL, LOME_TXEND, ll, onta_closest, ll_closest));
}

/* return spot in our pane if under ms 
 */
bool getOnTheAirPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll)
{
    // done if ms not showing our pane or not in our box
    PlotPane pp = findPaneChoiceNow (PLOT_CH_ONTA);
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
    if (onta_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            listrow_b.y = y0 + onta_ss.getDisplayRow(i) * LISTING_DY;
            if (inBox (ms, listrow_b)) {
                // ms is over this spot
                *dxs = ontawl_spots[i];
                *ll = dxs->tx_ll;
                return (true);
            }
        }
    }

    // none
    return (false);
}


/* return whether we are rotating through multiple organizations
 */
bool isONTARotating(void)
{
    return (onta_norgs > 1 && !onta_merge && onta_ss.atNewest());
}
