/* manage space weather stats.
 */


#include "HamClock.h"


// retrieve pages
static const char bzbt_page[] = "/Bz/Bz.txt";
static const char swind_page[] = "/solar-wind/swind-24hr.txt";
static const char ssn_page[] = "/ssn/ssn-31.txt";
static const char sf_page[] = "/solar-flux/solarflux-99.txt";
static const char drap_page[] = "/drap/stats.txt";
static const char kp_page[] = "/geomag/kindex.txt";
static const char xray_page[] = "/xray/xray.txt";
static const char proton_page[] = "/proton/protons.txt";
static const char noaaswx_page[] = "/NOAASpaceWX/noaaswx.txt";
static const char aurora_page[] = "/aurora/aurora.txt";
static const char dst_page[] = "/dst/dst.txt";
static const char sw_rank_page[] = "/NOAASpaceWX/rank2_coeffs.txt";

// caches
static BzBtData bzbt_cache;
static SolarWindData sw_cache;
static SunSpotData ssn_cache;
static SolarFluxData sf_cache;
static DRAPData drap_cache;
static XRayData xray_cache;
static ProtonData proton_cache;
static KpData kp_cache;
static NOAASpaceWxData noaasw_cache = {0, false, {'R', 'S', 'G'}, {}};
static AuroraData aurora_cache;
static DSTData dst_cache;

#define X(a,b,c,d,e,f,g,h,i) {a,b,c,d,e,f,g,h,i},     // expands SPCWX_DATA to each array initialization in {}
SpaceWeather_t space_wx[SPCWX_N] = {
    SPCWX_DATA
};
#undef X


/* bit mask of user's NXDXF_b SPCWX_t, or Auto (ie, sort based on impact) if none.
 * N.B. user selection relies on ranks being set
 */
static uint32_t spcwx_chmask;
#define SPCWX_AUTO 0                            // spcwx_chmask value that means sort based on impact



// handy conversion from space_wx value to it ranking contribution
/* compute rank from space weather value
 */
static int computeSWRank (const SpaceWeather_t *sp)
{
    return ((int) roundf ((sp->a * sp->value + sp->b) * sp->value + sp->c));
}

/* qsort-style function to compare the scaled value of two SpaceWeather_t.
 * N.B. largest first, any bad values at the end
 */
static int swQSF (const void *v1, const void *v2)
{
    const SpaceWeather_t *s1 = (SpaceWeather_t *)v1;
    const SpaceWeather_t *s2 = (SpaceWeather_t *)v2;

    if (!s1->value_ok)
        return (s2->value_ok);
    else if (!s2->value_ok)
        return (-1);

    int rank_val1 = computeSWRank(s1);
    int rank_val2 = computeSWRank(s2);
    return (rank_val2 - rank_val1);
}

/* init the slope and intercept of each space wx stat from sw_rank_page.
 * return whether successful.
 */
static bool initSWFit(void)
{
    WiFiClient sw_client;
    bool ok = false;

    Serial.println (sw_rank_page);
    if (sw_client.connect(backend_host, backend_port)) {

        updateClocks(false);

        char line[50];

        httpHCGET (sw_client, backend_host, sw_rank_page);

        if (!httpSkipHeader (sw_client)) {
            Serial.println ("RANKSW: rank page header short");
            goto out;
        }

        // local defaults until know all are good
        float a[SPCWX_N];
        float b[SPCWX_N];
        float c[SPCWX_N];

        for (int n_c = 0; n_c < SPCWX_N; ) {
            // read line
            if (!getTCPLine (sw_client, line, sizeof(line), NULL)) {
                Serial.printf ("RANKSW: rank file is short: %d/%d\n", n_c, SPCWX_N);
                goto out;
            }

            // ignore comments or empty
            if (line[0] == '#' || line[0] == '\0')
                continue;

            // require matching index and coeffs
            int get_i;
            if (sscanf (line, "%d %f %f %f", &get_i, &a[n_c], &b[n_c], &c[n_c]) != 4 || get_i != n_c) {
                Serial.printf ("RANKSW: bad rank line: %s\n", line);
                goto out;
            }

            // count
            n_c++;
        }

        // all good: log and store
        Serial.println ("RANKSW:   Coeffs Name       a       b       c");
        for (int i = 0; i < SPCWX_N; i++) {
            SpaceWeather_t &sw_i = space_wx[i];
            sw_i.a = a[i];
            sw_i.b = b[i];
            sw_i.c = c[i];
            Serial.printf ("RANKSW: %13s %7g %7g %7g\n", plot_names[sw_i.pc], sw_i.a, sw_i.b, sw_i.c);
        }

        ok = true;
    }

  out:

    // clean up -- aleady logged any errors
    updateClocks(false);
    sw_client.stop();
    return (ok);
}

/* go through space_wx and set the rank according to the importance of each value.
 */
static void sortSpaceWx()
{
    // copy space_wx and sort best first
    SpaceWeather_t sw_sort[SPCWX_N];
    memcpy (sw_sort, space_wx, sizeof(sw_sort));
    qsort (sw_sort, SPCWX_N, sizeof(SpaceWeather_t), swQSF);
    
    // set and log rank of each entry, 0 is best
    Serial.println ("RANKSW: rank      name    value score");
    for (int i = 0; i < SPCWX_N; i++) {
        SPCWX_t sp_i = sw_sort[i].sp;
        SpaceWeather_t &sw_i = space_wx[sp_i];
        sw_i.rank = i;
        Serial.printf ("RANKSW: %d %12s %8.2g %3d\n", i, plot_names[sw_i.pc], sw_i.value,
                                computeSWRank(&sw_i));
    }
}

/* present menu of all Space Weather choices in NCDXF_b, allow op to choose up to four or Auto.
 */
