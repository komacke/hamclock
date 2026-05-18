/* manage selection and display of one earth sat.
 *
 * we call "pass" the overhead view shown in dx_info_b, "path" the orbit shown on the map.
 *
 * N.B. our satellite info server changes blanks to underscores in sat names.
 * N.B. we always assign sat_state[0] first then sat_state[1] only if want a second sat
 */


#include "HamClock.h"

#define MAX_ACTIVE_SATS 2               // increasing this works here but requires more colors

bool dx_info_for_sat;                   // global to indicate whether dx_info_b is for DX info or sat info

// path drawing
#define MAX_PATHPTS     512             // N.B. MAX_PATHPTS must be power of 2 for dashed lines to work right
#define FOOT_ALT0       250             // n points in altitude 0 foot print
#define FOOT_ALT30      100             // n points in altitude 30 foot print
#define FOOT_ALT60      75              // n points in altitude 60 foot print
#define N_FOOT          3               // number of footprint altitude loci
#define ARROW_N         25              // n arrows
#define ARROW_EVERY     (MAX_PATHPTS/ARROW_N)   // one arrow every these many steps
#define ARROW_L         15              // arrow length, canonical pixels


// layout
#define ALARM_DT        (1.0F/1440.0F)  // flash this many days before an event
#define SATLED_RISING_HZ  1             // flash at this rate when sat about to rise
#define SATLED_SETTING_HZ 2             // flash at this rate when sat about to set
#define SAT_TOUCH_R     20U             // touch radius, pixels
#define SAT_UP_R        2               // dot radius when up
#define PASS_STEP       10.0F           // pass step size, seconds
#define TBORDER         50              // top border
#define FONT_H          (dx_info_b.h/6) // height for SMALL_FONT
#define FONT_D          5               // font descent
#define SAT_COLOR       RA8875_WHITE    // overall annotation color
#define BTN_COLOR       RA8875_GREEN    // button fill color
#define SATUP_COLOR     RGB565(0,200,0) // time color when sat is up
#define SOON_COLOR      RGB565(200,0,0) // table text color for pass soon
#define GONE_COLOR      RGB565(255,90,90) // table text color for a sat that has gone missing
#define SOON_MINS       10              // "soon", minutes
#define CB_SIZE         20              // size of selection check box
#define CELL_H          32              // display cell height
#define N_COLS          4               // n cols in name table
#define CELL_W          (800/N_COLS)    // display cell width
#define N_ROWS          ((480-TBORDER)/CELL_H)  // n rows in name table
#define MAX_NSAT        (N_ROWS*N_COLS) // max names we can display
#define MAX_PASS_STEPS  30              // max lines to draw for pass map
#define OFFSCRN         20000           // x or y coord that is definitely off screen
static SBox ok_b = {730,10,55,35};      // Ok button

// NV_SATnFLAGS bit masks
typedef enum {
    SF_PATH_MASK = 1,
} SatFlags;

// used so findNextPass() can be used for contexts other than the current sat now
typedef struct {
    DateTime rise_time, set_time;       // next pass times
    bool rise_ok, set_ok;               // whether rise_time and set_time are valid
    float rise_az, set_az;              // rise and set az, degrees, if valid
    bool ever_up, ever_down;            // whether sat is ever above or below SAT_MIN_EL in next day
} SatRiseSet;

// handy pass states from findPassState()
typedef enum {
    PS_NONE,            // no sat rise/set in play or unknown
    PS_UPSOON,          // pass lies ahead
    PS_UPNOW,           // pass in progress
    PS_HASSET,          // down after being up
} PassState;

// files
static const char esat_ufn[] = "user-esats.txt";        // name of user's tle file
static const char esat_sfn[] = "esats.txt";             // local cached file from server
static const char esat_url[] = "/esats/esats.txt";      // server file URL
#define MAX_CACHE_AGE   10000                           // max cache age, seconds

// used by readNextSat()
typedef enum {
    RNS_INIT = 0,                                       // check user's list
    RNS_SERVER,                                         // check backend list
    RNS_DONE                                            // checked both
} RNS_t;

// foot configuration
static const uint16_t max_foot[N_FOOT] = {FOOT_ALT0, FOOT_ALT30, FOOT_ALT60};   // max dots on each altitude 
static const float foot_alts[N_FOOT] = {0.0F, 30.0F, 60.0F};                    // alt of each segment, degs

// state
typedef struct {
    Satellite *sat;                                     // satellite definition, NULL if inactive
    SatRiseSet rs;                                      // event info
    SCoord *path;                                       // full res coords for orbit, [0] always now
    int n_path;                                         // n in path[]
    SCoord *foot[N_FOOT];                               // full res coords for each footprint altitude
    int n_foot[N_FOOT];                                 // n in each foot[]
    bool show_path;                                     // whether to pass as well as foot
    SBox name_b;                                        // canonical coords of name on map
    char name[NV_SATNAME_LEN];                          // name, spaces are underscores
    NV_Name nv_name;                                    // NV property for persistent name
    NV_Name nv_flags;                                   // NV property for persistent option flags
    ColorSelection cs;                                  // path control
} SatState;
static SatState sat_state[MAX_ACTIVE_SATS];             // [1].sat is set only if [0].sat is also set
static bool new_pass;                                   // set when new pass is ready

// remember names of sats that were saved/selected but could not be found in the TLE lists.
// these are NOT a fatal condition: the sat is just dropped and flagged here so the selection
// table can show it in a distinct color instead of throwing an in-your-face error.
#define MAX_MISSING_SATS  8
static char missing_sats[MAX_MISSING_SATS][NV_SATNAME_LEN];
static int n_missing_sats;

/* record that the named sat could not be found so the chooser can mark it.
 * silently ignores duplicates and overflow.
 */
static void noteMissingSat (const char *name)
{
    if (!name || !name[0])
        return;
    for (int i = 0; i < n_missing_sats; i++)
        if (strcasecmp (missing_sats[i], name) == 0)
            return;                                     // already known
    if (n_missing_sats < MAX_MISSING_SATS) {
        strncpy (missing_sats[n_missing_sats], name, NV_SATNAME_LEN-1);
        missing_sats[n_missing_sats][NV_SATNAME_LEN-1] = '\0';
        n_missing_sats++;
    }
}

/* forget that the named sat was missing, eg, once it is found again or reselected.
 */
static void clearMissingSat (const char *name)
{
    for (int i = 0; i < n_missing_sats; i++) {
        if (strcasecmp (missing_sats[i], name) == 0) {
            for (int j = i+1; j < n_missing_sats; j++)
                strcpy (missing_sats[j-1], missing_sats[j]);
            n_missing_sats--;
            return;
        }
    }
}

#define NO_CUR_SAT  (-1)                                // flag for currentSat and dxpaneSat

// index of sat_state to be shown in the DX pane, else NO_CUR_SAT
static int dxpaneSat = NO_CUR_SAT;

// return whether the given sat_state has a defined name
static inline bool SAT_NAME_IS_SET (SatState &s) { return s.name[0] != '\0'; }

// number of sat_states in use.
// N.B we always assign [0] first then [1] only if want a second sat
static inline int nActiveSats(void) { return sat_state[1].sat ? 2 : (sat_state[0].sat ? 1 : 0); }

// current observer (same for all sats)
static Observer *obs;                                   // DE


