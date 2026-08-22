/* manage the On The Air activation Panes for any "On the Air" organization.
 * server collects using fetchONTA.pl.
 *
 * We actually keep two lists:
 *   onta_spots: the complete raw list, not sorted; length in n_ontaspots, simple hash to detect change.
 *   ontawl_spots: watchlist-filterd and sorted for display; length in onta_ss.n_data
 */

#include "HamClock.h"

#include <unordered_map>
#include <unordered_set>
#include <string>


// config
static const char onta_page[] = "/ONTA/onta.txt";       // query page
static const char onta_file[] = "onta.txt";             // local cache file
#define MAX_ONTAORGS    10                              // max organizations
#define ONTA_COLOR      RGB565(150,250,255)             // title and spot text color

// second source, same onta.txt line schema, generated server-side by gen_iota.pl from the
// Spothole API's sig=IOTA filter (see gen_iota.pl for why IOTA needs its own feed: it has no
// dedicated per-org spotting API of its own like POTA/SOTA/WWFF, only spots whose comments
// happened to mention an IOTA reference). Purely additive -- kept as a separate file so
// onta.txt itself and its other consumers are completely undisturbed. Same {file,page,interval}
// shape as onta_page/onta_file above, just read into the very same onta_spots array.
static const char onta_iota_page[] = "/ONTA/iota_spots.txt";
static const char onta_iota_file[] = "iota_spots.txt";

// third source, same idea again: gen_xonta.pl covers whatever "extra" xOTA programs Spothole
// has live beyond POTA/SOTA/WWFF/IOTA -- currently GMA, LLOTA and WWTOTA, see gen_xonta.pl's
// own header comment for why that specific set and not the many others Spothole's README
// mentions. Same purely-additive, separate-file treatment as iota_spots.txt above.
static const char onta_xonta_page[] = "/ONTA/xonta_spots.txt";
static const char onta_xonta_file[] = "xonta_spots.txt";

// each entry is one onta.txt-schema source to merge into onta_spots
typedef struct {
    const char *file;
    const char *page;
} ONTASource;
static const ONTASource onta_sources[] = {
    { onta_file, onta_page },
    { onta_iota_file, onta_iota_page },
    { onta_xonta_file, onta_xonta_page },
};
#define N_ONTASOURCES NARRAY(onta_sources)

// per-org marker color, drawn as the background of the reference ("id") field -- same idea
// as the freq field's own band-color background just below it, not a separate letter column:
// unlike the DX Cluster pane's marker (spots.cpp's IOTA_MARK_W reserved column), ONTA's row
// layout is already character-budgeted to the pixel (see formatONTASpot()'s comment), with no
// spare width for another field. Colors chosen to avoid RA8875_RED (watchlist highlight) and
// to stay visually distinct from each other; deliberately does NOT need to match the DX
// Cluster pane's own WCA/ARLHS/etc marker colors from spots.cpp -- different pane, different
// (non-overlapping) set of orgs, no reason they need to share a palette.
typedef struct {
    const char *org;
    uint16_t color;
} ONTAOrgColor;
static const ONTAOrgColor onta_org_colors[] = {
    { "POTA",   RGB565(60,200,90)   },   // green
    { "SOTA",   RGB565(255,150,30)  },   // orange
    { "WWFF",   RGB565(0,190,160)   },   // teal
    { "IOTA",   RGB565(150,250,255) },   // cyan -- matches ONTA_COLOR
    { "GMA",    RGB565(150,150,170) },   // slate grey
    { "LLOTA",  RGB565(90,90,220)   },   // indigo
    { "WWTOTA", RGB565(184,115,51)  },   // copper
};

/* look up the marker color for an org name (spot.rx_grid, repurposed -- see header comment);
 * returns RA8875_BLACK as a "no specific color" sentinel for anything not in the table above,
 * eg an org the user's own onta.txt org filter produces that we haven't accounted for -- falls
 * back to plain, uncolored text rather than misbehaving.
 */
static uint16_t ontaOrgMarkerColor (const char *org)
{
    if (org)
        for (const ONTAOrgColor &oc : onta_org_colors)
            if (!strcasecmp (oc.org, org))
                return (oc.color);
    return (RA8875_BLACK);
}

// park/summit reference -> 2-letter state/province, purely additive side file. this
// data is essentially static (park locations don't move), so it's cached far longer
// than the spots feed itself and a missing/stale/absent file is never fatal -- the
// display just falls back to the 2-letter country code already embedded in every
// reference's own prefix (eg "US" from "US-2389").
static const char onta_parks_page[] = "/ONTA/onta_parks.txt";
static const char onta_parks_file[] = "onta_parks.txt";
#define ONTA_PARKS_INTERVAL ONTA_INTERVAL                    // refresh interval, secs
static std::unordered_map<std::string, std::string> onta_park_states;

// which activations the op has personally (re)spotted this session -- keyed by
// activator+reference (not tied to a specific frequency/mode/time, matching how a
// person would think of it: "have I already spotted this operator at this park").
// deliberately in-memory only: never written to NVRAM or any file, so it always
// starts empty on a fresh run and just reflects what happened since the app started.
static std::unordered_set<std::string> onta_spotted_by_me;

static std::string ontaSpottedKey (const DXSpot &s)
{
    return std::string(s.tx_call) + "|" + s.rx_call;          // rx_call repurposed: reference
}

static bool ontaWasSpottedByMe (const DXSpot &s)
{
    return onta_spotted_by_me.count (ontaSpottedKey(s)) != 0;
}

static void ontaMarkSpottedByMe (const DXSpot &s)
{
    onta_spotted_by_me.insert (ontaSpottedKey(s));
}


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


