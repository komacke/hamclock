/* handle the "Nearby APRS" pane.
 *
 * Connects to a user-configured APRS-IS server (Setup: host, port and search radius, same style
 * as the DX Cluster host/port config) using a location-radius login filter so the server only
 * sends us traffic from stations within range. Every heard station is kept in a small list, sorted
 * by distance from DE, and shown as CALL / DIR / DISTANCE / SYMBOL similar to the DX Cluster pane.
 * Decodes uncompressed, compressed, and Mic-E position formats -- see the parsing section below.
 *
 * Radius is entered in Setup using whatever distance unit the user has chosen there (miles for
 * Imperial and British, km for Metric -- see showDistKm()) but is stored canonically in statute
 * miles; we convert to km only when building the APRS-IS filter spec, since that's what the
 * protocol expects.
 *
 * APRS-IS reference: http://www.aprs-is.net/Connecting.aspx and http://www.aprs-is.net/javAPRSFilter.aspx
 * Position formats: APRS101.pdf chapters 8 (uncompressed), 9 (compressed), and 10 (Mic-E).
 */

#include "HamClock.h"
#include "aprsicons.h"

/* broad categories every symbol (primary or alternate table) is bucketed into, so the pane can
 * offer a manageable "show/hide this whole category" filter instead of asking anyone to pick
 * through 145+ individual symbols one at a time.
 */
typedef enum {
    APRSCAT_VEHICLE,                    // cars, trucks, vans, bikes, motorcycles, RVs...
    APRSCAT_AIRMARINE,                  // aircraft, boats, ships, balloons...
    APRSCAT_WEATHER,                    // WX stations/sites, storms, clouds, rain...
    APRSCAT_EMERGENCY,                  // police/fire/ambulance, hazards, ICP, shelters...
    APRSCAT_DIGITAL,                    // digis, repeaters, gateways, D-Star/DMR, TNC...
    APRSCAT_INFRA,                      // servers, BBS, grid squares, computers, satellites...
    APRSCAT_PLACES,                     // houses, hotels, hospitals, shops, parks...
    APRSCAT_PEOPLE,                     // people, animals, scouts, wheelchairs...
    APRSCAT_OTHER,                      // everything left over, and anything unrecognized
    N_APRSCAT
} APRSCategory;

// short enough that 2 columns of these fit within even the narrowest pane (PLOTBOX0_W, 139px)
// without spilling into whatever pane sits to the right -- the enum comments above and the
// tooltip when filtering give the fuller picture for anyone who wants it.
static const char *aprs_cat_names[N_APRSCAT] = {
    "Vehicles", "Air/Boat", "Weather", "Emerg", "Digi/Rpt",
    "Infra/PC", "Places", "Ppl/Anml", "Other"
};

// bit i set means category i is SHOWN. all bits set (the default) means no filtering is in
// effect at all -- "Filter Off". persisted across restarts.
static uint16_t aprs_cat_filter = (1U << N_APRSCAT) - 1;
#define APRS_FILTER_IS_OFF()   (aprs_cat_filter == (uint16_t)((1U << N_APRSCAT) - 1))

/* load aprs_cat_filter from NVRAM the first time it's needed; cheap to call repeatedly after that.
 */
static void loadAPRSCatFilterOnce (void)
{
    static bool loaded;
    if (loaded)
        return;
    loaded = true;
    uint16_t v;
    if (!NVReadUInt16 (NV_APRSCATFILTER, &v) || v == 0 || v > ((1U << N_APRSCAT) - 1)) {
        v = (uint16_t)((1U << N_APRSCAT) - 1);      // default: everything shown, filter "Off"
        NVWriteUInt16 (NV_APRSCATFILTER, v);
    }
    aprs_cat_filter = v;
}

/* persist the current aprs_cat_filter -- caller is responsible for triggering a display rebuild
 */
static void saveAPRSCatFilter (void)
{
    NVWriteUInt16 (NV_APRSCATFILTER, aprs_cat_filter);
}

// title/accent color -- the same color the user has configured for the 2m ham band, since that's
// the one band virtually all APRS traffic actually runs on (144.390 MHz in the US, 144.800 in EU,
// etc.) -- ties the pane in with the rest of HamClock's band coloring without implying any
// per-station distinction, since there normally isn't one to make (see getBandColor()).
static uint16_t aprsColor (void)
{
    return (getMapColor (findColSel (HAMBAND_2M)));
}

// fixed colors, matching the storms pane's plain white title (STORM_COLOR_TITLE) rather than the
// user-configurable 2m band color above -- that band color can be quite dim/dark depending on how
// the user has their band colors set up, which made the title, CLR/FLT, the "new spots" symbol,
// and the scroll arrows all look washed-out/inactive next to other panes. All of these reuse this
// same fixed white so the pane reads as bright and active regardless of the user's 2m color choice.
// The error-state header color (RA8875_RED, used when not configured/connected) is unaffected.
#define APRS_COLOR_TITLE RA8875_WHITE
#define APRS_COLOR_BTN   RA8875_WHITE
#define APRS_COLOR_NEW   RA8875_WHITE   // "new spots" symbol, normal (non-error) state
#define APRS_COLOR_SCRL  RA8875_WHITE   // scroll up/down arrows

// timing
#define APRS_BGCHECK_DT  250            // background socket-check period, millis -- keep it snappy,
                                         // this is meant to feel "real time"
#define APRS_HBEAT_MS    (18*60*1000UL) // send a harmless comment if idle this long, keeps NAT/proxies open
#define APRS_STALE_DT    (2*60*60)      // drop a station if not heard again in this long, secs
#define MAX_APRS_SPOTS   200            // cap on simultaneously tracked stations

// Clear-list control, same proportions as the DX Cluster pane's CLR button
#define APRS_CLRBOX_DX   6              // clear control box left offset
#define APRS_CLRBOX_DY   6              // "  top offset
#define APRS_CLRBOX_W    20             // "  width
#define APRS_CLRBOX_H    11             // "  height
static SBox aprsclr_b;

#define APRS_FLTBOX_DX   APRS_CLRBOX_DX                     // filter control box left offset --
                                                             // same column as CLR/New, stacked below
#define APRS_FLTBOX_DY   (18+9+2)                           // below the "New spots" symbol (which
                                                             // itself sits at NEWSYM_DY=18, height 9,
                                                             // same column -- see scrollstate.cpp)
#define APRS_FLTBOX_W    APRS_CLRBOX_W  // "  width -- same as CLR so the two line up/match visually
#define APRS_FLTBOX_H    APRS_CLRBOX_H  // "  height
static SBox aprsflt_b;

// with the filter button now stacked below title/CLR/New on the left, the subtitle and the list
// itself both need to move down to clear it -- APRS uses its own Y offsets here rather than the
// shared SUBTITLE_Y0/LISTING_Y0 that other panes use, since this pane has two extra header rows.
#define APRS_SUBTITLE_Y  (APRS_FLTBOX_DY + APRS_FLTBOX_H + 4)   // host:port row
#define APRS_LIST_Y0     (APRS_SUBTITLE_Y + 14)                 // column header row / list top

// connection
static WiFiClient aprs_client;
static uint32_t aprs_activity_ms;       // millis() of last socket activity, for heartbeat timing

// data -- two lists, same split as DX Cluster's dxc_spots/dxwl_spots:
//   aprs_spots   -- master list, updated live in the background regardless of scroll position
//   aprs_disp    -- sorted COPY actually shown/scrolled, only refreshed while at the newest entry so
//                   a user paging back through history doesn't have the list shift under them
static APRSSpot *aprs_spots;            // malloced master list
static int n_aprs_spots;
static APRSSpot *aprs_disp;             // malloced display copy; count is aprs_ss.n_data
static ScrollState aprs_ss;
static bool aprs_list_changed;          // set when aprs_spots contents change, to trigger resort/redraw
static time_t aprs_last_change_tm;      // myNow() of the most recent change to aprs_spots
static time_t aprs_scrolledaway_tm;     // myNow() when user scrolled away from newest, else 0

// simple header row drawn just above the scrollable list
#define APRS_HDR_DY      LISTING_DY     // header consumes one row's worth of space

#if defined(__GNUC__)
static void showAPRSClusterErr (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void showAPRSClusterErr (const char *fmt, ...);
#endif


/* log with our own prefix
 */
static void aprsLog (const char *fmt, ...)
{
    char msg[200];
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);
    chompString (msg);
    Serial.printf ("APRS: %s\n", msg);
}

/* report an error, log it and close the connection
 */
static void showAPRSClusterErr (const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start (ap, fmt);
    size_t ml = snprintf (buf, sizeof(buf), "APRS error: ");
    vsnprintf (buf+ml, sizeof(buf)-ml, fmt, ap);
    va_end (ap);
    mapMsg (3000, "%s", buf);
    aprsLog ("%s", buf);

    aprs_client.stop();
}

/* return whether we currently have a live connection
 */
bool isAPRSClusterConnected (void)
{
    return (useAPRSCluster() && aprs_client);
}

/* free spot lists and reset counts, ready for a fresh connection
 */
static void resetAPRSMem (void)
{
    free (aprs_spots);
    aprs_spots = NULL;
    n_aprs_spots = 0;
    free (aprs_disp);
    aprs_disp = NULL;
    aprs_ss.n_data = 0;
    aprs_list_changed = true;
    aprs_last_change_tm = 0;
    aprs_scrolledaway_tm = 0;
}

/* ***********************************************************************************************
 * APRS primary symbol table (table char '/') lookup -- short human-friendly names for the pane's
 * SYMBOL column. This is not exhaustive -- APRS defines well over a hundred codes across the
 * primary and alternate ('\') tables plus overlays -- just the ones most likely to show up on a
 * typical local monitor. Anything not listed here, or from the alternate table, falls back to "-".
 */