#if defined(__GNUC__)
static void fatalSatError (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void fatalSatError (const char *fmt, ...);
#endif



/* return all IO pins to quiescent state
 */
void satResetIO()
{
    disableBlinker (SATALARM_PIN);
}

/* set alarm SATALARM_PIN flashing with the given frequency or one of BLINKER_*.
 */
static void risetAlarm (int hz)
{
    // insure helper thread is running
    startBinkerThread (SATALARM_PIN, false); // on is hi

    // tell helper thread what we want done
    setBlinkerRate (SATALARM_PIN, hz);
}


/* return index of sat_state considered "current" by outside systems such as gimbal, or NO_CUR_SAT if none.
 * N.B. [0] can still be considered current even if none are shown in the DX pane.
 */
static int currentSat(void)
{
    if (sat_state[0].sat && dxpaneSat == 0)
        return 0;

    if (sat_state[1].sat && dxpaneSat == 1)
        return 1;

    // assign one?
    if (dxpaneSat == NO_CUR_SAT) {
        if (sat_state[0].sat) {
            dxpaneSat = 0;
            return 0;
        }
        if (sat_state[1].sat) {
            dxpaneSat = 1;
            return 1;
        }
    }

    return (NO_CUR_SAT);
}

/* completely undefine and reclaim memory for the given sat
 */
static void unsetSat (SatState &s)
{
    // reset sat and its path
    if (s.sat) {
        delete s.sat;
        s.sat = NULL;
    }
    if (s.path) {
        free (s.path);
        s.path = NULL;
    }
    for (int i = 0; i < N_FOOT; i++) {
        if (s.foot[i]) {
            free (s.foot[i]);
            s.foot[i] = NULL;
        }
    }

    // reset name and flags here and in NV
    s.name[0] = '\0';
    NVWriteString (s.nv_name, s.name);

    // no more sat if last one
    if (nActiveSats() == 0) {
        dx_info_for_sat = false;
        risetAlarm (BLINKER_OFF);
    }
}

/* fill s.foot with loci of points that see the sat at various viewing altitudes.
 * N.B. call this before updateSatPath malloc's its memory
 */
static void updateFootPrint (SatState &s, float satlat, float satlng)
{
    // complement of satlat
    float cosc = sinf(satlat);
    float sinc = cosf(satlat);

    // fill each segment along each altitude
    for (uint8_t alt_i = 0; alt_i < N_FOOT; alt_i++) {

        // start with max n points
        int n_malloc = max_foot[alt_i]*sizeof(SCoord);
        s.foot[alt_i] = (SCoord *) realloc (s.foot[alt_i], n_malloc);
        if (!s.foot[alt_i] && n_malloc > 0)
            fatalError ("no memort for sat foot: %d", n_malloc);

        // satellite viewing altitude
        float valt = deg2rad(foot_alts[alt_i]);

        // great-circle radius from subsat point to viewing circle at altitude valt
        float vrad = s.sat->viewingRadius(valt);

        // compute each unique point around viewing circle
        uint16_t n_foot = 0;
        uint16_t m = max_foot[alt_i];
        for (uint16_t foot_i = 0; foot_i < m; foot_i++) {

            // compute next point
            float cosa, B;
            float A = foot_i*2*M_PI/m;
            solveSphere (A, vrad, cosc, sinc, &cosa, &B);
            float vlat = M_PIF/2-acosf(cosa);
            float vlng = fmodf(B+satlng+5*M_PIF,2*M_PIF)-M_PIF; // require [-180.180)
            ll2sRaw (vlat, vlng, s.foot[alt_i][n_foot], 2);

            // skip duplicate points
            if (n_foot == 0 || memcmp (&s.foot[alt_i][n_foot], &s.foot[alt_i][n_foot-1], sizeof(SCoord)))
                n_foot++;
        }

        // reduce memory to only points actually used
        s.n_foot[alt_i] = n_foot;
        s.foot[alt_i] = (SCoord *) realloc (s.foot[alt_i], n_foot*sizeof(SCoord));
        // Serial.printf ("alt %g: n_foot %u / %u\n", foot_alts[alt_i], n, m);
    }
}

/* return a DateTime for the given time
 */
static DateTime userDateTime(time_t t)
{
    int yr = year(t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour(t);
    int mn = minute(t);
    int sc = second(t);

    DateTime dt(yr, mo, dy, hr, mn, sc);

    return (dt);
}

/* find next rise and set times if sat valid starting from the given time_t.
 * always find rise and set in the future, so set_time will be < rise_time iff pass is in progress.
 * also update flags ever_up, set_ok, ever_down and rise_ok.
 * name is only used for local logging, set to NULL to avoid even this.
 */
static void findNextPass (Satellite *sat, const char *name, time_t t, SatRiseSet &rs)
{
    if (!sat || !obs) {
        rs.set_ok = rs.rise_ok = false;
        return;
    }

    // measure how long this takes
    uint32_t t0 = millis();

    #define COARSE_DT   90L             // seconds/step forward for fast search
    #define FINE_DT     (-2L)           // seconds/step backward for refined search
    float pel;                          // previous elevation
    long dt = COARSE_DT;                // search time step size, seconds
    DateTime t_now = userDateTime(t);   // search starting time
    DateTime t_srch = t_now + -FINE_DT; // search time, start beyond any previous solution
    float tel, taz, trange, trate;      // target el and az, degrees

    // init pel and make first step
    sat->predict (t_srch);
    sat->topo (obs, pel, taz, trange, trate);
    t_srch += dt;

    // search up to a few days ahead for next rise and set times (for example for moon)
    rs.set_ok = rs.rise_ok = false;
    rs.ever_up = rs.ever_down = false;
    while ((!rs.set_ok || !rs.rise_ok) && t_srch < t_now + 2.0F) {

        // find circumstances at time t_srch
        sat->predict (t_srch);
        sat->topo (obs, tel, taz, trange, trate);

        // check for rising or setting events
        if (tel >= SAT_MIN_EL) {
            rs.ever_up = true;
            if (pel < SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined set event (recall we are going backwards),
                    // record and resume forward time.
                    rs.set_time = t_srch;
                    rs.set_az = taz;
                    rs.set_ok = true;
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!rs.rise_ok) {
                    // found a coarse rise event, go back slower looking for better set
                    dt = FINE_DT;
                    pel = tel;
                }
            }
        } else {
            rs.ever_down = true;
            if (pel > SAT_MIN_EL) {
                if (dt == FINE_DT) {
                    // found a refined rise event (recall we are going backwards).
                    // record and resume forward time but skip if set is within COARSE_DT because we
                    // would jump over it and find the NEXT set.
                    float check_tel, check_taz;
                    DateTime check_set = t_srch + COARSE_DT;
                    sat->predict (check_set);
                    sat->topo (obs, check_tel, check_taz, trange, trate);
                    if (check_tel >= SAT_MIN_EL) {
                        rs.rise_time = t_srch;
                        rs.rise_az = taz;
                        rs.rise_ok = true;
                    }
                    // regardless, resume forward search
                    dt = COARSE_DT;
                    pel = tel;
                } else if (!rs.set_ok) {
                    // found a coarse set event, go back slower looking for better rise
                    dt = FINE_DT;
                    pel = tel;
                }
            }
        }

        // Serial.printf ("R %d S %d dt %ld from_now %8.3fs tel %g\n", rs.rise_ok, rs.set_ok, dt, SECSPERDAY*(t_srch - t_now), tel);

        // advance time and save tel
        t_srch += dt;
        pel = tel;
    }

    // new pass ready
    new_pass = true;

    if (name) {
        int yr;
        uint8_t mo, dy, hr, mn, sc;
        t_now.gettime(yr, mo, dy, hr, mn, sc);
        Serial.printf (
            "SAT: %*s @ %04d-%02d-%02d %02d:%02d:%02d next rise in %6.3f hrs, set in %6.3f (%u ms)\n",
            NV_SATNAME_LEN, name, yr, mo, dy, hr, mn,sc,
            rs.rise_ok ? 24*(rs.rise_time - t_now) : 0.0F, rs.set_ok ? 24*(rs.set_time - t_now) : 0.0F,
            millis() - t0);
    }

}

/* display next pass for sat in sky dome.
 * N.B. we assume findNextPass has been called to fill sat_rs
 */
static void drawSatSkyDome (SatState &s)
{
    // size and center of screen path
    uint16_t r0 = satpass_c.r;
    uint16_t xc = satpass_c.s.x;
    uint16_t yc = satpass_c.s.y;

    // erase sky dome
    tft.fillRect (dx_info_b.x+1, dx_info_b.y+2*FONT_H+1, dx_info_b.w-2, dx_info_b.h-2*FONT_H-1, RA8875_BLACK);

    // skip if no sat or never up
    if (!s.sat || !obs || !s.rs.ever_up)
        return;

    // find n steps, step duration and starting time
    bool full_pass = false;
    int n_steps = 0;
    float step_dt = 0;
    DateTime t;

    if (s.rs.rise_ok && s.rs.set_ok) {

        // find start and pass duration in days
        float pass_duration = s.rs.set_time - s.rs.rise_time;
        if (pass_duration < 0) {
            // rise after set means pass is underway so start now for remaining duration
            DateTime t_now = userDateTime(nowWO());
            pass_duration = s.rs.set_time - t_now;
            t = t_now;
        } else {
            // full pass so start at next rise
            t = s.rs.rise_time;
            full_pass = true;
        }

        // find step size and number of steps
        n_steps = pass_duration/(PASS_STEP/SECSPERDAY) + 1;
        if (n_steps > MAX_PASS_STEPS)
            n_steps = MAX_PASS_STEPS;
        step_dt = pass_duration/n_steps;

    } else {

        // it doesn't actually rise or set within the next 24 hour but it's up some time 
        // so just show it at its current position (if it's up)
        n_steps = 1;
        step_dt = 0;
        t = userDateTime(nowWO());
    }

    // draw horizon and compass points
    #define HGRIDCOL RGB565(50,90,50)
    tft.drawCircle (xc, yc, r0, BRGRAY);
    for (float a = 0; a < 2*M_PIF; a += M_PIF/6) {
        uint16_t xr = lroundf(xc + r0*cosf(a));
        uint16_t yr = lroundf(yc - r0*sinf(a));
        tft.drawLine (xc, yc, xr, yr, HGRIDCOL);
        tft.fillCircle (xr, yr, 1, RA8875_WHITE);
    }

    // draw elevations
    for (uint8_t el = 30; el < 90; el += 30)
        tft.drawCircle (xc, yc, r0*(90-el)/90, HGRIDCOL);

    // label sky directions
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (BRGRAY);
    tft.setCursor (xc - r0, yc - r0 + 2);
    tft.print ("NW");
    tft.setCursor (xc + r0 - 12, yc - r0 + 2);
    tft.print ("NE");
    tft.setCursor (xc - r0, yc + r0 - 8);
    tft.print ("SW");
    tft.setCursor (xc + r0 - 12, yc + r0 - 8);
    tft.print ("SE");

    // connect several points from t until s.rs.set_time, find max elevation for labeling
    float max_el = 0;
    uint16_t max_el_x = 0, max_el_y = 0;
    uint16_t prev_x = 0, prev_y = 0;
    for (uint8_t i = 0; i < n_steps; i++) {

        // find topocentric position @ t
        float el, az, range, rate;
        s.sat->predict (t);
        s.sat->topo (obs, el, az, range, rate);
        if (el < 0 && n_steps == 1)
            break;                                      // only showing pos now but it's down

        // find screen postion
        float r = r0*(90-el)/90;                        // screen radius, zenith at center 
        uint16_t x = xc + r*sinf(deg2rad(az)) + 0.5F;   // want east right
        uint16_t y = yc - r*cosf(deg2rad(az)) + 0.5F;   // want north up

        // find max el
        if (el > max_el) {
            max_el = el;
            max_el_x = x;
            max_el_y = y;
        }

        // connect if have prev or just dot if only one
        if (i > 0 && (prev_x != x || prev_y != y))      // avoid bug with 0-length line
            tft.drawLine (prev_x, prev_y, x, y, SAT_COLOR);
        else if (n_steps == 1)
            tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);

        // label the set end if last step of several and full pass
        if (full_pass && i == n_steps - 1) {
            // x,y is very near horizon, try to move inside a little for clarity
            x += x > xc ? -12 : 2;
            y += y > yc ? -8 : 4;
            tft.setCursor (x, y);
            tft.print('S');
        }

        // save
        prev_x = x;
        prev_y = y;

        // next t
        t += step_dt;
    }

    // label max elevation and time up iff we have a full pass
    if (max_el > 0 && full_pass) {

        // max el
        uint16_t x = max_el_x, y = max_el_y;
        bool draw_left_of_pass = max_el_x > xc;
        bool draw_below_pass = max_el_y < yc;
        x += draw_left_of_pass ? -30 : 20;
        y += draw_below_pass ? 5 : -18;
        tft.setCursor (x, y); 
        tft.print(max_el, 0);
        tft.drawCircle (tft.getCursorX()+2, tft.getCursorY(), 1, BRGRAY);       // simple degree symbol

        // pass duration
        int s_up = (s.rs.set_time - s.rs.rise_time)*SECSPERDAY;
        char tup_str[32];
        if (s_up >= 3600) {
            int h = s_up/3600;
            int m = (s_up - 3600*h)/60;
            snprintf (tup_str, sizeof(tup_str), "%dh%02d", h, m);
        } else {
            int m = s_up/60;
            int s = s_up - 60*m;
            snprintf (tup_str, sizeof(tup_str), "%d:%02d", m, s);
        }
        uint16_t bw = getTextWidth (tup_str);
        if (draw_left_of_pass)
            x = tft.getCursorX() - bw + 4;                                  // account for deg
        y += draw_below_pass ? 12 : -11;
        tft.setCursor (x, y);
        tft.print(tup_str);
    }

}

