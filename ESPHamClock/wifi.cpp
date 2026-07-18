/* manage most wifi uses, including pane and map updates.
 */

#include "HamClock.h"

#define DEFAULT_HOST "clearskyinstitute.com"
// host name and port of backend server
const char *backend_host = DEFAULT_HOST;
int backend_port = 80;
// host name of software server
const char *software_host = DEFAULT_HOST;
// IP where server thinks we came from
char remote_addr[16];                           // INET_ADDRSTRLEN

// user's date and time, UNIX only
time_t usr_datetime;

// ADIF pane
#define ADIF_INTERVAL   2                       // polling interval, secs

// DX Cluster update
#define DXC_INTERVAL    1                       // update interval, secs

// env sensor
#define ENV_INTERVAL    20                      // update interval, secs

// SDO own rotation period when panes are not rotating
#define SDO_INTERVAL    60                      // update interval, secs


// band conditions and voacap map, models change each hour
uint16_t bc_powers[] = {1, 5, 10, 50, 100, 500, 1000};
const int n_bc_powers = NARRAY(bc_powers);
static const char bc_page[] = "/fetchBandConditions.pl";
static time_t bc_time;                          // nowWO() when bc_matrix was loaded
BandCdtnMatrix bc_matrix;                       // percentage reliability for each band
uint16_t bc_power;                              // VOACAP power setting
float bc_toa;                                   // VOACAP take off angle
uint8_t bc_utc_tl;                              // label band conditions timeline in utc else DE local

uint8_t bc_modevalue;                           // VOACAP sensitivity value
// N.B. these must match the tables in fetchBandConditions.pl etc
const BCModeSetting bc_modes[N_BCMODES] {
    {"CW",   19},
    {"RTTY", 22},
    {"SSB",  38},
    {"AM",   49},
    {"WSPR",  3},
    {"FT8",  13},
    {"FT4",  17}
};
uint8_t findBCModeValue (const char *name)      // find value give name, else 0
{
    for (int i = 0; i < N_BCMODES; i++)
        if (strcmp (name, bc_modes[i].name) == 0)
            return (bc_modes[i].value);
    return (0);
}
const char *findBCModeName (uint8_t value)      // find name given value, else NULL
{
    for (int i = 0; i < N_BCMODES; i++)
        if (bc_modes[i].value == value)
            return (bc_modes[i].name);
    return (NULL);
}

// geolocation web page
static const char locip_page[] = "/fetchIPGeoloc.pl";


// moon display
#define MOON_INTERVAL   50                      // annotation update interval, secs


// list of default NTP servers unless user has set their own
static NTPServer ntp_list[] = {                 // init times to 0 insures all get tried initially
    {"time.google.com",     NTP_TOO_LONG},
    {"time.apple.com",      NTP_TOO_LONG},
    {"pool.ntp.org",        NTP_TOO_LONG},
    {"europe.pool.ntp.org", NTP_TOO_LONG},
    {"asia.pool.ntp.org",   NTP_TOO_LONG},
    {"time.nist.gov",       NTP_TOO_LONG},
};
#define N_NTP NARRAY(ntp_list)                  // number of possible servers


// web site retry interval and max, secs
#define WIFI_RETRY      (15)
#define WIFI_MAXRETRY   (5*60)

/* "reverting" refers to restoring PANE_1 after temporarily forced to show DE or DX weather.
 */
static time_t revert_time;                      // when to resume normal pane operation
static PlotPane revert_pane;                    // which pane is being temporarily reverted

/* rotation time control.
 * 0 will refresh immediately.
 * reset all in initWiFiRetry()
 */
static time_t next_rotation[PANE_N];            // next pane rotation
time_t next_update[PANE_N];                     // next function call
static time_t next_map;                         // next map check
static time_t map_time;                         // nowWO() when map was loaded
static bool fresh_redraw[PLOT_CH_N];            // whether full pane redraw is required


/* return absolute difference in two time_t regardless of time_t implementation is signed or unsigned.
 */
static time_t tdiff (const time_t t1, const time_t t2)
{
    if (t1 > t2)
        return (t1 - t2);
    if (t2 > t1)
        return (t2 - t1);
    return (0);
}

/* return the next retry time_t.
 * retries are spaced out every WIFI_RETRY but never more than WIFI_MAXRETRY
 */
static time_t nextWiFiRetry (void)
{
    // set and save next retry time
    static time_t prev_try;
    time_t now = myNow();
    time_t next_t0 = now + WIFI_RETRY;                          // basic interval after now
    time_t next_try = prev_try + WIFI_RETRY;                    // interval extension after prev
    if (next_try > now + WIFI_MAXRETRY)
        next_try = now + WIFI_MAXRETRY;                         // but clamp to WIFI_MAXRETRY
    prev_try = next_try > next_t0 ? next_try : next_t0;         // use whichever is later
    return (prev_try);
}

/* calls nextWiFiRetry() and logs the given string
 */
time_t nextWiFiRetry (const char *str)
{
    time_t next_try = nextWiFiRetry();
    int dt = next_try - myNow();
    int nm = millis()/1000+dt;
    Serial.printf ("Next %s retry in %d sec at %d\n", str, dt, nm);
    return (next_try);
}

/* calls nextWiFiRetry() and logs the given plot choice.
 */
time_t nextWiFiRetry (PlotChoice pc)
{
    return (nextWiFiRetry (plot_names[pc]));
}

// ---------------------------------------------------------------------------
// VOACAP-specific exponential-backoff-with-jitter and per-fleet click throttle.
// Independent of nextWiFiRetry() so other backends (DEWX, MUF-RT, RSS, ...)
// keep their existing behavior. Reset on success via resetVOACAPRetry().
// ---------------------------------------------------------------------------

static time_t voacap_backoff = VOACAP_RETRY_BASE;       // current delay, secs
static time_t last_voacap_attempt;                      // wall time of last fetch attempt

bool isVOACAPMap (CoreMaps cm)
{
    return (cm == CM_MUF_V || cm == CM_PMTOA || cm == CM_PMREL);
}

bool voacapThrottled (time_t now)
{
    // only throttle while in an active failure-backoff cycle.
    // After a successful fetch, voacap_backoff is reset to VOACAP_RETRY_BASE,
    // so clicks pass through normally until the next failure.
    if (voacap_backoff <= VOACAP_RETRY_BASE)
        return false;
    return (last_voacap_attempt > 0
         && (now - last_voacap_attempt) < VOACAP_MIN_INTERVAL);
}

void noteVOACAPAttempt (time_t now)
{
    last_voacap_attempt = now;
}

time_t lastVOACAPAttempt (void)
{
    return last_voacap_attempt;
}

void resetVOACAPRetry (void)
{
    if (voacap_backoff != VOACAP_RETRY_BASE)
        Serial.printf ("VOACAP: backoff reset (was %ld s, base %d s)\n",
                       (long)voacap_backoff, VOACAP_RETRY_BASE);
    voacap_backoff = VOACAP_RETRY_BASE;
}

/* schedule the next VOACAP retry after a failed fetch.
 * Doubles backoff up to VOACAP_RETRY_MAX, with +/-25% jitter to avoid
 * a synchronized fleet hammering the backend. Logs each scheduling.
 */
time_t nextVOACAPRetry (const char *str)
{
    time_t now = myNow();

    // +/-25% jitter so a herd of HamClocks doesn't sync up
    long jitter = (long)voacap_backoff / 4;
    long delay  = (long)voacap_backoff + (random(2*jitter+1) - jitter);
    if (delay < VOACAP_RETRY_BASE)
        delay = VOACAP_RETRY_BASE;

    time_t next_try = now + delay;

    Serial.printf ("VOACAP: %s failed; backing off %ld s (cur=%ld, max=%d), next at +%ld\n",
                   str, delay, (long)voacap_backoff, VOACAP_RETRY_MAX, delay);

    // double for next time, cap at max
    voacap_backoff *= VOACAP_RETRY_MULT;
    if (voacap_backoff > VOACAP_RETRY_MAX)
        voacap_backoff = VOACAP_RETRY_MAX;

    return (next_try);
}

/* given a plot choice return time of its next update.
 * if choice is in play and rotating use pane rotation time else the given interval.
 */
static time_t nextPaneUpdate (PlotChoice pc, int interval)
{
    PlotPane pp = findPaneForChoice (pc);
    if (pp == PANE_NONE)
        fatalError ("nextPaneUpdate %s PANE_NONE", plot_names[pc]);

    time_t t0 = myNow();
    time_t next = t0 + interval;
    int dt = next - t0;
    int at = millis()/1000+dt;

    if (interval > 60)
        Serial.printf ("Pane %d now showing %s updates in %d sec at %d\n", pp, plot_names[pc], dt, at);

    return (next);
}

/* return time of next rotation for pane pp or brb_next_update if pp == PANE_NONE.
 */
time_t nextRotation (PlotPane pp)
{
    // find latest of all currently scheduled rotations
    time_t latest_rot = 0;
    for (int i = 0; i < PANE_N; i++)
        if ((isPaneRotating(pp) || isSpecialPaneRotating(pp)) && next_rotation[i] > latest_rot)
            latest_rot = next_rotation[i];
    if (BRBIsRotating() && brb_next_update > latest_rot)
        latest_rot = brb_next_update;

    // bring up to present if all old
    time_t t0 = myNow();
    if (latest_rot < t0)
        latest_rot = t0;

    // next rotation follows
    time_t next_rot;
    if (pp == PANE_NONE) {
        next_rot = brb_next_update = latest_rot + ROTATION_INTERVAL;
        int dt = next_rot - t0;
        if (dt > 5) {                                   // reduce mog noise when rotating quickly
            int at = millis()/1000+dt;
            if (BRBIsRotating())
                Serial.printf ("BRB: next rotation in %d sec at %d\n", dt, at);
            else
                Serial.printf ("BRB: next update in %d sec at %d\n", dt, at);
        }
    } else {
        next_rot = next_rotation[pp] = latest_rot + ROTATION_INTERVAL;
        int dt = next_rot - t0;
        if (dt > 5) {                                   // reduce mog noise when rotating quickly
            int at = millis()/1000+dt;
            Serial.printf ("Pane %d now %s next rotation in %d sec at %d\n", pp, plot_names[plot_ch[pp]],
                                                        dt, at);
        }
    }

    return (next_rot);
}


/* set de_ll.lat_d and de_ll.lng_d from the given ip else our public ip.
 * report status via tftMsg
 */