typedef struct {
    char code;
    const char *name;
    uint8_t cat;                        // APRSCategory this symbol belongs to, for the pane's filter
} APRSSymDef;

static const APRSSymDef aprs_primary_syms[] = {
    { '>', "CAR", APRSCAT_VEHICLE },
    { '-', "HOUSE", APRSCAT_PLACES },
    { '_', "WX", APRSCAT_WEATHER },
    { 'k', "TRUCK", APRSCAT_VEHICLE },
    { 'b', "BIKE", APRSCAT_VEHICLE },
    { 'v', "VAN", APRSCAT_VEHICLE },
    { 'R', "RV", APRSCAT_VEHICLE },
    { 'j', "JEEP", APRSCAT_VEHICLE },
    { 'f', "FIRE", APRSCAT_EMERGENCY },
    { 'a', "AMBUL", APRSCAT_EMERGENCY },
    { 'U', "BUS", APRSCAT_VEHICLE },
    { 'Y', "BOAT", APRSCAT_AIRMARINE },
    { 'O', "BALLOON", APRSCAT_AIRMARINE },
    { 'g', "GLIDER", APRSCAT_AIRMARINE },
    { '\'', "PLANE", APRSCAT_AIRMARINE },
    { '^', "AIRCRFT", APRSCAT_AIRMARINE },
    { 'r', "REPEATR", APRSCAT_DIGITAL },
    { '#', "DIGI", APRSCAT_DIGITAL },
    { '$', "PHONE", APRSCAT_DIGITAL },
    { 'P', "POLICE", APRSCAT_EMERGENCY },
    { 'W', "WX-SITE", APRSCAT_WEATHER },
    { 'x', "HELO", APRSCAT_AIRMARINE },
    { '9', "GAS", APRSCAT_PLACES },
    { '[', "PERSON", APRSCAT_PEOPLE },                  // seen from MMDVM/D-Star gateways in the wild
    { 'E', "EYEBALL", APRSCAT_OTHER },                 // per aprs.org spec: "EYEBALL (Events, etc!)" -- corrected
                                         // from an earlier wrong guess based on a secondary source
    { '!', "POLICE", APRSCAT_EMERGENCY },
    { '%', "DXCLSTR", APRSCAT_DIGITAL },
    { '&', "GATEWAY", APRSCAT_DIGITAL },
    { '(', "MOBSAT", APRSCAT_DIGITAL },
    { ')', "WHEELCHR", APRSCAT_PEOPLE },
    { '*', "SNOMOBIL", APRSCAT_VEHICLE },
    { '+', "REDCROSS", APRSCAT_EMERGENCY },
    { ',', "SCOUTS", APRSCAT_PEOPLE },
    { '.', "X", APRSCAT_OTHER },
    { '/', "DOT", APRSCAT_OTHER },
    { ':', "FIRE", APRSCAT_EMERGENCY },
    { ';', "CAMPGRND", APRSCAT_PLACES },
    { '<', "MOTORCYC", APRSCAT_VEHICLE },
    { '=', "RAILROAD", APRSCAT_VEHICLE },
    { '?', "SERVER", APRSCAT_INFRA },
    { 'A', "AID-STN", APRSCAT_EMERGENCY },
    { 'B', "BBS", APRSCAT_INFRA },
    { 'C', "CANOE", APRSCAT_AIRMARINE },
    { 'F', "TRACTOR", APRSCAT_VEHICLE },
    { 'G', "GRIDSQ", APRSCAT_INFRA },
    { 'q', "GRIDSQ", APRSCAT_INFRA },
    { 'H', "HOTEL", APRSCAT_PLACES },
    { 'I', "TCPIP", APRSCAT_DIGITAL },
    { 'K', "SCHOOL", APRSCAT_PLACES },
    { 'L', "PC-USER", APRSCAT_INFRA },
    { 'M', "MACAPRS", APRSCAT_INFRA },
    { 'N', "NTS", APRSCAT_DIGITAL },
    { 'S', "SHUTTLE", APRSCAT_AIRMARINE },
    { 'T', "SSTV", APRSCAT_DIGITAL },
    { 'V', "ATV", APRSCAT_VEHICLE },
    { 'Z', "WINAPRS", APRSCAT_INFRA },
    { '\\', "TRIANGLE", APRSCAT_OTHER },
    { ']', "MAIL", APRSCAT_PLACES },
    { '`', "DISH", APRSCAT_DIGITAL },
    { 'c', "ICP", APRSCAT_EMERGENCY },
    { 'd', "FIREDEPT", APRSCAT_EMERGENCY },
    { 'e', "HORSE", APRSCAT_PEOPLE },
    { 'h', "HOSPITAL", APRSCAT_PLACES },
    { 'i', "IOTA", APRSCAT_OTHER },
    { 'l', "LAPTOP", APRSCAT_INFRA },
    { 'm', "MIC-E-RPT", APRSCAT_DIGITAL },
    { 'n', "NODE", APRSCAT_DIGITAL },
    { 'o', "EOC", APRSCAT_EMERGENCY },
    { 'p', "ROVER", APRSCAT_PEOPLE },
    { 's', "SHIP", APRSCAT_AIRMARINE },
    { 't', "TRK-STOP", APRSCAT_PLACES },
    { 'u', "SEMI", APRSCAT_VEHICLE },
    { 'w', "H2O", APRSCAT_PLACES },
    { 'y', "YAGI", APRSCAT_DIGITAL },
    { '|', "TNC-SS", APRSCAT_DIGITAL },
    { '~', "TNC-SS", APRSCAT_DIGITAL },
};
#define N_APRS_SYMS  NARRAY(aprs_primary_syms)

/* ---------------------------------------------------------------------------------------------
 * ALTERNATE symbol table ('\' or any digit/uppercase-letter overlay char, per aprs.org spec).
 * This is what most D-Star/DMR/MMDVM gateways and mobile igates in the wild actually use, so
 * skipping it (as an earlier version of this file did) misses a large fraction of real traffic
 * in areas with heavy digital-voice activity. The overlay character itself (when present, ie
 * table is a digit/letter rather than a literal '\') is NOT part of what selects the symbol --
 * it's a badge drawn on top in real APRS clients -- so we only use it to detect "yes, alternate
 * table" and otherwise ignore it; the actual symbol comes from code alone, same table for all.
 * Skipping the same handful of TBD/reserved/AVAIL slots as the primary table.
 * ------------------------------------------------------------------------------------------- */
static const APRSSymDef aprs_alt_syms[] = {
    { '!', "EMERGENCY", APRSCAT_EMERGENCY },
    { '#', "DIGI", APRSCAT_DIGITAL },
    { '$', "BANK", APRSCAT_PLACES },
    { '%', "PWRPLANT", APRSCAT_PLACES },
    { '&', "GATEWAY", APRSCAT_DIGITAL },
    { '\'', "CRASH", APRSCAT_EMERGENCY },
    { '(', "CLOUDY", APRSCAT_WEATHER },
    { ')', "FIRENET", APRSCAT_EMERGENCY },
    { '+', "CHURCH", APRSCAT_PLACES },
    { ',', "GIRLSCTS", APRSCAT_PEOPLE },
    { '-', "HOUSE", APRSCAT_PLACES },
    { '.', "AMBIG", APRSCAT_OTHER },
    { '/', "WAYPT", APRSCAT_OTHER },
    { '9', "GAS", APRSCAT_PLACES },
    { ';', "PARK", APRSCAT_PLACES },
    { '<', "ADVISORY", APRSCAT_WEATHER },
    { '>', "VEHICLE", APRSCAT_VEHICLE },
    { '?', "INFO", APRSCAT_INFRA },
    { '@', "HURRICANE", APRSCAT_WEATHER },
    { 'A', "DTMF/RFID", APRSCAT_DIGITAL },
    { 'C', "COASTGRD", APRSCAT_AIRMARINE },
    { 'D', "DEPOT", APRSCAT_PLACES },
    { 'E', "SMOKE", APRSCAT_EMERGENCY },
    { 'H', "HAZARD", APRSCAT_EMERGENCY },
    { 'I', "RAINSHWR", APRSCAT_WEATHER },
    { 'K', "KENWOOD", APRSCAT_DIGITAL },
    { 'L', "LIGHTHSE", APRSCAT_AIRMARINE },
    { 'M', "MARS", APRSCAT_DIGITAL },
    { 'N', "NAVBUOY", APRSCAT_AIRMARINE },
    { 'O', "BALLOON", APRSCAT_AIRMARINE },
    { 'P', "PARKING", APRSCAT_PLACES },
    { 'Q', "QUAKE", APRSCAT_EMERGENCY },
    { 'R', "RESTRNT", APRSCAT_PLACES },
    { 'S', "SATELITE", APRSCAT_INFRA },
    { 'T', "TSTORM", APRSCAT_WEATHER },
    { 'U', "SUNNY", APRSCAT_WEATHER },
    { 'V', "VORTAC", APRSCAT_AIRMARINE },
    { 'W', "NWS-SITE", APRSCAT_WEATHER },
    { 'X', "PHARMACY", APRSCAT_PLACES },
    { 'Y', "RADIO", APRSCAT_DIGITAL },
    { '[', "W.CLOUD", APRSCAT_WEATHER },
    { '\\', "GPS", APRSCAT_DIGITAL },
    { '^', "AIRCRFT", APRSCAT_AIRMARINE },
    { '_', "WX-SITE", APRSCAT_WEATHER },
    { '`', "RAIN", APRSCAT_WEATHER },
    { 'a', "DSTAR-ETC", APRSCAT_DIGITAL },
    { 'c', "RACES", APRSCAT_EMERGENCY },
    { 'd', "DXSPOT", APRSCAT_DIGITAL },
    { 'e', "SLEET", APRSCAT_WEATHER },
    { 'f', "FUNNEL", APRSCAT_WEATHER },
    { 'g', "GALE", APRSCAT_WEATHER },
    { 'h', "HAMFEST", APRSCAT_PLACES },
    { 'i', "POI", APRSCAT_PLACES },
    { 'j', "WORKZONE", APRSCAT_PLACES },
    { 'k', "SUV/4X4", APRSCAT_VEHICLE },
    { 'l', "AREA", APRSCAT_OTHER },
    { 'm', "SIGN", APRSCAT_OTHER },
    { 'n', "TRIANGLE", APRSCAT_OTHER },
    { 'o', "CIRCLE", APRSCAT_OTHER },
    { 'r', "RESTROOM", APRSCAT_PLACES },
    { 's', "SHIP", APRSCAT_AIRMARINE },
    { 't', "TORNADO", APRSCAT_WEATHER },
    { 'u', "TRUCK", APRSCAT_VEHICLE },
    { 'v', "VAN", APRSCAT_VEHICLE },
    { 'w', "FLOODING", APRSCAT_WEATHER },
    { 'x', "WRECK", APRSCAT_EMERGENCY },
    { 'y', "SKYWARN", APRSCAT_WEATHER },
    { 'z', "SHELTER", APRSCAT_EMERGENCY },
    { '|', "TNC-SS", APRSCAT_DIGITAL },
    { '~', "TNC-SS", APRSCAT_DIGITAL },
};
#define N_APRS_ALT_SYMS  NARRAY(aprs_alt_syms)