/* draw name of s IFF used in dx_info box
 */
static void drawSatName (SatState &s)
{
    if (!s.sat || !obs || !SAT_NAME_IS_SET(s) || !dx_info_for_sat || SHOWING_PANE_0())
        return;

    // retrieve saved name without '_'
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);

    // shorten until fits in satname_b
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t bw = maxStringW (user_name, satname_b.w);

    // draw
    tft.setTextColor (SAT_COLOR);
    fillSBox (satname_b, RA8875_BLACK);
    tft.setCursor (satname_b.x + (satname_b.w - bw)/2, satname_b.y+FONT_H - 2);
    tft.print (user_name);
}

/* set s.name_b with where sat name should go on map, else s.name_b.x = 0
 */
static void setSatMapNameLoc (SatState &s)
{
    // set size
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    s.name_b.w = getTextWidth(s.name) + 4;
    s.name_b.h = 11;

    // try near current location but beware edges, use canonical units
    s.name_b.x = s.path[0].x/tft.SCALESZ;
    s.name_b.y = s.path[0].y/tft.SCALESZ;
    if (s.name_b.x) {
        s.name_b.x = CLAMPF (s.name_b.x, map_b.x + 10, map_b.x + map_b.w - s.name_b.w - 10);
        s.name_b.y = CLAMPF (s.name_b.y, map_b.y + 10, map_b.y + map_b.h - s.name_b.h - 10);
    }

    // one last check for over usable map
    if (!overMap (s.name_b))
        s.name_b.x = 0;
}

/* mark current sat pass location 
 */
static void drawSatPassMarker()
{

    SatNow satnow;
    getSatNow (satnow);

    // size and center of screen path
    uint16_t r0 = satpass_c.r;
    uint16_t xc = satpass_c.s.x;
    uint16_t yc = satpass_c.s.y;

    float r = r0*(90-satnow.el)/90;                            // screen radius, zenith at center 
    uint16_t x = xc + r*sinf(deg2rad(satnow.az)) + 0.5F;       // want east right
    uint16_t y = yc - r*cosf(deg2rad(satnow.az)) + 0.5F;       // want north up

    if (y + SAT_UP_R < tft.height() - 1)                       // beware lower edge
        tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);
}

/* draw event label with time dt and current az/el in the dx_info box unless dt < 0 then just show label.
 * dt is in days: if > 1 hour show HhM else M:S
 */
static void drawSatTime (SatState &s, const char *label, uint16_t color, float event_dt, float event_az)
{
    if (!s.sat)
        return;

    // layout
    const uint16_t fast_h = 10;                                 // spacing for FAST_FONT
    const uint16_t rs_y = dx_info_b.y+FONT_H + 4;               // below name
    const uint16_t azel_y = rs_y + fast_h;
    const uint16_t age_y = azel_y + fast_h;

    // erase drawing area
    tft.fillRect (dx_info_b.x+1, rs_y-2, dx_info_b.w-2, 3*fast_h+2, RA8875_BLACK);
    // tft.drawRect (dx_info_b.x+1, rs_y-2, dx_info_b.w-2, 3*fast_h+2, RA8875_GREEN);    // RBF
    tft.setTextColor (color);

    // draw
    if (event_dt >= 0) {

        // fast font
        selectFontStyle (LIGHT_FONT, FAST_FONT);

        // format time as HhM else M:S
        event_dt *= 24;                                         // event_dt is now hours
        int a, b;
        char sep;
        formatSexa (event_dt, a, sep, b);

        // build label + time + az
        char str[100];
        snprintf (str, sizeof(str), "%s %2d%c%02d @ %.0f", label, a, sep, b, event_az);
        uint16_t s_w = getTextWidth(str);
        tft.setCursor (dx_info_b.x + (dx_info_b.w-s_w)/2, rs_y);
        tft.print(str);

        // draw az and el
        DateTime t_now = userDateTime(nowWO());
        float el, az, range, rate;
        s.sat->predict (t_now);
        s.sat->topo (obs, el, az, range, rate);
        snprintf (str, sizeof(str), "Az: %.0f    El: %.0f", az, el);
        tft.setCursor (dx_info_b.x + (dx_info_b.w - getTextWidth(str))/2, azel_y);
        tft.printf (str);

        // draw TLE age
        DateTime t_sat = s.sat->epoch();
        snprintf (str, sizeof(str), "TLE Age %.1f days", t_now-t_sat);
        uint16_t a_w = getTextWidth(str);
        tft.setCursor (dx_info_b.x + (dx_info_b.w-a_w)/2, age_y);
        tft.print(str);

    } else {

        // just draw label centered across entire box

        selectFontStyle (LIGHT_FONT, SMALL_FONT);               // larger font
        uint16_t s_w = getTextWidth(label);
        tft.setCursor (dx_info_b.x + (dx_info_b.w-s_w)/2, rs_y + FONT_H - FONT_D);
        tft.print(label);
    }
}

/* return whether the given line appears to be a valid TLE
 * only count digits and '-' counts as 1
 */
static bool tleHasValidChecksum (const char *line)
{
    // sum first 68 chars
    int sum = 0;
    for (uint8_t i = 0; i < 68; i++) {
        char c = *line++;
        if (c == '-')
            sum += 1;
        else if (c == '\0')
            return (false);         // too short
        else if (c >= '0' && c <= '9')
            sum += c - '0';
    }

    // last char is sum of previous modulo 10
    return ((*line - '0') == (sum%10));
}

/* clear screen, show the given message then restart operation after user ack.
 */
static void fatalSatError (const char *fmt, ...)
{
    // common prefix
    char buf[65] = "Sat error: ";               // max on one line
    va_list ap;

    // format message to fit after prefix
    int prefix_l = strlen (buf);
    va_start (ap, fmt);
    vsnprintf (buf+prefix_l, sizeof(buf)-prefix_l, fmt, ap);
    va_end (ap);

    // log 
    Serial.println (buf);

    // clear screen and show message centered
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t mw = getTextWidth (buf);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor ((tft.width()-mw)/2, tft.height()/3);
    tft.print (buf);

    // ok button
    SBox ok_b;
    const char button_msg[] = "Continue";
    uint16_t bw = getTextWidth(button_msg);
    ok_b.x = (tft.width() - bw)/2;
    ok_b.y = tft.height() - 40;
    ok_b.w = bw + 30;
    ok_b.h = 35;
    drawStringInBox (button_msg, ok_b, false, RA8875_WHITE);

    // wait forever for user to do anything
    UserInput ui = {
        ok_b,
        UI_UFuncNone,
        UF_UNUSED,
        UI_NOTIMEOUT,
        UF_NOCLOCKS,
        {0, 0}, TT_NONE, '\0', false, false
    };
    (void) waitForUser (ui);

    // restart without sats
    for (int i = 0; i < MAX_ACTIVE_SATS; i++)
        unsetSat (sat_state[i]);
    initScreen();
}


/* return whether sat epoch is known to be good at the given time.
 */
static bool satEpochOk (Satellite *sat, const char *name, time_t t)
{
    if (!sat)
        return (false);

    DateTime t_now = userDateTime(t);
    DateTime t_sat = sat->epoch();

    // N.B. can not use isSatMoon because sat_name is not set
    float max_age = strcasecmp(name,"Moon") == 0 ? 1.5F : maxTLEAgeDays();

    bool ok = t_sat + max_age > t_now && t_now + max_age > t_sat;

    if (!ok) {
        int year;
        uint8_t mon, day, h, m, s;
        Serial.printf ("SAT: %s age %g > %g days:\n", name, t_now - t_sat, max_age);
        t_now.gettime (year, mon, day, h, m, s);
        Serial.printf ("SAT: Ep: now = %d-%02d-%02d  %02d:%02d:%02d\n", year, mon, day, h, m, s);
        t_sat.gettime (year, mon, day, h, m, s);
        Serial.printf ("SAT:     sat = %d-%02d-%02d  %02d:%02d:%02d\n", year, mon, day, h, m, s);
    }

    return (ok);

}

/* each call returns the next TLE from user's file then seamlessly from the backend server.
 * first time: call with fp = NULL and state = RNS_INIT then leave them alone for us to manage.
 * we return true if another TLE was found from either source, else false with fp closed.
 * N.B. if caller wants to stop calling us before we return false, they must fclose(fp) if it's != NULL.
 * N.B. name[] will be in the internal '_' format
 */
static bool readNextSat (FILE *&fp, RNS_t &state,
char name[NV_SATNAME_LEN], char t1[TLE_LINEL], char t2[TLE_LINEL])
{
    // prep for user, then server, then done.
  next:

    if (state == RNS_INIT) {
        if (!fp) {
            fp = fopenOurs (esat_ufn, "r");
            if (!fp) {
                if (debugLevel (DEBUG_ESATS, 1))
                    Serial.printf ("SAT: %s: %s\n", esat_ufn, strerror (errno));
                state = RNS_SERVER;
            }
        }
    }

    if (state == RNS_SERVER) {
        if (!fp) {
            fp = openCachedFile (esat_sfn, esat_url, MAX_CACHE_AGE, 0);     // ok if empty
            if (!fp) {
                Serial.printf ("SAT: no server sats file\n");
                state = RNS_DONE;
            }
        }
    }

    if (state == RNS_DONE)
        return (false);



    // find next 3 lines other than comments or blank
    int n_found;
    for (n_found = 0; n_found < 3; ) {

        // read next useful line
        char line[TLE_LINEL+10];
        if (fgets (line, sizeof(line), fp) == NULL)
            break;
        chompString(line);
        if (line[0] == '#' || line[0] == '\0')
            continue;

        // assign
        switch (n_found) {
        case 0:
            line[NV_SATNAME_LEN-1] = '\0';
            strTrimAll(line);
            strncpySubChar (name, line, '_', ' ', NV_SATNAME_LEN);      // internal name form
            n_found++;
            break;
        case 1:
            strTrimEnds(line);
            quietStrncpy (t1, line, TLE_LINEL);
            n_found++;
            break;
        case 2:
            strTrimEnds(line);
            quietStrncpy (t2, line, TLE_LINEL);
            n_found++;
            break;
        }
    }

    if (n_found == 3) {

        if (debugLevel (DEBUG_ESATS, 1)) {
            Serial.printf ("SAT: found TLE from %s:\n", state == RNS_INIT ? "user" : "server");
            Serial.printf ("   '%s'\n", name);
            Serial.printf ("   '%s'\n", t1);
            Serial.printf ("   '%s'\n", t2);
        }

    } else {

        // no more from this file
        if (debugLevel (DEBUG_ESATS, 1))
            Serial.printf ("SAT: no more TLE from %s\n", state == RNS_INIT ? "user" : "server");

        // close fp
        fclose (fp);
        fp = NULL;

        // advance to next state
        switch (state) {
        case RNS_INIT:   state = RNS_SERVER; break;
        case RNS_SERVER: state = RNS_DONE; break;
        case RNS_DONE:   break;
        }

        // resume
        goto next;
    }

    // if get here one or the other was a success
    return (true);
}