// mode filter: independent checkboxes, "at least 1" enforced by the menu itself.
// PHONE is a convenience shortcut meaning "SSB or FM or AM", not a distinct mode
// string of its own -- unlike the other 5 boxes it never appears in spot.mode.
#define ONTAMB_CW       (1<<0)
#define ONTAMB_FM       (1<<1)
#define ONTAMB_FT8      (1<<2)
#define ONTAMB_SSB      (1<<3)
#define ONTAMB_PHONE    (1<<4)
#define ONTAMB_DATA     (1<<5)
#define ONTAMB_ALL      (ONTAMB_CW|ONTAMB_FM|ONTAMB_FT8|ONTAMB_SSB|ONTAMB_PHONE|ONTAMB_DATA)
static uint8_t onta_modes;                              // bitmask of ONTAMB_*

// band filter: same bitmask convention as Live Spots' psk_bands, but only 8 of the 12
// supported bands get their own checkbox -- matching what POTA.app's own spot filter
// dropdown offers (40/20/30/17/15/12/10/6). Everything else (160/80/60/2m, or any
// frequency outside a known ham band at all) is covered by a single "Other" checkbox
// instead, using a dedicated bit well outside HamBandSetting's 0..11 range.
static uint32_t onta_bands;                             // bit i set means HamBandSetting i shown
#define TST_ONTABAND(b) ((b) != HAMBAND_NONE && (onta_bands & (1U<<(b))) != 0)
#define SET_ONTABAND(b) (onta_bands |= (1U<<(b)))
#define ONTA_BAND_OTHER (1U<<31)                                // catch-all bit
#define ONTA_NBANDS_SHOWN 8                                     // how many get their own checkbox
static const HamBandSetting onta_shown_bands[ONTA_NBANDS_SHOWN] = {
    HAMBAND_40M, HAMBAND_20M, HAMBAND_30M, HAMBAND_17M,
    HAMBAND_15M, HAMBAND_12M, HAMBAND_10M, HAMBAND_6M,
};


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


/* return whether the given spot passes the current mode+band filter.
 * a blank mode or a frequency outside any known ham band always passes -- we don't
 * hide a spot just because we can't classify it.
 */