/* look up a short name for the given symbol table+code, else "-"
 */
static const char *aprsSymbolName (char table, char code)
{
    if (table == '/') {
        for (size_t i = 0; i < N_APRS_SYMS; i++)
            if (aprs_primary_syms[i].code == code)
                return (aprs_primary_syms[i].name);
    } else if (table == '\\' || isdigit((unsigned char)table) ||
               (table >= 'A' && table <= 'Z')) {
        for (size_t i = 0; i < N_APRS_ALT_SYMS; i++)
            if (aprs_alt_syms[i].code == code)
                return (aprs_alt_syms[i].name);
    }
    return ("-");
}

/* look up the APRSCategory for the given symbol table+code, for the pane's category filter.
 * unrecognized codes -- including the whole alternate table when it's not actually in use, and
 * anything genuinely unlisted -- fall into APRSCAT_OTHER rather than being unfilterable.
 */
static APRSCategory aprsSymbolCategory (char table, char code)
{
    if (table == '/') {
        for (size_t i = 0; i < N_APRS_SYMS; i++)
            if (aprs_primary_syms[i].code == code)
                return ((APRSCategory) aprs_primary_syms[i].cat);
    } else if (table == '\\' || isdigit((unsigned char)table) ||
               (table >= 'A' && table <= 'Z')) {
        for (size_t i = 0; i < N_APRS_ALT_SYMS; i++)
            if (aprs_alt_syms[i].code == code)
                return ((APRSCategory) aprs_alt_syms[i].cat);
    }
    return (APRSCAT_OTHER);
}

/* return a short (max 2 char) compass abbreviation for the given TRUE OR MAGNETIC bearing,
 * already resolved to whichever the user prefers -- see desiredBearing()
 */
