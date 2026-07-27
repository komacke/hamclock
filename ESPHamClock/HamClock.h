/* HamClock glue
 */



#ifndef _HAMCLOCK_H
#define _HAMCLOCK_H


// POSIX modules
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <dirent.h>
#include <sys/file.h>


#include "ArduinoLib.h"


// N.B. keep showDefines() up to date


// whether we have native IO
#if defined(_NATIVE_GPIO_FREEBSD) || defined(_NATIVE_GPIO_LINUX)
  #define _SUPPORT_NATIVE_GPIO
#endif

// kx3 on any system with NATIVE_GPIO
#if defined(_SUPPORT_NATIVE_GPIO)
  #define _SUPPORT_KX3
#endif

// whether to even look for DSI touchscreen
#if !defined(_WEB_ONLY) && (defined(_IS_LINUX_RPI) || defined(_USE_FB0))
    #define _SUPPORT_DSI
#endif


// full res app, map, moon and running man sizes
// N.B. RUNNER sizes must match runner.pl
#if defined(_CLOCK_1600x960)

#define HC_MAP_W (660*2)
#define HC_MAP_H (330*2)
#define HC_MOON_W (148*2)
#define HC_MOON_H (148*2)
#define HC_RUNNER_W (11*2)
#define HC_RUNNER_H (13*2)
#define BUILD_W 1600
#define BUILD_H 960

#elif defined(_CLOCK_2400x1440)

#define HC_MAP_W (660*3)
#define HC_MAP_H (330*3)
#define HC_MOON_W (148*3)
#define HC_MOON_H (148*3)
#define HC_RUNNER_W (11*3)
#define HC_RUNNER_H (13*3)
#define BUILD_W 2400
#define BUILD_H 1440

#elif defined(_CLOCK_3200x1920)

#define HC_MAP_W (660*4)
#define HC_MAP_H (330*4)
#define HC_MOON_W (148*4)
#define HC_MOON_H (148*4)
#define HC_RUNNER_W (11*4)
#define HC_RUNNER_H (13*4)
#define BUILD_W 3200
#define BUILD_H 1920

#else   // original size

#define HC_MAP_W 660
#define HC_MAP_H 330
#define HC_MOON_W 148
#define HC_MOON_H 148
#define HC_RUNNER_W 11
#define HC_RUNNER_H 13
#define BUILD_W 800
#define BUILD_H 480

#endif

// canonical map size 
#define EARTH_H   330
#define EARTH_W   660


// see Adafruit_RA8875.h
#define USE_ADAFRUIT_GFX_FONTS

// community modules
#include <Arduino.h>
#include <TimeLib.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <WiFiUdp.h>

// screen coordinates, upper left at [0,0]
typedef struct {
    uint16_t x, y;
} SCoord;
#include "Adafruit_RA8875.h"
#include "Adafruit_MCP23X17.h"

// HamClock modules
#include "P13.h"




// handy nelements in array
// N.B. call with real array, not a pointer
#define NARRAY(a)       ((int)(sizeof(a)/sizeof(a[0])))

// handy range clamp
#define CLAMPF(v,minv,maxv)      fmaxf(fminf((v),(maxv)),(minv))

// handy microseconds difference in two struct timeval: t1 - t0
#define TVDELUS(t0,t1)    ((t1.tv_sec-t0.tv_sec)*1000000 + (t1.tv_usec-t0.tv_usec))

// float versions
#define M_PIF   3.14159265F
#define M_PI_2F (M_PIF/2)

#define deg2rad(d)      ((M_PIF/180)*(d))
#define rad2deg(d)      ((180/M_PIF)*(d))


// time to leave new DX path up, millis()
#define DXPATH_LINGER   20000   

// path segment length, degrees
#define PATH_SEGLEN     (1.0F/pan_zoom.zoom)

// default menu timeout, millis
#define MENU_TO         30000

// maidenhead character arrey length, including EOS
#define MAID_CHARLEN     7

#define NV_ROTHOST_LEN          18
#define NV_RIGHOST_LEN          18
#define NV_FLRIGHOST_LEN        18
#define NV_COREMAPSTYLE_LEN     10


/* allows reading from array, WiFiClient or FILE
 */
extern bool getTCPChar (WiFiClient &client, char *cp);
class GenReader 
{
    public:
        
        // instantiate to read from a WiFiClient client, with optional content_length
        // N.B. caller is expected to stop client
        GenReader (WiFiClient &client, long content_length = 0) {
            my_type = GR_CLIENT;
            my_client = &client;
            my_clen = content_length;   // ignored if 0
        }

        // instantiate to read from a FILE *p
        // N.B. caller is expected to fclose
        GenReader (FILE *fp) {
            my_type = GR_FILE;
            my_fp = fp;
        }

        // instantiate to read from a memory array
        GenReader (const char *a, int n_a)
        {
            my_type = GR_ARRAY;
            my_array = a;
            my_array_end = a + n_a;
        }

        // return next byte from the source
        bool getChar (char *bp) {
            switch (my_type) {
            case GR_ARRAY:
                if (my_array < my_array_end) {
                    *bp = *my_array++;
                    return (true);
                }
                return(false);
                break;
            case GR_FILE: {
                int i = fgetc(my_fp);
                if (i == EOF || feof(my_fp) || ferror(my_fp))
                    return (false);
                *bp = (char)i;
                return (true);
                }
                break;
            case GR_CLIENT:
                return ((!my_clen || my_clen-- > 0) && getTCPChar (*my_client, bp));
                break;
            default:
                return (false);
            }
        }

        // type tests
        bool isFile(void) { return (my_type == GR_FILE); }
        bool isClient(void) { return (my_type == GR_CLIENT); }
        bool isArray(void) { return (my_type == GR_ARRAY); }

    private:

        typedef enum {
            GR_ARRAY,
            GR_FILE,
            GR_CLIENT
        } GRType;

        GRType my_type;
        FILE *my_fp;
        WiFiClient *my_client;  // pointer to avoid having to init the reference everywhere with a dummy
        long my_clen;
        const char *my_array;
        const char *my_array_end;
};


/* handy malloc wrapper that frees automatically when leaves scope
 */
class StackMalloc 
{
    public:

        StackMalloc (size_t nbytes) {
            // printf ("SM: new %lu\n", nbytes);
            mem = (char *) malloc (nbytes);
            siz = nbytes;
        }

        StackMalloc (const char *string) {
            // printf ("SM: new %s\n", string);
            mem = (char *) strdup (string);
            siz = strlen(string) + 1;
        }

        ~StackMalloc (void) {
            // printf ("SM: free(%d)\n", siz);
            free (mem);
        }

        size_t getSize(void) {
            return (siz);
        }

        void *getMem(void) {
            return (mem);
        }

    private:

        char *mem;
        size_t siz;
};


/* handy class for capturing brief messages, typically failures
 * N.B. any trailing newlines are removed.
 */
class Message
{
    public:

        Message(void) {
            msg[0] = '\0';
            msg_len = 0;
        }

        void printf (const char *fmt, ...) {
            va_list ap;
            va_start(ap, fmt);
            msg_len = vsnprintf (msg, sizeof(msg), fmt, ap);
            va_end(ap);
            noNL();
        }

        const char *get(void) {
            return (msg);
        }

        void set (const char *str) {
            msg_len = snprintf (msg, sizeof(msg), "%s", str);
            noNL();
        }

    private:

        char msg[100];
        size_t msg_len;

        void noNL() {
            if (msg_len > 0 && msg[msg_len-1] == '\n')
                msg[--msg_len] = '\0';
        }
};


// handy unit conversions
#define FAH2CEN(f)      ((5.0F/9.0F)*((f) - 32.0F))
#define CEN2FAH(c)      ((9.0F/5.0F)*(c) + 32.0F)
#define HPA2INHG(h)     ((h)/33.8639F)
#define INHG2HPA(i)     ((i)*33.8639F)

/* time styles in auxtime_b
 */
#define AUXTIMES                        \
    X(AUXT_DATE,        "Date")         \
    X(AUXT_DOY,         "Day of Year")  \
    X(AUXT_JD,          "Julian Date")  \
    X(AUXT_MJD,         "Modified JD")  \
    X(AUXT_SIDEREAL,    "Sidereal")     \
    X(AUXT_SOLAR,       "Solar")        \
    X(AUXT_UNIX,        "UNIX seconds")

#define X(a,b) a,               // expands AUXTIME to each enum and comma
typedef enum {
    AUXTIMES
    AUXT_N
} AuxTimeFormat;
#undef X

extern AuxTimeFormat auxtime;
extern const char *auxtime_names[AUXT_N];


/* names of each non-volatil entry.
 * N.B. the entries here must match those in nv_sizes[]
 */