static bool ontaModeBandOk (const DXSpot &s)
{
    // band -- one of the 8 bands with their own checkbox, or everything else (160/80/60/2m,
    // or any frequency outside a known ham band entirely) bucketed under "Other"
    HamBandSetting b = findHamBand (s.kHz);
    bool is_shown_band = false;
    for (int i = 0; i < ONTA_NBANDS_SHOWN && !is_shown_band; i++)
        if (b == onta_shown_bands[i])
            is_shown_band = true;
    if (is_shown_band) {
        if (!TST_ONTABAND(b))
            return (false);
    } else {
        if (!(onta_bands & ONTA_BAND_OTHER))
            return (false);
    }

    // mode -- note a blank mode is deliberately NOT auto-passed here; it falls through
    // to the "data" catch-all below so it's actually filterable (previously it bypassed
    // the filter entirely, which is why spots with missing mode data could appear under
    // every single mode checkbox regardless of what was actually checked)
    bool cw  = strcasecmp (s.mode, "CW")  == 0;
    bool ft8 = strcasecmp (s.mode, "FT8") == 0;
    bool ssb = strcasecmp (s.mode, "SSB") == 0 || strcasecmp (s.mode, "USB") == 0
                                                || strcasecmp (s.mode, "LSB") == 0;
    bool fm  = strcasecmp (s.mode, "FM")  == 0;
    bool am  = strcasecmp (s.mode, "AM")  == 0;
    bool data = !cw && !ft8 && !ssb && !fm && !am;         // catch-all: blank, FT4, RTTY, etc

    if (cw   && (onta_modes & ONTAMB_CW))    return (true);
    if (ft8  && (onta_modes & ONTAMB_FT8))   return (true);
    if (ssb  && (onta_modes & ONTAMB_SSB))   return (true);
    if (fm   && (onta_modes & ONTAMB_FM))    return (true);
    if (data && (onta_modes & ONTAMB_DATA))  return (true);
    if ((ssb || fm || am) && (onta_modes & ONTAMB_PHONE)) return (true);   // shortcut

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
    if (!NVReadUInt8 (NV_ONTA_MODES, &onta_modes)) {
        onta_modes = ONTAMB_ALL;
        NVWriteUInt8 (NV_ONTA_MODES, onta_modes);
    }
    if (!NVReadUInt32 (NV_ONTA_BANDS, &onta_bands)) {
        onta_bands = ONTA_BAND_OTHER;
        for (int i = 0; i < ONTA_NBANDS_SHOWN; i++)
            SET_ONTABAND(onta_shown_bands[i]);
        NVWriteUInt32 (NV_ONTA_BANDS, onta_bands);
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
    NVWriteUInt8 (NV_ONTA_MODES, onta_modes);
    NVWriteUInt32 (NV_ONTA_BANDS, onta_bands);
}

/* fill out[3] with the 2-letter state/province for the given reference if we have
 * one cached (from onta_parks.txt), else fall back to the 2-letter country code
 * parsed directly out of the reference's own prefix (eg "US" from "US-2389", "DE"
 * from a German reference) -- that prefix is always there regardless of whether
 * the state lookup ever loads, so this always has *something* useful to show.
 */
static void ontaStateOrCountry (const char *ref, char out[3])
{
    auto it = onta_park_states.find (ref);
    if (it != onta_park_states.end() && it->second.length() >= 2) {
        out[0] = it->second[0];
        out[1] = it->second[1];
    } else {
        const char *dash = strchr (ref, '-');
        size_t n = dash ? (size_t)(dash - ref) : strlen(ref);
        out[0] = n >= 1 ? ref[0] : '?';
        out[1] = n >= 2 ? ref[1] : '?';
    }
    out[2] = '\0';
}

// ontheair row layout, in characters -- shared between formatONTASpot() (drawing)
// and checkOnTheAirTouch() (tap-the-callsign-for-full-text) so the two can't drift
// out of sync. FAST_FONT is fixed-width at ONTA_CHAR_W pixels/char, same constant
// drawONTAVisSpots() already uses for the frequency field's background box.
#define ONTA_CHAR_W       6
#define ONTA_P0_FREQ_LEN  5
#define ONTA_P0_CALL_LEN  3
#define ONTA_P0_ID_LEN    9
#define ONTA_PX_FREQ_LEN  6
#define ONTA_PX_CALL_LEN  5
#define ONTA_PX_ID_LEN    9

/* create a line of text for the given spot that fits within the box known to be for PANE_0 or PANE_123.
 * pass back n chars assigned to frequency.
 */
static void formatONTASpot (const DXSpot &spot, const SBox &box, char *line, size_t l_len, int &freq_len)
{
    const char *id = spot.rx_call;                      // repurposed
    int age = myNow() - spot.spotted;                   // seconds old

    char st[3];
    ontaStateOrCountry (id, st);

    if (BOX_IS_PANE_0(box)) {

        // box is 22 wide: freq(5) call(3) " " id(9) " " state(2) age(1)
        // callsign is the field trimmed to make room for state/country (park
        // reference kept full-length); tap directly on the callsign to see it
        // in full via a tooltip, see checkOnTheAirTouch().

        freq_len     = ONTA_P0_FREQ_LEN;
        int call_len = ONTA_P0_CALL_LEN;
                    // 1 blank
        int id_len   = ONTA_P0_ID_LEN;
                    // 1 blank
                    // 2 state/country
                    // 1 age code

        // 5 chars for freq requires MHz when > 99999
        if (spot.kHz > 99999) {
            size_t ns = snprintf (line, l_len, "%4.0fM%*.*s %*.*s %2.2s",
                    spot.kHz * 1e-3,
                    call_len, call_len, spot.tx_call,
                    id_len, id_len, id,
                    st);
            formatAge (age, line+ns, l_len-ns, 1);
        } else {
            size_t ns = snprintf (line, l_len, "%*.0f%*.*s %*.*s %2.2s",
                    freq_len, spot.kHz,
                    call_len, call_len, spot.tx_call,
                    id_len, id_len, id,
                    st);
            formatAge (age, line+ns, l_len-ns, 1);
        }

    } else {

        // box is 26 wide: freq(6) call(5) " " id(9) " " state(2) " " age(1)
        // same trade-off as above

        freq_len     = ONTA_PX_FREQ_LEN;
        int call_len = ONTA_PX_CALL_LEN;
                    // 1 blank
        int id_len   = ONTA_PX_ID_LEN;
                    // 1 blank
                    // 2 state/country
                    // 1 blank
                    // 1 age mark

        size_t ns = snprintf (line, l_len, "%*.0f%*.*s %*.*s %2.2s ",
                freq_len, spot.kHz,
                call_len, call_len, spot.tx_call,
                id_len, id_len, id,
                st);

        formatAge (age, line+ns, l_len-ns, 1);
    }
}

/* fill f[] (of size NV_ONTAORG_LEN) with the currently effective org filter label:
 * the merged filter string, the single org currently being cycled through, or "All".
 * shared by drawONTAPane (subtitle) and drawONTAVisSpots (empty-results message) so
 * the two never drift out of sync.
 */
static void ontaOrgLabel (char *f)
{
    if (onta_merge)
        quietStrncpy (f, onta_orgfilter, NV_ONTAORG_LEN);               // show original including +
    else if (onta_norgs > 0)
        quietStrncpy (f, onta_orgs[next_ontaorg], NV_ONTAORG_LEN);      // show current
    else
        quietStrncpy (f, "All", NV_ONTAORG_LEN);                        // show "All"
}

/* print a short message centered in the *listing* portion of an ONTA-style pane, ie below
 * the title/subtitle. deliberately does NOT call prepPlotBox() like plotMessage() does --
 * that would erase the title/subtitle and draw an unwanted inner border. the listing area
 * is assumed to already be freshly cleared to black by the caller.
 */
static void drawONTAEmptyMsg (const SBox &box, const char *message)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    char *msg_cpy = strdup (message);
    size_t msg_len = strlen (message);
    uint16_t msg_printed = 0;
    uint16_t y = box.y + LISTING_Y0 + 2*LISTING_DY;

    for (int n_lines = 0; n_lines < 4 && msg_printed < msg_len; n_lines++) {

        // chop at max width -- maxStringW overwrites all beyond with 0's
        size_t l_before = strlen (msg_cpy);
        (void) maxStringW (msg_cpy, box.w-4);
        size_t l_after = strlen (msg_cpy);

        // unless finished, look for a closer blank so we don't break mid-word
        if (l_after < l_before) {
            char *blank = strrchr (msg_cpy, ' ');
            if (blank)
                blank[1] = '\0';
        }

        uint16_t msgw = getTextWidth (msg_cpy);
        tft.setCursor (box.x + (box.w-msgw)/2, y);
        tft.print (msg_cpy);

        msg_printed += strlen (msg_cpy);
        strcpy (msg_cpy, message+msg_printed+(message[msg_printed] == ' ' ? 1 : 0));
        y += 2*LISTING_DY;
    }

    free (msg_cpy);
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

                // show remainder of line in white, or green + struck through once we've
                // personally (re)spotted this activation this session -- except the id (park/
                // summit/etc reference) field, which gets its own org-colored background the
                // same way freq gets a band-colored one above, see ontaOrgMarkerColor(). that
                // field's text color follows the background for contrast, same as freq's does,
                // regardless of spotted state -- consistent with how freq's own text color
                // already ignores spotted state.
                bool spotted = ontaWasSpottedByMe (spot);
                uint16_t rem_color = spotted ? RA8875_GREEN : RA8875_WHITE;
                int call_len = BOX_IS_PANE_0(box) ? ONTA_P0_CALL_LEN : ONTA_PX_CALL_LEN;
                int id_len   = BOX_IS_PANE_0(box) ? ONTA_P0_ID_LEN   : ONTA_PX_ID_LEN;

                // call field + 1 trailing blank, plain
                uint16_t call_x = x + freq_len*ONTA_CHAR_W;
                tft.setTextColor (rem_color);
                tft.setCursor (call_x, y);
                tft.printf ("%.*s", call_len+1, line+freq_len);

                // id field, org-colored background
                uint16_t id_x = call_x + (call_len+1)*ONTA_CHAR_W;
                uint16_t id_col = ontaOrgMarkerColor (spot.rx_grid);          // repurposed: program name
                if (id_col != RA8875_BLACK) {
                    tft.fillRect (id_x, y-LISTING_OS, id_len*ONTA_CHAR_W, LISTING_DY-2, id_col);
                    tft.setTextColor (getGoodTextColor (id_col));
                } else
                    tft.setTextColor (rem_color);
                tft.setCursor (id_x, y);
                tft.printf ("%.*s", id_len, line+freq_len+call_len+1);

                // remainder: blank(s) + state/country + age, plain
                uint16_t tail_x = id_x + id_len*ONTA_CHAR_W;
                tft.setTextColor (rem_color);
                tft.setCursor (tail_x, y);
                tft.printf (line+freq_len+call_len+1+id_len);

                if (spotted) {
                    uint16_t rem_x0 = call_x;
                    uint16_t rem_w = getTextWidth (line+freq_len);
                    tft.drawLine (rem_x0, y+3, rem_x0+rem_w, y+3, RA8875_GREEN);
                }
            }
        }
    } else {
        // nothing to show -- if a specific org is selected (not the "All" pool, which
        // realistically always has something), say so rather than leave the pane blank.
        // POTA is unlikely to ever hit this, but SOTA/WWFF/IOTA can go quiet for a while.
        char f[NV_ONTAORG_LEN];
        ontaOrgLabel (f);
        if (strcmp (f, "All")) {
            char msg[NV_ONTAORG_LEN+50];
            snprintf (msg, sizeof(msg), "No '%s' org spots found - watch here for more", f);
            drawONTAEmptyMsg (box, msg);
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
    ontaOrgLabel (f);
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

/* percent-encode s as one query-string value into buf (buf_len must be at least
 * 3*strlen(s)+1). mirrors the encoder already used for the pane's "search the web"
 * feature, just factored out here since this file has its own outbound query to build.
 */
static void ontaURLEncode (const char *s, char *buf, size_t buf_len)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p && used+4 < buf_len; p++) {
        char c = (char)*p;
        if (c == ' ')
            buf[used++] = '+';
        else if (isalnum((unsigned char)c) || c=='-' || c=='_' || c=='.' || c=='~')
            buf[used++] = c;
        else
            used += snprintf (buf+used, buf_len-used, "%%%02X", (unsigned char)c);
    }
    buf[used < buf_len ? used : buf_len-1] = '\0';
}

