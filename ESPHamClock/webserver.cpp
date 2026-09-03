/* RESTful interface and automatic demo mode.
 * (the live webserver has been broken out to liveweb.cpp.)
 */

#include "HamClock.h"
#include <map>


// platform
#if defined (_IS_ESP8266)
const char platform[] = "ESPHamClock";
#elif defined(_IS_ANDROID) || defined(__ANDROID__)
char platform[32] = "HamClock-android";
#elif defined(_IS_LINUX_ARMBIAN)
const char platform[] = "HamClock-armbian";
#elif defined(_IS_LINUX_RPI)
const char platform[] = "HamClock-rpi";
#elif defined(_IS_LINUX)
const char platform[] = "HamClock-linux";
#elif defined (_IS_APPLE)
const char platform[] = "HamClock-apple";
#elif defined (_IS_FREEBSD)
const char platform[] = "HamClock-FreeBSD";
#elif defined (_IS_NETBSD)
const char platform[] = "HamClock-NetBSD";
#else
const char platform[] = "HamClock-UNIX";
#endif


#if defined(__GNUC__)
static void sendHTTPError (WiFiClient &client, const char *fmt, ...) __attribute__ ((format(__printf__,2,3)));
static void demoMsg (bool ok, int n, char buf[], size_t buf_len, const char *fmt, ...)
        __attribute__ ((format (__printf__, 5, 6)));
#else
static void sendHTTPError (WiFiClient &client, const char *fmt, ...);
static void demoMsg (bool ok, int n, char buf[], size_t buf_len, const char *fmt, ...);
#endif



// persistent server listening for restful connections
static WiFiServer *restful_server;
int restful_port = RESTFUL_PORT;

// captured from header Content-Length if available; handy for readings POSTs
static long content_length;

// handy default message strings
static const char garbcmd[] = "Garbled command";
static const char notsupp[] = "Not supported";


// list of DEMO mode choice codes
typedef enum {
    // 0
    DEMO_PANE1,
    DEMO_PANE2,
    DEMO_PANE3,
    DEMO_RSS,
    DEMO_NEWDX,

    // 5
    DEMO_MAPPROJ,
    DEMO_MAPNIGHT,
    DEMO_MAPGRID,
    DEMO_MAPSTYLE,
    DEMO_NCDXF,

    // 10
    DEMO_CALLFG,
    DEMO_CALLBG,
    DEMO_DEFMT,
    DEMO_ONAIR,
    DEMO_SAT,

    // 15
    DEMO_EME,
    DEMO_AUXTIME,
    DEMO_PSKMASK,
    DEMO_PANE0,

    DEMO_N,

} DemoChoice;
static bool runDemoChoice (DemoChoice choice, bool &slow, char msg[], size_t msg_len);


// some color names culled from rgb.txt
typedef struct {
    uint8_t r, g, b;
    char name[11];
} ColorName;
static ColorName cnames[] = {
    {  0, 255, 255,	"aqua"},
    {127, 255, 212,	"aquamarine"},
    {240, 255, 255,	"azure"},
    {245, 245, 220,	"beige"},
    {255, 228, 196,	"bisque"},
    {  0,   0,   0,	"black"},
    {  0,   0, 255,	"blue"},
    {165,  42,  42,	"brown"},
    {222, 184, 135,	"burlywood"},
    {127, 255,   0,	"chartreuse"},
    {210, 105,  30,	"chocolate"},
    {255, 127,  80,	"coral"},
    {255, 248, 220,	"cornsilk"},
    {220,  20,  60,	"crimson"},
    {  0, 255, 255,	"cyan"},
    {178,  34,  34,	"firebrick"},
    {255,   0, 255,	"fuchsia"},
    {220, 220, 220,	"gainsboro"},
    {255, 215,   0,     "gold"},
    {218, 165,  32,	"goldenrod"},
    {  0, 255,   0,	"green"},
    {240, 255, 240,	"honeydew"},
    { 75,   0, 130,	"indigo"},
    {255, 255, 240,	"ivory"},
    {240, 230, 140,	"khaki"},
    {230, 230, 250,	"lavender"},
    {  0, 255,   0,	"lime"},
    {250, 240, 230,	"linen"},
    {255,   0, 255,	"magenta"},
    {176,  48,  96,	"maroon"},
    {255, 228, 181,	"moccasin"},
    {  0,   0, 128,	"navy"},
    {128, 128,   0,	"olive"},
    {255, 165,   0,	"orange"},
    {218, 112, 214,	"orchid"},
    {205, 133,  63,	"peru"},
    {255, 192, 203,	"pink"},
    {221, 160, 221,	"plum"},
    {160,  32, 240,	"purple"},
    {255,   0,   0,	"red"},
    {250, 128, 114,	"salmon"},
    {255, 245, 238,	"seashell"},
    {160,  82,  45,	"sienna"},
    {192, 192, 192,	"silver"},
    {255, 250, 250,	"snow"},
    {210, 180, 140,	"tan"},
    {  0, 128, 128,	"teal"},
    {216, 191, 216,	"thistle"},
    {255,  99,  71,	"tomato"},
    { 64, 224, 208,	"turquoise"},
    {238, 130, 238,	"violet"},
    {245, 222, 179,	"wheat"},
    {255, 255, 255,	"white"},
    {255, 255,   0,	"yellow"},
};

/* lookup some common color names
 */
static bool findColorName (const char *name, int &r, int &g, int &b)
{
    for (int i = 0; i < NARRAY(cnames); i++) {
        if (strcmp (name, cnames[i].name) == 0) {
            r = cnames[i].r;
            g = cnames[i].g;
            b = cnames[i].b;
            return (true);
        }
    }

    return (false);
}

/* print list of colors to the given client
 */
static void printColorNames (WiFiClient &client)
{
    for (int i = 0; i < NARRAY(cnames); i++)
        client.println (cnames[i].name);
}

static bool rgbOk (int r, int g, int b)
{
    return (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255);
}

/* find name within wa and set its value.
 * return true if ok, else false with reason in ynot[].
 * N.B. we accommodate name and value pointing within ynot[].
 */
static bool setWCValue (WebArgs &wa, const char *name, const char *value, char ynot[], size_t ynot_len)
{
    // name is required
    if (!name || name[0] == '\0') {
        strcpy (ynot, "missing name");
        return (false);
    }

    // copy name in case it is within ynot -- N.B. free() before leaving!
    char *name_copy = strdup (name);

    // find name
    for (int i = 0; i < wa.nargs; i++) {
        if (strcmp (name_copy, wa.name[i]) == 0) {
            // found, but beware dup
            if (wa.found[i]) {
                snprintf (ynot, ynot_len, "%s set more than once", name_copy);
                free (name_copy);
                return (false);
            }
            wa.value[i] = value;
            wa.found[i] = true;
            free (name_copy);
            return (true);
        }
    }

    snprintf (ynot, ynot_len, "unknown arg: %s", name_copy);

    free (name_copy);
    return (false);
}

/* given wa containing all expected names, parse the given web GET command for each of their values, if any.
 * line is one or more of <empty> A or A=x separated by &.
 * we assume line[] starts just after "GET /xxx?" and has the trailing http/n removed.
 * Notes:
 *   line will be modified by changing all '&' and '=' to '\0'.
 *   all value pointers will point into line[].
 *   value will be NULL if no '=' or "" or nothing after '='.
 * return true if ok else false with excuse in back in line
 */
bool parseWebCommand (WebArgs &wa, char line[], size_t line_len)
{
    // init all unknown
    memset (wa.value, 0, sizeof (wa.value));
    memset (wa.found, 0, sizeof (wa.found));

    // parser states
    typedef enum {
        PWC_LOOKING_4_NAME,
        PWC_LOOKING_4_VALUE,
    } PWCState;
    PWCState state = PWC_LOOKING_4_NAME;

    // init working name and value pointers
    char *name = line, *value = NULL;
    char *line0 = line;

    // scan entire line -- N.B. this loop returns when it finds '\0'
    for (; ; line++) {
        switch (state) {
        case PWC_LOOKING_4_NAME:
            if (*line == '=') {
                if (name[0] == '\0') {
                    quietStrncpy (line0, "missing name", line_len);
                    return (false);
                }
                *line = '\0';                                   // terminate name
                value = line + 1;                               // start value
                state = PWC_LOOKING_4_VALUE;                    // now look for value
            } else if (*line == '&') {
                *line = '\0';                                   // terminate name
                if (!setWCValue (wa, strTrimAll(name), NULL, line0, line_len))
                    return (false);
                name = line + 1;                                // start next name
                value = NULL;                                   // no value yet
            } else if (*line == '\0') {
                if (name[0] == '\0')
                    return (true);                              // no name at all is ok
                return (setWCValue (wa, strTrimAll(name), strTrimAll(value), line0, line_len));
            }
            break;

        case PWC_LOOKING_4_VALUE:
            if (*line == '&') {
                *line = '\0';                                   // terminate value
                if (!setWCValue (wa, strTrimAll(name), strTrimAll(value), line0, line_len))
                    return (false);
                name = line + 1;                                // start next name
                value = NULL;                                   // no value yet
                state = PWC_LOOKING_4_NAME;                     // now look for name
            } else if (*line == '\0') {
                return (setWCValue (wa, strTrimAll(name), strTrimAll(value), line0, line_len));
            }
            break;
        }
    }

    // should never get here
    quietStrncpy (line0, "Bogus syntax", line_len);
    return (false);
}


/* convert the given string to integer.
 * return whether the string was entirely valid digits.
 */
static bool atoiOnly (const char *str, int *ip)
{
    if (str == NULL)
        return (false);

    char *endp;
    int i = (int) strtol (str, &endp, 10);
    if (*str != '\0' && *endp == '\0') {
        *ip = i;
        return (true);
    }

    return (false);
}


/* convert the given string to float.
 * return whether the string was entirely valid digits.
 */
static bool atofOnly (const char *str, float *fp)
{
    if (str == NULL)
        return (false);

    char *endp;
	float f;
    f = (float) strtod (str, &endp);
    if (*str != '\0' && *endp == '\0') {
        *fp = f;
        return (true);
    }

    return (false);
}

/* convert a hex digit to its numeric value.
 * N.B. assumes ASCII encoding.
 * N.B. we do no range checking.
 */
static int hex2Int (char x)
{
        if (x <= '9')
            return (x - '0');
        else
            return (toupper(x) - 'A' + 10);
}

/* replace all "%XX" or "\xXX" with hex value and + with space, IN PLACE.
 * return whether any such changes were performed.
 */
static bool replaceEncoding (char *from)
{
    bool mod = false;

    char *to = from;
    while (*from) {
        if (from[0] == '+') {
            *to++ = ' ';
            from += 1;
            mod = true;
        } else if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            *to++ = 16*hex2Int(from[1]) + hex2Int(from[2]);
            from += 3;
            mod = true;
        } else if (from[0] == '\\' && from[1] == 'x' && isxdigit(from[2]) && isxdigit(from[3])) {
            *to++ = 16*hex2Int(from[2]) + hex2Int(from[3]);
            from += 4;
            mod = true;
        } else {
            *to++ = *from++;
        }
    }
    *to = '\0';

    return (mod);
}

/* send initial response indicating body will be plain text
 */
static void startPlainText (WiFiClient &client)
{
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: text/plain; charset=us-ascii");
    client.println ("Connection: close\r\n");             // include extra blank line
}

/* send the given message as HTTP error 400 Bad request.
 * N.B. we expect the resulting message to include a final newline.
 */
static void sendHTTPError (WiFiClient &client, const char *fmt, ...)
{
    // expand
    StackMalloc errmsg_pool(200);
    char *errmsg = (char *) errmsg_pool.getMem();
    size_t em_len = errmsg_pool.getSize();
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (errmsg, em_len, fmt, ap);
    va_end (ap);

    // preserve locally
    Serial.print (errmsg);

    // send to client
    client.println ("HTTP/1.0 400 Bad request");
    sendUserAgent (client);
    client.println ("Content-Type: text/plain; charset=us-ascii");
    client.println ("Connection: close\r\n");             // include extra blank line
    client.print (errmsg);
}

/* report all choices for the given pane to client
 */
static void reportPaneChoices (WiFiClient &client, PlotPane pp)
{
    // which pane
    char buf[100];
    snprintf (buf, sizeof(buf), "Pane%d    ", (int)pp);
    client.print(buf);

    // current choice
    PlotChoice pc_now = plot_ch[pp];

    // print rotset, flagging current
    if (pc_now == PLOT_CH_NONE) {                               // possible in PANE_0
        client.println(" off");
    } else {
        // list current first then any others in rotset
        snprintf (buf, sizeof(buf), " %s", plot_names[pc_now]);
        client.print (buf);
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (i != pc_now && plot_rotset[pp] & PLOTBIT(i)) {
                snprintf (buf, sizeof(buf), " %s", plot_names[i]);
                client.print (buf);
            }
        }

        // show next rotation, if any
        if (isPaneRotating(pp)) {
            snprintf (buf, sizeof(buf), " rotating in %ld seconds", (long)(next_update[pp] - myNow()));
            client.print (buf);
        }

        // done
        client.println();
    }
}

/* format the current stopwatch state into buf
 */
static void getStopwatchReport (char buf[], size_t bufl)
{
    // get current state and times
    uint32_t sw, cd;    // millis
    SWEngineState sws = getSWEngineState (&sw, &cd);

    // break out times, sw and cd end up as remainders in ms
    int sw_hr = sw/(1000*3600);
    sw -= sw_hr*(1000*3600);
    int sw_mn = sw/(1000*60);
    sw -= sw_mn*(1000*60);
    int sw_sc = sw/1000;
    sw -= sw_sc*1000;

    int cd_hr = cd/(1000*3600);
    cd -= cd_hr*(1000*3600);
    int cd_mn = cd/(1000*60);
    cd -= cd_mn*(1000*60);
    int cd_sc = cd/1000;
    cd -= cd_sc*1000;

    // format state
    const char *state_name = "?";
    switch (sws) {
    case SWE_RESET:     state_name = "Reset";     break;
    case SWE_RUN:       state_name = "Running";   break;
    case SWE_STOP:      state_name = "Stopped";   break;
    case SWE_LAP:       state_name = "Lap";       break;
    case SWE_COUNTDOWN: state_name = "Countdown"; break;
    }

    // format report
    size_t bl = snprintf (buf, bufl, "%s %02d:%02d:%02d.%02d",
                                                state_name, sw_hr, sw_mn, sw_sc, (int)(sw/10));
    if (sws == SWE_COUNTDOWN)
        bl += snprintf (buf+bl, bufl-bl, " / %02d:%02d:%02d", cd_hr, cd_mn, cd_sc);
    buf[bl++] = '\n';
    buf[bl] = '\0';
}

/* send screen capture as bmp file
 */
static bool getWiFiCaptureBMP(WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // hack around frame buffer readback weirdness that requires ignoring the very first pixel
    static bool first_pixel = true;

    // build BMP header
    const int bpp = sizeof(uint16_t);                   // bytes per pixel
    uint8_t *hdr;                                       // must free!
    int hdr_len;
    int n_bytes;                                        // total bytes in file
    createBMP565Header (hdr, hdr_len, n_bytes, BUILD_W, BUILD_H);
    
    // send the web page header
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: image/bmp");
    client.println ("Cache-Control: no-cache");
    client.print ("Content-Length: "); client.println (n_bytes);
    client.println ("Connection: close\r\n");
    // Serial.println("web header sent");

    // send the image header then free
    client.write (hdr, hdr_len);
    free (hdr);

    // send the RGB565 pixels
    tft.graphicsMode();
    tft.setXY(0,0);
    tft.writeCommand(RA8875_MRWC);
    if (first_pixel) {
        // skip first pixel first time
        tft.readData();
        tft.readData();
        first_pixel = false;
    }
    for (int y = 0; y < BUILD_H; y++) {                 // increment by pixel

        // fill and send each row
        char row[BUILD_W*bpp];                          // 2-byte pixels
        for (int x = 0; x < BUILD_W*2; x += bpp) {      // increment by byte
            // read 2-byte pixels swap bytes
            row[x+1] = tft.readData();
            row[x+0] = tft.readData();
        }
        client.write ((uint8_t*)row, BUILD_W*bpp);
    }

    // never fails :-)
    return (true);
}

/* helper to report DE or DX info which are very similar
 */
static bool getWiFiDEDXInfo_helper (WiFiClient &client, char line[], size_t line_len, bool want_de)
{
    (void)(line);
    (void)(line_len);

    char buf[100];

    // handy which
    TZInfo &tz  =        want_de ? dx_tz : de_tz;
    LatLong &ll =        want_de ? dx_ll : de_ll;
    const char *prefix = want_de ? "DX_" : "DE_";
    NV_Name nv_grid =    want_de ? NV_DX_GRID : NV_DE_GRID;
    int tzs = getTZ(tz);

    // start response
    startPlainText(client);


    // report prefix and path if dx else de call
    if (want_de) {
        // show prefix
        char prefix[MAX_PREF_LEN];
        if (getDXPrefix(prefix)) {
            snprintf (buf, sizeof(buf), "DX_prefix     %s\n", prefix);
            client.print(buf);
        }

        // show short path info
        float dist, B;
        propDEPath (false, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        bool B_ismag = desiredBearing (de_ll, B);
        if (showDistKm())
            dist *= KM_PER_MI;
        client.print ("DX_path_SP    ");
        snprintf (buf, sizeof(buf), "%.0f %s @ %.0f deg %s\n", dist, showDistKm() ? "km" : "mi", B,
                B_ismag ? "magnetic" : "true");
        client.print (buf);

        // show long path info
        propDEPath (true, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        B_ismag = desiredBearing (de_ll, B);
        if (showDistKm())
            dist *= KM_PER_MI;
        client.print ("DX_path_LP    ");
        snprintf (buf, sizeof(buf), "%.0f %s @ %.0f deg %s\n", dist, showDistKm() ? "km" : "mi", B,
                B_ismag ? "magnetic" : "true");
        client.print (buf);

    } else {
        snprintf (buf, sizeof(buf), "Call          %s\n", getCallsign());
        client.print (buf);
    }

    // report local time
    time_t t = nowWO();
    time_t local = t + tzs;
    int yr = year (local);
    int mo = month(local);
    int dy = day(local);
    int hr = hour (local);
    int mn = minute (local);
    int sc = second (local);
    snprintf (buf, sizeof(buf), "%stime       %d-%02d-%02dT%02d:%02d:%02d\n", prefix,yr,mo,dy,hr,mn,sc);
    client.print (buf);

    // report timezone
    snprintf (buf, sizeof(buf), "%stz         UTC%+g\n", prefix, tzs/3600.0);
    client.print (buf);

    // report lat
    snprintf (buf, sizeof(buf), "%slat        %.2f deg\n", prefix, ll.lat_d);
    client.print (buf);

    // report lng
    snprintf (buf, sizeof(buf), "%slng        %.2f deg\n", prefix, ll.lng_d);
    client.print (buf);

    // report grid
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv_grid, maid);
    snprintf (buf, sizeof(buf), "%sgrid       %s\n", prefix, maid);
    client.print (buf);



    // get moon info
    time_t rise, set;
    AstroCir cir;
    getLunarRS (t, ll, &rise, &set);
    getLunarCir (t, ll, cir);

    // report moon rise
    snprintf (buf, sizeof(buf), "%sMoonAz     %.1f deg\n", prefix, rad2deg(cir.az));
    client.print(buf);
    snprintf (buf, sizeof(buf), "%sMoonEl     %.1f deg\n", prefix, rad2deg(cir.el));
    client.print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), "%sMoonRise   %02d:%02d\n", prefix, hour(rise+tzs), minute (rise+tzs));
    } else {
        snprintf (buf, sizeof(buf), "%sMoonRise   none\n", prefix);
    }
    client.print(buf);

    // report moon set
    if (set > 0) {
        snprintf (buf, sizeof(buf), "%sMoonSet    %02d:%02d\n", prefix, hour(set+tzs), minute (set+tzs));
    } else {
        snprintf (buf, sizeof(buf), "%sMoonSet    none\n", prefix);
    }
    client.print(buf);

    // print moon velocity
    snprintf (buf, sizeof(buf), "%sMoonVel    %.1f m/s\n", prefix, cir.vel);
    client.print(buf);



    // get sun info
    getSolarRS (t, ll, &rise, &set);
    getSolarCir (t, ll, cir);

    // report sun rise
    snprintf (buf, sizeof(buf), "%sSunAz      %.1f deg\n", prefix, rad2deg(cir.az));
    client.print(buf);
    snprintf (buf, sizeof(buf), "%sSunEl      %.1f deg\n", prefix, rad2deg(cir.el));
    client.print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), "%sSunRise    %02d:%02d\n", prefix, hour(rise+tzs), minute (rise+tzs));
    } else {
        snprintf (buf, sizeof(buf), "%sSunRise    none\n", prefix);
    }
    client.print(buf);

    // report sun set
    if (set > 0) {
        snprintf (buf, sizeof(buf), "%sSunSet     %02d:%02d\n", prefix, hour(set+tzs), minute (set+tzs));
    } else {
        snprintf (buf, sizeof(buf), "%sSunSet     none\n", prefix);
    }
    client.print(buf);


    // get weather
    StackMalloc wxi_mem(sizeof(WXInfo));
    WXInfo *wip = (WXInfo *) wxi_mem.getMem();
    Message ynot;
    if (getCurrentWX (ll, want_de, wip, ynot)) {
        float x;

        x = showTempC() ? wip->temperature_c : CEN2FAH(wip->temperature_c);
        snprintf (buf, sizeof(buf), "%sWxTemp     %.1f %c\n", prefix, x, showTempC() ? 'C' : 'F');
        client.print(buf);

        x = showATMhPa() ? wip->pressure_hPa : HPA2INHG(wip->pressure_hPa);
        snprintf (buf, sizeof(buf), "%sWxPressure %.2f %s %s\n", prefix, x,
            showATMhPa() ? "hPa" : "inHg",
            wip->pressure_chg < 0 ? "falling" : (wip->pressure_chg > 0 ? "rising" : "steady"));
        client.print(buf);

        snprintf (buf, sizeof(buf), "%sWxHumidity %.1f %%\n", prefix, wip->humidity_percent);
        client.print(buf);

        x = (showDistKm() ? 3.6F : 2.237F) * wip->wind_speed_mps; // kph or mph
        snprintf (buf, sizeof(buf), "%sWxWindSpd  %.1f %s\n", prefix, x, showDistKm()?"kph":"mph");
        client.print(buf);

        snprintf (buf, sizeof(buf), "%sWxWindDir  %s\n", prefix, wip->wind_dir_name);
        client.print(buf);

        snprintf (buf, sizeof(buf), "%sWxClouds   %s\n", prefix, wip->clouds);
        client.print(buf);

        snprintf (buf, sizeof(buf), "%sWxCondx    %s\n", prefix, wip->conditions);
        client.print(buf);

        snprintf (buf, sizeof(buf), "%sWxFrom     %s\n", prefix, wip->attribution);
        client.print(buf);

    } else {
        client.print(prefix);
        client.print("WxErr      ");
        client.println(ynot.get());
    }

    // ok
    return (true);
}