static void runNCDXFSpcWxMenu (void)
{
    // build menu of all SPCWX_N plus gap plus Auto
    MenuItem mitems[SPCWX_N+2];
    for (int i = 0; i < SPCWX_N; i++)
        mitems[i] = {MENU_0OFN, (spcwx_chmask & (1<<i)) != 0, 1, 1, space_wx[i].name, 0};
    mitems[SPCWX_N]   = {MENU_BLANK, 0, 0, 0, 0, 0};
    mitems[SPCWX_N+1] = {MENU_TOGGLE, spcwx_chmask == SPCWX_AUTO, 2, 1, "Auto", 0};
            
    // its box  
    SBox menu_b = NCDXF_b;                      // copy, not ref!
    menu_b.x += 1;  
    menu_b.w = 0;                               // shrink wrap
                
    // run          
    SBox ok_b;      
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, NARRAY(mitems), mitems};
    if (runMenu (menu)) {
                
        // set mask bits and ascending rank to implement chosen params unless explicit Auto
        if (mitems[SPCWX_N+1].set) {
            Serial.printf ("SPCWX: NCDXF table is now Auto\n");
            spcwx_chmask = SPCWX_AUTO;
            sortSpaceWx();
        } else {
            // N.B. assign ranks in same order as initSpaceWX()
            spcwx_chmask = 0;
            int rank = 0;
            for (int i = 0; i < SPCWX_N; i++) {
                if (mitems[i].set) {
                    space_wx[i].rank = rank++;
                    spcwx_chmask |= (1<<i);
                } else {
                    space_wx[i].rank = SPCWX_N;   // impossible rank
                }
            }       
            if (rank > NCDXF_B_NFIELDS)
                Serial.printf ("SPCWX: NDXCF table using only first %d selections\n", NCDXF_B_NFIELDS);
        }

        // save mask
        NVWriteUInt32 (NV_SPCWXCHOICE, spcwx_chmask);
        Serial.printf ("SPCWX: choice mask 0x%08x\n", spcwx_chmask);

        // refresh box
        drawNCDXFSpcWxStats(RA8875_BLACK);
    }
}

/* handle touch location s known to be within NCDXF_b showing space weather stats.
 * if within a numeric value: insure the space stat is in a visible Pane.
 * else in parameter name:    offer menu of desired parameters or Auto
 * N.B. coordinate layout with drawNCDXFStats()
 */
void doNCDXFSpcWxTouch (const SCoord &s)
{
    // decide which row, counting down from 0
    const uint16_t y_top = s.y - NCDXF_b.y;
    const uint16_t h = NCDXF_b.h / NCDXF_B_NFIELDS;
    const uint16_t r = y_top / h;

    // decide whether s is in number or name
    if (y_top > r*h + 3*h/4) {

        // s is in the thin title area so run menu to select stats or Auto
        runNCDXFSpcWxMenu();

    } else {

        // s is in larger name portion so engage data pane whose rank == row
        for (int i = 0; i < SPCWX_N; i++)
            if (space_wx[i].rank == r)
                setPlotVisible (space_wx[i].pc);
    }

}

/* draw the NCDXF_B_NFIELDS highest ranking space_wx in NCDXF_b.
 * use the given color for everything unless black then use the associated pane colors.
 */
void drawNCDXFSpcWxStats(uint16_t color)
{
    // handy
    static const char err[] = "Err";

    // arrays for drawNCDXFStats()
    char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN];
    uint16_t colors[NCDXF_B_NFIELDS];

    // assign by rank down from top, 0 first
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {
        // reset in case not all used
        titles[i][0] = values[i][0] = '\0';
        for (int j = 0; j < SPCWX_N; j++) {
            if (space_wx[j].rank == i) {

                // set title i for param j, using common error else per-param value

                strcpy (titles[i], space_wx[j].name);
                if (!space_wx[j].value_ok) {
                    strcpy (values[i], err);
                    colors[i] = RA8875_RED;
                } else {

                    switch ((SPCWX_t)j) {

                    case SPCWX_SSN:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_SSN].value);
                        colors[i] = SSN_COLOR;
                        break;

                    case SPCWX_XRAY:
                        xrayLevel (values[i], space_wx[SPCWX_XRAY]);
                        colors[i] = RGB565(255,134,0);      // XRAY_LCOLOR is too alarming
                        break;

                    case SPCWX_FLUX:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_FLUX].value);
                        colors[i] = SFLUX_COLOR;
                        break;

                    case SPCWX_KP:
                        snprintf (values[i], sizeof(values[i]), "%.1f", space_wx[SPCWX_KP].value);
                        colors[i] = KP_COLOR;
                        break;

                    case SPCWX_SOLWIND:
                        snprintf (values[i], sizeof(values[i]), "%.1f", space_wx[SPCWX_SOLWIND].value);
                        colors[i] = SWIND_COLOR;
                        break;

                    case SPCWX_DRAP:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_DRAP].value);
                        colors[i] = DRAPPLOT_COLOR;
                        break;

                    case SPCWX_BZ:
                        snprintf (values[i], sizeof(values[i]), "%.1f", space_wx[SPCWX_BZ].value);
                        colors[i] = BZBT_BZCOLOR;
                        break;

                    case SPCWX_NOAASPW:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_NOAASPW].value);
                        colors[i] = NOAASPW_COLOR;
                        break;

                    case SPCWX_AURORA:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_AURORA].value);
                        colors[i] = AURORA_COLOR;
                        break;

                    case SPCWX_DST:
                        snprintf (values[i], sizeof(values[i]), "%.0f", space_wx[SPCWX_DST].value);
                        colors[i] = DST_COLOR;
                        break;

                    case SPCWX_N:
                        break;              // lint

                    }
                }
            }
        }
    }

    // do it
    drawNCDXFStats (color, titles, values, colors);
}