typedef enum {
    // 0
    NV_TOUCH_CAL_A,             // touch calibration coefficient
    NV_TOUCH_CAL_B,             // touch calibration coefficient
    NV_TOUCH_CAL_C,             // touch calibration coefficient
    NV_TOUCH_CAL_D,             // touch calibration coefficient
    NV_TOUCH_CAL_E,             // touch calibration coefficient

    // 5
    NV_TOUCH_CAL_F,             // touch calibration coefficient
    NV_TOUCH_CAL_DIV,           // touch calibration normalization
    NV_DXMAX_N,                 // n lost dx connections since NV_DXMAX_T
    NV_DE_TIMEFMT,              // DE: 0=info; 1=analog; 2=cal; 3=analog+day; 4=dig 12hr; 5=dig 24hr
    NV_DE_LAT,                  // DE latitude, degrees N

    // 10
    NV_DE_LNG,                  // DE longitude, degrees E
    NV_PANE0ROTSET,             // PlotChoice bitmask of pane 0 rotation choices
    NV_PLOT_0,                  // Pane 0 PlotChoice
    NV_DX_LAT,                  // DX latitude, degrees N
    NV_DX_LNG,                  // DX longitude, degrees E

    // 15
    NV_DX_GRID_OLD,             // deprecated
    NV_CALL_FG,                 // Call foreground color as RGB 565
    NV_CALL_BG,                 // Call background color as RGB 565 unless...
    NV_CALL_RAINBOW,            // set if Call background is to be rainbow
    NV_PSK_SHOWDIST,            // Live spots shows max distance, else counts

    // 20
    NV_UTC_OFFSET,              // offset from UTC, seconds
    NV_PLOT_1,                  // Pane 1 PlotChoice
    NV_PLOT_2,                  // Pane 2 PlotChoice
    NV_BRB_ROTSET_OLD,          // deprecated after it became too small
    NV_PLOT_3,                  // Pane 3 PlotChoice

    // 25
    NV_RSS_ON,                  // whether to display RSS
    NV_BPWM_DIM,                // dim PWM, 0..255
    NV_PHOT_DIM,                // photo r dim value, 0 .. 1023
    NV_BPWM_BRIGHT,             // bright PWM, 0..255
    NV_PHOT_BRIGHT,             // photo r bright value, 0 .. 1023

    // 30
    NV_LP,                      // whether to show DE-DX long or short path info
    NV_UNITS,                   // 0: imperial, 1: metric 2: british
    NV_LKSCRN_ON,               // whether screen lock is on
    NV_MAPPROJ,                 // 0: merc 1: azim 2: azim 1
    NV_ROTATE_SCRN_OLD,         // deprecated after removing ESP

    // 35
    NV_WIFI_SSID,               // WIFI SSID
    NV_WIFI_PASSWD_OLD,         // deprecated
    NV_CALLSIGN,                // call 
    NV_SAT1NAME,                // satellite 1 name with underscore for each space
    NV_DE_SRSS,                 // whether DE pane shows sun times 0=until or 1=at

    // 40
    NV_DX_SRSS,                 // whether DX pane shows sun times 0=until or 1=at or 2=DX prefix
    NV_GRIDSTYLE,               // map grid style 0=off; 1=tropics; 2=lat-lng; 3=maindenhead, 4=radial
    NV_DPYON_OLD,               // deprecated since NV_DAILYONOFF
    NV_DPYOFF_OLD,              // deprecated since NV_DAILYONOFF
    NV_DXHOST,                  // DX cluster host name, unless using WSJT

    // 45
    NV_DXPORT,                  // DX cluster port number
    NV_SWHUE,                   // stopwatch color RGB 565
    NV_TEMPCORR76,              // BME280 76 temperature correction, NV_UNITS units
    NV_GPSDHOST_OLD,            // deprecated in 4.07
    NV_KX3BAUD,                 // KX3 baud rate or 0

    // 50
    NV_BCPOWER,                 // VOACAP power, watts
    NV_CD_PERIOD,               // stopwatch count down period, seconds
    NV_PRESCORR76,              // BME280 76 pressure correction, NV_UNITS units
    NV_BR_IDLE,                 // idle period, minutes
    NV_BR_MIN,                  // minimum brightness, percent of display range

    // 55
    NV_BR_MAX,                  // maximum brightness, percent of display range
    NV_DE_TZ,                   // DE offset from UTC, seconds, or NVTZ_AUTO
    NV_DX_TZ,                   // DX offset from UTC, seconds, or NVTZ_AUTO
    NV_COREMAPSTYLE,            // name of core map background images (not voacap propmaps)
    NV_USEDXCLUSTER,            // whether to attempt using a DX cluster

    // 60
    NV_USEGPSD,                 // bit 1: use gpsd for time, bit 2: use for location
    NV_LOGUSAGE,                // whether to phone home with clock settings
    NV_LBLSTYLE,                // DX spot annotations: 0=none; 1=just prefix; 2=full call;
    NV_WIFI_PASSWD,             // WIFI password
    NV_NTPSET,                  // whether to use NV_NTPHOST

    // 65
    NV_NTPHOST_OLD,             // deprecated in 4.07
    NV_GPIOOK,                  // whether ok to use GPIO pins
    NV_SAT1COLOR,               // satellite 1 color as RGB 565
    NV_SAT2COLOR,               // satellite 2 color as RGB 565
    NV_X11FLAGS,                // set if want full screen

    // 70
    NV_BCFLAGS,                 // Big Clock bitmask: 1=date;2=wx;4=dig;8=12hr;16=nosec;32=UTC;64=an+dig;128=hrs;256=SpWx;512=hands;1024=sat
    NV_DAILYONOFF,              // 7 2-byte on times then 7 off times, each mins from midnight
    NV_TEMPCORR77,              // BME280 77 temperature correction, NV_UNITS units
    NV_PRESCORR77,              // BME280 77 pressure correction, NV_UNITS units
    NV_SHORTPATHCOLOR,          // prop short path color as RGB 565

    // 75
    NV_LONGPATHCOLOR,           // prop long path color as RGB 565
    NV_PLOTOPS_OLD,             // deprecated since NV_PANE_CH
    NV_NIGHT_ON,                // whether to show night on map
    NV_DE_GRID,                 // DE 6 char grid
    NV_DX_GRID,                 // DX 6 char grid

    // 80
    NV_GRIDCOLOR,               // map grid color as RGB 565
    NV_CENTERLNG,               // mercator center longitude
    NV_NAMES_ON,                // whether to show roving place names
    NV_PANE1ROTSET,             // PlotChoice bitmask of pane 1 rotation choices
    NV_PANE2ROTSET,             // PlotChoice bitmask of pane 2 rotation choices

    // 85
    NV_PANE3ROTSET,             // PlotChoice bitmask of pane 3 rotation choices
    NV_AUX_TIME,                // 0=date, DOY, JD, MJD, LST, UNIX
    NV_DAILYALARM,              // daily alarm time 60*hr + min, + 60*24 if armed; always DE TZ
    NV_BC_UTCTIMELINE,          // band conditions timeline labeled in UTC else DE
    NV_RSS_INTERVAL,            // RSS update interval, seconds

    // 90
    NV_DATEMDY,                 // 0 = MDY 1 = see NV_DATEDMYYMD
    NV_DATEDMYYMD,              // 0 = DMY 1 = YMD
    NV_ROTUSE,                  // whether to use rotctld
    NV_ROTHOST,                 // rotctld tcp host
    NV_ROTPORT,                 // rotctld tcp port

    // 95
    NV_RIGUSE,                  // whether to use rigctld
    NV_RIGHOST,                 // rigctld tcp host
    NV_RIGPORT,                 // rigctld tcp port
    NV_DXLOGIN,                 // DX cluster login
    NV_FLRIGUSE,                // whether to use flrig

    // 100
    NV_FLRIGHOST,               // flrig tcp host
    NV_FLRIGPORT,               // flrig tcp port
    NV_DXCMD0_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD1_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD2_OLD,              // deprecated when lengthened in 4.08

    // 105
    NV_DXCMD3_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMDUSED_OLD,           // deprecated as of V3.06
    NV_PSK_MODEBITS,            // live spots mode: bit 0: on=psk off=wspr bit 1: on=bycall off=bygrid
    NV_PSK_BANDS,               // live spots bands: bit mask 0 .. 11 160 .. 2m
    NV_160M_COLOR,              // 160 m path color as RGB 565

    // 110
    NV_80M_COLOR,               // 80 m path color as RGB 565
    NV_60M_COLOR,               // 60 m path color as RGB 565
    NV_40M_COLOR,               // 40 m path color as RGB 565
    NV_30M_COLOR,               // 30 m path color as RGB 565
    NV_20M_COLOR,               // 20 m path color as RGB 565

    // 115
    NV_17M_COLOR,               // 17 m path color as RGB 565
    NV_15M_COLOR,               // 15 m path color as RGB 565
    NV_12M_COLOR,               // 12 m path color as RGB 565
    NV_10M_COLOR,               // 10 m path color as RGB 565
    NV_6M_COLOR,                // 6 m path color as RGB 565

    // 120
    NV_2M_COLOR,                // 2 m path color as RGB 565
    NV_CSELDASHED,              // current ColorSelection bitmask set for dashed
    NV_BEAR_MAG,                // show magnetic bearings, else true
    NV_WSJT_SETSDX_OLD,         // deprecated
    NV_WSJT_DX,                 // whether dx cluster is WSJT-X

    // 125
    NV_PSK_MAXAGE,              // live spots max age, minutes
    NV_WEEKMON,                 // whether week starts on Monday
    NV_BCMODE,                  // CW=19 SSB=38 AM=49 WSPR=3 FT8=13 FT4=17
    NV_SDO,                     // sdo pane choice 0..6
    NV_SDOROT,                  // whether SDO pane is rotating

    // 130
    NV_ONTASPOTA_OLD,           // POTA sort, deprecated in 4.09
    NV_ONTASSOTA_OLD,           // SOTA sort, deprecated at 4.09
    NV_BRB_ROTSET,              // Beacon box mode bit mask
    NV_ROTCOLOR,                // rotator map color
    NV_CONTESTS,                // bit 1 to show date, bit 2 use DE timezone

    // 135
    NV_BCTOA,                   // VOACAP take off angle, degs
    NV_ADIFFN_OLD,              // deprecated when lengthened in v4.06
    NV_I2CFN,                   // I2C device filename
    NV_I2CON,                   // whether to use I2C
    NV_DXMAX_T,                 // time when n lost dx connections exceeded max

    // 140
    NV_POTAWLIST1_OLD,          // deprecated when lengthened in v4.06
    NV_SCROLLDIR,               // 0=bottom 1=top
    NV_SCROLLLEN_OLD,           // deprecated in V4.04
    NV_DXCMD4_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD5_OLD,              // deprecated when lengthened in 4.08

    // 145
    NV_DXCMD6_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD7_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD8_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD9_OLD,              // deprecated when lengthened in 4.08
    NV_DXCMD10_OLD,             // deprecated when lengthened in 4.08

    // 150
    NV_DXCMD11_OLD,             // deprecated when lengthened in 4.08
    NV_DXCMDMASK,               // bitmask of dx cluster commands in use
    NV_DXWLISTMASK,             // 0: off, 1: not, 2: on, 3: only
    NV_RANKSW_OLD,              // deprecated as of 4.07
    NV_NEWDXDEWX,               // whether to show new DX or DE weather

    // 155
    NV_WEBFS,                   // whether to enable full screen web interface
    NV_ZOOM,                    // integral zoom factor
    NV_PANX,                    // center x from 0 center, + right, @ zoom 1
    NV_PANY,                    // center y from 0 center, + up, @ zoom 1
    NV_POTAWLISTMASK_OLD,       // deprecated 4.09

    // 160
    NV_SOTAWLIST1_OLD,          // deprecated when lengthened in v4.06
    NV_ONCEALARM,               // one-time alarm time(). always in UTC
    NV_ONCEALARMMASK,           // bit 1 = armed, 2 = user wants UTC (else DE TZ)
    NV_PANEROTP,                // pane rotation period, seconds
    NV_SHOWPIP,                 // whether to show public IP

    // 165
    NV_MAPROTP,                 // map rotation period, seconds
    NV_MAPROTSET,               // core_map rotation bit mask
    NV_GRAYDPY,                 // whether to use gray scale
    NV_SOTAWLISTMASK_OLD,       // deprecated 4.09
    NV_ADIFWLISTMASK,           // 0: off, 1: not, 2: on, 3: only

    // 170
    NV_DXWLIST,                 // DX watch list
    NV_ADIFWLIST,               // ADIF watch list
    NV_ADIFSORT,                // 0 age 1 distance
    NV_ADIFBANDS_OLD,           // deprecated in V4.04 -- replaced by watch list 
    NV_POTAWLIST_OLD,           // deprecated 4.09

    // 175
    NV_SOTAWLIST_OLD,           // deprecated V4.09
    NV_ADIFFN,                  // ADIF file name, if any
    NV_NTPHOST,                 // user defined NTP host name
    NV_GPSDHOST,                // gpsd daemon host name
    NV_NMEAFILE,                // NMEA serial file name

    // 180
    NV_USENMEA,                 // bit 1: use NMEA for time, bit 2: use for location
    NV_NMEABAUD,                // NMEA connection baud rate
    NV_BCTOABAND,               // band conditions TOA map band code
    NV_BCRELBAND,               // band conditions REL map band code
    NV_AUTOMAP,                 // whether to turn on maps automatically

    // 185
    NV_DXCAGE,                  // oldest dx cluster entry, minutes
    NV_ONAIR_FG,                // ON AIR text foreground color as RGB 565
    NV_ONAIR_BG,                // ON AIR text background color as RGB 565 unless...
    NV_ONAIR_RAINBOW,           // set if ON AIR background is to be rainbow
    NV_DXCMD0,                  // dx cluster command 0

    // 190
    NV_DXCMD1,                  // dx cluster command 1
    NV_DXCMD2,                  // dx cluster command 2
    NV_DXCMD3,                  // dx cluster command 3
    NV_DXCMD4,                  // dx cluster command 4
    NV_DXCMD5,                  // dx cluster command 5

    // 195
    NV_DXCMD6,                  // dx cluster command 6
    NV_DXCMD7,                  // dx cluster command 7
    NV_DXCMD8,                  // dx cluster command 8
    NV_DXCMD9,                  // dx cluster command 9
    NV_DXCMD10,                 // dx cluster command 10

    // 200
    NV_DXCMD11,                 // dx cluster command 11
    NV_ONAIR_MSG,               // ON AIR text
    NV_SETRADIO,                // whether to issue radio commands
    NV_ONTAWLIST,               // ONTA watch list
    NV_ONTAWLISTMASK,           // 0: off, 1: not, 2: on, 3: only

    // 205
    NV_ONTASORTBY,              // ONTA sort 0-3: Band Call Org Age
    NV_ONTAORG,                 // ONTA organization filter
    NV_TITLE,                   // alternate callsign title text
    NV_TITLE_FG,                // alternate callsign fg as RGB 565
    NV_TITLE_BG,                // alternate callsign bg as RGB 565

    // 210
    NV_TITLE_RAINBOW,           // alternate callsign is rainbow
    NV_ONTA_MAXAGE,             // max ONTA spot age, mins
    NV_QRZID,                   // which bio source
    NV_CALLPREF,                // prefer call title or both
    NV_CSELONOFF,               // current ColorSelection bitmask set for on/off

    // 215
    NV_CSELTHIN,                // current ColorSelection bitmask set for thin
    NV_CSELDASHED_A,            // set A ColorSelection bitmask set for on/off
    NV_CSELONOFF_A,             // set A ColorSelection bitmask set for on/off
    NV_CSELTHIN_A,              // set A ColorSelection bitmask set for thin
    NV_CSELDASHED_B,            // set B ColorSelection bitmask set for on/off

    // 220
    NV_CSELONOFF_B,             // set B ColorSelection bitmask set for on/off
    NV_CSELTHIN_B,              // set B ColorSelection bitmask set for thin
    NV_ONTABIO,                 // whether clicking an ONTA spot shows biography
    NV_DXCBIO,                  // whether clicking a DXC spot shows biography
    NV_X11GEOM_X,               // app window x coord

    // 225
    NV_X11GEOM_Y,               // app window y coord
    NV_X11GEOM_W,               // app window width
    NV_X11GEOM_H,               // app window height
    NV_DEWXCHOICE,              // bit mask of NCDXF box DE wx stats
    NV_DXWXCHOICE,              // bit mask of NCDXF box DX wx stats

    // 230
    NV_UDPSETSDX,               // whether a new UDP packet sets DX
    NV_SPCWXCHOICE,             // bit mask of NCDXF box space wx stats
    NV_DXPEDS,                  // bit mask of menu options
    NV_UDPSPOTS,                // whether to allow UDP with rx_call other than our call
    NV_AUTOUPGRADE,             // local hour to upgrade, else -1

    // 235
    NV_MAXTLEAGE,               // max allowed tle age, days
    NV_MINLBLDIST,              // minimum labeling distance
    NV_SAT2NAME,                // satellite 2 name with underscore for each space
    NV_PSK_SHOWPATH,            // whether Live Spots show paths
    NV_SAT1FLAGS,               // satellite 1 options

    // 240
    NV_SAT2FLAGS,               // satellite 2 options

    NV_N

} NV_Name;

#define NV_NONE NV_N            // handy alias

// N.B. must match setup.cpp::csel_pr[] order 
typedef enum {
    SHORTPATH_CSPR,
    LONGPATH_CSPR,
    SAT1_CSPR,
    SAT2_CSPR,
    GRID_CSPR,
    ROTATOR_CSPR,
    // N.B. see loadPSKColorTable()
    BAND160_CSPR,
    BAND80_CSPR,
    BAND60_CSPR,
    BAND40_CSPR,
    BAND30_CSPR,
    BAND20_CSPR,
    BAND17_CSPR,
    BAND15_CSPR,
    BAND12_CSPR,
    BAND10_CSPR,
    BAND6_CSPR,
    BAND2_CSPR,
    N_CSPR
} ColorSelection;


/* plot choices and pane locations
 */

// N.B. take care that names will fit in menu built by askPaneChoice()
// N.B. names should not include blanks, but _ are changed to blanks for prettier printing
#define PLOTNAMES \
    X(PLOT_CH_BC,           "VOACAP_DEDX")      \
    X(PLOT_CH_DEWX,         "DE_Wx")            \
    X(PLOT_CH_DXCLUSTER,    "DX_Cluster")       \
    X(PLOT_CH_DXWX,         "DX_Wx")            \
    X(PLOT_CH_FLUX,         "Solar_Flux")       \
    X(PLOT_CH_KP,           "Planetary_K")      \
    X(PLOT_CH_MOON,         "Moon")             \
    X(PLOT_CH_NOAASPW,      "NOAA_SpcWx")       \
    X(PLOT_CH_SSN,          "Sunspot_N")        \
    X(PLOT_CH_XRAY,         "X-Ray")            \
    X(PLOT_CH_GIMBAL,       "Rotator")          \
    X(PLOT_CH_TEMPERATURE,  "ENV_Temp")         \
    X(PLOT_CH_PRESSURE,     "ENV_Press")        \
    X(PLOT_CH_HUMIDITY,     "ENV_Humid")        \
    X(PLOT_CH_DEWPOINT,     "ENV_DewPt")        \
    X(PLOT_CH_SDO,          "SDO")              \
    X(PLOT_CH_SOLWIND,      "Solar_Wind")       \
    X(PLOT_CH_DRAP,         "DRAP")             \
    X(PLOT_CH_COUNTDOWN,    "Countdown")        \
    X(PLOT_CH_CONTESTS,     "Contests")         \
    X(PLOT_CH_PSK,          "Live_Spots")       \
    X(PLOT_CH_BZBT,         "Bz_Bt")            \
    X(PLOT_CH_ONTA,         "On_The_Air")       \
    X(PLOT_CH_ADIF,         "ADIF")             \
    X(PLOT_CH_AURORA,       "Aurora")           \
    X(PLOT_CH_DXPEDS,       "DXPeditions")      \
    X(PLOT_CH_DST,          "Disturbance")

#define X(a,b)  a,              // expands PLOTNAMES to each enum and comma
typedef enum {
    PLOTNAMES
    PLOT_CH_N
} PlotChoice;
#undef X

// reuse count also handy flag for not found
#define PLOT_CH_NONE    PLOT_CH_N

typedef enum {
    PANE_0,                             // DE/DX overlay
    PANE_1,                             // top three
    PANE_2,
    PANE_3,
    PANE_N
} PlotPane;

// reuse count also handy flag for not found
#define PANE_NONE       PANE_N




// screen coords of box ul and size
typedef struct {
    uint16_t x, y, w, h;
} SBox;

// screen center, radius
typedef struct {
    SCoord s;
    uint16_t r;
} SCircle;

/* capture lat, lng with some handy related tools.
 *
 * only thing I don't like about this is that the need to set the unit vector prevents using const LatLong
 * when the intent is to promise not to change location.
 */
class LatLong
{
    public:

        LatLong(void) {
            lat_set = lng_set = -9.75e10;               // any unlikely value
            lat = lat_d = lng = lng_d = 0;              // just to be nice
        }

        LatLong (float latitude_d, float longitude_d) : LatLong() {
            lat_d = latitude_d;
            lng_d = longitude_d;
            normalize();
        }

        /* direct access -- too late for setters/getters -- that's why we need lat/lng_set
         */
        float lat, lat_d;                               // rads and degrees N
        float lng, lng_d;                               // rads and degrees E

        /* great circle distance between us and ll in rads; mult by ERAD_M to get miles
         */
        float GSD (LatLong &ll) {
            setXYZ();
            ll.setXYZ();
            float dx = x - ll.x;
            float dy = y - ll.y;
            float dz = z - ll.z;
            float chord = sqrtf (dx*dx + dy*dy + dz*dz);
            return (2 * asinf (chord/2));               // convert chord to great circle
        }

        /* given the _d degree members:
         *   clamp lat to [-90,90];
         *   modulo lng to [-180,180).
         * then fill in the radian members and set up the unit vector too.
         */
        void normalize (void) {
            lat_d = fmaxf(fminf(lat_d,90),-90);         // clamp lat
            lat = deg2rad(lat_d);
            lng_d = fmodf(lng_d+(2*360+180),360)-180;   // wrap lng
            lng = deg2rad(lng_d);
            setXYZ();
        }


    private:

        // insure vector on unit sphere at lat/lng is ready
        void setXYZ(void) {
            if (lat_set != lat || lng_set != lng) {
                float clat = cosf (lat);
                x = clat * cosf (lng);
                y = clat * sinf (lng);
                z = sinf (lat);
                lat_set = lat;
                lng_set = lng;
            }
        }