/* post (or re-post) a spot for the given ONTA activation to the OHB backend, which relays
 * it on to whichever network (POTA etc) the spot came from. the spotter is always our own
 * configured callsign and the reporting software is always identified as HamClock -- op has
 * no say over either, same as Hunterlog and similar tools always identify themselves.
 * return whether accepted, with a short reason in ynot either way.
 */
static bool postONTASpot (const DXSpot &s, const char *comment, Message &ynot)
{
    WiFiClient onta_client;
    bool ok = false;

    // encode each dynamic field separately -- reference codes in particular often
    // contain '/' (eg SOTA "W7A/AN-011") which is not safe unencoded in a query value
    char e_org[3*MAX_SPOTGRID_LEN+4], e_call[3*MAX_SPOTCALL_LEN+4], e_ref[3*MAX_SPOTCALL_LEN+4];
    char e_mode[3*MAX_SPOTMODE_LEN+4], e_spotter[3*NV_CALLSIGN_LEN+4], e_comment[3*60+4];
    ontaURLEncode (s.rx_grid, e_org, sizeof(e_org));                    // repurposed: program name
    ontaURLEncode (s.tx_call, e_call, sizeof(e_call));
    ontaURLEncode (s.rx_call, e_ref, sizeof(e_ref));                    // repurposed: reference id
    ontaURLEncode (s.mode[0] ? s.mode : "", e_mode, sizeof(e_mode));
    ontaURLEncode (getCallsign(), e_spotter, sizeof(e_spotter));
    ontaURLEncode (comment ? comment : "", e_comment, sizeof(e_comment));

    char page[600];
    snprintf (page, sizeof(page),
        "/ONTA/spot.pl?org=%s&call=%s&ref=%s&khz=%.1f&mode=%s&spotter=%s&source=HamClock&comment=%s",
        e_org, e_call, e_ref, s.kHz, e_mode, e_spotter, e_comment);

    if (onta_client.connect (backend_host, backend_port)) {

        updateClocks(false);

        Serial.printf ("ONTA: %s\n", page);
        httpHCGET (onta_client, backend_host, page);

        // check the HTTP status line ourselves before trying to parse a body -- a missing
        // or misconfigured spot.pl (404, 500 etc) would otherwise just look like a garbled,
        // confusing response instead of the clear, specific problem it actually is
        char status_line[100];
        int code = 0;
        if (!getTCPLine (onta_client, status_line, sizeof(status_line), NULL)) {
            ynot.set ("Spot timeout");
        } else if (sscanf (status_line, "HTTP/%*s %d", &code) != 1 || code == 0) {
            ynot.set ("Unexpected reply");
        } else if (code == 404) {
            ynot.set ("spot.pl not found");
        } else if (code != 200) {
            char buf[100];
            snprintf (buf, sizeof(buf), "Backend error %d", code);
            ynot.set (buf);
        } else if (!httpSkipHeader (onta_client)) {
            ynot.set ("Spot timeout");
        } else {
            char line[100];
            while (getTCPLine (onta_client, line, sizeof(line), NULL)) {
                if (strncmp (line, "error=", 6) == 0) {
                    ynot.set (line+6);
                    ok = false;
                    break;
                }
                if (strncmp (line, "ok=", 3) == 0)
                    ok = true;
            }
            if (!ok && ynot.get()[0] == '\0')
                ynot.set ("No reply");
        }

        onta_client.stop();

    } else {
        ynot.set ("Connection failed");
    }

    return (ok);
}