/* return the next time of routine download.
 */
time_t nextRetrieval (PlotChoice pc, int interval)
{
    time_t next_update = myNow() + interval;
    int nm = millis()/1000 + interval;
    Serial.printf ("%s data now good for %d sec at %d\n", plot_names[pc], interval, nm);
    return (next_update);
}


/* retrieve sun spot and SPCWX_SSN if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveSunSpots (SunSpotData &ssn)
{
    // check cache first
    if (myNow() < ssn_cache.next_update) {
        ssn = ssn_cache;
        return (true);
    }

    // get fresh
    char line[100];
    WiFiClient ss_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_SSN].value_ok = false;
    ssn.data_ok = ssn_cache.data_ok = false;

    Serial.println(ssn_page);
    if (ss_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (ss_client, backend_host, ssn_page);

        // skip response header
        if (!httpSkipHeader (ss_client)) {
            Serial.print ("SSN: header fail\n");
            goto out;
        }

        // transaction successful
        ok = true;

        // read lines into ssn array and build corresponding time value
        int8_t ssn_i;
        for (ssn_i = 0; ssn_i < SSN_NV && getTCPLine (ss_client, line, sizeof(line), NULL); ssn_i++) {
            ssn_cache.x[ssn_i] = 1-SSN_NV + ssn_i;
            ssn_cache.ssn[ssn_i] = atof(line+11);
        }

        updateClocks(false);

        // ok if all received
        if (ssn_i == SSN_NV) {

            // capture latest
            space_wx[SPCWX_SSN].value = ssn_cache.ssn[SSN_NV-1];
            space_wx[SPCWX_SSN].value_ok = true;
            ssn_cache.data_ok = true;
            ssn = ssn_cache;

        } else {

            Serial.printf ("SSN: data short %d / %d\n", ssn_i, SSN_NV);
        }

    } else {

        Serial.print ("SSN: connection failed\n");
    }

out:

    // set next update
    ssn_cache.next_update = ok ? nextRetrieval (PLOT_CH_SSN, SSN_INTERVAL) : nextWiFiRetry(PLOT_CH_SSN);

    // clean up
    updateClocks(false);
    ss_client.stop();
    return (ok);
}


/* return whether new SPCWX_SSN data are ready, even if bad.
 */
static bool checkForNewSunSpots (void)
{
    if (myNow() < ssn_cache.next_update)
        return (false);

    SunSpotData ssn;
    return (retrieveSunSpots (ssn));
}

/* retrieve solar flux and SPCWX_FLUX if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveSolarFlux (SolarFluxData &sf)
{
    // check cache first
    if (myNow() < sf_cache.next_update) {
        sf = sf_cache;
        return (true);
    }

    // get fresh
    char line[120];
    WiFiClient sf_client;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_FLUX].value_ok = false;
    sf.data_ok = sf_cache.data_ok = false;

    Serial.println (sf_page);
    if (sf_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (sf_client, backend_host, sf_page);

        // skip response header
        if (!httpSkipHeader (sf_client)) {
            Serial.print ("SFlux: header fail\n");
            goto out;
        }

        // transaction successful
        ok = true;

        // read lines into flux array and build corresponding time value
        int8_t sf_i;
        for (sf_i = 0; sf_i < SFLUX_NV && getTCPLine (sf_client, line, sizeof(line), NULL); sf_i++) {
            sf_cache.x[sf_i] = (sf_i - (SFLUX_NV-9-1))/3.0F; // 3x(30 days history + 3 days predictions)
            sf_cache.sflux[sf_i] = atof(line);
        }

        updateClocks(false);

        // ok if found all
        if (sf_i == SFLUX_NV) {

            // capture current value (not predictions)
            space_wx[SPCWX_FLUX].value = sf_cache.sflux[SFLUX_NV-10];
            space_wx[SPCWX_FLUX].value_ok = true;
            sf_cache.data_ok = true;
            sf = sf_cache;

        } else {

            Serial.printf ("SFlux: data short: %d / %d\n", sf_i, SFLUX_NV);
        }

    } else {

        Serial.print ("SFlux: connection failed\n");
    }

out:

    // set next update
    sf_cache.next_update = ok ? nextRetrieval (PLOT_CH_FLUX, SFLUX_INTERVAL) : nextWiFiRetry (PLOT_CH_FLUX);

    // clean up
    updateClocks(false);
    sf_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_FLUX data are ready, even if bad.
 */
static bool checkForNewSolarFlux (void)
{
    if (myNow() < sf_cache.next_update)
        return (false);

    SolarFluxData sf;
    return (retrieveSolarFlux (sf));
}