        float lat_set, lng_set;                 // xyz valid iff these match the public values
        float x, y, z;                          // location on unit sphere
};



// timezone info
typedef struct {
    SBox box;                           // where to display
    uint16_t color;                     // display text color
    const LatLong &ll;                  // handy location, usually de_ll or dx_ll
    bool auto_tz;                       // whether automatic else user-set
    int tz_secs;                        // local - UTC, seconds
} TZInfo;


#define NV_CALLSIGN_LEN         12      // max call sign, including EOS
#define NV_ONAIR_LEN            30      // max ONAIR text, including EOS
#define NV_TITLE_LEN            70      // max alternate callsign, including EOS


// manage callsign display area and alternate uses
typedef enum {
    CT_CALL,                            // display real call
    CT_TITLE,                           // display title text
    CT_BOTH,                            // alternate between call and title
    CT_ONAIR,                           // display ON AIR message
} Call_t;
typedef struct {
    uint16_t fg, bg;                    // fg/bg colors unless...
    uint8_t rainbow;                    // bg is rainbow
} CallColors_t;
typedef struct {
    char call[NV_CALLSIGN_LEN];         // real callsign for CT_CALL, used by setup.cpp
    char onair[NV_ONAIR_LEN];           // real on-air message for CT_ONAIR, used by setup.cpp
    char title[NV_TITLE_LEN];           // CT_TITLE text if used
    Call_t ct_prefer;                   // CT_CALL CT_TITLE or CT_BOTH set via menu
    Call_t now_showing;                 // displaying CT_CALL CT_TITLE or CT_ONAIR
    time_t next_update;                 // when to rotate if ct_prefer is CT_BOTH
    CallColors_t call_col;
    CallColors_t title_col;
    CallColors_t onair_col;
    SBox box;                           // text box
} CallsignInfo;
extern CallsignInfo cs_info;


#define DE_INFO_ROWS    3               // n text rows in DE pane -- not counting top row
#define DX_INFO_ROWS    5               // n text rows in DX pane


extern Adafruit_RA8875 tft;             // drawing layer
extern Adafruit_MCP23X17 mcp;           // I2C digital IO device
extern bool found_mcp;                  // whether found
extern TZInfo de_tz, dx_tz;             // time zone info
extern SBox NCDXF_b;                    // NCDXF box, and more

#define PLOTBOX123_W    160             // top plot box width
#define PLOTBOX123_H    148             // top plot box height, ends just above map border
#define PLOTBOX0_W      139             // PANE_0 width - overlays DE/DX panels including borders
#define PLOTBOX0_H      332             // PANE_0 height - overlays DE/DX panels including borders
extern SBox sensor_b;

#define PANETITLE_H     27              // pane title baseline
#define SUBTITLE_Y0     32              // sub title y down from box top
#define LISTING_Y0      47              // first entry y down from box top
#define LISTING_DY      14              // listing row separation

// rect offset above listing text
#if BUILD_W==800
#define LISTING_OS      2               // listing row rect offset
#else
#define LISTING_OS      3               // listing row rect offset
#endif

extern SBox clock_b;                    // main time
extern SBox auxtime_b;                  // extra time 
extern SCircle satpass_c;               // satellite pass horizon

extern uint8_t night_on;                // show night portion of map on/off
extern uint8_t names_on;                // show place names when roving

extern SBox desrss_b, dxsrss_b;         // sun rise/set display
extern uint8_t desrss, dxsrss;          // sun rise/set chpice
enum {
    DXSRSS_INAGO,                       // display time from now
    DXSRSS_ATAT,                        // display local time
    DXSRSS_PREFIX,                      // must be last
    DXSRSS_N,
};

// show NCDXF beacons or one of other controls
// N.B. names must fit within NCDXF_b
#define BRBMODES                  \
    X(BRB_SHOW_BEACONS, "NCDXF")  \
    X(BRB_SHOW_ONOFF,   "On/Off") \
    X(BRB_SHOW_PHOT,    "PhotoR") \
    X(BRB_SHOW_BR,      "Brite")  \
    X(BRB_SHOW_SWSTATS, "Spc Wx") \
    X(BRB_SHOW_BME76,   "BME@76") \
    X(BRB_SHOW_BME77,   "BME@77") \
    X(BRB_SHOW_DXWX,    "DX Wx")  \
    X(BRB_SHOW_DEWX,    "DE Wx")

#define X(a,b)  a,                      // expands BRBMODES to enum and comma
typedef enum {
    BRBMODES
    BRB_N                               // count
} BRB_MODE;
#undef X

extern uint8_t brb_mode;                // one of BRB_MODE
extern time_t brb_next_update;          // time at which to update
extern uint16_t brb_rotset;             // bitmask of all active BRB_MODE choices
                                        // N.B. brb_rotset must always include brb_mode
#define BRBIsRotating()                 ((brb_rotset & ~(1 << brb_mode)) != 0)  // any bits other than mode
extern const char *brb_names[BRB_N];    // menu names -- must be in same order as BRB_MODE

// map projection styles
extern uint8_t map_proj;

#define MAPPROJS \
    X(MAPP_MERCATOR,  "Mercator")  \
    X(MAPP_AZIMUTHAL, "Azimuthal") \
    X(MAPP_AZIM1,     "Azim One")  \
    X(MAPP_ROB,       "Robinson")

#define X(a,b)  a,                      // expands MAPPROJS to enum plus comma
typedef enum {
    MAPPROJS
    MAPP_N
} MapProjection;
#undef X

extern const char *map_projnames[MAPP_N];   // projection names

#define AZIM1_ZOOM       1.1F           // horizon will be 180/AZIM1_ZOOM degrees from DE
#define AZIM1_FISHEYE    2.0F          // center zoom -- 1 is natural

// map grid options
typedef enum {
    MAPGRID_OFF,
    MAPGRID_TROPICS,
    MAPGRID_LATLNG,
    MAPGRID_MAID,
    MAPGRID_AZIM,
    MAPGRID_CQZONES,
    MAPGRID_ITUZONES,
    MAPGRID_N
} MapGridStyle;
extern uint8_t mapgrid_choice;
extern const char *grid_styles[MAPGRID_N];

extern SBox dx_info_b;                  // dx info pane
extern SBox satname_b;                  // satellite name pick
extern SBox de_info_b;                  // de info pane
extern SBox map_b;                      // main map 
extern SBox view_btn_b;                 // map view menu button
extern SBox dx_maid_b;                  // dx maidenhead pick
extern SBox de_maid_b;                  // de maidenhead pick
extern SBox lkscrn_b;                   // screen lock icon button

extern SBox skip_b;                     // common "Skip" button


// size and location of maidenhead labels
#define MH_TR_H  9                      // top row background height
#define MH_TR_DX 2                      // top row char cell x indent
#define MH_TR_DY 1                      // top row char cell y down
#define MH_RC_W  8                      // right columns background width
#define MH_RC_DX 1                      // right column char cell x indent
#define MH_RC_DY 5                      // right column char cell y down


#define RSS_BG_COLOR    RGB565(0,40,80) // RSS banner background color
#define RSS_FG_COLOR    RA8875_WHITE    // RSS banner text color
#define RSS_DEF_INT     15              // RSS default interval, secs
#define RSS_MIN_INT     5               // RSS minimum interval, secs

extern char *stack_start;               // used to estimate stack usage



// touch screen actions
typedef enum {
    TT_NONE,                            // no touch event
    TT_TAP,                             // brief touch event
    TT_TAP_BX,                          // tap with any button other than 1
} TouchType;

// master state whether we are showing the main hamclock page
extern bool mainpage_up;


typedef struct {
    // fields from data source
    char city[32];
    float temperature_c;
    float humidity_percent;
    float pressure_hPa;                 // sea level
    float wind_speed_mps;
    char wind_dir_name[4];
    char clouds[32];
    char conditions[32];
    char attribution[32];
    int8_t pressure_chg;                // < = > 0
    int timezone;                       // seconds WRT UTC
} WXInfo;

#define N_WXINFO_FIELDS 11              // n fields from data source


// cursor distance to map point
#define MAX_CSR_DIST    (150/pan_zoom.zoom)             // miles


// DXSpot used in several places
#define MAX_PREF_LEN            6       // maximumm prefix length, including EOS
#define MAX_SPOTCALL_LEN        12      // including \0
#define MAX_SPOTMODE_LEN        8       // including \0
#define MAX_SPOTGRID_LEN        MAID_CHARLEN
typedef struct {

    // adif:           "my_*" fields are considered RX
    // dxcluster:      spotted is TX, spotter is RX
    // ontheair:       spotted is TX, repurpose rx_call for id and rx_grid for program name
    // psk live spots: if PSKMB_OFDE then DE is TX, else DE is RX

    char tx_call[MAX_SPOTCALL_LEN];
    char tx_grid[MAX_SPOTGRID_LEN];
    char rx_call[MAX_SPOTCALL_LEN];
    char rx_grid[MAX_SPOTGRID_LEN];

    int tx_dxcc;
    int rx_dxcc;

    char mode[MAX_SPOTMODE_LEN];        // operating mode
    LatLong rx_ll, tx_ll;               // locations
    float kHz;                          // freq
    float snr;                          // only used by pskreporter.cpp
    time_t spotted;                     // UTC when spotted
} DXSpot;


/*********************************************************************************************
 *
 * ESPHamClock.cpp
 *
 */


extern void drawAllSymbols(void);
extern void drawTZ (TZInfo &tzi);
extern bool inBox (const SCoord &s, const SBox &b);
extern bool inCircle (const SCoord &s, const SCircle &c);
extern bool boxesOverlap (const SBox &b1, const SBox &b2);
extern void doReboot (bool minus_K, bool minus_0);
extern void printFreeHeap (const __FlashStringHelper *label);
extern void getWorstMem (int *heap, int *stack);
extern void wdDelay(int ms);
extern bool timesUp (uint32_t *prev, uint32_t dt);
extern const SCoord raw2appSCoord (const SCoord &s_raw);
extern bool overMap (const SCoord &s);
extern bool overMap (const SBox &b);
extern bool overRSS (const SCoord &s);
extern bool overRSS (const SBox &b);
extern void setScreenLock (bool on);
extern void newDE (LatLong &ll, const char grid[MAID_CHARLEN]);
extern void newDX (LatLong &ll, const char grid[MAID_CHARLEN], const char *override_prefix);
extern void drawDXPath(void);
extern bool screenIsLocked(void);
extern time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs);
extern void eraseScreen(void);
extern void setMapTagBox (const char *tag, const SCoord &c, uint16_t r, SBox &box);
extern void drawMapTag (const char *tag, const SBox &box, uint16_t txt_color = RA8875_WHITE,
        uint16_t bg_color = RA8875_BLACK);
extern void setDXPrefixOverride (char p[MAX_PREF_LEN]);
extern bool getDXPrefix (char p[MAX_PREF_LEN]);
extern void drawDemoRunner(void);
extern void fillSBox (const SBox &box, uint16_t color);
extern void drawSBox (const SBox &box, uint16_t color);
extern void shadowString (const char *str, bool shadow, uint16_t color, uint16_t x0, uint16_t y0);
extern bool overMapScale (const SCoord &s);
extern uint16_t getGoodTextColor (uint16_t bg_c);
extern void drawDEFormatMenu(void);
extern bool postDiags (void);



#if defined(__GNUC__)
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...) __attribute__ ((format (__printf__, 3, 4)));
#else
extern void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...);
#endif

#if defined(__GNUC__)
extern void fatalError (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
extern void fatalError (const char *fmt, ...);
#endif






/*********************************************************************************************
 *
 * BME280.cpp
 *
 */

// measurement queues
#define N_BME_READINGS          250     // n measurements stored for each sensor
typedef struct {
    time_t u[N_BME_READINGS];           // circular queue of UNIX sensor read times, 0 if no data
    float t[N_BME_READINGS];            // circular queue of temperature values in user's units
    float p[N_BME_READINGS];            // circular queue of pressure values in user's units
    float h[N_BME_READINGS];            // circular queue of humidity values in percent
    int q_head;                         // index of next q entries to use
    int i2c;                            // i2c addr
} BMEData;

typedef enum {
    BME_76,                             // index for sensor at 0x76
    BME_77,                             // index for sensor at 0x77
    MAX_N_BME                           // max sensors connected
} BMEIndex; 

extern void initBME280 (void);
extern void readBME280 (void);
extern void drawBMEStats (void);
extern void drawBME280Panes(void);
extern void drawOneBME280Pane (const SBox &box, PlotChoice ch);
extern bool newBME280data (void);
extern const BMEData *getBMEData (BMEIndex i, bool fresh_read);
extern int getNBMEConnected (void);
extern float dewPoint (float T, float RH);
extern void doBMETouch (const SCoord &s);
extern bool recalBMETemp (BMEIndex device, float new_corr);
extern bool recalBMEPres (BMEIndex device, float new_corr);





/*********************************************************************************************
 *
 * OTAupdate.cpp
 *
 */

extern bool newVersionIsAvailable (char *nv, uint16_t nvl);
extern bool askOTAupdate(char *new_ver, bool show_pending, bool def_yes);
extern void doOTAupdate(const char *ver);








/*********************************************************************************************
 *
 * adif.cpp
 *
 */


extern bool from_set_adif;
extern void updateADIF (const SBox &box, bool fresh);
extern bool checkADIFTouch (const SCoord &s, const SBox &box);
extern void drawADIFSpotsOnMap (void);
extern void loadADIFFile (GenReader &gr, int &n_good, int &n_bad);
extern void freshenADIFFile (void);
extern void drawADIFPane (const SBox &box, const char *filename);
extern bool getClosestADIFSpot (LatLong &ll, DXSpot *sp, LatLong *llp);
extern bool checkADIFFilename (const char *fn, Message &ynot);
extern bool getADIFPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll);
extern bool onADIFList (const DXSpot &spot, bool chk_dxcc, bool chk_grid, bool chk_pref, bool chk_band);






/*********************************************************************************************
 *
 * adif_parser.cpp
 *
 */

extern int readADIFFile (GenReader &gr, DXSpot *&spots, bool use_wl, int &n_bad);




/*********************************************************************************************
 *
 * asknewpos.cpp
 *
 */

extern bool askNewPos (const SBox &b, LatLong &ll, char grid[MAID_CHARLEN]);





/*********************************************************************************************
 *
 * astro.cpp
 *
 */

typedef struct {
    float az, el;                               // topocentric, rads
    float ra, dec;                              // geocentric EOD, rads
    float gha;                                  // geocentric rads
    float dist;                                 // geocentric km
    float vel;                                  // topocentric m/s
    float phase;                                // rad angle from new
} AstroCir;

extern AstroCir lunar_cir, solar_cir;

extern void now_lst (double mjd, double lng, double *lst);
extern void getLunarCir (time_t t0, const LatLong &ll, AstroCir &cir);
extern void getSolarCir (time_t t0, const LatLong &ll, AstroCir &cir);
extern void getSolarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett);
extern void getLunarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett);

#define SECSPERDAY              (3600*24L)      // seconds per day
#define MINSPERDAY              (24*60)         // minutes per day
#define DAYSPERWEEK             7               // days per week





/*********************************************************************************************
 *
 * bands.cpp
 *
 */


/* consolidated list of supported bands
 */