/* remote report DE info
 */
static bool getWiFiDEInfo (WiFiClient &client, char line[], size_t line_len)
{
    return (getWiFiDEDXInfo_helper (client, line, line_len, false));
}

/* remote report DX info
 */
static bool getWiFiDXInfo (WiFiClient &client, char line[], size_t line_len)
{
    return (getWiFiDEDXInfo_helper (client, line, line_len, true));
}

/* helper to print list of tx DXSpot.
 */
static void spotsHelper (WiFiClient &client, const DXSpot *spots, int nspots, char *buf, int buf_len)
{
    // print each row
    client.print ("#  kHz   Call        UTC     Mode Grid      Lat     Lng     DEDist   DEBearing\n");
    for (uint8_t i = 0; i < nspots; i++) {

        const DXSpot &spot = spots[i];

        // start with pretty freq, fixed 8 chars
        const char *f_fmt = spot.kHz < 1e6 ? "%8.1f" : "%8.0f";
        int bufl = snprintf (buf, buf_len, f_fmt, spot.kHz);

        // distance and bearing from DE
        float dist, bear;
        propDEPath (show_lp, spot.tx_ll, &dist, &bear);
        dist *= ERAD_M;                                 // angle to miles
        bear *= 180/M_PIF;                              // rad -> degrees
        bool bear_ismag = desiredBearing (de_ll, bear);
        if (showDistKm())
            dist *= KM_PER_MI;

        // add remaining fields
        snprintf (buf+bufl, buf_len-bufl, " %-*s %02d%02d %7s %6s %7.2f %7.2f   %6.0f   %4.0f %s\n",
                MAX_SPOTCALL_LEN-1, spot.tx_call, hour(spot.spotted), minute(spot.spotted), spot.mode,
                spot.tx_grid, spot.tx_ll.lat_d, spot.tx_ll.lng_d, dist, bear,
                bear_ismag ? "magnetic" : "true");

        // print
        client.print(buf);
    }
}


/* report current set of DX spots
 */
static bool getWiFiDXSpots (WiFiClient &client, char line[], size_t line_len)
{
    // start reply
    startPlainText (client);

    // retrieve spots, if available
    DXSpot *spots;
    uint8_t nspots;
    if (!getDXClusterSpots (&spots, &nspots)) {
        strcpy (line, "No dx spots");
        return (false);
    }

    // list
    spotsHelper (client, spots, nspots, line, line_len);

    return (true);

}

/* report next solar eclipse local circumstances for DE
 */
static bool getWiFiEclipse (WiFiClient &client, char line[], size_t line_len)
{
    (void)(line_len);

    EclipseCir ec;
    if (!getNextSolarEclipse (nowWO(), de_ll, 36, ec)) {
        strcpy (line, "No eclipse found in search window");
        return (false);
    }

    // send html header
    startPlainText (client);

    static const char *tnames[] = {"none", "partial", "annular", "total"};
    char buf[200];
    snprintf (buf, sizeof(buf), "Type          %s\n", tnames[ec.type]);
    client.print (buf);
    snprintf (buf, sizeof(buf), "Max_UTC       %4d-%02d-%02dT%02d:%02d:%02dZ\n",
                year(ec.t_max), month(ec.t_max), day(ec.t_max),
                hour(ec.t_max), minute(ec.t_max), second(ec.t_max));
    client.print (buf);
    if (ec.t_c1) {
        snprintf (buf, sizeof(buf), "C1_UTC        %4d-%02d-%02dT%02d:%02d:%02dZ\n",
                    year(ec.t_c1), month(ec.t_c1), day(ec.t_c1),
                    hour(ec.t_c1), minute(ec.t_c1), second(ec.t_c1));
        client.print (buf);
    }
    if (ec.t_c4) {
        snprintf (buf, sizeof(buf), "C4_UTC        %4d-%02d-%02dT%02d:%02d:%02dZ\n",
                    year(ec.t_c4), month(ec.t_c4), day(ec.t_c4),
                    hour(ec.t_c4), minute(ec.t_c4), second(ec.t_c4));
        client.print (buf);
    }
    snprintf (buf, sizeof(buf), "Magnitude     %.3f\n", ec.magnitude);
    client.print (buf);
    snprintf (buf, sizeof(buf), "Obscuration   %.1f%%\n", ec.obscuration*100);
    client.print (buf);
    snprintf (buf, sizeof(buf), "Sun_el_deg    %.1f\n", ec.sun_el*180/M_PIF);
    client.print (buf);
    snprintf (buf, sizeof(buf), "Sun_up        %s\n", ec.visible ? "yes" : "no");
    client.print (buf);

    return (true);
}

/* report current set of all the given On The Air program activators
 */
static bool getWiFiOnTheAir (WiFiClient &client, char line[], size_t line_len)
{
    // start reply
    startPlainText (client);

    // retrieve spots, if available
    DXSpot *spots;
    uint8_t nspots;
    if (!getOnTheAirSpots (&spots, &nspots)) {
        strcpy (line, "No ONTA spots");
        return (false);
    }

    // list
    spotsHelper (client, spots, nspots, line, line_len);

    return (true);
}

/* report current Live Spots stats, if active
 */
static bool getWiFiLiveStats (WiFiClient &client, char *line, size_t line_len)
{
    PSKBandStats stats[HAMBAND_N];
    const char *names[HAMBAND_N];
    if (!getPSKBandStats (stats, names)) {
        snprintf (line, line_len, "No live stats");
        return (false);
    }

    // start reply
    startPlainText (client);

    // heading
    char buf[100];
    if (showDistKm())
        client.print ("# Band Count MaxKm    @Lat    @Lng\n");
    else
        client.print ("# Band Count MaxMi    @Lat    @Lng\n");

    // table
    for (int i = 0; i < HAMBAND_N; i++) {
        PSKBandStats &s = stats[i];
        snprintf (buf, sizeof(buf), "%-6s %5d %5.0f %7.2f %7.2f\n", names[i], s.count,
                showDistKm() ? s.maxkm : s.maxkm / KM_PER_MI, s.maxll.lat_d, s.maxll.lng_d);
        client.print(buf);
    }

    // done
    return (true);
}

/* remote report current known set of contests.
 */
static bool getWiFiContests (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // fetch
    char **titles, **dates;
    int n_con = getContests (titles, dates);

    // start reply
    startPlainText (client);

    if (n_con > 0) {

        // print and free
        for (int i = 0; i < n_con; i++) {
            char str[1000];
            snprintf (str, sizeof(str), "%-40s %s\n", titles[i], dates[i]);
            client.print (str);
            free (titles[i]);
            free (dates[i]);
        }

        free (titles);
        free (dates);

    } else {
        client.print ("no contests until pane is opened\n");
    }

    // done
    return (true);

}

/* get a user GPIO pin
 */
static bool getWiFiGPIO (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "pin";
    wa.name[wa.nargs++] = "latched";    // optional, false by default

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // get pin
    int pin = 0;
    if (wa.found[0])
        pin = atoi (wa.value[0]);
    else {
        snprintf (line, line_len, "MCP pin is required");
        return (false);
    }

    // get whether latched, default false
    bool latched = false;
    if (wa.found[1]) {
        if (strcasecmp (wa.value[1], "true") == 0)
            latched = true;
        else if (strcasecmp (wa.value[1], "false") != 0) {
            snprintf (line, line_len, "latched true or false");
            return (false);
        }
    }

    // get state
    Message ynot;
    bool state;
    if (!getUserGPIO (pin, latched, state, ynot)) {
        snprintf (line, line_len, "%s", ynot.get());
        return (false);
    }

    // reply with state
    startPlainText (client);
    client.println (state ? 1 : 0);
    return (true);
}


/* remote report current known set of DXpeditions
 */
static bool getWiFiDXPeds (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // fetch
    char **titles, **dates;
    int n_dxp = getDXPeds (titles, dates);

    // start reply
    startPlainText (client);

    if (n_dxp > 0) {

        // print and free
        for (int i = 0; i < n_dxp; i++) {
            char str[1000];
            snprintf (str, sizeof(str), "%-40s %s\n", titles[i], dates[i]);
            client.print (str);
            free (titles[i]);
            free (dates[i]);
        }

        free (titles);
        free (dates);

    } else {
        client.print ("no expeditions until pane is opened\n");
    }

    // done
    return (true);

}

/* remote report some basic clock configuration
 */
static bool getWiFiConfig (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    const __FlashStringHelper *not_sup = "N/A";
    char buf[100];
    size_t nbuf;

    // start reply
    startPlainText (client);

    // report whether screen is locked
    client.print ("Screen    ");
    if (screenIsLocked())
        client.println ("locked");
    else
        client.println ("unlocked");


    // report active map styles, starting with core_map
    client.print ("MapStyles ");
    for (int i = 0; i < CM_N; i++) {
        int ci = (core_map + i) % CM_N;                 // list starting with core_map
        if (IS_CMROT(ci)) {
            snprintf (buf, sizeof(buf), "%s ", getCoreMapStyle ((CoreMaps)ci, buf));
            client.print (buf);
        }
    }
    if (mapIsRotating()) {
        snprintf (buf, sizeof(buf), " rotation %d secs", getMapRotationPeriod());
        client.print (buf);
    }
    if (!night_on)
        client.print (" without night");
    client.println();


    // report map projection
    snprintf (buf, sizeof(buf), "MapProj   %s\n", map_projnames[map_proj]);
    client.print(buf);

    // report grid overlay
    snprintf (buf, sizeof(buf), "MapGrid   %s\n", grid_styles[mapgrid_choice]);
    client.print(buf);

    // report merc center longitude
    snprintf (buf, sizeof(buf), "MerCenter %d degs\n", getCenterLng());
    client.print(buf);

    // report panes
    for (int pp = PANE_0; pp < PANE_N; pp++)
        reportPaneChoices (client, (PlotPane)pp);

    // report psk if active
    client.print ("LiveSpots ");
    if (findPaneForChoice(PLOT_CH_PSK) != PANE_NONE) {
        bool ispsk = (psk_mask & PSKMB_SRCMASK) == PSKMB_PSK;
        bool iswspr = (psk_mask & PSKMB_SRCMASK) == PSKMB_WSPR;
        snprintf (buf, sizeof(buf), "%s DE %s using %s\n", 
                            psk_mask & PSKMB_OFDE ? "of" : "by",
                            psk_mask & PSKMB_CALL ? "call" : "grid",
                            (ispsk ? "PSK" : (iswspr ? "WSPR" : "RBN")));
    } else {
        strcpy (buf, "off\n");
    }
    client.print(buf);

    // report each selected NCDXF beacon box state
    client.print ("NCDXF     ");
    for (unsigned i = 0; i < BRB_N; i++) {
        int j = (brb_mode + i) % BRB_N; // start list with current
        if (brb_rotset & (1 << j)) {
            if (j != brb_mode)
                client.print (", ");
            client.print(brb_names[j]);
        }
    }
    if (BRBIsRotating())
        snprintf (buf, sizeof(buf), " rotating in %ld seconds",  (long)(brb_next_update - myNow()));
    else
        buf[0] = '\0';
    client.println (buf);

    // report display brightness and timers
    uint16_t pcon, t_idle, t_idle_left;
    client.print ("Bright    ");
    if (getDisplayInfo (pcon, t_idle, t_idle_left)) {

        // display brightness info
        if (brDimmableOk())
            snprintf (buf, sizeof(buf), "%d <= %d <= %d%%\n", getBrMin(), pcon, getBrMax());
        else
            snprintf (buf, sizeof(buf), "%d%%\n", pcon);
        client.print (buf);

        // display dimming info
        client.print ("AutoDim   ");
        if (t_idle == 0)
            strcpy (buf, "off");
        else {
            nbuf = snprintf (buf, sizeof(buf), "%d mins, off ", t_idle);
            if (t_idle_left > 0) {
                int mn = t_idle_left/60;
                int sc = t_idle_left - mn*60;
                snprintf (buf+nbuf, sizeof(buf)-nbuf, "in %02d:%02d", mn, sc);
            } else {
                snprintf (buf+nbuf, sizeof(buf)-nbuf, "now");
            }
        }
        client.println (buf);

        // display on/off time for each day
        for (int dow = 1; dow <= DAYSPERWEEK; dow++) {
            uint16_t on_mins, off_mins;
            if (getDisplayOnOffTimes (dow, on_mins, off_mins)) {
                snprintf (buf, sizeof(buf), "OnOff_%s %02d:%02d %02d:%02d\n", dayShortStr(dow),
                                    on_mins/60, on_mins%60, off_mins/60, off_mins%60);
                client.print (buf);
            } else
                break;
        }

    } else
        client.println ( not_sup);


    // report daily alarm
    client.print ("Alarm     ");
    AlarmState d_as;
    uint16_t d_dehr, d_demn;
    bool utc;
    getDailyAlarmState (d_as, d_dehr, d_demn, utc);
    const char *tzone = utc ? "UTC" : "DE";
    switch (d_as) {
    case ALMS_OFF:
        snprintf (buf, sizeof(buf), "Off (%s %02d:%02d)\n", tzone, d_dehr, d_demn);
        break;
    case ALMS_ARMED:
        snprintf (buf, sizeof(buf), "Armed for %s %02d:%02d\n", tzone, d_dehr, d_demn);
        break;
    case ALMS_RINGING:
        snprintf (buf, sizeof(buf), "Ringing since %s %02d:%02d\n", tzone, d_dehr, d_demn);
        break;
    }
    client.print (buf);

    // report once-only alarm
    client.print ("One Alarm ");
    AlarmState wo_as;
    time_t wo_t;
    bool wo_utc;
    char wo_str[50];
    getOneTimeAlarmState (wo_as, wo_t, wo_utc, wo_str, sizeof(wo_str));
    switch (wo_as) {
    case ALMS_OFF:
        snprintf (buf, sizeof(buf), "Off (%s)\n", wo_str);
        break;
    case ALMS_ARMED:
        snprintf (buf, sizeof(buf), "Armed for %s\n", wo_str);
        break;
    case ALMS_RINGING:
        snprintf (buf, sizeof(buf), "Ringing since %s\n", wo_str);
        break;
    }
    client.print (buf);



    // report stopwatch
    getStopwatchReport (buf, sizeof(buf));
    client.print ("Stopwatch ");
    client.print (buf);


    // time source
    client.print ("TimeFrom  ");
    if (useOSTime())
        snprintf (buf, sizeof(buf), "host computer\n");
    else if (useGPSDTime())
        snprintf (buf, sizeof(buf), "GPSD %s\n", getGPSDHost());
    else if (useNMEATime())
        snprintf (buf, sizeof(buf), "NMEA %s\n", getNMEAFile());
    else
        snprintf (buf, sizeof(buf), "NTP %s\n", findBestNTP()->server);
    client.print(buf);

    // gpsd?
    client.print ("GPSD      ");
    if (useGPSDTime() || useGPSDLoc()) {
        client.print ("get");
        if (useGPSDTime())
            client.print (" time");
        if (useGPSDLoc())
            client.print (" location");
        client.print (" from ");
        client.println (getGPSDHost());
    } else {
        client.println ("off");
    }

    // gimbal activity
    bool gconn, vis_now, has_el, gstop, gauto;
    float az, el;
    bool at_all = getGimbalState (gconn, vis_now, has_el, gstop, gauto, az, el);
    if (at_all) {
        size_t bufl = 0;
        bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "Rotator   ");
        if (gconn && vis_now)  {
            bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "%s, %s @ ",
                            gstop ? "stopped" : "active",
                            gauto ? "auto" : "manual");
            if (has_el)
                bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "%.1f %.1f\n", az, el);
            else
                bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "%.1f\n", az);
        } else {
            bufl += snprintf (buf+bufl, sizeof(buf)-bufl, "not connected\n");
        }
        client.print(buf);
    }


    // report what DE pane is being used for
    snprintf (buf, sizeof(buf), "DEPane    %s, TZ=UTC%+g%s\n",
                detime_names[de_time_fmt], getTZ(de_tz)/3600.0,
                de_time_fmt == DETIME_INFO ? (desrss ? ", RSAtAt" : ", RSInAgo") : "");
    client.print(buf);


    // report what DX pane is being used for
    nbuf = snprintf (buf, sizeof(buf), "DXPane    %s", dx_info_for_sat ? "sat\n" : "DX ");
    if (!dx_info_for_sat) {
        nbuf += snprintf (buf+nbuf, sizeof(buf)-nbuf, "TZ=UTC%+g %s\n", getTZ(dx_tz)/3600.0,
                            dxsrss == DXSRSS_INAGO ? "RSInAgo"
                            : (dxsrss == DXSRSS_ATAT ? "RSAtAt" : "Prefix"));
    }
    client.print(buf);


    // report rss
    snprintf (buf, sizeof(buf), "RSS       %s, interval %d secs\n", rss_on ? "On" : "Off", rss_interval);
    client.print (buf);

    // report demo mode
    snprintf (buf, sizeof(buf), "DEMO      %s\n", getDemoMode() ? "On" : "Off");
    client.print (buf);

    // report dxcluster state
    client.print ("DXCluster ");
    if (useDXCluster()) {
        snprintf (buf, sizeof(buf), "%s:%d %sconnected\n", getDXClusterHost(), getDXClusterPort(),
                                        isDXClusterConnected() ? "" : "dis");
        client.print (buf);
    } else
        client.println ("off");


    // report units
    snprintf (buf, sizeof(buf), "Units     %s %s %s\n",
                    showDistKm() ? "km" : "mi",
                    showTempC()  ? "C"  : "F",
                    showATMhPa() ? "hPa" : "inHg");
    client.print(buf);


    // report BME info
    client.print ("BME280    ");
    nbuf = 0;
    for (int i = 0; i < MAX_N_BME; i++) {
        const BMEData *dp = getBMEData((BMEIndex)i, false);
        if (dp)
            nbuf += snprintf (buf+nbuf,sizeof(buf)-nbuf, "dTemp@%x= %g dPres@%x= %g ",
                                    dp->i2c, getBMETempCorr(i), dp->i2c, getBMEPresCorr(i));
    }
    if (nbuf > 0)
        client.println (buf);
    else
        client.println ("none");

    // report KX3 info
    client.print ("KX3       ");
    client.print ("ONAIR     ");
    #if defined(_SUPPORT_KX3)
        uint32_t baud = getKX3Baud();
        if (baud > 0) {
            snprintf (buf, sizeof(buf), "%d baud\n", baud);
            client.print (buf);
        } else
            client.println ("off");
    #else
        client.println ( not_sup);
    #endif // _SUPPORT_KX3


    // report call sign and color info
    nbuf = snprintf (buf, sizeof(buf), "CALL      fg %d,%d,%d", RGB565_R(cs_info.call_col.fg),
                RGB565_G(cs_info.call_col.fg), RGB565_B(cs_info.call_col.fg)); 
    if (cs_info.call_col.rainbow)
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg rainbow: ");
    else
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg %d,%d,%d: ", RGB565_R(cs_info.call_col.bg),
                RGB565_G(cs_info.call_col.bg), RGB565_B(cs_info.call_col.bg)); 
    client.print(buf);
    client.println(cs_info.call);

    // report alternate title and color info
    nbuf = snprintf (buf, sizeof(buf), "TITLE     fg %d,%d,%d", RGB565_R(cs_info.title_col.fg),
                RGB565_G(cs_info.title_col.fg), RGB565_B(cs_info.title_col.fg)); 
    if (cs_info.title_col.rainbow)
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg rainbow: ");
    else
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg %d,%d,%d: ", RGB565_R(cs_info.title_col.bg),
                RGB565_G(cs_info.title_col.bg), RGB565_B(cs_info.title_col.bg)); 
    client.print(buf);
    client.println(cs_info.title);

    // report onair message and color info
    nbuf = snprintf (buf, sizeof(buf), "ONAIR     fg %d,%d,%d", RGB565_R(cs_info.onair_col.fg),
                RGB565_G(cs_info.onair_col.fg), RGB565_B(cs_info.onair_col.fg)); 
    if (cs_info.onair_col.rainbow)
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg rainbow: ");
    else
        snprintf (buf+nbuf, sizeof(buf)-nbuf, " bg %d,%d,%d: ", RGB565_R(cs_info.onair_col.bg),
                RGB565_G(cs_info.onair_col.bg), RGB565_B(cs_info.onair_col.bg)); 
    client.print(buf);
    client.println(cs_info.onair);


    // report GPIO
    client.print ("GPIO      ");
    if (GPIOOk())
        client.println ("on");
    else
        client.println ("off");

    // report photosensor info
    client.print ("Light sen ");
    if (found_ltr)
        client.println ("LTR329");
    else if (found_phot)
        client.println ("photoresistor");
    else
        client.println ("none");

    // report each map color
    for (int i = 0; i < N_CSPR; i++) {
        uint16_t color = getMapColor((ColorSelection)i);
        const char *color_name = getMapColorName((ColorSelection)i);
        char uscore_name[30];
        strncpySubChar (uscore_name, color_name, '_', ' ', sizeof(uscore_name));
        snprintf (buf, sizeof(buf), "%-16s%d,%d,%d\n", uscore_name,
                        RGB565_R(color), RGB565_G(color), RGB565_B(color)); 
        client.print(buf);
    }

    // done
    return (true);
}