/* look up name. if found set up sat, else inform user and remove sat altogether.
 * return whether found it.
 */
static bool satLookup (SatState &s)
{
    if (!SAT_NAME_IS_SET(s))
        return (false);

    if (debugLevel (DEBUG_ESATS, 1))
        Serial.printf ("SAT: Looking up '%s'\n", s.name);

    // delete then restore if found
    if (s.sat) {
        delete s.sat;
        s.sat = NULL;
    }

    // prepare for readNextSat()
    FILE *rns_fp = NULL;
    RNS_t rns_state = RNS_INIT;

    // read and check each name
    char name[NV_SATNAME_LEN];
    char t1[TLE_LINEL];
    char t2[TLE_LINEL];
    bool ok = false;
    char err_msg[100] = "";                     // user default err msg if this stays ""
    while (!ok && readNextSat (rns_fp, rns_state, name, t1, t2)) {
        if (strcasecmp (name, s.name) == 0) {
            if (!tleHasValidChecksum (t1))
                snprintf (err_msg, sizeof(err_msg), "Bad checksum for %s TLE line 1", name);
            else if (!tleHasValidChecksum (t2))
                snprintf (err_msg, sizeof(err_msg), "Bad checksum for %s TLE line 2", name);
            else
                ok = true;
        }
    }

    // finished with fp regardless
    if (rns_fp)
        fclose(rns_fp);

    // final check
    if (ok) {
        // TLE looks good: define new sat and clear any prior missing flag
        s.sat = new Satellite (t1, t2);
        clearMissingSat (s.name);
    } else {
        // NOT fatal: just log, remember the name as missing so the chooser can mark it,
        // and let the caller drop this sat. The app keeps running normally.
        if (err_msg[0])
            Serial.printf ("SAT: %s\n", err_msg);
        else
            Serial.printf ("SAT: %s disappeared\n", s.name);
        noteMissingSat (s.name);
    }

    return (ok);
}

/* show table selection box marked or not
 */
static void showSelectionBox (int r, int c, bool on)
{
    const uint16_t x = c*CELL_W;
    const uint16_t y = TBORDER + r*CELL_H;

    uint16_t fill_color = on ? BTN_COLOR : RA8875_BLACK;
    tft.fillRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, fill_color);
    tft.drawRect (x, y+(CELL_H-CB_SIZE)/2+3, CB_SIZE, CB_SIZE, RA8875_WHITE);
}

/* return whether one of sat_state[] is the given name
 */
static bool satNameIsActive (const char *name)
{
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {
        SatState &s = sat_state[i];
        if (s.sat && SAT_NAME_IS_SET(s) && strcasecmp (s.name, name) == 0)
            return (true);
    }
    return (false);
}

/* show all names and allow op to choose up to two.
 * save selections and return count therein.
 */
static int askSat (char selections[MAX_ACTIVE_SATS][NV_SATNAME_LEN])
{
    // init count
    int n_selections = 0;

    // entire display is one big menu box
    SBox screen_b;
    screen_b.x = 0;
    screen_b.y = 0;
    screen_b.w = tft.width();
    screen_b.h = tft.height();

    // handy
    time_t now = nowWO();

    // prep for user input (way up here to avoid goto warnings)
    UserInput ui = {
        screen_b,
        UI_UFuncNone,
        UF_UNUSED,
        MENU_TO,
        UF_NOCLOCKS,
        {0, 0}, TT_NONE, '\0', false, false
    };

    // don't inherit anything lingering after the tap that got us here
    drainTouch();

    // erase screen and set font
    eraseScreen();
    tft.setTextColor (RA8875_WHITE);

    // show title and prompt
    uint16_t title_y = 3*TBORDER/4;
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (5, title_y);
    tft.print ("Select up to two satellites");

    // show rise units
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (tft.width()-450, title_y);
    tft.print ("Rise in HH:MM");

    // show what SOON_COLOR means
    tft.setTextColor (SOON_COLOR);
    tft.setCursor (tft.width()-280, title_y);
    tft.printf ("<%d Mins", SOON_MINS);

    // show what SATUP_COLOR means
    tft.setTextColor (SATUP_COLOR);
    tft.setCursor (tft.width()-170, title_y);
    tft.print ("Up Now");

    // show Ok button
    drawStringInBox ("Ok", ok_b, false, RA8875_WHITE);


    // storage for each posible name and n used
    // N.B. stored alphabetically in column-major order
    char sat_table[MAX_NSAT][NV_SATNAME_LEN];
    int n_sat_table;

    // prepare for readNextSat()
    FILE *rns_fp = NULL;
    RNS_t rns_state = RNS_INIT;


    //*******************************************************************************************
    // display table, highlighting and adding to selections[] any names already in sat_state[]
    //*******************************************************************************************

    // read up to MAX_NSAT and display each name, allow tapping part way through to stop
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (n_sat_table = 0; n_sat_table < MAX_NSAT; n_sat_table++) {

        // handy
        char *tbl_name = sat_table[n_sat_table];

        // read user's file until it's empty, then read from server
        char t1[TLE_LINEL];
        char t2[TLE_LINEL];
        if (!readNextSat (rns_fp, rns_state, tbl_name, t1, t2))
            break;

        // row and column, col-major order
        int r = n_sat_table % N_ROWS;
        int c = n_sat_table / N_ROWS;

        // ul corner of this cell
        SCoord cell_s;
        cell_s.x = c*CELL_W;
        cell_s.y = TBORDER + r*CELL_H;

        // allow early stop by tapping while drawing matrix
        SCoord tap_s;
        if (readCalTouchWS (tap_s) != TT_NONE || tft.getChar(NULL,NULL) != 0) {
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (cell_s.x, cell_s.y + FONT_H);
            tft.print ("Listing stopped");
            break;
        }

        // draw tick box, saved and pre-selected if it's one we already have
        if (satNameIsActive (tbl_name)) {
            if (n_selections >= MAX_ACTIVE_SATS)
                fatalError ("bogus build n_selections %d for sat %s", n_selections, tbl_name);
            strcpy (selections[n_selections++], tbl_name);
            showSelectionBox (r, c, true);
        } else
            showSelectionBox (r, c, false);

        // display next rise time of this sat
        Satellite *sat = new Satellite (t1, t2);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (cell_s.x + CB_SIZE + 8, cell_s.y + FONT_H);
        if (satEpochOk(sat, tbl_name, now)) {
            SatRiseSet rs;
            findNextPass (sat, tbl_name, now, rs);
            if (rs.rise_ok) {
                DateTime t_now = userDateTime(now);
                if (rs.rise_time < rs.set_time) {
                    // pass lies ahead
                    float hrs_to_rise = (rs.rise_time - t_now)*24.0;
                    if (hrs_to_rise*60 < SOON_MINS)
                        tft.setTextColor (SOON_COLOR);
                    int mins_to_rise = (hrs_to_rise - floor(hrs_to_rise))*60;
                    if (hrs_to_rise < 1 && mins_to_rise < 1)
                        mins_to_rise = 1;   // 00:00 looks wrong
                    if (hrs_to_rise < 10)
                        tft.print ('0');
                    tft.print ((uint16_t)hrs_to_rise);
                    tft.print (':');
                    if (mins_to_rise < 10)
                        tft.print ('0');
                    tft.print (mins_to_rise);
                    tft.print (' ');
                } else {
                    // pass in progress
                    tft.setTextColor (SATUP_COLOR);
                    tft.print ("Up ");
                }
            } else if (!rs.ever_up) {
                tft.setTextColor (GRAY);
                tft.print ("NoR ");
            } else if (!rs.ever_down) {
                tft.setTextColor (SATUP_COLOR);
                tft.print ("NoS ");
            }
        } else {
            tft.setTextColor (GRAY);
            tft.print ("Age ");
        }

        // recycle sat
        delete sat;

        // followed by scrubbed name
        char user_name[NV_SATNAME_LEN];
        strncpySubChar (user_name, tbl_name, ' ', '_', NV_SATNAME_LEN);
        tft.print (user_name);
    }

    // append any sats that went missing so the user can see what happened instead of
    // getting a fatal error. these are shown in red with a "Gone" marker and are NOT
    // selectable: n_sat_table is left pointing past them so the tap bounds check rejects
    // taps in this region.
    {
        int slot = n_sat_table;
        for (int m = 0; m < n_missing_sats && slot < MAX_NSAT; m++) {

            // skip if this missing name actually showed up in the live table above
            bool present = false;
            for (int k = 0; k < n_sat_table; k++) {
                if (strcasecmp (sat_table[k], missing_sats[m]) == 0) { present = true; break; }
            }
            if (present)
                continue;

            int r = slot % N_ROWS;
            int c = slot / N_ROWS;
            SCoord cell_s;
            cell_s.x = c*CELL_W;
            cell_s.y = TBORDER + r*CELL_H;

            // empty (unchecked, but not drawn as selectable) box
            showSelectionBox (r, c, false);

            // status + name in the dedicated "gone" color
            tft.setTextColor (GONE_COLOR);
            tft.setCursor (cell_s.x + CB_SIZE + 8, cell_s.y + FONT_H);
            tft.print ("Gone ");
            char user_name[NV_SATNAME_LEN];
            strncpySubChar (user_name, missing_sats[m], ' ', '_', NV_SATNAME_LEN);
            tft.print (user_name);

            slot++;
        }
    }

    // bale if no satellites displayed
    if (n_sat_table == 0)
        goto out;


    //**************************************************************************************************
    // operate table by taps, updating selections[] which already contain active sat_state names, if any
    //**************************************************************************************************

    // follow input to make selections
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    while (waitForUser (ui)) {

        // tap Ok button or type Enter or ESC
        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC || inBox (ui.tap, ok_b)){
            // show Ok button highlighted
            drawStringInBox ("Ok", ok_b, true, RA8875_WHITE);
            wdDelay(200);
            goto out;
        }

        // skip if any other char -- we only support tap control
        if (ui.kb_char != CHAR_NONE)
            continue;

        // find table index at tap
        int r = (ui.tap.y - TBORDER)/CELL_H;
        int c = ui.tap.x/CELL_W;
        int tbl_idx = c*N_ROWS + r;                     // column-major order
        if (r < 0 || r >= N_ROWS || c < 0 || c >= N_COLS || tbl_idx < 0 || tbl_idx >= n_sat_table)
            continue;

        // update tapped cell, maintaining rule that selections[1] is used only if [0] also used
        const char *tbl_name = sat_table[tbl_idx];
        if (n_selections == 0) {
            // first selection
            strcpy (selections[n_selections++], tbl_name);
            showSelectionBox (r, c, true);
        } else if (n_selections == 1) {
            if (strcmp (tbl_name, selections[0]) == 0) {
                // remove from selections[0] and toggle off
                n_selections = 0;
                showSelectionBox (r, c, false);
            } else {
                // add to selections[] and toggle on
                strcpy (selections[n_selections++], tbl_name);
                showSelectionBox (r, c, true);
            }
        } else if (n_selections == 2) {
            if (strcmp (tbl_name, selections[0]) == 0) {
                // tapped [0] so remove by copying from [1] and toggle off
                strcpy (selections[0], selections[1]);
                n_selections = 1;
                showSelectionBox (r, c, false);
            } else if (strcmp (tbl_name, selections[1]) == 0) {
                // tapped [1] so just drop count and toggle off
                n_selections = 1;
                showSelectionBox (r, c, false);
            }
        } else
            fatalError ("bogus use n_selections %d for sat %s", n_selections, tbl_name);

    }

  out:

    // one final fclose in case we didn't read all
    if (rns_fp)
        fclose (rns_fp);

    if (n_sat_table == 0) {
        fatalSatError ("%s", "No satellites found");
        return (false);
    }

    return (n_selections);
}