#define SUPPORTED_BANDS                                 \
    X(HAMBAND_160M, 160, "160",  90, BAND160_CSPR)      \
    X(HAMBAND_80M,   80,  "80",  82, BAND80_CSPR)       \
    X(HAMBAND_60M,   60,  "60",  77, BAND60_CSPR)       \
    X(HAMBAND_40M,   40,  "40",  70, BAND40_CSPR)       \
    X(HAMBAND_30M,   30,  "30",  62, BAND30_CSPR)       \
    X(HAMBAND_20M,   20,  "20",  51, BAND20_CSPR)       \
    X(HAMBAND_17M,   17,  "17",  43, BAND17_CSPR)       \
    X(HAMBAND_15M,   15,  "15",  34, BAND15_CSPR)       \
    X(HAMBAND_12M,   12,  "12",  25, BAND12_CSPR)       \
    X(HAMBAND_10M,   10,  "10",  12, BAND10_CSPR)       \
    X(HAMBAND_6M,     6,   "6",   4, BAND6_CSPR)        \
    X(HAMBAND_2M,     2,   "2",   0, BAND2_CSPR)

#define X(a,b,c,d,e) a,                         // expands SUPPORTED_BANDS to each enum and comma
typedef enum {
    SUPPORTED_BANDS
    HAMBAND_N
} HamBandSetting;
#undef X

#define HAMBAND_NONE HAMBAND_N                  // handy impossible alias

extern int findBandEdges (HamBandSetting h, const char *mode, float min_kHz[], float max_kHz[], int n_kHz);
extern HamBandSetting findHamBand (float kHz);
extern HamBandSetting findHamBand (int meters);
extern ColorSelection findColSel (HamBandSetting h);
extern const char *findBandName (HamBandSetting h);
extern bool isValidSubBand (const char *mode);
extern const char *findHamMode (float kHz);








/*********************************************************************************************
 *
 * blinker.cpp
 *
 */

// special rate cookies
#define BLINKER_OFF     (-10)           // special hz to mean constant off
#define BLINKER_ON      (-11)           // special hz to mean constant on, 0 ok since rate can not be zero
#define BLINKER_UNKNOWN (-12)           // special hz to mean unknown state


extern void startBinkerThread (int pin, bool on_is_low);
extern void setBlinkerRate (int pin, int hz);
extern void disableBlinker (int pin);

extern void startMCPPoller (int pin);
extern void disableMCPPoller (int pin);
extern bool readMCPPoller (int pin);
extern bool setUserGPIO (int pin, int hz, Message &ynot);
extern bool getUserGPIO (int pin, bool latched, bool &state, Message &ynot);






/*********************************************************************************************
 *
 * bmp.cpp
 *
 */

// means by which to fit an arbitrary image size into SBox
typedef enum {
    FIT_CROP,                   // crop center, no resizing
    FIT_RESIZE,                 // resize to fit
    FIT_FILL,                   // stretch to fill
} ImageRefit;

extern bool createBMP565Header (uint8_t *&hdr, int &hdr_len, int &file_bytes, int img_w, int img_h);
extern bool readBMPHeader (GenReader &gr, int &img_w, int &img_h, int &img_bpp, int &img_pad, Message &ynot);
extern bool readBMPImage (GenReader &gr, const SBox &box, uint16_t *&box_565, ImageRefit fit, Message &ynot);
extern bool writeBMP565File (const char *filename, uint16_t *&pix_565, int img_w, int img_h, Message &ynot);






/*********************************************************************************************
 *
 * brightness.cpp
 *
 */


extern void drawBrightness (void);
extern void initBrightness (void);
extern void setupBrightness (void);
extern void followBrightness (void);
extern bool brightnessOn(void);
extern void brightnessOff(void);
extern bool setDisplayOnOffTimes (int dow, uint16_t on, uint16_t off, int &idle);
extern bool getDisplayOnOffTimes (int dow, uint16_t &on, uint16_t &off);
extern bool getDisplayInfo (uint16_t &percent, uint16_t &idle_min, uint16_t &idle_left_sec);
extern bool brDimmableOk(void);
extern bool brOnOffOk(void);
extern bool found_phot, found_ltr;
extern void doNCDXFBoxTouch (TouchType tt, const SCoord &s);



/*********************************************************************************************
 *
 * cachefile.cpp
 *
 */

// special cleanCache ages
#define CACHE_FOREVER 0                         // never remove matching files
#define CACHE_NONE    1                         // remove all files older than 1 second

extern FILE *openCachedFile (const char *fn, const char *url, int max_age, int min_size);
extern bool cleanCache (const char *contains, int max_age);





/*********************************************************************************************
 *
 * callsign.cpp
 *
 */
extern void initCallsignInfo(void);
extern void doCallsignTouch (const SCoord &s);
extern void updateCallsign (bool force_draw);
extern void setOnAirHW (bool on);
extern void setOnAirSW (bool on);
extern void setCallsignInfo (Call_t t, const char *text, uint16_t *fg, uint16_t *bg, uint8_t *rainbow);




/*********************************************************************************************
 *
 * cities.cpp
 *
 */
extern const char *getNearestCity (const LatLong &ll, LatLong &city_ll, int *max_l);




/*********************************************************************************************
 *
 * clocks.cpp
 *
 */


// DETIME options
#define DETIMES                                   \
    X(DETIME_INFO,         "All info")            \
    X(DETIME_ANALOG,       "Simple analog")       \
    X(DETIME_CAL,          "Calendar")            \
    X(DETIME_ANALOG_DTTM,  "Annotated analog")    \
    X(DETIME_DIGITAL_12,   "Digital 12 hour")     \
    X(DETIME_DIGITAL_24,   "Digital 24 hour")

#define X(a,b)  a,                      // expands DETIMES to each enum followed by comma
enum {
    DETIMES
    DETIME_N
};
#undef X

extern const char *detime_names[DETIME_N];

extern uint8_t de_time_fmt;     // one of DETIME_*
extern void initTime(void);
extern time_t nowWO(void);
extern time_t myNow(void);
extern void updateClocks(bool all);
extern bool clockTimeOk(void);
extern void changeTime (time_t t);
extern bool checkClockTouch (SCoord &s);
extern bool TZMenu (TZInfo &tzi, const LatLong &ll);
extern void drawDESunRiseSetInfo(void);
extern void drawCalendar(bool force);
extern void hideClocks(void);
extern void showClocks(void);
extern void drawDXSunRiseSetInfo(void);
extern int DEWeekday(void);
extern int utcOffset(void);
extern bool crackMonth (const char *name, int *monp);





/*********************************************************************************************
 *
 * color.cpp
 *
 */

#define GRAY    RGB565(140,140,140)
#define BRGRAY  RGB565(200,200,200)
#define DKGRAY  RGB565(50,50,50)
#define DYELLOW RGB565(255,212,112)

extern void hsvtorgb (uint8_t *r, uint8_t *g, uint8_t *b, uint8_t h, uint8_t s, uint8_t v);
extern void rgbtohsv (uint8_t *h, uint8_t *s, uint8_t *v, uint8_t r, uint8_t g, uint8_t b);
extern void RGB565_2_HSV (uint16_t rgb565, uint8_t *hp, uint8_t *sp, uint8_t *vp);
extern uint16_t HSV_2_RGB565 (uint8_t h, uint8_t s, uint8_t v);






/*********************************************************************************************
 *
 * configs.cpp
 *
 */

extern void runConfigManagement(void);





/*********************************************************************************************
 *
 * contests.cpp
 *
 */

#define CONTESTS_INTERVAL (2)                   // pane update interval, secs

extern bool updateContests (const SBox &box, bool fresh);
extern bool checkContestsTouch (const SCoord &s, const SBox &box);
extern int getContests (char **&titles, char **&dates);





/*********************************************************************************************
 *
 * cputemp.cpp
 *
 */

extern bool getCPUTemp (float &t_C);
extern void plotCPUTempHistory (void);





/*********************************************************************************************
 *
 * debug.cpp
 *
 */

// see ArduinoLib.h for DEBUG_* subsystem defines

extern void prDebugLevels (WiFiClient &client, int indent);





/*********************************************************************************************
 *
 * drawextra.cpp
 *
 */

extern void fillPolygon (const SCoord poly[], int n_poly, uint16_t color);
extern void drawPolygon (const SCoord poly[], int n_poly, uint16_t color);




/*********************************************************************************************
 *
 * dxcluster.cpp
 *
 */

// whether to draw transmit/receive symbols
typedef enum {
    LOME_TXEND,
    LOME_RXEND,
    LOME_BOTH,
} LabelOnMapEnd;

// whether to draw text label or just dot symbol
typedef enum {
    LOMD_ALL,
    LOMD_JUSTDOT
} LabelOnMapDot;

extern bool updateDXCluster (const SBox &box, bool fresh);
extern void checkDXCluster(void);
extern void closeDXCluster(void);
extern bool checkDXClusterTouch (const SCoord &s, const SBox &box);
extern bool getDXClusterSpots (DXSpot **spp, uint8_t *nspotsp);
extern void drawDXClusterSpotsOnMap (void);
extern bool isDXClusterConnected(void);
extern void sendDXClusterDELLGrid(void);
extern bool getClosestDXCluster (LatLong &ll, DXSpot *sp, LatLong *llp);
extern bool getDXCPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll);
extern bool connectDXCluster (void);
extern const DXSpot *findDXCCall (const char *call);
extern bool injectDXClusterSpot (const char *tx_call, const char *rx_call, const char *kHz, Message &ynot);