/* retrieve DRAP and SPCWX_DRAP if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveDRAP (DRAPData &drap)
{
    // check cache first
    if (myNow() < drap_cache.next_update) {
        drap = drap_cache;
        return (true);
    }

    // get fresh

    #define _DRAPDATA_MAXMI     (DRAPDATA_NPTS/10)                      // max allowed missing intervals
    #define _DRAP_MINGOODI      (DRAPDATA_NPTS-3600/DRAPDATA_INTERVAL)  // min index with good data

    char line[100];                                                     // text line
    WiFiClient drap_client;                                             // wifi client connection
    bool ok = false;                                                    // set iff all ok

    // want to find any holes in data so init x values to all 0
    memset (drap_cache.x, 0, DRAPDATA_NPTS*sizeof(float));

    // want max in each interval so init y values to all 0
    memset (drap_cache.y, 0, DRAPDATA_NPTS*sizeof(float));

    // mark data as bad until proven otherwise
    space_wx[SPCWX_DRAP].value_ok = false;
    drap.data_ok = drap_cache.data_ok = false;

    Serial.println (drap_page);
    if (drap_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (drap_client, backend_host, drap_page);

        // skip response header
        if (!httpSkipHeader (drap_client)) {
            Serial.print ("DRAP: header short\n");
            goto out;
        }

        // transaction itself is successful
        ok = true;

        // init state
        time_t t_now = myNow();

        // read lines, oldest first
        int n_lines = 0;
        while (getTCPLine (drap_client, line, sizeof(line), NULL)) {
            n_lines++;

            // crack
            long utime;
            float min, max, mean;
            if (sscanf (line, "%ld : %f %f %f", &utime, &min, &max, &mean) != 4) {
                Serial.printf ("DRAP: garbled: %s\n", line);
                goto out;
            }
            // Serial.printf ("DRAP: %ld %g %g %g\n", utime, min, max, mean;

            // find age for this datum, skip if crazy new or too old
            int age = t_now - utime;
            int xi = DRAPDATA_NPTS*(DRAPDATA_PERIOD - age)/DRAPDATA_PERIOD;
            if (xi < 0 || xi >= DRAPDATA_NPTS) {
                // Serial.printf ("DRAP: skipping age %g hrs\n", age/3600.0F);
                continue;
            }
            drap_cache.x[xi] = age/(-3600.0F);                             // seconds to hours ago

            // set in array if larger
            if (max > drap_cache.y[xi]) {
                // if (y[xi] > 0)
                    // Serial.printf ("DRAP: saw xi %d utime %ld age %d again\n", xi, utime, age);
                drap_cache.y[xi] = max;
            }

            // Serial.printf ("DRAP: %3d %6d: %g %g\n", xi, age, x[xi], y[xi]);
        }
        Serial.printf ("DRAP: read %d lines\n", n_lines);

        // look alive
        updateClocks(false);

        // check for missing data
        int n_missing = 0;
        int maxi_good = 0;
        for (int i = 0; i < DRAPDATA_NPTS; i++) {
            if (drap_cache.x[i] == 0) {
                drap_cache.x[i] = (DRAPDATA_PERIOD - i*DRAPDATA_PERIOD/DRAPDATA_NPTS)/-3600.0F;
                if (i > 0)
                    drap_cache.y[i] = drap_cache.y[i-1];                      // fill with previous
                // Serial.printf ("DRAP: filling missing interval %d at age %g hrs to %g\n", i, drap_cache.x[i], drap_cache.y[i]);
                n_missing++;
            } else {
                maxi_good = i;
            }
        }

        // check for too much missing or newest too old
        if (n_missing > _DRAPDATA_MAXMI) {
            Serial.print ("DRAP: data too sparse\n");
            goto out;
        }
        if (maxi_good < _DRAP_MINGOODI) {
            Serial.print ("DRAP: data too old\n");
            goto out;
        }

        // ok! capture current value
        space_wx[SPCWX_DRAP].value = drap_cache.y[DRAPDATA_NPTS-1];
        space_wx[SPCWX_DRAP].value_ok = true;
        drap_cache.data_ok = true;
        drap = drap_cache;

    } else {

        Serial.print ("DRAP: connection failed\n");
    }

out:

    // set next update
    drap_cache.next_update = ok ? nextRetrieval (PLOT_CH_DRAP, DRAPPLOT_INTERVAL)
                                : nextWiFiRetry (PLOT_CH_DRAP);

    // clean up
    updateClocks(false);
    drap_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_DRAP data are ready, even if bad.
 */
bool checkForNewDRAP ()
{
    if (myNow() < drap_cache.next_update)
        return (false);

    DRAPData drap;
    return (retrieveDRAP (drap));
}

/* retrieve Kp and SPCWX_KP if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveKp (KpData &kp)
{
    // check cache first
    if (myNow() < kp_cache.next_update) {
        kp = kp_cache;
        return (true);
    }

    // get fresh
    WiFiClient kp_client;                               // wifi client connection
    int kp_i = 0;                                       // next kp index to use
    char line[100];                                     // text line
    bool ok = false;                                    // set if no network errors

    // mark value as bad until proven otherwise
    space_wx[SPCWX_KP].value_ok = false;
    kp.data_ok = kp_cache.data_ok = false;

    Serial.println(kp_page);
    if (kp_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (kp_client, backend_host, kp_page);

        // skip response header
        if (!httpSkipHeader (kp_client)) {
            Serial.print ("Kp: header short\n");
            goto out;
        }

        // transaction successful even if data is not
        ok = true;

        // read lines into kp array and build x
        const int now_i = KP_NHD*KP_VPD-1;              // last historic is now
        for (kp_i = 0; kp_i < KP_NV && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
            kp_cache.x[kp_i] = (kp_i-now_i)/(float)KP_VPD;
            float p = atof(line);
            if (p < 0 || p > 9)                         // guard missing-data sentinels (eg unpublished
                p = kp_i > 0 ? kp_cache.p[kp_i-1] : 0;   // future predictions); repeat prior value
            kp_cache.p[kp_i] = p;
        }

        // record sw
        if (kp_i == KP_NV) {

            // save current (not last!) value
            space_wx[SPCWX_KP].value = kp_cache.p[now_i];
            space_wx[SPCWX_KP].value_ok = true;
            kp_cache.data_ok = true;
            kp = kp_cache;

        } else {

            Serial.printf ("Kp: data short: %d of %d\n", kp_i, KP_NV);
        }

    } else {

        Serial.print ("Kp: connection failed\n");
    }

out:

    // set next update
    kp_cache.next_update = ok ? nextRetrieval (PLOT_CH_KP, KP_INTERVAL) : nextWiFiRetry(PLOT_CH_KP);

    // clean up
    updateClocks(false);
    kp_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_KP data are ready, even if bad.
 */
static bool checkForNewKp (void)
{
    if (myNow() < kp_cache.next_update)
        return (false);

    KpData kp;
    return (retrieveKp (kp));
}