static const char *compassAbbr (float deg)
{
    static const char *dirs[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    deg = fmodf (deg + 360.0F, 360.0F);
    int idx = ((int)((deg + 22.5F) / 45.0F)) % 8;
    return (dirs[idx]);
}

/* ***********************************************************************************************
 * spot list management
 */

/* find the given call in aprs_spots[], else -1
 */
static int findAPRSSpot (const char *call)
{
    for (int i = 0; i < n_aprs_spots; i++)
        if (strcasecmp (aprs_spots[i].call, call) == 0)
            return (i);
    return (-1);
}

/* everything a successful position decode can produce beyond the bare lat/lon -- bundled up so
 * addOrUpdateAPRSSpot() doesn't need a dozen parameters. Zero-initialize and fill in whatever the
 * particular packet format/content actually provided; unset fields keep their false/-1 defaults.
 */
struct APRSExtra {
    char comment[MAX_APRSCOMMENT_LEN] = "";
    bool has_course_speed = false;
    float course_deg = 0, speed_mph = 0;
    bool has_alt = false;
    float alt_ft = 0;
    bool has_wx = false;
    float wx_temp_f = 0;
    int wx_humidity = -1;
    float wx_wind_mph = -1, wx_gust_mph = -1, wx_baro_mb = -1;
};

/* add or update the entry for call.
 * has_pos/ll/symbol/extra are only applied if has_pos is true -- a station heard without a
 * position report keeps whatever position (if any) it already had.
 */
static void addOrUpdateAPRSSpot (const char *call, bool has_pos, LatLong ll, const char *symbol,
                                  char table, char code, const APRSExtra &extra)
{
    int i = findAPRSSpot (call);
    if (i < 0) {
        if (debugLevel (DEBUG_APRS, 1))
            aprsLog ("new station %s (%d total)\n", call, n_aprs_spots+1);
        // new station -- room?
        if (n_aprs_spots >= MAX_APRS_SPOTS) {
            // evict the least-recently-heard entry to make room
            int oldest = 0;
            for (int j = 1; j < n_aprs_spots; j++)
                if (aprs_spots[j].heard < aprs_spots[oldest].heard)
                    oldest = j;
            memmove (&aprs_spots[oldest], &aprs_spots[oldest+1],
                     (n_aprs_spots-oldest-1) * sizeof(APRSSpot));
            n_aprs_spots--;
        }
        aprs_spots = (APRSSpot *) realloc (aprs_spots, (n_aprs_spots+1) * sizeof(APRSSpot));
        if (!aprs_spots)
            fatalError ("No mem for %d APRS spots", n_aprs_spots+1);
        i = n_aprs_spots++;
        aprs_spots[i] = APRSSpot();
        quietStrncpy (aprs_spots[i].call, call, sizeof(aprs_spots[i].call));
        aprs_spots[i].category = APRSCAT_OTHER;    // until/unless a real position sets it below
    } else if (debugLevel (DEBUG_APRS, 2)) {
        aprsLog ("update %s\n", call);
    }

    APRSSpot &sp = aprs_spots[i];
    sp.heard = myNow();
    if (has_pos) {
        sp.has_pos = true;
        sp.ll = ll;
        float dist_rad, bear_rad;
        propDEPath (false, ll, &dist_rad, &bear_rad);   // short path angular dist + true bearing
        sp.dist_mi = dist_rad * ERAD_M;
        sp.bear_deg = bear_rad * 180.0F/M_PIF;
        quietStrncpy (sp.symbol, symbol, sizeof(sp.symbol));
        sp.sym_table = table;
        sp.sym_code = code;
        sp.category = (uint8_t) aprsSymbolCategory (table, code);

        quietStrncpy (sp.comment, extra.comment, sizeof(sp.comment));
        sp.has_course_speed = extra.has_course_speed;
        sp.course_deg = extra.course_deg;
        sp.speed_mph = extra.speed_mph;
        sp.has_alt = extra.has_alt;
        sp.alt_ft = extra.alt_ft;
        sp.has_wx = extra.has_wx;
        sp.wx_temp_f = extra.wx_temp_f;
        sp.wx_humidity = extra.wx_humidity;
        sp.wx_wind_mph = extra.wx_wind_mph;
        sp.wx_gust_mph = extra.wx_gust_mph;
        sp.wx_baro_mb = extra.wx_baro_mb;
    }

    aprs_list_changed = true;
    aprs_last_change_tm = myNow();
}

/* remove any spot not heard within APRS_STALE_DT; return whether list changed
 */
static bool purgeStaleAPRSSpots (void)
{
    time_t oldest = myNow() - APRS_STALE_DT;
    bool changed = false;
    for (int i = n_aprs_spots; --i >= 0; ) {
        if (aprs_spots[i].heard < oldest) {
            memmove (&aprs_spots[i], &aprs_spots[i+1], (n_aprs_spots-i-1) * sizeof(APRSSpot));
            n_aprs_spots--;
            changed = true;
        }
    }
    return (changed);
}

/* qsort-style compare: positioned stations first, then either nearest-first (default) or
 * most-recently-heard-first depending on aprs_sort_recent -- toggled by tapping the KM/MI column
 * header; unpositioned stations always sort alphabetically by callsign, sort mode doesn't apply
 * to them since they have neither a distance nor a meaningful "recent" position to rank by.
 */
static bool aprs_sort_recent;                  // false=nearest first (default), true=most recent first
static int qsAPRSSpot (const void *p1, const void *p2)
{
    const APRSSpot *s1 = (const APRSSpot *) p1;
    const APRSSpot *s2 = (const APRSSpot *) p2;
    if (s1->has_pos != s2->has_pos)
        return (s1->has_pos ? -1 : 1);
    if (s1->has_pos) {
        if (aprs_sort_recent)
            return (s1->heard > s2->heard ? -1 : (s1->heard < s2->heard ? 1 : 0));
        return (s1->dist_mi < s2->dist_mi ? -1 : (s1->dist_mi > s2->dist_mi ? 1 : 0));
    }
    return (strcasecmp (s1->call, s2->call));
}

/* rebuild the displayed (sorted) list as a fresh copy of aprs_spots, and reposition the scroll
 * window at the newest entries. N.B. only call this while aprs_ss.atNewest() -- see updateAPRSCluster()
 * -- so a user scrolled back through history never has the list shift under them; new arrivals
 * accumulate in aprs_spots regardless and are picked up whenever they return to newest.
 */
static void rebuildAPRSDisplay (void)
{
    purgeStaleAPRSSpots();

    aprs_disp = (APRSSpot *) realloc (aprs_disp, (n_aprs_spots ? n_aprs_spots : 1) * sizeof(APRSSpot));
    if (n_aprs_spots && !aprs_disp)
        fatalError ("No mem for %d APRS display spots", n_aprs_spots);

    int n_shown = 0;
    for (int i = 0; i < n_aprs_spots; i++)
        if (APRS_FILTER_IS_OFF() || (aprs_cat_filter & (1U << aprs_spots[i].category)))
            aprs_disp[n_shown++] = aprs_spots[i];
    qsort (aprs_disp, n_shown, sizeof(APRSSpot), qsAPRSSpot);

    aprs_ss.n_data = n_shown;
    aprs_ss.scrollToNewest();
    aprs_list_changed = false;
}

/* handy check whether we should show the New spots indicator: user is scrolled away from newest
 * AND something has arrived since they scrolled away.
 */
static bool showingNewAPRS (void)
{
    return (aprs_scrolledaway_tm > 0 && aprs_last_change_tm > aprs_scrolledaway_tm);
}

/* ***********************************************************************************************
 * APRS-IS packet parsing
 *
 * We decode three position formats:
 *    !DDMM.mmN/DDDMM.mmWsym...       uncompressed, no timestamp
 *    =DDMM.mmN/DDDMM.mmWsym...       uncompressed, no timestamp, with APRS messaging
 *    /DDHHMMzDDMM.mmN/DDDMM.mmWsym.. uncompressed, with timestamp
 *    @DDHHMMzDDMM.mmN/DDDMM.mmWsym.. uncompressed, with timestamp, with APRS messaging
 *    <same four, but compressed position instead of the DDMM.mm.. text form>
 *    Mic-E (destination-field-encoded lat/N-S/W-E, info field starting `\x60` or `'`)
 * across four packet shapes:
 *    plain position reports (above)
 *    ;OBJECT reports    (shown under the object's own name)
 *    )ITEM reports       (shown under the item's own name)
 *    }third-party-wrapped packets (unwrapped and parsed recursively)
 * Anything else (status, messages, telemetry, ...) is still recorded as "heard" so it shows in the
 * list, just without a usable position/symbol -- matching how a station with no position report
 * yet shows in the mockup (distance and symbol both "--"/"-").
 */

/* APRS allows "position ambiguity": trailing digits of the minutes fields may be replaced with
 * spaces to indicate reduced precision (eg "2815.  N" means only degrees and tens-of-minutes are
 * known). accept space as a legit digit character, worth 0, so these still produce a (less
 * precise) position instead of being rejected outright -- some real-world traffic in this area
 * uses it, eg local repeater/frequency objects with ambiguity intentionally left in.
 */
static inline bool aprsDigitOK (char c) { return isdigit((unsigned char)c) || c == ' '; }
static inline int aprsDigitVal (char c) { return c == ' ' ? 0 : (c - '0'); }

/* attempt to parse an uncompressed lat/lon + symbol starting at p, which must point to the first
 * digit of the latitude (8 chars: DDMM.mmX). return whether successful.
 */
static bool parseUncompressedPos (const char *p, LatLong &ll, char &table, char &code)
{
    if (strlen(p) < 19)
        return (false);

    // latitude: DDMM.mmX
    if (!aprsDigitOK(p[0]) || !aprsDigitOK(p[1]) ||
        !aprsDigitOK(p[2]) || !aprsDigitOK(p[3]) || p[4] != '.' ||
        !aprsDigitOK(p[5]) || !aprsDigitOK(p[6]))
        return (false);
    char latNS = p[7];
    if (latNS != 'N' && latNS != 'S')
        return (false);
    float lat_deg = aprsDigitVal(p[0])*10 + aprsDigitVal(p[1]);
    float lat_min = aprsDigitVal(p[2])*10 + aprsDigitVal(p[3]) +
                     (aprsDigitVal(p[5])*10 + aprsDigitVal(p[6]))/100.0F;
    float lat = lat_deg + lat_min/60.0F;
    if (latNS == 'S')
        lat = -lat;

    table = p[8];

    const char *q = p+9;
    // longitude: DDDMM.mmY
    if (!aprsDigitOK(q[0]) || !aprsDigitOK(q[1]) || !aprsDigitOK(q[2]) ||
        !aprsDigitOK(q[3]) || !aprsDigitOK(q[4]) || q[5] != '.' ||
        !aprsDigitOK(q[6]) || !aprsDigitOK(q[7]))
        return (false);
    char lngEW = q[8];
    if (lngEW != 'E' && lngEW != 'W')
        return (false);
    float lng_deg = aprsDigitVal(q[0])*100 + aprsDigitVal(q[1])*10 + aprsDigitVal(q[2]);
    float lng_min = aprsDigitVal(q[3])*10 + aprsDigitVal(q[4]) +
                     (aprsDigitVal(q[6])*10 + aprsDigitVal(q[7]))/100.0F;
    float lng = lng_deg + lng_min/60.0F;
    if (lngEW == 'W')
        lng = -lng;

    code = q[9];

    ll = LatLong (lat, lng);
    return (true);
}

/* attempt to parse a COMPRESSED lat/lon + symbol starting at p (APRS101.pdf section 9): 1 byte
 * symbol table id, 4 bytes base-91 latitude, 4 bytes base-91 longitude, 1 byte symbol code, plus
 * (ignored here) 2 bytes course/speed-or-similar and 1 compression-type byte. return whether
 * successful; the 8 base-91 characters must each fall in the printable range 33..126.
 */
static bool parseCompressedPos (const char *p, LatLong &ll, char &table, char &code)
{
    if (strlen(p) < 10)
        return (false);

    // per spec the symbol table id is restricted to '/', '\', a digit, or an uppercase letter
    // (overlay) -- NOT any printable character. checking this closes off a real false-positive
    // risk: non-standard/garbled traffic that happens to fall in the base-91 byte range 33..126
    // can otherwise be silently accepted as a plausible-looking but entirely bogus position (seen
    // in the wild from a malformed LoRa-gateway packet whose free text coincidentally validated).
    // this doesn't catch every case -- compressed format's encoding is inherently permissive -- but
    // it's a meaningful tightening for very little cost.
    char t = p[0];
    if (t != '/' && t != '\\' && !isdigit((unsigned char)t) && !(t >= 'A' && t <= 'Z'))
        return (false);

    for (int i = 1; i <= 8; i++)
        if ((unsigned char)p[i] < 33 || (unsigned char)p[i] > 126)
            return (false);

    long y = (long)(p[1]-33);
    y = y*91 + (p[2]-33);
    y = y*91 + (p[3]-33);
    y = y*91 + (p[4]-33);
    float lat = 90.0F - y/380926.0F;

    long x = (long)(p[5]-33);
    x = x*91 + (p[6]-33);
    x = x*91 + (p[7]-33);
    x = x*91 + (p[8]-33);
    float lng = -180.0F + x/190463.0F;

    if (lat < -90 || lat > 90 || lng < -180 || lng > 180)
        return (false);                                  // reject obvious garbage

    table = p[0];
    code = p[9];
    ll = LatLong (lat, lng);
    return (true);
}

/* attempt to decode a Mic-E packet: latitude and N/S, longitude-offset and W/E flags are smuggled
 * in dest (the TNC2 destination field, 6+ chars); longitude, symbol code and symbol table live in
 * the first 8 bytes of the info field payload, right after the leading data-type byte.
 * Per APRS101.pdf section 10. return whether successful.
 *
 * N.B. we do not attempt to decode speed/course/status text here -- this pane has no columns for
 * them -- only enough to recover a usable position and symbol.
 */
static bool parseMicE (const char *dest, const char *payload, LatLong &ll, char &table, char &code)
{
    if (strlen(dest) < 6 || strlen(payload) < 9)
        return (false);

    int digit[6];
    bool ns_north = false, long_offset = false, we_west = false;
    for (int i = 0; i < 6; i++) {
        char c = (char) toupper ((unsigned char)dest[i]);
        int d;
        bool bit;                                        // false="standard" range, true="custom" range
        if (c >= '0' && c <= '9')      { d = c-'0'; bit = false; }
        else if (c >= 'A' && c <= 'J') { d = c-'A'; bit = true;  }
        else if (c >= 'P' && c <= 'Y') { d = c-'P'; bit = true;  }
        else if (c == 'K')             { d = 0;     bit = true;  }
        else if (c == 'L')             { d = 0;     bit = false; }
        else if (c == 'Z')             { d = 0;     bit = true;  }
        else
            return (false);                              // not a Mic-E-shaped destination field
        digit[i] = d;
        if (i == 3) ns_north   = bit;                     // 4th char: N/S
        if (i == 4) long_offset = bit;                    // 5th char: longitude +100 offset
        if (i == 5) we_west    = bit;                     // 6th char: E/W
    }

    int lat_deg = digit[0]*10 + digit[1];
    float lat_min = digit[2]*10 + digit[3] + (digit[4]*10 + digit[5])/100.0F;
    float lat = lat_deg + lat_min/60.0F;
    if (!ns_north)
        lat = -lat;

    const unsigned char *ip = (const unsigned char *)payload + 1;   // skip the leading data-type byte

    int lon_d = ip[0] - 28;
    if (long_offset)
        lon_d += 100;
    if (lon_d >= 180 && lon_d <= 189)
        lon_d -= 80;
    else if (lon_d >= 190 && lon_d <= 199)
        lon_d -= 190;

    int lon_m = ip[1] - 28;
    if (lon_m >= 60)
        lon_m -= 60;

    int lon_h = ip[2] - 28;
    if (lon_h < 0 || lon_h > 99 || lon_d < 0 || lon_d > 179)
        return (false);

    float lng = lon_d + (lon_m + lon_h/100.0F)/60.0F;
    if (we_west)
        lng = -lng;

    // ip[3..5] are speed/course, intentionally unused here
    code  = (char) ip[6];
    table = (char) ip[7];

    ll = LatLong (lat, lng);
    return (true);
}

/* look for a leading "CCC/SSS" course/speed prefix at *comment (3-digit course degrees, '/',
 * 3-digit speed in KNOTS -- the standard APRS convention, also reused by WX stations to mean wind
 * direction/speed instead). if found, converts speed to mph, advances *comment past the 7
 * consumed characters, and returns true; otherwise leaves everything untouched and returns false.
 *
 * N.B. Mic-E and compressed formats each also define their own binary course/speed sub-encoding
 * (in the bytes immediately following the symbol) which we do not decode -- this text convention
 * is however extremely commonly used in the comment/status text of all three formats regardless,
 * so checking for it here covers the large majority of real moving-station reports with one
 * code path instead of three.
 */
static bool stripCourseSpeed (const char **comment, float &course_deg, float &speed_mph)
{
    const char *p = *comment;
    if (strlen(p) < 7 || !isdigit((unsigned char)p[0]) || !isdigit((unsigned char)p[1]) ||
        !isdigit((unsigned char)p[2]) || p[3] != '/' || !isdigit((unsigned char)p[4]) ||
        !isdigit((unsigned char)p[5]) || !isdigit((unsigned char)p[6]))
        return (false);
    int course = (p[0]-'0')*100 + (p[1]-'0')*10 + (p[2]-'0');
    if (course > 360)
        return (false);
    int speed_kt = (p[4]-'0')*100 + (p[5]-'0')*10 + (p[6]-'0');
    course_deg = (float) course;
    speed_mph = speed_kt * 1.15078F;                        // knots -> statute mph
    *comment = p + 7;
    return (true);
}

/* look anywhere in comment for a "/A=NNNNNN" altitude token (6 digits, feet); if found, remove it
 * in place (closing the gap) and return true with alt_ft filled in, else leave comment untouched
 * and return false. comment must point into writable memory.
 */
static bool extractAltitude (char *comment, float &alt_ft)
{
    char *a = strstr (comment, "/A=");
    if (!a || strlen(a) < 9)
        return (false);
    for (int i = 3; i < 9; i++)
        if (!isdigit((unsigned char)a[i]))
            return (false);
    // N.B. must read exactly these 6 digits, not atol(a+3) -- atol has no length limit and will
    // happily keep consuming whatever digits immediately follow in the comment text (a station
    // reporting "/A=000000" right before a comment that happens to start with, say, "70cm ..."
    // would otherwise get its altitude misread as 70 instead of the correct 0)
    char digits[7] = { a[3], a[4], a[5], a[6], a[7], a[8], '\0' };
    alt_ft = (float) atol (digits);
    memmove (a, a+9, strlen(a+9)+1);                        // remove the token, closing the gap
    return (true);
}

/* parse the standard APRS weather micro-format from comment (already past position/symbol), eg
 * "223/004g005t077h50b10197" -- wind dir/speed, gust, temp, humidity, pressure, in that relative
 * order, each optional. rainfall fields (r/p/P) are recognized just enough to skip cleanly over
 * them since this pane has nowhere to show them yet. only meaningful when the station's symbol is
 * the WX icon (code=='_' table=='/'). returns whether anything at all was found.
 */
static bool parseWxFields (const char *comment, float &temp_f, int &humidity, float &wind_mph,
                            float &gust_mph, float &baro_mb)
{
    humidity = -1;
    gust_mph = -1;
    baro_mb = -1;
    wind_mph = -1;
    temp_f = 0;
    bool found = false;

    const char *p = comment;
    float course_deg, spd;
    if (stripCourseSpeed (&p, course_deg, spd)) {
        wind_mph = spd;                                     // repurposed as wind speed for WX
        found = true;
    }

    while (*p) {
        if (p[0]=='g' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
                          isdigit((unsigned char)p[3])) {
            gust_mph = (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0');
            found = true;
            p += 4;
        } else if (p[0]=='t' && (isdigit((unsigned char)p[1]) || p[1]=='-') &&
                                  isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])) {
            char buf[4] = { p[1], p[2], p[3], 0 };
            temp_f = (float) atoi (buf);
            found = true;
            p += 4;
        } else if ((p[0]=='r'||p[0]=='p'||p[0]=='P') && isdigit((unsigned char)p[1]) &&
                    isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])) {
            p += 4;                                         // rainfall -- skip, not shown yet
        } else if (p[0]=='h' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2])) {
            int hh = (p[1]-'0')*10 + (p[2]-'0');
            humidity = (hh == 0) ? 100 : hh;                 // "00" means 100% per spec
            found = true;
            p += 3;
        } else if (p[0]=='b' && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
                    isdigit((unsigned char)p[3]) && isdigit((unsigned char)p[4]) &&
                    isdigit((unsigned char)p[5])) {
            char buf[6] = { p[1], p[2], p[3], p[4], p[5], 0 };
            baro_mb = atoi (buf) / 10.0F;
            found = true;
            p += 6;
        } else
            p++;
    }

    return (found);
}