#if defined(__GNUC__)
extern void dxcLog (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else   
extern void dxcLog (const char *fmt, ...);
#endif   





/*********************************************************************************************
 *
 * dxpeds.cpp
 *
 */

#define DXPEDS_INTERVAL (2)                     // pane update interval, secs

// info about one DX expedition. must be global for drawMouseLoc()
typedef struct {
    time_t start_t;                             // contest start time, always UTC
    time_t end_t;                               // contest end time, always UTC
    char *date_str;                             // malloced string describing expedition period
    char *title;                                // malloced string describing expedition
    char *call;                                 // malloc call sign
    char *loc;                                  // malloc location description
    char *url;                                  // malloced web page URL
    bool was_active;                            // set once now > start_t
    LatLong ll;                                 // location derived from call
    int dxcc;                                   // DXCC
    char prefix[MAX_PREF_LEN];                  // call's prefix
} DXPedEntry;

// info returned from findDXPedsWorked()
typedef struct {
    HamBandSetting hb;
    char mode[MAX_SPOTMODE_LEN];
} DXPedsWorked;

extern bool updateDXPeds (const SBox &box, bool fresh);
extern bool checkDXPedsTouch (const SCoord &s, const SBox &box);
extern int getDXPeds (char **&titles, char **&dates);
extern void drawDXPedsOnMap (void);
extern bool getPaneDXPed (const SCoord &ms, DXPedEntry *&dxp);
extern bool getClosestDXPed (LatLong &ll, DXPedEntry *&dxp);
extern void tellDXPedsSpotChanged (void);
extern void addDXPedsWorked (const DXSpot &s);
extern void resetDXPedsWorked (void);
extern int findDXPedsWorked (const DXPedEntry *dxp, DXPedsWorked *&worked);
extern bool findDXPedsCall (const DXSpot *sp);
extern bool dxpedsWatchingCluster(void);






/*********************************************************************************************
 *
 * dxpeds_hide.cpp
 *
 */

extern bool isDXPedsHidden (const char *line);
extern bool isDXPedsHidden (const DXPedEntry *dxp);
extern int nDXPedsHidden (void);
extern void addDXPedsHidden (const DXPedEntry *dxp);
extern void rmDXPedsHidden (const DXPedEntry *dxp);





/*********************************************************************************************
 *
 * earthmap.cpp
 *
 */



#define DX_R    6                       // dx marker radius (erases better if even)
#define DX_COLOR RA8875_GREEN

typedef struct {
    uint8_t zoom;                       // integral value only [MIN_ZOOM,MAX_ZOOM]
    int16_t pan_x, pan_y;               // offset from original position, unzoomed pixels, + right/up
} PanZoom;
extern PanZoom pan_zoom;
#define MIN_ZOOM     1                                  // minimum zoom factor
#define MAX_ZOOM     (BUILD_W == 800 ? 4 : 3)           // max zoom factor
#define MIN_PANX     (-EARTH_W/2)                       // smallest allowed pan_x
#define MAX_PANX     (EARTH_W/2)                        // largest allowed pan_x
#define MIN_PANY(z)  (-(EARTH_H/2) + (EARTH_H/2)/(z))   // smallest allowed pan_y, depends on zoom
#define MAX_PANY(z)  ((EARTH_H/2) - (EARTH_H/2)/(z))    // largest allowed pan_y, depends on zoom

typedef struct {
    LatLong ll;                         // proposed location
    SCoord s;                           // tap location
    bool pending;                       // whether to engage at proper map drawing time
} MapPopup;
extern MapPopup map_popup;

extern SCircle dx_c;
extern LatLong dx_ll;

extern uint16_t map_x0, map_y0;
extern uint16_t map_w, map_h;

extern bool mapmenu_pending;            // draw map menu at next opportunity
extern uint8_t show_lp;                 // show prop long path, else short path
#define ERAD_M          3959.0F         // earth radius, miles
#define MI_PER_KM       0.621371F
#define KM_PER_MI       1.609344F

#define DE_R 6                          // radius of DE marker   (erases better if even)
#define DEAP_R 6                        // radius of DE antipodal marker (erases better if even)
#define DE_COLOR  RGB565(255,125,0)     // orange

extern uint16_t EARTH_GRIDC, EARTH_GRIDC00;

extern SCircle de_c;
extern LatLong de_ll;
extern float sdelat, cdelat;
extern SCircle deap_c;
extern LatLong deap_ll;
extern LatLong sun_ss_ll;
extern LatLong moon_ss_ll;

#define SUN_R 6                         // radius of sun marker
extern float sslng, sslat, csslat, ssslat;
extern SCircle sun_c;

#define MOON_R 6                        // radius of moon marker
#define MOON_COLOR  RGB565(150,150,150)
extern SCircle moon_c;

extern uint8_t flash_crc_ok;

extern void drawMoreEarth (void);
extern void eraseDEMarker (void);
extern void eraseDEAPMarker (void);
extern void drawDEMarker (bool force);
extern bool showDXMarker(void);
extern bool showDEMarker(void);
extern void drawDEAPMarker (void);
extern void drawDEInfo (void);
extern void drawDECalTime (bool center);
extern void drawDXTime (void);
extern void initEarthMap (void);
extern void antipode (LatLong &to, const LatLong &from);
extern void drawMapCoord (const SCoord &s);
extern void drawMapCoord (uint16_t x, uint16_t y);
extern void drawSun (void);
extern void drawMoon (void);
extern void drawDXInfo (void);
extern void ll2s (const LatLong &ll, SCoord &s, uint8_t edge);
extern void ll2s (float lat, float lng, SCoord &s, uint8_t edge);
extern void ll2sRaw (const LatLong &ll, SCoord &s, uint8_t edge);
extern void ll2sRaw (float lat, float lng, SCoord &s, uint8_t edge);
extern bool s2ll (uint16_t x, uint16_t y, LatLong &ll);
extern bool s2ll (const SCoord &s, LatLong &ll);
extern bool checkPathDirTouch (const SCoord &s);
extern void propDEPath (bool long_path, const LatLong &to_ll, float *distp, float *bearp);
extern void propPath (bool long_path, const LatLong &from_ll, float sflat, float cflat, const LatLong &to_ll,
        float *distp, float *bearp);
extern bool waiting4DXPath(void);
extern void eraseSCircle (const SCircle &c);
extern void roundLatLong (LatLong &ll);
extern void initScreen(void);
extern float lngDiff (float dlng);
extern bool overViewBtn (const SCoord &s, uint16_t border);
extern bool segmentSpanOk (const SCoord &s0, const SCoord &s1, uint16_t border);
extern bool segmentSpanOkRaw (const SCoord &s0, const SCoord &s1, uint16_t border);
extern bool desiredBearing (const LatLong &ll, float &bear);
extern void checkBGMap(void);
extern void normalizePanZoom (PanZoom &pz);








/*********************************************************************************************
 *
 * earthsat.cpp
 *
 */

#define NV_SATNAME_LEN          9       // max sat name, including EOS

typedef struct _sat_now {
    char name[NV_SATNAME_LEN];          // name
    float az, el;                       // az, el degs
    float range, rate;                  // km, m/s + receding
    float raz, saz;                     // rise and set az, degs; either may be SAT_NOAZ
    float rdt, sdt;                     // next rise and set, hrs from now; rdt < 0 if up now
    _sat_now() { name[0] = '\0'; }      // constructor to insure name properly empty
} SatNow;
#define SAT_NOAZ        (-999)          // error flag for raz or saz
#define SAT_MIN_EL      -0.4F           // min elevation, rough approx for refraction
#define TLE_LINEL       70              // TLE line length, including EOS

extern void updateSatPath(void);
extern void drawSatPathAndFoot(void);
extern void updateSatPass(void);
extern bool querySatSelection(void);
extern bool checkSatMapTouch (const SCoord &s);
extern bool checkSatNameTouch (const SCoord &s);
extern void drawSatPass(void);
extern bool setNewSatCircumstance (void);
extern void drawSatName(void);
extern void drawOneTimeDX(void);
extern void drawOneTimeDE(void);
extern bool setSatFromName (const char *new_name);
extern bool setSatFromTLE (const char *name, const char *t1, const char *t2);
extern bool initSat(void);
extern bool getSatNow (SatNow &satnow);
extern bool getSatCir (Observer *snow_obs, time_t t0, SatNow &sat_at_t0);
extern bool isNewPass(void);
extern bool isSatMoon(void);
extern const char **getAllSatNames(void);
extern int nextSatRSEvents (time_t **rises, float **raz, time_t **sets, float **saz);
extern bool isSatDefined(void);
extern void drawDXSatMenu(const SCoord &s);
extern bool dx_info_for_sat;
extern void satResetIO(void);





/*********************************************************************************************
 *
 * emetool.cpp
 *
 */

extern void drawEMETool (void);



/*********************************************************************************************
 *
 * favicon.cpp
 *
 */

extern void writeFavicon (FILE *fp);





/*********************************************************************************************
 *
 * fsfree.cpp
 *
 */


typedef unsigned long long DSZ_t;               // really really paranoid size type
typedef struct {
    char name[50];      // name with EOS
    char date[30];      // ISO 8601 date with EOS
    time_t t0;          // unix time
    DSZ_t len;          // n bytes
} FS_Info;

extern FS_Info *getConfigDirInfo (int *n_info, char **fs_name, DSZ_t *fs_size, DSZ_t *fs_used);
extern bool getFSSize (DSZ_t &cap, DSZ_t &used);
extern bool checkFSFull (bool &really);






/*********************************************************************************************
 *
 * gimbal.cpp
 *
 */

extern bool haveGimbal(void);
extern void updateGimbal (const SBox &box);
extern bool checkGimbalTouch (const SCoord &s, const SBox &box);
extern void stopGimbalNow(void);
extern void closeGimbal(void);
extern bool getGimbalState (bool &connected, bool &vis_now, bool &has_el, bool &is_stop, bool &is_auto,
    float &az, float &el);
extern bool commandRotator (const char *new_state, const char *new_az, const char *new_el, Message &ynot);







/*********************************************************************************************
 *
 * gpsd.cpp
 *
 */

extern bool getGPSDLatLong(LatLong *llp);
extern time_t getGPSDUTC(void);
extern void updateGPSDLoc(void);
extern time_t crackISO8601 (const char *iso);






/*********************************************************************************************
 *
 * grayline.cpp
 *
 */

extern void plotGrayline(void);




/*********************************************************************************************
 *
 * infobox.cpp
 *
 */

extern void drawInfoBox(void);





/*********************************************************************************************
 *
 * kd3tree.cpp
 *
 */

struct kd_node_t {
    float s[3];                         // xyz coords on unit sphere
    struct kd_node_t *left, *right;     // branches
    void *data;                         // user data
};

typedef struct kd_node_t KD3Node;

extern KD3Node* mkKD3NodeTree (KD3Node *t, int len, int idx);
extern void freeKD3NodeTree (KD3Node *t, int n_t);
extern void nearestKD3Node (const KD3Node *root, const KD3Node *nd, int level, const KD3Node **best,
    float *best_dist, int *n_visited);
extern void ll2KD3Node (const LatLong &ll, KD3Node *kp);
extern void KD3Node2ll (const KD3Node &n, LatLong *llp);
extern float nearestKD3Dist2Miles(float d);





/*********************************************************************************************
 *
 * liveweb-html.cpp
 *
 */

#define LIVE_BYPPIX     3                               // bytes per pixel

extern char live_html[];




/*********************************************************************************************
 *
 * liveweb.cpp
 *
 */


extern void initLiveWeb(bool verbose);
extern bool liveweb_fs_ready;
extern int n_roweb, n_rwweb;
extern void openLiveWebURL (const char *url);
extern bool isLiveWebTouch (void);





/*********************************************************************************************
 *
 * magdecl.cpp
 *
 */

extern bool magdecl (float l, float L, float e, float y, float *mdp);




/*********************************************************************************************
 *
 * maidenhead.cpp
 *
 */


extern void ll2maidenhead (char maid[MAID_CHARLEN], const LatLong &ll);
extern bool maidenhead2ll (LatLong &ll, const char maid[MAID_CHARLEN]);
extern void setNVMaidenhead (NV_Name nv, LatLong &ll);
extern void getNVMaidenhead (NV_Name nv, char maid[MAID_CHARLEN]);




/*********************************************************************************************
 *
 * mapmanage.cpp
 *
 */

// unique enum for each band in BandCdtnMatrix
typedef enum {
    PROPBAND_80M,
    PROPBAND_40M,
    PROPBAND_30M,
    PROPBAND_20M,
    PROPBAND_17M,
    PROPBAND_15M,
    PROPBAND_12M,
    PROPBAND_10M,
    PROPBAND_N,
} PropMapBand;

#define PROPBAND_NONE           PROPBAND_N      // handy alias for none

// CoreMaps enum and corresponding CoreMapInfo
// N.B. add only to the end of this table to preserve prior eeprom settings
#define COREMAPS                                                                        \
    X(CM_COUNTRIES, 7*SECSPERDAY,       "Countries", PROPBAND_NONE, false, false)       \
    X(CM_TERRAIN,   7*SECSPERDAY,       "Terrain",   PROPBAND_NONE, false, false)       \
    X(CM_DRAP,      DRAPMAP_INTERVAL,   "DRAP",      PROPBAND_NONE, false, false)       \
    X(CM_MUF_V,     VOACAP_INTERVAL,    "MUF-VCAP",  PROPBAND_NONE, false, false)       \
    X(CM_MUF_RT,    MUF_RT_INTERVAL,    "MUF-RT",    PROPBAND_NONE, false, false)       \
    X(CM_AURORA,    AURORA_INTERVAL,    "Aurora",    PROPBAND_NONE, false, false)       \
    X(CM_WX,        DXWX_INTERVAL,      "Weather",   PROPBAND_NONE, false, false)       \
    X(CM_PMTOA,     BC_INTERVAL,        "TOA",       PROPBAND_NONE, false, false)       \
    X(CM_PMREL,     BC_INTERVAL,        "REL",       PROPBAND_NONE, false, false)       \
    X(CM_CLOUDS,    CLOUDS_INTERVAL,    "Clouds",    PROPBAND_NONE, false, false)       \
    X(CM_USER,      CACHE_FOREVER,      "User",      PROPBAND_NONE, false, false)

#define X(a,b,c,d,e,f)  a,                      // expands COREMAPS to each enum followed by comma
typedef enum {
    COREMAPS
    CM_N
} CoreMaps;
#undef X

#define CM_NONE CM_N                            // handy alias meaning none active

// macro to test whether the given core map style is just a file (not an active query)
#define CM_ISFILE(cm)     ((cm) == CM_COUNTRIES || (cm) == CM_TERRAIN || (cm) == CM_DRAP || \
                           (cm) == CM_AURORA    || (cm) == CM_WX      || (cm) == CM_MUF_RT || \
                           (cm) == CM_CLOUDS    || (cm) == CM_USER)

typedef struct {
    int max_age;                                // refresh interval, secs
    const char *name;                           // style name
    PropMapBand band;                           // band iff CM_PMTOA or CM_PMREL else CM_NONE
    bool saw_hi, saw_lo;                        // for automap hysteresis control
} CoreMapInfo;

extern CoreMaps core_map;                       // currently visible map. must be set in map_rotset
extern CoreMapInfo cm_info[CM_N];               // info about each core map

extern SBox mapscale_b;                         // map scale box

extern void initCoreMaps(void);
extern bool installFreshMaps(void);
extern float propBand2MHz (PropMapBand band);
extern int propBand2Band (PropMapBand band);
extern bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp);
extern bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp);
extern const char *getCoreMapStyle (CoreMaps cm, char s[NV_COREMAPSTYLE_LEN]);
extern void drawMapScale(void);
extern bool mapScaleIsUp(void);
extern void insureCoreMap(void);
extern bool installWebMapImages (WiFiClient &client, long con_length, ImageRefit fit, Message &ynot);
extern void rmWebMapImages (void);
extern bool allWebMapImagesOk(void);



extern uint16_t map_rotset;                     // maps in rotation, must include core_map
extern bool mapIsRotating(void);
extern time_t nextMapUpdate (int interval);
extern void rotateNextMap();
extern void saveCoreMaps(void);
extern void logMapRotSet(void);

// handy
#define IS_CMROT(cm)    ((map_rotset & (1<<(cm))) != 0)                                 // cm in rotation
#define RM_CMROT(cm)    (map_rotset &= ~(1<<(int)(cm)))                                 // remove cm 
#define DO_CMROT(cm)    do {map_rotset |= (1<<(int)(cm)); core_map = cm;} while (0)     // set cm
#define CM_PMACTIVE()   (core_map == CM_PMTOA || core_map == CM_PMREL)                  // showing either map


#if defined(__GNUC__)
extern void mapMsg (uint32_t dwell_ms, const char *fmt, ...) __attribute__ ((format (__printf__, 2, 3)));
#else
extern void mapMsg (uint32_t dwell_ms, const char *fmt, ...);
#endif


extern bool zinfWiFiFILE (WiFiClient &in_client, int in_n, FILE *out_fp);
extern FILE *fopenOurs (const char *filename, const char *how);
extern void unlinkOurs (const char *filename);



/*********************************************************************************************
 *
 * menu.cpp
 *
 */


typedef enum {
    UF_CLOCKSOK,
    UF_NOCLOCKS,
} UI_UFClock;

typedef enum {
    M_CANCELOK,
    M_NOCANCEL,
} MenuCancellable;

typedef struct _menu_text {
    char *text;                 // mutable "value" memory, must include EOS
    size_t t_mem;               // total text[] memory (string may be shorter)
    char *label;                // mutable "label" memory replaces const MenuItem label, must include EOS
    size_t l_mem;               // total label[] memory (string may be shorter)
    bool to_upper;              // whether to always shift entries to upper case
    unsigned c_pos;             // text[] cursor position index
    unsigned w_pos;             // text[] left window position index
    bool (*text_fp)(struct _menu_text *, Message &ynot);        // call to check text, unless NULL
    void (*label_fp)(struct _menu_text *);                      // call to affect label, unless NULL
} MenuText;

typedef enum {
    MENU_LABEL,                 // insensitive string
    MENU_0OFN,                  // any number set including none, round selector
    MENU_1OFN,                  // exactly 1 of this set, round selector
    MENU_01OFN,                 // exactly 0 or 1 of this set, round selector
    MENU_AL1OFN,                // at least 1 of this set, square selector
    MENU_TOGGLE,                // simple on/off with no grouping, square selector
    MENU_IGNORE,                // ignore this entry entirely
    MENU_TEXT,                  // must supply MenuText* -- N.B. at most one and must be last in items[]
    MENU_BLANK,                 // empty space
} MenuFieldType;

// return whether the given MenuFieldType involves active user interaction
#define MENU_ACTIVE(i)          ((i)!=MENU_LABEL && (i)!=MENU_IGNORE && (i)!=MENU_BLANK)

typedef enum {
    MENU_OK_OK,                 // normal ok button appearance
    MENU_OK_BUSY,               // busy ok button appearance
    MENU_OK_ERR,                // error ok button appearance
} MenuOkState;

typedef struct {
    MenuFieldType type;         // appearance and behavior
    bool set;                   // whether selected
    uint8_t group;              // association
    uint8_t indent;             // pixels to indent
    const char *label;          // string -- user must manage memory
    MenuText *textf;            // text field -- type must be MENU_TEXT -- our label is ignored
} MenuItem;

typedef struct {
    SBox &menu_b;               // initial menu box -- sized automatically and may be moved
    SBox &ok_b;                 // box for Ok button -- user may use later with menuRedrawOk()
    UI_UFClock update_clocks;   // whether to update clocks while waiting
    MenuCancellable cancel;     // whether to just have Ok button
    int n_cols;                 // number of columns in which to display items
    int n_items;                // number of items[]
    MenuItem *items;            // list -- user must manage memory
} MenuInfo;

extern bool runMenu (MenuInfo &menu);
extern void menuMsg (const SBox &box, uint16_t color, const char *msg);
extern void menuRedrawOk (SBox &ok_b, MenuOkState oks);

typedef enum {
    UF_UNUSED,
    UF_TRUE,
    UF_FALSE,
} UI_UFRet;

typedef bool (*UI_UF_t)(void);  // user check function or ...
#define UI_UFuncNone NULL       // ... unused

#define UI_NOTIMEOUT 0          // use to mark to_ms as forever

typedef struct {
    const SBox &inbox;          // overall input box bounds
    UI_UF_t fp;                 // user check function, else UI_UFuncNone
    UI_UFRet fp_true;           // whether fp returned true, or UF_UNUSED
    uint32_t to_ms;             // timeout, msec, or UI_NOTIMEOUT
    UI_UFClock update_clocks;   // whether to update clocks while waiting
    SCoord tap;                 // tapped location and ..
    TouchType tt;               // what type of touch, if any or
    char kb_char;               // keyboard input char code or CHAR_NONE if tap
    bool kb_ctrl, kb_shift;     // whether kb_char was accommpanied by modifier keys
} UserInput;

extern bool waitForUser (UserInput &ui);


/*******************************************************************************************n
 *
 * moonpane.cpp and moon_imgs.cpp
 *
 */

extern void updateMoonPane (const SBox &box);
extern const uint16_t moon_image[HC_MOON_W*HC_MOON_H] PROGMEM;
extern bool checkMoonTouch (const SCoord &s, const SBox &box);










/*********************************************************************************************
 *
 * ncdxf.cpp
 *
 */

#define NCDXF_B_NFIELDS         4       // n fields in NCDXF_b
#define NCDXF_B_MAXLEN          9       // max field length, including EOS

extern void updateBeacons (bool immediate);
extern void updateBeaconMapLocations(void);
extern bool drawNCDXFBox(void);
extern void initBRBRotset(void);
extern void checkBRBRotset(void);
extern void drawNCDXFStats (uint16_t color,
                            const char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                            const char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                            const uint16_t colors[NCDXF_B_NFIELDS]);





/*********************************************************************************************
 *
 * nmea.cpp
 *
 */

extern bool getNMEALatLong(LatLong &ll);
extern time_t getNMEAUTC(void);
extern void updateNMEALoc(void);
extern bool checkNMEAFilename (const char *fn, Message &ynot);






/*********************************************************************************************
 *
 * nvram.cpp
 *
 */