/* use rs to catagorize the state of a pass.
 * if optional days and az are provided these also return timing info:
 *   if return PS_NONE then values are not modified
 *   if return PS_UPSOON then days is days until rise, az is rise
 *   if return PS_UPNOW then days is days until set, az is set
 *   if return PS_HASSET then days is days since set, az is unused
 */
static PassState findPassState (SatRiseSet &rs, float *days, float *az)
{
    PassState ps;

    DateTime t_now = userDateTime(nowWO());

    if (!rs.ever_up || !rs.ever_down) {

        ps = PS_NONE;

    } else if (rs.rise_time < rs.set_time) {

        if (t_now < rs.rise_time) {
            // pass lies ahead
            ps = PS_UPSOON;
            if (days && az) {
                *days = rs.rise_time - t_now;
                *az = rs.rise_az;
            }
        } else if (t_now < rs.set_time) {
            // pass in progress
            ps = PS_UPNOW;
            if (days && az) {
                *days = rs.set_time - t_now;
                *az = rs.set_az;
            }
        } else {
            // just set
            ps = PS_HASSET;
            if (days)
                *days = t_now - rs.set_time;
        }

    } else {

        if (t_now < rs.set_time) {
            // pass in progress
            ps = PS_UPNOW;
            if (days && az) {
                *days = rs.set_time - t_now;
                *az = rs.set_az;
            }
        } else {
            // just set
            ps = PS_HASSET;
            if (days)
                *days = t_now - rs.set_time;
        }
    }

    return (ps);
}

/* called often to keep s.sat and s.rs updated, including creating s.sat if a s.name is known.
 * return whether ok to use and, if so, whether elements or s.rs were also updated (if care).
 */
static bool checkSatUpToDate (SatState &s, bool *updated)
{
    // bale fast if no obs or not even a name
    if (!obs || !SAT_NAME_IS_SET(s))
        return (false);

    // do fresh lookup in case local file changed but first capture current epoch to check if updated
    // (don't worry, sat files are heavily cached locally)
    DateTime e0_dt = s.sat ? s.sat->epoch() : DateTime();
    if (!satLookup(s))
        return (false);                         // already posted error

    // confirm age still ok
    time_t now_wo = nowWO();
    if (!satEpochOk (s.sat, s.name, now_wo)) {
        // not fatal: flag as missing/stale and let caller drop it
        Serial.printf ("SAT: Epoch for %s is out of date\n", s.name);
        noteMissingSat (s.name);
        return (false);
    }

    // check if epoch changed
    DateTime e1_dt = s.sat->epoch();
    float e_diff = e1_dt - e0_dt;
    bool new_epoch = e_diff != 0;

    // update rs too if new epoch or just set
    if (new_epoch || findPassState(s.rs, NULL, NULL) == PS_HASSET) {
        findNextPass (s.sat, s.name, now_wo, s.rs);
        if (updated)
            *updated = true;
    } else {
        if (updated)
            *updated = false;
    }

    // lookup succeeded regardless of whether elements changed
    return (true);
}

/* show pass time of sat_rs(void)
 */
static void drawSatRSEvents(SatState &s)
{
    if (SHOWING_PANE_0())
        return;

    float days, az;

    switch (findPassState (s.rs, &days, &az)) {

    case PS_NONE:
        // neither
        if (!s.rs.ever_up)
            drawSatTime (s, "No rise", SAT_COLOR, -1, 0);
        else if (!s.rs.ever_down)
            drawSatTime (s, "No set", SAT_COLOR, -1, 0);
        else
            fatalError ("Bug! no rise/set from PS_NONE");
        break;

    case PS_UPSOON:
        // pass lies ahead
        drawSatTime (s, "Rise in", SAT_COLOR, days, az);
        break;

    case PS_UPNOW:
        // pass in progress
        drawSatTime (s, "Set in", SAT_COLOR, days, az);
        drawSatPassMarker();
        break;

    case PS_HASSET:
        // just set
        break;
    }
}

/* operate the LED alarm GPIO pin depending on current state of pass
 */
static void checkLEDAlarmEvents()
{
    // get current sat
    int cs = currentSat();
    if (!obs || cs == NO_CUR_SAT)
        return;
    SatState &s = sat_state[cs];

    float days, az;

    switch (findPassState (s.rs, &days, &az)) {

    case PS_NONE:
        // no pass: turn off
        risetAlarm(BLINKER_OFF);
        break;

    case PS_UPSOON:
        // pass lies ahead: flash if within ALARM_DT
        risetAlarm(days < ALARM_DT ? SATLED_RISING_HZ : BLINKER_OFF);
        break;

    case PS_UPNOW:
        // pass in progress: check for soon to set
        risetAlarm(days < ALARM_DT ? SATLED_SETTING_HZ : BLINKER_ON);
        break;

    case PS_HASSET:
        // set: turn off
        risetAlarm(BLINKER_OFF);
        break;
    }
}

/* set new satellite observer location to de_ll and update all sat info to current time and loc.
 * return whether ready to go.
 */
bool setNewSatCircumstance (void)
{
    // update obs, used by both sats
    if (obs)
        delete obs;
    obs = new Observer (de_ll.lat_d, de_ll.lng_d, 0);

    // update each active sat
    int n_ok = 0;
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {
        SatState &s = sat_state[i];
        if (s.sat) {
            bool updated = false;
            bool ok = checkSatUpToDate (s, &updated);
            if (!ok)
                unsetSat(s);
            else {
                n_ok++;
                if (!updated)
                    findNextPass (s.sat, s.name, nowWO(), s.rs);
            }
        }
    }

    return (n_ok > 0);
}

/* handy getSatCir() for right now
 */
bool getSatNow (SatNow &satnow)
{
    return (getSatCir (obs, nowWO(), satnow));
}


/* if a satellite is currently in play, return its name, az, el, range, rate, az of next rise and set,
 *    and hours until next rise and set at time t0.
 * even if return true, rise and set az may be SAT_NOAZ, for example geostationary, in which case rdt
 *    and sdt are undefined.
 * N.B. if sat is up at t0, rdt could be either < 0 to indicate time since previous rise or > sdt
 *    to indicate time until rise after set
 */
bool getSatCir (Observer *snow_obs, time_t t0, SatNow &sat_at_t0)
{
    // get current sat, if any
    int cs = currentSat();
    if (!obs || cs == NO_CUR_SAT)
        return (false);
    SatState &s = sat_state[cs];

    // public name
    strncpySubChar (sat_at_t0.name, s.name, ' ', '_', NV_SATNAME_LEN);

    // compute location now
    DateTime t_now = userDateTime(t0);
    s.sat->predict (t_now);
    s.sat->topo (snow_obs, sat_at_t0.el, sat_at_t0.az, sat_at_t0.range, sat_at_t0.rate);

    // horizon info, if available
    sat_at_t0.raz = s.rs.rise_ok ? s.rs.rise_az : SAT_NOAZ;
    sat_at_t0.saz = s.rs.set_ok  ? s.rs.set_az  : SAT_NOAZ;

    // times
    if (s.rs.rise_ok)
        sat_at_t0.rdt = (s.rs.rise_time - t_now)*24;
    if (s.rs.set_ok)
        sat_at_t0.sdt = (s.rs.set_time - t_now)*24;

    // ok
    return (true);
}

/* display full sat pass unless !dx_info_for_sat
 */
static void drawSatPass (SatState &s)
{
    if (!obs || !dx_info_for_sat || SHOWING_PANE_0())
        return;

    // erase outside the box
    tft.fillRect (dx_info_b.x-1, dx_info_b.y-1, dx_info_b.w+2, dx_info_b.h+2, RA8875_BLACK);
    tft.drawRect (dx_info_b.x-1, dx_info_b.y-1, dx_info_b.w+2, dx_info_b.h+2, GRAY);

    drawSatName(s);
    drawSatRSEvents(s);
    drawSatSkyDome(s);
}

/* public version that shows the "current" satellite
 */
void drawSatPass (void)
{
    // get current sat
    int cs = currentSat();
    if (cs == NO_CUR_SAT)
        return;
    drawSatPass (sat_state[cs]);
}

/* called by main loop() to update _pass_ info for current sat, get out fast if nothing to do.
 * the _path_ is updated much less often in updateSatPath().
 * N.B. beware this is called by loop() while stopwatch is up
 * N.B. update rs even if !dx_info_for_sat so drawSatPath can draw rise/set time in name_b
 */