/* try uncompressed then compressed position decode at p; return how many characters were
 * consumed (so the caller knows where any trailing comment/status text begins), or 0 if neither
 * format matched.
 */
static int tryAPRSPos (const char *p, LatLong &ll, char &table, char &code)
{
    if (parseUncompressedPos (p, ll, table, code))
        return (19);
    if (parseCompressedPos (p, ll, table, code))
        return (strlen(p) >= 13 ? 13 : 10);
    return (0);
}

/* parse one APRS-IS TNC2-format line, updating the spot list as appropriate.
 * lines beginning with '#' are server comments, silently ignored (other than logging).
 * depth guards against pathological chains of third-party-wrapped ('}') packets; real traffic
 * never nests more than one or two deep. line must point into writable memory -- we trim/extract
 * tokens from the trailing comment/status text in place.
 */
static void parseAPRSLine (char *line, int depth = 0)
{
    if (depth > 3)
        return;

    // raw traffic dump for diagnosing decode oddities -- enable with eg "-a aprs=3"
    if (debugLevel (DEBUG_APRS, 3))
        aprsLog ("< %s\n", line);

    if (line[0] == '#') {
        aprsLog ("< %s", line);
        return;
    }

    // src>dest,path:payload
    char *gt = strchr (line, '>');
    char *colon = strchr (line, ':');
    if (!gt || !colon || colon < gt)
        return;                                          // not a recognizable packet

    char call[MAX_APRSCALL_LEN];
    size_t call_len = gt - line;
    if (call_len == 0 || call_len >= sizeof(call))
        return;
    memcpy (call, line, call_len);
    call[call_len] = '\0';

    // destination field: from just after '>' to the first ',' (if there's a digipeat path) or the
    // colon (if not) -- this is where Mic-E hides latitude/N-S/W-E, so keep it even though we
    // otherwise ignore the path
    char dest[16];
    char *comma = strchr (gt+1, ',');
    char *dest_end = (comma && comma < colon) ? comma : colon;
    size_t dest_len = dest_end - (gt+1);
    if (dest_len >= sizeof(dest))
        dest_len = sizeof(dest)-1;
    memcpy (dest, gt+1, dest_len);
    dest[dest_len] = '\0';

    char *payload = colon+1;
    if (!payload[0])
        return;

    bool has_pos = false;
    LatLong ll;
    char table = 0, code = 0;
    char *restp = NULL;                                  // where any trailing comment/status begins

    switch (payload[0]) {
    case '!':                                             // fallthru
    case '=': {
        int used = tryAPRSPos (payload+1, ll, table, code);
        if (used > 0) {
            has_pos = true;
            restp = payload+1+used;
        }
        break;
    }
    case '/':                                             // fallthru
    case '@':
        // 7-char timestamp (DDHHMMz or HHMMSSh etc) then position
        if (strlen (payload) > 8) {
            int used = tryAPRSPos (payload+8, ll, table, code);
            if (used > 0) {
                has_pos = true;
                restp = payload+8+used;
            }
        }
        break;
    case '`':                                             // fallthru -- Mic-E, current GPS data
    case '\'':                                            // fallthru -- Mic-E, old GPS data
        has_pos = parseMicE (dest, payload, ll, table, code);
        if (has_pos)
            restp = payload+9;                            // datatype byte + 8-byte lon/course/sym block
        break;
    case ';': {
        // object report: ;NAME(9 chars, space padded)*|_ DDHHMMz <position><symbol>...
        // shown under the object's own name rather than the reporting station's callsign, same as
        // every other APRS client does -- that's what identifies it on the air
        if (strlen (payload) >= 18) {
            char obj_name[10];
            memcpy (obj_name, payload+1, 9);
            obj_name[9] = '\0';
            for (int k = 8; k >= 0 && obj_name[k] == ' '; k--)
                obj_name[k] = '\0';                       // trim trailing pad
            if (obj_name[0])
                quietStrncpy (call, obj_name, sizeof(call));
            char *pos = payload + 18;
            int used = tryAPRSPos (pos, ll, table, code);
            if (used > 0) {
                has_pos = true;
                restp = pos+used;
            }
        }
        break;
    }
    case ')': {
        // item report: )NAME(3-9 chars, variable)!|_<position><symbol>...
        char *term = payload+1;
        while (*term && *term != '!' && *term != '_')
            term++;
        if (*term) {
            size_t name_len = term - (payload+1);
            char item_name[MAX_APRSCALL_LEN];
            if (name_len >= sizeof(item_name))
                name_len = sizeof(item_name)-1;
            memcpy (item_name, payload+1, name_len);
            item_name[name_len] = '\0';
            if (item_name[0])
                quietStrncpy (call, item_name, sizeof(call));
            char *pos = term+1;
            int used = tryAPRSPos (pos, ll, table, code);
            if (used > 0) {
                has_pos = true;
                restp = pos+used;
            }
        }
        break;
    }
    case '}':
        // third-party traffic: payload is itself a complete wrapped "SRC>DEST,PATH:payload" packet,
        // very common from igates relaying between networks -- unwrap and parse it directly rather
        // than recording the relaying station itself
        parseAPRSLine (payload+1, depth+1);
        return;
    default:
        // status, telemetry, messages, etc -- not decoded, but the station is still "heard" so it
        // appears in the list
        break;
    }

    APRSExtra extra;
    if (has_pos && restp) {
        bool is_wx = (code == '_' && table == '/');
        char *rest = restp;
        if (is_wx) {
            extra.has_wx = parseWxFields (rest, extra.wx_temp_f, extra.wx_humidity,
                                           extra.wx_wind_mph, extra.wx_gust_mph, extra.wx_baro_mb);
        } else {
            const char *cs_p = rest;
            if (stripCourseSpeed (&cs_p, extra.course_deg, extra.speed_mph)) {
                extra.has_course_speed = true;
                rest = (char *) cs_p;
            }
        }
        extra.has_alt = extractAltitude (rest, extra.alt_ft);
        while (*rest == ' ')
            rest++;                                        // trim leading pad before what's left
        quietStrncpy (extra.comment, rest, sizeof(extra.comment));
    }

    const char *symname = has_pos ? aprsSymbolName (table, code) : "-";
    addOrUpdateAPRSSpot (call, has_pos, ll, symname, table, code, extra);
}