/* report current satellite info to the given WiFi connection, or none.
 * always return true
 */
static bool getWiFiSatellite (WiFiClient &client, char *line, size_t ll)
{
    // use line for our own usage, not used by caller

    // start reply
    startPlainText (client);

    // get name and current position
    SatNow sn;
    if (!getSatNow (sn)) {
        client.println ("none");
        return (true);
    }

    // table of info
    snprintf (line, ll, "Name          %s\n", sn.name);                    client.print(line);
    snprintf (line, ll, "Alt           %.2f deg\n", sn.el);                client.print(line);
    snprintf (line, ll, "Az            %.2f deg\n", sn.az);                client.print(line);
    snprintf (line, ll, "Range         %.2f km\n", sn.range);              client.print(line);
    snprintf (line, ll, "Rate          %.2f m/s + away\n", sn.rate);       client.print(line);
    snprintf (line, ll, "144MHzDoppler %.6f kHz\n", -sn.rate*144000/3e8);  client.print(line);
    snprintf (line, ll, "440MHzDoppler %.6f kHz\n", -sn.rate*440000/3e8);  client.print(line);
    snprintf (line, ll, "1.3GHzDoppler %.6f kHz\n", -sn.rate*1.3e6/3e8);   client.print(line);
    snprintf (line, ll, "10GHzDoppler  %.6f kHz\n", -sn.rate*1e7/3e8);     client.print(line);

    // real-frequency Doppler for the current satellite's active transmitters, if known.
    // RX = where to tune to hear the downlink; TX = where to transmit so the bird receives
    // its nominal uplink. Based on the live range rate (sn.rate, + = receding).
    {
        SatFreq *fl = NULL;
        int nfl = getSatFreqs (sn.norad, &fl);
        const double dop = sn.rate / 2.99792458e8;      // fractional Doppler, + = receding
        int shown = 0;
        for (int i = 0; i < nfl; i++) {
            SatFreq *sf = &fl[i];
            if (strcmp (sf->status, "active") != 0)     // operate active transmitters only
                continue;
            int t = ++shown;
            char key[24];

            snprintf (key, sizeof(key), "Tx%dMode", t);
            snprintf (line, ll, "%-14s%s\n", key, sf->mode[0] ? sf->mode : "-");
            client.print (line);

            // downlink: observed lower when receding
            if (sf->dl_lo > 0) {
                if (sf->dl_hi > 0 && sf->dl_hi != sf->dl_lo) {
                    snprintf (key, sizeof(key), "Tx%dRXloMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->dl_lo*(1-dop)/1e6); client.print (line);
                    snprintf (key, sizeof(key), "Tx%dRXhiMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->dl_hi*(1-dop)/1e6); client.print (line);
                } else {
                    snprintf (key, sizeof(key), "Tx%dRXMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->dl_lo*(1-dop)/1e6); client.print (line);
                }
            }

            // uplink: transmit higher when receding
            if (sf->ul_lo > 0) {
                if (sf->ul_hi > 0 && sf->ul_hi != sf->ul_lo) {
                    snprintf (key, sizeof(key), "Tx%dTXloMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->ul_lo*(1+dop)/1e6); client.print (line);
                    snprintf (key, sizeof(key), "Tx%dTXhiMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->ul_hi*(1+dop)/1e6); client.print (line);
                } else {
                    snprintf (key, sizeof(key), "Tx%dTXMHz", t);
                    snprintf (line, ll, "%-14s%.6f\n", key, sf->ul_lo*(1+dop)/1e6); client.print (line);
                }
            }
        }
        if (fl)
            free (fl);
    }

    // add table of next several events, if any
    time_t *rises, *sets;
    float *razs, *sazs;
    int n_times;
    if (sn.raz == SAT_NOAZ || sn.saz == SAT_NOAZ
                           || (n_times = nextSatRSEvents (&rises, &razs, &sets, &sazs)) == 0) {
        client.print ("No rise or set\n");
    } else {
        // next events
        if (sn.rdt > 0) {
            snprintf (line, ll, "NextRiseIn    %02dh%02d\n", (int)sn.rdt,
                (int)(60*(sn.rdt-(int)sn.rdt)+0.5F));
            client.print(line);
        }
        snprintf (line, ll, "NextSetIn     %02dh%02d\n",(int)sn.sdt,
                (int)(60*(sn.sdt-(int)sn.sdt)+0.5F));
        client.print(line);

        // print heading
        client.print ("       Upcoming DE Passes\n");
        client.print ("Day  Rise    Az   Set    Az   Up\n");
        client.print ("___  _____  ___  _____  ___  _____\n");
        // snprintf (line, ll, "%.3s  %02dh%02d  %02dh%02d  %02d:%02d\n"

        // print table
        int tzs = getTZ (de_tz);
        for (int i = 0; i < n_times; i++) {

            // DE timezone
            time_t rt = rises[i] + tzs;
            time_t st = sets[i] + tzs;
            int up = st - rt;

            // detect crossing midnight by comparing weekday
            int rt_wd = weekday(rt);
            int st_wd = weekday(st);

            // start with rise day and time/az for sure
            int l = snprintf (line, ll, "%.3s  %02dh%02d  %3.0f", dayShortStr(rt_wd),
                                                                    hour(rt), minute(rt), razs[i]);

            // if set time is tomorrow start new line with set day and blank rise
            if (rt_wd != st_wd)
                l += snprintf (line+l, ll-l, "\n%s            ", dayShortStr(st_wd));

            // show set time/az
            l += snprintf (line+l, ll-l, "  %02dh%02d  %3.0f  ",hour(st), minute(st), sazs[i]);

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                l += snprintf (line+l, ll-l, "%02dh%02d\n", up/3600, (up-3600*(up/3600))/60);
            else
                l += snprintf (line+l, ll-l, "%02d:%02d\n", up/60, up-60*(up/60));

            // done with this line!
            client.print (line);
        }

        // clean up
        free ((void*)rises);
        free ((void*)razs);
        free ((void*)sets);
        free ((void*)sazs);
    }

    return (true);
}


/* report all available satellites.
 */
static bool getWiFiAllSatellites (WiFiClient &client, char line[], size_t line_len)
{
    // get names and elements
    const char **all_names = getAllSatNames();
    if (all_names == NULL) {
        (void) snprintf (line, line_len, "No sats");
        return (false);
    }

    // list and free
    startPlainText(client);
    const char **all_names_0 = all_names;
    const char *name;
    while ((name = *all_names++) != NULL) {
        client.println (name);
        free ((void*)name);
    }
    free ((void*)all_names_0);

    // ok
    return (true);
}


/* send the current collection of sensor data to client in tabular format.
 */
static bool getWiFiSensorData (WiFiClient &client, char line[], size_t line_len)
{
    if (getNBMEConnected() == 0) {
        (void) snprintf (line, line_len, "No sensors");
        return (false);
    }

    // send html header
    startPlainText(client);

    // send content header
    if (showTempC() && showATMhPa())
        client.print ("#   UTC ISO 8601      UNIX secs I2C  Temp,C   P,hPa   Hum,%  DewP,C\n");
    else
        client.print ("#   UTC ISO 8601      UNIX secs I2C  Temp,F  P,inHg   Hum,%  DewP,F\n");

    // send data for each connected sensor
    for (int i = 0; i < MAX_N_BME; i++) {
        const BMEData *dp = getBMEData((BMEIndex)i, true);
        if (dp) {
            // head points to oldest
            for (int j = 0; j < N_BME_READINGS; j++) {
                uint8_t qj = (dp->q_head+j)%N_BME_READINGS;
                long u = dp->u[qj];
                if (u) {
                    char buf[100];
                    snprintf (buf, sizeof(buf),
                                "%4d-%02d-%02dT%02d:%02d:%02dZ %ld  %02x %7.2f %7.2f %7.2f %7.2f\n",
                                year(u), month(u), day(u), hour(u), minute(u), second(u), u,
                                dp->i2c, dp->t[qj], dp->p[qj], dp->h[qj],
                                dewPoint (dp->t[qj], dp->h[qj]));
                    client.print (buf);
                }
            }
            client.print ("\n");
        }
    }

    return (true);
}

/* send the current space weather stats to client
 */
static bool getWiFiSpaceWx (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // send html header
    startPlainText(client);

    // send fresh values
    (void) checkForNewSpaceWx();
    char buf[100];
    char xray_str[10];

    client.print (" Datum     Value\n");
    client.print ("-------- ---------\n");

    if (space_wx[SPCWX_SSN].value_ok) {
        snprintf (buf, sizeof(buf), "SSN      %9.1f\n", space_wx[SPCWX_SSN].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_KP].value_ok) {
        snprintf (buf, sizeof(buf), "KP        %8.1f\n", space_wx[SPCWX_KP].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_FLUX].value_ok) {
        snprintf (buf, sizeof(buf), "FLUX     %9.1f\n", space_wx[SPCWX_FLUX].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_XRAY].value_ok) {
        snprintf (buf, sizeof(buf), "XRAY      %8s\n", xrayLevel(xray_str, space_wx[SPCWX_XRAY]));
        client.print (buf);
    }

    if (space_wx[SPCWX_SOLWIND].value_ok) {
        snprintf (buf, sizeof(buf), "SOLWIND   %8.1f\n", space_wx[SPCWX_SOLWIND].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_DRAP].value_ok) {
        snprintf (buf, sizeof(buf), "DRAP      %8.1f\n", space_wx[SPCWX_DRAP].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_BZ].value_ok) {
        snprintf (buf, sizeof(buf), "Bz        %8.1f\n", space_wx[SPCWX_BZ].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_NOAASPW].value_ok) {
        snprintf (buf, sizeof(buf), "NOAA      %8.0f\n", space_wx[SPCWX_NOAASPW].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_AURORA].value_ok) {
        snprintf (buf, sizeof(buf), "Aurora    %8.0f\n", space_wx[SPCWX_AURORA].value);
        client.print (buf);
    }

    if (space_wx[SPCWX_DST].value_ok) {
        snprintf (buf, sizeof(buf), "DST       %8.0f\n", space_wx[SPCWX_DST].value);
        client.print (buf);
    }


    // show path reliability for the current hour
    if (bc_matrix.ok) {
        int hr = hour(myNow());
        for (int i = BMTRX_COLS; --i >= 0; ) {
            int band = propBand2Band ((PropMapBand)i);
            snprintf (buf, sizeof(buf), "DEDX_%02dm  %8d\n", band, bc_matrix.m[hr][i]);
            client.print (buf);
        }
    }

    // show NOAA conditions formatted similar to PLOT_CH_NOAASPW
    NOAASpaceWxData noaasw;
    if (retrieveNOAASWx (noaasw) && noaasw.data_ok) {
        for (int i = 0; i < N_NOAASW_C; i++) {
            size_t bl = snprintf (buf, sizeof(buf), "NSPW_%c    ", noaasw.cat[i]);
            for (int j = 0; j < N_NOAASW_V; j++)
                bl += snprintf (buf+bl, sizeof(buf)-bl, " %d", noaasw.val[i][j]);
            client.println (buf);
        }
    }

    // ok
    return (true);
}



/* send some misc system info
 */
static bool getWiFiSys (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    char buf[200];

    // send html header
    startPlainText(client);

    // get latest worst stats
    int worst_heap, worst_stack;
    getWorstMem (&worst_heap, &worst_stack);

    // show basic info
    client.print ("Version  "); client.println (hc_version);
    snprintf (buf, sizeof(buf), "BuildSz  %d x %d\n", BUILD_W, BUILD_H); client.print(buf);
    client.print ("MaxStack "); client.println (worst_stack);
    client.print ("Platform "); client.println (platform);
    client.print ("BEHost   "); client.println (backend_host);
    client.print ("BEPort   "); client.println (backend_port);
    client.print ("MACaddr  "); client.println (WiFi.macAddress().c_str());
    client.print ("S/N      "); client.println (ESP.getChipId());

    // show EEPROM used
    uint16_t ee_used, ee_size;
    reportEESize (ee_used, ee_size);
    snprintf (buf, sizeof(buf), "EEPROM   %u used of %u bytes\n", ee_used, ee_size);
    client.print (buf);

    // show uptime
    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        snprintf (buf, sizeof(buf), "UpTime   %dd%02d:%02d:%02d\n", days, hrs, mins, secs);
        client.print (buf);
    }

    // show NTP servers
    const NTPServer *ntp_list;
    int n_ntp = getNTPServers (&ntp_list);
    for (int i = 0; i < n_ntp; i++) {
        int bl = snprintf (buf, sizeof(buf), "NTP      %s ", ntp_list[i].server);
        int rsp = ntp_list[i].rsp_time;
        if (rsp == 0)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", "- Not yet measured");
        else if (rsp == NTP_TOO_LONG)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", "- Timed out");
        else
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%d ms\n", rsp);
        client.print (buf);
    }

    // show debug levels
    const char *db_names[DEBUG_SUBSYS_N];
    int db_levels[DEBUG_SUBSYS_N];
    getDebugs (db_names, db_levels);
    client.println ("Debugs:");
    for (int i = 0; i < DEBUG_SUBSYS_N; i++) {
        snprintf (buf, sizeof(buf), "%9s%10s = %d\n", "", db_names[i], db_levels[i]);
        client.print (buf);
    }

    // show file system info
    int n_info;
    DSZ_t fs_size, fs_used;
    char *fs_name;
    FS_Info *fip0 = getConfigDirInfo (&n_info, &fs_name, &fs_size, &fs_used);
    if (fip0) {
        client.print (fs_name);
        if (fs_size > 1000000000UL) {
            snprintf (buf, sizeof(buf), " %llu / %lld MiB %.2f%%\n",
                            fs_used/1048576ULL, fs_size/1048576ULL, 100.0F*fs_used/fs_size);
        } else
            snprintf (buf, sizeof(buf), " %llu / %llu B %.2f%%\n",
                            fs_used, fs_size, 100.0F*fs_used/fs_size);
        client.print (buf);
        for (int i = 0; i < n_info; i++) {
            FS_Info *fip = &fip0[i];
            snprintf (buf, sizeof(buf), "  %s %8llu %s\n", fip->date, fip->len, fip->name);
            client.print (buf);
        }
        free (fs_name);
        free (fip0);
    }

    return (true);
}

/* report the current VOACAP Band Conditions matrix
 */
static bool getWiFiVOACAP (WiFiClient &client, char *line, size_t line_len)
{
    // bale if not valid
    if (!bc_matrix.ok) {
        (void) snprintf (line, line_len, "VOACAP data not valid");
        return (false);
    }

    // send html header
    startPlainText(client);

    // find utc and DE hour now. these will be the matrix row in plot column 0.
    int utc_hour_now = hour (nowWO());
    int de_hour_now = hour (nowWO() + getTZ (de_tz));
    int hr_now = bc_utc_tl ? utc_hour_now : de_hour_now;

    // display matrix -- rotated with same layout as plotBandConditions()
    char buf[128];
    int buf_l = 0;
    for (int p_row = 0; p_row < BMTRX_COLS; p_row++) {  // print 1 row for each matrix column
        int m_col = BMTRX_COLS - 1 - p_row;             // convert to bm col with 10 (last col) on top

        // start with band name
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "%2dm  ", propBand2Band((PropMapBand)m_col));

        // scan matrix _column_ for this band
        for (int p_col = 0; p_col < BMTRX_ROWS; p_col++) {

            // shift so current time is first column
            int m_row = (p_col + utc_hour_now + 48) % 24;

            // bc_matrix is a matrix of 24 rows of UTC 0 .. 23, 8 columns of bands 80-40-30-20-17-15-12-10.
            uint8_t rel = bc_matrix.m[m_row][m_col];

            buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "%4d", rel);
        }

        // finish with band freq
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, " %5.0f MHz\n", propBand2MHz((PropMapBand)m_col));

        // print and restart
        client.print (buf);
        buf_l = 0;
    }

    // bottom row is time DE or utc depending on bc_utc_tl but "now" always column 1
    buf_l = 0;
    buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "%-5s", bc_utc_tl ? "UTC" : "DE");
    for (int p_col = 0; p_col < BMTRX_ROWS; p_col++) {
        int hr = (hr_now + p_col) % 24;
        if ((hr%4) != 0)
            buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "    ");
        else
            buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "%4d", hr);
    }
    client.println (buf);

    // ok!
    return (true);
}


/* send current clock time
 */