/* retrieve DST and SPCWX_DST if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveDST (DSTData &dst)
{
    // check cache first
    if (myNow() < dst_cache.next_update) {
        dst = dst_cache;
        return (true);
    }

    // get fresh
    WiFiClient dst_client;                              // wifi client connection
    char line[100];                                     // text line
    bool ok = false;                                    // set if no network errors

    // mark value as bad until proven otherwise
    space_wx[SPCWX_DST].value_ok = false;
    dst.data_ok = dst_cache.data_ok = false;

    Serial.println(dst_page);
    if (dst_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (dst_client, backend_host, dst_page);

        // skip response header
        if (!httpSkipHeader (dst_client)) {
            Serial.print ("DST: header short\n");
            goto out;
        }

        // transaction successful even if data is not
        ok = true;

        // handy now for finding age
        time_t now = myNow();

        // read lines into DST array and build cache
        // 2025-04-15T00:00:00 -25
        int dst_i = 0;
        for (dst_i = 0; dst_i < DST_NV && getTCPLine (dst_client, line, sizeof(line), NULL); dst_i++) {

            // determine age as hours ago
            time_t val_time = crackISO8601 (line);
            if (val_time == 0) {
                Serial.printf ("DST: bogus line: %s\n", line);
                break;
            }
            float age_hrs = (val_time - now)/3600.0F;
            if (age_hrs > DST_MAXAGE) {
                Serial.printf ("DST: too old: %s\n", line);
                break;
            }

            dst_cache.age_hrs[dst_i] = age_hrs;
            dst_cache.values[dst_i] = atof(line + 19);
        }

        // record sw
        if (dst_i == DST_NV) {

            // save current (not last!) value
            space_wx[SPCWX_DST].value = dst_cache.values[DST_NV-1];
            space_wx[SPCWX_DST].value_ok = true;
            dst_cache.data_ok = true;
            dst = dst_cache;

        } else {

            Serial.printf ("DST: data short: %d of %d\n", dst_i, DST_NV);
        }

    } else {

        Serial.print ("DST: connection failed\n");
    }

out:

    // set next update
    dst_cache.next_update = ok ? nextRetrieval (PLOT_CH_DST, DST_INTERVAL) : nextWiFiRetry(PLOT_CH_DST);

    // clean up
    updateClocks(false);
    dst_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_DST data are ready, even if bad.
 */
static bool checkForNewDST (void)
{
    if (myNow() < dst_cache.next_update)
        return (false);

    DSTData dst;
    return (retrieveDST (dst));
}

/* retrieve XRay and SPCWX_XRAY if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveXRay (XRayData &xray)
{
    // check cache first
    if (myNow() < xray_cache.next_update) {
        xray = xray_cache;
        return (true);
    }

    // get fresh
    WiFiClient xray_client;
    char line[100];
    uint16_t ll;
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_XRAY].value_ok = false;
    xray.data_ok = xray_cache.data_ok = false;

    Serial.println(xray_page);
    if (xray_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (xray_client, backend_host, xray_page);

        // soak up remaining header
        if (!httpSkipHeader (xray_client)) {
            Serial.print ("XRay: header short\n");
            goto out;
        }

        // transaction successful
        ok = true;

        // collect content lines and extract both wavelength intensities
        int xray_i = 0;
        float raw_lxray = 0;
        while (xray_i < XRAY_NV && getTCPLine (xray_client, line, sizeof(line), &ll)) {

            if (line[0] == '2' && ll >= 56) {

                // short
                float s = atof(line+35);
                if (s <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    s = 1e-9;
                xray_cache.s[xray_i] = log10f(s);

                // long
                float l = atof(line+47);
                if (l <= 0)                             // missing values are set to -1.00e+05, also guard 0
                    l = 1e-9;
                xray_cache.l[xray_i] = log10f(l);
                raw_lxray = l;                          // last one will be current

                // time in hours back from 0
                xray_cache.x[xray_i] = (xray_i-XRAY_NV)/6.0;       // 6 entries per hour

                // good
                xray_i++;
            }
        }

        // capture iff we found all
        if (xray_i == XRAY_NV) {

            // capture
            space_wx[SPCWX_XRAY].value = raw_lxray;
            space_wx[SPCWX_XRAY].value_ok = true;
            xray_cache.data_ok = true;
            xray = xray_cache;


        } else {

            Serial.printf ("XRay: data short %d of %d\n", xray_i, XRAY_NV);
        }

    } else {

        Serial.print ("XRay: connection failed\n");
    }

out:

    // set next update
    xray_cache.next_update = ok ? nextRetrieval (PLOT_CH_XRAY, XRAY_INTERVAL) : nextWiFiRetry(PLOT_CH_XRAY);

    // clean up
    updateClocks(false);
    xray_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_XRAY data are ready, even if bad.
 */
static bool checkForNewXRay (void)
{
    if (myNow() < xray_cache.next_update)
        return (false);

    XRayData xray;
    return (retrieveXRay (xray));
}