/* read and process all currently available lines from aprs_client, without blocking.
 */
static void incomingAPRS (void)
{
    char line[300];
    while (aprs_client.available() && getTCPLine (aprs_client, line, sizeof(line), NULL)) {
        aprs_activity_ms = millis();
        parseAPRSLine (line);
    }

    // send a harmless comment if idle too long, just to keep any intermediate NAT/proxy open
    if (timesUp (&aprs_activity_ms, APRS_HBEAT_MS))
        aprs_client.print ("# HamClock keepalive\r\n");

    // still connected?
    if (!aprs_client) {
        aprsLog ("lost connection\n");
        aprs_client.stop();
    }
}

/* ***********************************************************************************************
 * connection
 */

/* try to connect and log in with a location-radius filter.
 * return whether now connected.
 */
static bool connectAPRSCluster (void)
{
    resetAPRSMem();

    const char *host = getAPRSClusterHost();
    int port = getAPRSClusterPort();
    if (!host || !host[0]) {
        showAPRSClusterErr ("no server configured -- see Setup");
        return (false);
    }

    mapMsg (0, "Connecting to %s:%d", host, port);

    aprs_client.stop();
    if (!aprs_client.connect (host, port)) {
        showAPRSClusterErr ("%s:%d connection failed", host, port);
        return (false);
    }

    // radius filter wants km regardless of the user's display units
    float radius_km = getAPRSClusterRadiusMiles() * KM_PER_MI;
    if (radius_km < 1)
        radius_km = 1;

    const char *mycall = getCallsign();
    if (!mycall || !mycall[0])
        mycall = "N0CALL";

    char login[128];
    snprintf (login, sizeof(login),
              "user %s pass -1 vers HamClock 1.0 filter r/%.4f/%.4f/%.0f\r\n",
              mycall, (double)de_ll.lat_d, (double)de_ll.lng_d, (double)radius_km);
    aprs_client.print (login);
    aprsLog ("> %s", login);

    aprs_activity_ms = millis();

    mapMsg (1000, "Connected to %s:%d", host, port);

    return (true);
}

/* insure connection is closed and memory is released
 */
static void closeAPRSCluster (void)
{
    if (aprs_client) {
        aprs_client.stop();
        aprsLog ("disconnect\n");
    }
    resetAPRSMem();
}

/* public: called whenever the user changes DE (see newDE() in ESPHamClock.cpp) so the server-side
 * location filter tracks wherever "nearby" now means. APRS-IS supports updating the active filter
 * on an already-open connection by simply sending a new "#filter ..." line -- no need to log out
 * or reconnect (http://www.aprs-is.net/javAPRSFilter.aspx). If we're not currently connected this
 * is a no-op: connectAPRSCluster() always builds its filter from the live DE position anyway, so
 * the next connection picks up the change automatically.
 */
void sendAPRSClusterNewDE (void)
{
    if (!useAPRSCluster() || !aprs_client)
        return;

    float radius_km = getAPRSClusterRadiusMiles() * KM_PER_MI;
    if (radius_km < 1)
        radius_km = 1;

    char filt[80];
    snprintf (filt, sizeof(filt), "#filter r/%.4f/%.4f/%.0f\r\n",
              (double)de_ll.lat_d, (double)de_ll.lng_d, (double)radius_km);
    aprs_client.print (filt);
    aprsLog ("> %s", filt);

    // every existing spot's distance/bearing was computed relative to the old DE and the list itself
    // was only ever populated from stations near the old DE in the first place -- clearest correct
    // behavior is to drop it and start fresh under the new filter, same as a brand new connection
    resetAPRSMem();
}

/* ***********************************************************************************************
 * drawing
 */

/* print text at x,y, current font, but first clip (truncate, no ellipsis -- there's no room for one)
 * to fit within maxw pixels so it can never run into whatever column comes next.
 */
static void printClipped (uint16_t x, uint16_t y, const char *text, uint16_t maxw)
{
    char buf[20];
    quietStrncpy (buf, text, sizeof(buf));
    size_t l = strlen (buf);
    while (l > 0 && getTextWidth (buf) > maxw)
        buf[--l] = '\0';
    tft.setCursor (x, y);
    tft.print (buf);
}

/* draw, else erase, the clear-list control
 */
static void drawAPRSClearListBtn (bool draw)
{
    uint16_t color = draw ? APRS_COLOR_BTN : RA8875_BLACK;

    drawSBox (aprsclr_b, color);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (aprsclr_b.x+1, aprsclr_b.y+2);
    tft.setTextColor (color);
    tft.print ("CLR");
}

/* draw, else erase, the category-filter control -- colored green when actively filtering (a
 * clear "something is hidden" cue), else the normal pane color like everything else when off.
 * label stays a fixed "FLT" (matching CLR's width/shape) rather than switching text between
 * "F:On"/"F:Off", since the color already carries the on/off state and a fixed-width label
 * keeps the CLR/FLT pair visually consistent instead of the box changing width.
 */
static void drawAPRSFilterBtn (bool draw)
{
    uint16_t color = !draw ? RA8875_BLACK : (APRS_FILTER_IS_OFF() ? APRS_COLOR_BTN : RA8875_GREEN);

    drawSBox (aprsflt_b, color);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (aprsflt_b.x+1, aprsflt_b.y+2);
    tft.setTextColor (color);
    tft.print ("FLT");
}

/* draw the pane title and host:port subtitle
 */
static void drawAPRSHeader (const SBox &box, uint16_t color)
{
    prepPlotBox (box);

    loadAPRSCatFilterOnce();

    aprsclr_b = {(uint16_t)(box.x+APRS_CLRBOX_DX), (uint16_t)(box.y+APRS_CLRBOX_DY),
                 APRS_CLRBOX_W, APRS_CLRBOX_H};
    aprsflt_b = {(uint16_t)(box.x+APRS_FLTBOX_DX), (uint16_t)(box.y+APRS_FLTBOX_DY),
                 APRS_FLTBOX_W, APRS_FLTBOX_H};

    const char *title = "Nearby APRS";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    // "Nearby APRS" doesn't fit every pane -- PANE_0 is always narrow, and a regular flex pane can
    // be sized down small too, so judge by actual measured width rather than assuming only PANE_0
    // needs the short form. Leave a little headroom on the left for the CLR button -- the Filter
    // button now sits in its own row below CLR/New, out of the title's way.
    if (BOX_IS_PANE_0(box) || getTextWidth(title) > box.w - (APRS_CLRBOX_DX+APRS_CLRBOX_W+6))
        title = "APRS";
    // title uses the fixed APRS_COLOR_TITLE (see above), not the passed-in band-tied color, which
    // is still used below for the new-spots symbol/scroll controls
    tft.setTextColor (APRS_COLOR_TITLE);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    if (useAPRSCluster()) {
        char host[50];
        snprintf (host, sizeof(host), "%s:%d", getAPRSClusterHost(), getAPRSClusterPort());
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        uint16_t hw = getTextWidth (host);
        if (hw > box.w - 5)
            snprintf (host, sizeof(host), "%s", getAPRSClusterHost());
        tft.setCursor (box.x + (box.w-getTextWidth(host))/2, box.y + APRS_SUBTITLE_Y);
        tft.print (host);
    }

    aprs_ss.max_vis = (box.h - (APRS_LIST_Y0+APRS_HDR_DY))/LISTING_DY;
    aprs_ss.initNewSpotsSymbol (box, color);
    aprs_ss.dir = aprs_ss.DIR_FROMSETUP;
    aprs_ss.scrollToNewest();

    drawAPRSClearListBtn (useAPRSCluster());
    drawAPRSFilterBtn (useAPRSCluster());
}

/* pop up a checkbox list letting the user show/hide entire categories of symbols at once --
 * individually listing 145+ symbols would be unusable, so this is the practical granularity.
 * deselecting everything is treated as selecting everything (there's no useful "show nothing").
 */
static void drawAllVisAPRSSpots (const SBox &box);         // fwd -- defined below, used here and
                                                             // by the row/header touch handlers