static void geolocateIP (const char *ip)
{
    WiFiClient iploc_client;                            // wifi client connection
    float lat, lng;
    char llline[80];
    char ipline[80];
    char credline[80];
    int nlines = 0;

    if (iploc_client.connect(backend_host, backend_port)) {

        // create proper query
        strcpy (llline, locip_page);
        size_t l = sizeof(locip_page) - 1;              // not EOS
        if (ip)                                         // else locip_page will use client IP
            l += snprintf (llline+l, sizeof(llline)-l, "?IP=%s", ip);
        Serial.println(llline);

        // send
        httpHCGET (iploc_client, backend_host, llline);
        if (!httpSkipHeader (iploc_client)) {
            Serial.println ("geoIP header short");
            goto out;
        }

        // expect 4 lines: LAT=, LNG=, IP= and CREDIT=, anything else first line is error message
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lat = atof (llline+4);
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lng = atof (llline+4);
        if (!getTCPLine (iploc_client, ipline, sizeof(ipline), NULL))
            goto out;
        nlines++;
        if (!getTCPLine (iploc_client, credline, sizeof(credline), NULL))
            goto out;
        nlines++;
    }

out:

    if (nlines == 4) {
        // ok

        tftMsg (true, 0, "IP %s geolocation", ipline+3);
        tftMsg (true, 0, "  by %s", credline+7);
        tftMsg (true, 0, "  %.2f%c %.2f%c", fabsf(lat), lat < 0 ? 'S' : 'N',
                                fabsf(lng), lng < 0 ? 'W' : 'E');

        de_ll.lat_d = lat;
        de_ll.lng_d = lng;
        de_ll.normalize();
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
        setNVMaidenhead (NV_DE_GRID, de_ll);
        setTZAuto (de_tz);
        NVWriteTZ (NV_DE_TZ, de_tz);


    } else {
        // trouble, error message if 1 line

        if (nlines == 1) {
            tftMsg (true, 0, "IP geolocation err:");
            tftMsg (true, 1000, "  %s", llline);
        } else
            tftMsg (true, 1000, "IP geolocation failed");
    }

    iploc_client.stop();
}

/* return best ntp server.
 * if user has set their own then always return that one;
 * else search ntp_list for the fastest so far, if all look bad then just roll through.
 * N.B. never return NULL
 */
NTPServer *findBestNTP()
{
    static NTPServer user_server;
    static uint8_t prev_fixed;

    // user's first
    if (useLocalNTPHost()) {
        user_server.server = getLocalNTPHost();
        return (&user_server);
    }

    // else look through ntp_list
    NTPServer *best_ntp = &ntp_list[0];
    int rsp_min = ntp_list[0].rsp_time;

    for (int i = 1; i < N_NTP; i++) {
        NTPServer *np = &ntp_list[i];
        if (np->rsp_time < rsp_min) {
            best_ntp = np;
            rsp_min = np->rsp_time;
        }
    }

    // if nothing good there just roll through
    if (rsp_min == NTP_TOO_LONG) {
        prev_fixed = (prev_fixed+1) % N_NTP;
        best_ntp = &ntp_list[prev_fixed];
    }

    return (best_ntp);
}


/* init and connect, inform via tftMsg() if verbose.
 * non-verbose is used for automatic retries that should not clobber the display.
 */