static bool getWiFiTime (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // send html header
    startPlainText(client);

    // report user's idea of time
    char buf[100];
    time_t t = nowWO();
    int yr = year (t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour (t);
    int mn = minute (t);
    int sc = second (t);
    int bl = snprintf (buf, sizeof(buf)-10, "Clock_UTC %d-%02d-%02dT%02d:%02d:%02d ",
                        yr, mo, dy, hr, mn, sc);

    // indicate any time offset
    int off = utcOffset();                      // seconds
    if (off == 0) {
        strcat (buf, "Z\n");                    // append Z if above time really is UTC
    } else {
        int off_abs = abs(off);
        int dy_os = off_abs/SECSPERDAY;
        off_abs -= dy_os*SECSPERDAY;
        int hr_os = off_abs/3600;
        off_abs -= hr_os*3600;
        int mn_os = off_abs/60;
        off_abs -= mn_os*60;
        int sc_os = off_abs;
        bl += snprintf (buf+bl, sizeof(buf)-bl, "= Z%c", off < 0 ? '-' : '+');
        if (dy_os != 0)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%dd", dy_os);
        bl += snprintf (buf+bl, sizeof(buf)-bl, "%02d:%02d:%02d\n", hr_os, mn_os, sc_os);
    }
    client.print (buf);

    return (true);
}

/* remote command to set call sign to some message and set fg and bg colors.
 * all are optional in any order; restore defaults if no args at all.
 */
static bool setWiFiTitle (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "call";
    wa.name[wa.nargs++] = "title";
    wa.name[wa.nargs++] = "onair";
    wa.name[wa.nargs++] = "fg";
    wa.name[wa.nargs++] = "bg";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *call  = wa.found[0] ? wa.value[0] : NULL;
    const char *title = wa.found[1] ? wa.value[1] : NULL;
    const char *onair = wa.found[2] ? wa.value[2] : NULL;
    const char *fg    = wa.found[3] ? wa.value[3] : NULL;
    const char *bg    = wa.found[4] ? wa.value[4] : NULL;

    // can not set call this way
    if (call && strlen(call) > 0) {
        strcpy (line, "use set_newde to change call");
        return (false);
    }

    // require exactly one type
    if (!!call + !!title + !!onair != 1) {
        strcpy (line, "must set one of call title or onair");
        return (false);
    }

    // crack fg if found
    uint16_t fg_c = 0;
    if (fg) {
        int r, g, b;
        if (sscanf (fg, "%d,%d,%d", &r, &g, &b) == 3) {
            if (!rgbOk (r, g, b)) {
                strcpy (line, "bad fg RGB");
                return (false);
            }
        } else if (!findColorName (fg, r, g, b)) {
            startPlainText (client);
            client.print("color names:\n");
            printColorNames (client);
            return (true);
        }
        fg_c = RGB565(r,g,b);
    }

    // crack bg if found
    uint16_t bg_c = 0;
    uint8_t rainbow = 0;
    bool rainbow_set = false;
    if (bg) {
        if (strcmp (bg, "rainbow") == 0) {
            rainbow = 1;
            rainbow_set = true;
        } else {
            int r, g, b;
            if (sscanf (bg, "%d,%d,%d", &r, &g, &b) == 3) {
                if (!rgbOk (r, g, b)) {
                    strcpy (line, "bad bg RGB");
                    return (false);
                }
            } else if (!findColorName (bg, r, g, b)) {
                startPlainText (client);
                client.print("color names:\n");
                printColorNames (client);
                return (true);
            }
            bg_c = RGB565(r,g,b);
        }
    }

    // update
    if (onair) {
        if (strlen (onair) >= NV_ONAIR_LEN) {
            snprintf (line, line_len, "too long - max is %d\n", NV_ONAIR_LEN-1);
            return (false);
        }
        setCallsignInfo (CT_ONAIR, onair, fg ? &fg_c : NULL, bg ? &bg_c : NULL, rainbow_set ? &rainbow:NULL);
    } else if (title) {
        if (strlen (title) >= NV_TITLE_LEN) {
            snprintf (line, line_len, "too long - max is %d\n", NV_TITLE_LEN-1);
            return (false);
        }
        setCallsignInfo (CT_TITLE, title, fg ? &fg_c : NULL, bg ? &bg_c : NULL, rainbow_set ? &rainbow:NULL);
    } else if (call) {
        if (strlen (call) >= NV_CALLSIGN_LEN) {
            snprintf (line, line_len, "too long - max is %d\n", NV_CALLSIGN_LEN-1);
            return (false);
        }
        setCallsignInfo (CT_CALL, NULL, fg ? &fg_c : NULL, bg ? &bg_c : NULL, rainbow_set ? &rainbow:NULL);
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // engage
    updateCallsign (true);

    // ack
    startPlainText (client);
    client.print ("ok\n");

    return (true);
}

/* run a DemoChoice or turn off
 */
static bool setWiFiDemo (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "on";
    wa.name[wa.nargs++] = "off";
    wa.name[wa.nargs++] = "n";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    char buf[200];

    // crack
    if (wa.found[0] && wa.value[0] == NULL) {
        // on
        setDemoMode(true);
        drawDemoRunner();
        strcpy (buf, "Demo mode on\n");
        Serial.print (buf);

    } else if (wa.found[1] && wa.value[1] == NULL) {
        // off
        setDemoMode(false);
        drawDemoRunner();
        strcpy (buf, "Demo mode off\n");
        Serial.print (buf);

    } else if (wa.found[2] && wa.value[2] != NULL && wa.value[2][0] != '\0') {
        // n=x
        int choice;
        if (!atoiOnly(wa.value[2], &choice) || choice < 0 || choice >= DEMO_N) {
            snprintf (line, line_len, "Demo codes 0 .. %d", DEMO_N-1);
            return (false);
        }

        // turn on if not already
        if (!getDemoMode()) {
            setDemoMode(true);
            drawDemoRunner();
        }

        // run it
        bool slow;
        if (runDemoChoice ((DemoChoice)choice, slow, buf, sizeof(buf))) {
            strcat (buf, "\n");
            Serial.print (buf);
        } else {
            strcpy (line, buf);
            return (false);
        }

    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // ack
    startPlainText (client);
    client.print (buf);

    // good
    return (true);
}

/* inject a spot into the dxcluster stream
 */
static bool setWiFiSpot (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "tx_call";
    wa.name[wa.nargs++] = "rx_call";
    wa.name[wa.nargs++] = "kHz";
    wa.name[wa.nargs++] = "comment";            // optional; only used to test IOTA ref matching

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // first 3 required, comment is optional
    if (!wa.found[0] || !wa.found[1] || !wa.found[2]) {
        snprintf (line, line_len, "%s", "tx_call=x&rx_call=x&kHz=x[&comment=x]");
        return (false);
    }

    // inject
    Message ynot;
    if (!injectDXClusterSpot (wa.value[0], wa.value[1], wa.value[2],
                                wa.found[3] ? wa.value[3] : NULL, ynot)) {
        quietStrncpy (line, ynot.get(), line_len);
        return (false);
    }

    // ack
    startPlainText (client);
    client.print ("ok\n");

    // good
    return (true);
}


/* remote command to set the DE time format and/or atin
 * set_defmt?fmt=[one from menu]&atin=RSAtAt|RSInAgo"
 */
static bool setWiFiDEformat (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "fmt";
    wa.name[wa.nargs++] = "atin";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // fmt is required
    if (!wa.found[0]) {
        strcpy (line, "fmt is required");
        return (false);
    }

    // handy
    const char *fmt = wa.value[0];
    const char *atin = wa.value[1];

    // search format options
    int new_fmt = -1;
    for (int i = 0; i < DETIME_N; i++) {
        if (strcmp (fmt, detime_names[i]) == 0) {
            new_fmt = i;
            break;
        }
    }
    if (new_fmt < 0) {
        size_t ll = snprintf (line, line_len, "Formats: %s", detime_names[0]);
        for (int i = 1; i < DETIME_N; i++)
            ll += snprintf (line+ll, line_len-ll, ", %s", detime_names[i]);
        return (false);
    }

    // if fmt is DETIME_INFO, also allow setting optional at/in
    uint8_t new_desrss = desrss;
    if (atin) {
        if (new_fmt != DETIME_INFO) {
            snprintf (line, line_len, "atin requires %s", detime_names[DETIME_INFO]);
            return (false);
        }
        if (strcmp (atin, "RSAtAt") == 0)
            new_desrss = DXSRSS_ATAT;
        else if (strcmp (atin, "RSInAgo") == 0)
            new_desrss = DXSRSS_INAGO;
        else {
            strcpy (line, "unknown atin");
            return (false);
        }
    }

    // set, save and update
    de_time_fmt = new_fmt;
    NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    desrss = new_desrss;
    NVWriteUInt8 (NV_DE_SRSS, desrss);
    drawDEInfo();

    // ack
    startPlainText (client);
    snprintf (line, line_len, "%s %s\n", fmt, atin ? atin : "");
    client.println (line);

    // ok
    return (true);
}

/* remote command to set a new dx cluster
 */
static bool setWiFiCluster (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "host";
    wa.name[wa.nargs++] = "port";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    char *host = (char*) wa.value[0];   // setDXCluster will trim
    char *port = (char*) wa.value[1];   // setDXCluster will trim

    // try to save
    Message ynot;
    if (!setDXCluster (host, port, ynot)) {
        quietStrncpy (line, ynot.get(), line_len);
        return (false);
    }

    // close, it will reopen if active
    closeDXCluster();

    // ack
    startPlainText (client);
    client.print ("ok\n");

    // we tried
    return (true);
}

/* remote command to set new debug level
 * name=xxx&level=n"
 */
static bool setWiFiDebug (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "name";
    wa.name[wa.nargs++] = "level";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);
    if (!wa.found[0] || !wa.found[1]) {
        strcpy (line, garbcmd);
        return (false);
    }

    // handy
    char *name = (char*) wa.value[0];
    int level = atoi ((char*) wa.value[1]);

    // try to set
    if (!setDebugLevel (name, level)) {
        strcpy (line, "unknown debug");
        return (false);
    }

    // ack
    startPlainText (client);
    client.print ("ok\n");

    // ok
    return (true);
}


/* remote command to set up daily alarm clock
 *   state=off|armed&time=HR:MN&utc=yes|no
 */
static bool setWiFiAlarm (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "state";
    wa.name[wa.nargs++] = "time";
    wa.name[wa.nargs++] = "utc";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *state = wa.value[0];
    const char *timespec = wa.value[1];
    const char *utc_val = wa.value[2];

    // get current state
    AlarmState as;
    uint16_t hr16, mn16;
    bool utc;
    getDailyAlarmState (as, hr16, mn16, utc);

    // crack new state
    if (state) {
        if (strcmp (state, "off") == 0)
            as = ALMS_OFF;
        else if (strcmp (state, "armed") == 0)
            as = ALMS_ARMED;
        else {
            strcpy (line, "unknown state");
            return (false);
        }
    } else {
        strcpy (line, "state is required");
        return (false);
    }

    // crack new time spec if given
    if (timespec) {
        if (!state || as == ALMS_OFF) {
            strcpy (line, "arming is required to set time");
            return(false);
        }
        int hr = 0, mn = 0;
        if (sscanf (timespec, "%d:%d", &hr, &mn) != 2 || hr < 0 || hr >= 24 || mn < 0 || mn >= 60) {
            strcpy (line, "invalid time spec");
            return (false);
        }
        hr16 = hr;
        mn16 = mn;
        if (utc_val) {
            if (strcasecmp (utc_val, "yes") == 0)
                utc = true;
            else if (strcasecmp (utc_val, "no") == 0)
                utc = false;
            else {
                strcpy (line, "utc must be yes or no");
                return (false);
            }
        }
    }

    // engage
    setDailyAlarmState (as, hr16, mn16, utc);

    // ack
    startPlainText (client);
    if (as == ALMS_OFF)
        client.print ("alarm off\n");
    else {
        char buf[50];
        snprintf (buf, sizeof(buf), "armed at %02d:%02d\n", hr16, mn16);
        client.print (buf);
    }

    return (true);
}

/* remote command to set the once-only alarm
 *  state=off|armed&time=YYYY-MM-DDTHR:MN&tz=DE|UTC
 */
static bool setWiFiOnceAlarm (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "state";
    wa.name[wa.nargs++] = "time";
    wa.name[wa.nargs++] = "tz";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *state = wa.value[0];
    const char *timespec = wa.value[1];
    const char *tz = wa.value[2];

    // get current state
    AlarmState as;
    time_t a_t;
    bool utc;
    char str[50];
    getOneTimeAlarmState (as, a_t, utc, str, sizeof(str));

    // crack new state
    if (state) {
        if (strcmp (state, "off") == 0)
            as = ALMS_OFF;
        else if (strcmp (state, "armed") == 0)
            as = ALMS_ARMED;
        else {
            strcpy (line, "unknown state");
            return (false);
        }
    } else {
        strcpy (line, "state is required");
        return (false);
    }

    // crack tz if given
    if (tz) {
        if (strcmp (tz, "DE") == 0)
            utc = false;
        else if (strcmp (tz, "UTC") == 0)
            utc = true;
        else {
            strcpy (line, "unknown tz");
            return (false);
        }
    }

    // use new time spec if given
    if (timespec) {
        if (as == ALMS_OFF) {
            strcpy (line, "may set time only if armed");
            return(false);
        }
        quietStrncpy (str, timespec, sizeof(str));
    }

    // engage
    if (!setOneTimeAlarmState (as, utc, str, NULL)) {
        strcpy (line, "time garbled or too old");
        return (false);
    }

    // ack
    startPlainText (client);
    if (as == ALMS_OFF)
        client.println ("alarm off");
    else {
        client.print ("armed for ");
        client.print (utc ? "UTC " : "DE ");
        client.println ( str);
    }

    return (true);
}


/* remote command to set the aux time format
 */
static bool setWiFiAuxTime (WiFiClient &client, char line[], size_t line_len)
{
    // parse
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "format";
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // look for matching name
    bool found = false;
    if (wa.found[0]) {
        for (int i = 0; i < AUXT_N; i++) {
            if (strcmp (wa.value[0], auxtime_names[i]) == 0) {
                auxtime = (AuxTimeFormat)i;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        int n = snprintf (line, line_len, "set %s", auxtime_names[0]);
            for (int i = 1; i < AUXT_N; i++)
                n += snprintf (line+n, line_len-n, ",%s", auxtime_names[i]);
        return (false);
    }

    // good: engage
    updateClocks(true);

    // ack
    startPlainText (client);
    client.println (wa.value[0]);

    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient &client, char line[], size_t line_len)
{
    if (brOnOffOk()) {

        // define all possible args
        WebArgs wa;
        wa.nargs = 0;
        wa.name[wa.nargs++] = "on";
        wa.name[wa.nargs++] = "off";

        // parse
        if (!parseWebCommand (wa, line, line_len))
            return (false);

        // engage
        if (wa.found[0] && wa.value[0] == NULL)
            brightnessOn();
        else if (wa.found[1] && wa.value[1] == NULL)
            brightnessOff();
        else {
            strcpy (line, "Specify just on or off");
            return (false);
        }

        // ack with same state
        startPlainText (client);
        client.print ("display ");
        client.println (line);

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}

/* convert 3-letter day-of-week abbreviation to 1..7 (Sun..Sat),
 * return whether successful.
 */
static bool crackDOW (const char *daystr, int &dow)
{
    for (uint8_t i = 1; i <= DAYSPERWEEK; i++) {
        if (strcmp (dayShortStr(i), daystr) == 0) {
            dow = i;
            return (true);
        }
    }
    return (false);
}

/* command the rotator
 * state=[un]stop|[un]auto&az=X&el=X" },
 */
static bool setWiFiRotator (WiFiClient &client, char line[], size_t line_len)
{
    // define args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "state";
    wa.name[wa.nargs++] = "az";
    wa.name[wa.nargs++] = "el";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // ok or say why not
    Message ynot;
    if (commandRotator (wa.value[0], wa.value[1], wa.value[2], ynot)) {
        startPlainText (client);
        client.print ("ok\n");
        return (true);
    } else {
        quietStrncpy (line, ynot.get(), line_len);
        return (false);
    }
}

/*
 * get supported parameter values from server
 */
 
/* helper function to parse returned records */
bool extract_columns(const char *s, std::string& a, std::string& b, std::string& c)
{
    const char *p = s;
    const char *comma1 = strchr(p, ',');
    if (!comma1) return false;

    const char *comma2 = strchr(comma1 + 1, ',');
    if (!comma2) return false;

    a = std::string(p, comma1);
    b = std::string(comma1 + 1, comma2);
    c = std::string(comma2 + 1);
    return true;
} 
void antennas_supported_values (
                const std::map<std::string, std::string>& remoteinput,
                std::map<std::string, std::string>& remoteout)
{
    // prep
    WiFiClient compat_client;
    char line[256];
    line[0] = '\0';
    // reset count and index
    char compat_pagehead[]="/fetchBandConditions.pl?INFOREQUEST=1";
	
	std::string compat_page = compat_pagehead;
	for (const auto& kv : remoteinput) {
		const std::string& key = kv.first;
		const std::string& val = kv.second;

		compat_page += '&';
		compat_page += key;
		compat_page += '=';
		compat_page += val;		
	}
	
    Serial.println(compat_page.c_str());
    if (compat_client.connect(backend_host, backend_port)) {

        // fetch feed page
        httpHCGET (compat_client, backend_host, compat_page.c_str());

        // skip response header
        if (!httpSkipHeader (compat_client)) {
            Serial.println ("compat header short");
            goto out;
        }
        std::string    parm;
		std::string    inval;
		std::string    outval;
        // get lines 
        while (1) {
            if (!getTCPLine (compat_client, line, sizeof(line), NULL))
                goto out;
            Serial.printf ("Fetched %s\n", line);
			if (extract_columns(line,parm,inval,outval)) {
				// only update if key already exists in remoteout
				if (remoteout.count(parm) > 0) {
					remoteout[parm] = outval;
				}
				Serial.printf ("parsed%s,%s,%s\n",parm.c_str(),inval.c_str(),outval.c_str());
			}
        }
    }
  out:
    compat_client.stop();
    return;
}


/*
 * remote command to serve the mobile dashboard
 */

static bool getWiFiDashboardGui (WiFiClient &client, char line[], size_t line_len)
{
    client.println ("HTTP/1.0 200 OK");
    sendUserAgent (client);
    client.println ("Content-Type: text/html; charset=utf-8");
    client.println ("Cache-Control: no-store");
    client.println ("Connection: close\r\n");
    client.print (dashboard_html);
    Serial.printf ("Served dashboard.html\n");
    return (true);
}


/*
 * remote command to list antennas
 */

 
 static bool getWiFiAntennasGui (WiFiClient &client, char line[], size_t line_len)
 {

	client.println ("HTTP/1.0 200 OK");
	sendUserAgent (client);
	client.println ("Content-Type: text/html; charset=utf-8");
	client.println ("Cache-Control: no-store");
	client.println ("Connection: close\r\n");
	client.print (antennas_html);
	Serial.printf ("Served antennas.html\n");
	return (true);
 }

/*
 * remote command to list antennas
 */

 
 static bool getWiFiAntennas (WiFiClient &client, char line[], size_t line_len)
 {
	char buf[100];

    startPlainText (client);    
	
    std::map<std::string, std::string> remotevals;
    std::map<std::string, std::string> supportedvals;	
	
    remotevals["ANTDEDXCONTROL"]    = std::to_string(antennas_dedx_control);
	supportedvals["ANTDEDXCONTROL"] = "0";
    remotevals["ANTDEINDEX"]    = std::to_string(antennas_de);
	supportedvals["ANTDEINDEX"] = "0";
    remotevals["ANTDXINDEX"]    = std::to_string(antennas_dx);
	supportedvals["ANTDXINDEX"] = "0";	

    snprintf(buf, sizeof(buf), "%.1f", antennas_de_az);
    remotevals["ANTDEAZ"] = buf;
	supportedvals["ANTDEAZ"] = "0.0";	
	
    snprintf(buf, sizeof(buf), "%.1f", antennas_dx_az);
    remotevals["ANTDXAZ"] = buf;	
	supportedvals["ANTDXAZ"] = "0.0";			
	
	
	antennas_supported_values(remotevals,supportedvals);	

 
	client.println ("get antennas: ");
    client.print("backend server: ");
	client.print(backend_host);
	client.println(", supported values below shown in []");

	snprintf (buf, sizeof(buf), "antdedxcontrol: %u [%s]", 
		antennas_dedx_control,supportedvals["ANTDEDXCONTROL"].c_str()) ;
	client.println(buf);
	
    int supported_dedx_control = atoi(supportedvals["ANTDEDXCONTROL"].c_str());
	snprintf (buf, sizeof(buf), "antdeindex   set to %u [%s] and will %sbe used", 
		antennas_de,supportedvals["ANTDEINDEX"].c_str(), (supported_dedx_control & 1)?"":"not ");
	client.println(buf);
 
	snprintf (buf, sizeof(buf), "antdeaz      set to %.1f [%s] and will %sbe used", 
		antennas_de_az,supportedvals["ANTDEAZ"].c_str(), (supported_dedx_control & 1)?"":"not ");
	client.println(buf);

	snprintf (buf, sizeof(buf), "antdxindex   set to %u [%s] and will %sbe used", 
		antennas_dx,supportedvals["ANTDXINDEX"].c_str(), (supported_dedx_control & 2)?"":"not ");
	client.println(buf);	
	
	snprintf (buf, sizeof(buf), "antdxaz      set to %.1f [%s] and will %sbe used", 
		antennas_dx_az,supportedvals["ANTDXAZ"].c_str(), (supported_dedx_control & 1)?"":"not ");
	client.println(buf);	
	

	return (true);
 }
 
static bool getWiFiAntennalist (WiFiClient &client, char line[], size_t line_len)
 {
	char buf[100];

    startPlainText (client);    
 
	client.println ("list antennas: ");

	size_t i=0;
	bool rc=true;
	while (rc) {
		rc = antenna_getline(buf,sizeof(buf),i++);
		if (rc)
			client.println(buf);
	}
	return (true);
 }
 
/*
 * remote command to setW
 * set_antennas?antdeindex=n,antdxindex=n,antdedxcontrol=n
 */
 static bool setWiFiAntennas (WiFiClient &client, char line[], size_t line_len)
 {
	WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "antdeindex";
    wa.name[wa.nargs++] = "antdxindex";
    wa.name[wa.nargs++] = "antdedxcontrol";
    wa.name[wa.nargs++] = "antdeaz";
    wa.name[wa.nargs++] = "antdxaz";
	
	// parse
	if (!parseWebCommand (wa, line, line_len))
		return (false);
	const char *ant_de_index     = wa.value[0];
	const char *ant_dx_index     = wa.value[1];
	const char *ant_dedx_control = wa.value[2];
	const char *ant_de_az        = wa.value[3];
	const char *ant_dx_az        = wa.value[4];	

	int    ant_de_index_int=0;
	int    ant_dx_index_int=0;	
	int    ant_dedx_control_int=0;
	float  ant_de_az_f=0.0;
	float  ant_dx_az_f=0.0;	
	
	bool      ant_de_index_update=false;
	bool      ant_dx_index_update=false;
	bool      ant_dedx_control_update=false;	
	bool      ant_de_az_update=false;
	bool      ant_dx_az_update=false;

		
	// validate 
	if (ant_dedx_control) {	
		bool ant_dedx_control_ok = atoiOnly (ant_dedx_control, &ant_dedx_control_int);	
		if (!ant_dedx_control_ok) {
			strcpy (line, "ant_dedx_control must be a number");
			return (false);
		}
		if (ant_dedx_control_int < 0 || ant_dedx_control_int > 3) {
			strcpy (line, "antxrx must be  [0..3]");
			return (false);
		}
		ant_dedx_control_update=true;
	}
	
	if (ant_de_index) {	
		bool ant_de_index_ok = atoiOnly (ant_de_index, &ant_de_index_int);	
		if (!ant_de_index_ok) {
			strcpy (line, "antdeindex must be a number");
			return (false);
		}
		if (!antenna_validindex(ant_de_index_int)) {
			strcpy (line, "antdeindex must be a valid index\nlist valid indexes with command list_antennas");
			return (false);
		}
		ant_de_index_update=true;
	}	
	
	if (ant_dx_index) {	
		bool ant_dx_index_ok = atoiOnly (ant_dx_index, &ant_dx_index_int);	
		if (!ant_dx_index_ok) {
			strcpy (line, "antdxindex must be a number");
			return (false);
		}
		if (!antenna_validindex(ant_dx_index_int)) {
			strcpy (line, "antdxindex must be a valid index\nlist valid indexes with command list_antennas");
			return (false);
		}
		ant_dx_index_update=true;
	}	

	if (ant_de_az) {
		bool ant_de_az_ok = atofOnly (ant_de_az, &ant_de_az_f);	
		if (!ant_de_az_ok) {
			strcpy (line, "antdeaz must be a floating point number");
			return (false);
		}
		if ( ant_de_az_f < 0.0 || ant_de_az_f > 360.0) {
			strcpy (line, "antdeaz must be in range [0.0..360.0]");
			return (false);
		}
		ant_de_az_update=true;
	}		
	if (ant_dx_az) {
		bool ant_dx_az_ok = atofOnly (ant_dx_az, &ant_dx_az_f);	
		if (!ant_dx_az_ok) {
			strcpy (line, "antdxaz must be a floating point number");
			return (false);
		}
		if ( ant_dx_az_f < 0.0 || ant_dx_az_f > 360.0) {
			strcpy (line, "antdxaz must be in range [0.0..360.0]");
			return (false);
		}
		ant_dx_az_update=true;
	}	
	// execute
	if (ant_dedx_control_update) {
		antennas_dedx_control = (uint8_t) ant_dedx_control_int;
        NVWriteUInt8 (NV_ANT_DEDX_CONTROL, antennas_dedx_control);		
	}
	if (ant_de_index_update) {
		antennas_de = (uint16_t) ant_de_index_int;
        NVWriteUInt16 (NV_ANT_DE_INDEX, antennas_de);		
	}
	if (ant_dx_index_update) {
		antennas_dx = (uint16_t) ant_dx_index_int;
        NVWriteUInt16 (NV_ANT_DX_INDEX, antennas_dx);		
	}
	if (ant_de_az_update) {
		antennas_de_az = ant_de_az_f;
        NVWriteFloat (NV_ANT_DE_AZ, antennas_de_az);		
	}
	if (ant_dx_az_update) {
		antennas_dx_az = ant_dx_az_f;
        NVWriteFloat (NV_ANT_DX_AZ, antennas_dx_az);		
	}
	
	if (ant_dedx_control_update || ant_de_index_update || ant_dx_index_update || ant_de_az_update || ant_dx_az_update) {
		startPlainText (client);
        char buf[100];

		if (ant_dedx_control_update) {
			snprintf (buf, sizeof(buf), "antdedxcontrol set to %u", antennas_dedx_control);
			client.println(buf);
		}
		if (ant_de_index_update) {
			snprintf (buf, sizeof(buf), "antdeindex   set to %u", antennas_de);
			client.println(buf);
		}
		if (ant_dx_index_update) {
			snprintf (buf, sizeof(buf), "antdxindex   set to %u", antennas_dx);
			client.println(buf);
		}		
		if (ant_de_az_update) {
			snprintf (buf, sizeof(buf), "antdeaz   set to %.1f", antennas_de_az);
			client.println(buf);
		}	
		if (ant_dx_az_update) {
			snprintf (buf, sizeof(buf), "antdxaz   set to %.1f", antennas_dx_az);
			client.println(buf);
		}	
		
		client.println("ok");
		return (true);
	}

	// nothing to execute
	strcpy (line, "no parameters given");
	return (false);
 }
 
/* remote command to set display on/off/idle times
 * on=HR:MN&off=HR:MN&day=[Sun..Sat]&idle=mins
 */
static bool setWiFiDisplayTimes (WiFiClient &client, char line[], size_t line_len)
{

    if (brOnOffOk()) {

        // define all possible args
        WebArgs wa;
        wa.nargs = 0;
        wa.name[wa.nargs++] = "on";
        wa.name[wa.nargs++] = "off";
        wa.name[wa.nargs++] = "day";
        wa.name[wa.nargs++] = "idle";

        // parse
        if (!parseWebCommand (wa, line, line_len))
            return (false);

        // handy
        const char *on = wa.value[0];
        const char *off = wa.value[1];
        const char *day = wa.value[2];
        const char *idle = wa.value[3];

        // crack -- need at least one of on&off or idle
        int on_hr = -1, on_mn = -1, off_hr = -1, off_mn = -1, idle_mins = -1;
        bool found_onoff = wa.found[0] && on && wa.found[1] && off
                            && sscanf (on, "%d:%d", &on_hr, &on_mn) == 2
                            && sscanf (off, "%d:%d", &off_hr, &off_mn) == 2;
        bool found_idle = wa.found[3] && idle && atoiOnly(idle,&idle_mins);

        // check idle
        if (found_idle && idle_mins < 0) {
            strcpy (line, "Invalid idle");
            return (false);
        }

        // pack times and validate
        int dow = -1;
        uint16_t on_mins = 0;
        uint16_t off_mins = 0;
        bool found_day = wa.found[2] && day;
        if (found_onoff) {

            on_mins = on_hr*60 + on_mn;
            off_mins = off_hr*60 + off_mn;
            if (on_mins >= MINSPERDAY || off_mins >= MINSPERDAY) {
                strcpy (line, "Invalid time");
                return (false);
            }

            // default today if no dow
            if (found_day) {
                if (!crackDOW (day, dow)) {
                    strcpy (line, "Invalid day");
                    return (false);
                }
            } else {
                // imply today if no day
                dow = DEWeekday();
            }
        } else if (found_day) {
            strcpy (line, "day requires on and off");
            return (false);
        }

        if (!found_onoff && !found_idle) {
            strcpy (line, garbcmd);
            return (false);
        }

        // engage
        if (!setDisplayOnOffTimes (dow, on_mins, off_mins, idle_mins)) {
            strcpy (line, notsupp);
            return (false);
        }

        // ack
        startPlainText (client);
        char buf[100];

        if (found_onoff) {
            client.print ("On    ");
            snprintf (buf, sizeof(buf), "%02d:%02d", on_hr, on_mn);
            client.println (buf);
            client.print ("Off   ");
            snprintf (buf, sizeof(buf), "%02d:%02d", off_hr, off_mn);
            client.println (buf);
            client.print ("Day   ");
            strcpy (buf, dayShortStr(dow));
            client.println (buf);
        }

        if (found_idle) {
            client.print ("Idle  ");
            client.println (idle_mins);
        }

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}



/* set DE or DX from lat/long or maidenhead.
 * also allowing setting callsign if new de.
 */
static bool setWiFiNewDEDX_helper (WiFiClient &client, bool new_dx, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "lat";
    wa.name[wa.nargs++] = "lng";
    wa.name[wa.nargs++] = "grid";
    wa.name[wa.nargs++] = "call";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *lat_spec = wa.value[0];
    const char *lng_spec = wa.value[1];
    const char *grid_spec = wa.value[2];
    const char *call = wa.value[3];

    // set automatic TZ
    setTZAuto (new_dx ? dx_tz : de_tz);

    // update call if set and this is a new de
    if (call) {
        if (new_dx) {
            strcpy (line, "may only set call for new DE");
            return (false);
        }
        if (setCallsign (call)) {
            updateCallsign (true);
        } else {
            strcpy (line, "invalid call");
            return (false);
        }
    }

    // check and engage location depending on what, if anything, is given
    if (lat_spec || lng_spec) {

        if (grid_spec) {
            strcpy (line, "do not mix lat/lng and grid");
            return (false);
        }

        // init with current de then update either or both
        LatLong ll = de_ll;

        if (lat_spec && !latSpecIsValid (lat_spec, ll.lat_d)) {
            strcpy (line, "bad lat");
            return (false);
        }
        if (lng_spec && !lngSpecIsValid (lng_spec, ll.lng_d)) {
            strcpy (line, "bad lng");
            return (false);
        }

        // engage
        if (new_dx)
            newDX (ll, NULL, NULL);
        else
            newDE (ll, NULL);

    } else if (grid_spec) {

        size_t gridlen = strlen(grid_spec);
        if (gridlen != 4 && gridlen != 6) {
            strcpy (line, "grid must be 4 or 6 chars");
            return (false);
        }
        LatLong ll;
        if (!maidenhead2ll (ll, grid_spec)) {
            strcpy (line, "bad grid");
            return (false);
        }

        // engage
        if (new_dx)
            newDX (ll, grid_spec, NULL);
        else
            newDE (ll, grid_spec);
    }

    // ack with updated info as if get
    return (getWiFiDEDXInfo_helper (client, line, line_len, new_dx));
}

/* set DE from grid or lat/lng
 */
static bool setWiFiNewDE (WiFiClient &client, char line[], size_t line_len)
{
    return (setWiFiNewDEDX_helper (client, false, line, line_len));
}

/* inject (or clear) a synthetic marine warning for testing the geofence + auto-popup logic
 * without needing a live NWS alert. runs through the exact same code path real backend data
 * would -- it's not just a fake pane switch.
 */
static bool setWiFiMarineTest (WiFiClient &client, char line[], size_t line_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "id";
    wa.name[wa.nargs++] = "lat";
    wa.name[wa.nargs++] = "lng";
    wa.name[wa.nargs++] = "minutes";
    wa.name[wa.nargs++] = "headline";
    wa.name[wa.nargs++] = "office";
    wa.name[wa.nargs++] = "clear";

    if (!parseWebCommand (wa, line, line_len))
        return (false);

    if (wa.found[6]) {
        clearTestMarineWarnings();
        startPlainText (client);
        client.println ("marine test warnings cleared");
        return (true);
    }

    const char *id_s     = wa.value[0];
    const char *lat_s    = wa.value[1];
    const char *lng_s    = wa.value[2];
    const char *min_s    = wa.value[3];
    const char *headline = wa.value[4];
    const char *office   = wa.value[5];

    if (!id_s || !lat_s || !lng_s) {
        strcpy (line, "require id, lat, lng (or clear)");
        return (false);
    }

    float lat = atof (lat_s);
    float lng = atof (lng_s);
    int minutes = min_s ? atoi (min_s) : 15;

    if (!injectTestMarineWarning (id_s, office, lat, lng, minutes, headline)) {
        strcpy (line, "inject failed");
        return (false);
    }

    startPlainText (client);
    client.println ("ok");
    return (true);
}

/* same as setWiFiMarineTest but for Fire Wx */
static bool setWiFiFireTest (WiFiClient &client, char line[], size_t line_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "id";
    wa.name[wa.nargs++] = "lat";
    wa.name[wa.nargs++] = "lng";
    wa.name[wa.nargs++] = "minutes";
    wa.name[wa.nargs++] = "headline";
    wa.name[wa.nargs++] = "office";
    wa.name[wa.nargs++] = "clear";

    if (!parseWebCommand (wa, line, line_len))
        return (false);

    if (wa.found[6]) {
        clearTestFireWx();
        startPlainText (client);
        client.println ("fire wx test warnings cleared");
        return (true);
    }

    const char *id_s     = wa.value[0];
    const char *lat_s    = wa.value[1];
    const char *lng_s    = wa.value[2];
    const char *min_s    = wa.value[3];
    const char *headline = wa.value[4];
    const char *office   = wa.value[5];

    if (!id_s || !lat_s || !lng_s) {
        strcpy (line, "require id, lat, lng (or clear)");
        return (false);
    }

    float lat = atof (lat_s);
    float lng = atof (lng_s);
    int minutes = min_s ? atoi (min_s) : 15;

    if (!injectTestFireWx (id_s, office, lat, lng, minutes, headline)) {
        strcpy (line, "inject failed");
        return (false);
    }

    startPlainText (client);
    client.println ("ok");
    return (true);
}

/* inject (or clear) a synthetic earthquake for testing the Quakes pane */
static bool setWiFiQuakeTest (WiFiClient &client, char line[], size_t line_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "id";
    wa.name[wa.nargs++] = "lat";
    wa.name[wa.nargs++] = "lng";
    wa.name[wa.nargs++] = "mag";
    wa.name[wa.nargs++] = "depth";
    wa.name[wa.nargs++] = "place";
    wa.name[wa.nargs++] = "clear";

    if (!parseWebCommand (wa, line, line_len))
        return (false);

    if (wa.found[6]) {
        clearTestQuakes();
        startPlainText (client);
        client.println ("quake test events cleared");
        return (true);
    }

    const char *id_s    = wa.value[0];
    const char *lat_s   = wa.value[1];
    const char *lng_s   = wa.value[2];
    const char *mag_s   = wa.value[3];
    const char *depth_s = wa.value[4];
    const char *place   = wa.value[5];

    if (!id_s || !lat_s || !lng_s) {
        strcpy (line, "require id, lat, lng (or clear)");
        return (false);
    }

    float lat   = atof (lat_s);
    float lng   = atof (lng_s);
    float mag   = mag_s ? atof (mag_s) : 4.0;
    float depth = depth_s ? atof (depth_s) : 10.0;

    if (!injectTestQuake (id_s, mag, depth, lat, lng, place)) {
        strcpy (line, "inject failed");
        return (false);
    }

    startPlainText (client);
    client.println ("ok");
    return (true);
}

/* set DX from grid or lat/lng
 */
static bool setWiFiNewDX (WiFiClient &client, char line[], size_t line_len)
{
    return (setWiFiNewDEDX_helper (client, true, line, line_len));
}

/* set a map view color, names must match those displayed in Setup after changing all '_' to ' '
 *   setup=name&color=R,G,B" },
 */
static bool setWiFiMapColor (WiFiClient &client, char line[], size_t line_len)
{
    (void)(line_len);

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "setup";
    wa.name[wa.nargs++] = "color";
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // crack color into rgb by value or by name
    int r, g, b;
    if (!wa.found[1]) {
        strcpy (line, "missing color");
        return (false);
    }
    if (sscanf (wa.value[1], "%d,%d,%d", &r, &g, &b) == 3) {
        if (!rgbOk (r, g, b)) {
            strcpy (line, "bad rgb values");
            return (false);
        }
    } else if (!findColorName(wa.value[1],r,g,b)) {
        startPlainText (client);
        client.print("color names:\n");
        printColorNames (client);
        return (true);
    }

    // try to install
    if (!wa.found[0]) {
        strcpy (line, "missing setup name");
        return (false);
    }
    if (!setMapColor (wa.value[0], RGB565(r,g,b))) {
        strcpy (line, "unknown setup name");
        return (false);
    }

    // it worked, so restart map
    initEarthMap();

    // update panes that use band colors -- they know whether they are really in use
    scheduleNewPlot(PLOT_CH_PSK);
    scheduleNewPlot(PLOT_CH_DXCLUSTER);
    scheduleNewPlot(PLOT_CH_ONTA);
    if (brb_mode == BRB_SHOW_BEACONS)
        (void) drawNCDXFBox();

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}

/* set a new map center longitude
 */
static bool setWiFiMapCenter (WiFiClient &client, char line[], size_t line_len)
{
    // crack args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "lng";
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // get candidate longitude
    float new_lng;
    if (!lngSpecIsValid (wa.value[0], new_lng)) {
        strcpy (line, "bad longitude");
        return (false);
    }

    // set and restart map if showing map that uses this
    setCenterLng (new_lng);
    if (map_proj == MAPP_MERCATOR || map_proj == MAPP_ROB)
        initEarthMap();

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}



/* set one or more view features of the map, same as menu, but NOT the VOACAP ones, leave those to set_voacap.
 * syntax: Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off
 * all keywords optional but require at least 1.
 */
static bool setWiFiMapView (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "Style";
    wa.name[wa.nargs++] = "Grid";
    wa.name[wa.nargs++] = "Projection";
    wa.name[wa.nargs++] = "RSS";
    wa.name[wa.nargs++] = "Night";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *S = wa.value[0];
    const char *G = wa.value[1];
    const char *P = wa.value[2];
    const char *R = wa.value[3];
    const char *N = wa.value[4];

    // require at least 1
    if (!S && !G && !P && !R && !N) {
        strcpy (line, "no args");
        return (false);
    }

    // check style -- NOT VOACAP
    CoreMaps my_cm = CM_NONE;
    if (S) {
        for (int i = 0; i < CM_N; i++) {
            if (strcmp (S, cm_info[i].name) == 0) {
                my_cm = (CoreMaps) i;
                break;
            }
        }
        if (my_cm == CM_NONE || my_cm == CM_PMTOA || my_cm == CM_PMREL) {
            // reply with list of possibilites
            size_t ll = snprintf (line, line_len, "Styles:");
            for (int i = 0; i < CM_N; i++)
                if (i != CM_PMTOA && i != CM_PMREL)
                    ll += snprintf (line+ll, line_len-ll, " %s", cm_info[i].name);
            return (false);
        }
    }

    // check grid
    int my_llg = -1;
    if (G) {
        for (int i = 0; i < MAPGRID_N; i++) {
            if (strcmp (G, grid_styles[i]) == 0) {
                my_llg = i;
                break;
            }
        }
        if (my_llg < 0) {
            // reply with list of possibilites
            size_t ll = snprintf (line, line_len, "Grid: %s", grid_styles[0]);
            for (int i = 1; i < MAPGRID_N; i++)
                ll += snprintf (line+ll, line_len-ll, ", %s", grid_styles[i]);
            return (false);
        }
    }

    // check projection
    int my_proj = -1;
    if (P) {
        for (int i = 0; i < MAPP_N; i++) {
            if (strcmp (P, map_projnames[i]) == 0) {
                my_proj = i;
                break;
            }
        }
        if (my_proj < 0) {
            // reply with list of possibilites
            size_t ll = snprintf (line, line_len, "Projection: %s", map_projnames[0]);
            for (int i = 1; i < MAPP_N; i++)
                ll += snprintf (line+ll, line_len-ll, ", %s", map_projnames[i]);
            return (false);
        }
    }

    // check RSS
    int my_rss = -1;
    if (R) {
        if (!strcmp (R, "on"))
            my_rss = 1;
        else if (!strcmp (R, "off"))
            my_rss = 0;
        else {
            strcpy (line, "RSS: on or off");
            return (false);
        }
    }

    // check Night
    int my_night = -1;
    if (N) {
        if (!strcmp (N, "on"))
            my_night = 1;
        else if (!strcmp (N, "off"))
            my_night = 0;
        else {
            strcpy (line, "Night: on or off");
            return (false);
        }
    }

    // all options look good, engage any that have changed.
    // this is rather like drawMapMenu().

    if (S && my_cm != core_map) {
        // just schedule for updating
        scheduleNewCoreMap (my_cm);
    }
    if (G && my_llg != mapgrid_choice) {
        mapgrid_choice = my_llg;
        NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
    }
    if (P && my_proj != map_proj) {
        map_proj = my_proj;
        NVWriteUInt8 (NV_MAPPROJ, map_proj);
    }
    if (N && my_night != night_on) {
        night_on = my_night;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
    }
    if (R && my_rss != rss_on) {
        rss_on = my_rss;
        NVWriteUInt8 (NV_RSS_ON, rss_on);
    }

    // restart map
    initEarthMap();

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}

/* set a new correction for the given sensor
 *   sensor=76|77&dTemp=X&dPres=Y" },
 */
static bool setWiFiSensorCorr (WiFiClient &client, char line[], size_t line_len)
{
    if (getNBMEConnected() == 0) {
        strcpy (line, "No sensors");
        return (false);
    }

    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "sensor";
    wa.name[wa.nargs++] = "dTemp";
    wa.name[wa.nargs++] = "dPres";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *S = wa.value[0];
    const char *T = wa.value[1];
    const char *P = wa.value[2];

    // sensor is required
    if (!S) {
        strcpy (line, "missing sensor");
        return (false);
    }
    int sensor_i2c;
    BMEIndex sensor_idx;
    bool s_ok = atoiOnly (S, &sensor_i2c);
    if (s_ok) {
        if (sensor_i2c == 76)
            sensor_idx = BME_76;
        else if (sensor_i2c == 77)
            sensor_idx = BME_77;
        else
            s_ok = false;
    }
    if (!s_ok) {
        strcpy (line, "sensor must be 76 or 77");
        return (false);
    }

    // at least one of T and P are required
    if (!T && !P) {
        strcpy (line, "missing value");
        return (false);
    }

    // try dPres if set
    if (P) {
        if (!recalBMEPres (sensor_idx, atof(P))) {
            strcpy (line, "bad dPres sensor");
            return (false);
        }
    }

    // try dTemp if set
    if (T) {
        if (!recalBMETemp (sensor_idx, atof(T))) {
            strcpy (line, "bad dTemp sensor");
            return (false);
        }
    }

    // ack
    startPlainText (client);
    client.print("ok\n");

    // good
    return (true);
}

/* load an ADIF file via POST or turn off.
 */
static bool setWiFiADIF (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "pane";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    if (wa.found[0]) {

        // pane arg
        int pane = wa.value[0] ? atoi(wa.value[0]) : PANE_N;    // bogus pane if not specified
        if (pane < PANE_0 || pane >= PANE_N) {
            snprintf (line, line_len, "pane must be %d .. %d", PANE_0, PANE_N-1);
            return (false);
        }
        PlotPane pp = (PlotPane)pane;

        // already up elsewhere?
        PlotPane adif_pp = findPaneForChoice(PLOT_CH_ADIF);
        if (adif_pp != PANE_NONE && adif_pp != pp) {
            snprintf (line, line_len, "ADIF already in pane %d", adif_pp);
            return (false);
        }

        // assign if not already
        if (adif_pp == PANE_NONE && setPlotChoice (pp, PLOT_CH_ADIF)) {
            adif_pp = pp;
            plot_rotset[adif_pp] |= PLOTBIT(PLOT_CH_ADIF);
            savePlotOps();
        }

        // create GenReader using existing client
        GenReader gr (client, content_length);

        // process POST content which immediately follows header
        int n_good, n_bad;
        loadADIFFile (gr, n_good, n_bad);
        if (n_good == 0) {
            strcpy (line, "No qualifying spots found");
            return (false);
        }
        drawADIFPane (plot_b[adif_pp], "via set_adif");

        // ack count
        startPlainText (client);
        char msg[100];
        snprintf (msg, sizeof(msg), "found %d qualifying %d busted spots\n", n_good, n_bad);
        client.print (msg);

        return (true);

    } else {
        strcpy (line, garbcmd);
        return (false);
    }
}

/* load a BMP file via POST else turn off.
 */
static bool setWiFiloadBMP (WiFiClient &client, char line[], size_t line_len)
{
    // handy wa indices
    enum {
        LBMP_PANE,
        LBMP_FIT,
        LBMP_OFF,
        LBMP_N
    };

    // define all possible args
    WebArgs wa;
    wa.name[LBMP_PANE] = "pane";
    wa.name[LBMP_FIT] = "fit";
    wa.name[LBMP_OFF] = "off";
    wa.nargs = LBMP_N;

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // pane arg is always required
    if (!wa.found[LBMP_PANE] || !wa.value[LBMP_PANE]) {
        snprintf (line, line_len, "pane %d..%d or map is required", PANE_1, PANE_N-1);
        return(false);
    }
    const char *pane_arg = wa.value[LBMP_PANE];

    // crack fit if given
    ImageRefit fit = FIT_RESIZE;         // not a default, just required lint
    bool found_fit = false;
    if (wa.found[LBMP_FIT] && wa.value[LBMP_FIT]) {
        if (strcasecmp (wa.value[LBMP_FIT], "resize") == 0) {
            fit = FIT_RESIZE;
            found_fit = true;
        } else if (strcasecmp (wa.value[LBMP_FIT], "crop") == 0) {
            fit = FIT_CROP;
            found_fit = true;
        } else if (strcasecmp (wa.value[LBMP_FIT], "fill") == 0) {
            fit = FIT_FILL;
            found_fit = true;
        }
    }
    // fit is required unless off
    if (found_fit && wa.found[LBMP_OFF]) {
        strcpy (line, garbcmd);
        return (false);
    }
    if (!wa.found[LBMP_OFF] && !found_fit) {
        snprintf (line, line_len, "fit=[resize,crop,fill]");
        return(false);
    }

    // "map" or pane number
    if (strcmp (pane_arg, "map") == 0) {

        if (wa.found[LBMP_OFF]) {

            // remove user map

            // fit makes no sense
            if (wa.found[LBMP_FIT]) {
                strcpy (line, garbcmd);
                return (false);
            }

            // remove supporing files
            rmWebMapImages ();

            // insure not being displayed
            if (IS_CMROT(CM_USER)) {
                Serial.printf ("removing CM_USER from rotset\n");
                RM_CMROT (CM_USER);
                insureCoreMap();
                saveCoreMaps();
                logMapRotSet();
                scheduleNewCoreMap (core_map);
            }


        } else {

            // install user map

            // content length is required
            if (content_length == 0) {
                quietStrncpy (line, "missing header Content-Length", line_len);
                return (false);
            }

            // POST content immediately follows header
            Message ynot;
            if (!installWebMapImages (client, content_length, fit, ynot)) {
                quietStrncpy (line, ynot.get(), line_len);
                return (false);
            }

            // engage
            scheduleNewCoreMap (CM_USER);
        }

    } else {

        // show image in specified pane

        int pane = atoi(pane_arg);
        if (pane < PANE_1 || pane >= PANE_N) {
            snprintf (line, line_len, "pane %d..%d or map", PANE_1, PANE_N-1);
            return(false);
        }

        // show file else restore
        if (wa.found[LBMP_OFF]) {

            // "off" means restore pane content
            scheduleNewPlot(plot_ch[pane]);

        } else {

            // POST content immediately follows header
            GenReader gr (client);
            Message ynot;
            if (!installBMPBox (gr, plot_b[pane], fit, ynot)) {
                quietStrncpy (line, ynot.get(), line_len);
                return (false);
            }
        }
    }


    // ack
    startPlainText (client);
    client.print ("ok\n");
    return (true);
}

/*  set desire pan and/or zoom, allow either abs or diff panning.
 *    pan_x=X&pan_y=Y&pan_dx=dX&pan_dy=dY&zoom=Z" },
 */
static bool setWiFiPanZoom (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "pan_x";              // 0
    wa.name[wa.nargs++] = "pan_y";              // 1
    wa.name[wa.nargs++] = "pan_dx";             // 2
    wa.name[wa.nargs++] = "pan_dy";             // 3
    wa.name[wa.nargs++] = "zoom";               // 4

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // get copy to update
    PanZoom new_pz = pan_zoom;


    // crack zoom
    if (wa.found[4]) {
        if (map_proj != MAPP_MERCATOR) {
            snprintf (line, line_len, "Zooming requires Mercator projection");
            return (false);
        }
        new_pz.zoom = atoi (wa.value[4]);
    }


    // crack x panning
    if ((wa.found[0] || wa.found[2]) && !(map_proj == MAPP_MERCATOR || map_proj == MAPP_ROB)) {
        snprintf (line, line_len, "Panning X requires Mercator or Robinson projection");
        return (false);
    }
    if (wa.found[0] && wa.found[2]) {
        snprintf (line, line_len, "Use only one of pan_x or pan_dx");
        return (false);
    }
    if (wa.found[0])
        new_pz.pan_x = atoi(wa.value[0]);
    else if (wa.found[2])
        new_pz.pan_x += atoi(wa.value[2]);


    // crack y panning
    if ((wa.found[1] || wa.found[3]) && map_proj != MAPP_MERCATOR) {
        snprintf (line, line_len, "Panning Y requires Mercator projection");
        return (false);
    }
    if (wa.found[1] && wa.found[3]) {
        snprintf (line, line_len, "Use only one of pan_y or pan_dy");
        return (false);
    }
    if (wa.found[1])
        new_pz.pan_y = atoi(wa.value[1]);
    else if (wa.found[3])
        new_pz.pan_y += atoi(wa.value[3]);


    // check ranges
    if (new_pz.zoom < MIN_ZOOM || new_pz.zoom > MAX_ZOOM) {
        snprintf (line, line_len, "Zoom must be [%d,%d]", MIN_ZOOM, MAX_ZOOM);
        return (false);
    }
    if (new_pz.pan_x < MIN_PANX || new_pz.pan_x > MAX_PANX) {
        snprintf (line, line_len, "Pan x must be [%d,%d)", MIN_PANX, MAX_PANX);
        return (false);
    }
    if (new_pz.pan_y < MIN_PANY(new_pz.zoom) || new_pz.pan_y > MAX_PANY(new_pz.zoom)) {
        snprintf (line, line_len, "Pan y must be [%d,%d]", MIN_PANY(new_pz.zoom), MAX_PANY(new_pz.zoom));
        return (false);
    }

    // one more check
    normalizePanZoom (new_pz);

    // save and go if any change, else just report
    if (memcmp (&pan_zoom, &new_pz, sizeof(pan_zoom))) {
        pan_zoom = new_pz;
        NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
        NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
        NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
        initEarthMap();
        scheduleFreshMap();
    }

    // ack
    startPlainText (client);
    char buf[100];
    snprintf (buf, sizeof(buf), "Pan now %d,%d zoom %dx\n", pan_zoom.pan_x, pan_zoom.pan_y, pan_zoom.zoom);
    client.print (buf);
    return (true);

}


/* control RSS list:
 *    reset      empty local list
 *    add=X      add 1 to local list
 *    network    resume network connection
 *    file       list of titles follows header in POST format
 *    interval   set refresh interval, secs, min 5
 *    on         display RSS, be it local or network
 *    off        do not display RSS
 */
static bool setWiFiRSS (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "reset";              // 0
    wa.name[wa.nargs++] = "add";                // 1
    wa.name[wa.nargs++] = "file";               // 2
    wa.name[wa.nargs++] = "network";            // 3
    wa.name[wa.nargs++] = "interval";           // 4
    wa.name[wa.nargs++] = "on";                 // 5
    wa.name[wa.nargs++] = "off";                // 6

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    int n_titles, n_max;

    // send ok unless set first
    char buf[150] = "ok\n";

    if (wa.found[3] && wa.value[3] == NULL) {
        // restore normal rss network queries
        (void) setRSSTitle (NULL, n_titles, n_max);
        snprintf (buf, sizeof(buf), "Restored RSS network feeds\n");

    } else if (wa.found[0] && wa.value[0] == NULL) {
        // turn off network and empty local list
        (void) setRSSTitle ("", n_titles, n_max);
        snprintf (buf, sizeof(buf), "List is reset\n");

    } else if (wa.found[1] && wa.value[1] != NULL) {
        // turn off network and add title to local list if room
        if (!setRSSTitle (wa.value[1], n_titles, n_max)) {
            snprintf (line, line_len, "List is full -- max %d", n_max);
            return (false);
        }
        snprintf (buf, sizeof(buf), "List now contains %d titles\n", n_titles);

    } else if (wa.found[2] && wa.value[2] == NULL) {
        // titles follow header
        (void) setRSSTitle ("", n_titles, n_max);       // reset list
        long nr = 0;                                    // assume getTCPLine stripped off \n not \r too
        uint16_t ll = 0;                                // each line length

        // use content_length to avoid waiting for time out after last line
        char line[150];
        while ((!content_length || (nr += ll+1) < content_length)
                                && getTCPLine (client, line, sizeof(line), &ll))
            (void) setRSSTitle (line, n_titles, n_max);
        snprintf (buf, sizeof(buf), "List now contains %d titles\n", n_titles);

    } else if (wa.found[4] && wa.value[4] != NULL) {
        int new_i;
        if (atoiOnly (wa.value[4], &new_i) && new_i >= RSS_MIN_INT) {
            rss_interval = new_i;
            snprintf (buf, sizeof(buf), "RSS interval now %d secs\n", rss_interval);
            NVWriteUInt8 (NV_RSS_INTERVAL, rss_interval);
        } else {
            snprintf (line, line_len, "Min interval is %d seconds", RSS_MIN_INT);
            return (false);
        }

    } else if (wa.found[5] && wa.value[5] == NULL) {
        // turn on display with immediate update
        rss_on = 1;;
        initEarthMap();                                 // immediate on
        NVWriteUInt8 (NV_RSS_ON, rss_on);

    } else if (wa.found[6] && wa.value[6] == NULL) {
        // turn off
        if (rss_on) {
            rss_on = 0;
            initEarthMap();                             // immediate erase
            NVWriteUInt8 (NV_RSS_ON, rss_on);
        }

    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // ack
    startPlainText (client);
    client.print (buf);
    Serial.print (buf);

    // ok
    return (true);
}

/* set new collection of plot choices for a given pane.
 * return whether ok
 */
static bool setWiFiPane (WiFiClient &client, char line[], size_t line_len)
{
    // first arg is pane number
    int pane;
    char *equals;
    if (sscanf (line, "Pane%d", &pane) != 1 || (equals = strchr(line,'=')) == NULL) {
        strcpy (line, garbcmd);
        return (false);
    }
    if (pane < PANE_0 || pane >= PANE_N) {
        strcpy (line, "Bad pane num");
        return (false);
    }
    PlotPane pp = (PlotPane)(pane);

    // look for special case Pane0=off if on now
    if (strcmp (line, "Pane0=off") == 0) {
        if (SHOWING_PANE_0()) {
            restoreNormPANE0();
            startPlainText (client);
            client.println ("ok");
        } else {
            strcpy (line, "Pane 0 is not on");
            return (false);
        }
    } else {

        // convert remaining args to list of PlotChoices
        PlotChoice pc[PLOT_CH_N];           // max size, only first n_pc in use
        int n_pc = 0;
        char *start = equals + 1;
        for (char *tok = NULL; (tok = strtok (start, ",")) != NULL; start = NULL) {

            // tok is within line, so copy it so we can use line for err msg
            char tok_copy[30];
            strncpy (tok_copy, tok, sizeof(tok_copy));
            tok_copy[sizeof(tok_copy)-1] = '\0';

            // find tok in plot_names
            PlotChoice tok_pc = PLOT_CH_NONE;
            for (int i = 0; i < PLOT_CH_N; i++) {
                if (PLOT_CH_IS_REAL(i) && strcmp (tok_copy, plot_names[i]) == 0) {
                    tok_pc = (PlotChoice)i;
                    break;
                }
            }

            // found?
            if (tok_pc == PLOT_CH_NONE) {
                snprintf (line, line_len, "Unknown choice for pane %d: %s", pane, tok_copy);
                return (false);
            }

            // ok on PANE_0?
            if (pp == PANE_0 && (PLOTBIT(tok_pc) & ~PANE_0_CH_MASK) != 0) {
                strcpy (line, "not supported on Pane 0");
                return (false);
            }

            // in use elsewhere?
            PlotPane inuse_pp = findPaneForChoice(tok_pc);
            if (inuse_pp != PANE_NONE && inuse_pp != pp) {
                snprintf (line, line_len, "%s already set in pane %d", tok_copy, (int)inuse_pp);
                return (false);
            }

            // available?
            if (!plotChoiceIsAvailable(tok_pc)) {
                snprintf (line, line_len, "%s is not available", tok_copy);
                return (false);
            }

            // room for more?
            if (n_pc == PLOT_CH_N) {
                snprintf (line, line_len, "too many choices");
                return (false);
            }

            // ok!
            pc[n_pc++] = tok_pc;
        }

        // require at least 1
        if (n_pc == 0) {
            snprintf (line, line_len, "specify at least one choice for pane %d", pane);
            return (false);
        }

        // build candidate rotset
        PlotMask new_rotset = 0;
        for (int i = 0; i < n_pc; i++)
            new_rotset |= PLOTBIT(pc[i]);

        // ok! engage new rotset and show first in list
        plot_rotset[pp] = new_rotset;
        if (!setPlotChoice (pp, pc[0])) {
            snprintf (line, line_len, "%s failed for pane %d", plot_names[pc[0]], pane);
            return (false);
        }

        // note
        logPaneRotSet(pp, pc[0]);

        // ok!
        startPlainText (client);
        reportPaneChoices (client, pp);
    }

    // good
    return (true);
}



/* try to set the satellite to the given name.
 * return whether command is successful.
 */
static bool setWiFiSatName (WiFiClient &client, char line[], size_t line_len)
{
    // do it
    if (setSatFromName (line))
        return (getWiFiSatellite (client, line, line_len));

    // nope
    strcpy (line, "Unknown sat");
    return (false);
}

/* set satellite from given TLE: set_sattle?name=n&t1=line1&t2=line2
 * return whether command is successful.
 */
static bool setWiFiSatTLE (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "name";
    wa.name[wa.nargs++] = "t1";
    wa.name[wa.nargs++] = "t2";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *name = wa.value[0];
    const char *t1 = wa.value[1];
    const char *t2 = wa.value[2];
    if (!name || !t1 || !t2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // enforce known line lengths
    if (strlen(t1) != TLE_LINEL-1) {
        strcpy (line, "bogus t1");
        return(false);
    }
    if (strlen(t2) != TLE_LINEL-1) {
        strcpy (line, "bogus t2");
        return(false);
    }

    // try to install
    if (setSatFromTLE (name, t1, t2))
        return (getWiFiSatellite (client, line, line_len));

    // nope
    strcpy (line, "Bad spec");
    return (false);
}

/* remote command to control stopwatch engine state
 */
static bool setWiFiStopwatch (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "reset";
    wa.name[wa.nargs++] = "run";
    wa.name[wa.nargs++] = "stop";
    wa.name[wa.nargs++] = "lap";
    wa.name[wa.nargs++] = "countdown";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // crack
    SWEngineState sws;
    int mins = 0;
    if (wa.found[4] && atoiOnly(wa.value[4], &mins)) {
        sws = SWE_COUNTDOWN;
    } else if (wa.found[0] && wa.value[0] == NULL) {
        sws = SWE_RESET;
    } else if (wa.found[1] && wa.value[1] == NULL) {
        sws = SWE_RUN;
    } else if (wa.found[2] && wa.value[2] == NULL) {
        sws = SWE_STOP;
    } else if (wa.found[3] && wa.value[3] == NULL) {
        sws = SWE_LAP;
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // engage
    if (!setSWEngineState (sws, mins*60000)) {        // mins -> ms
        strcpy (line, "State is not applicable");
        return (false);
    }

    // turn off any PLOT_CH_COUNTDOWN if no longer applicable
    insureCountdownPaneSensible();

    // ack
    startPlainText(client);
    char buf[100];
    getStopwatchReport (buf, sizeof(buf));
    client.print(buf);

    // ok
    return (true);
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "ISO";
    wa.name[wa.nargs++] = "unix";
    wa.name[wa.nargs++] = "Now";
    wa.name[wa.nargs++] = "change";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);


    // crack
    if (wa.found[3] && wa.value[3] != NULL) {

        changeTime (nowWO() + atol(wa.value[3]));

    } else if (wa.found[2] && wa.value[2] == NULL) {

        changeTime (0);

    } else if (wa.found[1] && wa.value[1] != NULL) {

        // crack and engage
        changeTime (atol(wa.value[1]));

    } else if (wa.found[0] && wa.value[0] != NULL) {

        // convert and engage if ok
        time_t new_t = crackISO8601 (wa.value[0]);
        if (new_t)
            changeTime (new_t);
        else {
            strcpy (line, "garbled ISO");
            return (false);
        }

    } else {

        strcpy (line, garbcmd);
        return (false);
    }

    // reply
    startPlainText(client);
    char buf[30];
    snprintf (buf, sizeof(buf), "UNIX_time %ld\n", (long int) nowWO());
    client.print (buf);

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET.
 * doesn nothing if coords are within map_b.
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // require x and y within screen size
    int x, y;
    if (!atoiOnly(wa.value[0],&x) || x < 0 || x >= tft.width()
                || !atoiOnly(wa.value[1],&y) || y < 0 || y >= tft.height()) {
        snprintf (line,line_len, "require 0 .. x .. %d and 0 .. y .. %d", tft.width()-1, tft.height()-1);
        return (false);
    }

#ifdef _NO_IN_MAP_TOUCH         // why did we ever prevent this?
    // require x and y outside map_b
    SCoord s = {(uint16_t)x, (uint16_t)y};
    if (inBox (s, map_b)) {
        snprintf (line, line_len, "touch coords must be outside the map");
        return (false);
    }
#endif

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = TT_TAP;

    // ack
    startPlainText (client);
    char buf[100];
    snprintf (buf, sizeof(buf), "Web touch %d %d\n", x, y);
    client.print(buf);

    // ok
    return (true);
}

/* set a user GPIO pin
 *   pin=MCP&level=[hi,lo]&blink=hz
 */
static bool setWiFiGPIO (WiFiClient &client, char line[], size_t line_len)
{
    enum {
        SETG_PIN,
        SETG_LEVEL,
        SETG_BLINK
    };

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "pin";
    wa.name[wa.nargs++] = "level";
    wa.name[wa.nargs++] = "blink";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // get pin
    int pin = 0;
    if (wa.found[SETG_PIN])
        pin = atoi (wa.value[SETG_PIN]);
    else {
        snprintf (line, line_len, "MCP pin is required");
        return (false);
    }

    // exactly one of level or blink is required
    if (wa.found[SETG_LEVEL] == wa.found[SETG_BLINK]) {
        snprintf (line, line_len, "set one of level or blink");
        return (false);
    }

    Message ynot;

    // check level
    if (wa.found[SETG_LEVEL]) {
        if (strcasecmp (wa.value[SETG_LEVEL], "hi") == 0 || strcmp (wa.value[SETG_LEVEL], "1") == 0) {
            if (!setUserGPIO (pin, BLINKER_ON, ynot)) {
                snprintf (line, line_len, "%s", ynot.get());
                return (false);
            }
        } else if (strcasecmp (wa.value[SETG_LEVEL], "lo") == 0 || strcmp (wa.value[SETG_LEVEL], "0") == 0) {
            if (!setUserGPIO (pin, BLINKER_OFF, ynot)) {
                snprintf (line, line_len, "%s", ynot.get());
                return (false);
            }
        } else {
            snprintf (line, line_len, "level must be hi or lo");
            return (false);
        }
    }

    // check blink
    if (wa.found[SETG_BLINK]) {
        int hz = atoi (wa.value[SETG_BLINK]);
        if (!setUserGPIO (pin, hz, ynot)) {
            snprintf (line, line_len, "%s", ynot.get());
            return (false);
        }
    }


    // ack
    startPlainText (client);
    client.print("ok\n");
    return (true);
}

/* set the VOACAP DE-DX map options
 * return whether all ok.
 */
static bool setWiFiVOACAP (WiFiClient &client, char line[], size_t line_len)
{
    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "band";
    wa.name[wa.nargs++] = "map";
    wa.name[wa.nargs++] = "power";
    wa.name[wa.nargs++] = "tz";
    wa.name[wa.nargs++] = "mode";
    wa.name[wa.nargs++] = "TOA";

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // handy
    const char *B = wa.found[0] ? wa.value[0] : NULL;
    const char *T = wa.found[1] ? wa.value[1] : NULL;
    const char *P = wa.found[2] ? wa.value[2] : NULL;
    const char *Z = wa.found[3] ? wa.value[3] : NULL;
    const char *M = wa.found[4] ? wa.value[4] : NULL;
    const char *A = wa.found[5] ? wa.value[5] : NULL;

    // init new stats
    PropMapBand new_band = PROPBAND_N;
    CoreMaps new_map = CM_NONE;

    // crack band
    if (B) {
        int band;
        if (!atoiOnly (B, &band)) {
            strcpy (line, garbcmd);
            return (false);
        }
        // find its PropMapBand
        for (int i = 0; i < PROPBAND_N; i++) {
            if (propBand2Band((PropMapBand)i) == band) {
                new_band = (PropMapBand)i;
                break;
            }
        }
        if (new_band == PROPBAND_N) {
            strcpy (line, "band: one from VOACAP DE-DX");
            return (false);
        }
    }

    // crack map type
    if (T) {
        if (strcmp(T,"TOA") == 0) {
            new_map = CM_PMTOA;
        } else if (strcmp(T,"REL") == 0) {
            new_map = CM_PMREL;
        } else if (strcmp(T,"OFF") != 0) {
            strcpy (line, "type: REL or TOA or OFF");
            return (false);
        }
    }

    // crack power
    int new_power = bc_power;
    if (P) {
        int p;
        bool p_ok = atoiOnly (P,&p);
        bool p_legal = false;
        if (p_ok) {
            for (int i = 0; !p_legal && i < n_bc_powers; i++)
                if (p == bc_powers[i])
                    p_legal = true;
        }
        if (!p_ok || !p_legal) {
            strcpy (line, "power must be one from VOACAP menu");
            return (false);
        }
        new_power = p;
    }

    // crack timeline time zone
    int new_utc = bc_utc_tl;
    if (Z) {
        if (strcmp (Z, "UTC") == 0) {
            new_utc = 1;
        } else if (strcmp (Z, "DE") == 0) {
            new_utc = 0;
        } else {
            strcpy (line, "tl must be DE or UTC");
            return (false);
        }
    }

    // crack mode
    uint8_t new_modevalue = bc_modevalue;
    if (M) {
        new_modevalue = findBCModeValue(M);
        if (new_modevalue == 0) {
            strcpy (line, "mode must be one from VOACAP menu");
            return (false);
        }
    }

    // crack TOA
    float new_toa = bc_toa;
    if (A) {
        new_toa = atof(A);
        if (new_toa <= 0) {
            strcpy (line, "TOA must be > 0");
            return (false);
        }
    }

    // see if what changed
    if (new_power != bc_power || new_modevalue != bc_modevalue || new_toa != bc_toa
                                    || new_map != core_map || cm_info[new_map].band != new_band) {
        // save new values and update
        bc_power = new_power;
        bc_modevalue = new_modevalue;
        bc_utc_tl = new_utc;
        bc_toa = new_toa;
        if (new_map == CM_NONE) {
            // turn off maps entirely
            RM_CMROT (CM_PMTOA);
            RM_CMROT (CM_PMREL);
            insureCoreMap();
            scheduleNewCoreMap (core_map);
        } else {
            // set new map
            cm_info[new_map].band = new_band;
            scheduleNewCoreMap (new_map);
        }

    } else if (new_utc != bc_utc_tl) {
        // changing only timeline units, no need to recompute anything
        bc_utc_tl = new_utc;
    }

    // show DE-DX too if up
    scheduleNewPlot(PLOT_CH_BC);

    // ack
    startPlainText (client);
    char buf[200];
    size_t buf_l = snprintf (buf, sizeof(buf), "VOACAP ");
    if (CM_PMACTIVE())
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "%dm/%s", propBand2Band(cm_info[core_map].band),
                                                                   cm_info[core_map].name);
    else
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "map off");
    buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, ", power %d W", bc_power);
    buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, ", timeline %s", bc_utc_tl ? "UTC" : "DE");
    buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, ", mode %s", findBCModeName(bc_modevalue));
    buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, ", TOA>%.1f\n", bc_toa);
    client.print(buf);

    // ok
    return (true);
}



/* finish the wifi then restart
 */
static bool doWiFiReboot (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // send html header then close
    startPlainText(client);

#if defined(NO_HAMCLOCK_CONTROLS)
    client.println ("HamClock restart is not supported on this platform.");
    return (true);
#else
    client.println ("restarting ... bye for now.");
    wdDelay(100);
    client.flush();
    client.stop();
    wdDelay(1000);

    Serial.println ("restarting...");
    doReboot(false, false);

    // never returns but compiler doesn't know that
    return (true);
#endif
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    // prep for response but won't be one if we succeed with update
    startPlainText(client);

#if defined(NO_UPGRADE)
    client.println ("Software updates are not supported directly in this build.");
    return (true);
#else
    // proceed if newer version is available
    char ver[100];
    if (newVersionIsAvailable (ver, sizeof(ver))) {
        char msg[200];
        snprintf (msg, sizeof(msg), "updating from %s to %s ... \n", hc_version, ver);
        client.print(msg);
        doOTAupdate(ver);                               // never returns if successful
        client.println ("update failed");
    } else
        client.println ("You're up to date!");    // match tapping version

    return (true);
#endif
}

/* post diagnostics file
 */
static bool doWiFiPostDiags (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    Serial.printf ("webserver posting diagnostics:\n");

    startPlainText(client);
    if (postDiags())
        client.println ("ok");
    else
        client.println ("fail");

    return (true);
}

/* set locked mode on or off
 */
static bool setWiFiScreenLock (WiFiClient &client, char line[], size_t line_len)
{
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "lock";                       // 0

    // parse
    if (!parseWebCommand (wa, line, line_len))
        return (false);

    // engage
    if (wa.found[0]) {
        if (strcmp (wa.value[0], "on") == 0)
            setScreenLock (true);
        else if (strcmp (wa.value[0], "off") == 0)
            setScreenLock (false);
        else {
            snprintf (line, line_len, "must be on or off");
            return (false);
        }
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // ack
    startPlainText(client);
    client.print ("ok\n");

    // ok
    return (true);
}


/* change the live spots configuration
 *   spot=of|by&what=call|grid&show=maxdist|counts&data=psk|wspr|rbn&age=mins
 *   &bands=all|160,...,2,2200,630,4,23 (2200/630/4/23 mutually exclusive)
 */
static bool setWiFiLiveSpots (WiFiClient &client, char line[], size_t line_len)
{
    // handy names -- N.B. keep in sync!
    enum {
        _LS_SPOT, _LS_WHAT, _LS_DATA, _LS_BANDS, _LS_AGE, _LS_SHOW, _LS_PATH
    };

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "spot";
    wa.name[wa.nargs++] = "what";
    wa.name[wa.nargs++] = "data";
    wa.name[wa.nargs++] = "bands";
    wa.name[wa.nargs++] = "age";
    wa.name[wa.nargs++] = "show";
    wa.name[wa.nargs++] = "path";

    const char *usage =
        "spot=of|by&what=call|grid&show=maxdist|counts&data=psk|wspr|rbn&age=mins"
        "&bands=all|160,...,2,2200,630,4,23 (2200/630/4/23 mutually exclusive, 23=23cm)";

    // parse
    if (!parseWebCommand (wa, line, line_len)) {
        strncpy (line, usage, line_len);
        return (false);
    }

    // since components are optional we start with the current mask and modify
    uint8_t new_mask = psk_mask;

    if (wa.found[_LS_SPOT]) {
        const char *spot = wa.value[_LS_SPOT];
        if (strcmp (spot, "of") == 0)
            new_mask |= PSKMB_OFDE;
        else if (strcmp (spot, "by") == 0)
            new_mask &= ~PSKMB_OFDE;
        else {
            strcpy (line, "spot must be by or of");
            return (false);
        }
    }

    if (wa.found[_LS_WHAT]) {
        const char *what = wa.value[_LS_WHAT];
        if (strcmp (what, "call") == 0)
            new_mask |= PSKMB_CALL;
        else if (strcmp (what, "grid") == 0)
            new_mask &= ~PSKMB_CALL;
        else {
            strcpy (line, "what must be call or grid");
            return (false);
        }
    }

    if (wa.found[_LS_DATA]) {
        const char *data = wa.value[_LS_DATA];
        new_mask &= ~PSKMB_SRCMASK;
        if (strcmp (data, "psk") == 0)
            new_mask |= PSKMB_PSK;
        else if (strcmp (data, "wspr") == 0)
            new_mask |= PSKMB_WSPR;
        else if (strcmp (data, "rbn") == 0)
            new_mask |= PSKMB_RBN;
        else {
            strcpy (line, "data must be psk wspr or rbn");
            return (false);
        }
    }

    // check for RBN
    if (((new_mask & PSKMB_SRCMASK) == PSKMB_RBN) && (!(new_mask & PSKMB_CALL) || !(new_mask & PSKMB_OFDE))){
        strcpy (line, "RBN requires of-call");
        return (false);
    }

    // new set of bands, if set
    uint32_t new_bands = 0;
    if (wa.found[_LS_BANDS]) {
        // don't clobber value
        StackMalloc bands_mem(wa.value[_LS_BANDS]);
        char *bands = (char *) bands_mem.getMem();
        strcpy (bands, wa.value[_LS_BANDS]);
        if (strcmp (bands, "all") == 0) {
            // N.B. can't just set every bit: 2200/630/4/23cm are mutually exclusive in the Live
            // Spots pane (see checkPSKTouch() in pskreporter.cpp), so blindly setting all
            // HAMBAND_N bits would turn all four on at once. "all" here means "every band except
            // that at most one of the mutually exclusive group may be on" -- matches
            // initPSKState()'s own fresh-install default of everything-but-4m/2200m/23cm, with
            // 630m the one left on.
            new_bands = (1 << HAMBAND_N) - 1;
            new_bands &= ~(1 << HAMBAND_4M);
            new_bands &= ~(1 << HAMBAND_2200M);
            new_bands &= ~(1 << HAMBAND_23CM);
        } else {
            char *token, *string = bands;
            while ((token = strsep (&string, ",")) != NULL) {
                switch (atoi(token)) {
                case 160: new_bands |= (1 << HAMBAND_160M); break;
                case 80:  new_bands |= (1 << HAMBAND_80M);  break;
                case 60:  new_bands |= (1 << HAMBAND_60M);  break;
                case 40:  new_bands |= (1 << HAMBAND_40M);  break;
                case 30:  new_bands |= (1 << HAMBAND_30M);  break;
                case 20:  new_bands |= (1 << HAMBAND_20M);  break;
                case 17:  new_bands |= (1 << HAMBAND_17M);  break;
                case 15:  new_bands |= (1 << HAMBAND_15M);  break;
                case 12:  new_bands |= (1 << HAMBAND_12M);  break;
                case 10:  new_bands |= (1 << HAMBAND_10M);  break;
                case 6:   new_bands |= (1 << HAMBAND_6M);   break;
                case 2:   new_bands |= (1 << HAMBAND_2M);   break;
                case 2200:new_bands |= (1 << HAMBAND_2200M);break;
                case 630: new_bands |= (1 << HAMBAND_630M); break;
                case 4:   new_bands |= (1 << HAMBAND_4M);   break;
                case 23:  new_bands |= (1 << HAMBAND_23CM); break;   // matches token "23" or "23cm"
                default:
                    strcpy (line, "unknown band");
                    return (false);
                }
            }
            // 2200m, 630m, 4m and 23cm share a single menu group in the Live Spots pane and are
            // mutually exclusive there -- see checkPSKTouch() -- so reject any request that tries
            // to turn on more than one of them together, same as the UI would.
            int n_ex = ((new_bands & (1 << HAMBAND_2200M)) != 0)
                     + ((new_bands & (1 << HAMBAND_630M)) != 0)
                     + ((new_bands & (1 << HAMBAND_4M)) != 0)
                     + ((new_bands & (1 << HAMBAND_23CM)) != 0);
            if (n_ex > 1) {
                strcpy (line, "2200, 630, 4 and 23 (23cm) are mutually exclusive");
                return (false);
            }
        }
    } else {
        // no change if not specified
        new_bands = psk_bands;
    }

    // new age, if set
    uint16_t new_age = psk_maxage_mins;
    if (wa.found[_LS_AGE]) {
        int age = atoi (wa.value[_LS_AGE]);
        if (maxPSKageOk(age))
            new_age = age;
        else {
            strcpy (line, "bad age");
            return (false);
        }
    }

    // change show, if set
    uint8_t new_dist = psk_showdist;
    if (wa.found[_LS_SHOW]) {
        if (strcmp (wa.value[_LS_SHOW], "maxdist") == 0)
            new_dist = 1;
        else if (strcmp (wa.value[_LS_SHOW], "counts") == 0)
            new_dist = 0;
        else {
            strcpy (line, "show: maxdist or counts");
            return (false);
        }
    }

    // change path, if set
    uint8_t new_path = psk_showpath;
    if (wa.found[_LS_PATH]) {
        if (strcmp (wa.value[_LS_PATH], "on") == 0)
            new_dist = 1;
        else if (strcmp (wa.value[_LS_PATH], "off") == 0)
            new_dist = 0;
        else {
            strcpy (line, "path: on or off");
            return (false);
        }
    }

    // skip if no changes
    if (psk_mask == new_mask && psk_bands == new_bands && psk_maxage_mins == new_age
                        && psk_showdist == new_dist && psk_showpath == new_path) {
        strncpy (line, usage, line_len);
        return (false);
    }

    // ok - engage
    psk_mask = new_mask;
    psk_bands = new_bands;
    psk_maxage_mins = new_age;
    psk_showdist = new_dist;
    psk_showpath = new_path;
    savePSKState();
    scheduleNewPlot(PLOT_CH_PSK);

    // ack
    startPlainText (client);
    client.print ("ok\n");

    // ok
    return (true);
}


/* report current Live Spots list, if active
 */
static bool getWiFiLiveSpots (WiFiClient &client, char *line, size_t line_len)
{
    (void)(line_len);

    const DXSpot *rp;
    int n_rep;
    getPSKSpots (rp, n_rep);
    if (n_rep == 0) {
        strcpy (line, "no live spots");
        return (false);
    }

    // start reply
    startPlainText (client);

    // heading
    char buf[200];
    snprintf (buf, sizeof(buf), "#Age,s txcall    txgrid  rxcall    rxgrid   mode      lat      lng        kHz   snr\n");
    client.print(buf);

    // table
    long long t0 = myNow();
    for (int i = 0; i < n_rep; i++) {
        const DXSpot &r = rp[i];
        snprintf (buf, sizeof(buf), "%5lld  %-10s %-4.4s   %-10s %-4.4s    %-8s %6.2f  %7.2f  %9.3f %5.1f\n",
            t0-r.spotted, r.tx_call, r.tx_grid,  r.rx_call,  r.rx_grid, r.mode, r.tx_ll.lat_d, r.tx_ll.lng_d, 
            r.kHz, r.snr);
        client.print(buf);
    }

    // done
    return (true);
}

/* exit
 */
static bool doWiFiExit (WiFiClient &client, char *unused_line, size_t line_len)
{
    (void)(unused_line);
    (void)(line_len);

    startPlainText(client);

#if defined(NO_HAMCLOCK_CONTROLS)
    client.println ("HamClock exit is not supported on this platform.");
    return (true);
#else
    // ack then die
    client.println ("exiting");

    Serial.print ("Exiting\n");
    doExit();

    // lint
    return (true);
#endif
}



/* table of command strings, its implementing function and additional info for help.
 * functions are called with user input string beginning just after the command and sans HTTP.
 * N.B. set_ functions returning false shall replace the input string with a brief error message.
 *      functions returning true shall send http reply to client.
 *      command[]'s without ? shall include trailing space to detect and prevent trailing garbage.
 *      table is located down here in this file so all handlers are already conveniently defined above.
 *      last N_UNDOC_CMD entries are not shown with help
 */
typedef bool (*PCTF)(WiFiClient &client, char line[], size_t line_len);   // ptr to command table function
#define CT_FUNP(ctp) ((PCTF)ctp->funp)                  // handy function pointer
typedef struct {
    const char *command;                                // command string
    PCTF funp;                                          // handler function
    const char *help;                                   // more info if available
} CmdTble;
static const CmdTble command_table[] = {
    { "antennas.html ",     getWiFiAntennasGui,    "antennas widget to administer antenna information" },	
    { "dashboard.html ",    getWiFiDashboardGui,   "mobile dashboard for HamClock REST data" },
    { "get_antennas.txt ",  getWiFiAntennas,       "Get antenna information from hamclock and backend" },
    { "get_capture.bmp ",   getWiFiCaptureBMP,     "get live screen shot in bmp format" },
    { "get_config.txt ",    getWiFiConfig,         "get current display settings" },
    { "get_contests.txt ",  getWiFiContests,       "get current list of contests" },
    { "get_de.txt ",        getWiFiDEInfo,         "get DE info" },
    { "get_dx.txt ",        getWiFiDXInfo,         "get DX info" },
    { "get_dxpeds.txt ",    getWiFiDXPeds,         "get current list of DXpeditions" },
    { "get_dxspots.txt ",   getWiFiDXSpots,        "get DX spots" },
    { "get_eclipse.txt ",   getWiFiEclipse,        "get next solar eclipse local circumstances" },
    { "get_gpio?",          getWiFiGPIO,           "pin=MCP&latched=[true,false]" }, // params!
    { "get_livespots.txt ", getWiFiLiveSpots,      "get live spots list" },
    { "get_livestats.txt ", getWiFiLiveStats,      "get live spots statistics" },
    { "get_ontheair.txt ",  getWiFiOnTheAir,       "get POTA/SOTA activators" },
    { "get_satellite.txt ", getWiFiSatellite,      "get current sat info" },
    { "get_satellites.txt ",getWiFiAllSatellites,  "get list of all sats" },
    { "get_sensors.txt ",   getWiFiSensorData,     "get sensor data" },
    { "get_spacewx.txt ",   getWiFiSpaceWx,        "get space weather info" },
    { "get_sys.txt ",       getWiFiSys,            "get system stats" },
    { "get_time.txt ",      getWiFiTime,           "get current time" },
    { "get_voacap.txt ",    getWiFiVOACAP,         "get current band conditions matrix" },
    { "list_antennas.txt ", getWiFiAntennalist,    "list antenna indexes and description" },
    { "set_adif?",          setWiFiADIF,           "pane=[0123] (POST)" },
    { "set_alarm?",         setWiFiAlarm,          "state=off|armed&time=HR:MN&utc=yes|no" },
    { "set_antennas?",      setWiFiAntennas,       "antdeindex=n&antdxindex=n&antdedxcontrol=n&antdeaz=n.n&antdxaz=n.n" },	
    { "set_auxtime?",       setWiFiAuxTime,        "format=[one_from_menu]" },
    { "set_bmp?",           setWiFiloadBMP,        "pane=[1,2,3,map]&fit=[resize,crop,fill][&off] (POST)" },
    { "set_cluster?",       setWiFiCluster,        "host=xxx&port=yyy" },
    { "set_debug?",         setWiFiDebug,          "name=xxx&level=n" },
    { "set_defmt?",         setWiFiDEformat,       "fmt=[one_from_menu]&atin=RSAtAt|RSInAgo" },
    { "set_displayOnOff?",  setWiFiDisplayOnOff,   "on|off" },
    { "set_displayTimes?",  setWiFiDisplayTimes,   "on=HR:MN&off=HR:MN&day=[Sun..Sat]&idle=mins" },
    { "set_gpio?",          setWiFiGPIO,           "pin=MCP&level=[hi,lo]&blink=hz" },
    { "set_livespots?",     setWiFiLiveSpots,      "(see error message)" },
    { "set_mapcenter?",     setWiFiMapCenter,      "lng=X" },
    { "set_mapcolor?",      setWiFiMapColor,       "setup=name&color=R,G,B" },
    { "set_mapview?",       setWiFiMapView,        "Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off" },
    { "set_marine_test?",   setWiFiMarineTest,     "id=X&lat=Y&lng=Z&minutes=N&headline=TEXT&office=OFF | clear" },
    { "set_firewx_test?",   setWiFiFireTest,       "id=X&lat=Y&lng=Z&minutes=N&headline=TEXT&office=OFF | clear" },
    { "set_quake_test?",    setWiFiQuakeTest,      "id=X&lat=Y&lng=Z&mag=M&depth=D&place=TEXT | clear" },
    { "set_newde?",         setWiFiNewDE,          "grid=AB12&lat=X&lng=Y&call=AA0XYZ" },
    { "set_newdx?",         setWiFiNewDX,          "grid=AB12&lat=X&lng=Y" },
    { "set_once_alarm?",    setWiFiOnceAlarm,      "state=off|armed&time=YYYY-MM-DDTHR:MN&tz=DE|UTC" },
    { "set_pane?",          setWiFiPane,           "Pane[0123]=X,Y,Z... any from:" },
    { "set_panzoom?",       setWiFiPanZoom,        "pan_x=X&pan_y=Y&pan_dx=dX&pan_dy=dY&zoom=Z" },
    { "set_rotator?",       setWiFiRotator,        "state=[un]stop|[un]auto&az=X&el=X" },
    { "set_rss?",           setWiFiRSS,            "reset|add=X|network|interval=secs|on|off|file (POST)" },
    { "set_satname?",       setWiFiSatName,        "abc|none" },
    { "set_sattle?",        setWiFiSatTLE,         "name=abc&t1=line1&t2=line2" },
    { "set_screenlock?",    setWiFiScreenLock,     "lock=on|off" },
    { "set_senscorr?",      setWiFiSensorCorr,     "sensor=76|77&dTemp=X&dPres=Y" },
    { "set_stopwatch?",     setWiFiStopwatch,      "reset|run|stop|lap|countdown=mins" },
    { "set_time?",          setWiFiTime,           "change=delta_seconds" },
    { "set_time?",          setWiFiTime,           "ISO=YYYY-MM-DDTHH:MM:SS" },
    { "set_time?",          setWiFiTime,           "Now" },
    { "set_time?",          setWiFiTime,           "unix=secs_since_1970" },
    { "set_title?",         setWiFiTitle,          "call|title|onair=[text]&fg=R,G,B&bg=R,G,B|rainbow" },
    { "set_touch?",         setWiFiTouch,          "x=X&y=Y" },
    { "set_voacap?",        setWiFiVOACAP,         "band=X&power=W&tz=DE|UTC&mode=X&map=X&TOA=X" },
    { "exit ",              doWiFiExit,            "exit HamClock" },
    { "postDiags ",         doWiFiPostDiags,       "post diagnostic logs and configuration settings" },
    { "restart ",           doWiFiReboot,          "restart HamClock" },
    { "updateVersion ",     doWiFiUpdate,          "update to latest version"},

    // the following entries are never shown with --help -- update N_UNDOC_CMD if change
    { "set_demo?",          setWiFiDemo,           "on|off|n=N" },
    { "set_spot?",          setWiFiSpot,           "tx_call=x&rx_call=x&kHz=x[&comment=x]" },
};

#define N_CMDTABLE      NARRAY(command_table)           // real n entries in command table
#define N_UNDOC_CMD     2                               // n undocumented commands at end of table

/* return whether the given command is allowed in read-only web service
 */
static bool roCommandOk (const char *cmd)
{
    return (strncmp (cmd, "get_", 4) == 0
                    || strncmp (cmd, "set_daily_alarm", 15) == 0
                    || strncmp (cmd, "set_once_alarm", 14) == 0
                    || strncmp (cmd, "set_stopwatch", 13) == 0
                    || strncmp (cmd, "set_touch", 9) == 0
                    || strncmp (cmd, "set_screenlock", 14) == 0
                    || strncmp (cmd, "restart", 7) == 0
                    || strncmp (cmd, "postDiags", 9) == 0
                    || strncmp (cmd, "exit", 4) == 0);
}

/* run the given web server command.
 * send ack or error messages to client.
 * return strictly whether command was recognized, regardless of whether it returned an error.
 * N.B. caller must close client, we don't.
 */
static bool runWebserverCommand (WiFiClient &client, bool ro, char *command, size_t max_cmd_len)
{
    // search for command depending on context, execute its implementation function if found
    if (!ro || roCommandOk (command)) {
        for (int i = 0; i < N_CMDTABLE; i++) {
            const CmdTble *ctp = &command_table[i];
            int cmd_len = strlen (ctp->command);
            if (strncmp (command, ctp->command, cmd_len) == 0) {

                // found command, skip to params immediately following
                char *params = command+cmd_len;

                // replace any %XX encoded values
                if (replaceEncoding (params))
                    Serial.printf ("Decoded: %s\n", params);      // print decoded version

                // chop off trailing HTTP _after_ looking for commands because get_ commands end with blank.
                char *http = strstr (params, " HTTP");
                if (http)
                    *http = '\0';

                // run handler, passing string starting right after the command, reply with error if trouble.
                PCTF funp = CT_FUNP(ctp);
                if (!(*funp)(client, params, max_cmd_len - cmd_len))
                    sendHTTPError (client, "%.*s error: %s\n", cmd_len, command, params);

                // command found, even if it reported an error
                return (true);
            }
        }
    }

    // not found (or allowed) -- but leave client open
    Serial.printf ("Unknown RESTful command: %s\n", command);
    return (false);
}

/* return whether the given line is a valid POST command
 */
static bool isPOST (const char *line)
{
    return (strncmp (line, "POST /set_rss?", 14) == 0
            || strncmp (line, "POST /set_adif?", 15) == 0
            || strncmp (line, "POST /set_bmp?", 14) == 0);
}

/* return whether the given line is a valid GET command
 */
static bool isGET (const char *line)
{
    return (strncmp (line, "GET /", 5) == 0);
}

/* service remote restful connection.
 * if ro, only accept the get commands and a few more as listed in roCommandOk().
 * N.B. caller must close client, we don't.
 */
static void serveRemote(WiFiClient &client, bool ro)
{
    StackMalloc line_mem(TLE_LINEL*4);          // accommodate longest query, probably set_sattle with %20s
    char *line = (char *) line_mem.getMem();    // handy access to malloced buffer

    // read query
    if (!getTCPLine (client, line, line_mem.getSize(), NULL)) {
        sendHTTPError (client, "empty RESTful query\n");
        return;
    }

    // first line must be the GET except a few can be POST
    if (!isGET(line) && !isPOST(line)) {
        sendHTTPError (client, "Method must be GET or selected POST:\n");
        Serial.println (line);
        return;
    }
    // Serial.printf ("web: %s\n", line);

    // discard remainder of header, but capture content length if available
    content_length = 0;
    char cl_str[20];
    if (httpSkipHeader (client, "Content-Length:", cl_str, sizeof(cl_str)))
        content_length = atol (cl_str);
    else {
        Serial.printf ("bogus header after %s\n", line);
        return;
    }

    // log sender
    Serial.printf ("Command from %s: %s\n", client.remoteIP().toString().c_str(), line);
    if (content_length)
        Serial.printf ("Content-Length: %ld\n", content_length);

    // find beginning just after first -- we aleady know there is a /
    char *cmd_start = strchr (line,'/')+1;

    // run command
    if (runWebserverCommand (client, ro, cmd_start, line + line_mem.getSize() - cmd_start))
        return;

    // if get here, command was not found but client is still open for help
    startPlainText(client);
    if (liveweb_rw_port > 0) {
        snprintf (line, line_mem.getSize(), "HamClock Live is R/W on port %d\r\n", liveweb_rw_port);
        client.print (line);
    }
    if (liveweb_ro_port > 0) {
        snprintf (line, line_mem.getSize(), "HamClock Live is R/O on port %d\r\n", liveweb_ro_port);
        client.print (line);
    }
    for (uint8_t i = 0; i < N_CMDTABLE-N_UNDOC_CMD; i++) {
        const CmdTble *ctp = &command_table[i];

        // skip if not available for ro
        if (ro && !roCommandOk(ctp->command))
            continue;

        // command followed by help in separate column
        const int indent = 22;
        int cmd_len = strlen (ctp->command);
        client.print (ctp->command);
        snprintf (line, line_mem.getSize(), "%*s", indent-cmd_len, "");
        client.print (line);
        client.println (ctp->help);

        // also list pane choices for setWiFiPane
        PCTF funp = CT_FUNP(ctp);
        if (funp == setWiFiPane) {
            const int max_w = 70;
            const char indent[] = "  ";
            int ll = 0;
            for (int i = 0; i < PLOT_CH_N; i++) {
                if (plotChoiceIsAvailable ((PlotChoice)i)) {
                    if (ll == 0)
                        ll = snprintf (line, line_mem.getSize(), "%s", indent);
                    ll += snprintf (line+ll, line_mem.getSize()-ll, " %s", plot_names[i]);
                    if (ll > max_w) {
                        client.println (line);
                        ll = 0;
                    }
                }
            }
            client.println (line);
        }
    }
}

/* check if someone is trying to tell/ask us something.
 * N.B, all such commands bypass the password system.
 */
void checkWebServer(bool ro)
{
    if (restful_server) {
        // not crazy fast
        static uint32_t ws_ms;
        if (timesUp (&ws_ms, 250)) {
            WiFiClient client = restful_server->available();
            if (client) {
                bypass_pw = true;
                serveRemote(client, ro);
                bypass_pw = false;
                client.stop();
            }
        }
    }
}

/* call to start restful server unless disabled.
 * report ok with tftMsg but fatalError if trouble.
 */
void initWebServer()
{
    if (restful_port < 0) {
        tftMsg (true, 0, "RESTful API service is disabled");
        return;
    }


    restful_server = new WiFiServer(restful_port);
    char ynot[100];
    if (!restful_server->begin(ynot))
        fatalError ("Failed to start RESTful server on port %d: %s", restful_port, ynot);

    tftMsg (true, 0, "RESTful API server on port %d", restful_port);

}

/* like readCalTouch() but also checks for remote web server touch.
 */
TouchType readCalTouchWS (SCoord &s)
{
    // check for read-only remote commands
    checkWebServer (true);

    // return info for remote else local touch.
    // N.B. capture cur_touch_live here too, same as checkTouch() -- this is the other place
    // wifi_tt gets consumed (menus, setup wizard, stopwatch), and any of those can still lead to
    // an openURL()/openQRZBio() call down the line that asks isLiveWebTouch().
    TouchType tt;
    if (wifi_tt != TT_NONE) {
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
        cur_touch_live = wifi_tt_live;
        wifi_tt_live = false;
    } else {
        tt = readCalTouch (s);
        cur_touch_live = false;
    }

    // return event type
    return (tt);
}


/* called from main loop() to run another demo command if time.
 * N.B. demo commands bypass the password system,
 */
void runNextDemoCommand()
{
    // out fast if not using this mode
    if (!getDemoMode())
        return;

    // wait for :15 or :45
    static bool prev_slow;
    static time_t prev_t0;
    time_t t0 = nowWO();
    int t060 = t0 % 60;
    if ( t0 == prev_t0 || ! (t060 == 15 || t060 == 45) )
        return;
    prev_t0 = t0;

    // list of probabilities for each DemoChoice, must sum to 100
    static const uint8_t item_probs[DEMO_N] = {

                // 0
        6,      // DEMO_PANE1
        6,      // DEMO_PANE2
        6,      // DEMO_PANE3
        4,      // DEMO_RSS
        9,      // DEMO_NEWDX

                // 5
        3,      // DEMO_MAPPROJ
        3,      // DEMO_MAPNIGHT
        3,      // DEMO_MAPGRID
        2,      // DEMO_MAPSTYLE
        9,      // DEMO_NCDXF
                
                // 10
        5,      // DEMO_CALLFG
        4,      // DEMO_CALLBG
        12,     // DEMO_DEFMT
        4,      // DEMO_ONAIR
        5,      // DEMO_SAT

                // 15
        5,      // DEMO_EME
        6,      // DEMO_AUXTIME
        2,      // DEMO_PSKMASK
        6,      // DEMO_PANE0
    };

    // record previous choice to avoid repeats
    static DemoChoice prev_choice = DEMO_N;             // init to an impossible choice

    // confirm propabilities sum to 100 on first call
    if (prev_choice == DEMO_N) {
        unsigned sum = 0;
        for (int i = 0; i < DEMO_N; i++)
            sum += item_probs[i];
        if (sum != 100)
            fatalError ("demo probs sum %u != 100\n", sum);
    }

    // attempt a change until successful.
    bool ok = false;
    do {
        // make next choice, never repeat
        DemoChoice choice;
        do {
            unsigned p = random(100);
            unsigned sum = 0;
            choice = (DemoChoice)(DEMO_N - 1);          // default is end of prob list
            for (int i = 0; i < DEMO_N-1; i++) {
                if (p < (sum += item_probs[i])) {
                    choice = (DemoChoice) i;
                    break;
                }
            }
        } while (choice == prev_choice);
        prev_choice = choice;

        // run choice, bypassing any passwords
        char msg[200];
        bypass_pw = true;
        ok = runDemoChoice (choice, prev_slow, msg, sizeof(msg));
        bypass_pw = false;
        Serial.println (msg);

    } while (!ok);
}

/* handy helper to consistently format a demo command response into buf[]
 */
static void demoMsg (bool ok, int n, char buf[], size_t buf_len, const char *fmt, ...)
{
    // format the message
    char msg[100];
    va_list ap;
    va_start (ap, fmt);
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end (ap);

    // save with boilerplate to buf
    snprintf (buf, buf_len, "Demo %d %s: %s", n, ok ? "Ok" : "No", msg);
}

/* run the given DemoChoice. 
 * return whether appropriate and successful and, if so, whether it is likely to be slower than usual.
 */
static bool runDemoChoice (DemoChoice choice, bool &slow, char msg[], size_t msg_len)
{
    // assume not
    slow = false;

    // init bad
    bool ok = false;

    switch (choice) {
    case DEMO_PANE0:
        {
            // toggle
            if (SHOWING_PANE_0()) {
                restoreNormPANE0();
                ok = true;
                demoMsg (ok, choice, msg, msg_len, "Pane 0 restored");
            } else {
                // select an appropriate choice for PANE_0
                PlotChoice pc0 = getAnyAvailablePane0Choice();
                if (pc0 != PLOT_CH_NONE)
                    plot_rotset[PANE_0] = PLOTBIT(pc0);   // no auto rotation
                ok = pc0 != PLOT_CH_NONE && setPlotChoice (PANE_0, pc0);
                if (ok) {
                    logPaneRotSet (PANE_0, pc0);
                    demoMsg (ok, choice, msg, msg_len, "Pane 0 now %s", plot_names[pc0]);
                } else
                    demoMsg (ok, choice, msg, msg_len, "Pane 0 no choices");
            }
        }
        break;

    case DEMO_PANE1:
        {
            PlotChoice pc = getAnyAvailableChoice();
            plot_rotset[PANE_1] = PLOTBIT(pc);   // no auto rotation
            ok = setPlotChoice (PANE_1, pc);
            if (ok) {
                logPaneRotSet (PANE_1, pc);
                demoMsg (ok, choice, msg, msg_len, "Pane 1 now %s", plot_names[pc]);
            } else
                demoMsg (ok, choice, msg, msg_len, "Pane 1 no choices");
        }
        break;

    case DEMO_PANE2:
        {
            PlotChoice pc = getAnyAvailableChoice();
            plot_rotset[PANE_2] = PLOTBIT(pc);   // no auto rotation
            ok = setPlotChoice (PANE_2, pc);
            if (ok) {
                logPaneRotSet (PANE_2, pc);
                demoMsg (ok, choice, msg, msg_len, "Pane 2 now %s", plot_names[pc]);
            } else
                demoMsg (ok, choice, msg, msg_len, "Pane 2 no choices");
        }
        break;

    case DEMO_PANE3:
        {
            PlotChoice pc = getAnyAvailableChoice();
            plot_rotset[PANE_3] = PLOTBIT(pc);   // no auto rotation
            ok = setPlotChoice (PANE_3, pc);
            if (ok) {
                logPaneRotSet (PANE_3, pc);
                demoMsg (ok, choice, msg, msg_len, "Pane 3 now %s", plot_names[pc]);
            } else
                demoMsg (ok, choice, msg, msg_len, "Pane 3 no choices");
        }
        break;

    case DEMO_RSS:
        rss_on = !rss_on;
        NVWriteUInt8 (NV_RSS_ON, rss_on);
        initEarthMap();                         // immediately udate
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "RSS %s", rss_on ? "On" : "Off");
        break;

    case DEMO_NEWDX:
        {
            // walk a small collection of cities
            static LatLong demo_ll[] = {
                LatLong (-26.2,  28.0),       // Johannesburg, South Africa
                LatLong (-22.9, -43.2),       // Rio de Janeiro, Brazi
                LatLong ( 35.7, 139.7),       // Tokyo, Japan 
                LatLong (-33.9, 151.2),       // Sydney, Australia
                LatLong ( 40.7, -74.0),       // New York City
                LatLong ( 51.5,  -0.2),       // London
            };
            static int demo_ll_i;
            LatLong &dll = demo_ll[demo_ll_i];
            newDX (dll, NULL, NULL);
            if (++demo_ll_i == NARRAY(demo_ll))
                demo_ll_i = 0;
            ok = true;
            demoMsg (ok, choice, msg, msg_len, "NewDX %g %g", dll.lat_d, dll.lng_d);
        }
        break;

    case DEMO_MAPPROJ:
        map_proj = (map_proj + 1) % MAPP_N;
        NVWriteUInt8 (NV_MAPPROJ, map_proj);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "Proj %s", map_projnames[map_proj]);
        break;

    case DEMO_MAPNIGHT:
        night_on = !night_on;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "Night %s", night_on ? "On" : "Off");
        break;

    case DEMO_MAPGRID:
        mapgrid_choice = (mapgrid_choice + 1) % MAPGRID_N;
        NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        initEarthMap();
        if (map_proj != MAPP_MERCATOR)
            slow = true;
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "Grid %s", grid_styles[mapgrid_choice]);
        break;

    case DEMO_MAPSTYLE:
        {
            char s[NV_COREMAPSTYLE_LEN];
            CoreMaps cm = (CoreMaps)(random(CM_N));                             // random style
            ok = true;
            if (cm == CM_PMTOA || cm == CM_PMREL)
                cm_info[cm].band = (PropMapBand)random(PROPBAND_N);             // random prop band
            else if (cm == CM_USER && !allWebMapImagesOk())
                ok = false;
            if (ok)
                scheduleNewCoreMap (cm);
            demoMsg (ok, choice, msg, msg_len, "Map %s", getCoreMapStyle (cm, s));
            slow = true;
        }
        break;

    case DEMO_NCDXF:
        // cycle only states always available
        switch (brb_mode) {
        case BRB_SHOW_SWSTATS: brb_mode = BRB_SHOW_DXWX; break;
        case BRB_SHOW_DXWX:    brb_mode = BRB_SHOW_DEWX; break;
        case BRB_SHOW_DEWX:    brb_mode = BRB_SHOW_BEACONS; break;
        default:               brb_mode = BRB_SHOW_SWSTATS; break;
        }
        (void) drawNCDXFBox();
        updateBeacons(true);
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "NCDXF %s", brb_mode == BRB_SHOW_BEACONS ? "On" : "Off");
        break;

    case DEMO_CALLFG:
        {
            // fake a touch to rotate the text color
            SCoord s;
            s.x = cs_info.box.x + cs_info.box.w/4;
            s.y = cs_info.box.y + cs_info.box.h/2;
            (void) doCallsignTouch (s);
            ok = true;
            demoMsg (ok, choice, msg, msg_len, "call FG");
        }
        break;

    case DEMO_CALLBG:
        {
            // fake a touch to rotate the bg color
            SCoord s;
            s.x = cs_info.box.x + 3*cs_info.box.w/4;
            s.y = cs_info.box.y + cs_info.box.h/2;
            (void) doCallsignTouch (s);
            ok = true;
            demoMsg (ok, choice, msg, msg_len, "call BG");
        }
        break;

    case DEMO_DEFMT:
        de_time_fmt = (de_time_fmt + 1) % DETIME_N;
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
        drawDEInfo();
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "DE fmt %s", detime_names[de_time_fmt]);
        break;

    case DEMO_ONAIR:
        {
            static bool toggle;
            setOnAirSW (toggle = !toggle);
            ok = true;
            demoMsg (ok, choice, msg, msg_len, "ONAIR %s", toggle ? "On" : "Off");
        }
        break;

    case DEMO_SAT:
        {
            if (isSatDefined()) {
                // sat is assigned, turn off
                ok = setSatFromName("none");
                demoMsg (ok, choice, msg, msg_len, "Sat none");
            } else {
                // assign new sat by cycling through list
                static const char *sats[] = {
                    "ISS", "SO-50", "FOX-1B"
                };
                static int next_sat;
                next_sat = (next_sat + 1) % NARRAY(sats);
                ok = setSatFromName(sats[next_sat]);
                demoMsg (ok, choice, msg, msg_len, "Sat %s", sats[next_sat]);
            }
            slow = true;
        }
        break;

    case DEMO_EME:
        ok = false;             // no longer resumes by itself
        demoMsg (ok, choice, msg, msg_len, "EME");
        break;

    case DEMO_AUXTIME:
        auxtime = (AuxTimeFormat)random(AUXT_N);
        updateClocks(true);
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "Aux time %s", auxtime_names[(int)auxtime]);
        break;

    case DEMO_PSKMASK:
        psk_mask = 0;
        if (random(100) > 50)
            psk_mask |= PSKMB_OFDE;
        if (random(100) > 50)
            psk_mask |= PSKMB_CALL;
        if (random(100) > 50)
            psk_mask |= PSKMB_PSK;
        ok = true;
        demoMsg (ok, choice, msg, msg_len, "psk_mask 0x%X", psk_mask);
        break;

    case DEMO_N:
        break;
    }

    return (ok);
}