// string valued lengths including trailing EOS
#define NV_WIFI_SSID_LEN        32
#define NV_WIFI_PW_OLD_LEN      32
// NV_CALLSIGN_LEN needed above for CallsignInfo
// NV_SATNAME_LEN needed above for SatNow
#define NV_DXHOST_LEN           26
#define NV_GPSDHOST_OLD_LEN     18
#define NV_GPSDHOST_LEN         36
#define NV_NMEAFILE_LEN         36
#define NV_NTPHOST_OLD_LEN      18
#define NV_NTPHOST_LEN          36
// NV_COREMAPSTYLE_LEN needed above for mapmanage.cpp
#define NV_WIFI_PW_LEN          64
#define NV_DAILYONOFF_LEN       28      // (2*DAYSPERWEEK*sizeof(uint16_t))
#define NV_DE_GRID_LEN          MAID_CHARLEN
#define NV_DX_GRID_LEN          MAID_CHARLEN
// NV_ROTHOST_LEN needed above for setup.cpp
// NV_RIGHOST_LEN needed above for setup.cpp
// NV_FLRIGHOST_LEneeded above for setup.cpp
#define NV_ADIFFN_OLD_LEN       30
#define NV_ADIFFN_LEN           50
#define NV_I2CFN_LEN            30
#define NV_DXLOGIN_LEN          NV_CALLSIGN_LEN
#define NV_DXCLCMD_OLD_LEN      35
#define NV_DXCLCMD_LEN          60
#define NV_DXWLIST_LEN          50
#define NV_POTAWLIST1_OLD_LEN   26
#define NV_POTAWLIST_OLD_LEN    50
#define NV_SOTAWLIST1_OLD_LEN   26
#define NV_SOTAWLIST_OLD_LEN    50
#define NV_ADIFWLIST_LEN        50
#define NV_ONTAWLIST_LEN        50
#define NV_ONTAORG_LEN          30



// accessor functions
extern void NVWriteFloat (NV_Name e, float f);
extern void NVWriteUInt32 (NV_Name e, uint32_t u);
extern void NVWriteInt32 (NV_Name e, int32_t i);
extern void NVWriteUInt16 (NV_Name e, uint16_t u);
extern void NVWriteInt16 (NV_Name e, int16_t i);
extern void NVWriteUInt8 (NV_Name e, uint8_t u);
extern void NVWriteInt8 (NV_Name e, int8_t i);
extern void NVWriteString (NV_Name e, const char *str);
extern void NVWriteColorTable (int tbl_AB, const uint8_t r[N_CSPR], const uint8_t g[N_CSPR],
    const uint8_t b[N_CSPR]);
extern void NVWriteTZ (NV_Name e, const TZInfo &tz);
extern bool NVReadFloat (NV_Name e, float *fp);
extern bool NVReadUInt32 (NV_Name e, uint32_t *up);
extern bool NVReadInt32 (NV_Name e, int32_t *ip);
extern bool NVReadUInt16 (NV_Name e, uint16_t *up);
extern bool NVReadInt16 (NV_Name e, int16_t *ip);
extern bool NVReadUInt8 (NV_Name e, uint8_t *up);
extern bool NVReadInt8 (NV_Name e, int8_t *ip);
extern bool NVReadString (NV_Name e, char *buf);
extern bool NVReadColorTable (int tbl_AB, uint8_t r[N_CSPR], uint8_t g[N_CSPR], uint8_t b[N_CSPR]);
extern bool NVReadTZ (NV_Name e, TZInfo &tz);

#define NVTZ_AUTO 12345                 // NV_DE_TZ or NV_DX_TZ special value to mean auto_tz


extern void reportEESize (uint16_t &ee_used, uint16_t &ee_size);



/*********************************************************************************************
 *
 * ontheair.cpp
 *
 */

#define ONTA_INTERVAL   70                              // polling interval

extern bool updateOnTheAir (const SBox &box, bool fresh);
extern bool checkOnTheAirTouch (const SCoord &s, const SBox &box);
extern bool getOnTheAirSpots (DXSpot **spp, uint8_t *nspotsp);
extern void drawOnTheAirSpotsOnMap (void);
extern bool getClosestOnTheAirSpot (LatLong &ll, DXSpot *sp, LatLong *llp);
extern bool getOnTheAirPaneSpot (const SCoord &ms, DXSpot *dxs, LatLong *ll);
extern bool isONTARotating (void);




/*********************************************************************************************
 *
 * parsespot.cpp
 *
 */

extern bool crackClusterSpot (char line[], DXSpot &spot);
extern bool crackXMLSpot (const char xml[], DXSpot &spot);
extern bool crackADIFSpot (const char adif[], DXSpot &spot);
extern bool wsjtxIsStatusMsg (uint8_t **bpp);
extern bool wsjtxParseStatusMsg (uint8_t *msg, DXSpot &spot);








/*********************************************************************************************
 *
 * passwd.cpp
 *
 */

extern bool askPasswd (const char *category, bool restore);





/*********************************************************************************************
 *
 * plot.cpp
 *
 */

#define BMTRX_ROWS      24                              // time: UTC 0 .. 23
#define BMTRX_COLS      PROPBAND_N                      // PropMapBand bands: 80-40-30-20-17-15-12-10
typedef struct {
    bool ok;                                            // whether matrix is valid
    uint8_t m[BMTRX_ROWS][BMTRX_COLS];                  // percent circuit reliability as matrix of 24 rows
    time_t next_update;                                 // when next to retrieve
} BandCdtnMatrix;

extern bool installBMPBox (GenReader &gr, const SBox &box, ImageRefit fit, Message &ynot);
extern void plotBandConditions (const SBox &box, int busy, const BandCdtnMatrix *bmp, char *config_str);
extern bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
        const char *ylabel, uint16_t color, float y_min, float y_max, float big_value);
extern bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel,
        const char *ylabel, uint16_t color, float y_min, float y_max, char *label_str);
extern void plotWX (const SBox &b, uint16_t color, const WXInfo &wi);
extern void plotMessage (const SBox &b, uint16_t color, const char *message);
extern bool plotNOAASWx (const SBox &box);
extern void prepPlotBox (const SBox &box);





/*********************************************************************************************
 *
 * plotmap.cpp
 *
 */

extern void plotMapData (const char title[], const char x_label[], float x_data[], float y_data[],int n_data);
extern void plotServerFile (const char *filename, const char title[], const char x_label[]);






/*********************************************************************************************
 *
 * plotmgmnt.cpp
 *
 */


extern const SBox plot_b[PANE_N];          // box for each pane
extern PlotChoice plot_ch[PANE_N];         // current choice in each pane, or PLOT_CH_NONE for PANE_0
extern const char *plot_names[PLOT_CH_N];  // must be in same order as PlotChoice
extern uint32_t plot_rothold;              // bitmask of PlotChoice in rotset but temporarily holding
extern uint32_t plot_rotset[PANE_N];       // bitmask of each pane's PlotChoice rotation choices
                                           // N.B. plot_rotset[i] must always include plot_ch[i] unless NONE

#define ROTHOLD_SET(pc)         (plot_rothold |= (1<<(pc)))
#define ROTHOLD_CLR(pc)         (plot_rothold &= ~(1<<(pc)))
#define ROTHOLD_TST(pc)         ((plot_rothold & (1<<(pc))) != 0)

#define SHOWING_PANE_0()        (plot_ch[PANE_0] != PLOT_CH_NONE)
#define BOX_IS_PANE_0(b)        ((b).w == PLOTBOX0_W && (b).h == PLOTBOX0_H)

// bit mask of plot choices suitable for PANE_0
#define PANE_0_CH_MASK          ((1<<PLOT_CH_DXCLUSTER) | (1<<PLOT_CH_CONTESTS) | (1<<PLOT_CH_ADIF) \
                                 | (1<<PLOT_CH_ONTA) | (1<<PLOT_CH_DXPEDS))

// compute number of bits set in PANE_0_CH_MASK at compile time :-)
// https://stackoverflow.com/questions/109023/count-the-number-of-set-bits-in-a-32-bit-integer
// https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
#define NBITS_SET(v) ((((((((v) - (((v) >> 1) & 0x55555555)) & 0x33333333) + ((((v) - (((v) >> 1) & 0x55555555)) >> 2) & 0x33333333)) + (((((v) - (((v) >> 1) & 0x55555555)) & 0x33333333) + ((((v) - (((v) >> 1) & 0x55555555)) >> 2) & 0x33333333)) >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24)
#define N_PANE_0_CH             ((int)(NBITS_SET((uint32_t)PANE_0_CH_MASK)))

#define PLOT_ROTWARN_DT        4           // show rotation about to occur, secs

extern void insureCountdownPaneSensible(void);
extern bool checkPlotTouch (TouchType tt, const SCoord &s, PlotPane pp);
extern PlotPane findPaneForChoice (PlotChoice pc);
extern PlotPane findPaneChoiceNow (PlotChoice pc);
extern PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc);
extern PlotChoice getAnyAvailableChoice (void);
extern PlotChoice getAnyAvailablePane0Choice (void);
extern void forcePaneRotation (PlotPane pp);
extern bool plotChoiceIsAvailable (PlotChoice ch);
extern void logPaneRotSet (PlotPane pp, PlotChoice ch);
extern void logBRBRotSet(void);
extern void showRotatingBorder (void);
extern void initPlotPanes(void);
extern void savePlotOps(void);
extern int tickmarks (float min, float max, int numdiv, float ticks[]);
extern bool isPaneRotating (PlotPane pp);
extern bool isSpecialPaneRotating (PlotPane pp);
extern bool enforceCDownAlone (const SBox &box, uint32_t rotset);
extern void restoreNormPANE0(void);









/*********************************************************************************************
 *
 * prefixes.cpp
 *
 */

extern bool ll2Prefix (const LatLong &ll, char prefix[MAX_PREF_LEN]);
extern bool call2LL (const char *call, LatLong &ll);
extern bool call2DXCC (const char *call, int &dxcc);
extern void findCallPrefix (const char *call, char prefix[MAX_PREF_LEN]);
extern void splitCallSign (const char *call, char home_call[NV_CALLSIGN_LEN], char dx_call[NV_CALLSIGN_LEN]);








/*********************************************************************************************
 *
 * pskreporter.cpp
 *
 */


// all implementations share the following:

typedef enum {
    PSKMB_SRC0 = 1,                     // data source, see PSKIS/PSKSET
    PSKMB_CALL = 2,                     // using call, else grid
    PSKMB_OFDE = 4,                     // spot of DE, else by DE
    PSKMB_SRC1 = 8,                     // data source, see PSKIS/PSKSET
} PSKModeBits;

#define PSKMB_SRCMASK   (PSKMB_SRC0|PSKMB_SRC1)
#define PSKMB_PSK       (PSKMB_SRC0)
#define PSKMB_WSPR      (0)
#define PSKMB_RBN       (PSKMB_SRC1)
#define PSK_INTERVAL    (90)            // polling period. secs
#define PSK_DOTR        2               // end point marker radius for several paths, not just PSK

// current stats for each band
typedef struct {
    int count;                          // spots count
    float maxkm;                        // distance of farthest spot from DE, km
    LatLong maxll;                      // location of farthest spot
    char maxcall[MAX_SPOTCALL_LEN];     // call of farthest station
} PSKBandStats;

extern uint8_t psk_mask;                // bitmask of PSKModeBits
extern uint32_t psk_bands;              // bitmask of 1 << PSKBandSetting
extern uint16_t psk_maxage_mins;        // max age, minutes
extern uint8_t psk_showdist;            // show distances, else counts
extern uint8_t psk_showpath;            // whether to draw paths

extern bool updatePSKReporter (const SBox &box, bool force);
extern bool checkPSKTouch (const SCoord &s, const SBox &box);
extern void initPSKState(void);
extern void savePSKState(void);
extern void drawFarthestPSKSpots(void);
extern bool maxPSKageOk (int m);
extern uint16_t getBandColor (float kHz);
extern bool getBandPathDashed (float kHz);
extern int getRawBandPathWidth (float kHz);
extern int getRawBandSpotRadius (float kHz);
extern void drawPSKPaths (void);
extern void getPSKSpots (const DXSpot* &rp, int &n_rep);
extern bool getClosestPSK (LatLong &ll, DXSpot *sp, LatLong *mark_ll);
extern bool getMaxDistPSK (const SCoord &ms, DXSpot *sp, LatLong *mark_ll);



/*********************************************************************************************
 *
 * qrz.cpp
 *
 */

// N.B. WB0OEW will be replaced by desired call
#define QRZTABLE                                                                \
    X(QRZ_NONE,     "No",          NULL)                                        \
    X(QRZ_QRZ,      "qrz.com",     "https://www.qrz.com/db/WB0OEW")             \
    X(QRZ_HAMCALL,  "hamcall.net", "https://hamcall.net/call?callsign=WB0OEW")  \
    X(QRZ_CQQRZ,    "cqqrz.com",   "https://www.qrzcq.com/call/WB0OEW")

#define X(a,b,c)  a,                    // expands QRZTABLE to each enum and comma
typedef enum {
    QRZTABLE
    QRZ_N
} QRZURLId;
#undef X

typedef struct {
    const char *label;                  // menu label
    const char *url;                    // url with my call, or NULL
} QRZURLTable;

extern QRZURLTable qrz_urltable[QRZ_N];

extern void openQRZBio (const DXSpot &s);
extern void openURL (const char *url);
extern void openMovieURL (const char *hc_page, const char *orig_url);



/*********************************************************************************************
 *
 * radio.cpp
 *
 */

extern void pollRadio (void);
extern void setRadioSpot (float kHz);
extern void radioResetIO(void);




/*********************************************************************************************
 *
 * robinson.cpp
 *
 */

extern void ll2sRobinson (const LatLong &ll, SCoord &s, int edge, int scalesz);
extern bool s2llRobinson (const SCoord &s, LatLong &ll);
extern float RobLat2G (const float lat_d);




/*********************************************************************************************
 *
 * rss.cpp
 *
 */

extern SBox rss_bnr_b;                  // rss banner button
extern uint8_t rss_on;                  // rss on/off
extern bool rss_local;
extern void checkRSS(void);
extern void checkRSSTouch(void);





/*********************************************************************************************
 *
 * runner.cpp
 *
 */
extern const uint16_t runner[HC_RUNNER_W*HC_RUNNER_H] PROGMEM;




/*********************************************************************************************
 *
 * santa.cpp
 *
 */

extern void drawSanta(void);
extern void drawFireworks(void);
extern SBox santa_b;





/*********************************************************************************************
 *
 * sattool.cpp
 *
 */

extern void drawSatTool (void);






/*********************************************************************************************
 *
 * scrollbar.cpp
 *
 */

/* info and methods for a vertical scroll bar
 */
class ScrollBar {

    public:

        void init (int mv, int nd, SBox &b);
        bool checkTouch (char kb, SCoord &s);
        int getTop(void) {return (top_vis); };

    private:

        int max_vis;
        int top_vis;
        int n_data;
        SBox *sc_bp;                    // user's overall box
        SBox up_b, dw_b, trough_b;      // internal control surfaces

        bool okToScrollDown (void) { return (top_vis + max_vis < n_data); }
        bool okToScrollUp (void) {return (top_vis > 0); }
        void scrollUp();
        void scrollDown();
        void draw();

        bool canScrollUp(void);
        bool canScrollDw(void);

        const uint16_t fg = RA8875_WHITE;
        const uint16_t ARROW_GAP = 4;
        const uint16_t THUMB_GAP = 2;
};





/*********************************************************************************************
 *
 * scrollstate.cpp
 *
 */

/* info and methods to control scrolling with up/down buttons
 */
class ScrollState {

    public:

        typedef enum {
            DIR_TOPDOWN,                                // new items are added at the top
            DIR_BOTUP,                                  // new items are added at the bottom
            DIR_FROMSETUP,                              // as per scrollTopToBottom()
            DIR_UNDEF                                   // no yet defined
        } ScrollDir;