static void initWiFi (bool verbose)
{
    // N.B. look at the usages and make sure this is "big enough"
    static const char dots[] = ".........................................";

    // probable mac when only localhost -- used to detect LAN but no WLAN
    const char *mac_lh = "FF:FF:FF:FF:FF:FF";


    // begin
    // N.B. ESP seems to reconnect much faster if avoid begin() unless creds change
    // N.B. non-RPi UNIX systems return NULL from getWiFI*()
    WiFi.mode(WIFI_STA);
    const char *myssid = getWiFiSSID();
    const char *mypw = getWiFiPW();
    if (myssid && mypw && (strcmp (WiFi.SSID().c_str(), myssid) || strcmp (WiFi.psk().c_str(), mypw)))
        WiFi.begin ((char*)myssid, (char*)mypw);

    // prep
    uint32_t t0 = millis();
    uint32_t timeout = verbose ? 30000UL : 3000UL;      // dont wait nearly as long for a retry, millis
    uint16_t ndots = 0;                                 // progress counter
    char mac[30];
    strcpy (mac, WiFi.macAddress().c_str());
    tftMsg (verbose, 0, "MAC addr: %s", mac);

    // wait for connection
    if (myssid)
        tftMsg (verbose, 0, "\r");                      // init overwrite
    do {
        if (myssid)
            tftMsg (verbose, 0, "Connecting to %s %.*s\r", myssid, ndots, dots);
        Serial.printf ("Trying network %d\n", ndots);
        if (timesUp(&t0,timeout) || ndots == (sizeof(dots)-1)) {
            if (myssid)
                tftMsg (verbose, 1000, "WiFi failed -- signal? credentials?");
            else
                tftMsg (verbose, 1000, "Network connection attempt failed");
            return;
        }

        wdDelay(1000);
        ndots++;

        // WiFi.printDiag(Serial);

    } while (strcmp (mac, mac_lh) && (WiFi.status() != WL_CONNECTED));

    // init retry times
    initWiFiRetry();

    // report stats
    if (WiFi.status() == WL_CONNECTED) {

        // just to get remote_addr
        char line[50];
        (void)newVersionIsAvailable (line, sizeof(line));

        IPAddress ip = WiFi.localIP();
        tftMsg (verbose, 0, "Local IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        tftMsg (verbose, 0, "Public IP: %s", remote_addr);
        ip = WiFi.subnetMask();
        tftMsg (verbose, 0, "Mask: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.gatewayIP();
        tftMsg (verbose, 0, "GW: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.dnsIP();
        tftMsg (verbose, 0, "DNS: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        tftMsg (verbose, 0, "BE: %s:%d", backend_host, backend_port);
        if (strcmp(backend_host,software_host) != 0)
            tftMsg (verbose, 0, "SE: %s", software_host);
        int rssi;
        bool is_dbm;
        if (readWiFiRSSI(rssi,is_dbm)) {
            tftMsg (verbose, 0, "Signal strength: %d %s", rssi, is_dbm ? "dBm" : "%");
            tftMsg (verbose, 0, "Channel: %d", WiFi.channel());
        }

        tftMsg (verbose, 0, "S/N: %u", ESP.getChipId());
    }
}

/* call exactly once to init wifi, maps and maybe time and location.
 * report on initial startup screen with tftMsg.
 */
void initSys()
{
    // "Skip" tooltip text
    static const char skip_ttt[] = "click to proceed immediately to HamClock";


    // insure core_map is defined -- N.B. before initWiFi calls sendUserAgent()
    initCoreMaps();

    // start/check WLAN
    initWiFi(true);

    // start web servers
    initWebServer();
    initLiveWeb(true);

    // init location if desired
    if (useGeoIP() || init_iploc || init_locip) {
        if (WiFi.status() == WL_CONNECTED)
            geolocateIP (init_locip);
        else
            tftMsg (true, 0, "no network for geo IP");

    } else if (useGPSDLoc()) {
        LatLong ll;
        if (getGPSDLatLong(&ll)) {

            // good -- set de_ll
            de_ll = ll;
            de_ll.normalize();
            NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
            NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
            setNVMaidenhead(NV_DE_GRID, de_ll);

            // leave user's tz offset
            // de_tz.tz_secs = getTZ (de_ll);
            // NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

            tftMsg (true, 0, "GPSD: %.2f%c %.2f%c",
                                fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N',
                                fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');

        } else
            tftMsg (true, 1000, "GPSD: no Lat/Long");

    } else if (useNMEALoc()) {
        LatLong ll;
        if (getNMEALatLong(ll)) {

            // good -- set de_ll
            de_ll = ll;
            de_ll.normalize();
            NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
            NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
            setNVMaidenhead(NV_DE_GRID, de_ll);

            // leave user's tz offset
            // de_tz.tz_secs = getTZ (de_ll);
            // NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

            tftMsg (true, 0, "NMEA: %.2f%c %.2f%c",
                                fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N',
                                fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');

        } else
            tftMsg (true, 1000, "NMEA: no Lat/Long");
    }


    // skip box
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox ("Skip", skip_b, false, RA8875_WHITE);
    bool skipped_here = false;


    // init time service as desired
    if (useGPSDTime()) {
        if (getGPSDUTC())
            tftMsg (true, 0, "GPSD: time ok");
        else
            tftMsg (true, 1000, "GPSD: no time");

    } else if (useNMEATime()) {
        if (getNMEAUTC())
            tftMsg (true, 0, "NMEA: time ok");
        else
            tftMsg (true, 1000, "NMEA: no time");

    } else if (useOSTime()) {
        tftMsg (true, 0, "Time is from computer");

    } else if (WiFi.status() == WL_CONNECTED) {

        if (useLocalNTPHost()) {

            // test user choice
            NTPServer *user_ntp = findBestNTP();        // will always return user's ntp
            if (getNTPUTC(user_ntp))
                tftMsg (true, 0, "NTP %s: %d ms\r", user_ntp->server, user_ntp->rsp_time);
            else
                tftMsg (true, 0, "NTP %s: fail\r", user_ntp->server);

        } else {

            // try all the NTP servers to find the fastest (with sneaky way out)
            SCoord s;
            drainTouch();
            tftMsg (true, 0, "Finding best NTP ...");
            NTPServer *best_ntp = NULL;
            for (int i = 0; i < N_NTP; i++) {
                NTPServer *np = &ntp_list[i];

                // measure the next. N.B. assumes we stay in sync
                if (getNTPUTC(np) == 0)
                    tftMsg (true, 0, "%s: err\r", np->server);
                else {
                    tftMsg (true, 0, "%s: %d ms\r", np->server, np->rsp_time);
                    if (!best_ntp || np->rsp_time < best_ntp->rsp_time)
                        best_ntp = np;
                }

                // cancel scan if found at least one good and tapped or typed
                TouchType tt = TT_NONE;
                if (best_ntp && (skip_skip || tft.getChar(NULL,NULL)
                                   || ((tt = readCalTouchWS(s)) != TT_NONE && inBox (s, skip_b)))) {
                    if (tt == TT_TAP_BX)
                        tooltip (s, skip_ttt);
                    else {
                        drawStringInBox ("Skip", skip_b, true, RA8875_WHITE);
                        Serial.printf ("NTP search cancelled with %s\n", best_ntp->server);
                        skipped_here = true;
                        break;
                    }
                }
            }
            if (!skip_skip)
                wdDelay(800); // linger to show last time
            if (best_ntp)
                tftMsg (true, 0, "Best NTP: %s %d ms\r", best_ntp->server, best_ntp->rsp_time);
            else
                tftMsg (true, 0, "No NTP\r");
            drainTouch();
        }
        tftMsg (true, 0, NULL);   // next row

    } else {

        tftMsg (true, 0, "No time");
    }

    // go
    initTime();

    // track from user's time if set
    if (usr_datetime > 0)
        setTime (usr_datetime);


    // init bc_power, bc_toa, bc_utc_tl and bc_modevalue
    if (!NVReadUInt16 (NV_BCPOWER, &bc_power)) {
        bc_power = 100;
        NVWriteUInt16 (NV_BCPOWER, bc_power);
    }
    if (!NVReadFloat (NV_BCTOA, &bc_toa)) {
        bc_toa = 3;
        NVWriteFloat (NV_BCTOA, bc_toa);
    }
    if (!NVReadUInt8 (NV_BC_UTCTIMELINE, &bc_utc_tl)) {
        bc_utc_tl = 0;  // default to local time line
        NVWriteUInt8 (NV_BC_UTCTIMELINE, bc_utc_tl);
    }
    if (!NVReadUInt8 (NV_BCMODE, &bc_modevalue)) {
        bc_modevalue = findBCModeValue("CW");           // default to CW
        NVWriteUInt8 (NV_BCMODE, bc_modevalue);
    }

    // init space wx
    initSpaceWX();

    // offer time to peruse unless alreay opted to skip
    if (!skipped_here) {
        #define     TO_DS 50                                // timeout delay, decaseconds
        drawStringInBox ("Skip", skip_b, false, RA8875_WHITE);
        uint8_t s_left = TO_DS/10;                          // seconds remaining
        uint32_t t0 = millis();
        drainTouch();
        for (uint8_t ds_left = TO_DS; !skip_skip && ds_left > 0; --ds_left) {
            SCoord s;
            TouchType tt = TT_NONE;
            if (tft.getChar(NULL,NULL) || ((tt = readCalTouchWS(s)) != TT_NONE && inBox(s, skip_b))) {
                if (tt == TT_TAP_BX)
                    tooltip (s, skip_ttt);
                else {
                    drawStringInBox ("Skip", skip_b, true, RA8875_WHITE);
                    break;
                }
            }
            if ((TO_DS - (millis() - t0)/100)/10 < s_left) {
                // just printing every ds_left/10 is too slow due to overhead
                tftMsg (true, 0, "Ready ... %d\r", s_left--);
            }
            wdDelay(100);
        }
    }
}

/* perform the active algorithm for the given autoMap.
 * turn on if passes above hi threshold and saw_lo, turn off if passes below lo threshold and saw_hi.
 */
static void doAutoMap (CoreMaps cm, float value_now, float thresh_hi, float thresh_lo)
{
    CoreMapInfo &cmi = cm_info[cm];

    if (value_now > thresh_hi) {

        if (!cmi.saw_hi && cmi.saw_lo) {
            // just went hi and came up from lo so turn on
            if (IS_CMROT(cm)) {
                Serial.printf ("AUTOMAP: %s already on: %g > %g > %g\n",
                                                                cmi.name, value_now, thresh_hi, thresh_lo);
            } else {
                Serial.printf ("AUTOMAP: turning on %s: %g > %g > %g\n",
                                                                cmi.name, value_now, thresh_hi, thresh_lo);
                scheduleNewCoreMap (cm);
            }
        }

        // update state
        cmi.saw_hi = true;
        cmi.saw_lo = false;         // require a new transition

    } else if (value_now < thresh_lo) {

        if (!cmi.saw_lo && cmi.saw_hi) {
            // just went lo and came down from hi so turn off but beware edge cases
            if (!IS_CMROT(cm)) {
                Serial.printf ("AUTOMAP: %s already off: %g < %g < %g\n",
                                                                cmi.name, value_now, thresh_lo, thresh_hi);
            } else if (mapIsRotating()) {
                // turn off cm but make sure there's still at least one on
                RM_CMROT (cm);
                insureCoreMap();
                scheduleNewCoreMap (core_map);
                Serial.printf ("AUTOMAP: turning off %s: %g < %g\n", cmi.name, value_now, thresh_lo);
            } else {
                Serial.printf ("AUTOMAP: leaving lone %s on: %g < %g < %g\n",
                                                                cmi.name, value_now, thresh_lo, thresh_hi);
            }
        }

        // update state
        cmi.saw_lo = true;
        cmi.saw_hi = false;         // require a new transition
    }
}

/* manage maps associated with high levels of space weather indices.
 */
static void checkAutoMap()
{
    // out fast if not on
    if (!autoMap())
        return;

    DRAPData drap;
    if (retrieveDRAP(drap) && space_wx[SPCWX_DRAP].value_ok)
        doAutoMap (CM_DRAP, space_wx[SPCWX_DRAP].value, DRAP_AUTOMAP_ON, DRAP_AUTOMAP_OFF);

    AuroraData a;
    if (retrieveAurora(a) && space_wx[SPCWX_AURORA].value_ok)
        doAutoMap (CM_AURORA, space_wx[SPCWX_AURORA].value, AURORA_AUTOMAP_ON, AURORA_AUTOMAP_OFF);
}

/* check if time to update background map, either rotating or stale
 */
void checkBGMap(void)
{
    // check for any auto maps
    checkAutoMap();

    // for sure update if later than next_map; there are other reasons too
    bool time_to_refresh = myNow() > next_map;

    // check for rotating, but not if just scheduled
    if (next_map != 0 && time_to_refresh && mapIsRotating())
        rotateNextMap();

    // note whether BC is up
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    bool bc_up = bc_pp != PANE_NONE && bc_matrix.ok && myNow() > revert_time;

    // note whether core_map is one of the BC maps and whether it's time for it to update
    bool bc_map = CM_PMACTIVE();
    bool bc_now = bc_up && bc_map && tdiff(map_time,bc_time) >= 3600;

    // update if time or to stay in sync with BC or it's been over an hour
    if (time_to_refresh || bc_now || tdiff(nowWO(),map_time)>=3600) {

        // show busy if BC up and we are updating its map
        if (bc_up && bc_map)
            plotBandConditions (plot_b[bc_pp], 1, NULL, NULL);

        // update map, schedule next
        bool ok = installFreshMaps();
        if (ok) {
            if (core_map == CM_DRAP)
                next_map = nextMapUpdate(DRAPMAP_INTERVAL);
            else if (core_map == CM_MUF_RT)
                next_map = nextMapUpdate(MUF_RT_INTERVAL);
            else if (bc_map)
                next_map = nextMapUpdate(VOACAP_INTERVAL);
            else
                next_map = nextMapUpdate(OTHER_MAPS_INTERVAL);

            if (isVOACAPMap(core_map))
                resetVOACAPRetry();

            // fresh
            map_time = nowWO();                                         // map is now current
            initEarthMap();                                             // restart fresh

        } else {
            if (isVOACAPMap(core_map))
                next_map = nextVOACAPRetry (cm_info[core_map].name);
            else
                next_map = nextWiFiRetry (cm_info[core_map].name);      // schedule retry
            if (bc_map)
                map_time = bc_time;                                     // match bc to avoid immediate retry
            else
                map_time = nowWO()+10;                                  // avoid immediate retry
        }

        // show result of effort if BC up even if not now a BC map
        if (bc_up)
            plotBandConditions (plot_b[bc_pp], ok ? 0 : -1, NULL, NULL);

        time_t dt = next_map - myNow();
        Serial.printf ("Next %s map check in %ld s at %ld\n", cm_info[core_map].name,
                                    (long)dt, (long)(millis()/1000+dt));
    }
}

/* given a GOES XRAY Flux value, return its event level designation in buf.
 */
char *xrayLevel (char *buf, const SpaceWeather_t &xray)
{
    if (!xray.value_ok)
        strcpy (buf, "Err");
    else if (xray.value < 1e-8)
        strcpy (buf, "A0.0");
    else {
        static const char levels[] = "ABCMX";
        int power = floorf(log10f(xray.value));
        if (power > -4)
            power = -4;
        float mantissa = xray.value*powf(10.0F,-power);
        char alevel = levels[8+power];
        snprintf (buf, 10, "%c%.1f", alevel, mantissa);
    }
    return (buf);
}


/* retrieve bc_matrix and optional config line underneath PLOT_CH_BC.
 * return whether at least config line was received (even if data was not)
 */
static bool retrieveBandConditions (char *config)
{
    WiFiClient bc_client;
    bool ok = false;

    // init data unknown
    bc_matrix.ok = false;

    // start by cleaning cache.
    // N.B. make sure search string match name we use below
    (void) cleanCache ("bc-", BC_INTERVAL);

    // build query
    time_t t = nowWO();
    char query[sizeof(bc_page) + 200];
    snprintf (query, sizeof(query),
        "%s?YEAR=%d&MONTH=%d&RXLAT=%.3f&RXLNG=%.3f&TXLAT=%.3f&TXLNG=%.3f&UTC=%d&PATH=%d&POW=%d&MODE=%d&TOA=%.1f",
        bc_page, year(t), month(t), dx_ll.lat_d, dx_ll.lng_d, de_ll.lat_d, de_ll.lng_d,
        hour(t), show_lp, bc_power, bc_modevalue, bc_toa);
    antenna_addargs(query+strlen(query), sizeof(query)-strlen(query));   // add antenna selection arguments to query

    // build local cache file name
    char cache_fn[100];
    snprintf (cache_fn, sizeof(cache_fn), "bc-%010u.txt", stringHash(query)); // N.B. see cleanCache() above

    // open cache or get fresh
    FILE *fp = openCachedFile (cache_fn, query, 12*3600L, 100, VOACAP_TIMEOUT_MS);
    if (fp) {

        char buf[100];

        // first line is CSV path reliability for the requested time between DX and DE, 9 bands 80-10m
        if (!fgets (buf, sizeof(buf), fp)) {
            Serial.println ("BC: No CSV");
            goto out;
        }

        // next line is configuration summary, save if interested
        if (!fgets (buf, sizeof(buf), fp)) {
            Serial.println ("BC: No config line");
            goto out;
        }
        if (config) {
            chompString (buf);
            strcpy (config, buf);
        }

        // transaction for at least config is ok
        ok = true;

        // next 24 lines are reliability matrix.
        // N.B. col 1 is UTC but runs from 1 .. 24, 24 is really 0
        // lines include data for 9 bands, 80-10, but we drop 60 for BandCdtnMatrix
        float rel[BMTRX_COLS];          // values are path reliability 0 .. 1
        for (int r = 0; r < BMTRX_ROWS; r++) {

            // read next row
            if (!fgets (buf, sizeof(buf), fp)) {
                Serial.printf ("BC: fail row %d\n", r);
                goto out;
            }

            // crack row, skipping 60 m
            int utc_hr;
            if (sscanf(buf, "%d %f,%*f,%f,%f,%f,%f,%f,%f,%f", &utc_hr,
                        &rel[0], &rel[1], &rel[2], &rel[3], &rel[4], &rel[5], &rel[6], &rel[7])
                            != BMTRX_COLS + 1) {
                Serial.printf ("BC: bad matrix line: %s\n", buf);
                goto out;
            }

            // insure correct utc
            utc_hr %= 24;

            // add to bc_matrix as integer percent
            for (int c = 0; c < BMTRX_COLS; c++)
                bc_matrix.m[utc_hr][c] = (uint8_t)(100*rel[c]);
        }

        // #define _TEST_BAND_MATRIX
        #if defined(_TEST_BAND_MATRIX)
            for (int r = 0; r < BMTRX_ROWS; r++)                    // time 0 .. 23
                for (int c = 0; c < BMTRX_COLS; c++)                // band 80 .. 10
                    bc_matrix.m[r][c] = 100*r*c/BMTRX_ROWS/BMTRX_COLS;
        #endif

        // matrix ok
        bc_matrix.ok = true;

        // finished with file
        fclose(fp);

    } else {
        Serial.println ("VOACAP connection failed");
    }

out:

    // out
    return (ok);
}

/* convert an array of 4 big-endian network-order bytes into a uint32_t
 */
static uint32_t crackBE32 (uint8_t bp[])
{
    union {
        uint32_t be;
        uint8_t ba[4];
    } be4;

    be4.ba[3] = bp[0];
    be4.ba[2] = bp[1];
    be4.ba[1] = bp[2];
    be4.ba[0] = bp[3];

    return (be4.be);
}

/* keep the NCDXF_b up to date.
 * N.B. this is called often so do minimal work.
 */
static void checkBRB (time_t t)
{
    // routine update of NCFDX map beacons
    updateBeacons (false);

    // see if it's time to rotate
    if (t > brb_next_update) {

        // move brb_mode to next rotset if rotating
        if (BRBIsRotating()) {
            for (int i = 1; i < BRB_N; i++) {
                int next_mode = (brb_mode + i) % BRB_N;
                if (brb_rotset & (1 << next_mode)) {
                    brb_mode = next_mode;
                    Serial.printf ("BRB: rotating to mode \"%s\"\n", brb_names[brb_mode]);
                    break;
                }
            }
        }

        // update brb_mode
        if (drawNCDXFBox()) {

            brb_next_update = nextRotation(PANE_NONE);

        } else {

            // trouble
            brb_next_update = nextWiFiRetry("BRB");
        }

    } else {

        // check a few that need spontaneous updating

        switch (brb_mode) {

        case BRB_SHOW_BME76:
        case BRB_SHOW_BME77:
            // only if new BME
            if (newBME280data())
                (void) drawNCDXFBox();
            break;

        case BRB_SHOW_SWSTATS:
            // only if new space stats
            if (checkForNewSpaceWx())
                (void) drawNCDXFBox();
            break;

        case BRB_SHOW_DEWX:
        case BRB_SHOW_DXWX:
            // routine drawNCDXFBox() are enough
            break;

        case BRB_SHOW_PHOT:
        case BRB_SHOW_BR:
            // these are updated from followBrightness() in main loop()
            break;
        }

    }
}

/* insure the given plot choice is visible in some pane by itself.
 * N.B. never use PANE_0
 */
void setPlotVisible (PlotChoice pc)
{
    // done if the choice is already on display
    if (findPaneChoiceNow (pc) != PANE_NONE)
        return;

    // not on display now but maybe in rotation, else just pick one
    PlotPane pp = findPaneForChoice (pc);
    if (pp == PANE_NONE)
        pp = PANE_3;

    // install as the only choice
    (void) setPlotChoice (pp, pc);
    plot_rotset[pp] = PLOTBIT(pc);
    savePlotOps();
}

/* set the given pane to the given plot choice now.
 * return whether successful.
 * N.B. we might change plot_ch but we NEVER change plot_rotset here
 * N.B. it's harmless to set pane to same choice again.
 */
bool setPlotChoice (PlotPane pp, PlotChoice pc)
{
    // ignore if new choice is already in some other pane
    PlotPane pp_now = findPaneForChoice (pc);
    if (pp_now != PANE_NONE && pp_now != pp)
        return (false);

    // display box
    const SBox &box = plot_b[pp];

    // first check a few plot types that require extra tests or processing.
    switch (pc) {
    case PLOT_CH_DXCLUSTER:
        if (!useDXCluster())
            return (false);
        break;
    case PLOT_CH_GIMBAL:
        if (!haveGimbal())
            return (false);
        break;
    case PLOT_CH_TEMPERATURE:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, pc);
        break;
    case PLOT_CH_PRESSURE:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, pc);
        break;
    case PLOT_CH_HUMIDITY:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, pc);
        break;
    case PLOT_CH_DEWPOINT:
        if (getNBMEConnected() == 0)
            return (false);
        drawOneBME280Pane (box, pc);
        break;
    case PLOT_CH_COUNTDOWN:
        if (getSWEngineState(NULL,NULL) != SWE_COUNTDOWN)
            return (false);
        if (getSWDisplayState() == SWD_NONE) {
            plot_ch[pp] = pc;           // must set here so force arg ripples through to drawCDTimeRemaining()
            drawMainPageStopwatch(true);
        }
        break;
    default:
        break;          // lint
    }

    // ok, commit choice to the given pane with immediate refresh
    plot_ch[pp] = pc;
    fresh_redraw[pc] = true;
    next_update[pp] = 0;

    // insure gimbal off if no longer selected for display
    if (findPaneChoiceNow (PLOT_CH_GIMBAL) == PANE_NONE)
        closeGimbal();

    // persist
    savePlotOps();

    // ok!
    return (true);
}


/* NTP time server query.
 * returns UNIX time if ok, or 0 if trouble.
 * for good NTP packet description try
 *   http://www.cisco.com
 *      /c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
 */
time_t getNTPUTC (NTPServer *ntp)
{
    // NTP contents packet
    static const uint8_t timeReqA[] = { 0xE3, 0x00, 0x06, 0xEC };
    static const uint8_t timeReqB[] = { 0x31, 0x4E, 0x31, 0x34 };

    // create udp endpoint
    WiFiUDP ntp_udp;
    if (!ntp_udp.begin(1000+random(50000))) {                   // any local port
        Serial.println ("NTP: UDP startup failed");
        return (0);
    }

    // NTP buffer and timers
    uint8_t  buf[48];
    uint32_t tx_ms, rx_ms;

    // Assemble request packet
    memset(buf, 0, sizeof(buf));
    memcpy(buf, timeReqA, sizeof(timeReqA));
    memcpy(&buf[12], timeReqB, sizeof(timeReqB));

    // send
    Serial.printf("NTP: Issuing request to %s\n", ntp->server);
    ntp_udp.beginPacket (ntp->server, 123);                     // NTP uses port 123
    ntp_udp.write(buf, sizeof(buf));
    tx_ms = millis();                                           // record when packet sent
    if (!ntp_udp.endPacket()) {
        Serial.println ("NTP: UDP write failed");
        ntp->rsp_time = NTP_TOO_LONG;                           // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }
    // Serial.print ("NTP: Sent 48 ... ");

    // receive response
    // Serial.print("NTP: Awaiting response ... ");
    memset(buf, 0, sizeof(buf));
    uint32_t t0 = millis();
    while (ntp_udp.parsePacket() == 0) {
        if (timesUp (&t0, NTP_TOO_LONG)) {
            Serial.println("NTP: UDP timed out");
            ntp->rsp_time = NTP_TOO_LONG;                       // force different choice next time
            ntp_udp.stop();
            return (0UL);
        }
        wdDelay(10);
    }
    rx_ms = millis();                                           // record when packet arrived

    // record response time
    ntp->rsp_time = rx_ms - tx_ms;
    // Serial.printf ("NTP: %s replied after %d ms\n", ntp->server, ntp->rsp_time);

    // read response
    if (ntp_udp.read (buf, sizeof(buf)) != sizeof(buf)) {
        Serial.println ("NTP: UDP read failed");
        ntp->rsp_time = NTP_TOO_LONG;                           // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }

    // only accept server responses which are mode 4
    uint8_t mode = buf[0] & 0x7;
    if (mode != 4) {                                            // insure server packet
        Serial.printf ("NTP: RX mode must be 4 but it is %d\n", mode);
        ntp_udp.stop();
        return (0UL);
    }

    // crack and advance to next whole second
    time_t unix_s = crackBE32 (&buf[40]) - 2208988800UL;        // packet transmit time - (1970 - 1900)
    if ((uint32_t)unix_s > 0x7FFFFFFFUL) {                      // sanity check beyond unsigned value
        Serial.printf ("NTP: crazy large UNIX time: %ld\n", (long)unix_s);
        ntp_udp.stop();
        return (0UL);
    }
    uint32_t fraction_more = crackBE32 (&buf[44]);              // x / 10^32 additional second
    uint16_t ms_more = 1000UL*(fraction_more>>22)/1024UL;       // 10 MSB to ms
    uint16_t transit_time = (rx_ms - tx_ms)/2;                  // transit = half the round-trip time
    ms_more += transit_time;                                    // with transit now = unix_s + ms_more
    uint16_t sec_more = ms_more/1000U+1U;                       // whole seconds behind rounded up
    wdDelay (sec_more*1000U - ms_more);                         // wait to next whole second
    unix_s += sec_more;                                         // account for delay
    // Serial.print ("NTP: Fraction "); Serial.print(ms_more);
    // Serial.print (", transit "); Serial.print(transit_time);
    // Serial.print (", seconds "); Serial.print(sec_more);
    // Serial.print (", UNIX "); Serial.print (unix_s); Serial.println();

    // one more sanity check
    if (unix_s < 1577836800L) {          // Jan 1 2020
        Serial.printf ("NTP: crazy small UNIX time: %ld\n", (long)unix_s);
        ntp_udp.stop();
        return (0UL);
    }

    ntp_udp.stop();
    return (unix_s);
}

/* read next char from client, waiting a short while if necessary.
 * return whether another character was in fact available.
 */
bool getTCPChar (WiFiClient &client, char *cp)
{
    int c = client.read();
    if (c == -1)
        return (false);
    *cp = (char)c;
    return (true);
}

/* send User-Agent to client
 */
void sendUserAgent (WiFiClient &client)
{
    // don't send full list until first time main page is up to insure all subsystems are up.
    static bool ready;
    if (mainpage_up)
        ready = true;

    char ua[400];

    if (logUsageOk() && ready) {

        // display mode: 0=X11 1=fb0 2=X11full 3=X11+live 4=X11full+live 5=noX
        int dpy_mode = 0;
        #if defined(_USE_FB0)
            dpy_mode = 1;
        #else
            bool fs = getX11FullScreen() || getWebFullScreen();
            bool live = n_rwweb > 0;
            if (live)
                dpy_mode = fs ? 4 : 3;
            else if (fs)
                dpy_mode = 2;
        #endif
        #if defined(_WEB_ONLY)
            dpy_mode = 5;
        #endif

        // encode stopwatch if on else as per map_proj
        int main_page = 0;
        switch (getSWDisplayState()) {
        default:
        case SWD_NONE:
            // < V2.81: main_page = azm_on ? 1: 0;
            // >= 2.96: add MAPP_MOLL
            // >= 3.05: change MAP_MOLL to MAPP_ROB
            switch ((MapProjection)map_proj) {
            case MAPP_MERCATOR:  main_page = 0; break;
            case MAPP_AZIMUTHAL: main_page = 1; break;
            case MAPP_AZIM1:     main_page = 5; break;
            case MAPP_ROB:       main_page = 6; break;
            default: fatalError("sendUserAgent() map_proj %d", map_proj);
            }
            break;
        case SWD_MAIN:
            main_page = 2;
            break;
        case SWD_BCDIGITAL:
            main_page = 4;
            break;
        case SWD_BCANALOG:
            main_page = 3;
            break;
        }

        // alarm clocks
        AlarmState a_ds, a_os;
        time_t a_utct;
        bool a_utc;
        uint16_t a_hr, a_mn;
        bool alarm_utc;
        getDailyAlarmState (a_ds, a_hr, a_mn, alarm_utc);
        getOneTimeAlarmState (a_os, a_utct, a_utc);
        int alarms = 0;
        if (a_ds != ALMS_OFF)
            alarms += 1;
        if (a_os != ALMS_OFF)
            alarms += 2;


        // encode plot options
        // prior to V2.67: value was either plot_ch or 99
        // since V2.67:    value is 100 + plot_ch
        // V3.07: added PANE_0
        int plotops[PANE_N];
        for (int i = 0; i < PANE_N; i++)
            plotops[i] = isPaneRotating((PlotPane)i) ? 100+(int)plot_ch[i] : (int)plot_ch[i];

        // prefix map style with N unless showing night and/or R if in a rotation set
        char map_style[NV_COREMAPSTYLE_LEN+2];
        char *mp = map_style;
        if (!night_on)
            *mp++ = 'N';
        if (mapIsRotating())
            *mp++ = 'R';
        (void) getCoreMapStyle(core_map, mp);

        // kx3 baud else gpio on/off
        int gpio = getKX3Baud();
        if (gpio == 0) {
            if (GPIOOk())
                gpio = 1;
            else if (found_mcp)
                gpio = 2;
        }

        // which phot, if any
        int io = 0;
        if (found_phot) io |= 1;
        if (found_ltr) io |= 2;
        if (found_mcp) io |= 4;
        if (getI2CFilename()) io |= 8;

        // combine rss_on and rss_local
        int rss_code = rss_on + 2*rss_local;

        // gimbal and rig bit mask: 4 = rig, 2 = azel  1 = az only
        bool gconn, vis_now, has_el, gstop, gauto;
        float az, el;
        bool gbl_on = getGimbalState (gconn, vis_now, has_el, gstop, gauto, az, el);
        bool rig_on = getRigctld (NULL, NULL);
        bool flrig = getFlrig (NULL, NULL);
        int rr_score = (gbl_on ? (has_el ? 2 : 1) : 0) | (rig_on ? 4 : 0) | (flrig ? 8 : 0);

        // brb_mode plus 100 to indicate rotation code
        int brb = brb_mode;
        if (BRBIsRotating())
            brb += 100;

        // GPSD
        int gpsd = 0;
        if (useGPSDTime())
            gpsd |= 1;
        if (useGPSDLoc())
            gpsd |= 2;

        // date formatting
        int dayf = (int)getDateFormat();
        if (weekStartsOnMonday())                       // added in 2.86
            dayf |= 4;

        // number of dashed colors                      // added to first LV6 in 2.90
        int n_dashed = 0;
        for (int i = 0; i < N_CSPR; i++)
            if (getPathDashed((ColorSelection)i))
                n_dashed++;

        // path size: 0 none, 1 thin, 2 wide
        int path = 1;                                   // pointless as of 4.10

        // label spots: 0 no, 1 prefix, 2 call, 3 dot
        int spots = 0;
        switch (getSpotLabelType()) {
        case LBL_NONE:   spots = 0; break;
        case LBL_DOT:    spots = 3; break;
        case LBL_PREFIX: spots = 1; break;
        case LBL_CALL:   spots = 2; break;
        case LBL_N:      fatalError ("Bogus log spots\n");
        }

        // crc code
        int crc = flash_crc_ok;
        if (want_kbcursor)
            crc |= (1<<15);                             // max old _debug_ was 1<<14

        // callsign colors
        uint16_t call_fg, call_bg;
        uint8_t call_rb;
        NVReadUInt16 (NV_CALL_FG, &call_fg);
        NVReadUInt8 (NV_CALL_RAINBOW, &call_rb);
        if (call_rb)
            call_bg = 1;                                // unlikely color to mean rainbow
        else
            NVReadUInt16 (NV_CALL_BG, &call_bg);

        // ntp source
        int ntp = 0;
        if (useLocalNTPHost())
            ntp = 1;
        else if (useOSTime())
            ntp = 2;                                    // added in 3.05

        // active watch lists
        int wl = 0;
        for (int i = 0; i < WLID_N; i++) {
            if (getWatchListState ((WatchListId)i, NULL) != WLA_OFF)
                wl |= (1 << i);
        }

        // decide units: 0 imperial, 1 metric, 2 british
        // was just useMetricUnits() prior to 4_09
        int units = showDistKm() ? 1 : (showATMhPa() ? 2 : 0);  // only Brits mix miles + hPa

        // panzoom
        bool pz = pan_zoom.zoom != MIN_ZOOM || pan_zoom.pan_x != 0 || pan_zoom.pan_y != 0;

        // autoo upgrade
        int aup_hr;
        (void) autoUpgrade (aup_hr);


        snprintf (ua, sizeof(ua),
            "User-Agent: %s/%s (id %u up %lld) crc %d "
                "LV7 %s %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f %.2f %d %d %d %d "
                "%d %d %d %d %d %d %d %d %d %d %d %d "
                "%d %d %d %d %d %u %u %d "                              // LV6 starts here
                "LV7 %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d "     // LV7 starts here
                "\r\n",
            platform, hc_version, ESP.getChipId(), (long long)getUptime(NULL,NULL,NULL,NULL), crc,
            map_style, main_page, mapgrid_choice, plotops[PANE_1], plotops[PANE_2], plotops[PANE_3],
            de_time_fmt, brb, dx_info_for_sat, rss_code, units,
            getNBMEConnected(), gpio, io, getBMETempCorr(BME_76), getBMEPresCorr(BME_76),
            desrss, dxsrss, BUILD_W, dpy_mode,

            // new for LV5:
            alarms, getCenterLng(), (int)auxtime /* getDoy() before 2.80 */, names_on, getDemoMode(),
            (int)getSWEngineState(NULL,NULL), (int)getBigClockBits(), utcOffset(), gpsd,
            rss_interval, dayf, rr_score,

            // new for LV6:
            useMagBearing(), n_dashed, ntp, path, spots,
            call_fg, call_bg, !clockTimeOk(), // default clock 0 == ok

            // new for LV7:
            scrollTopToBottom(),
            0, // nMoreSroll rm V4.04
            1, // rankSpaceWx rm V4.07
            showNewDXDEWx(), getPaneRotationPeriod(), pw_file != NULL, n_roweb>0, pz,
            plotops[PANE_0], screenIsLocked(), showPIP(), (int)getGrayDisplay(), wl,
            aup_hr, 0);

    } else {
        snprintf (ua, sizeof(ua), "User-Agent: %s/%s (id %u up %lld) crc %d\r\n",
            platform, hc_version, ESP.getChipId(), (long long)getUptime(NULL,NULL,NULL,NULL), flash_crc_ok);
    }

    // send
    client.print(ua);

}

/* issue an HTTP Get for an arbitary page
 */
static void httpGET (WiFiClient &client, const char *server, const char *page)
{
    client.print ("GET "); client.print (page); client.print (" HTTP/1.0\r\n");
    client.print ("Host: "); client.println (server);
    sendUserAgent (client);
    client.print ("Connection: close\r\n\r\n");
}

/* issue an HTTP Get to a /ham/HamClock page named in ram
 */
void httpHCGET (WiFiClient &client, const char *server, const char *hc_page)
{
    static const char hc[] = "/ham/HamClock";
    StackMalloc full_mem(strlen(hc_page) + sizeof(hc));         // sizeof includes the EOS
    char *full_hc_page = (char *) full_mem.getMem();
    snprintf (full_hc_page, full_mem.getSize(), "%s%s", hc, hc_page);
    httpGET (client, server, full_hc_page);
}

/* issue an HTTPS Get to a /ham/HamClock page named in ram by using a curl command
 */

bool connecthttpsHCGET (WiFiClient &client, const char *server, const char *hc_page)
{
    static const char c1[] = "curl -A \"";  //then platform
    static const char c2[] = "/";                    //then hc_version
    static const char c3[] = "\" --max-time 15 --silent --retry 2 https://"; //then server
    static const char hc[] = "/ham/HamClock";        // then hc_page
    int memlen = strlen(c1)+strlen(platform)+strlen(c2)+strlen(hc_version)+strlen(c3)+strlen(server)+strlen(hc)+strlen(hc_page)+1;
    StackMalloc curlbuf(memlen);
    char *curl = (char *) curlbuf.getMem();
    snprintf (curl, memlen, "%s%s%s%s%s%s%s%s",c1,platform,c2,hc_version,c3,server,hc, hc_page);
    printf("wifi: connecting to command %s\n",curl);
    return (client.connectCommand(curl));
}
/* skip the given wifi client stream ahead to just after the first blank line, return whether ok.
 * this is often used so subsequent stop() on client doesn't slam door in client's face with RST.
 * Along the way, if find a header field with the given name (unless NULL) return value in the given string.
 * if header is not found, we still return true but value[0] will be '\0'.
 */
bool httpSkipHeader (WiFiClient &client, const char *header, char *value, int value_len)
{
    char line[200];

    // prep
    int hdr_len = header ? strlen(header) : 0;
    if (value)
        value[0] = '\0';
    char *hdr;

    // read until find a blank line
    do {
        if (!getTCPLine (client, line, sizeof(line), NULL))
            return (false);
        // Serial.println (line);

        if (header && value && (hdr = strstr (line, header)) != NULL)
            snprintf (value, value_len, "%s", hdr + hdr_len);

    } while (line[0] != '\0');  // getTCPLine absorbs \r\n so this tests for a blank line

    return (true);
}

/* same but when we don't care about any header field;
 * so we pick up Remote_Addr for postDiags()
 */
bool httpSkipHeader (WiFiClient &client)
{
    return (httpSkipHeader (client, "Remote_Addr: ", remote_addr, sizeof(remote_addr)));
}

/* retrieve and plot latest and predicted DRAP indices, return whether io ok
 */
static bool updateDRAP (const SBox &box)
{
    DRAPData drap;
    bool ok = retrieveDRAP (drap);

    updateClocks(false);

    if (ok) {

        if (!drap.data_ok) {
            plotMessage (box, DRAPPLOT_COLOR, "DRAP data invalid");
        } else {
            // plot
            plotXY (box, drap.x, drap.y, DRAPDATA_NPTS, "Hours", "DRAP, max MHz", DRAPPLOT_COLOR,
                                                            0, 0, drap.y[DRAPDATA_NPTS-1]);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, DRAPPLOT_COLOR, "DRAP connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest and predicted kp indices, return whether io ok
 */
static bool updateKp(const SBox &box)
{
    // data are provided every 3 hours == 8/day. collect 7 days of history + 2 days of predictions
    KpData kp;

    bool ok = retrieveKp (kp);

    updateClocks(false);

    if (ok) {

        if (!kp.data_ok) {
            plotMessage (box, KP_COLOR, "Kp data invalid");
        } else {
            // plot
            plotXY (box, kp.x, kp.p, KP_NV, "Days", "Planetary Kp", KP_COLOR, 0, 9, space_wx[SPCWX_KP].value);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, KP_COLOR, "Kp connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest and predicted DST indices, return whether io ok
 */
static bool updateDST(const SBox &box)
{
    DSTData dst;

    bool ok = retrieveDST (dst);

    updateClocks(false);

    if (ok) {

        if (!dst.data_ok) {
            plotMessage (box, DST_COLOR, "DST data invalid");
        } else {
            // plot
            plotXY (box, dst.age_hrs, dst.values, DST_NV, "Hours", "Dist Storm Time, nT", DST_COLOR, 0, 0,
                                                                                space_wx[SPCWX_DST].value);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, DST_COLOR, "DST connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest xray indices, return whether io ok
 */
static bool updateXRay(const SBox &box)
{
    XRayData xray;

    bool ok = retrieveXRay (xray);

    updateClocks(false);

    if (ok) {

        if (!xray.data_ok) {
            plotMessage (box, XRAY_LCOLOR, "XRay data invalid");
        } else {
            // overlay short over long with fixed y axis
            char level_str[10];
            plotXYstr (box, xray.x, xray.l, XRAY_NV, "Hours", "GOES 16 X-Ray", XRAY_LCOLOR, -9, -2, NULL)
                 && plotXYstr (box, xray.x, xray.s, XRAY_NV, NULL, NULL, XRAY_SCOLOR, -9, -2,
                                xrayLevel(level_str, space_wx[SPCWX_XRAY]));
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, XRAY_LCOLOR, "X-Ray connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot fresh sun spot indices, return whether io ok
 */
static bool updateSunSpots (const SBox &box)
{
    SunSpotData ssn;

    bool ok = retrieveSunSpots (ssn);

    updateClocks(false);

    if (ok) {

        if (!ssn.data_ok) {
            plotMessage (box, SSN_COLOR, "SSN data invalid");
        } else {
            // plot, showing value as traditional whole number
            char label[20];
            snprintf (label, sizeof(label), "%.0f", ssn.ssn[SSN_NV-1]);
            plotXYstr (box, ssn.x, ssn.ssn, SSN_NV, "Days", "Sunspot Number", SSN_COLOR, 0, 0, label);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, SSN_COLOR, "SSN connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest and predicted solar flux indices, return whether io ok.
 */
static bool updateSolarFlux(const SBox &box)
{
    SolarFluxData sf;

    bool ok = retrieveSolarFlux(sf);

    updateClocks(false);

    if (ok) {
        if (!sf.data_ok) {
            plotMessage (box, SFLUX_COLOR, "Solar Flux data invalid");
        } else {
            plotXY (box, sf.x, sf.sflux, SFLUX_NV, "Days", "10.7 cm Solar flux",
                                                SFLUX_COLOR, 0, 0, sf.sflux[SFLUX_NV-10]);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, SFLUX_COLOR, "Flux connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest and predicted solar wind indices, return whether io ok.
 */
static bool updateSolarWind(const SBox &box)
{
    SolarWindData sw;

    bool ok = retrieveSolarWind (sw);

    if (ok) {
        if (!sw.data_ok) {
            plotMessage (box, SWIND_COLOR, "Solar wind data invalid");
        } else {
            plotXY (box, sw.x, sw.y, sw.n_values, "Hours", "Solar wind", SWIND_COLOR,0,0,sw.y[sw.n_values-1]);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {
        plotMessage (box, SWIND_COLOR, "Sol Wind connection failed");
    }

    // done
    return (ok);
}

/* retrieve and plot latest BZBT indices, return whether io ok
 */
static bool updateBzBt (const SBox &box)
{
    BzBtData bzbt;

    bool ok = retrieveBzBt (bzbt);

    if (ok) {

        if (!bzbt.data_ok) {
            plotMessage (box, BZBT_BZCOLOR, "BzBt data invalid");
        } else {

            // find first within 25 hours thence min/max over both
            float min_bzbt = 1e10, max_bzbt = -1e10;
            int f25 = -1;
            for (int i = 0; i < BZBT_NV; i++) {
                if (f25 < 0 && bzbt.x[i] >= -25)
                    f25 = i;
                if (f25 >= 0) {
                    if (bzbt.bz[i] < min_bzbt)
                        min_bzbt = bzbt.bz[i];
                    else if (bzbt.bz[i] > max_bzbt)
                        max_bzbt = bzbt.bz[i];
                    if (bzbt.bt[i] < min_bzbt)
                        min_bzbt = bzbt.bt[i];
                    else if (bzbt.bt[i] > max_bzbt)
                        max_bzbt = bzbt.bt[i];
                }
            }

            // plot
            char bz_label[30];
            snprintf (bz_label, sizeof(bz_label), "%.1f", bzbt.bz[BZBT_NV-1]);         // newest Bz
            plotXYstr (box, bzbt.x+f25, bzbt.bz+f25, BZBT_NV-f25, "Hours", "Solar Bz and Bt, nT",
                                        BZBT_BZCOLOR, min_bzbt, max_bzbt, NULL)
                     && plotXYstr (box, bzbt.x+f25, bzbt.bt+f25, BZBT_NV-f25, NULL, NULL,
                                        BZBT_BTCOLOR, min_bzbt, max_bzbt, bz_label);
        }

        // update NCDXF box too either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {

        plotMessage (box, BZBT_BZCOLOR, "BzBt update error");
    }

    // done
    return (ok);
}

/* retrieve and draw latest band conditions in box b if needed or requested.
 * return whether io ok.
 */
static bool updateBandConditions(const SBox &box, bool force)
{
    // update if asked to or out of sync with prop map or it's just been a while
    bool update_bc = force || (CM_PMACTIVE() && tdiff(bc_time,map_time) >= 3600)
                           || (tdiff (nowWO(), bc_time) >= 3600)
                           || myNow() >= bc_matrix.next_update;

    static char config[100];    // retain value after each retrieval

    // download if so and note whether io ok
    bool io_ok = true;
    if (update_bc) {

        // honor VOACAP rate limit (silent for auto-retry path)
        time_t now = myNow();
        if (voacapThrottled(now)) {
            Serial.printf ("VOACAP: BC throttled, %ld s since last attempt (min %d s)\n",
                           (long)(now - lastVOACAPAttempt()), VOACAP_MIN_INTERVAL);
            bc_matrix.next_update = now + VOACAP_MIN_INTERVAL;
            bc_time = nowWO();          // count as an attempt for map-coordination tdiffs
            io_ok = false;
        } else {
            noteVOACAPAttempt(now);

            // fresh download
            if (retrieveBandConditions (config)) {
                bc_matrix.next_update = nextRetrieval (PLOT_CH_BC, BC_INTERVAL);
                resetVOACAPRetry();
            } else {
                bc_matrix.next_update = nextVOACAPRetry("BC");
                io_ok = false;
            }

            // note time of attemp to coordinate with maps
            bc_time = nowWO();
        }
    }

    // plot
    if (bc_matrix.ok) {

        plotBandConditions (box, 0, &bc_matrix, config);

    } else {

        plotMessage (box, RA8875_RED, "No VOACAP data");

        // if problem persists more than an hour, this prevents the tdiff's above from being true every time
        map_time = bc_time = nowWO() - 1000;
    }

    return (io_ok);
}

/* display the RSG NOAA solar environment scale values in the given box.
 * return whether data transaction was valid, even if data are bad.
 */
static bool updateNOAASWx(const SBox &box)
{
    updateClocks(false);
    return (plotNOAASWx (box));
}

/* retrieve and plot latest aurora indices, return whether io ok
 */
static bool updateAurora (const SBox &box)
{
    AuroraData a;

    bool ok = retrieveAurora (a);

    if (ok) {

        if (!a.data_ok) {
            plotMessage (box, AURORA_COLOR, "Aurora data invalid");
        } else {

            // plot
            char aurora_label[30];
            snprintf (aurora_label, sizeof(aurora_label), "%.0f", a.percent[a.n_points-1]);
            plotXYstr (box, a.age_hrs, a.percent, a.n_points, "Hours", "Aurora Chances, max %",
                                        AURORA_COLOR, 0, 100, aurora_label);
        }

        // update NCDXF box either way
        if (brb_mode == BRB_SHOW_SWSTATS)
            drawNCDXFSpcWxStats(RA8875_BLACK);

    } else {

        plotMessage (box, AURORA_COLOR, "Aurora error");
    }

    // done
    return (ok);
}

/* check for tap at s known to be within BandConditions box b:
 *    tapping left-half band toggles REF map, right-half toggles TOA map
 *    tapping timeline toggles bc_utc_tl;
 *    tapping power offers power menu;
 *    tapping TOA offers take-off menu;
 *    tapping SP/LP toggles.
 * return whether tap was useful for us.
 * N.B. coordinate tap positions with plotBandConditions()
 */
bool checkBCTouch (const SCoord &s, const SBox &b)
{
    // not ours if not in our box or tapped titled or data not valid
    if (!inBox (s, b) || s.y < b.y+PANETITLE_H || !bc_matrix.ok)
        return (false);

    // tap area for power cycle
    SBox power_b;
    power_b.x = b.x + 5;
    power_b.y = b.y + 13*b.h/14;
    power_b.w = b.w/5;
    power_b.h = b.h/12;
    // drawSBox (power_b, RA8875_RED);     // RBF

    // tap area for mode choice
    SBox mode_b;
    mode_b.x = power_b.x + power_b.w + 1;
    mode_b.y = power_b.y;
    mode_b.w = b.w/6;
    mode_b.h = power_b.h;
    // drawSBox (mode_b, RA8875_RED);      // RBF

    // tap area for TOA
    SBox toa_b;
    toa_b.x = mode_b.x + mode_b.w + 1;
    toa_b.y = mode_b.y;
    toa_b.w = b.w/5;
    toa_b.h = mode_b.h;
    // drawSBox (toa_b, RA8875_RED);       // RBF

    // tap area for SP/LP
    SBox splp_b;
    splp_b.x = toa_b.x + toa_b.w + 1;
    splp_b.y = toa_b.y;
    splp_b.w = b.w/6;
    splp_b.h = toa_b.h;
    // drawSBox (splp_b, RA8875_RED);      // RBF

    // tap area for timeline strip
    SBox tl_b;
    tl_b.x = b.x + 1;
    tl_b.y = b.y + 12*b.h/14;
    tl_b.w = b.w - 2;
    tl_b.h = b.h/12;
    // drawSBox (tl_b, RA8875_WHITE);      // RBF

    if (inBox (s, power_b)) {

        // build menu of available power choices
        MenuItem mitems[n_bc_powers];
        char labels[n_bc_powers][20];
        for (int i = 0; i < n_bc_powers; i++) {
            MenuItem &mi = mitems[i];
            mi.type = MENU_1OFN;
            mi.set = bc_power == bc_powers[i];
            mi.group = 1;
            mi.indent = 5;
            mi.label = labels[i];
            snprintf (labels[i], sizeof(labels[i]), "%d watt%s", bc_powers[i],
                                bc_powers[i] > 1 ? "s" : "");
        };

        SBox menu_b;
        menu_b.x = power_b.x;
        menu_b.y = b.y + b.h/4;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, NARRAY(mitems), mitems};
        uint16_t new_power = bc_power;
        if (runMenu (menu)) {
            for (int i = 0; i < n_bc_powers; i++) {
                if (menu.items[i].set) {
                    new_power = bc_powers[i];
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if power changed
        bool power_changed = new_power != bc_power;
        if (power_changed) {
            bc_power = new_power;
            NVWriteUInt16 (NV_BCPOWER, bc_power);
            if (CM_PMACTIVE())
                scheduleNewCoreMap(core_map);
        }
        (void) updateBandConditions (b, power_changed);

    } else if (inBox (s, mode_b)) {

        // show menu of available mode choices
        MenuItem mitems[N_BCMODES];
        for (int i = 0; i < N_BCMODES; i++)
            mitems[i] = {MENU_1OFN, bc_modevalue == bc_modes[i].value, 1, 5, bc_modes[i].name, 0};

        SBox menu_b;
        menu_b.x = mode_b.x;
        menu_b.y = b.y + b.h/3;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, N_BCMODES, mitems};
        uint16_t new_modevalue = bc_modevalue;
        if (runMenu (menu)) {
            for (int i = 0; i < N_BCMODES; i++) {
                if (menu.items[i].set) {
                    new_modevalue = bc_modes[i].value;
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if mode changed
        bool mode_changed = new_modevalue != bc_modevalue;
        if (mode_changed) {
            bc_modevalue = new_modevalue;
            NVWriteUInt8 (NV_BCMODE, bc_modevalue);
            if (CM_PMACTIVE())
                scheduleNewCoreMap(core_map);
        }
        (void) updateBandConditions (b, mode_changed);

    } else if (inBox (s, toa_b)) {

        // show menu of available TOA choices
        // N.B. line display width can only accommodate 1 character
        MenuItem mitems[3];
        mitems[0] = {MENU_1OFN, bc_toa <= 1,              1, 5, ">1 deg", 0};
        mitems[1] = {MENU_1OFN, bc_toa > 1 && bc_toa < 9, 1, 5, ">3 degs", 0};
        mitems[2] = {MENU_1OFN, bc_toa >= 9,              1, 5, ">9 degs", 0};

        SBox menu_b;
        menu_b.x = toa_b.x;
        menu_b.y = b.y + b.h/2;
        menu_b.w = 0;           // shrink to fit

        // run menu, find selection
        SBox ok_b;
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, NARRAY(mitems), mitems};
        float new_toa = bc_toa;
        if (runMenu (menu)) {
            for (int i = 0; i < NARRAY(mitems); i++) {
                if (menu.items[i].set) {
                    new_toa = atof (mitems[i].label+1);
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if mode changed
        bool toa_changed = new_toa != bc_toa;
        if (toa_changed) {
            bc_toa = new_toa;
            NVWriteFloat (NV_BCTOA, bc_toa);
            if (CM_PMACTIVE())
                scheduleNewCoreMap(core_map);
        }
        (void) updateBandConditions (b, toa_changed);

    } else if (inBox (s, splp_b)) {

        // toggle short/long path -- update DX info too
        show_lp = !show_lp;
        NVWriteUInt8 (NV_LP, show_lp);
        if (CM_PMACTIVE())
            scheduleNewCoreMap(core_map);
        drawDXInfo ();
        (void) updateBandConditions (b, true);

    } else if (inBox (s, tl_b)) {

        // toggle bc_utc_tl and redraw
        bc_utc_tl = !bc_utc_tl;
        NVWriteUInt8 (NV_BC_UTCTIMELINE, bc_utc_tl);
        plotBandConditions (b, 0, NULL, NULL);

    } else {

        // check tapping a row in the table. if so toggle band and type.

        int tap_band = (b.y + b.h - 20 - s.y) / ((b.h - LISTING_Y0)/BMTRX_COLS);
        if (tap_band >= 0 && tap_band < PROPBAND_N) {

            // note whether map needs refresh which means core_map changed
            bool refresh_map = false;

            // check which side
            if (s.x < b.x + b.w/2) {
                // tapped on left for REL map
                if (IS_CMROT(CM_PMREL) && cm_info[CM_PMREL].band == tap_band) {
                    // tapped same VOACAP selection so turn off
                    refresh_map = core_map == CM_PMREL;         // refresh if going away
                    RM_CMROT (CM_PMREL);
                    insureCoreMap();
                } else {
                    // tapped a different VOACAP selection
                    refresh_map = true;                         // new band
                    cm_info[CM_PMREL].band = (PropMapBand)tap_band;
                    DO_CMROT (CM_PMREL);
                }
            } else {
                // tapped on right for TOA map
                if (IS_CMROT(CM_PMTOA) && cm_info[CM_PMTOA].band == tap_band) {
                    // tapped same VOACAP selection so turn off, insure one is still on
                    refresh_map = core_map == CM_PMTOA;         // refresh if going away
                    RM_CMROT (CM_PMTOA);
                    insureCoreMap();
                } else {
                    // tapped a different VOACAP selection
                    refresh_map = true;                         // new band
                    cm_info[CM_PMTOA].band = (PropMapBand)tap_band;
                    DO_CMROT (CM_PMTOA);
                }
            }

            // update
            if (refresh_map)
                scheduleNewCoreMap (core_map);
            plotBandConditions (b, 0, NULL, NULL);

            // save
            saveCoreMaps();
            logMapRotSet();
        }
    }

    // ours just because tap was below title
    return (true);
}

/* check if it is time to update any info via wifi.
 * proceed even if no wifi to allow subsystems to update.
 */
void updateWiFi(void)
{

    // time now
    time_t t0 = myNow();

    // update each pane
    for (int i = PANE_0; i < PANE_N; i++) {

        // too bad you can't iterate an enum
        PlotPane pp = (PlotPane)i;

        // handy
        const SBox &box = plot_b[pp];
        PlotChoice pc = plot_ch[pp];

        // rotate if this pane is rotating and it's time
        if (t0 >= next_rotation[pp] && (isPaneRotating(pp) || isSpecialPaneRotating(pp))) {

            pc = plot_ch[pp] = getNextRotationChoice(pp, plot_ch[pp]);
            next_rotation[pp] = nextRotation(pp);
            showRotatingBorder ();

            // if a choice needs fresh_redraw when newly exposed set it here
            if (pc == PLOT_CH_DXCLUSTER) fresh_redraw[PLOT_CH_DXCLUSTER] = true;
            if (pc == PLOT_CH_ADIF)      fresh_redraw[PLOT_CH_ADIF] = true;
            if (pc == PLOT_CH_ONTA)      fresh_redraw[PLOT_CH_ONTA] = true;
            if (pc == PLOT_CH_CONTESTS)  fresh_redraw[PLOT_CH_CONTESTS] = true;
            if (pc == PLOT_CH_ACTIVENETS) fresh_redraw[PLOT_CH_ACTIVENETS] = true;
            if (pc == PLOT_CH_DXPEDS)    fresh_redraw[PLOT_CH_DXPEDS] = true;
            if (pc == PLOT_CH_STORMS)    fresh_redraw[PLOT_CH_STORMS] = true;
            if (pc == PLOT_CH_LAUNCHES)  fresh_redraw[PLOT_CH_LAUNCHES] = true;

            // go now
            next_update[pp] = 0;
        }


        switch (pc) {

        case PLOT_CH_BC:
            if (t0 >= next_update[pp]) {
                if (updateBandConditions (box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, BC_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = bc_matrix.next_update;     // use schedule set by updateBandConditions
            }
            break;

        case PLOT_CH_DEWX:
            if (t0 >= next_update[pp]) {
                if (updateDEWX(box))
                    next_update[pp] = nextPaneUpdate (pc, DEWX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_DXCLUSTER:
            if (t0 >= next_update[pp]) {
                if (updateDXCluster (box, fresh_redraw[pc])) {
                    next_update[pp] = myNow() + DXC_INTERVAL;   // very fast
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry (PLOT_CH_DXCLUSTER);
            }
            break;

        case PLOT_CH_DXWX:
            if (t0 >= next_update[pp]) {
                if (updateDXWX(box))
                    next_update[pp] = nextPaneUpdate (pc, DXWX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_FLUX:
            if (t0 >= next_update[pp]) {
                if (updateSolarFlux(box))
                    next_update[pp] = nextPaneUpdate (pc, SFLUX_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_KP:
            if (t0 >= next_update[pp]) {
                if (updateKp(box))
                    next_update[pp] = nextPaneUpdate (pc, KP_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_MOON:
            if (t0 >= next_update[pp]) {
                updateMoonPane (box);           // all local -- can't fail ;-)
                next_update[pp] = nextPaneUpdate (pc, MOON_INTERVAL);
            }
            break;

        case PLOT_CH_NOAASPW:
            if (t0 >= next_update[pp]) {
                if (updateNOAASWx(box))
                    next_update[pp] = nextPaneUpdate (pc, NOAASPW_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_SSN:
            if (t0 >= next_update[pp]) {
                if (updateSunSpots(box))
                    next_update[pp] = nextPaneUpdate (pc, SSN_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_XRAY:
            if (t0 >= next_update[pp]) {
                if (updateXRay(box))
                    next_update[pp] = nextPaneUpdate (pc, XRAY_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_GIMBAL:
            if (t0 >= next_update[pp]) {
                updateGimbal(box);
                next_update[pp] = 0;            // constant poll
            }
            break;

        case PLOT_CH_TEMPERATURE:               // fallthru
        case PLOT_CH_PRESSURE:                  // fallthru
        case PLOT_CH_HUMIDITY:                  // fallthru
        case PLOT_CH_DEWPOINT:
            if (t0 >= next_update[pp]) {
                drawOneBME280Pane (box, pc);
                next_update[pp] = nextPaneUpdate (pc, ENV_INTERVAL);
            } else if (newBME280data()) {
                drawOneBME280Pane (box, pc);
            }
            break;

        case PLOT_CH_SDO:
            if (t0 >= next_update[pp]) {
                if (updateSDOPane (box))        // knows how to rotate on its own
                    next_update[pp] = nextPaneUpdate (pc, SDO_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_SOLWIND:
            if (t0 >= next_update[pp]) {
                if (updateSolarWind(box))
                    next_update[pp] = nextPaneUpdate (pc, SWIND_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_DRAP:
            if (t0 >= next_update[pp]) {
                if (updateDRAP(box))
                    next_update[pp] = nextPaneUpdate (pc, DRAPPLOT_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_COUNTDOWN:
            // handled by stopwatch system
            break;

        case PLOT_CH_CONTESTS:
            if (t0 >= next_update[pp]) {
                if (updateContests(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, CONTESTS_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_ACTIVENETS:
            if (t0 >= next_update[pp]) {
                if (updateActiveNets(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, ACTIVENETS_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_PSK:
            if (t0 >= next_update[pp]) {
                if (updatePSKReporter(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, PSK_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_BZBT:
            if (t0 >= next_update[pp]) {
                if (updateBzBt(box))
                    next_update[pp] = nextPaneUpdate (pc, BZBT_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_ONTA:
            if (t0 >= next_update[pp]) {
                if (updateOnTheAir(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, ONTA_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_SATACT:
            if (t0 >= next_update[pp]) {
                if (updateHamsat(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, 90);     // secs, matches hamsat.cpp cache age
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_ADIF:
            if (t0 >= next_update[pp]) {
                updateADIF (box, fresh_redraw[pc]);
                next_update[pp] = nextPaneUpdate (pc, ADIF_INTERVAL);
                fresh_redraw[pc] = false;
            }
            break;

        case PLOT_CH_AURORA:
            if (t0 >= next_update[pp]) {
                if (updateAurora(box))
                    next_update[pp] = nextPaneUpdate (pc, AURORA_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_HFCOND:
            if (t0 >= next_update[pp]) {
                if (updateHFConditions(box))
                    next_update[pp] = nextPaneUpdate (pc, HQ_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_VHFCOND:
            if (t0 >= next_update[pp]) {
                if (updateVHFConditions(box))
                    next_update[pp] = nextPaneUpdate (pc, HQ_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_DXPEDS:
            if (t0 >= next_update[pp]) {
                if (updateDXPeds(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, DXPEDS_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_STORMS:
            if (t0 >= next_update[pp]) {
                if (updateStorms(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, STORM_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

	case PLOT_CH_LAUNCHES:
            if (t0 >= next_update[pp]) {
                if (updateLaunches(box, fresh_redraw[pc])) {
                    next_update[pp] = nextPaneUpdate (pc, LAUNCHES_INTERVAL);
                    fresh_redraw[pc] = false;
                } else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_DST:
            if (t0 >= next_update[pp]) {
                if (updateDST(box))
                    next_update[pp] = nextPaneUpdate (pc, DST_INTERVAL);
                else
                    next_update[pp] = nextWiFiRetry(pc);
            }
            break;

        case PLOT_CH_N:
            break;              // lint
        }

    }

    // freshen NCDXF_b
    checkBRB(t0);

    // freshen RSS
    checkRSS();

    // check for new Message of the Day file on backend
    checkMOTD();

    // update lightning overlay if due
    updateLightning();

    // maps are checked after each full earth draw -- see drawMoreEarth()

    // check for server commands
    checkWebServer(false);
}

/* get next line from client in line[] then return true, else nothing and return false.
 * line[] will have \r and \n removed and end with \0, optional line length in *ll will not include \0.
 * if line is longer than line_len it will be silently truncated.
 */
bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll)
{
    // decrement available length so there's always room to add '\0'
    line_len -= 1;

    uint16_t i = 0;
    while (true) {
        char c;
        if (!getTCPChar (client, &c))
            return (false);
        if (c == '\r')
            continue;
        if (c == '\n') {
            line[i] = '\0';
            if (ll)
                *ll = i;
            // Serial.println(line);
            return (true);
        } else if (i < line_len)
            line[i++] = c;
    }
}

/* arrange for everything to update immediately
 */
void initWiFiRetry()
{
    // freshen all
    memset (next_update, 0, sizeof(next_update));
    memset (fresh_redraw, 1, sizeof(fresh_redraw));     // works ok even if bools aren't bytes

    // a few more misc
    next_map = 0;
    brb_next_update = 0;
    scheduleRSSNow();
}

/* show PLOT_CH_DEWX or PLOT_CH_DXWX in pp immediately then arrange to resume normal pp operation
 * after DXPATH_LINGER using revert_time and revert_pp.
 */
static void revertWXPane (PlotPane pp, PlotChoice wxpc)
{
    // easier to just show weather immediately without using the pane rotation system
    if (wxpc == PLOT_CH_DEWX) {
        (void) updateDEWX (plot_b[pp]);
    } else if (wxpc == PLOT_CH_DXWX) {
        (void) updateDXWX (plot_b[pp]);
    } else {
        fatalError ("revertWXPane with pc %d\n", (int)wxpc);
        return;                 // lint
    }

    // record where a revert is in progress and when it will be over.
    revert_time = next_update[pp] = myNow() + DXPATH_LINGER/1000;
    revert_pane = pp;

    // best to do a fresh redraw of original content when revert is over
    fresh_redraw[plot_ch[pp]] = true;

    // a few plot types require extra processing when shut down
    switch (plot_ch[pp]) {
    case PLOT_CH_GIMBAL:
        closeGimbal();          // will reopen after revert
        break;
    default:
        break;                  // lint
    }
}

/* request that the given pane use fresh data, if and when it next becomes visible.
 * for most panes:
 *   if the pane is currently visible: fresh update immediately;
 *   if in rotation but not visible:   it is marked for fresh update when its turn comes;
 *   if not selected anywhere:         we do nothing.
 * for PLOT_CH_DEWX or PLOT_CH_DXWX which are only for temporary display called reverting:
 *   if the pane is currently visible: it will refresh immediately;
 *   if in rotation but not visible:   immediately displayed in its pane, normal rotation after DXPATH_LINGER
 *   if not selected anywhere:         immediately displayed in PANE_1, normal rotation after DXPATH_LINGER
 */
void scheduleNewPlot (PlotChoice pc)
{
    PlotPane pp = findPaneChoiceNow (pc);
    if (pp == PANE_NONE) {
        // not currently visible ...
        pp = findPaneForChoice (pc);
        if (pp == PANE_NONE) {
            // ... and not in any rotation set either
            if (pc == PLOT_CH_DEWX || pc == PLOT_CH_DXWX) {
                if (showNewDXDEWx()) {
                    // force immediate WX in PANE_1, then revert after DXPATH_LINGER
                    revertWXPane (PANE_1, pc);
                }
            }
            // ignore all others
        } else {
            // ... but is in rotation
            if (pc == PLOT_CH_DEWX || pc == PLOT_CH_DXWX) {
                if (showNewDXDEWx()) {
                    // force immediate WX in pane pp, then revert after DXPATH_LINGER
                    revertWXPane (pp, pc);
                }
            } else {
                // just mark for fresh redraw when it's turn comes
                fresh_redraw[pc] = true;
            }
        }
    } else {
        // currently visible: force fresh redraw now
        fresh_redraw[pc] = true;
        next_update[pp] = 0;
    }
}

/* called to schedule an update of the give core map.
 */
void scheduleNewCoreMap (CoreMaps cm)
{
    if (cm == CM_NONE)
        fatalError ("Bug! setting no core map");

    // update and signal go
    DO_CMROT(cm);

    // VOACAP-derived maps: rate-limit forced fetches so user clicks can't
    // bypass the backoff and pound the backend.
    time_t now = myNow();
    if (isVOACAPMap(cm) && voacapThrottled(now)) {
        long since = (long)(now - lastVOACAPAttempt());
        long wait  = (long)VOACAP_MIN_INTERVAL - since;
        Serial.printf ("VOACAP: click for %s throttled (%ld s since last, wait %ld s)\n",
                       cm_info[cm].name, since, wait);
        mapMsg (3000, "Server Busy Please Wait");
        // schedule first fetch attempt for end of throttle window so the new
        // map will retrieve as soon as the rate limit allows
        next_map = lastVOACAPAttempt() + VOACAP_MIN_INTERVAL;
    } else {
        next_map = 0;
    }

    // persist
    saveCoreMaps();
}

/* schedule a refresh of the current map
 */
void scheduleFreshMap (void)
{
    time_t now = myNow();
    if (isVOACAPMap(core_map) && voacapThrottled(now)) {
        long since = (long)(now - lastVOACAPAttempt());
        long wait  = (long)VOACAP_MIN_INTERVAL - since;
        Serial.printf ("VOACAP: refresh of %s throttled (%ld s since last, wait %ld s)\n",
                       cm_info[core_map].name, since, wait);
        mapMsg (3000, "Server Busy Please Wait");
        // schedule first fetch attempt for end of throttle window
        next_map = lastVOACAPAttempt() + VOACAP_MIN_INTERVAL;
        return;
    }
    next_map = 0;
    scheduleMapRedraw();
}

/* return current NTP response time list.
 * N.B. this is the real data, caller must not modify.
 */
int getNTPServers (const NTPServer **listp)
{
    *listp = ntp_list;
    return (N_NTP);
}

/* return when the given pane will next update.
 */
time_t nextPaneRotation(PlotPane pp)
{
    return (next_rotation[pp]);
}

/* force the given pane to rotate now.
 * it's ok if pp is not actually rotating.
 */
void forcePaneRotation (PlotPane pp)
{
    next_rotation[pp] = 0;
}

/* return pane for which taps are to be ignored because a revert is in progress, if any
 */
PlotPane ignorePaneTouch()
{
    if (myNow() < revert_time)
        return (revert_pane);
    return (PANE_NONE);
}