static void runAPRSFilterMenu (const SBox &box)
{
    loadAPRSCatFilterOnce();

    MenuItem mitems[N_APRSCAT];
    for (int i = 0; i < N_APRSCAT; i++)
        mitems[i] = {MENU_TOGGLE, (bool)((aprs_cat_filter >> i) & 1), 1, 2, aprs_cat_names[i], NULL};

    // start as close to the top of the pane as the CLR/FLT stack allows, and lay out in 2 columns
    // (see the shortened aprs_cat_names above) -- 9 items in 1 column ran to 9 rows plus the
    // footer/Ok/Cancel row, tall enough to spill well down into the map on most pane sizes; 2
    // columns cuts that to 5 rows and starting higher keeps it from reaching as far down.
    SBox menu_b = {aprsflt_b.x, (uint16_t)(box.y + 2), 0, 0};
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 2, N_APRSCAT, mitems};
    if (runMenu (menu)) {
        uint16_t newmask = 0;
        for (int i = 0; i < N_APRSCAT; i++)
            if (mitems[i].set)
                newmask |= (1U << i);
        if (newmask == 0)
            newmask = (uint16_t)((1U << N_APRSCAT) - 1);
        aprs_cat_filter = newmask;
        saveAPRSCatFilter();
        rebuildAPRSDisplay();
        drawAPRSHeader (box, APRS_COLOR_NEW);
        drawAllVisAPRSSpots (box);
    }
}

/* column layout for the list: fixed pixel widths counted in from the right edge so DIST and SYM
 * never crowd together regardless of how narrow the pane is (CALL just takes whatever is left
 * on the the left and clips if a callsign is unusually long, same tradeoff every other narrow
 * HamClock list pane makes).
 */
// column widths were re-tuned when AGE was added: the pane is only ~160px wide and adding a
// whole new column with no other changes squeezed CALL down to near-nothing, so SYM shrank
// (icons are only 12px; the wide fallback text column was rarely used at full width anyway)
// and the inter-column gap shrank slightly too, to make room. DIR was dropped entirely (still
// available via the row's detail tooltip) to give CALL more room still. SYM was tightened
// again afterward since the icons only need ~12px and were leaving unused space.
#define APRS_SYM_W       20             // reserved width of the SYM column
#define APRS_DIST_W      30             // reserved width of the DIST/KM/MI column
#define APRS_AGE_W       22             // reserved width of the AGE column
#define APRS_COL_GAP      4             // minimum gap left between adjacent columns

/* draw the CALL / DISTANCE / SYMBOL / AGE column header just above the scrollable list
 */
static void drawAPRSColHeader (const SBox &box)
{
    uint16_t age_x  = box.x + box.w - APRS_AGE_W;
    uint16_t sym_x  = age_x - APRS_COL_GAP - APRS_SYM_W;
    uint16_t dist_x = sym_x - APRS_COL_GAP - APRS_DIST_W;
    uint16_t call_w = dist_x > box.x+3+APRS_COL_GAP ? dist_x - APRS_COL_GAP - (box.x+3) : 0;

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    uint16_t y = box.y + APRS_LIST_Y0;
    printClipped (box.x + 3, y, "CALL", call_w);
    tft.setCursor (dist_x, y);
    tft.print (showDistKm() ? "KM" : "MI");
    tft.setCursor (sym_x, y);
    tft.print ("SYM");
    tft.setCursor (age_x, y);
    tft.print ("AGE");
}

/* look up the sprite pixel data for the given symbol table+code, else NULL. all sprites are
 * APRS_ICON_SZ x APRS_ICON_SZ RGB565, row-major, generated offline by gen_aprs_icons.py into
 * aprsicons.h -- see that file for how to add or tweak one (needs Python + Pillow, not part of
 * the normal build).
 */
static const uint16_t *findAPRSIconPixels (char table, char code)
{
    if (table == '/') {
        for (size_t i = 0; i < N_APRS_ICONS; i++)
            if (aprs_icons[i].code == code)
                return (aprs_icons[i].px);
    } else if (table == '\\' || isdigit((unsigned char)table) ||
               (table >= 'A' && table <= 'Z')) {
        for (size_t i = 0; i < N_APRS_ALT_ICONS; i++)
            if (aprs_alt_icons[i].code == code)
                return (aprs_alt_icons[i].px);
    }
    return (NULL);
}

/* blit a real sprite icon for the given symbol table+code at (x,y) -- the top-left of the SYM
 * column cell for this row -- vertically centered within the row. return whether a sprite was
 * actually available; caller falls back to the text abbreviation when there isn't one (the
 * alternate symbol table, and anything not in the sprite sheet yet).
 */
static bool drawAPRSSymbolIcon (uint16_t x, uint16_t y, char table, char code)
{
    const uint16_t *px = findAPRSIconPixels (table, code);
    if (!px)
        return (false);

    uint16_t iy = y + (LISTING_DY - APRS_ICON_SZ)/2 - 2;    // roughly centered on the text row
    uint16_t rowbuf[APRS_ICON_SZ];
    for (int r = 0; r < APRS_ICON_SZ; r++) {
        for (int c = 0; c < APRS_ICON_SZ; c++)
            rowbuf[c] = px[r*APRS_ICON_SZ + c];
        tft.drawPixels (rowbuf, APRS_ICON_SZ, x, iy + r);
    }
    return (true);
}

/* draw all currently visible spots, plus the "not configured" message if applicable
 */
static void drawAllVisAPRSSpots (const SBox &box)
{
    uint16_t y0 = box.y + APRS_LIST_Y0 + APRS_HDR_DY;
    tft.fillRect (box.x+1, y0-LISTING_OS, box.w-2, box.h - (y0-box.y-LISTING_OS+1), RA8875_BLACK);

    if (!useAPRSCluster()) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RGB565(180,140,60));
        tft.setCursor (box.x+3, y0);
        tft.print ("Configure APRS in Setup");
        return;
    }

    drawAPRSColHeader (box);

    uint16_t age_x  = box.x + box.w - APRS_AGE_W;
    uint16_t sym_x  = age_x - APRS_COL_GAP - APRS_SYM_W;
    uint16_t dist_x = sym_x - APRS_COL_GAP - APRS_DIST_W;
    uint16_t call_w = dist_x > box.x+3+APRS_COL_GAP ? dist_x - APRS_COL_GAP - (box.x+3) : 0;
    uint16_t sym_w  = age_x > sym_x+APRS_COL_GAP ? age_x - APRS_COL_GAP - sym_x - 1 : 0;

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    int min_i, max_i;
    if (aprs_ss.getVisDataIndices (min_i, max_i) > 0) {
        for (int i = min_i; i <= max_i; i++) {
            const APRSSpot &sp = aprs_disp[i];
            uint16_t y = y0 + aprs_ss.getDisplayRow(i) * LISTING_DY;

            tft.setTextColor (aprsColor());
            printClipped (box.x + 3, y, sp.call, call_w);

            char dist_str[10] = "--";
            if (sp.has_pos) {
                float d = showDistKm() ? sp.dist_mi * KM_PER_MI : sp.dist_mi;
                snprintf (dist_str, sizeof(dist_str), "%.0f", d);
            }
            tft.setCursor (dist_x, y);
            tft.print (dist_str);

            if (!sp.has_pos || !drawAPRSSymbolIcon (sym_x, y, sp.sym_table, sp.sym_code))
                printClipped (sym_x, y, sp.has_pos ? sp.symbol : "-", sym_w);

            // age since last heard: counts up in seconds (1s, 2s, ... 59s) then switches to
            // whole minutes, then hours/days/etc -- same convention as formatAge() uses for
            // DX Cluster and every other spot list in HamClock.
            char age_str[8];
            time_t age = myNow() - sp.heard;
            tft.setCursor (age_x, y);
            tft.print (formatAge (age, age_str, sizeof(age_str), 3));
        }
    }

    aprs_ss.drawScrollUpControl (box, APRS_COLOR_SCRL, APRS_COLOR_SCRL);
    aprs_ss.drawScrollDownControl (box, APRS_COLOR_SCRL, APRS_COLOR_SCRL);
}

/* ***********************************************************************************************
 * public interface
 */

/* called often while pane is visible; fresh is set when newly so.
 * connect if not already, then (re)draw. return whether "ok" (even "not configured" counts as ok
 * so the pane doesn't get treated as a WiFi failure and retried aggressively).
 */
bool updateAPRSCluster (const SBox &box, bool fresh)
{
    if (!useAPRSCluster()) {
        drawAPRSHeader (box, RA8875_RED);
        drawAllVisAPRSSpots (box);
        return (true);
    }

    if (!aprs_client) {
        if (!connectAPRSCluster()) {
            drawAPRSHeader (box, RA8875_RED);
            return (false);
        }
        fresh = true;
    }

    if (fresh)
        drawAPRSHeader (box, APRS_COLOR_NEW);

    if (aprs_ss.atNewest()) {
        // safe to refresh the display copy -- nothing for the user to lose track of
        if (aprs_list_changed) {
            rebuildAPRSDisplay();
            aprs_ss.drawNewSpotsSymbol (false, false);         // insure off
            aprs_scrolledaway_tm = 0;
            scheduleMapRedraw();
        }
        ROTHOLD_CLR(PLOT_CH_APRSCLUSTER);                      // resume rotation
    } else {
        // user is scrolled back through history -- leave aprs_disp alone, just keep accumulating
        // into aprs_spots in the background and let them know something new has come in
        if (showingNewAPRS())
            aprs_ss.drawNewSpotsSymbol (true, false);          // show passively
        ROTHOLD_SET(PLOT_CH_APRSCLUSTER);                      // disable rotation
    }

    drawAllVisAPRSSpots (box);

    return (true);
}

/* called often to read any pending APRS-IS traffic, independent of whether the pane is currently
 * shown -- but we only bother staying connected while it (still) is.
 */
void checkAPRSCluster (void)
{
    if (!useAPRSCluster() || !aprs_client)
        return;

    static uint32_t prev_check;
    if (!timesUp (&prev_check, APRS_BGCHECK_DT))
        return;

    if (findPaneForChoice (PLOT_CH_APRSCLUSTER) == PANE_NONE) {
        aprsLog ("closing because no longer in any pane\n");
        closeAPRSCluster();
        return;
    }

    incomingAPRS();
}