        void init (int mv, int tv, int nd, ScrollDir sd) {
            max_vis = mv;
            top_vis = tv;
            n_data = nd;
            dir = sd;
        };

        void drawScrollUpControl (const SBox &box, uint16_t arrow_color, uint16_t number_color) const;
        void drawScrollDownControl (const SBox &box, uint16_t arrow_color, uint16_t number_color) const;
        bool checkScrollUpTouch (const SCoord &s, const SBox &b) const;
        bool checkScrollDownTouch (const SCoord &s, const SBox &b) const;

        void initNewSpotsSymbol (const SBox &box, uint16_t color);
        void drawNewSpotsSymbol (bool draw, bool active) const;
        bool checkNewSpotsTouch (const SCoord &s, const SBox &b) const;

        void scrollDown (void);
        void scrollUp (void);
        bool okToScrollDown (void) const;
        bool okToScrollUp (void) const;
        bool atNewest (void) const;

        void scrollToNewest (void);
        bool findDataIndex (int display_row, int &array_index) const;
        int getVisDataIndices (int &min_i, int &max_i) const;
        int getDisplayRow (int array_index) const;

        int max_vis;        // maximum rows in the displayed list
        int top_vis;        // index into the data array being dislayed at the front of the list
        int n_data;         // the number of entries in the data array
        ScrollDir dir = DIR_UNDEF;

    private:

        void moveTowardsOlder();
        void moveTowardsNewer();
        int nMoreAbove (void) const;
        int nMoreBeneath (void) const;
        bool scrollT2B(void) const;
        SBox newsym_b;
        uint16_t newsym_color;
};




/*********************************************************************************************
 *
 * sdo.cpp
 *
 */

extern bool checkSDOTouch (const SCoord &s, const SBox &b);
extern bool updateSDOPane (const SBox &box);
extern bool isSDORotating(void);






/*********************************************************************************************
 *
 * selectFont.cpp
 *
 */


extern const GFXfont Germano_Regular16pt7b PROGMEM;
extern const GFXfont Germano_Bold16pt7b PROGMEM;
extern const GFXfont Germano_Bold30pt7b PROGMEM;

typedef enum {
    BOLD_FONT,
    LIGHT_FONT
} FontWeight;

typedef enum {
    FAST_FONT,
    SMALL_FONT,
    LARGE_FONT
} FontSize;

extern void selectFontStyle (FontWeight w, FontSize s);
extern void getFontStyle (FontWeight *wp, FontSize *sp);









/*********************************************************************************************
 *
 * setup.cpp
 *
 */

#define LABELSTYLES             \
    X(LBL_NONE,     "None")     \
    X(LBL_PREFIX,   "Prefix")   \
    X(LBL_CALL,     "Call")     \
    X(LBL_DOT,      "Dot")

#define X(a,b)  a,              // expands LABELSTYLES to each enum and comma
typedef enum {
    LABELSTYLES
    LBL_N
} LabelType;
#undef X

typedef enum {
    DF_MDY,
    DF_DMY,
    DF_YMD,
    DF_N
} DateFormat;

#define N_DXCLCMDS      12                      // n dx cluster user commands

#define RAWTHINPATHSZ   (tft.SCALESZ)           // thin raw path size
#define RAWWIDEPATHSZ   (5*RAWTHINPATHSZ/2)     // wide raw path size

#define FOLLOW_DT       (5*60*1000L)            // gpsd/nmea follow update interval, millis
#define FOLLOW_MIND     3                       // gpsd/nmea follow min motion dist, miles

extern void clockSetup(void);
extern const char *getWiFiSSID(void);
extern const char *getWiFiPW(void);
extern const char *getCallsign(void);
extern bool setCallsign (const char *cs);
extern const char *getDXClusterHost(void);
extern int getDXClusterPort(void);
extern bool setDXCluster (char *host, char *port_str, Message &ynot);
extern bool showTempC(void);
extern bool showATMhPa(void);
extern bool showDistKm(void);
extern bool useGeoIP(void);
extern bool useGPSDTime(void);
extern bool useGPSDLoc(void);
extern const char *getGPSDHost(void);
extern bool useNMEATime(void);
extern bool useNMEALoc(void);
extern const char *getNMEAFile(void);
extern const char *getNMEABaud(void);
extern float getBMETempCorr(int i);
extern float getBMEPresCorr(int i);
extern bool setBMETempCorr(BMEIndex i, float delta);
extern bool setBMEPresCorr(BMEIndex i, float delta);
extern bool useLocalNTPHost(void);
extern const char *getLocalNTPHost(void);
extern bool useDXCluster(void);
extern uint32_t getKX3Baud(void);
extern void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color);
extern bool logUsageOk(void);
extern uint16_t getMapColor (ColorSelection cid);
extern const char* getMapColorName (ColorSelection cid);
extern uint8_t getBrMax(void);
extern uint8_t getBrMin(void);
extern bool getX11FullScreen(void);
extern bool getWebFullScreen(void);
extern bool latSpecIsValid (const char *lng_spec, float &lng);
extern bool lngSpecIsValid (const char *lng_spec, float &lng);
extern bool getDemoMode(void);
extern int16_t getCenterLng(void);
extern DateFormat getDateFormat(void);
extern bool getRigctld (char host[NV_RIGHOST_LEN], int *portp);
extern bool getRotctld (char host[NV_ROTHOST_LEN], int *portp);
extern bool getFlrig (char host[NV_FLRIGHOST_LEN], int *portp);
extern const char *getDXClusterLogin(void);
extern int getRawPathWidth (ColorSelection id);
extern int getRawSpotRadius (ColorSelection id);
extern LabelType getSpotLabelType (void);
extern bool setMapColor (const char *name, uint16_t rgb565);
extern void getDXClCommands(const char *cmds[N_DXCLCMDS], bool on[N_DXCLCMDS]);
extern bool getPathDashed(ColorSelection id);
extern bool useMagBearing(void);
extern bool useWSJTX(void);
extern bool weekStartsOnMonday(void);
extern void formatLat (float lat_d, char s[], int s_len);
extern void formatLng (float lng_d, char s[], int s_len);
extern const char *getADIFilename(void);
extern void setADIFFilename (const char *fn);
extern bool scrollTopToBottom(void);
extern bool useOSTime (void);
extern bool showNewDXDEWx(void);
extern int getPaneRotationPeriod (void);
extern bool showPIP(void);
extern bool autoMap(void);
extern int getMapRotationPeriod(void);
extern GrayDpy_t getGrayDisplay(void);
extern bool setRadio (void);
extern bool UDPSetsDX(void);
extern bool useUDPSpot (const DXSpot &s);
extern bool autoUpgrade (int &at_hour);
extern int maxTLEAgeDays (void);
extern float minLabelDist(void);

extern void drawToolTipReminder(void);

// how to display a spot after checking whether it is on a watchlist
typedef enum {
    WLS_NORM,                                   // show call with normal colors
    WLS_HILITE,                                 // show call with highlighting colors
    WLS_NO                                      // do not show call at all
} WatchListShow;

// the individual watch list IDs
typedef enum {
    WLID_DX,
    WLID_ONTA,
    WLID_ADIF,
    WLID_N,
} WatchListId;

// watch list filtering states
#define _WATCH_DEFN             \
    X(WLA_OFF,  "Off")          \
    X(WLA_FLAG, "Red:")         \
    X(WLA_ONLY, "Only:")        \
    X(WLA_NOT,  "Not:")

#define X(a,b) a,                               // expands _WATCH_DEFN to each enum and comma
typedef enum {
    _WATCH_DEFN
    WLA_N
} WatchListState;
#undef X

#define WLA_NONE WLA_N                          // handy pseudonym 

#define WLA_MAXLEN      6                       // longest watch list filter state label, including EOS


extern void getWatchList (WatchListId wl, char **wlpp, size_t *wl_len);
extern void setWatchList (WatchListId wl, const char *new_state, char *new_wlstr);
extern void rotateWatchListState (struct _menu_text *tfp);
extern WatchListState getWatchListState (WatchListId wl, char name[WLA_MAXLEN]);
extern WatchListState lookupWatchListState (const char *wl_state);
extern const char *getWatchListName (WatchListId wl_id);
extern QRZURLId getQRZId (void);






/*********************************************************************************************
 *
 * sevenseg.cpp
 *
 */

extern void drawImgDigit (unsigned digit, uint8_t *img, const SBox &b, const uint8_t txt_clr[LIVE_BYPPIX]);
extern void drawImgNumber (unsigned n, uint8_t *img, SBox &b, const uint8_t txt_clr[LIVE_BYPPIX]);
extern void drawImgR (uint8_t *img, SBox b, const uint8_t txt_clr[LIVE_BYPPIX]);
extern void drawImgO (uint8_t *img, SBox b, const uint8_t txt_clr[LIVE_BYPPIX]);
extern void drawImgW (uint8_t *img, SBox b, const uint8_t txt_clr[LIVE_BYPPIX]);

extern void drawDigit (const SBox &b, int digit, uint16_t lt, uint16_t bg, uint16_t fg);




/*********************************************************************************************
 *
 * spacewx.cpp
 *
 */


// Bz Bt solar magnetic flux info, new data posted every few minutes
#define BZBT_INTERVAL           (180)                   // polling interval, secs
#define BZBT_BZCOLOR            RGB565(230,75,74)       // BZ plot color
#define BZBT_BTCOLOR            RGB565(100,100,200)     // BT plot color
#define BZBT_NV                 (6*25)                  // n lines to collect = 25 hours @ 10 mins per line


// solar wind info, new data posted every five minutes
#define SWIND_INTERVAL          (340)                   // polling interval, secs
#define SWIND_COLOR             RA8875_MAGENTA          // loading message text color
#define SWIND_DT                (10*60)                 // data interval, secs
#define SWIND_PER               (24*3600)               // data period, secs
#define SWIND_MAXN              (SWIND_PER/SWIND_DT)    // max expected SW values
#define SWIND_MINN              10                      // minimum require SW values


// sunspot info, new data posted daily
#define SSN_INTERVAL            (2400)                  // polling interval, secs
#define SSN_COLOR               RA8875_CYAN             // plot and history color
#define SSN_NV                  31                      // n ssn to plot, 1 per day back 30 days, including 0

// solar flux info, new data posted three times a day
#define SFLUX_NV                99                      // n solar flux values, three per day for 33 days
#define SFLUX_INTERVAL          (2300)                  // polling interval, secs
#define SFLUX_COLOR             RA8875_GREEN            // plot and history color


// DRAP plot info, new data posted every few minutes
// collect 24 hours of max value found in each 10 minute interval
#define DRAPDATA_INTERVAL       (10*60)                 // design interval, seconds
#define DRAPPLOT_INTERVAL       (DRAPMAP_INTERVAL+5)    // polling interval, secs. N.B. avoid race with MAP
#define DRAPDATA_PERIOD         (24*3600)               // total period, seconds
#define DRAPDATA_NPTS           (DRAPDATA_PERIOD/DRAPDATA_INTERVAL)     // number of points to download
#define DRAPPLOT_COLOR          RGB565(188,143,143)     // plotting color
#define DRAP_AUTOMAP_ON         25.0F                   // automap on threshold, MHz
#define DRAP_AUTOMAP_OFF        15.0F                   // automap off threshold, MHz


// kp historical and predicted info, new data posted every 3 hours
#define KP_INTERVAL             (2500)                  // polling period, secs
#define KP_COLOR                RA8875_YELLOW           // loading message text color
#define KP_VPD                  8                       // number of values per day
#define KP_NHD                  7                       // N historical days
#define KP_NPD                  2                       // N predicted days
#define KP_NV                   ((KP_NHD+KP_NPD)*KP_VPD)// N total Kp values


// xray info, new data posted every 10 minutes
#define XRAY_INTERVAL           (610)                   // polling interval, secs
#define XRAY_LCOLOR             RGB565(255,50,50)       // long wavelength plot color, reddish
#define XRAY_SCOLOR             RGB565(50,50,255)       // short wavelength plot color, blueish
#define XRAY_NV                 (6*25)                  // n lines to collect = 25 hours @ 10 mins per line


// space weather pane update intervals
#define NOAASPW_INTERVAL        (2400)                  // polling interval, secs
#define NOAASPW_COLOR           RGB565(154,205,210)     // plotting color

// aurora info
#define AURORA_INTERVAL         (1700)                  // interval, seconds
#define AURORA_COLOR            RGB565(100,200,150)     // plot color
#define AURORA_MAXPTS           (48)                    // every 30 minutes for 24 hours
#define AURORA_MAXAGE           (24.0F)                 // max age to plot, hours
#define AURORA_AUTOMAP_ON       50.0F                   // automap on threshold, percent
#define AURORA_AUTOMAP_OFF      25.0F                   // automap off threshold, percent

// DST info
#define DST_INTERVAL            (1900)                  // interval, seconds
#define DST_COLOR               RGB565(184,134,11)      // plot color
#define DST_NV                  (24)                    // every hour for 24 hours
#define DST_MAXAGE              (24.0F)                 // max age to plot, hours

// clouds info
#define CLOUDS_INTERVAL         2000                    // cloud map interval, seconds


/* consolidated space weather enum and stats. #define X to extract desired components.
 * N.B. max name chars NCDXF_B_MAXLEN-1
 * N.B. set rank of four [0,3] then set the rest to 9.
 */
#define SPCWX_DATA                                                        \
    X("SSN",      SPCWX_SSN,      PLOT_CH_SSN,     0, false, 9, 0, 1, 0)  \
    X("X-Ray",    SPCWX_XRAY,     PLOT_CH_XRAY,    0, false, 1, 0, 1, 0)  \
    X("SFI",      SPCWX_FLUX,     PLOT_CH_FLUX,    0, false, 0, 0, 1, 0)  \
    X("Kp",       SPCWX_KP,       PLOT_CH_KP,      0, false, 2, 0, 1, 0)  \
    X("Sol Wind", SPCWX_SOLWIND,  PLOT_CH_SOLWIND, 0, false, 9, 0, 1, 0)  \
    X("DRAP",     SPCWX_DRAP,     PLOT_CH_DRAP,    0, false, 9, 0, 1, 0)  \
    X("Bz",       SPCWX_BZ,       PLOT_CH_BZBT,    0, false, 3, 0, 1, 0)  \
    X("NOAA SpW", SPCWX_NOAASPW,  PLOT_CH_NOAASPW, 0, false, 9, 0, 1, 0)  /* value = max noaa_sw.val[] */ \
    X("Aurora",   SPCWX_AURORA,   PLOT_CH_AURORA,  0, false, 9, 0, 1, 0)  \
    X("DST",      SPCWX_DST,      PLOT_CH_DST,     0, false, 9, 0, 1, 0)

#define X(a,b,c,d,e,f,g,h,i) b,                 // expands SPCWX_DATA to each enum and comma
typedef enum {
    SPCWX_DATA
    SPCWX_N
} SPCWX_t;
#undef X

/* manage the display and sorting of space weather in NCDXF box
 */
typedef struct {
    const char *name;                           // printable name
    SPCWX_t sp;                                 // which one we are, needed for sorting
    PlotChoice pc;                              // corresponding plot choice if tapped
    float value;                                // current value
    bool value_ok;                              // whether value is valid
    int rank;                                   // display order after sorting for NCDXF, 0 is best
    float a, b, c;                              // ax^2 + bx + c to convert value when finding rank
} SpaceWeather_t;

extern SpaceWeather_t space_wx[SPCWX_N];