void updateSatPass()
{
    // get current sat
    int cs = currentSat();
    if (!obs || cs == NO_CUR_SAT)
        return;
    SatState &s = sat_state[cs];

    // always operate the LED at full rate
    checkLEDAlarmEvents();

    // other stuff once per second is fine
    static uint32_t last_run;
    if (!timesUp(&last_run, 1000))
        return;

    // done if can't even get the basics
    bool fresh_update;
    if (!checkSatUpToDate (s, &fresh_update))
        return;

    // do minimal display update if showing
    if (dx_info_for_sat && mainpage_up) {
        if (fresh_update) 
            drawSatPass(s);             // full display update
        else
            drawSatRSEvents(s);         // just refesh times
    }
}

/* compute satellite geocentric _path_ into path[] and footprint into s.foot[].
 * called once at the top of each map sweep.
 * the _pass_ is updated in updateSatPass().
 */
void updateSatPath()
{
    // N.B. do NOT call checkSatUpToDate() here -- it can cause updateSatPass() to miss PS_HASSET

    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {

        SatState &s = sat_state[i];

        // ski if not defined
        if (!s.sat || !obs || !SAT_NAME_IS_SET(s))
            continue;

        // from here we have a valid sat to report

        // free s.path first since it was last to be malloced
        if (s.path) {
            free (s.path);
            s.path = NULL;
        }

        // fill s.foot
        time_t t_wo = nowWO();
        DateTime t = userDateTime(t_wo);
        float satlat, satlng;
        s.sat->predict (t);
        s.sat->geo (satlat, satlng);
        if (debugLevel (DEBUG_ESATS, 2))
            Serial.printf ("SAT: JD %.6f Lat %7.3f Lng %8.3f\n", t_wo/86400.0+2440587.5,
                                                        rad2deg(satlat), rad2deg(satlng));
        updateFootPrint (s, satlat, satlng);
        updateClocks(false);

        // start s.path max size, then reduce when know size needed
        s.path = (SCoord *) malloc (MAX_PATHPTS * sizeof(SCoord));
        if (!s.path)
            fatalError ("No memory for satellite path");

        // decide line width, if used
        int lw = getRawPathWidth(s.cs);

        // fill s.path
        float period = s.sat->period();
        s.n_path = 0;

        // moon is just the current location
        uint16_t max_path = !strcasecmp (s.name, "Moon") ? 1 : MAX_PATHPTS;

        int dashed = 0;
        for (uint16_t p = 0; p < max_path; p++) {

            // place dashed line points off screen courtesy overMap()
            if (getPathDashed(s.cs) && (dashed++ & (MAX_PATHPTS>>5))) {   // first always on for center dot
                s.path[s.n_path] = {OFFSCRN, OFFSCRN};
            } else {
                // compute next point along path
                ll2sRaw (satlat, satlng, s.path[s.n_path], 2*lw);   // allow for end dot
            }

            // skip duplicate points
            if (s.n_path == 0 || memcmp (&s.path[s.n_path], &s.path[s.n_path-1], sizeof(SCoord)))
                s.n_path++;

            t += period/max_path;   // show 1 rev
            s.sat->predict (t);
            s.sat->geo (satlat, satlng);
        }

        updateClocks(false);
        // Serial.printf ("%s n_path %u / %u\n", s.name, s.n_path, MAX_PATHPTS);

        // reduce memory to only points actually used
        s.path = (SCoord *) realloc (s.path, s.n_path * sizeof(SCoord));

        // set map name location
        setSatMapNameLoc(s);
    }
}

/* draw the entire sat paths and footprints, connecting points with lines.
 */
void drawSatPathAndFoot()
{
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {

        SatState &s = sat_state[i];
        if (!s.sat)
            continue;

        // draw path if on with arrows
        int pw = getRawPathWidth(s.cs);
        if (s.show_path && pw) {
            static const float cos_20 = 0.940F;
            static const float sin_20 = 0.342F;
            const bool dashed = getPathDashed(s.cs);
            uint16_t pc = getMapColor(s.cs);
            int prev_vis_i = 0;                             // last i drawn in this segment
            int last_vis_i;                                 // very last path i that is visible
            bool prev_vis = true;                           // whether previous point was visible
            for (last_vis_i = s.n_path-1; s.path[last_vis_i].x == OFFSCRN; --last_vis_i )
                continue;
            for (int i = 1; i < s.n_path; i++) {
                SCoord &sp0 = s.path[i-1];
                SCoord &sp1 = s.path[i];
                if (segmentSpanOkRaw(sp0, sp1, 2*pw)) {
                    if (i == 1) {
                        // first coord is always the current location, show only if visible
                        // N.B. set ll2s edge to accommodate this dot
                        tft.fillCircleRaw (sp0.x, sp0.y, 2*pw, pc);
                        tft.drawCircleRaw (sp0.x, sp0.y, 2*pw, RA8875_BLACK);
                    }

                    // shouldn't matter but this aligns with arrows better than sp0.sp1
                    tft.drawLineRaw (sp1.x, sp1.y, sp0.x, sp0.y, pw, pc);

                    // directional arrow flares out 20 degs from sp1 or last point drawn if dashed
                    if (i == last_vis_i || 
                                    ((dashed && !prev_vis) || (!dashed && (i % ARROW_EVERY) == 0))) {
                        int tip_i = dashed && !prev_vis ? prev_vis_i : i;         // tip index
                        SCoord &arrow_tip = s.path[tip_i];                        // tip point
                        SCoord &arrow_flare = s.path[tip_i - ARROW_EVERY/2];      // flare point halfway back
                        if (segmentSpanOkRaw(arrow_tip, arrow_flare, 2*pw)) {
                            int path_dx = (int)arrow_flare.x - (int)arrow_tip.x;  // dx tip to flare
                            int path_dy = (int)arrow_flare.y - (int)arrow_tip.y;  // dy tip to flare
                            float path_len = hypotf (path_dx, path_dy);
                            float arrow_dx = path_dx * ARROW_L * pw / path_len;
                            float arrow_dy = path_dy * ARROW_L * pw / path_len;
                            float ccw_dx =  arrow_dx * cos_20 - arrow_dy * sin_20;
                            float ccw_dy =  arrow_dx * sin_20 + arrow_dy * cos_20;
                            float cw_dx  =  arrow_dx * cos_20 + arrow_dy * sin_20;
                            float cw_dy  = -arrow_dx * sin_20 + arrow_dy * cos_20;
                            SCoord ccw, cw;
                            ccw.x =  roundf (arrow_tip.x + ccw_dx/2);
                            ccw.y =  roundf (arrow_tip.y + ccw_dy/2);
                            cw.x  =  roundf (arrow_tip.x + cw_dx/2);
                            cw.y  =  roundf (arrow_tip.y + cw_dy/2);
                            if (segmentSpanOkRaw(arrow_tip, ccw, 2*pw) && segmentSpanOkRaw(arrow_tip, cw, 2*pw))
                                tft.fillTriangleRaw (arrow_tip.x, arrow_tip.y, ccw.x, ccw.y, cw.x, cw.y, pc);
                        }
                    }

                    // update state for dashed arrows
                    prev_vis_i = i;
                    prev_vis = true;

                } else
                    prev_vis = false;
            }
        }

        // draw foots
        int fw = getRawPathWidth(s.cs);
        if (fw) {
            uint16_t fc = getMapColor(s.cs);
            for (int alt_i = 0; alt_i < N_FOOT; alt_i++) {
                for (uint16_t foot_i = 0; foot_i < s.n_foot[alt_i]; foot_i++) {
                    SCoord &sf0 = s.foot[alt_i][foot_i];
                    SCoord &sf1 = s.foot[alt_i][(foot_i+1)%s.n_foot[alt_i]];   // closure!
                    if (segmentSpanOkRaw (sf0, sf1, 1))
                        tft.drawLineRaw (sf0.x, sf0.y, sf1.x, sf1.y, fw, fc);
                }
            }
        }
    }
}

/* draw sat name in name_b if all conditions are met.
 */
void drawSatName (void)
{
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {

        SatState &s = sat_state[i];

        // check a myriad of conditions (!)
        if (!s.sat || !obs || s.name_b.x == 0 || (dx_info_for_sat && !SHOWING_PANE_0() && nActiveSats()<2))
            return;

        // retrieve saved name without '_'
        char user_name[NV_SATNAME_LEN];
        strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);

        // draw name
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        uint16_t un_x = s.name_b.x + 2;
        uint16_t un_y = s.name_b.y + 2;
        fillSBox (s.name_b, RA8875_BLACK);
        drawSBox (s.name_b, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (un_x, un_y);
        tft.print (user_name);
    }
}

/* return whether user has tapped near the head of a satellite path or in a map name
 * and if so, set dxpaneSat
 */
bool checkSatMapTouch (const SCoord &tap)
{
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {

        SatState &s = sat_state[i];

        // skip if no sat
        if (!s.sat || !s.path)
            continue;

        // allow tapping near the current location or over the name 
        SBox now_b;
        now_b.x = s.path[0].x/tft.SCALESZ - SAT_TOUCH_R;
        now_b.y = s.path[0].y/tft.SCALESZ - SAT_TOUCH_R;
        now_b.w = 2*SAT_TOUCH_R;
        now_b.h = 2*SAT_TOUCH_R;

        if (inBox (tap, now_b) || inBox (tap, s.name_b)) {
            dxpaneSat = i;
            return (true);
        }
    }

    return (false);
}

/* return whether user has tapped the "DX" label while showing DX info which means op wants
 * to set a new satellite
 */
bool checkSatNameTouch (const SCoord &s)
{
    if (!dx_info_for_sat) {
        // check just the left third so symbol (*) and TZ button are not included
        SBox lt_b = {dx_info_b.x, dx_info_b.y, (uint16_t)(dx_info_b.w/3), 30};
        return (inBox (s, lt_b));
    } else {
        return (false);
    }
}

/* present list of satellites and let user select up to two, preselecting last known if any.
 * return whether any sat was chosen or not.
 * N.B. caller must call initScreen on return regardless
 */