/* handy check whether New Spot symbol needs changing on/off, called after any scroll change
 */
static void checkNewSpotSymbolAPRS (bool was_at_newest)
{
    if (was_at_newest && !aprs_ss.atNewest()) {
        aprs_scrolledaway_tm = myNow();                        // record when moved off top
        ROTHOLD_SET(PLOT_CH_APRSCLUSTER);                      // disable rotation
    } else if (!was_at_newest && aprs_ss.atNewest()) {
        aprs_ss.drawNewSpotsSymbol (false, false);              // turn off entirely
        aprs_list_changed = true;                               // force a rebuild back at newest
        aprs_scrolledaway_tm = 0;
        ROTHOLD_CLR(PLOT_CH_APRSCLUSTER);                       // resume rotation
    }
}

/* shift the visible list up, if possible
 */
static void scrollAPRSUp (const SBox &box)
{
    bool was_at_newest = aprs_ss.atNewest();
    if (aprs_ss.okToScrollUp()) {
        aprs_ss.scrollUp();
        drawAllVisAPRSSpots (box);
    }
    checkNewSpotSymbolAPRS (was_at_newest);
}

/* shift the visible list down, if possible
 */
static void scrollAPRSDown (const SBox &box)
{
    bool was_at_newest = aprs_ss.atNewest();
    if (aprs_ss.okToScrollDown()) {
        aprs_ss.scrollDown();
        drawAllVisAPRSSpots (box);
    }
    checkNewSpotSymbolAPRS (was_at_newest);
}

/* handle a touch within our pane; box is known to contain s.
 * return whether we claimed it -- returning false for an unclaimed title-bar tap lets the caller
 * bring up the normal pane-choice picker, same as every other pane without its own submenu.
 */
bool checkAPRSClusterTouch (const SCoord &s, const SBox &box)
{
    // covers the whole CLR/New/Filter button stack in the upper-left, not just the original
    // title-row height -- Filter now sits in its own row below New, past PANETITLE_H, so the
    // old title-only guard here missed it and let taps fall through to the pane-choice picker
    if (s.y < box.y + APRS_SUBTITLE_Y) {

        // scroll up?
        if (aprs_ss.checkScrollUpTouch (s, box)) {
            scrollAPRSUp (box);
            return (true);
        }

        // scroll down?
        if (aprs_ss.checkScrollDownTouch (s, box)) {
            scrollAPRSDown (box);
            return (true);
        }

        // clear control?
        if (useAPRSCluster() && inBox (s, aprsclr_b)) {
            aprsLog ("User erased list of %d spots\n", n_aprs_spots);
            resetAPRSMem();
            drawAPRSHeader (box, APRS_COLOR_NEW);
            drawAllVisAPRSSpots (box);
            return (true);
        }

        // filter control?
        if (useAPRSCluster() && inBox (s, aprsflt_b)) {
            runAPRSFilterMenu (box);
            return (true);
        }

        // New spots?
        if (aprs_ss.checkNewSpotsTouch (s, box)) {
            if (!aprs_ss.atNewest() && showingNewAPRS()) {
                // scroll to newest, let updateAPRSCluster() do the rest
                aprs_ss.scrollToNewest();
                checkNewSpotSymbolAPRS (false);
                drawAllVisAPRSSpots (box);
            }
            return (true);                                      // claim ours even if not showing
        }

        // nothing else of ours in the title bar -- let the pane-choice picker open
        return (false);
    }

    // a tap on the column header row itself: CALL explains the list, KM/MI toggles sort order
    // (and says so), SYM shows a legend, AGE explains the countdown-style age display. Every one
    // of these tooltips also mentions the KM/MI sort toggle, since that's the one control here
    // that actually *does* something besides pop up help, and it's otherwise easy to miss.
    if (useAPRSCluster()) {
        uint16_t hdr_y0 = box.y + APRS_LIST_Y0;
        uint16_t hdr_y1 = hdr_y0 + APRS_HDR_DY;
        if (s.y >= hdr_y0 && s.y < hdr_y1) {
            uint16_t age_x  = box.x + box.w - APRS_AGE_W;
            uint16_t sym_x  = age_x - APRS_COL_GAP - APRS_SYM_W;
            uint16_t dist_x = sym_x - APRS_COL_GAP - APRS_DIST_W;

            if (s.x < dist_x) {
                tooltip (s, "Stations heard nearby. Tap a row for full detail. Tap KM/MI to sort "
                            "by distance or by time heard.");
                return (true);
            }

            if (s.x >= dist_x && s.x < sym_x) {
                aprs_sort_recent = !aprs_sort_recent;
                aprs_list_changed = true;
                if (aprs_ss.atNewest())
                    rebuildAPRSDisplay();
                drawAllVisAPRSSpots (box);
                tooltip (s, aprs_sort_recent ? "Sorted by most recently heard. Tap KM/MI again to "
                                                "sort by distance instead."
                                              : "Sorted by distance, nearest first. Tap KM/MI again "
                                                "to sort by time heard instead.");
                return (true);
            }

            if (s.x >= sym_x && s.x < age_x) {
                // most symbols now render as actual icons (see aprsicons.h) rather than text, so a
                // giant name dump isn't as useful here as it used to be -- explain what's covered
                // and what still falls back to "-" instead
                tooltip (s, "Most APRS symbols show as small icons. \"-\" means either the "
                            "alternate symbol table (not decoded) or one of a handful of unused/"
                            "reserved primary-table codes. Tap KM/MI to sort by distance or by "
                            "time heard.");
                return (true);
            }

            if (s.x >= age_x) {
                tooltip (s, "Time since last heard: counts up in seconds, then switches to "
                            "whole minutes, hours, days, etc. Tap KM/MI to sort by distance or "
                            "by time heard.");
                return (true);
            }
        }
    }

    // a tap on a data row: tapping directly on a truncated callsign shows the full call as a
    // tooltip (same convention as OnTheAir and Mesh Mon); tapping anywhere else on the row shows
    // a fuller detail tooltip -- position, grid square, distance/heading, symbol, and how long ago
    // it was heard -- since none of that fits in the compact list columns.
    if (useAPRSCluster()) {
        uint16_t y0 = box.y + APRS_LIST_Y0 + APRS_HDR_DY;
        int vis_row = (s.y - y0) / LISTING_DY;
        int spot_row;
        if (vis_row >= 0 && aprs_ss.findDataIndex (vis_row, spot_row)) {
            const APRSSpot &sp = aprs_disp[spot_row];

            uint16_t age_x  = box.x + box.w - APRS_AGE_W;
            uint16_t sym_x  = age_x - APRS_COL_GAP - APRS_SYM_W;
            uint16_t dist_x = sym_x - APRS_COL_GAP - APRS_DIST_W;
            uint16_t call_w = dist_x > box.x+3+APRS_COL_GAP ? dist_x - APRS_COL_GAP - (box.x+3) : 0;

            selectFontStyle (LIGHT_FONT, FAST_FONT);
            if (s.x < dist_x && getTextWidth (sp.call) > call_w) {
                tooltip (s, sp.call);
                return (true);
            }

            long age_s = (long)(myNow() - sp.heard);
            char age[16];
            if (age_s < 60)
                snprintf (age, sizeof(age), "%lds", age_s);
            else if (age_s < 3600)
                snprintf (age, sizeof(age), "%ldm", age_s/60);
            else
                snprintf (age, sizeof(age), "%ldh", age_s/3600);

            char tip[420];
            if (sp.has_pos) {
                char grid[MAID_CHARLEN];
                ll2maidenhead (grid, sp.ll);
                float bear = sp.bear_deg;
                bool ismag = desiredBearing (de_ll, bear);
                float dist = showDistKm() ? sp.dist_mi * KM_PER_MI : sp.dist_mi;
                int n = snprintf (tip, sizeof(tip),
                          "%s  %.4f%c %.4f%c  %s  %.0f %s  %s%s  heard %s ago",
                          sp.call,
                          fabsf (sp.ll.lat_d), sp.ll.lat_d < 0 ? 'S' : 'N',
                          fabsf (sp.ll.lng_d), sp.ll.lng_d < 0 ? 'W' : 'E',
                          grid, dist, showDistKm() ? "km" : "mi",
                          compassAbbr (bear), ismag ? "M" : "T", age);

                if (sp.has_wx) {
                    n += snprintf (tip+n, sizeof(tip)-n, "  %.0fF", sp.wx_temp_f);
                    if (sp.wx_humidity >= 0)
                        n += snprintf (tip+n, sizeof(tip)-n, " %d%%RH", sp.wx_humidity);
                    if (sp.wx_wind_mph >= 0)
                        n += snprintf (tip+n, sizeof(tip)-n, " wind %.0fmph", sp.wx_wind_mph);
                    if (sp.wx_gust_mph >= 0)
                        n += snprintf (tip+n, sizeof(tip)-n, " gust %.0fmph", sp.wx_gust_mph);
                    if (sp.wx_baro_mb >= 0)
                        n += snprintf (tip+n, sizeof(tip)-n, " %.1fmb", sp.wx_baro_mb);
                } else if (sp.has_course_speed) {
                    n += snprintf (tip+n, sizeof(tip)-n, "  %s %.0fmph",
                                   compassAbbr (sp.course_deg), sp.speed_mph);
                }

                if (sp.has_alt)
                    n += snprintf (tip+n, sizeof(tip)-n, "  alt %.0fft", sp.alt_ft);

                if (sp.comment[0])
                    snprintf (tip+n, sizeof(tip)-n, "  \"%s\"", sp.comment);
            } else {
                snprintf (tip, sizeof(tip), "%s  position unknown  heard %s ago", sp.call, age);
            }
            tooltip (s, tip);
            return (true);
        }
    }

    // no other special areas of our own -- nothing else to claim
    return (false);
}