/* data and age for each type of SP
 */

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[BZBT_NV];                           // age, hrs
    float bz[BZBT_NV];                          // value
    float bt[BZBT_NV];                          // value
} BzBtData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[SWIND_MAXN];                        // age, hrs
    float y[SWIND_MAXN];                        // value
    int n_values;                               // may not be full
} SolarWindData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[SSN_NV];                            // age, days ago
    float ssn[SSN_NV];                          // value
} SunSpotData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[SFLUX_NV];                          // age, days ago
    float sflux[SFLUX_NV];                      // value
} SolarFluxData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[DRAPDATA_NPTS];                     // age, days ago
    float y[DRAPDATA_NPTS];                     // value
} DRAPData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[XRAY_NV];                           // age, days ago
    float l[XRAY_NV];                           // long xray value
    float s[XRAY_NV];                           // short xray value
} XRayData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float x[KP_NV];                             // age, days ago
    float p[KP_NV];                             // value
} KpData;

#define N_NOAASW_C      3                       // n categories : R, S and G
#define N_NOAASW_V      4                       // values per cat : current and 3 days predictions
typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    char cat[N_NOAASW_C];                       // categories R S G
    int val[N_NOAASW_C][N_NOAASW_V];            // each serverity code for each day
} NOAASpaceWxData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float age_hrs[AURORA_MAXPTS];               // negative age "hours ago", oldest first
    float percent[AURORA_MAXPTS];               // percent likely
    int n_points;                               // n points defined
} AuroraData;

typedef struct {
    time_t next_update;                         // when to try to get new data
    bool data_ok;                               // set when data are known good
    float age_hrs[DST_NV];                      // negative age "hours ago", oldest first
    float values[DST_NV];                       // values
    int n_points;                               // n points defined
} DSTData;

extern bool retrieveBzBt (BzBtData &bzbt);
extern bool retrieveSolarWind(SolarWindData &sw);
extern bool retrieveSunSpots (SunSpotData &ssn);
extern bool retrieveSolarFlux (SolarFluxData &sf);
extern bool retrieveDRAP (DRAPData &drap);
extern bool retrieveXRay (XRayData &xray);
extern bool retrieveKp (KpData &kp);
extern bool retrieveNOAASWx (NOAASpaceWxData &noaa);
extern bool retrieveAurora (AuroraData &a);
extern bool retrieveDST (DSTData &a);

extern void doNCDXFSpcWxTouch (const SCoord &s);
extern void drawNCDXFSpcWxStats(uint16_t color);
extern bool checkForNewSpaceWx(void);           // check for any new data or ...
extern bool checkForNewDRAP(void);              // ... a few specific ones
extern bool checkForNewAurora(void);            // ... a few specific ones
extern time_t nextRetrieval (PlotChoice pc, int interval);
extern void initSpaceWX(void);



/*********************************************************************************************
 *
 * sphere.cpp
 *
 */

extern void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp);




/*********************************************************************************************
 *
 * spots.cpp
 *
 */

#define MAXDUP_DT       (5*60)                  // 2 spots are dups if this close in time, secs
#define MAXDUP_DF       (0.1F)                  // 2 spots are dups if this close in frequency, kHz

typedef bool (*SpotFilter)(const DXSpot *sp);

extern bool getClosestSpot (DXSpot *list, int n_list, SpotFilter sfp, LabelOnMapEnd which_ends,
    LatLong &ll, DXSpot *sp, LatLong *llp);
extern void drawSpotLabelOnMap (DXSpot &spot, LabelOnMapEnd txrx, LabelOnMapDot dot);
extern void drawSpotPathOnMap (const DXSpot &spot);
extern void ditherLL (LatLong &ll);
extern void drawSpotDot (int16_t raw_x, int16_t raw_y, uint16_t radius, LabelOnMapEnd txrx, uint16_t color);
extern void drawVisibleSpots (WatchListId wl_id, const DXSpot *spots, const ScrollState &ss, const SBox &box,
    int16_t app_color);


typedef int (*PQSF)(const void *, const void *);        // pointer to qsort-style compare function

extern int qsDXCFreq (const void *v1, const void *v2);
extern int qsDXCRXGrid (const void *v1, const void *v2);
extern int qsDXCTXCall (const void *v1, const void *v2);
extern int qsDXCSpotted (const void *v1, const void *v2);
extern int qsDXCDist (const void *v1, const void *v2);

extern bool getPSKBandStats (PSKBandStats stats[HAMBAND_N], const char *names[HAMBAND_N]);








/*********************************************************************************************
 *
 * stopwatch.cpp
 *
 */

// bit mask values for NV_BCFLAGS
typedef enum {
    SW_BCDATEBIT =  (1<<0),                     // showing bigclock date
    SW_BCWXBIT   =  (1<<1),                     // showing bigclock weather
    SW_BCDIGBIT  =  (1<<2),                     // big clock is digital else analog
    SW_DB12HBIT  =  (1<<3),                     // digital clock is 12 else 24
    SW_NOSECBIT  =  (1<<4),                     // set if not showing seconds
    SW_UTCBIT    =  (1<<5),                     // set if Big Clock showing 24 hr UTC 
    SW_ANWDBIT   =  (1<<6),                     // set if analog clock also showing digital time
    SW_ANNUMBIT  =  (1<<7),                     // set if analog clock also shows hour numbers on face
    SW_BCSPWXBIT =  (1<<8),                     // showing bigclock space weather
    SW_ANCOLHBIT =  (1<<9),                     // color the analog hands
    SW_LSTBIT    =  (1<<10),                    // set if Big Clock showing 24 hr local sidereal time 
    SW_BCSATBIT  =  (1<<11),                    // set if BC potentially showing satellite up/down
} SWBCBits;

// state of stopwatch engine, _not_ what is being display
typedef enum {
    SWE_RESET,                                  // showing 0, ready to run
    SWE_RUN,                                    // running, can Stop or Lap
    SWE_STOP,                                   // holding time, can run or reset
    SWE_LAP,                                    // hold time, can resume or reset
    SWE_COUNTDOWN,                              // counting down
} SWEngineState;

// what stopwatch is displaying, _not_ the state of the engine
typedef enum {
    SWD_NONE,                                   // not displaying any part of stopwatch
    SWD_MAIN,                                   // basic stopwatch
    SWD_BCDIGITAL,                              // Big Clock, digital
    SWD_BCANALOG,                               // Big Clock, analog
} SWDisplayState;

// alarm state
typedef enum {
    ALMS_OFF,
    ALMS_ARMED,
    ALMS_RINGING
} AlarmState;

extern SBox stopwatch_b;                        // clock icon on main display

extern void initStopwatch(void);
extern void checkStopwatchTouch(void);
extern void checkCountdownTouch(void);
extern bool runStopwatch(void);
extern void drawMainPageStopwatch (bool force);
extern bool setSWEngineState (SWEngineState nsws, uint32_t ms);
extern SWEngineState getSWEngineState (uint32_t *sw_timer, uint32_t *cd_period);
extern SWDisplayState getSWDisplayState (void);
extern void getDailyAlarmState (AlarmState &as, uint16_t &de_hr, uint16_t &de_mn, bool &utc);
extern void setDailyAlarmState (const AlarmState &as, uint16_t de_hr, uint16_t de_mn, bool utc);
extern void getOneTimeAlarmState (AlarmState &as, time_t &t, bool &utc, char str[], size_t str_l);
extern void getOneTimeAlarmState (AlarmState &as, time_t &t, bool &utc);
extern bool setOneTimeAlarmState (AlarmState as, bool utc, const char *t_str, const char *title);
extern bool setOneTimeAlarmState (AlarmState as, bool utc, time_t t, const char *title);
extern SWBCBits getBigClockBits(void);
extern void SWresetIO(void);







/*********************************************************************************************
 *
 * string.cpp
 *
 */

extern uint32_t stringHash (const char *str);
extern char * strtolower (char *str);
extern char * strtoupper (char *str);
extern char *strTrimEnds (char *str);
extern char *strcompress (char *str);
extern void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp);
extern uint16_t getTextWidth (const char str[]);
extern bool expandENV (const char *fn, char *with_env, size_t with_len);
extern uint16_t maxStringW (char *str, uint16_t maxw);
extern const char *strcistr (const char *haystack, const char *needle);
extern int qsAString (const void *v1, const void *v2);
extern int qsDString (const void *v1, const void *v2);
extern void chompString (char *str);
extern int strtokens (char *str, char *tokens[], int max_tokens);
extern void quietStrncpy (char *to, const char *from, int len);
extern void formatSexa (float dt_hrs, int &a, char &sep, int &b);
extern char *formatAge (time_t age, char *line, int line_l, int cols);
extern bool strHasAlpha (const char *s);
extern bool strHasDigit (const char *s);
extern bool strHasPunct (const char *s);
extern bool strHasSpace (const char *s);
extern void strncpySubChar (char to_str[], const char from_str[], char to_char, char from_char, int to_len);








/*********************************************************************************************
 *
 * tooltip.cpp
 *
 */

void tooltip (const SCoord &s, const char *tip);





/*********************************************************************************************
 *
 * touch.cpp
 *
 */

extern void drainTouch(void);
extern TouchType readCalTouch (SCoord &s);
extern TouchType checkKBWarp (SCoord &s);

// for passing web touch command to checkTouch()
extern TouchType wifi_tt;
extern SCoord wifi_tt_s;



/*********************************************************************************************
 *
 * tz.cpp
 *
 */

extern int getFastTZ (const LatLong &ll);
extern int getFastTZStep (const LatLong &ll);
extern int getTZ (TZInfo &tz);
extern void setTZSecs (TZInfo &tz, int secs);
extern void setTZAuto (TZInfo &tz);





/*********************************************************************************************
 *
 * watchlist.cpp
 *
 */

extern char NOTADIFDXCC_KW[];
extern char NOTADIFPREF_KW[];
extern char NOTADIFBAND_KW[];
extern char NOTADIFPREF_KW[];

extern WatchListShow checkWatchListSpot (WatchListId wl_id, const DXSpot &dxsp);
extern bool compileWatchList (WatchListId wl_id, const char *new_wlstr, Message &ynot);
extern void setupWLMenuText (WatchListId wl_id, MenuText &mt, char state[WLA_MAXLEN]);
extern char *wlCompress (char *spec);
extern bool wlIdOk (WatchListId wl_id);








/*********************************************************************************************
 *
 * webserver.cpp
 *
 */

// handy tool to parse web command arguments
#define MAX_WEBARGS     10
typedef struct {
    const char *name[MAX_WEBARGS];              // name to look for
    const char *value[MAX_WEBARGS];             // ptr to its value, or NULL
    bool found[MAX_WEBARGS];                    // whether this name was found in the original GET command
    int nargs;
} WebArgs;
extern bool parseWebCommand (WebArgs &wa, char line[], size_t line_len);


extern void initWebServer(void);
extern void checkWebServer(bool ro);
extern TouchType readCalTouchWS (SCoord &s);
extern const char platform[];
extern void runNextDemoCommand(void);
extern bool bypass_pw;



/*********************************************************************************************
 *
 * wifi.cpp
 *
 */

// core map update intervals
#define DRAPMAP_INTERVAL        (300)                   // polling interval, secs
#define MUF_RT_INTERVAL         (900)                   // polling interval, secs
#define DEWX_INTERVAL           (1700)                  // polling interval, secs
#define DXWX_INTERVAL           (1600)                  // polling interval, secs
#define BC_INTERVAL             (3400)                  // polling interval, secs
#define VOACAP_INTERVAL         (3500)                  // polling interval, secs
#define OTHER_MAPS_INTERVAL     (1800)                  // polling interval, secs
#define ROTATION_INTERVAL       (getPaneRotationPeriod()) // handy pane auto rotation period in seconds





typedef struct {
    const char *server;                         // name of server
    int rsp_time;                               // last known response time, millis()
} NTPServer;
#define NTP_TOO_LONG 5000U                      // too long response time, millis()

extern void initSys (void);
extern void initWiFiRetry(void);
extern void scheduleNewPlot (PlotChoice ch);
extern void scheduleNewCoreMap (CoreMaps cm);
extern void updateWiFi(void);
extern bool checkBCTouch (const SCoord &s, const SBox &b);
extern void setPlotVisible (PlotChoice pc);
extern bool setPlotChoice (PlotPane new_pp, PlotChoice new_ch);
extern time_t getNTPUTC (NTPServer *);
extern void scheduleRSSNow(void);
extern bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll);
extern void sendUserAgent (WiFiClient &client);
extern void httpHCGET (WiFiClient &client, const char *server, const char *hc_page);
extern bool httpSkipHeader (WiFiClient &client);
extern bool httpSkipHeader (WiFiClient &client, const char *header, char *value, int value_len);
extern int getNTPServers (const NTPServer **listp);
extern bool setRSSTitle (const char *title, int &n_titles, int &max_titles);
extern time_t nextPaneRotation (PlotPane pp);
extern time_t nextWiFiRetry (PlotChoice pc);
extern time_t nextWiFiRetry (const char *str);
extern void scheduleFreshMap (void);
extern PlotPane ignorePaneTouch(void);
extern NTPServer *findBestNTP(void);


extern char remote_addr[16];
extern time_t next_update[PANE_N];
extern uint8_t rss_interval;


#define N_BCMODES       7                       // n voacap modes
typedef struct {
    const char *name;                           // mode such as CW, SSB, etc
    uint8_t value;                              // voacap sensitivity value
} BCModeSetting;
extern const BCModeSetting bc_modes[N_BCMODES];

extern uint8_t findBCModeValue (const char *name);
extern const char *findBCModeName (uint8_t value);
extern BandCdtnMatrix bc_matrix;
extern uint8_t bc_modevalue;
extern uint16_t bc_power;
extern float bc_toa;
extern uint8_t bc_utc_tl;
extern const int n_bc_powers;
extern uint16_t bc_powers[];
extern char *xrayLevel (char *buf, const SpaceWeather_t &xray);



/*********************************************************************************************
 *
 * wifimeter.cpp
 *
 */


#define MIN_WIFI_DBM            (-75)           // minimum acceptable signal strength, dBm
#define MIN_WIFI_PERCENT        (10)            // minimum acceptable signal strength, percent

extern void plotWiFiHistory(void);
extern bool readWiFiRSSI(int &rssi, bool &is_dbm);





/*********************************************************************************************
 *
 * wx.cpp
 *
 */

/* set possible wx stats to display in NCDXF_b.
 * N.B. put TEMP first because 1) most likely used and 2) short enough for DE/DX prefix (see drawNCDXFBoxWx())
 * N.B. max name chars NCDXF_B_MAXLEN-1
 */
#define WXSTATS                   \
    X(WXS_TEMP,     "Temp")       \
    X(WXS_HUM,      "Humidity")   \
    X(WXS_DEW,      "DewPoint")   \
    X(WXS_PRES,     "Pressure")   \
    X(WXS_WSPD,     "Wind Spd")   \
    X(WXS_WDIR,     "Wind Dir")

#define X(a,b) a,               // expands WXSTATS to each enum and comma
typedef enum {
    WXSTATS
    WXS_N
} WeatherStats;
#undef X


extern bool updateDEWX (const SBox &box);
extern bool updateDXWX (const SBox &box);
extern bool drawNCDXFWx (BRB_MODE m);
extern const WXInfo *findTZCache (const LatLong &ll, bool is_de, Message &ynot);
extern const WXInfo *findWXFast (const LatLong &ll);
extern bool getFastWx (const LatLong &ll, WXInfo &wi);
extern bool getCurrentWX (const LatLong &ll, bool is_de, WXInfo *wip, Message &ynot);
extern void doNCDXFWXTouch (BRB_MODE m);






/*********************************************************************************************
 *
 * zones.cpp
 *
 */

typedef enum {
    ZONE_CQ,
    ZONE_ITU
} ZoneID;

extern bool findZoneNumber (ZoneID id, const SCoord &s, int *zone_n);
extern void updateZoneSCoords(ZoneID id);
extern void drawZone (ZoneID id, uint16_t color, int n_only);



#endif // _HAMCLOCK_H