bool querySatSelection()
{
    // we need the whole screen
    closeGimbal();          // avoid dangling connection
    hideClocks();

    // get user's choices.
    // N.B. leave sat_state active in order to show current selection
    char selections[MAX_ACTIVE_SATS][NV_SATNAME_LEN];
    int n_sel = askSat(selections);

    // reset all currently active
    for (int i = 0; i < MAX_ACTIVE_SATS; i++)
        unsetSat (sat_state[i]);

    // engage any selections
    for (int i = 0; i < n_sel; i++) {
        SatState &s = sat_state[i];
        strncpySubChar (s.name, selections[i], '_', ' ', NV_SATNAME_LEN);
        Serial.printf ("SAT: Selected sat '%s'\n", selections[i]);
        if (!satLookup(s))
            return (false);                     // already showed err
        NVWriteString (s.nv_name, s.name);
        findNextPass (s.sat, s.name, nowWO(), s.rs);
        Serial.printf ("SAT: sat '%s' is ready\n", s.name);
    }

    return (n_sel > 0);
}

/* install new satellite as the only sat, if valid, or remove if "none".
 * N.B. calls initScreen() if changes sat
 */
bool setSatFromName (const char *new_name)
{
    // remove all then add if find new_name
    for (int i = 0; i < MAX_ACTIVE_SATS; i++)
        unsetSat(sat_state[i]);
    dxpaneSat = NO_CUR_SAT;

    // remove sat pane too if "none"
    if (strcasecmp (new_name, "none") == 0) {
        dx_info_for_sat = false;
        drawOneTimeDX();
        initEarthMap();
        return (true);
    }

    // stop any tracking
    stopGimbalNow();

    // build internal name
    SatState &s = sat_state[0];
    strncpySubChar (s.name, new_name, '_', ' ', NV_SATNAME_LEN);

    // fresh look up
    if (checkSatUpToDate (s, NULL)) {

        // ok
        dx_info_for_sat = true;
        NVWriteString (s.nv_name, s.name);
        drawSatPass();
        initEarthMap();
        return (true);

    } else {
        // failed
        unsetSat(s);
        return (false);
    }
}

/* install a new satellite from its TLE.
 * return whether all good.
 * N.B. not saved in NV_SATNAME because we won't have the tle
 */
bool setSatFromTLE (const char *name, const char *t1, const char *t2)
{
    if (!tleHasValidChecksum(t1) || !tleHasValidChecksum(t2)) {
        Serial.printf ("Bad TLE checksum for %s\n", name);
        return(false);
    }

    // remove all then add
    for (int i = 0; i < MAX_ACTIVE_SATS; i++)
        unsetSat(sat_state[i]);
    dxpaneSat = NO_CUR_SAT;

    // stop any tracking
    stopGimbalNow();

    // build in first state
    SatState &s = sat_state[0];
    strncpySubChar (s.name, name, '_', ' ', NV_SATNAME_LEN);
    s.sat = new Satellite (t1, t2);

    // create and check
    if (satEpochOk (s.sat, s.name, nowWO())) {

        // ok
        dx_info_for_sat = true;
        findNextPass (s.sat, s.name, nowWO(), s.rs);
        drawSatPass();
        initEarthMap();
        return (true);

    } else {

        unsetSat (s);
        fatalSatError ("Elements for %s are out of data", name);
        return (false);
    }
}

/* called exactly once to return whether there is at least one valid sat in NV.
 * also a good time to insure alarm pin is off.
 */
bool initSat()
{
    // misc
    Serial.printf ("SAT: max tle age set to %d days\n", maxTLEAgeDays());
    risetAlarm(BLINKER_OFF);

    // set obs
    obs = new Observer (de_ll.lat_d, de_ll.lng_d, 0);

    // init each sat -- N.B. must do inline

    SatState &s0 = sat_state[0];
    s0.nv_name = NV_SAT1NAME;
    s0.nv_flags = NV_SAT1FLAGS;
    s0.cs = SAT1_CSPR;
    if (!NVReadString (NV_SAT1NAME, s0.name) || !SAT_NAME_IS_SET(s0) || !checkSatUpToDate (s0, NULL))
        unsetSat(s0);

    uint8_t flags0 = 0;
    if (!NVReadUInt8 (s0.nv_flags, &flags0)) {
        flags0 |= SF_PATH_MASK;
        NVWriteUInt8 (s0.nv_flags, flags0);
    }
    s0.show_path = (flags0 & SF_PATH_MASK) != 0;


    SatState &s1 = sat_state[1];
    s1.nv_name = NV_SAT2NAME;
    s1.nv_flags = NV_SAT2FLAGS;
    s1.cs = SAT2_CSPR;
    if (!NVReadString (NV_SAT2NAME, s1.name) || !SAT_NAME_IS_SET(s1) || !checkSatUpToDate (s1, NULL))
        unsetSat(s1);

    uint8_t flags1 = 0;
    if (!NVReadUInt8 (s1.nv_flags, &flags1)) {
        flags1 |= SF_PATH_MASK;
        NVWriteUInt8 (s1.nv_flags, flags1);
    }
    s1.show_path = (flags1 & SF_PATH_MASK) != 0;

    // N.B. enforce that [1] is active only if [0] is also active
    if (s1.sat && !s0.sat) {

        // rebuild what was in 1 as 0
        strcpy (s0.name, s1.name);
        NVWriteString (s0.nv_name, s1.name);
        s0.show_path = s1.show_path;
        NVWriteUInt8 (s0.nv_flags, s0.show_path ? SF_PATH_MASK : 0);
        unsetSat (s1);                                  // resets s1 name and nv_name
        if (!checkSatUpToDate (s0, NULL)) {
            // not fatal: it was already flagged missing by satLookup; just drop it
            Serial.printf ("SAT: %s disappeared during startup consolidation\n", s0.name);
            unsetSat (s0);
        }
    }

    // set current to lowest set
    if (s0.sat)
        dxpaneSat = 0;
    else if (s1.sat)
        dxpaneSat = 1;
    else
        dxpaneSat = NO_CUR_SAT;

    // return true if either ok
    return (s0.sat || s1.sat);
}

/* return whether new_pass has been set since last call, and always reset.
 */
bool isNewPass()
{
    bool np = new_pass;
    new_pass = false;
    return (np);
}

/* return whether the current satellite is in fact the moon
 */
bool isSatMoon()
{
    int cs = currentSat();
    if (cs == NO_CUR_SAT)
        return (false);
    return (sat_state[cs].sat && !strcasecmp (sat_state[cs].name, "Moon"));
}

/* return malloced array of malloced strings containing all available satellite names and their TLE;
 * last name is NULL. return NULL if trouble.
 * N.B. caller must free each name then array.
 */
const char **getAllSatNames()
{
    // init malloced list of malloced names
    const char **all_names = NULL;
    int n_names = 0;

    // prep for readNextSat
    FILE *rns_fp = NULL;
    RNS_t rns_state = RNS_INIT;

    // read and add each to all_names.
    char name[NV_SATNAME_LEN];
    char t1[TLE_LINEL];
    char t2[TLE_LINEL];
    while (readNextSat (rns_fp, rns_state, name, t1, t2)) {
        all_names = (const char **) realloc (all_names, (n_names+3)*sizeof(const char*));
        all_names[n_names++] = strdup (name);
        all_names[n_names++] = strdup (t1);
        all_names[n_names++] = strdup (t2);
    }

    Serial.printf ("SAT: found %d satellites\n", n_names/3);

    // add NULL then done
    all_names = (const char **) realloc (all_names, (n_names+1)*sizeof(char*));
    all_names[n_names++] = NULL;

    return (all_names);
}

/* produce and return count of parallel lists of next several days UTC rise and set events for the given sat.
 * caller can assume each rises[i] < sets[i].
 * N.B. caller must free each list iff return > 0.
 */
static int nextSatRSEvents (SatState &s, time_t **rises, float **raz, time_t **sets, float **saz)
{

    // start now
    time_t t0 = nowWO();
    DateTime t0dt = userDateTime(t0);
    time_t t = t0;

    // make lists for duration of elements
    int n_table = 0;
    while (satEpochOk(s.sat, s.name, t)) {

        SatRiseSet rs;
        findNextPass (s.sat, s.name, t, rs);

        // avoid messy edge cases
        if (rs.rise_ok && rs.set_ok) {

            // UTC
            time_t rt = t0 + SECSPERDAY*(rs.rise_time - t0dt);
            time_t st = t0 + SECSPERDAY*(rs.set_time - t0dt);
            int up = SECSPERDAY*(rs.set_time - rs.rise_time);

            // avoid messy edge cases
            if (up > 0) {

                // init tables for realloc
                if (n_table == 0) {
                    *rises = NULL;
                    *raz = NULL;
                    *sets = NULL;
                    *saz = NULL;
                }

                *rises = (time_t *) realloc (*rises, (n_table+1) * sizeof(time_t *));
                *raz = (float *) realloc (*raz, (n_table+1) * sizeof(float *));
                *sets = (time_t *) realloc (*sets, (n_table+1) * sizeof(time_t *));
                *saz = (float *) realloc (*saz, (n_table+1) * sizeof(float *));

                (*rises)[n_table] = rt;
                (*raz)[n_table] = rs.rise_az;
                (*sets)[n_table] = st;
                (*saz)[n_table] = rs.set_az;

                n_table++;
            }

            // start next search half an orbit after set
            t = st + s.sat->period()*SECSPERDAY/2;

        } else if (!rs.ever_up || !rs.ever_down) {

            break;
        }

        // don't go completely dead
        updateClocks(false);
    }

    // return count
    return (n_table);
}

/* public version
 */
int nextSatRSEvents (time_t **rises, float **raz, time_t **sets, float **saz)
{
    int cs = currentSat();
    if (cs == NO_CUR_SAT)
        return (0);
    return nextSatRSEvents (sat_state[cs], rises, raz, sets, saz);
}

/* display table of several local DE rise/set events for the given sat using whole screen.
 * return after user has clicked ok or time out.
 * N.B. caller should call initScreen() after return.
 */
