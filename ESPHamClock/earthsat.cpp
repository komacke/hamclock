/* manage selection and display of one earth sat.
 *
 * we call "pass" the overhead view shown in dx_info_b, "path" the orbit shown on the map.
 *
 * N.B. our satellite info server changes blanks to underscores in sat names.
 * N.B. we always assign sat_state[0] first then sat_state[1] only if want a second sat
 */


#include "HamClock.h"
#include "stars.h"     // 921 naked-eye stars V<=4.5

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
#define TBORDER         94              // top border
#define HDR_TXT_Y       37              // fixed y for header title/legend text, indep of TBORDER
#define FONT_H          (dx_info_b.h/6) // height for SMALL_FONT
#define FONT_D          5               // font descent
#define SAT_COLOR       RA8875_WHITE    // overall annotation color
#define BTN_COLOR       RA8875_GREEN    // button fill color
#define SATUP_COLOR     RGB565(0,200,0) // time color when sat is up
#define SOON_COLOR      RGB565(200,0,0) // table text color for pass soon
#define GONE_COLOR      RGB565(255,90,90) // table text color for a sat that has gone missing
#define SOON_MINS       10              // "soon", minutes
#define CB_SIZE         20              // size of selection check box
#define CELL_H          29              // display cell height
#define N_COLS          4               // n cols in name table
#define CELL_W          (800/N_COLS)    // display cell width
#define N_ROWS          ((480-TBORDER)/CELL_H)  // n rows in name table
#define MAX_NSAT        (N_ROWS*N_COLS) // max names we can display
#define MAX_PASS_STEPS  30              // max lines to draw for pass map
#define OFFSCRN         20000           // x or y coord that is definitely off screen
static SBox ok_b      = {735, 8, 60, 35}; // Ok button
static SBox refresh_b = {690,55,105,35}; // Refresh button below Ok, out of legend row

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
    int norad;                                          // NORAD catalog id, 0 if unknown
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
    s.norad = 0;
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
static void drawSatSkyDomeXY (SatState &s, const SatRiseSet &rs, const SCircle &dome_c, const SBox &erase_b)
{
    // size and center of screen path
    uint16_t r0 = dome_c.r;
    uint16_t xc = dome_c.s.x;
    uint16_t yc = dome_c.s.y;

    // erase sky dome
    tft.fillRect (erase_b.x, erase_b.y, erase_b.w, erase_b.h, RA8875_BLACK);

    // skip if no sat or never up
    if (!s.sat || !obs || !rs.ever_up)
        return;

    // find n steps, step duration and starting time
    bool full_pass = false;
    int n_steps = 0;
    float step_dt = 0;
    DateTime t;

    if (rs.rise_ok && rs.set_ok) {

        // find start and pass duration in days
        float pass_duration = rs.set_time - rs.rise_time;
        if (pass_duration < 0) {
            // rise after set means pass is underway so start now for remaining duration
            DateTime t_now = userDateTime(nowWO());
            pass_duration = rs.set_time - t_now;
            t = t_now;
        } else {
            // full pass so start at next rise
            t = rs.rise_time;
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

    // connect several points from t until rs.set_time, find max elevation for labeling
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
        int s_up = (rs.set_time - rs.rise_time)*SECSPERDAY;
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

static void drawSatSkyDome (SatState &s)
{
    // existing pass view: draw into the global satpass_c circle, erasing the dx_info dome area
    SBox eb;
    eb.x = dx_info_b.x+1; eb.y = dx_info_b.y+2*FONT_H+1;
    eb.w = dx_info_b.w-2; eb.h = dx_info_b.h-2*FONT_H-1;
    drawSatSkyDomeXY (s, s.rs, satpass_c, eb);
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
static void drawSatPassMarkerXY (const SCircle &dome_c, float az, float el)
{
    uint16_t r0 = dome_c.r;
    uint16_t xc = dome_c.s.x;
    uint16_t yc = dome_c.s.y;

    float r = r0*(90-el)/90;                                  // screen radius, zenith at center
    uint16_t x = xc + r*sinf(deg2rad(az)) + 0.5F;             // want east right
    uint16_t y = yc - r*cosf(deg2rad(az)) + 0.5F;             // want north up

    if (y + SAT_UP_R < tft.height() - 1)                      // beware lower edge
        tft.fillCircle (x, y, SAT_UP_R, SAT_COLOR);
}

/* mark current sat pass location */
static void drawSatPassMarker()
{
    SatNow satnow;
    getSatNow (satnow);
    drawSatPassMarkerXY (satpass_c, satnow.az, satnow.el);
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
        s.norad = atoi (t1+2);          // NORAD id is the digits right after the "1 " prefix
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


/* clear all missing satellite names that now exist in either the user's file or the server file.
 */
static void clearFoundMissingSats()
{
    FILE *rns_fp = NULL;
    RNS_t rns_state = RNS_INIT;
    char name[NV_SATNAME_LEN];
    char t1[TLE_LINEL];
    char t2[TLE_LINEL];

    while (readNextSat (rns_fp, rns_state, name, t1, t2)) {
        for (int i = 0; i < n_missing_sats; ) {
            if (strcasecmp (missing_sats[i], name) == 0)
                clearMissingSat (missing_sats[i]);
            else
                i++;
        }
    }

    if (rns_fp)
        fclose (rns_fp);
}

/* force a fresh copy of the central server satellite list into the local cache.
 * return whether the refreshed file could be opened afterwards.
 */
static bool refreshServerSatCache()
{
    FILE *old_fp = fopenOurs (esat_sfn, "r");
    if (old_fp) {
        fclose (old_fp);
        unlinkOurs (esat_sfn);
    }

    FILE *new_fp = openCachedFile (esat_sfn, esat_url, 0, 0);   // ok if empty
    if (new_fp) {
        fclose (new_fp);
        clearFoundMissingSats();
        Serial.printf ("SAT: refreshed %s from %s\n", esat_sfn, esat_url);
        return (true);
    }

    Serial.printf ("SAT: refresh of %s from %s failed\n", esat_sfn, esat_url);
    return (false);
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
    uint16_t title_y = HDR_TXT_Y;
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (5, title_y);
    tft.print ("Select satellites (two)");

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

    // show control buttons
    drawStringInBox ("Refresh", refresh_b, false, RA8875_WHITE);
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

        // refresh central server TLE cache then rebuild the chooser
        if (inBox (ui.tap, refresh_b)) {
            drawStringInBox ("Refresh", refresh_b, true, RA8875_WHITE);
            if (rns_fp) {
                fclose (rns_fp);
                rns_fp = NULL;
            }
            bool refresh_ok = refreshServerSatCache();
            tft.fillRect (5, TBORDER-12, 420, 10, RA8875_BLACK);
            tft.setTextColor (refresh_ok ? SATUP_COLOR : SOON_COLOR);
            tft.setCursor (5, TBORDER-4);
            tft.print (refresh_ok ? "TLEs refreshed" : "TLE refresh failed");
            wdDelay (500);
            if (refresh_ok)
                return (askSat (selections));
            drawStringInBox ("Refresh", refresh_b, false, RA8875_WHITE);
            continue;
        }

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

/* lightweight TLE lookup for a satellite NOT part of the globally-tracked sat_state[] --
 * does not disturb sat_state[], the map display, or NVRAM. used by satsked.cpp's group
 * schedule feature to get orbital elements for many satellites at once.
 * returns a heap-allocated Satellite* the caller owns (delete when done), or NULL if the
 * name isn't found or its TLE is unavailable/stale.
 */
Satellite *lookupSatByName (const char *name, int *norad)
{
    SatState tmp{};
    strncpySubChar (tmp.name, name, '_', ' ', NV_SATNAME_LEN);
    if (!checkSatUpToDate (tmp, NULL))
        return (NULL);
    if (norad)
        *norad = tmp.norad;
    return (tmp.sat);
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

    // catalog id for callers wanting transmitter lookups
    sat_at_t0.norad = s.norad;

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

/* find the raw screen bounding box of sat s's footprint (all N_FOOT loci), ignoring any
 * OFFSCRN points. return whether any valid points were found.
 */
static bool satFootBBox (SatState &s, SBox &bb)
{
    bool any = false;
    uint16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;           // will be set by first valid point

    for (int alt_i = 0; alt_i < N_FOOT; alt_i++) {
        for (uint16_t foot_i = 0; foot_i < s.n_foot[alt_i]; foot_i++) {
            SCoord &sf = s.foot[alt_i][foot_i];
            if (sf.x == OFFSCRN || sf.y == OFFSCRN)
                continue;
            if (!any) {
                x0 = x1 = sf.x;
                y0 = y1 = sf.y;
                any = true;
            } else {
                if (sf.x < x0) x0 = sf.x;
                if (sf.x > x1) x1 = sf.x;
                if (sf.y < y0) y0 = sf.y;
                if (sf.y > y1) y1 = sf.y;
            }
        }
    }

    if (any) {
        bb.x = x0;
        bb.y = y0;
        bb.w = x1 - x0 + 1;
        bb.h = y1 - y0 + 1;
    }
    return (any);
}

/* grow box a to also include box b (b must be valid; a is assumed valid iff *have is true) */
static void growSBox (SBox &a, const SBox &b, bool *have)
{
    if (!*have) {
        a = b;
        *have = true;
        return;
    }
    uint16_t x0 = a.x < b.x ? a.x : b.x;
    uint16_t y0 = a.y < b.y ? a.y : b.y;
    uint16_t x1 = (a.x+a.w) > (b.x+b.w) ? (a.x+a.w) : (b.x+b.w);
    uint16_t y1 = (a.y+a.h) > (b.y+b.h) ? (a.y+a.h) : (b.y+b.h);
    a.x = x0;
    a.y = y0;
    a.w = x1 - x0;
    a.h = y1 - y0;
}

/* small box around s.path[0] -- the "now" dot marking the satellite's current position -- sized
 * to comfortably cover the filled circle drawn there. return whether it's valid (on screen).
 */
static bool satNowBBox (SatState &s, SBox &bb)
{
    if (!s.path || s.n_path < 1)
        return (false);
    SCoord &now = s.path[0];
    if (now.x == OFFSCRN || now.y == OFFSCRN)
        return (false);

    int r = 2*getRawPathWidth(s.cs) + 4;                 // matches fillCircleRaw radius, plus pad
    bb.x = now.x > (uint16_t)r ? now.x - r : 0;
    bb.y = now.y > (uint16_t)r ? now.y - r : 0;
    bb.w = 2*r;
    bb.h = 2*r;
    return (true);
}

/* lightweight alternative to a full map sweep: recompute each active sat's position/footprint
 * and redraw only the screen region that actually needs it -- the union of where the footprint(s)
 * and current-position marker used to be and where they are now -- via redrawMapBox(). This lets
 * the footprint and satellite dot track the satellite's motion much more often than
 * EARTH_REDRAW_INTERVAL_MS without paying for a full, whole-map, pixel-by-pixel resweep each time.
 * N.B. intentionally ignores the rest of s.path[] (the orbit track) -- its shape barely changes
 * over the short intervals this is meant to run at, so it's left to the normal full sweep to refresh.
 */
void updateSatFootprintFast()
{
    if (!obs || core_map == CM_USER)
        return;

    // a full sweep recomputes the satellite's position immediately but doesn't paint it until
    // the sweep finishes several loop() iterations later. if we ran during that window we'd
    // erase based on a position that was never actually painted, leaving the truly-old footprint
    // on screen -- so just skip this cycle and try again on the next timer tick.
    if (mapSweepActive())
        return;

    // union of every current (soon to be stale) footprint + now-dot box, so we know what to erase
    SBox stale_b = {0,0,0,0};
    bool have_b = false;
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {
        SatState &s = sat_state[i];
        if (!s.sat || !SAT_NAME_IS_SET(s))
            continue;
        SBox bb;
        if (satFootBBox (s, bb))
            growSBox (stale_b, bb, &have_b);
        if (satNowBBox (s, bb))
            growSBox (stale_b, bb, &have_b);
    }

    if (!have_b)
        return;                                          // nothing currently shown, nothing to do

    // recompute position, footprint (and path, cheap either way)
    updateSatPath();

    // grow to also cover the new footprint + now-dot location(s)
    for (int i = 0; i < MAX_ACTIVE_SATS; i++) {
        SatState &s = sat_state[i];
        if (!s.sat || !SAT_NAME_IS_SET(s))
            continue;
        SBox bb;
        if (satFootBBox (s, bb))
            growSBox (stale_b, bb, &have_b);
        if (satNowBBox (s, bb))
            growSBox (stale_b, bb, &have_b);
    }

    // small margin for line width, in the same raw units as stale_b
    #define SFF_MARGIN 6
    int32_t rx0 = (int32_t)stale_b.x - SFF_MARGIN;
    int32_t ry0 = (int32_t)stale_b.y - SFF_MARGIN;
    int32_t rx1 = (int32_t)(stale_b.x + stale_b.w) + SFF_MARGIN;
    int32_t ry1 = (int32_t)(stale_b.y + stale_b.h) + SFF_MARGIN;
    if (rx0 < 0) rx0 = 0;
    if (ry0 < 0) ry0 = 0;

    // s.foot[]/s.path[] are in *raw* framebuffer coordinates (from ll2sRaw()), but
    // redrawMapBox()/drawMapCoord() work in *logical* app coordinates -- these only coincide
    // when tft.SCALESZ == 1. Convert down to logical space (floor the top/left, ceil the
    // bottom/right so the region fully covers the raw pixels) before clipping to map_b.
    int scl = tft.SCALESZ > 0 ? tft.SCALESZ : 1;
    int32_t x0 = rx0 / scl;
    int32_t y0 = ry0 / scl;
    int32_t x1 = (rx1 + scl - 1) / scl;
    int32_t y1 = (ry1 + scl - 1) / scl;

    if (x0 < map_b.x) x0 = map_b.x;
    if (y0 < map_b.y) y0 = map_b.y;
    if (x1 > map_b.x + map_b.w) x1 = map_b.x + map_b.w;
    if (y1 > map_b.y + map_b.h) y1 = map_b.y + map_b.h;
    if (x1 <= x0 || y1 <= y0)
        return;

    SBox box;
    box.x = (uint16_t)x0;
    box.y = (uint16_t)y0;
    box.w = (uint16_t)(x1 - x0);
    box.h = (uint16_t)(y1 - y0);
    redrawMapBox (box);

    if (debugLevel (DEBUG_ESATS, 1))
        Serial.printf ("SAT: fast foot refresh box %u,%u %ux%u (scale %d)\n",
                                                            box.x, box.y, box.w, box.h, scl);
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

/* ----- Satellite frequency / mode table (parallel server file esats-freq.txt) ----- */

// server file with one transmitter per line, keyed by NORAD; see backend fetch_sat_freq.sh.
// fields: norad,name,status,type,mode,uplink_low,uplink_high,downlink_low,downlink_high,baud,invert,description
static const char esatfq_sfn[] = "esats-freq.txt";          // local cached file from server
static const char esatfq_url[] = "/esats/esats-freq.txt";   // server file URL

// SatFreq struct is declared in HamClock.h so other modules (e.g. webserver) can use it.

/* load all transmitter rows for the given NORAD id from the cached freq file.
 * returns count and, if > 0, sets *fpp to a malloc'd array the caller must free().
 */
/* return field idx from split row f[0..nf-1], or "" if absent/out of range */
static const char *satFreqField (char **f, int nf, int idx)
{
    return (idx >= 0 && idx < nf) ? f[idx] : "";
}

/* load all transmitter rows for the given NORAD id from the cached freq file.
 * The file's "# fields:" header line is parsed to map columns by name, so the
 * column order may vary and an absent "status" column defaults to "active".
 * returns count and, if > 0, sets *fpp to a malloc'd array the caller must free().
 */
int getSatFreqs (int norad, SatFreq **fpp)
{
    *fpp = NULL;
    if (norad <= 0)
        return (0);

    FILE *fp = openCachedFile (esatfq_sfn, esatfq_url, MAX_CACHE_AGE, 0);    // ok if empty
    if (!fp) {
        Serial.printf ("SAT: no server freq file\n");
        return (0);
    }

    // column positions resolved from the header; -1 means the column is absent
    enum { F_NORAD, F_NAME, F_STATUS, F_TYPE, F_MODE, F_ULLO, F_ULHI, F_DLLO, F_DLHI,
           F_BAUD, F_INVERT, F_CTCSS, F_DESC, F_NFIELDS };
    int col[F_NFIELDS];
    for (int i = 0; i < F_NFIELDS; i++)
        col[i] = -1;
    int ncols = 0;

    enum { MAX_COLS = 16 };

    SatFreq *list = NULL;
    int n = 0;
    char line[300];

    while (fgets (line, sizeof(line), fp)) {
        chompString (line);

        if (line[0] == '#') {
            // learn column layout from the "fields:" header line, if present
            const char *fl = strstr (line, "fields:");
            if (fl) {
                char tmp[300];
                quietStrncpy (tmp, fl + 7, sizeof(tmp));
                int idx = 0;
                for (char *tok = strtok (tmp, " ,\t"); tok; tok = strtok (NULL, " ,\t"), idx++) {
                    if      (!strcasecmp (tok, "norad"))         col[F_NORAD]  = idx;
                    else if (!strcasecmp (tok, "name"))          col[F_NAME]   = idx;
                    else if (!strcasecmp (tok, "status"))        col[F_STATUS] = idx;
                    else if (!strcasecmp (tok, "type"))          col[F_TYPE]   = idx;
                    else if (!strcasecmp (tok, "mode"))          col[F_MODE]   = idx;
                    else if (!strcasecmp (tok, "uplink_low"))    col[F_ULLO]   = idx;
                    else if (!strcasecmp (tok, "uplink_high"))   col[F_ULHI]   = idx;
                    else if (!strcasecmp (tok, "downlink_low"))  col[F_DLLO]   = idx;
                    else if (!strcasecmp (tok, "downlink_high")) col[F_DLHI]   = idx;
                    else if (!strcasecmp (tok, "baud"))          col[F_BAUD]   = idx;
                    else if (!strcasecmp (tok, "invert"))        col[F_INVERT] = idx;
                    else if (!strcasecmp (tok, "ctcss"))         col[F_CTCSS]  = idx;
                    else if (!strcasecmp (tok, "description"))   col[F_DESC]   = idx;
                }
                ncols = idx;
            }
            continue;
        }
        if (line[0] == '\0')
            continue;
        if (ncols == 0 || col[F_NORAD] < 0)
            continue;                       // no usable header seen yet

        // split into exactly ncols fields; the final field keeps any remainder so
        // commas inside a trailing free-text description are preserved.
        char *f[MAX_COLS];
        int want = ncols < MAX_COLS ? ncols : MAX_COLS;
        int nf = 0;
        char *p = line;
        while (nf < want) {
            f[nf++] = p;
            char *c = strchr (p, ',');
            if (!c || nf == want)
                break;
            *c = '\0';
            p = c + 1;
        }
        if (nf < want)
            continue;                       // short row

        if (atoi (satFreqField (f, nf, col[F_NORAD])) != norad)
            continue;

        list = (SatFreq *) realloc (list, (n+1) * sizeof(SatFreq));
        if (!list)
            fatalError ("no memory for %d SatFreq", n+1);
        SatFreq *sf = &list[n++];
        memset (sf, 0, sizeof(*sf));

        const char *st = satFreqField (f, nf, col[F_STATUS]);
        quietStrncpy (sf->status, st[0] ? st : "active", sizeof(sf->status));
        quietStrncpy (sf->type, satFreqField (f, nf, col[F_TYPE]), sizeof(sf->type));
        quietStrncpy (sf->mode, satFreqField (f, nf, col[F_MODE]), sizeof(sf->mode));
        sf->ul_lo  = atol (satFreqField (f, nf, col[F_ULLO]));
        sf->ul_hi  = atol (satFreqField (f, nf, col[F_ULHI]));
        sf->dl_lo  = atol (satFreqField (f, nf, col[F_DLLO]));
        sf->dl_hi  = atol (satFreqField (f, nf, col[F_DLHI]));
        sf->baud   = atof (satFreqField (f, nf, col[F_BAUD]));
        sf->invert = (strcasecmp (satFreqField (f, nf, col[F_INVERT]), "true") == 0);
        quietStrncpy (sf->ctcss, satFreqField (f, nf, col[F_CTCSS]), sizeof(sf->ctcss));
        quietStrncpy (sf->desc, satFreqField (f, nf, col[F_DESC]), sizeof(sf->desc));
    }
    fclose (fp);

    *fpp = list;
    return (n);
}

/* format a Hz freq pair into b as MHz: "low-high" for a passband, single value
 * otherwise, or "-" if none.
 */
static void fmtSatFreqMHz (char *b, size_t bl, long lo, long hi)
{
    if (lo <= 0)
        quietStrncpy (b, "-", bl);
    else if (hi > 0 && hi != lo)
        snprintf (b, bl, "%.3f-%.3f", lo/1e6, hi/1e6);
    else
        snprintf (b, bl, "%.3f", lo/1e6);
}

/* full-screen table of the frequency/mode info for satellite s, dismissed with Ok.
 * built to mirror showNextSatEvents().
 */
/* live, self-updating Doppler detail for ONE transmitter tx of satellite s.
 * Recomputes from the satellite's current range rate ~once a second so the
 * RX/TX tuning frequencies visibly track the pass. Dismissed with Ok.
 */
/* draw a small weather condition glyph in a box of size s at (x,y), chosen from the condition text.
 * like the sun/moon, these are drawn from primitives -- there are no icon files.
 */
static void wxDrawCloud (uint16_t x, uint16_t y, uint16_t s, uint16_t col)
{
    uint16_t r = s/5;
    uint16_t by = y + s*3/5;
    tft.fillCircle (x + s*3/10, by, r, col);
    tft.fillCircle (x + s*7/10, by, r, col);
    tft.fillCircle (x + s/2,    by - r/2, r, col);
    tft.fillRect   (x + s*3/10, by, s*2/5, r+1, col);
}

static void wxDrawSun (uint16_t cx, uint16_t cy, uint16_t r, uint16_t col)
{
    for (int a = 0; a < 8; a++) {
        float ang = a*M_PIF/4;
        tft.drawLine (cx + (r+2)*cosf(ang), cy + (r+2)*sinf(ang),
                      cx + (r+5)*cosf(ang), cy + (r+5)*sinf(ang), col);
    }
    tft.fillCircle (cx, cy, r, col);
}

static void drawWXGlyph (uint16_t x, uint16_t y, uint16_t s, const char *cond)
{
    // case-insensitive copy for keyword matching
    char c[40];
    size_t i = 0;
    for (; cond[i] && i < sizeof(c)-1; i++) {
        char ch = cond[i];
        c[i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
    }
    c[i] = 0;
    #define WX_HAS(k) (strstr(c,(k)) != NULL)

    const uint16_t cloud_col = BRGRAY;
    const uint16_t rain_col  = RGB565(100,150,255);

    if (WX_HAS("thunder") || WX_HAS("storm")) {
        wxDrawCloud (x, y, s, cloud_col);
        uint16_t bx = x + s/2, by = y + s*3/5 + s/6;
        tft.drawLine (bx, by, bx - s/10, by + s/5, RA8875_YELLOW);
        tft.drawLine (bx - s/10, by + s/5, bx + s/12, by + s/5, RA8875_YELLOW);
        tft.drawLine (bx + s/12, by + s/5, bx - s/12, y + s, RA8875_YELLOW);
    } else if (WX_HAS("snow") || WX_HAS("sleet")) {
        wxDrawCloud (x, y, s, cloud_col);
        for (int k = 0; k < 3; k++)
            tft.fillCircle (x + s*(3+3*k)/10, y + s*9/10, 1, RA8875_WHITE);
    } else if (WX_HAS("rain") || WX_HAS("drizzle") || WX_HAS("shower")) {
        wxDrawCloud (x, y, s, cloud_col);
        for (int k = 0; k < 3; k++) {
            uint16_t dx = x + s*(3+3*k)/10;
            tft.drawLine (dx, y + s*4/5, dx - s/12, y + s, rain_col);
        }
    } else if (WX_HAS("mist") || WX_HAS("fog") || WX_HAS("haze") || WX_HAS("smoke")) {
        for (int k = 0; k < 4; k++)
            tft.drawLine (x + 2, y + s*(3+2*k)/12, x + s - 2, y + s*(3+2*k)/12, cloud_col);
    } else if (WX_HAS("few") || WX_HAS("scattered") || WX_HAS("partly")) {
        wxDrawSun (x + s/3, y + s/3, s/6, RA8875_YELLOW);
        wxDrawCloud (x + s/6, y + s/5, s*3/4, cloud_col);
    } else if (WX_HAS("cloud") || WX_HAS("overcast") || WX_HAS("broken")) {
        wxDrawCloud (x, y, s, cloud_col);
    } else if (WX_HAS("clear") || WX_HAS("sun") || WX_HAS("fair")) {
        wxDrawSun (x + s/2, y + s/2, s/4, RA8875_YELLOW);
    } else {
        wxDrawCloud (x, y, s, cloud_col);       // sensible default
    }
    #undef WX_HAS
}


static void showSatDoppler (SatState &s, const SatFreq &tx)
{
    hideClocks();
    eraseScreen();

    const uint16_t W = tft.width();
    const uint16_t H = tft.height();
    const uint16_t P = H/16;                            // proportional row pitch
    const uint16_t LR = (W/80 < 6) ? 6 : W/80;          // left/right border
    #define _SD_TIMEOUT 1000                            // ms between live refreshes
    const double C_MPS = 2.99792458e8;                  // speed of light, m/s

    // baseline y of text row i; (i+1)*P stays < H for i <= 14 on every build
    #define ROWY(i)  ((uint16_t)(((i)+1)*P))

    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);

    // Ok button, bottom right (clear of the dome, which sits top-right)
    SBox ok_b;
    ok_b.w = (W/8 < 70) ? 70 : W/8;
    ok_b.h = P;
    ok_b.x = W - ok_b.w - LR;
    ok_b.y = ROWY(12) - P;
    static const char ok_name[] = "Ok";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (ok_name, ok_b, false, RA8875_GREEN);

    // ---- sky dome geometry: right column, sized from the screen ----
    uint16_t dome_top = P;                              // below the title band
    uint16_t dome_bot = 8*P;                            // above the live zone (row 9)
    uint16_t rv = (dome_bot - dome_top)/2;
    uint16_t left_edge = (uint16_t)(W*3/5);             // text left of here, dome right of it
    uint16_t rh = (W - LR - left_edge)/2;
    uint16_t r0 = rv < rh ? rv : rh;
    SCircle dome_c;
    dome_c.r   = r0;
    dome_c.s.x = W - LR - r0;
    dome_c.s.y = dome_top + r0;
    SBox dome_erase;
    dome_erase.x = dome_c.s.x - r0 - 4;
    dome_erase.y = dome_c.s.y - r0 - P/2;
    dome_erase.w = 2*r0 + 8;
    dome_erase.h = 2*r0 + P;

    // ---- static left-column header (drawn once) ----
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (LR, ROWY(0));
    tft.printf ("%s Doppler", user_name);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    char dbuf[80];
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (LR, ROWY(1));
    tft.printf ("Mode %s   Type %s", tx.mode[0] ? tx.mode : "-", tx.type[0] ? tx.type : "-");
    quietStrncpy (dbuf, tx.desc[0] ? tx.desc : "-", sizeof(dbuf));
    (void) maxStringW (dbuf, left_edge - 2*LR);         // keep description clear of the dome
    tft.setCursor (LR, ROWY(2));
    tft.print (dbuf);

    // local (DE) weather on the gap row: temperature in the user's units + a condition glyph.
    // getFastWx is a non-blocking cache lookup; if cold we simply omit the line.
    WXInfo wxi;
    if (getFastWx (de_ll, wxi)) {
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (LR, ROWY(3));
        float t = showTempC() ? wxi.temperature_c : CEN2FAH(wxi.temperature_c);
        tft.printf ("Wx %.0f %c", t, showTempC() ? 'C' : 'F');
        uint16_t gx = tft.getCursorX() + LR;            // glyph just right of the temperature
        uint16_t gs = P*3/4;
        const char *cond = wxi.conditions[0] ? wxi.conditions : wxi.clouds;
        drawWXGlyph (gx, ROWY(3) - gs + 2, gs, cond);
        char cbuf[28];
        quietStrncpy (cbuf, cond, sizeof(cbuf));
        uint16_t tx0 = gx + gs + LR;
        if (left_edge > tx0 + LR) {
            (void) maxStringW (cbuf, left_edge - tx0 - LR);
            tft.setTextColor (GRAY);
            tft.setCursor (tx0, ROWY(3));
            tft.print (cbuf);
        }
    }

    char nb[48];
    tft.setTextColor (GRAY);
    tft.setCursor (LR, ROWY(4));
    fmtSatFreqMHz (nb, sizeof(nb), tx.dl_lo, tx.dl_hi);
    tft.printf ("Downlink nominal  %s MHz", nb);
    tft.setCursor (LR, ROWY(5));
    fmtSatFreqMHz (nb, sizeof(nb), tx.ul_lo, tx.ul_hi);
    tft.printf ("Uplink   nominal  %s MHz", nb);

    if (tx.ctcss[0]) {
        tft.setTextColor (GRAY);
        tft.setCursor (LR, ROWY(6));
        tft.printf ("CTCSS tone  %s Hz", tx.ctcss);
    }

    // ---- next-pass summary (computed once); rs is also reused for the dome arc ----
    time_t now = nowWO();
    SatRiseSet rs;
    findNextPass (s.sat, NULL, now, rs);
    {
        char ln[120];
        if (!rs.rise_ok && !rs.set_ok) {
            quietStrncpy (ln, rs.ever_up ? "Always up (no AOS/LOS)" : "No pass within 2 days",
                          sizeof(ln));
        } else {
            DateTime dt_now = userDateTime (now);
            bool up_now = (rs.set_ok && rs.rise_ok && rs.set_time < rs.rise_time)
                                    || (rs.set_ok && !rs.rise_ok);
            time_t aos_t = 0, los_t = 0;
            if (rs.rise_ok) aos_t = now + (time_t)((rs.rise_time - dt_now)*SECSPERDAY + 0.5);
            if (rs.set_ok)  los_t = now + (time_t)((rs.set_time  - dt_now)*SECSPERDAY + 0.5);
            time_t wa = up_now ? now : aos_t;
            time_t wb = los_t;
            time_t tca_t = wa;
            if (rs.set_ok && wb > wa) {
                float best = 1e30f;
                for (int i = 0; i <= 60; i++) {
                    time_t tsm = wa + (time_t)((double)(wb - wa)*i/60);
                    float el2, az2, rng2, rate2;
                    s.sat->predict (userDateTime (tsm));
                    s.sat->topo (obs, el2, az2, rng2, rate2);
                    if (rng2 < best) { best = rng2; tca_t = tsm; }
                }
            }
            int tz = getTZ (de_tz);
            if (up_now) {
                int q = snprintf (ln, sizeof(ln), "Up now");
                if (rs.set_ok && wb > wa) {
                    time_t c = tca_t + tz;
                    q += snprintf (ln+q, sizeof(ln)-q, "   TCA %02d:%02d", hour(c), minute(c));
                }
                if (rs.set_ok) {
                    time_t l = los_t + tz;
                    int rem = (int)(los_t - now);
                    q += snprintf (ln+q, sizeof(ln)-q, "   LOS %02d:%02d (in %dm)",
                                   hour(l), minute(l), rem/60);
                }
            } else if (rs.rise_ok && rs.set_ok) {
                time_t a = aos_t + tz, c = tca_t + tz, l = los_t + tz;
                int dur = (int)(los_t - aos_t);
                snprintf (ln, sizeof(ln),
                          "AOS %02d:%02d   TCA %02d:%02d   LOS %02d:%02d   dur %dm%02ds",
                          hour(a), minute(a), hour(c), minute(c), hour(l), minute(l), dur/60, dur%60);
            } else {
                time_t a = aos_t + tz;
                snprintf (ln, sizeof(ln), "AOS %02d:%02d", hour(a), minute(a));
            }
        }
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);
        (void) maxStringW (ln, left_edge - 2*LR);
        tft.setCursor (LR, ROWY(7));
        tft.print (ln);
    }

    // live-line baselines (full width, below the dome)
    const uint16_t stat_y = ROWY(9);
    const uint16_t rx_y   = ROWY(11);
    const uint16_t tx_y   = ROWY(12);

    SBox screen_b; screen_b.x = 0; screen_b.y = 0; screen_b.w = W; screen_b.h = H;
    UserInput ui = {
        screen_b, UI_UFuncNone, UF_UNUSED, _SD_TIMEOUT, UF_NOCLOCKS,
        {0, 0}, TT_NONE, CHAR_NONE, false, false
    };

    for (;;) {

        // live geometry for THIS sat (computed before the dome re-predicts)
        float el = 0, az = 0, range = 0, rate = 0;
        if (s.sat && obs) {
            DateTime t = userDateTime (nowWO());
            s.sat->predict (t);
            s.sat->topo (obs, el, az, range, rate);
        }
        double dop = rate / C_MPS;

        // dome + live now-dot (redrawn each tick; dome erases its own region)
        drawSatSkyDomeXY (s, rs, dome_c, dome_erase);
        if (el >= 0)
            drawSatPassMarkerXY (dome_c, az, el);

        selectFontStyle (LIGHT_FONT, SMALL_FONT);

        // az/el/range/rate
        tft.fillRect (0, stat_y - (P*4/5), ok_b.x - LR, P, RA8875_BLACK);   // stop left of Ok button
        tft.setTextColor (el >= 0 ? RA8875_GREEN : GRAY);
        tft.setCursor (LR, stat_y);
        tft.printf ("Az %.1f  El %.1f  Range %.0f km  Rate %+.0f m/s %s",
                    az, el, range, rate, rate >= 0 ? "(receding)" : "(approaching)");

        // RX (downlink, lower when receding)
        tft.fillRect (0, rx_y - (P*4/5), ok_b.x - LR, P, RA8875_BLACK);   // stop left of Ok button
        if (tx.dl_lo > 0) {
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (LR, rx_y);
            double rxlo = tx.dl_lo*(1-dop);
            if (tx.dl_hi > 0 && tx.dl_hi != tx.dl_lo) {
                double rxhi = tx.dl_hi*(1-dop);
                tft.printf ("RX tune  %.5f - %.5f MHz", rxlo/1e6, rxhi/1e6);
            } else
                tft.printf ("RX tune  %.5f MHz   (%+.2f kHz)", rxlo/1e6, (rxlo - tx.dl_lo)/1e3);
        }

        // TX (uplink, higher when receding)
        tft.fillRect (0, tx_y - (P*4/5), ok_b.x - LR, P, RA8875_BLACK);   // stop left of Ok button
        if (tx.ul_lo > 0) {
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (LR, tx_y);
            double txlo = tx.ul_lo*(1+dop);
            if (tx.ul_hi > 0 && tx.ul_hi != tx.ul_lo) {
                double txhi = tx.ul_hi*(1+dop);
                tft.printf ("TX xmit  %.5f - %.5f MHz", txlo/1e6, txhi/1e6);
            } else
                tft.printf ("TX xmit  %.5f MHz   (%+.2f kHz)", txlo/1e6, (txlo - tx.ul_lo)/1e3);
        }

        if (waitForUser (ui)) {
            if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC
                            || inBox (ui.tap, ok_b))
                break;
            ui.kb_char = CHAR_NONE;
            ui.tap.x = ui.tap.y = 0;
        }
    }

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (ok_name, ok_b, true, RA8875_GREEN);
}

/* full-screen, paginated table of the frequency/mode info for satellite s.
 * Tap a row to highlight it, then Ok to open a live Doppler view for that
 * transmitter. With nothing highlighted, Ok (or Enter/Esc) dismisses.
 * The highlight is temporary - it is never saved.
 */
static void showSatFreqs (SatState &s)
{
    hideClocks();

    #define _SF_LR_B      10                    // left/right border
    #define _SF_TIMEOUT   60000                 // ms
    #define _SF_ROWH      30                    // data row height
    #define _SF_TITLE_Y   8                     // title/button band y
    #define _SF_HDR_Y     70                    // column header baseline
    #define _SF_DATA_Y    100                   // first data row baseline

    // column x positions (tuned for 800-wide; wider builds just extend Description)
    #define _SF_MODE_X    (_SF_LR_B)
    #define _SF_UP_X      120
    #define _SF_DN_X      305
    #define _SF_BAUD_X    490
    #define _SF_STAT_X    548
    #define _SF_DESC_X    660

    // load transmitters for this sat
    SatFreq *fl = NULL;
    int n = getSatFreqs (s.norad, &fl);

    // friendly name (underscores back to spaces)
    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);

    // paging math
    int rows_per_page = (tft.height() - _SF_DATA_Y) / _SF_ROWH;
    if (rows_per_page < 1)
        rows_per_page = 1;
    int npages = (n > 0) ? (n + rows_per_page - 1) / rows_per_page : 1;
    int page = 0;

    // temporary (unsaved) highlighted transmitter, -1 = none
    int sel = -1;

    // Ok and (when paging) More buttons, top right
    SBox ok_b;
    ok_b.w = 90; ok_b.h = _SF_ROWH;
    ok_b.x = tft.width() - ok_b.w - _SF_LR_B;
    ok_b.y = _SF_TITLE_Y;
    SBox more_b;
    more_b.w = 90; more_b.h = _SF_ROWH;
    more_b.x = ok_b.x - more_b.w - 10;
    more_b.y = _SF_TITLE_Y;
    static const char ok_name[] = "Ok";
    static const char more_name[] = "More";

    // input watches the whole screen so any button/row/key is caught
    SBox screen_b;
    screen_b.x = 0; screen_b.y = 0;
    screen_b.w = tft.width(); screen_b.h = tft.height();
    UserInput ui = {
        screen_b, UI_UFuncNone, UF_UNUSED, _SF_TIMEOUT, UF_NOCLOCKS,
        {0, 0}, TT_NONE, CHAR_NONE, false, false
    };

    bool need_draw = true;
    for (;;) {

        int i0 = page * rows_per_page;
        int i1 = i0 + rows_per_page;
        if (i1 > n)
            i1 = n;

        if (need_draw) {
            eraseScreen();

            // title
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            tft.setTextColor (DE_COLOR);
            tft.setCursor (_SF_LR_B, _SF_TITLE_Y + 22);
            if (npages > 1)
                tft.printf ("%s freq/modes  %d-%d of %d", user_name, i0+1, i1, n);
            else
                tft.printf ("%s frequencies & modes", user_name);

            // buttons — use LIGHT/SMALL so Ok text fits inside the box
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            drawStringInBox (ok_name, ok_b, false, RA8875_GREEN);
            if (npages > 1)
                drawStringInBox (more_name, more_b, false, RA8875_GREEN);

            // column header
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (DE_COLOR);
            tft.setCursor (_SF_MODE_X, _SF_HDR_Y); tft.print ("Mode");
            tft.setCursor (_SF_UP_X,   _SF_HDR_Y); tft.print ("Up MHz");
            tft.setCursor (_SF_DN_X,   _SF_HDR_Y); tft.print ("Dn MHz");
            tft.setCursor (_SF_BAUD_X, _SF_HDR_Y); tft.print ("Baud");
            tft.setCursor (_SF_STAT_X, _SF_HDR_Y); tft.print ("Status");
            tft.setCursor (_SF_DESC_X, _SF_HDR_Y); tft.print ("Description");

            if (n == 0) {

                tft.setTextColor (RA8875_WHITE);
                tft.setCursor (_SF_LR_B, _SF_DATA_Y);
                tft.print (s.norad <= 0 ? "No catalog id known for this satellite"
                                        : "No frequency data available");

            } else {

                char buf[64];
                uint16_t y = _SF_DATA_Y;

                for (int i = i0; i < i1; i++) {
                    SatFreq *sf = &fl[i];

                    // color by status
                    uint16_t tc = RA8875_WHITE;
                    if (!strcmp (sf->status, "active"))        tc = RA8875_GREEN;
                    else if (!strcmp (sf->status, "inactive")) tc = GRAY;
                    else if (!strcmp (sf->status, "future"))   tc = RA8875_YELLOW;
                    tft.setTextColor (tc);

                    // mode, clipped to its column width so it can't bleed right
                    char mbuf[28];
                    quietStrncpy (mbuf, sf->mode[0] ? sf->mode : "-", sizeof(mbuf));
                    (void) maxStringW (mbuf, _SF_UP_X - _SF_MODE_X - 8);
                    tft.setCursor (_SF_MODE_X, y); tft.print (mbuf);

                    // uplink
                    fmtSatFreqMHz (buf, sizeof(buf), sf->ul_lo, sf->ul_hi);
                    tft.setCursor (_SF_UP_X, y); tft.print (buf);

                    // downlink (+ 'i' for inverting transponder)
                    fmtSatFreqMHz (buf, sizeof(buf), sf->dl_lo, sf->dl_hi);
                    tft.setCursor (_SF_DN_X, y); tft.print (buf);
                    if (sf->invert)
                        tft.print ('i');

                    // baud, compact (9600 -> 9k6, 1200 -> 1k2)
                    tft.setCursor (_SF_BAUD_X, y);
                    if (sf->baud >= 1000) {
                        int k = (int)(sf->baud/1000);
                        int frac = (int)((sf->baud - k*1000)/100);
                        if (frac)
                            snprintf (buf, sizeof(buf), "%dk%d", k, frac);
                        else
                            snprintf (buf, sizeof(buf), "%dk", k);
                        tft.print (buf);
                    } else if (sf->baud > 0) {
                        snprintf (buf, sizeof(buf), "%g", sf->baud);
                        tft.print (buf);
                    } else
                        tft.print ("-");

                    // status word
                    tft.setCursor (_SF_STAT_X, y);
                    tft.print (sf->status[0] ? sf->status : "-");

                    // description, clipped to the remaining width so it cannot
                    // wrap past the right edge and overprint the Mode column
                    char dbuf[64];
                    quietStrncpy (dbuf, sf->desc[0] ? sf->desc : "-", sizeof(dbuf));
                    (void) maxStringW (dbuf, tft.width() - _SF_DESC_X - _SF_LR_B);
                    tft.setCursor (_SF_DESC_X, y); tft.print (dbuf);

                    // highlight box on the selected row
                    if (i == sel)
                        tft.drawRect (_SF_LR_B - 5, y - 22,
                                      tft.width() - 2*(_SF_LR_B - 5), _SF_ROWH - 2, RA8875_WHITE);

                    y += _SF_ROWH;
                }
            }

            // bottom hint
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (GRAY);
            tft.setCursor (_SF_LR_B, tft.height() - tft.height()/32);   // leave room for descenders on any size
            tft.print ("Tap a row, then Ok, for its live Doppler");

            need_draw = false;
        }

        // wait for input (table is static, so a timeout just dismisses)
        if (!waitForUser (ui))
            break;

        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC)
            break;                                      // keyboard always exits

        if (inBox (ui.tap, ok_b)) {
            if (sel >= 0 && sel < n) {
                showSatDoppler (s, fl[sel]);            // drill into live Doppler
                need_draw = true;                       // then restore the table
            } else
                break;                                  // nothing selected -> exit
        } else if (npages > 1 && inBox (ui.tap, more_b)) {
            page = (page + 1) % npages;                 // next page, wrapping
            sel = -1;                                   // clear selection on page change
            need_draw = true;
        } else if (n > 0 && ui.tap.y >= (uint16_t)(_SF_DATA_Y - 22)) {
            // tap in the data area selects/toggles a row on this page
            int rk = (ui.tap.y - (_SF_DATA_Y - 22)) / _SF_ROWH;
            if (rk >= 0 && i0 + rk < i1) {
                int abs_i = i0 + rk;
                sel = (sel == abs_i) ? -1 : abs_i;      // toggle
                need_draw = true;
            }
        }

        // reset before next wait
        ui.kb_char = CHAR_NONE;
        ui.tap.x = ui.tap.y = 0;
    }

    // ack and clean up
    drawStringInBox (ok_name, ok_b, true, RA8875_GREEN);
    if (fl)
        free (fl);
}

static void showSatPassProg (SatState &s)
{
    #define _SPP_LABEL_W   50
    #define _SPP_TITLE_H   (LISTING_Y0 + 8)   // 55px: title@PANETITLE_H, legend@SUBTITLE_Y0, ruler@LISTING_Y0
    #define _SPP_N_DAYS    14
    #define _SPP_MAX_PASS  (_SPP_N_DAYS*12)
    #define _SPP_TIMEOUT   120000
    #define _SPP_BAR_MINW  4
    #define _SPP_SUNLIT_C  RGB565(50, 210, 80)
    #define _SPP_ECLIP_C   RGB565(70, 120, 210)
    #define _SPP_GRID_C    RGB565(35, 35, 50)
    #define _SPP_NOON_C    RGB565(60, 60, 80)
    #define _SPP_LBL_C     BRGRAY
    #define _SPP_RULER_C   RGB565(80, 80, 100)
    #define _SPP_TODAY_C   RA8875_WHITE

    int row_h = (map_b.h - _SPP_TITLE_H) / _SPP_N_DAYS;
    if (row_h < 10) row_h = 10;
    int tl_x = map_b.x + _SPP_LABEL_W;
    int tl_w = map_b.w - _SPP_LABEL_W - 2;
    auto sod2x = [&](int sod) -> uint16_t {
        return (uint16_t)(tl_x + (long)sod * tl_w / SECSPERDAY);
    };

    time_t now_t = nowWO();
    time_t tz     = (time_t) getTZ (de_tz);  // DE UTC offset, secs

    // Find start of current LOCAL day (midnight DE time) expressed as UTC
    time_t now_local  = now_t + tz;
    struct tm *tm_l   = gmtime (&now_local);
    struct tm tm_mid  = *tm_l;
    tm_mid.tm_hour = tm_mid.tm_min = tm_mid.tm_sec = 0;
    time_t t_midnight = timegm (&tm_mid) - tz;  // UTC of local midnight
    time_t t_end_all  = t_midnight + (time_t)_SPP_N_DAYS * SECSPERDAY;

    struct SPEntry { time_t aos, los, tca; float max_el; bool sunlit; };
    SPEntry *passes = (SPEntry *) malloc (_SPP_MAX_PASS * sizeof(SPEntry));
    if (!passes) { plotMessage (map_b, RA8875_RED, "Out of memory"); return; }
    int n_passes = 0;

    if (!satEpochOk (s.sat, s.name, t_midnight)) {
        plotMessage (map_b, RA8875_RED, "TLE expired - update TLEs");
        free (passes);
        return;
    }
    {
        DateTime t0dt = userDateTime (t_midnight);
        time_t t      = t_midnight;
        Sun sun;
        while (t < t_end_all && n_passes < _SPP_MAX_PASS) {
            SatRiseSet rs;
            findNextPass (s.sat, NULL, t, rs);
            if (!rs.rise_ok || !rs.set_ok) break;
            time_t rt = t_midnight + (time_t)(SECSPERDAY*(rs.rise_time - t0dt) + 0.5F);
            time_t st = t_midnight + (time_t)(SECSPERDAY*(rs.set_time  - t0dt) + 0.5F);
            if (rt >= t_end_all) break;
            if (st <= rt) st = rt + 300;
            float    max_el   = 0;
            DateTime t_max_dt = rs.rise_time;
            {
                float el, az, range, rate;
                DateTime t_ep = rs.set_time < rs.rise_time
                              ? rs.rise_time + 10.0F/1440.0F : rs.set_time;
                for (DateTime ts = rs.rise_time; ts < t_ep; ts += 20.0F/SECSPERDAY) {
                    s.sat->predict (ts);
                    s.sat->topo (obs, el, az, range, rate);
                    if (el > max_el) { max_el = el; t_max_dt = ts; }
                }
            }
            if (max_el >= SAT_MIN_EL) {
                sun.predict    (t_max_dt);
                s.sat->predict (t_max_dt);
                time_t tca_t = t_midnight
                             + (time_t)(SECSPERDAY*(t_max_dt - t0dt) + 0.5F);
                passes[n_passes++] = { rt, st, tca_t, max_el, !s.sat->eclipsed(&sun) };
            }
            t = st + (time_t)(s.sat->period() * SECSPERDAY / 2);
            updateClocks (false);
        }
    }

    char user_name[NV_SATNAME_LEN];
    strncpySubChar (user_name, s.name, ' ', '_', NV_SATNAME_LEN);
    char title[48];
    snprintf (title, sizeof(title), "%s Pass Progression", user_name);

    SBox resume_b;
    resume_b.w = 78; resume_b.h = 20;
    resume_b.x = map_b.x + map_b.w - resume_b.w - 3;
    resume_b.y = map_b.y + 3;              // top-right corner
    // Helper: draw all bars and labels into map_b (called on init and popup dismiss)
    auto drawAll = [&]() {
        fillSBox (map_b, RA8875_BLACK);
        // Resume button — top-right, small, drawn first
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        drawStringInBox ("Resume", resume_b, false, RA8875_GREEN);
        // Title on its own line using standard HC font-metric height
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (DE_COLOR);
        tft.setCursor (map_b.x + 2, map_b.y + PANETITLE_H);
        tft.print (title);
        // Legend: on same row as Resume, directly left of it
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        { uint16_t lx=(uint16_t)(resume_b.x-192), ly=(uint16_t)(resume_b.y+2);
          tft.fillRect(lx,ly,9,7,_SPP_SUNLIT_C); tft.setTextColor(_SPP_SUNLIT_C);
          tft.setCursor(lx+12,ly); tft.print("Sunlit");
          tft.fillRect(lx+66,ly,9,7,_SPP_ECLIP_C); tft.setTextColor(_SPP_ECLIP_C);
          tft.setCursor(lx+79,ly); tft.print("Eclipse"); }
        // Hour ruler on the listing line — labels just above ticks
        selectFontStyle (LIGHT_FONT, FAST_FONT); tft.setTextColor (_SPP_RULER_C);
        {
            // Show timezone offset at left of ruler
            char tz_lbl[10];
            int tz_h = (int)(tz / 3600);
            snprintf (tz_lbl, sizeof(tz_lbl), "UTC%+d", tz_h);
            tft.setCursor (map_b.x + 2, map_b.y + LISTING_Y0 - LISTING_DY);
            tft.print (tz_lbl);
        }
        uint16_t ry0 = map_b.y + LISTING_Y0 + 2;   // tick bottom
        for (int h=0; h<=24; h+=3) {
            uint16_t rx=sod2x(h*3600);
            tft.drawLine(rx,ry0-5,rx,ry0,1,_SPP_RULER_C);  // tick upward
            if (h%6==0 && h<24) { char lb[4]; snprintf(lb,sizeof(lb),"%02d",h);
              tft.setCursor(rx+2, map_b.y + LISTING_Y0 - LISTING_DY); tft.print(lb); } }
        // day rows
        for (int d=0; d<_SPP_N_DAYS; d++) {
            time_t t_ds=t_midnight+(time_t)d*SECSPERDAY, t_de=t_ds+SECSPERDAY;
            uint16_t ry=map_b.y+_SPP_TITLE_H+d*row_h;
            time_t t_ds_local = t_ds + tz;               // local midnight
            struct tm *tm_d=gmtime(&t_ds_local); char lbl[16];
            snprintf(lbl,sizeof(lbl),"%s %02d/%02d",
                     dayShortStr(tm_d->tm_wday+1),tm_d->tm_mon+1,tm_d->tm_mday);
            selectFontStyle(LIGHT_FONT,FAST_FONT);
            tft.setTextColor(d==0 ? _SPP_TODAY_C : _SPP_LBL_C);
            tft.setCursor(map_b.x+1,ry+row_h/2-4); tft.print(lbl);
            for (int h=6;h<24;h+=6) {
                uint16_t gx=sod2x(h*3600);
                tft.drawLine(gx,ry,gx,ry+row_h-1,1,h==12?_SPP_NOON_C:_SPP_GRID_C); }
            int n_today=0;
            for (int i=0;i<n_passes;i++) {
                if (passes[i].aos>=t_de||passes[i].los<=t_ds) continue;
                n_today++;
                int aos_sod=(int)(passes[i].aos-t_ds); if(aos_sod<0)aos_sod=0;
                int los_sod=(int)(passes[i].los-t_ds); if(los_sod>SECSPERDAY)los_sod=SECSPERDAY;
                uint16_t x1=sod2x(aos_sod),x2=sod2x(los_sod);
                uint16_t bw=(x2>x1+_SPP_BAR_MINW)?x2-x1:_SPP_BAR_MINW;
                uint16_t bh=(uint16_t)(passes[i].max_el/45.0F*(row_h-2));
                if(bh>(uint16_t)(row_h-1))bh=(uint16_t)(row_h-1);
                if(bh<4)bh=4;
                tft.fillRect(x1,ry+row_h-bh,bw,bh,
                             passes[i].sunlit?_SPP_SUNLIT_C:_SPP_ECLIP_C); }
            if (n_today==0) {
                selectFontStyle(LIGHT_FONT,FAST_FONT);
                tft.setTextColor(RGB565(50,50,65));
                tft.setCursor(tl_x+tl_w/2-20,ry+row_h/2-4); tft.print("no passes"); }
            tft.drawLine(map_b.x+1,ry+row_h-1,map_b.x+map_b.w-2,ry+row_h-1,
                         1,RGB565(30,30,45)); }
        // "You are here" — dashed yellow line at current local time
        {
            time_t now_local = nowWO() + tz;
            int now_sod = (int)(now_local % SECSPERDAY);
            if (now_sod < 0) now_sod += SECSPERDAY;
            uint16_t nx = sod2x(now_sod);
            if (nx >= (uint16_t)tl_x && nx < map_b.x + map_b.w) {
                // Dashed line across all 14 rows
                for (int d2=0; d2<_SPP_N_DAYS; d2++) {
                    uint16_t ry2 = map_b.y + _SPP_TITLE_H + d2*row_h;
                    for (uint16_t y2=ry2+2; y2<ry2+row_h-2; y2+=5)
                        tft.drawLine(nx,y2,nx,y2+2,1,RGB565(255,220,0));
                }
                // Downward triangle at ruler
                uint16_t ry0 = map_b.y + LISTING_Y0 + 2;
                tft.fillTriangle(nx-3,ry0-8,nx+3,ry0-8,nx,ry0-2,RGB565(255,220,0));
            }
        }
        tft.drawPR();
    };

    SBox   popup_b = {0,0,0,0};
    bool   popup_up = false;
    char   popup_line1[50] = {};
    char   popup_line2[50] = {};

    // Draw once up front, then refresh every 2 s so the sky dome
    // and "you are here" line stay live without blocking the left pane.
    drawAll();

    time_t expire_t = nowWO() + _SPP_TIMEOUT / 1000;

    for (;;) {
        // Refresh "you are here" + keep left-pane sky dome live
        drawAll();
        if (popup_up) {
            tft.fillRect(popup_b.x,popup_b.y,popup_b.w,popup_b.h,RGB565(15,15,30));
            tft.drawRect(popup_b.x,popup_b.y,popup_b.w,popup_b.h,DE_COLOR);
            selectFontStyle(LIGHT_FONT,FAST_FONT);
            tft.setTextColor(RA8875_WHITE);
            tft.setCursor(popup_b.x+5,popup_b.y+6);  tft.print(popup_line1);
            tft.setCursor(popup_b.x+5,popup_b.y+20); tft.print(popup_line2);
        }
        tft.drawPR();
        drawSatPass ();           // update sky dome in left pane

        if (nowWO() >= expire_t) break;

        UserInput ui = { map_b, UI_UFuncNone, UF_UNUSED, 2000, UF_CLOCKSOK,
                         {0,0}, TT_NONE, '\0', false, false };
        bool _got = waitForUser (ui);
        if (!_got) continue;   // 2-second tick — loop and redraw

        if (ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL || ui.kb_char == CHAR_ESC
                || inBox (ui.tap, resume_b)
                || (ui.kb_char == CHAR_NONE && !inBox (ui.tap, map_b)))
            break;

        if (ui.tap.x > (uint16_t)tl_x && ui.tap.y > map_b.y + _SPP_TITLE_H) {
            int d = (ui.tap.y - map_b.y - _SPP_TITLE_H) / row_h;
            if (d >= 0 && d < _SPP_N_DAYS) {
                time_t t_ds = t_midnight + (time_t)d * SECSPERDAY;
                time_t t_de = t_ds + SECSPERDAY;
                SPEntry *hit = NULL; int best_dx = INT_MAX;
                for (int i = 0; i < n_passes; i++) {
                    if (passes[i].aos >= t_de || passes[i].los <= t_ds) continue;
                    int a=(int)(passes[i].aos-t_ds); if(a<0)a=0;
                    int l=(int)(passes[i].los-t_ds); if(l>SECSPERDAY)l=SECSPERDAY;
                    uint16_t x1=sod2x(a),x2=sod2x(l);
                    if((int)ui.tap.x>=x1-6&&(int)ui.tap.x<=x2+6) {
                        int dx=abs((int)ui.tap.x-(x1+x2)/2);
                        if(dx<best_dx){best_dx=dx;hit=&passes[i];} }
                }
                if (hit) {
                    // Build popup — copy gmtime structs before second call
                    // (gmtime returns pointer to static buf; two calls overwrite each other)
                    char aos_s[10],tca_s[10],los_s[10],dur_s[12];
                    time_t aos_l = hit->aos + tz;
                    time_t tca_l = hit->tca + tz;
                    time_t los_l = hit->los + tz;
                    struct tm tm_aos = *gmtime(&aos_l);
                    struct tm tm_tca = *gmtime(&tca_l);
                    struct tm tm_los = *gmtime(&los_l);
                    snprintf(aos_s,sizeof(aos_s),"%02d:%02d",tm_aos.tm_hour,tm_aos.tm_min);
                    snprintf(tca_s,sizeof(tca_s),"%02d:%02d",tm_tca.tm_hour,tm_tca.tm_min);
                    snprintf(los_s,sizeof(los_s),"%02d:%02d",tm_los.tm_hour,tm_los.tm_min);
                    int dur=(int)(hit->los-hit->aos); if(dur<0)dur=0;
                    if(dur<3600) snprintf(dur_s,sizeof(dur_s),"%dm%02ds",(int)(dur/60),(int)(dur%60));
                    else snprintf(dur_s,sizeof(dur_s),"%dh%02dm",(int)(dur/3600),(int)((dur%3600)/60));
                    snprintf(popup_line1,sizeof(popup_line1),
                             "AOS %s  TCA %s  LOS %s",aos_s,tca_s,los_s);
                    snprintf(popup_line2,sizeof(popup_line2),"Max %.0f\xc2\xb0   Up %s   %s",
                             hit->max_el,dur_s,hit->sunlit?"Sunlit":"Eclipse");
                    uint16_t pw=280,ph=36;
                    uint16_t px=(uint16_t)(ui.tap.x+8);
                    uint16_t py=(uint16_t)(ui.tap.y-ph-6);
                    if(px+pw>map_b.x+map_b.w) px=(uint16_t)(ui.tap.x-pw-2);
                    if(py<map_b.y) py=(uint16_t)(ui.tap.y+4);
                    popup_b={px,py,pw,ph};
                    tft.fillRect(px,py,pw,ph,RGB565(15,15,30));
                    tft.drawRect(px,py,pw,ph,DE_COLOR);
                    selectFontStyle(LIGHT_FONT,FAST_FONT);
                    tft.setTextColor(RA8875_WHITE);
                    tft.setCursor(px+5,py+6);  tft.print(popup_line1);
                    tft.setCursor(px+5,py+20); tft.print(popup_line2);
                    popup_up=true;
                    tft.drawPR();
                } else if (popup_up) {
                    // Tapped empty space — dismiss popup
                    popup_up = false;
                    drawAll();
                }
            }
        } else if (popup_up && inBox(ui.tap, map_b)) {
            // Tapped title/ruler area with popup showing — dismiss
            popup_up = false;
            drawAll();
        }
    }

    // Flash Resume for tactile feedback, brief pause so user sees it
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    drawStringInBox ("Resume", resume_b, true, RA8875_GREEN);
    tft.drawPR();
    delay (250);
    free (passes);

    #undef _SPP_LABEL_W
    #undef _SPP_TITLE_H
    #undef _SPP_N_DAYS
    #undef _SPP_MAX_PASS
    #undef _SPP_TIMEOUT
    #undef _SPP_BAR_MINW
    #undef _SPP_SUNLIT_C
    #undef _SPP_ECLIP_C
    #undef _SPP_GRID_C
    #undef _SPP_NOON_C
    #undef _SPP_LBL_C
    #undef _SPP_RULER_C
    #undef _SPP_TODAY_C
}


static float starLST (time_t utc)
{
    double jd   = (double)utc / 86400.0 + 2440587.5;
    double d    = jd - 2451545.0;
    double gmst = fmod (280.46061837 + 360.98564736629*d
                        + 0.000387933*(d/36525.0)*(d/36525.0), 360.0);
    double lst  = fmod (gmst + (double)de_ll.lng_d, 360.0);
    if (lst < 0) lst += 360.0;
    return (float)lst;
}

static void showSkyView (SatState &s)
{
    const uint16_t TITLE_H = PANETITLE_H;
    const uint16_t INFO_H  = 36;
    uint16_t sky_x = map_b.x, sky_y = map_b.y + TITLE_H;
    uint16_t sky_w = map_b.w, sky_h = map_b.h - TITLE_H - INFO_H;
    uint16_t hz_y  = sky_y + (uint16_t)(sky_h * 0.62F);
    float px_per_el = (hz_y - sky_y) / 90.0F;
    float px_per_az = sky_w / 360.0F;

    SBox resume_b;
    resume_b.w = 78; resume_b.h = 20;
    resume_b.x = map_b.x + map_b.w - resume_b.w - 3;
    resume_b.y = map_b.y + 3;

    // az_offset is updated before every drawAll() so satellite stays centred.
    float az_offset = 0.0F;

    auto az2x = [&](float az) -> int {
        float daz = az - az_offset;
        while (daz >  180) daz -= 360;
        while (daz < -180) daz += 360;
        int x = (int)(sky_x + sky_w/2 + daz * px_per_az + 0.5F);
        // clamp -- callers may feed in extreme az (eg satellite trail) that would
        // otherwise map to a pixel outside the frame buffer
        if (x < 0) x = 0;
        else if (x > (int)tft.width()-1) x = tft.width()-1;
        return x;
    };
    auto el2y = [&](float el) -> int {
        int y = (int)(hz_y - el * px_per_el + 0.5F);
        // clamp -- eg a satellite trail point well below the horizon (el near -90)
        // would otherwise map to a pixel outside the frame buffer
        if (y < 0) y = 0;
        else if (y > (int)tft.height()-1) y = tft.height()-1;
        return y;
    };

    // Trailing track — record last _SKY_TRAIL_MAX positions (az/el/sunlit)
    // Updated each tick; drawn inside drawAll before the crosshair.
    #define _SKY_TRAIL_MAX  90              // ~3 min at 2s/tick
    float trail_az[_SKY_TRAIL_MAX] = {};
    float trail_el[_SKY_TRAIL_MAX] = {};
    bool  trail_sl[_SKY_TRAIL_MAX] = {};
    int   trail_n  = 0;

    auto drawAll = [&](float sat_az, float sat_el, bool sat_sunlit, float sat_range) {
        fillSBox (map_b, RA8875_BLACK);
        tft.fillRect (sky_x, hz_y+1, sky_w, (sky_y+sky_h)-(hz_y+1), RGB565(8,18,8));

        float lst=starLST(myNow()), lat_r=de_ll.lat_d*(float)M_PIF/180.0F;
        float sinlat=sinf(lat_r), coslat=cosf(lat_r);
        for (int si=0; si<N_STARS; si++) {
            const StarEntry &se=stars_bsc5[si];
            float ra_deg=se.ra_cd/100.0F, dec_r=se.dec_cd/100.0F*(float)M_PIF/180.0F;
            float sindec=sinf(dec_r), cosdec=cosf(dec_r);
            float H_r=(lst-ra_deg)*(float)M_PIF/180.0F, cosH=cosf(H_r), sinH=sinf(H_r);
            float sinEl=sindec*sinlat+cosdec*coslat*cosH;
            if (sinEl<sinf(-6.0F*(float)M_PIF/180.0F)) continue;
            float el_d=asinf(sinEl)*180.0F/(float)M_PIF, cosEl=cosf(asinf(sinEl));
            float cosAz=(sindec-sinlat*sinEl)/(coslat*cosEl+1e-9F);
            if(cosAz>1)cosAz=1;
            if(cosAz<-1)cosAz=-1;
            float az_d=acosf(cosAz)*180.0F/(float)M_PIF;
            if(sinH>0) az_d=360.0F-az_d;
            int sy=el2y(el_d);
            if(sy<(int)sky_y-4||sy>(int)(sky_y+sky_h)) continue;
            {
                int sx=az2x(az_d);
                if(sx>=(int)sky_x && sx<(int)(sky_x+sky_w)){
                    uint16_t col=(el_d<0)?RGB565(55,55,75):(el_d<12)?RGB565(140,140,155):RA8875_WHITE;
                    if(se.vmag_t<=10){tft.drawPixel(sx,sy,col);tft.drawPixel(sx+1,sy,col);
                                     tft.drawPixel(sx,sy+1,col);tft.drawPixel(sx+1,sy+1,col);}
                    else if(se.vmag_t<=25) tft.fillCircle(sx,sy,1,col);
                    else tft.drawPixel(sx,sy,col);
                }
            }
        }

        selectFontStyle(LIGHT_FONT,FAST_FONT);
        for(int eg=30;eg<=60;eg+=30){
            int gy=el2y(eg);
            if(gy<(int)sky_y||gy>(int)hz_y) continue;
            for(uint16_t gx=sky_x;gx<sky_x+sky_w;gx+=12)
                tft.drawLine(gx,gy,gx+6,gy,1,RGB565(35,35,60));
            tft.setTextColor(RGB565(55,55,90));
            char lb[6]; snprintf(lb,sizeof(lb),"%d\xc2\xb0",eg);
            tft.setCursor(sky_x+3,gy-8); tft.print(lb);
        }

        tft.drawLine(sky_x,hz_y,sky_x+sky_w-1,hz_y,1,RGB565(255,140,0));
        for(int at=0;at<360;at+=10){
            int tx=az2x(at); bool major=(at%45==0);
            if(tx<(int)sky_x||tx>=(int)(sky_x+sky_w)) continue;
            tft.drawLine(tx,hz_y-(major?5:2),tx,hz_y+(major?5:2),
                         1,major?RGB565(255,200,80):RGB565(100,75,30));
        }
        const struct{float az;const char*lbl;}dirs[]={
            {0,"N"},{45,"NE"},{90,"E"},{135,"SE"},
            {180,"S"},{225,"SW"},{270,"W"},{315,"NW"}};
        for(auto &d:dirs){
            int cx=az2x(d.az);
            if(cx<(int)sky_x+2||cx>=(int)(sky_x+sky_w)-10) continue;
            tft.setTextColor(RGB565(220,170,60));
            tft.setCursor((uint16_t)(cx-4),hz_y+7); tft.print(d.lbl);
        }

        // Trailing track — fade from invisible at tail to full at head
        if (trail_n > 1) {
            for (int ti = 1; ti < trail_n; ti++) {
                // Skip segments that cross the az=0/360 wrap
                float daz = fabsf (trail_az[ti] - trail_az[ti-1]);
                if (daz > 180.0F) continue;
                // Brightness: 0 at oldest, 200 near head
                float frac = (float)(ti-1) / (float)(trail_n > 1 ? trail_n-1 : 1);
                uint8_t v  = (uint8_t)(frac * 200.0F);
                uint16_t tcol = trail_sl[ti]
                    ? RGB565(0, v, 0)         // green — sunlit
                    : RGB565(v, v/2, 0);      // orange — eclipse
                int tx1=az2x(trail_az[ti-1]), tx2=az2x(trail_az[ti]);
                if(tx1>=(int)sky_x && tx1<(int)(sky_x+sky_w) &&
                   tx2>=(int)sky_x && tx2<(int)(sky_x+sky_w))
                    tft.drawLine (tx1, el2y(trail_el[ti-1]),
                                  tx2, el2y(trail_el[ti]), 1, tcol);
            }
        }

        int sat_sx=az2x(sat_az), sat_sy=el2y(sat_el), csy=sat_sy;
        // Clamp crosshair to the correct zone:
        //   above horizon (el >= 0) → sky area  [sky_y+4 .. hz_y-4]
        //   below horizon (el <  0) → ground area [hz_y+4 .. sky_y+sky_h-4]
        if (sat_el >= 0) {
            if (csy < (int)sky_y+4)   csy = sky_y+4;
            if (csy > (int)hz_y-4)    csy = hz_y-4;
        } else {
            if (csy < (int)hz_y+4)    csy = hz_y+4;
            if (csy > (int)(sky_y+sky_h-4)) csy = sky_y+sky_h-4;
        }
        // Full brightness when visible; dim when underground
        uint16_t scol = sat_el >= 0
            ? (sat_sunlit ? RGB565(50,230,50) : RGB565(255,110,0))
            : RGB565(60,60,60);
        tft.drawLine(sat_sx-8,csy,sat_sx+8,csy,1,scol);
        tft.drawLine(sat_sx,csy-8,sat_sx,csy+8,1,scol);
        tft.fillCircle(sat_sx,csy,3,scol);
        // Dashed guide line from crosshair down to horizon (above-horizon only)
        if (sat_el >= 0 && sat_sy < (int)hz_y)
            for(int gy=csy+10;gy<(int)hz_y;gy+=8)
                tft.drawLine(sat_sx,gy,sat_sx,gy+4,1,RGB565(40,80,40));

        selectFontStyle(LIGHT_FONT,SMALL_FONT);
        tft.setTextColor(DE_COLOR);
        char ttl[48]; snprintf(ttl,sizeof(ttl),"%s \xe2\x80\x94 Sky View",s.name);
        maxStringW(ttl,map_b.w-resume_b.w-12);
        tft.setCursor(map_b.x+2,map_b.y+PANETITLE_H); tft.print(ttl);
        selectFontStyle(LIGHT_FONT,FAST_FONT);
        drawStringInBox("Resume",resume_b,false,RA8875_GREEN);

        tft.setTextColor(BRGRAY);
        char info[90];
        if(sat_el>=-1)
            snprintf(info,sizeof(info),"Az %5.1f\xc2\xb0   El %+5.1f\xc2\xb0   Range %6.0f km   %s",
                     sat_az,sat_el,sat_range,sat_sunlit?"Sunlit":"Eclipse");
        else
            snprintf(info,sizeof(info),"Az %5.1f\xc2\xb0   El %+5.1f\xc2\xb0   (Below horizon)",
                     sat_az,sat_el);
        uint16_t iw=getTextWidth(info);
        tft.setCursor(map_b.x+(map_b.w-iw)/2,map_b.y+map_b.h-INFO_H+14); tft.print(info);
    };

    // Named star lookup table
    // Named star lookup table — IAU proper names + key Bayer designations
// Matched against stars_bsc5[] by RA/Dec proximity (within 0.5 degrees).
static const struct {
    uint16_t   ra_cd;
    int16_t    dec_cd;
    const char *name;
} star_names[] = {
    {  1090,  -1799, "Diphda"},
    {  1474,   6052, "Schedar"},
    {  1620,   2905, "Alpheratz"},
    {  2443,  -5724, "Achernar"},
    {  2501,   8926, "Polaris"},
    {  3174,   4090, "Algol"},
    {  3180,   2346, "Hamal"},
    {  3374,   2407, "Mirach"},
    {  3914,  -1356, "Cursa"},
    {  4305,  -5338, "Ankaa"},
    {  5108,   4986, "Mirfak"},
    {  5706,  -1796, "Phakt"},
    {  6047,   4498, "Menkalinan"},
    {  6399,  -5270, "Canopus"},
    {  6898,   1651, "Aldebaran"},
    {  7864,   -820, "Rigel"},
    {  7917,   4600, "Capella"},
    {  8007,  -4001, "Naos"},
    {  8063,     -3, "Mintaka"},
    {  8128,    635, "Bellatrix"},
    {  8157,   2861, "Elnath"},
    {  8405,   -120, "Alnilam"},
    {  8519,   -194, "Alnitak"},
    {  8879,    741, "Betelgeuse"},
    {  9360,  -5950, "Tureis"},
    {  9567,  -1796, "Mirzam"},
    {  9943,   1640, "Alhena"},
    { 10128,  -1672, "Sirius"},
    { 10466,  -2897, "Adhara"},
    { 10710,  -2639, "Wezen"},
    { 11310,  -1758, "Aludra"},
    { 11365,   3189, "Castor"},
    { 11483,    522, "Procyon"},
    { 11633,   2803, "Pollux"},
    { 12184,  -5796, "Aspidiske"},
    { 12239,  -4734, "Gamma Vel."},
    { 12563,  -5951, "Avior"},
    { 13117,  -5471, "Delta Vel."},
    { 13249,   5496, "Mizar"},
    { 13830,  -6972, "Miaplacidus"},
    { 14039,  -3638, "Gienah"},
    { 14190,   -865, "Alphard"},
    { 15017,   1154, "Coxa"},
    { 15209,   1197, "Regulus"},
    { 15499,   1984, "Algieba"},
    { 15884,   1215, "Denebola"},
    { 16087,   1961, "Zosma"},
    { 16524,   6175, "Merak"},
    { 16593,   6175, "Dubhe"},
    { 17252,  -3628, "Menkent"},
    { 17847,   2843, "Alphecca"},
    { 17860,   5367, "Phecda"},
    { 18665,  -6310, "Acrux"},
    { 18779,  -5711, "Gacrux"},
    { 19192,  -5969, "Mimosa"},
    { 19351,   5596, "Alioth"},
    { 19629,   1429, "Rasalgethi"},
    { 20052,   3086, "Eltanin"},
    { 20130,  -1116, "Spica"},
    { 20543,   4553, "Sadr 2"},
    { 20688,   4931, "Alkaid"},
    { 21096,  -6037, "Hadar"},
    { 21391,   1918, "Arcturus"},
    { 21990,  -6083, "Rigil Kent."},
    { 22230,   7415, "Kochab"},
    { 22523,   -384, "Sadalsuud"},
    { 22796,  -3864, "Kaus Bor."},
    { 23552,   7763, "Alderamin"},
    { 24735,  -2643, "Antares"},
    { 25012,  -1958, "Dschubba"},
    { 25218,  -6903, "Atria"},
    { 26168,  -3763, "Wei"},
    { 26266,   -978, "Rasalhague"},
    { 26340,  -3710, "Shaula"},
    { 26433,  -4300, "Sargas"},
    { 26528,  -3707, "Lesath"},
    { 26792,  -3406, "Girtab"},
    { 27531,  -3438, "Kaus Aust."},
    { 27856,   4528, "Sadr"},
    { 27923,   3878, "Vega"},
    { 27930,   2774, "Albireo"},
    { 28382,  -2630, "Nunki"},
    { 28830,  -2678, "Ascella"},
    { 29110,  -2101, "Kaus Med."},
    { 29770,    887, "Altair"},
    { 30360,    -16, "Enif"},
    { 30506,   2778, "Gienah Cyg."},
    { 30641,  -5674, "Peacock"},
    { 31036,   4528, "Deneb"},
    { 32081,   1521, "Sadalmelik"},
    { 33131,  -4696, "Al Nair"},
    { 33671,   1521, "Markab"},
    { 33993,   2817, "Scheat"},
    { 34441,  -2962, "Fomalhaut"},
};
#define N_STAR_NAMES 94

    SBox   star_popup_b = {0,0,0,0};
    bool   star_popup_up = false;
    char   star_popup_line1[32] = {};
    char   star_popup_line2[24] = {};
    Sun    sun;

    for(;;){
        DateTime t=userDateTime(myNow());
        s.sat->predict(t);
        float el,az,range,rate;
        s.sat->topo(obs,el,az,range,rate);
        sun.predict(t); s.sat->predict(t);
        bool sunlit=!s.sat->eclipsed(&sun);
        // Center panorama on current satellite azimuth
        az_offset = az;

        // Centre panorama on satellite before drawing
        az_offset = az;

        // Append position to trail (shift-down when full)
        if (trail_n < _SKY_TRAIL_MAX) {
            trail_az[trail_n] = az;
            trail_el[trail_n] = el;
            trail_sl[trail_n] = sunlit;
            trail_n++;
        } else {
            memmove (trail_az, trail_az+1, (trail_n-1)*sizeof(float));
            memmove (trail_el, trail_el+1, (trail_n-1)*sizeof(float));
            memmove (trail_sl, trail_sl+1, (trail_n-1)*sizeof(bool));
            trail_az[trail_n-1] = az;
            trail_el[trail_n-1] = el;
            trail_sl[trail_n-1] = sunlit;
        }

        drawAll(az,el,sunlit,range);

        // Redraw star popup on top after each full redraw
        if (star_popup_up) {
            tft.fillRect(star_popup_b.x,star_popup_b.y,
                         star_popup_b.w,star_popup_b.h,RGB565(15,15,35));
            tft.drawRect(star_popup_b.x,star_popup_b.y,
                         star_popup_b.w,star_popup_b.h,DE_COLOR);
            selectFontStyle(LIGHT_FONT,FAST_FONT);
            tft.setTextColor(RA8875_WHITE);
            tft.setCursor(star_popup_b.x+5, star_popup_b.y+5);
            tft.print(star_popup_line1);
            tft.setTextColor(BRGRAY);
            tft.setCursor(star_popup_b.x+5, star_popup_b.y+17);
            tft.print(star_popup_line2);
        }
        tft.drawPR();
        drawSatPass ();           // keep left-pane sky dome live

        UserInput ui={map_b,UI_UFuncNone,UF_UNUSED,2000,UF_CLOCKSOK,
                      {0,0},TT_NONE,'\0',false,false};
        bool got=waitForUser(ui);

        if (!got) continue;   // 2s timeout — loop and redraw satellite position

        // Exit
        if (inBox(ui.tap,resume_b)||ui.kb_char==CHAR_ESC||
            ui.kb_char==CHAR_CR||ui.kb_char==CHAR_NL||
            (!inBox(ui.tap,map_b)&&ui.kb_char==CHAR_NONE)) break;

        // Star tap — find nearest star within 14px of tap
        if (ui.tap.x > 0 && inBox(ui.tap,map_b) &&
            ui.tap.y > (uint16_t)(map_b.y+PANETITLE_H) &&
            ui.tap.y < (uint16_t)(map_b.y+map_b.h-INFO_H)) {

            float lst2=starLST(myNow());
            float lat2=de_ll.lat_d*(float)M_PIF/180.0F;
            float sl2=sinf(lat2), cl2=cosf(lat2);
            float best_d=14.0F; int best_i=-1;

            for (int si=0; si<N_STARS; si++) {
                const StarEntry &se=stars_bsc5[si];
                float ra_d=se.ra_cd/100.0F;
                float dec_r=se.dec_cd/100.0F*(float)M_PIF/180.0F;
                float sd=sinf(dec_r), cd=cosf(dec_r);
                float H_r=(lst2-ra_d)*(float)M_PIF/180.0F;
                float sinEl2=sd*sl2+cd*cl2*cosf(H_r);
                if(sinEl2<sinf(-6.0F*(float)M_PIF/180.0F)) continue;
                float el_s=asinf(sinEl2)*180.0F/(float)M_PIF;
                float cosEl2=cosf(asinf(sinEl2));
                float cAz=(sd-sl2*sinEl2)/(cl2*cosEl2+1e-9F);
                if(cAz>1)cAz=1;
                if(cAz<-1)cAz=-1;
                float az_s=acosf(cAz)*180.0F/(float)M_PIF;
                if(sinf(H_r)>0) az_s=360.0F-az_s;
                int sx=az2x(az_s), sy=el2y(el_s);
                float d=hypotf(sx-(int)ui.tap.x, sy-(int)ui.tap.y);
                if(d<best_d){best_d=d; best_i=si;}
            }

            star_popup_up = false;

            if (best_i >= 0) {
                const StarEntry &bs=stars_bsc5[best_i];
                const char *sname=NULL;
                for (int ni=0; ni<N_STAR_NAMES; ni++) {
                    if (abs((int)bs.ra_cd-(int)star_names[ni].ra_cd)<60 &&
                        abs((int)bs.dec_cd-(int)star_names[ni].dec_cd)<60) {
                        sname=star_names[ni].name; break;
                    }
                }
                snprintf(star_popup_line1, sizeof(star_popup_line1),
                         "%s", sname ? sname : "Star");
                snprintf(star_popup_line2, sizeof(star_popup_line2),
                         "Magnitude %.1f", bs.vmag_t/10.0F);

                uint16_t pw=150, ph=28;
                uint16_t px=(uint16_t)(ui.tap.x+10);
                uint16_t py=(uint16_t)(ui.tap.y-ph-4);
                if(px+pw>map_b.x+map_b.w) px=(uint16_t)(ui.tap.x-pw-4);
                if(py<(uint16_t)(map_b.y+PANETITLE_H)) py=(uint16_t)(ui.tap.y+6);
                star_popup_b={px,py,pw,ph};
                tft.fillRect(px,py,pw,ph,RGB565(15,15,35));
                tft.drawRect(px,py,pw,ph,DE_COLOR);
                selectFontStyle(LIGHT_FONT,FAST_FONT);
                tft.setTextColor(RA8875_WHITE);
                tft.setCursor(px+5,py+5);  tft.print(star_popup_line1);
                tft.setTextColor(BRGRAY);
                tft.setCursor(px+5,py+17); tft.print(star_popup_line2);
                star_popup_up=true;
                tft.drawPR();
            }
        }
    }
    selectFontStyle(LIGHT_FONT,FAST_FONT);
    drawStringInBox("Resume",resume_b,true,RA8875_GREEN);
    tft.drawPR(); delay(250);
}

/* Build OrbTrack URL for this satellite.
 * Uses NORAD catalog ID (satSCN) when known, falls back to URL-encoded name.
 */
static void buildOrbTrackURL (const SatState &s, char *buf, int bufsz)
{
    if (s.norad > 0) {
        snprintf (buf, bufsz, "https://www.orbtrack.org/#/?satSCN=%d", s.norad);
    } else {
        // HC stores spaces as underscores; encode for URL
        char enc[128] = {};
        int  ei = 0;
        for (const char *p = s.name; *p && ei < (int)sizeof(enc) - 4; p++) {
            char c = (*p == '_') ? ' ' : *p;
            if      (c == ' ')  { enc[ei++] = '%'; enc[ei++] = '2'; enc[ei++] = '0'; }
            else if (c == '(')  { enc[ei++] = '%'; enc[ei++] = '2'; enc[ei++] = '8'; }
            else if (c == ')')  { enc[ei++] = '%'; enc[ei++] = '2'; enc[ei++] = '9'; }
            else                  enc[ei++] = c;
        }
        snprintf (buf, bufsz, "https://www.orbtrack.org/#/?satName=%s", enc);
    }
}

/* Build SatNogs URL for this satellite (requires NORAD ID). */
static void buildSatNogsURL (const SatState &s, char *buf, int bufsz)
{
    if (s.norad > 0)
        snprintf (buf, bufsz,
                  "https://db.satnogs.org/satellite/%d/#mapcontent", s.norad);
    else
        snprintf (buf, bufsz, "https://db.satnogs.org/");
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
        _SMI_PATH1, _SMI_PASS1, _SMI_TABLE1, _SMI_FREQ1, _SMI_PLAN1,
        _SMI_MPROG1,
        _SMI_SKYVIEW1,
        _SMI_ORBTRACK1,
        _SMI_SATNOGS1,
        _SMI_NAME2,
        _SMI_PATH2, _SMI_PASS2, _SMI_TABLE2, _SMI_FREQ2, _SMI_PLAN2,
        _SMI_MPROG2,
        _SMI_SKYVIEW2,
        _SMI_ORBTRACK2,
        _SMI_SATNOGS2,
        _SMI_AMSAT_STATUS,
        _SMI_GROUPSKED,
        _SMI_COVIS,
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
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show freq/modes", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show planning tool", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show pass progression", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show sky view", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show OrbTrack", NULL},
        {menu_sat1,   false, 1, _DXS_INDENT2,  "Show SatNogs",  NULL},

        {menu_name2,  false, 1, _DXS_INDENT1,  name2, NULL},
        {menu_path2,  path2, 3, _DXS_INDENT2,  "Show track also", NULL},
        {menu_pass2,  false, 1, _DXS_INDENT2,  "Show pass here", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show rise/set table", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show freq/modes", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show planning tool", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show pass progression", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show sky view", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show OrbTrack", NULL},
        {menu_sat2,   false, 1, _DXS_INDENT2,  "Show SatNogs",  NULL},
        {MENU_1OFN,   false, 1, _DXS_INDENT1,  "Show Status",   NULL},
        {MENU_1OFN,   false, 1, _DXS_INDENT1,  "Group Schedule", NULL},
        {MENU_1OFN,   false, 1, _DXS_INDENT1,  "Co-Visibility", NULL},
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
                case _SMI_FREQ1:
                    // show freq/mode table for sat 0
                    dxpaneSat = 0;
                    showSatFreqs (sat_state[0]);
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
                case _SMI_FREQ2:
                    // show freq/mode table for sat 1
                    dxpaneSat = 1;
                    showSatFreqs (sat_state[1]);
                    initScreen();
                    break;
                case _SMI_PLAN2:
                    // restore DX pane and show tool for sat 1 then restore normal map
                    dxpaneSat = 1;
                    drawSatPass();
                    drawSatTool();
                    initEarthMap();
                    break;
                case _SMI_MPROG1:
                    // show multi-day pass progression for sat 0
                    dxpaneSat = 0;
                    showSatPassProg (sat_state[0]);
                    initEarthMap();
                    break;
                case _SMI_MPROG2:
                    // show multi-day pass progression for sat 1
                    dxpaneSat = 1;
                    showSatPassProg (sat_state[1]);
                    initEarthMap();
                    break;
                case _SMI_SKYVIEW1:
                    dxpaneSat = 0;
                    showSkyView (sat_state[0]);
                    initEarthMap();
                    break;
                case _SMI_SKYVIEW2:
                    dxpaneSat = 1;
                    showSkyView (sat_state[1]);
                    initEarthMap();
                    break;
                case _SMI_ORBTRACK1: {
                    char url[128];
                    buildOrbTrackURL (sat_state[0], url, sizeof(url));
                    openURL (url);
                    break;
                }
                case _SMI_ORBTRACK2: {
                    char url[128];
                    buildOrbTrackURL (sat_state[1], url, sizeof(url));
                    openURL (url);
                    break;
                }
                case _SMI_SATNOGS1: {
                    char url[128];
                    buildSatNogsURL (sat_state[0], url, sizeof(url));
                    openURL (url);
                    break;
                }
                case _SMI_SATNOGS2: {
                    char url[128];
                    buildSatNogsURL (sat_state[1], url, sizeof(url));
                    openURL (url);
                    break;
                }
                case _SMI_AMSAT_STATUS:
                    openURL ("https://www.amsat.org/status/");
                    break;
                case _SMI_GROUPSKED:
                    drawSatGroupSchedule ();
                    initEarthMap ();
                    break;
                case _SMI_COVIS:
                    drawSatCoVis ();
                    initEarthMap ();
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