/* show a small popup letting the op (re)spot the given ONTA activation, a la Hunterlog.
 * spotter is always our configured callsign; reporting software is always "HamClock".
 */
static void runONTASpotMenu (const SBox &box, const DXSpot &s)
{
    // read-only info lines -- kept short so the menu never needs to grow wider than
    // this (narrow) pane; a wider menu spills past the pane's own bounds and its
    // backing-store restore can't fully clean up the strip left outside the pane
    char line1[50], line2[50], line3[50];
    if (s.mode[0])
        snprintf (line1, sizeof(line1), "%s %.0f %s", s.tx_call, s.kHz, s.mode);
    else
        snprintf (line1, sizeof(line1), "%s %.0f", s.tx_call, s.kHz);
    snprintf (line2, sizeof(line2), "%s %s", s.rx_grid, s.rx_call);        // program + reference
    snprintf (line3, sizeof(line3), "Spotter: %s", getCallsign());

    // hard guarantee regardless of how long a callsign/reference/comment happens to be --
    // measure in the same font runMenu() will actually draw these labels in
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t safe_w = box.w > 40 ? box.w - 40 : box.w;    // leaves room for indent/selector/margin
    maxStringW (line1, safe_w);
    maxStringW (line2, safe_w);
    maxStringW (line3, safe_w);

    // editable comment field, starts blank
    MenuText cm_mt;
    memset (&cm_mt, 0, sizeof(cm_mt));
    char comment[60];
    comment[0] = '\0';
    cm_mt.text = comment;
    cm_mt.t_mem = sizeof(comment);
    static char cm_label[] = "Comment: ";
    cm_mt.label = cm_label;
    cm_mt.l_mem = sizeof(cm_label);
    cm_mt.c_pos = cm_mt.w_pos = 0;
    cm_mt.to_upper = false;

    MenuItem mitems[] = {
        {MENU_LABEL, false, 0, 2, "Spot?",   NULL},                         // 0
        {MENU_LABEL, false, 0, 2, line1,                       NULL},         // 1
        {MENU_LABEL, false, 0, 2, line2,                       NULL},         // 2
        {MENU_LABEL, false, 0, 2, line3,                       NULL},         // 3
        {MENU_TEXT,  false, 0, 2, cm_mt.label,                 &cm_mt},       // 4
    };
    #define ONTASPOTMENU_N NARRAY(mitems)

    SBox menu_b = box;                          // copy, not ref!
    menu_b.x = box.x + 5;
    menu_b.y = box.y + SUBTITLE_Y0;
    menu_b.w = box.w - 10;
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, ONTASPOTMENU_N, mitems};
    if (runMenu (menu)) {
        menuMsg (menu_b, RA8875_WHITE, "spotting...");
        Message ynot;
        if (postONTASpot (s, comment, ynot)) {
            ontaMarkSpottedByMe (s);
            menuMsg (menu_b, RA8875_GREEN, "spot sent");
        } else {
            // menuMsg() just centers and prints -- it doesn't wrap or clip -- so clamp
            // the message ourselves. it may include text relayed verbatim from spot.pl
            // or POTA's own HTTP reason phrase, which we don't control the length of.
            char errbuf[100];
            quietStrncpy (errbuf, ynot.get(), sizeof(errbuf));
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            maxStringW (errbuf, menu_b.w > 10 ? menu_b.w - 10 : menu_b.w);
            menuMsg (menu_b, RA8875_RED, errbuf);
        }
    }

    // belt and suspenders: force a full, guaranteed-clean repaint of the whole pane
    // regardless of Ok/Cancel, rather than trusting the menu system's own backing-store
    // restore to have perfectly undone every pixel it touched
    drawONTAPane (box);
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
    int n_old = 0, n_no_org = 0, n_no_modeband = 0, n_no_wl = 0;
    for (int i = 0; i < n_ontaspots; i++) {
        DXSpot &spot = onta_spots[i];
        if (spot.spotted < oldest)
            n_old++;
        else if (!isSpotOrgOk (spot))
            n_no_org++;
        else if (!ontaModeBandOk (spot))
            n_no_modeband++;
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

    Serial.printf ("ONTA: %d total - %d too-old - %d not-org - %d not-mode/band - %d not-WL = %d showing\n",
                    n_ontaspots, n_old, n_no_org, n_no_modeband, n_no_wl, onta_ss.n_data);

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

    // narrow layout needs BLANK, not IGNORE, for hidden bio -- see comment above the
    // narrow mitems array for why
    MenuFieldType bio_lbl_mft_n = show_bio_enabled ? MENU_LABEL : MENU_BLANK;
    MenuFieldType bio_yes_mft_n = show_bio_enabled ? MENU_1OFN : MENU_BLANK;
    MenuFieldType bio_no_mft_n  = show_bio_enabled ? MENU_1OFN : MENU_BLANK;

    // narrow "Data Pane" view (PANE_0) can't fit 3 columns -- use 2, more vertical, instead
    bool narrow = BOX_IS_PANE_0(box);

    SBox menu_b = box;                          // copy, not ref!
    menu_b.y = box.y + 7;
    if (!narrow && show_bio_enabled)
        menu_b.y -= 11;                          // one row height (MENU_RH in menu.cpp, not
                                                  // exported) -- 3-col layout is one row taller
                                                  // with Bio shown (its row isn't removed the
                                                  // way it is when Bio is off) -- shift up to
                                                  // compensate so it doesn't overlap the pane
    if (narrow) {
        // 2-col layout has plenty of slack (~104px content in a 139px pane) -- a small
        // inset stays comfortably clear of the map without risking any auto-widen
        menu_b.x = box.x + 5;
        menu_b.w = box.w - 10;
    } else {
        // 3-col layout's content is tighter than box.w-10 would allow; full width avoids
        // runMenu's own auto-widen growing the box off-center to the right
        menu_b.x = box.x;
        menu_b.w = box.w;
    }
    SBox ok_b;
    bool ok;

    if (narrow) {

        MenuItem mitems[] = {
            // column 1: Age/Sort first so they always start at the top row regardless of
            // whether Bio is shown; Bio (or blank padding if hidden) moved to the bottom
            {MENU_LABEL, false,                       0, 2,  "Age:", NULL},                         // 0
            {MENU_1OFN, onta_ages[0] == onta_age,     2, 12, onta_ages_str[0], NULL},               // 1
            {MENU_1OFN, onta_ages[1] == onta_age,     2, 12, onta_ages_str[1], NULL},               // 2
            {MENU_1OFN, onta_ages[2] == onta_age,     2, 12, onta_ages_str[2], NULL},               // 3
            {MENU_1OFN, onta_ages[3] == onta_age,     2, 12, onta_ages_str[3], NULL},               // 4
            {MENU_LABEL, false,                       0, 2,  "Sort:", NULL},                        // 5
            {MENU_1OFN, onta_sortby == ONTAS_AGE,     3, 12, onta_sorts[ONTAS_AGE].menu_name, NULL},// 6
            {MENU_1OFN, onta_sortby == ONTAS_BAND,    3, 12, onta_sorts[ONTAS_BAND].menu_name,NULL},// 7
            {MENU_1OFN, onta_sortby == ONTAS_CALL,    3, 12, onta_sorts[ONTAS_CALL].menu_name,NULL},// 8
            {MENU_1OFN, onta_sortby == ONTAS_ORG,     3, 12, onta_sorts[ONTAS_ORG].menu_name, NULL},// 9
            {bio_lbl_mft_n, false,                    0, 2,  "Bio:", NULL},                         // 10
            {bio_yes_mft_n, onta_showbio,              1, 12, "Yes", NULL},                          // 11
            {bio_no_mft_n, !onta_showbio,              1, 12, "No", NULL},                           // 12
            {MENU_BLANK, false,                       0, 2,  NULL, NULL},                           // 13
            {MENU_BLANK, false,                       0, 2,  NULL, NULL},                           // 14
            {MENU_BLANK, false,                       0, 2,  NULL, NULL},                           // 15
            {MENU_BLANK, false,                       0, 2,  NULL, NULL},                           // 16

            // column 2: modes then bands, each with its own header label, stacked vertically
            {MENU_LABEL, false,                       0, 2,  "Modes:", NULL},                       // 17
            {MENU_AL1OFN, (onta_modes&ONTAMB_CW)!=0,  6, 2,  "CW",  NULL},                          // 18
            {MENU_AL1OFN, (onta_modes&ONTAMB_FM)!=0,  6, 2,  "FM",  NULL},                          // 19
            {MENU_AL1OFN, (onta_modes&ONTAMB_FT8)!=0, 6, 2,  "FT8", NULL},                          // 20
            {MENU_AL1OFN, (onta_modes&ONTAMB_SSB)!=0, 6, 2,  "SSB", NULL},                          // 21
            {MENU_AL1OFN, (onta_modes&ONTAMB_PHONE)!=0,6,2,  "PHONE", NULL},                        // 22
            {MENU_AL1OFN, (onta_modes&ONTAMB_DATA)!=0, 6, 2, "DATA",  NULL},                        // 23
            {MENU_LABEL, false,                       0, 2,  "Bands:", NULL},                       // 24
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_40M),  7, 12, findBandName(HAMBAND_40M),  NULL},     // 25
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_20M),  7, 12, findBandName(HAMBAND_20M),  NULL},     // 26
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_30M),  7, 12, findBandName(HAMBAND_30M),  NULL},     // 27
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_17M),  7, 12, findBandName(HAMBAND_17M),  NULL},     // 28
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_15M),  7, 12, findBandName(HAMBAND_15M),  NULL},     // 29
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_12M),  7, 12, findBandName(HAMBAND_12M),  NULL},     // 30
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_10M),  7, 12, findBandName(HAMBAND_10M),  NULL},     // 31
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_6M),   7, 12, findBandName(HAMBAND_6M),   NULL},     // 32
            {MENU_AL1OFN, (onta_bands&ONTA_BAND_OTHER)!=0, 7, 12, "Other", NULL},                   // 33

            // text fields across the bottom
            {MENU_TEXT, false,                        4, 2, org_mt.label, &org_mt},                 // 34
            {MENU_TEXT, false,                        5, 2, wl_mt.label, &wl_mt},                   // 35
        };
        #define ONTAMENU_NARROW_N   NARRAY(mitems)

        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 2, ONTAMENU_NARROW_N, mitems};
        ok = runMenu (menu);
        if (ok) {
            onta_showbio = mitems[11].set;

            if (mitems[1].set)       onta_age = onta_ages[0];
            else if (mitems[2].set)  onta_age = onta_ages[1];
            else if (mitems[3].set)  onta_age = onta_ages[2];
            else if (mitems[4].set)  onta_age = onta_ages[3];
            else fatalError ("runONTASortMenu no age set (narrow)");

            if (mitems[6].set)       onta_sortby = ONTAS_AGE;
            else if (mitems[7].set)  onta_sortby = ONTAS_BAND;
            else if (mitems[8].set)  onta_sortby = ONTAS_CALL;
            else if (mitems[9].set)  onta_sortby = ONTAS_ORG;
            else fatalError ("runONTASortMenu no sort set (narrow)");

            onta_modes = 0;
            if (mitems[18].set) onta_modes |= ONTAMB_CW;
            if (mitems[19].set) onta_modes |= ONTAMB_FM;
            if (mitems[20].set) onta_modes |= ONTAMB_FT8;
            if (mitems[21].set) onta_modes |= ONTAMB_SSB;
            if (mitems[22].set) onta_modes |= ONTAMB_PHONE;
            if (mitems[23].set) onta_modes |= ONTAMB_DATA;

            onta_bands = 0;
            if (mitems[25].set) SET_ONTABAND(HAMBAND_40M);
            if (mitems[26].set) SET_ONTABAND(HAMBAND_20M);
            if (mitems[27].set) SET_ONTABAND(HAMBAND_30M);
            if (mitems[28].set) SET_ONTABAND(HAMBAND_17M);
            if (mitems[29].set) SET_ONTABAND(HAMBAND_15M);
            if (mitems[30].set) SET_ONTABAND(HAMBAND_12M);
            if (mitems[31].set) SET_ONTABAND(HAMBAND_10M);
            if (mitems[32].set) SET_ONTABAND(HAMBAND_6M);
            if (mitems[33].set) onta_bands |= ONTA_BAND_OTHER;
        }

    } else {

        MenuItem mitems[] = {
            // column 1
            {bio_lbl_mft, false,                      0, 2, "Bio:", NULL},                          // 0
            {MENU_LABEL, false,                       0, 2, "Age:", NULL},                          // 1
            {MENU_BLANK, false,                       0, 2, NULL, NULL},                            // 2
            {MENU_LABEL, false,                       0, 2, "Sort:", NULL},                         // 3
            {MENU_BLANK, false,                       0, 2, NULL, NULL},                            // 4
            {MENU_AL1OFN, (onta_modes&ONTAMB_CW)!=0,  6, 2,  "CW",  NULL},                          // 5
            {MENU_AL1OFN, (onta_modes&ONTAMB_FM)!=0,  6, 2,  "FM",  NULL},                          // 6
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_40M),  7, 12, findBandName(HAMBAND_40M),  NULL},     // 7
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_20M),  7, 12, findBandName(HAMBAND_20M),  NULL},     // 8
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_30M),  7, 12, findBandName(HAMBAND_30M),  NULL},     // 9

            // column 2
            {bio_yes_mft, onta_showbio,               1, 2, "Yes", NULL},                           // 10
            {MENU_1OFN, onta_ages[0] == onta_age,     2, 2, onta_ages_str[0], NULL},                // 11
            {MENU_1OFN, onta_ages[2] == onta_age,     2, 2, onta_ages_str[2], NULL},                // 12
            {MENU_1OFN, onta_sortby == ONTAS_AGE,     3, 2, onta_sorts[ONTAS_AGE].menu_name, NULL}, // 13
            {MENU_1OFN, onta_sortby == ONTAS_BAND,    3, 2, onta_sorts[ONTAS_BAND].menu_name,NULL}, // 14
            {MENU_AL1OFN, (onta_modes&ONTAMB_FT8)!=0, 6, 2,  "FT8", NULL},                          // 15
            {MENU_AL1OFN, (onta_modes&ONTAMB_SSB)!=0, 6, 2,  "SSB", NULL},                          // 16
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_17M),  7, 12, findBandName(HAMBAND_17M),  NULL},     // 17
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_15M),  7, 12, findBandName(HAMBAND_15M),  NULL},     // 18
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_12M),  7, 12, findBandName(HAMBAND_12M),  NULL},     // 19

            // columns 3
            {bio_no_mft, !onta_showbio,               1, 2, "No", NULL},                            // 20
            {MENU_1OFN, onta_ages[1] == onta_age,     2, 2, onta_ages_str[1], NULL},                // 21
            {MENU_1OFN, onta_ages[3] == onta_age,     2, 2, onta_ages_str[3], NULL},                // 22
            {MENU_1OFN, onta_sortby == ONTAS_CALL,    3, 2, onta_sorts[ONTAS_CALL].menu_name,NULL}, // 23
            {MENU_1OFN, onta_sortby == ONTAS_ORG,     3, 2, onta_sorts[ONTAS_ORG].menu_name, NULL}, // 24
            {MENU_AL1OFN, (onta_modes&ONTAMB_PHONE)!=0,6, 2, "PHONE", NULL},                        // 25
            {MENU_AL1OFN, (onta_modes&ONTAMB_DATA)!=0, 6, 2, "DATA",  NULL},                        // 26
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_10M),  7, 12, findBandName(HAMBAND_10M),  NULL},     // 27
            {MENU_AL1OFN, TST_ONTABAND(HAMBAND_6M),   7, 12, findBandName(HAMBAND_6M),   NULL},     // 28
            {MENU_AL1OFN, (onta_bands&ONTA_BAND_OTHER)!=0, 7, 12, "Other", NULL},                   // 29

            // text fields across the bottom
            {MENU_TEXT, false,                        4, 2, org_mt.label, &org_mt},                 // 30
            {MENU_TEXT, false,                        5, 2, wl_mt.label, &wl_mt},                   // 31
        };
        #define ONTAMENU_N   NARRAY(mitems)

        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 3, ONTAMENU_N, mitems};
        ok = runMenu (menu);
        if (ok) {
            onta_showbio = mitems[10].set;

            if (mitems[11].set)
                onta_age = onta_ages[0];
            else if (mitems[12].set)
                onta_age = onta_ages[2];
            else if (mitems[21].set)
                onta_age = onta_ages[1];
            else if (mitems[22].set)
                onta_age = onta_ages[3];
            else
                fatalError ("runONTASortMenu no age set");

            if (mitems[13].set)
                onta_sortby = ONTAS_AGE;
            else if (mitems[14].set)
                onta_sortby = ONTAS_BAND;
            else if (mitems[23].set)
                onta_sortby = ONTAS_CALL;
            else if (mitems[24].set)
                onta_sortby = ONTAS_ORG;
            else
                fatalError ("runONTASortMenu no sort set");

            onta_modes = 0;
            if (mitems[5].set)  onta_modes |= ONTAMB_CW;
            if (mitems[6].set)  onta_modes |= ONTAMB_FM;
            if (mitems[15].set) onta_modes |= ONTAMB_FT8;
            if (mitems[16].set) onta_modes |= ONTAMB_SSB;
            if (mitems[25].set) onta_modes |= ONTAMB_PHONE;
            if (mitems[26].set) onta_modes |= ONTAMB_DATA;

            onta_bands = 0;
            if (mitems[7].set)  SET_ONTABAND(HAMBAND_40M);
            if (mitems[8].set)  SET_ONTABAND(HAMBAND_20M);
            if (mitems[9].set)  SET_ONTABAND(HAMBAND_30M);
            if (mitems[17].set) SET_ONTABAND(HAMBAND_17M);
            if (mitems[18].set) SET_ONTABAND(HAMBAND_15M);
            if (mitems[19].set) SET_ONTABAND(HAMBAND_12M);
            if (mitems[27].set) SET_ONTABAND(HAMBAND_10M);
            if (mitems[28].set) SET_ONTABAND(HAMBAND_6M);
            if (mitems[29].set) onta_bands |= ONTA_BAND_OTHER;
        }
    }

    if (ok) {

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

/* (re)load the park/summit reference -> 2-letter state/province lookup, if due.
 * cheap and safe to call every time retrieveONTA() runs: openCachedFile() enforces
 * ONTA_PARKS_INTERVAL itself, so this is a no-op download-wise most of the time,
 * and the file is small (only currently-active references) so re-parsing it each
 * call is negligible. a missing/failed/empty file is not an error -- it just means
 * onta_park_states stays empty and every row falls back to showing its country.
 */
static void retrieveONTAParks (void)
{
    FILE *fp = openCachedFile (onta_parks_file, onta_parks_page, ONTA_PARKS_INTERVAL, 0);
    if (!fp)
        return;

    onta_park_states.clear();

    char line[50];
    while (fgets (line, sizeof(line), fp)) {
        chompString (line);
        if (line[0] == '#' || line[0] == '\0')
            continue;
        char ref[20], state[8];                          // N.B. match sscanf fields
        if (sscanf (line, "%19[^,],%7s", ref, state) == 2)
            onta_park_states[ref] = state;
    }

    fclose (fp);

    Serial.printf ("ONTA: read %d park states\n", (int)onta_park_states.size());
}

/* download one onta.txt-schema source and append its spots onto the (already allocated,
 * possibly non-empty) onta_spots/n_ontaspots pair. return whether io ok, even if no data --
 * same "ok" semantics retrieveONTA() used to have for its single source.
 */
static bool retrieveONTASource (const ONTASource &src)
{
    FILE *fp = openCachedFile (src.file, src.page, ONTA_INTERVAL, 0);
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
        fclose (fp);
    }

    // done
    Serial.printf ("ONTA: %s: read %d spots so far\n", src.file, n_ontaspots);

    // result
    return (ok);
}