static void showNextSatEvents (SatState &s)
{
    // clean
    hideClocks();
    eraseScreen();

    // setup layout
    #define _SNE_LR_B     10                    // left-right border
    #define _SNE_TOP_B    10                    // top border
    #define _SNE_DAY_W    60                    // width of day column
    #define _SNE_HHMM_W   130                   // width of HH:MM@az columns
    #define _SNE_ROWH     34                    // row height
    #define _SNE_TIMEOUT  30000                 // ms
    #define _SNE_OKY      12                    // Ok box y

    // init scan coords
    uint16_t x = _SNE_LR_B;
    uint16_t y = _SNE_ROWH + _SNE_TOP_B;

    // draw header prompt
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x, y); tft.print ("Day");
    tft.setCursor (x+_SNE_DAY_W, y); tft.print ("Rise    @Az");
    tft.setCursor (x+_SNE_DAY_W+_SNE_HHMM_W, y); tft.print ("Set      @Az");
    tft.setCursor (x+_SNE_DAY_W+2*_SNE_HHMM_W, y); tft.print (" Up");
    tft.setTextColor (RA8875_RED); tft.print (" >10 Mins      ");
    tft.setTextColor (DE_COLOR);
    tft.print (user_name);

    // draw ok button box
    SBox ok_b;
    ok_b.w = 100;
    ok_b.x = tft.width() - ok_b.w - _SNE_LR_B;
    ok_b.y = _SNE_OKY;
    ok_b.h = _SNE_ROWH;
    static const char button_name[] = "Ok";
    drawStringInBox (button_name, ok_b, false, RA8875_GREEN);

    // advance to first data row
    y += _SNE_ROWH;

    // get list of times
    time_t *rises, *sets;
    float *razs, *sazs;
    int n_times = nextSatRSEvents (s, &rises, &razs, &sets, &sazs);
    tft.fillRect (x, y-24, 250, 100, RA8875_BLACK);     // font y - font height

    // show list, if any
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    if (n_times == 0) {

        tft.setCursor (x, y);
        tft.print ("No events");

    } else {


        // draw table
        for (int i = 0; i < n_times; i++) {


            // font is variable width so we must space each column separately
            char buf[30];

            // convert to DE local time
            time_t rt = rises[i] + getTZ (de_tz);
            time_t st = sets[i] + getTZ (de_tz);
            int up = st - rt;       // nextSatRSEvents assures us this will be > 0

            // detect crossing midnight by comparing weekday
            int rt_wd = weekday(rt);
            int st_wd = weekday(st);

            // show rise day
            snprintf (buf, sizeof(buf), "%.3s", dayShortStr(rt_wd));
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (x, y);
            tft.print (buf);

            // show rise time/az
            snprintf (buf, sizeof(buf), "%02dh%02d @%.0f", hour(rt), minute(rt), razs[i]);
            tft.setCursor (x+_SNE_DAY_W, y);
            tft.print (buf);

            // if set time is tomorrow start new line with blank rise time
            if (rt_wd != st_wd) {
                // next row with wrap
                if ((y += _SNE_ROWH) > tft.height()) {
                    if ((x += tft.width()/2) > tft.width())
                        break;                          // no more room
                    y = 2*_SNE_ROWH + _SNE_TOP_B;       // skip ok_b
                }

                snprintf (buf, sizeof(buf), "%.3s", dayShortStr(st_wd));
                tft.setCursor (x, y);
                tft.print (buf);
            }

            // show set time/az
            snprintf (buf, sizeof(buf), "%02dh%02d @%.0f", hour(st), minute(st), sazs[i]);
            tft.setCursor (x+_SNE_DAY_W+_SNE_HHMM_W, y);
            tft.print (buf);

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                snprintf (buf, sizeof(buf), "%02dh%02d", up/3600, (up-3600*(up/3600))/60);
            else
                snprintf (buf, sizeof(buf), "%02d:%02d", up/60, up-60*(up/60));
            tft.setCursor (x+_SNE_DAY_W+2*_SNE_HHMM_W, y);
            tft.setTextColor (up >= 600 ? RA8875_RED : RA8875_WHITE);
            tft.print (buf);

            // next row with wrap
            if ((y += _SNE_ROWH) > tft.height()) {
                if ((x += tft.width()/2) > tft.width())
                    break;                              // no more room
                y = 2*_SNE_ROWH + _SNE_TOP_B;           // skip ok_b
            }
        }

        // finished with lists
        free ((void*)rises);
        free ((void*)razs);
        free ((void*)sets);
        free ((void*)sazs);
    }

    // wait for user to ack
    UserInput ui = {
        ok_b,
        UI_UFuncNone,
        UF_UNUSED,
        _SNE_TIMEOUT,
        UF_NOCLOCKS,
        {0, 0}, TT_NONE, '\0', false, false
    };

    do {
        waitForUser (ui);
    } while (! (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC
                        || inBox (ui.tap, ok_b)) );

    // ack
    drawStringInBox (button_name, ok_b, true, RA8875_GREEN);
}

/* called when tap within dx_info_b while showing a sat to show menu of choices.
 * s is known to be within dx_info_b.
 */
void drawDXSatMenu (const SCoord &s)
{
    // handy names for satellite menu indices.
    // N.B. must be in same order as mitems[] !!
    enum {
        _SMI_CHOOSE,
        _SMI_INFO,
        _SMI_NAME1,
        _SMI_PATH1, _SMI_PASS1, _SMI_TABLE1, _SMI_PLAN1,
        _SMI_NAME2,
        _SMI_PATH2, _SMI_PASS2, _SMI_TABLE2, _SMI_PLAN2,
        _SMI_N,
    };

    // decide which menu items to show
    const int n_sats = nActiveSats();
    const int curr_s = currentSat();
    const MenuFieldType menu_name1 = n_sats > 0  ? MENU_LABEL : MENU_IGNORE;
    const MenuFieldType menu_sat1  = n_sats > 0  ? MENU_1OFN  : MENU_IGNORE;
    const MenuFieldType menu_pass1 = curr_s != 0 ? MENU_1OFN  : MENU_IGNORE;
    const MenuFieldType menu_name2 = n_sats > 1  ? MENU_LABEL : MENU_IGNORE;
    const MenuFieldType menu_sat2  = n_sats > 1  ? MENU_1OFN  : MENU_IGNORE;
    const MenuFieldType menu_pass2 = curr_s != 1 ? MENU_1OFN  : MENU_IGNORE;

    // set path states
    bool path1 = sat_state[0].show_path;
    bool path2 = sat_state[1].show_path;
    bool moon1 = strcasecmp (sat_state[0].name, "Moon") == 0;
    bool moon2 = strcasecmp (sat_state[1].name, "Moon") == 0;
    const MenuFieldType menu_path1 = n_sats > 0 && !moon1 ? MENU_TOGGLE : MENU_IGNORE;
    const MenuFieldType menu_path2 = n_sats > 1 && !moon2 ? MENU_TOGGLE : MENU_IGNORE;

    // retrieve saved names without '_'
    char name1[NV_SATNAME_LEN] = "", name2[NV_SATNAME_LEN] = "";
    if (n_sats > 0)
        strncpySubChar (name1, sat_state[0].name, ' ', '_', NV_SATNAME_LEN);
    if (n_sats > 1)
        strncpySubChar (name2, sat_state[1].name, ' ', '_', NV_SATNAME_LEN);

    // mark current with *

    #define _DXS_INDENT1 5
    #define _DXS_INDENT2 10
    MenuItem mitems[_SMI_N] = {
        {MENU_1OFN,   false, 1, _DXS_INDENT1,  "Choose satellites", NULL},
        {MENU_1OFN,   false, 1, _DXS_INDENT1,  "Show DX Info here", NULL},

        {menu_name1,  false, 1, _DXS_INDENT1,  name1, NULL},
        {menu_path1,  path1, 2, _DXS_INDENT2,  "Show track also", NULL},
        {menu_pass1,  false, 1, _DXS_INDENT2,  "Show pass here", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show rise/set table", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show planning tool", NULL},

        {menu_name2,  false, 1, _DXS_INDENT1,  name2, NULL},
        {menu_path2,  path2, 3, _DXS_INDENT2,  "Show track also", NULL},
        {menu_pass2,  false, 1, _DXS_INDENT2,  "Show pass here", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show rise/set table", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show planning tool", NULL},
    };

    // box for menu
    SBox menu_b;
    menu_b.x = dx_info_b.x + 1;
    menu_b.y = dx_info_b.y + 40;
    menu_b.w = 0;                               // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_NOCLOCKS, M_CANCELOK, 1, _SMI_N, mitems};
    if (runMenu (menu)) {
        for (int i = 0; i < _SMI_N; i++) {

            // check path option
            switch (i) {
            case _SMI_PATH1:
                // toggle path for sat 0
                sat_state[0].show_path = mitems[i].set;
                NVWriteUInt8 (sat_state[0].nv_flags, sat_state[0].show_path ? SF_PATH_MASK : 0);
                break;
            case _SMI_PATH2:
                // toggle path for sat 1
                sat_state[1].show_path = mitems[i].set;
                NVWriteUInt8 (sat_state[1].nv_flags, sat_state[1].show_path ? SF_PATH_MASK : 0);
                break;
            }

            // other items just activate when set
            if (mitems[i].set) {
                switch (i) {
                case _SMI_CHOOSE:
                    // show selection of sats to choose
                    dx_info_for_sat = querySatSelection();
                    initScreen();
                    break;
                case _SMI_INFO:
                    // return to normal DX info but leave sats functional
                    dx_info_for_sat = false;
                    drawOneTimeDX();
                    drawDXInfo();
                    break;
                case _SMI_NAME1:
                    fatalError ("sat menu bogus entry %d", i);
                    break;
                case _SMI_PASS1:
                    // show pass for sat 0
                    dxpaneSat = 0;
                    drawSatPass();
                    break;
                case _SMI_TABLE1:
                    // show rise/set table for sat 0
                    dxpaneSat = 0;
                    showNextSatEvents (sat_state[0]);
                    initScreen();
                    break;
                case _SMI_PLAN1:
                    // restore DX pane and show tool for sat 0 then restore normal map
                    dxpaneSat = 0;
                    drawSatPass();
                    drawSatTool();
                    initEarthMap();
                    break;
                case _SMI_NAME2:
                    fatalError ("sat menu bogus entry %d", i);
                    break;
                case _SMI_PASS2:
                    // show pass for sat 1
                    dxpaneSat = 1;
                    drawSatPass();
                    break;
                case _SMI_TABLE2:
                    // show rise/set table for sat 1
                    dxpaneSat = 1;
                    showNextSatEvents (sat_state[1]);
                    initScreen();
                    break;
                case _SMI_PLAN2:
                    // restore DX pane and show tool for sat 1 then restore normal map
                    dxpaneSat = 1;
                    drawSatPass();
                    drawSatTool();
                    initEarthMap();
                    break;
                case _SMI_N:
                    // lint
                    fatalError ("sat menu bogus entry %d", i);
                    break;
                }
            }
        }
    }
}

/* return whether a satellite is currently in play
 */
bool isSatDefined()
{
    return (nActiveSats() > 0);
}