/* retrieve solar proton flux (>= 10 MeV, GOES integral, pfu) history, else use cache.
 * N.B. expected server format is PROTON_NV lines, oldest first, each containing a single
 *      decimal pfu value (not logged) at the same cadence/window as xray_page.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveProtonFlux (ProtonData &proton)
{
    // check cache first
    if (myNow() < proton_cache.next_update) {
        proton = proton_cache;
        return (true);
    }

    // get fresh
    WiFiClient proton_client;
    char line[100];
    bool ok = false;

    proton_cache.data_ok = false;

    Serial.println(proton_page);
    if (proton_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (proton_client, backend_host, proton_page);

        // skip response header
        if (!httpSkipHeader (proton_client)) {
            Serial.print ("Proton: header short\n");
            goto out;
        }

        // transaction successful even if data is not
        ok = true;

        // read lines into proton array, same age scale as XRayData.
        // N.B. reject the whole response the instant a line doesn't parse as a plain
        //      leading number -- eg an HTML error/redirect page from a missing server
        //      page -- rather than silently treating it as a low reading. This is what
        //      makes an absent /proton/protons.txt page show "No data" instead of a
        //      bogus rising line.
        int proton_i;
        for (proton_i = 0; proton_i < PROTON_NV; proton_i++) {
            if (!getTCPLine (proton_client, line, sizeof(line), NULL))
                break;
            char *endptr;
            float pfu = strtof (line, &endptr);
            if (endptr == line) {
                Serial.printf ("Proton: bad line: %s\n", line);
                break;
            }
            if (pfu <= 0)                               // guard log10(0) for legit tiny/zero readings
                pfu = 1e-3;
            proton_cache.p[proton_i] = log10f(pfu);
            proton_cache.x[proton_i] = (proton_i-PROTON_NV)/6.0;      // 6 entries per hour, matches xray
        }

        // capture iff we found all
        if (proton_i == PROTON_NV) {
            proton_cache.data_ok = true;
            proton = proton_cache;
        } else {
            Serial.printf ("Proton: data short %d of %d\n", proton_i, PROTON_NV);
        }

    } else {

        Serial.print ("Proton: connection failed\n");
    }

out:

    // set next update
    proton_cache.next_update = ok ? nextRetrieval (PLOT_CH_NOAASPW, PROTON_INTERVAL)
                                   : nextWiFiRetry (PLOT_CH_NOAASPW);

    // clean up
    updateClocks(false);
    proton_client.stop();
    return (ok);
}

/* retrieve BzBt data and SPCWX_BZBT if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveBzBt (BzBtData &bzbt)
{
    // check cache first
    if (myNow() < bzbt_cache.next_update) {
        bzbt = bzbt_cache;
        return (true);
    }

    // get fresh
    int bzbt_i;                                     // next index to use
    WiFiClient bzbt_client;
    char line[100];
    bool ok = false;
    time_t t0 = myNow();

    // mark data as bad until proven otherwise
    space_wx[SPCWX_BZ].value_ok = false;
    bzbt.data_ok = bzbt_cache.data_ok = false;

    Serial.println(bzbt_page);
    if (bzbt_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (bzbt_client, backend_host, bzbt_page);

        // skip over remaining header
        if (!httpSkipHeader (bzbt_client)) {
            Serial.print ("BZBT: header short\n");
            goto out;
        }

        // transaction successful
        ok = true;

        // collect content lines and extract both magnetic values, oldest first (newest last :-)
        // # UNIX        Bx     By     Bz     Bt
        // 1684087500    1.0   -2.7   -3.2    4.3
        bzbt_i = 0;
        while (bzbt_i < BZBT_NV && getTCPLine (bzbt_client, line, sizeof(line), NULL)) {

            // crack
            // Serial.printf("BZBT: %d %s\n", bzbt_i, line);
            long unix;
            float this_bz, this_bt;
            if (sscanf (line, "%ld %*f %*f %f %f", &unix, &this_bz, &this_bt) != 3) {
                // Serial.printf ("BZBT: rejecting %s\n", line);
                continue;
            }

            // store at bzbt_i
            bzbt_cache.bz[bzbt_i] = this_bz;
            bzbt_cache.bt[bzbt_i] = this_bt;

            // time in hours back from now but clamp at 0 in case we are slightly late
            bzbt_cache.x[bzbt_i] = unix < t0 ? (unix - t0)/3600.0 : 0;

            // n read
            bzbt_i++;
        }

        // proceed iff we found all and current
        if (bzbt_i == BZBT_NV && bzbt_cache.x[BZBT_NV-1] > -0.25F) {

            // capture latest
            space_wx[SPCWX_BZ].value = bzbt_cache.bz[BZBT_NV-1];
            space_wx[SPCWX_BZ].value_ok = true;
            bzbt_cache.data_ok = true;
            bzbt = bzbt_cache;

        } else {

            if (bzbt_i < BZBT_NV)
                Serial.printf ("BZBT: data short %d of %d\n", bzbt_i, BZBT_NV);
            else
                Serial.printf ("BZBT: data %g hrs old\n", -bzbt_cache.x[BZBT_NV-1]);
        }

    } else {

        Serial.print ("BZBT: connection failed\n");
    }

out:

    // set next update
    bzbt_cache.next_update = ok ? nextRetrieval (PLOT_CH_BZBT, BZBT_INTERVAL) : nextWiFiRetry(PLOT_CH_BZBT);

    // clean up
    updateClocks(false);
    bzbt_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_BZBT data are ready, even if bad.
 */
static bool checkForNewBzBt(void)
{
    if (myNow() < bzbt_cache.next_update)
        return (false);
    BzBtData bzbt;
    return (retrieveBzBt (bzbt));
}