/* download all spots from every onta.txt-schema source (onta.txt itself, plus IOTA's
 * separate iota_spots.txt) into the one onta_spots array.
 * return whether io ok, even if no data -- true so long as at least one source loaded ok,
 * so eg a transient hiccup fetching iota_spots.txt doesn't blank out real POTA/SOTA/WWFF data.
 */
static bool retrieveONTA (void)
{
    // reset
    free (onta_spots);
    onta_spots = NULL;
    n_ontaspots = 0;

    bool any_ok = false;
    for (size_t i = 0; i < N_ONTASOURCES; i++)
        if (retrieveONTASource (onta_sources[i]))
            any_ok = true;

    Serial.printf ("ONTA: read %d total spots from %d source(s)\n", n_ontaspots, (int)N_ONTASOURCES);

    // refresh the park->state lookup too -- independent of the outcome above
    retrieveONTAParks();

    // result
    return (any_ok);
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

    uint32_t prev_hash = spots_hash;
    bool ok = retrieveONTA();
    if (ok) {
        spots_hash = spotsHash (onta_spots, n_ontaspots);
        if (onta_ss.atNewest()) {
            bool rotated_org = false;
            if (onta_norgs > 0 && !onta_merge) {
                // rotate to next org
                next_ontaorg = (next_ontaorg + 1) % onta_norgs; // rotate org
                rotated_org = true;
                Serial.printf ("ONTA: now showing %s\n", onta_orgs[next_ontaorg]);
            }
            rebuildONTAWatchList();
            onta_ss.drawNewSpotsSymbol (false, false);                  // New symbol off
            ROTHOLD_CLR(PLOT_CH_ONTA);                                  // release rotation hold
            drawONTAPane (box);
            if (findPaneForChoice(PLOT_CH_ONTA) != PANE_NONE
                    && (fresh || rotated_org || spots_hash != prev_hash))
                scheduleMapRedraw();
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
 * a normal tap (TT_TAP) on a row engages it as before (QSY/bio/newDX); the secondary
 * tap (TT_TAP_BX, eg right-click) on a row instead offers to (re)spot it, a la Hunterlog.
 * return if something for us, else false to mean op wants to change the Pane option.
 */
bool checkOnTheAirTouch (TouchType tt, const SCoord &s, const SBox &box)
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

    // tapped a row: secondary tap offers to (re)spot; a normal tap on the callsign
    // column specifically (only when it's actually truncated on screen) shows the
    // full callsign as a tooltip instead of engaging; normal tap elsewhere engages
    // exactly as before
    int spot_row;
    int vis_row = (s.y - (box.y + LISTING_Y0))/LISTING_DY;
    if (onta_ss.findDataIndex (vis_row, spot_row)) {
        DXSpot &sp = ontawl_spots[spot_row];
        if (tt == TT_TAP_BX) {
            runONTASpotMenu (box, sp);
        } else {
            int call_len = BOX_IS_PANE_0(box) ? ONTA_P0_CALL_LEN : ONTA_PX_CALL_LEN;
            bool shown = false;
            if ((int)strlen(sp.tx_call) > call_len) {
                int freq_len = BOX_IS_PANE_0(box) ? ONTA_P0_FREQ_LEN : ONTA_PX_FREQ_LEN;
                uint16_t call_x0 = box.x + 1 + freq_len*ONTA_CHAR_W;
                uint16_t call_x1 = call_x0 + call_len*ONTA_CHAR_W;
                if (s.x >= call_x0 && s.x < call_x1) {
                    tooltip (s, sp.tx_call);
                    shown = true;
                }
            }
            if (!shown)
                engageONTARow (sp);
        }
    }

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