/* retrieve solar wind and SPCWX_SOLWIND if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveSolarWind(SolarWindData &sw)
{
    // check cache first
    if (myNow() < sw_cache.next_update) {
        sw = sw_cache;
        return (true);
    }

    // get fresh
    WiFiClient swind_client;
    char line[80];
    bool ok = false;

    // mark value as bad until proven otherwise
    space_wx[SPCWX_SOLWIND].value_ok = false;
    sw.data_ok = sw_cache.data_ok = false;

    Serial.println (swind_page);
    if (swind_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (swind_client, backend_host, swind_page);

        // skip response header
        if (!httpSkipHeader (swind_client)) {
            Serial.println ("SolWind: header short");
            goto out;
        }

        // transaction successful
        ok = true;

        // read lines into wind array and build corresponding x/y values
        time_t t0 = myNow();
        time_t start_t = t0 - SWIND_PER;
        time_t prev_unixs = 0;
        float max_y = 0;
        for (sw_cache.n_values = 0; sw_cache.n_values < SWIND_MAXN
                                                && getTCPLine (swind_client, line, sizeof(line), NULL); ) {
            // Serial.printf ("SolWind: %3d: %s\n", nsw, line);
            long unixs;         // unix seconds
            float density;      // /cm^2
            float speed;        // km/s
            if (sscanf (line, "%ld %f %f", &unixs, &density, &speed) != 3) {
                Serial.println ("SolWind: data garbled");
                goto out;
            }

            // want y axis to be 10^12 /s /m^2
            float this_y = density * speed * 1e-3;

            // capture largest value in this period
            if (this_y > max_y)
                max_y = this_y;

            // skip until find within period and new interval or always included last
            if ((unixs < start_t || unixs - prev_unixs < SWIND_DT) && sw_cache.n_values != SWIND_MAXN-1)
                continue;
            prev_unixs = unixs;

            // want x axis to be hours back from now
            sw_cache.x[sw_cache.n_values] = (t0 - unixs)/(-3600.0F);
            sw_cache.y[sw_cache.n_values] = max_y;
            // Serial.printf ("SolWind: %3d %5.2f %5.2f\n", nsw, x[nsw], y[nsw]);

            // good one
            max_y = 0;
            sw_cache.n_values++;
        }

        updateClocks(false);

        // good iff found enough
        if (sw_cache.n_values >= SWIND_MINN) {

            // capture latest
            space_wx[SPCWX_SOLWIND].value = sw_cache.y[sw_cache.n_values-1];
            space_wx[SPCWX_SOLWIND].value_ok = true;
            sw_cache.data_ok = true;
            sw = sw_cache;

        } else {
            Serial.println ("SolWind:: data error");
        }

    } else {

        Serial.println ("SolWind: connection failed");
    }

out:

    // set next update
    sw_cache.next_update = ok ? nextRetrieval (PLOT_CH_SOLWIND, SWIND_INTERVAL)
                              : nextWiFiRetry(PLOT_CH_SOLWIND);

    // clean up
    updateClocks(false);
    swind_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_SOLWIND data are ready, even if bad.
 */
static bool checkForNewSolarWind (void)
{
    if (myNow() < sw_cache.next_update)
        return (false);

    SolarWindData sw;
    return (retrieveSolarWind (sw));
}

/* retrieve NOAA space weather indices and SPCWX_NOAASPW if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveNOAASWx (NOAASpaceWxData &noaasw)
{
    // check cache first
    if (myNow() < noaasw_cache.next_update) {
        noaasw = noaasw_cache;
        return (true);
    }

    // expecting 3 reply lines of the following form, anything else is an error message
    //  R  0 0 0 0
    //  S  0 0 0 0
    //  G  0 0 0 0

    // get fresh
    WiFiClient noaaswx_client;
    bool ok = false;

    // mark data as bad until proven otherwise
    space_wx[SPCWX_NOAASPW].value_ok = false;
    noaasw.data_ok = noaasw_cache.data_ok = false;

    // read scales
    Serial.println(noaaswx_page);
    char line[100];
    if (noaaswx_client.connect(backend_host, backend_port)) {

        updateClocks(false);

        // fetch page
        httpHCGET (noaaswx_client, backend_host, noaaswx_page);

        // skip header then read the data lines
        if (httpSkipHeader (noaaswx_client)) {

            // transaction successful
            ok = true;

            // find max
            int noaasw_max = 0;

            // for each of N_NOAASW_C categories
            for (int i = 0; i < N_NOAASW_C; i++) {

                // read next line
                if (!getTCPLine (noaaswx_client, line, sizeof(line), NULL)) {
                    Serial.println ("NOAASW: missing data");
                    goto out;
                }
                // Serial.printf ("NOAA: %d %s\n", i, line);

                // category in first char must match
                if (noaasw_cache.cat[i] != line[0]) {
                    Serial.printf ("NOAASW: invalid class: %s\n", line);
                    goto out;
                }

                // for each of N_NOAASW_V values
                char *lp = line+1;
                for (int j = 0; j < N_NOAASW_V; j++) {

                    // convert next int
                    char *endptr;
                    noaasw_cache.val[i][j] = strtol (lp, &endptr, 10);
                    if (lp == endptr) {
                        Serial.printf ("NOAASW: invalid line: %s\n", line);
                        goto out;
                    }
                    lp = endptr;

                    // find max
                    if (noaasw_cache.val[i][j] > noaasw_max)
                        noaasw_max = noaasw_cache.val[i][j];
                }

            }

            // values ok
            space_wx[SPCWX_NOAASPW].value = noaasw_max;
            space_wx[SPCWX_NOAASPW].value_ok = true;
            noaasw_cache.data_ok = true;
            noaasw = noaasw_cache;

        } else {
            Serial.println ("NOAASW: header short");
            goto out;
        }

    } else {

        Serial.println ("NOAASW: connection failed");
    }

out:

    // set next update
    noaasw_cache.next_update = ok ? nextRetrieval (PLOT_CH_NOAASPW, NOAASPW_INTERVAL)
                                  : nextWiFiRetry(PLOT_CH_NOAASPW);

    // clean up
    updateClocks(false);
    noaaswx_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_NOAASPW data are ready, even if bad.
 */
static bool checkForNewNOAASWx (void)
{
    if (myNow() < noaasw_cache.next_update)
        return (false);
    NOAASpaceWxData noaasw;
    return (retrieveNOAASWx(noaasw));
}


/* retrieve aurora and SPCWX_AURORA if it's time, else use cache.
 * return whether transaction was ok (even if data was not)
 */
bool retrieveAurora (AuroraData &aurora)
{
    // check cache first
    if (myNow() < aurora_cache.next_update) {
        aurora = aurora_cache;
        return (true);
    }

    // get fresh
    WiFiClient aurora_client;                                           // wifi client connection
    char line[100];                                                     // text line
    bool ok = false;                                                    // set iff all ok

    // mark data as bad until proven otherwise
    space_wx[SPCWX_AURORA].value_ok = false;
    aurora.data_ok = aurora_cache.data_ok = false;

    Serial.println (aurora_page);
    if (aurora_client.connect(backend_host, backend_port)) {
        updateClocks(false);

        // query web page
        httpHCGET (aurora_client, backend_host, aurora_page);

        // skip response header
        if (!httpSkipHeader (aurora_client)) {
            Serial.print ("AURORA: header short\n");
            goto out;
        }

        // transaction itself is successful
        ok = true;

        // init state
        time_t t_now = myNow();
        float prev_age = 1e10;
        aurora_cache.n_points = 0;

        // read lines keep up to AURORA_NPTS newest
        while (getTCPLine (aurora_client, line, sizeof(line), NULL)) {

            // crack
            long utime;
            float percent;
            if (sscanf (line, "%ld %f", &utime, &percent) != 2) {
                Serial.printf ("AURORA: garbled: %s\n", line);
                goto out;
            }
            // Serial.printf ("AURORA: %ld %g\n", utime, percent

            // find age for this datum, skip if crazy new or too old or out of order
            float age = (t_now - utime)/3600.0F;        // seconds to hours
            if (age < 0 || age > AURORA_MAXAGE || age >= prev_age) {
                Serial.printf ("AURORA: skipping age %g hrs\n", age);
                continue;
            }
            prev_age = age;

            // add to list, shift out oldest if full
            if (aurora_cache.n_points == AURORA_MAXPTS) {
                memmove (&aurora_cache.age_hrs[0], &aurora_cache.age_hrs[1], (AURORA_MAXPTS-1)*sizeof(float));
                memmove (&aurora_cache.percent[0], &aurora_cache.percent[1], (AURORA_MAXPTS-1)*sizeof(float));
                aurora_cache.n_points = AURORA_MAXPTS - 1;
            }
            aurora_cache.age_hrs[aurora_cache.n_points] = -age;               // want "ago"
            aurora_cache.percent[aurora_cache.n_points] = percent;
            aurora_cache.n_points++;
        }

        // look alive
        updateClocks(false);

        // require at least a few recent
        if (aurora_cache.n_points < 5) {
            Serial.printf ("AURORA: only %d points\n", aurora_cache.n_points);
        } else if (aurora_cache.age_hrs[aurora_cache.n_points-1] <= -1.0F) {
            Serial.printf ("AURORA: newest is too old: %g hrs\n",
                                -aurora_cache.age_hrs[aurora_cache.n_points-1]);
        } else {

            // good
            Serial.printf ("AURORA: found %d points [%g,%g] hrs old\n", aurora_cache.n_points,
                    -aurora_cache.age_hrs[0], -aurora_cache.age_hrs[aurora_cache.n_points-1]);

            // capture newest value for space wx
            space_wx[SPCWX_AURORA].value = aurora_cache.percent[aurora_cache.n_points-1];
            space_wx[SPCWX_AURORA].value_ok = true;
            aurora_cache.data_ok = true;
            aurora = aurora_cache;
        }

    } else {

        Serial.print ("AURORA: connection failed\n");
    }

out:

    // set next update
    aurora_cache.next_update = ok ? nextRetrieval (PLOT_CH_AURORA, AURORA_INTERVAL)
                                  : nextWiFiRetry(PLOT_CH_AURORA);

    // clean up
    updateClocks(false);
    aurora_client.stop();
    return (ok);
}

/* return whether fresh SPCWX_AURORA data are ready, even if bad.
 */
bool checkForNewAurora ()
{
    if (myNow() < aurora_cache.next_update)
        return (false);
    AuroraData a;
    return (retrieveAurora(a));
}

/* update all space_wx stats but no faster than their respective panes would do.
 * return whether any actually updated.
 */
bool checkForNewSpaceWx()
{
    // check each
    bool sf = checkForNewSolarFlux();
    bool kp = checkForNewKp();
    bool ds = checkForNewDST();
    bool xr = checkForNewXRay();
    bool bz = checkForNewBzBt();
    bool dr = checkForNewDRAP();
    bool sw = checkForNewSolarWind();
    bool ss = checkForNewSunSpots();
    bool na = checkForNewNOAASWx();
    bool au = checkForNewAurora();

    // check whether any changed
    bool any_new = sf || ds || kp || xr || bz || dr || sw || ss || na || au;

    // if so redo ranking unless Auto
    if (any_new && spcwx_chmask == SPCWX_AUTO)
        sortSpaceWx();

    return (any_new);
}

/* one-time setup
 */
void initSpaceWX(void)
{
    // init all space_wx m and b
    bool mb_ok = initSWFit();
    if (!mb_ok)
        Serial.println ("RANKSW: no ranking available -- using default");

    // init user selection ranking
    if (!NVReadUInt32 (NV_SPCWXCHOICE, &spcwx_chmask)) {
        spcwx_chmask = SPCWX_AUTO;
        NVWriteUInt32 (NV_SPCWXCHOICE, spcwx_chmask);
    }
    Serial.printf ("SPCWX: initial choice mask 0x%08x\n", spcwx_chmask);

    // unless auto, set rank to match choice mask
    if (spcwx_chmask != SPCWX_AUTO) {
        // N.B. assign ranks in same order as runNCDXFSpcWxMenu()
        int rank = 0;
        for (int i = 0; i < SPCWX_N; i++)
            space_wx[i].rank = (spcwx_chmask & (1<<i)) ? rank++ : 99;   // 0 is highest rank
    }
}
