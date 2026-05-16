/* Manage display brightness or just on/off if can't dim.
 *
 * User may adjust brightness manually using GUI slider or automatically via ESP phot sensor or I2C LTR329.
 * If can't dim, can still set on/off thresholds for phot/LTR329.
 *
 * N.B. pane controls must coordinate with NCDXF.
 *
 * ESP phot circuit:
 *
 *       +3.3V
 *         |
 *        330K
 *         |
 *         --- A0
 *         |
 *   photoresistor
 *         |
 *        Gnd
 *
 *
 * H/W Capability matrix:
 *   FB0 = _IS_RPI && _USE_FB0
 *   FS  = !IS_ESP && getX11FullScreen()
 *   DSI = _IS_RPI and display is DSI
 *
 *                         On/Off      Dimmable     PhotoR     LTR329
 *  _IS_ESP8266              Y            Y           Y          Y
 *  (FB0 || FS) && DSI       Y            Y           N          Y
 *  FB0 || FS                Y            N           N          Y
 *  else                     N            N           N          N
 *    
 * In addition to the photosensor on ESP all platforms support the LTR329 I2C light sensor, eg,
 * Adafruit https://www.adafruit.com/product/5591.
 */


#include "HamClock.h"

#include <Adafruit_LTR329_LTR303.h>
static Adafruit_LTR329 ltr = Adafruit_LTR329();

#if defined(_IS_UNIX)
#include <sys/wait.h>
#endif

bool found_phot;                                // set if either real phot or ltr329 discovered
bool found_ltr;                                 // set if ltr329 discovered

// NCDXF_b or "BRB" public state
uint8_t brb_mode;                               // one of BRB_MODE
uint16_t brb_rotset;                            // mask of current BRB_MODEs
time_t brb_next_update;                         // time of next update

#define X(a,b)  b,                              // expands BRBMODES to name plus comma
const char *brb_names[BRB_N] = {
     BRBMODES
};
#undef X

// configuration values
#define BPWM_MAX        255                     // PWM for 100% brightness
#define PHOT_PIN        A0                      // Arduino name of analog pin with photo detector
#define PHOT_MAX        1024                    // 10 bits including known range bug
#define NO_TOL          20                      // Adc tolerance for no phot attached
#define BPWM_BLEND      0.3F                    // fraction of new brightness to keep
#define PHOT_BLEND      0.5F                    // fraction of new pwm to keep
#define BPWM_COL        RA8875_WHITE            // brightness scale color
#define PHOT_COL        RA8875_CYAN             // phot scale color
#define BRIGHT_COL      RA8875_RED              // bright marker color  
#define DIM_COL         RA8875_RED              // dim marker color  
#define N_ROWS          11                      // rows of clock info, including gaps
#define SFONT_H         7                       // small font height
#define MARKER_H        3                       // scaler marker height
#define SCALE_W         5                       // scale width
#define PHOT_DT         300                     // read phot this often, ms; should be > LTR3XX_MEASRATE_200

static int16_t bpwm;                            // current brightness PWM value 0 .. BPWM_MAX
static uint16_t phot;                           // current photoresistor value 0 .. PHOT_MAX

// these are fast access shadows of what's is in NVRAM.
// they capture the user's taps that set the two points for linear brightness interp.
// N.B. maintain invariant that X_dim < X_bright
static uint16_t fast_phot_bright, fast_phot_dim;// measured brightness interp points
static uint16_t fast_bpwm_bright, fast_bpwm_dim;// corresponding commanded brightness interp points

// timers, idle and hw config
static uint16_t mins_on, mins_off;              // user's local on/off times, stored as hr*60 + min
static uint16_t idle_mins;                      // user's idle timeout period, minutes; 0 for none
static uint32_t idle_t0;                        // time of last user action, millis
static bool clock_off;                          // whether clock is forced off by timers
static uint8_t user_max, user_min;              // user's on and off brightness PWM value from setup
static bool support_onoff;                      // whether we support display on/off
static bool support_dim;                        // whether we support display fine brightness control

#if defined(_SUPPORT_DSI)
// RPi path to set DSI brightness, write 0 .. 255 string to control
static const char *dsi_paths[] = {
    "/sys/class/backlight/rpi_backlight/brightness",            // buster
    "/sys/class/backlight/10-0045/brightness",                  // bullseye
    "/sys/class/backlight/6-0045/brightness",                   // bookworm
};
#endif
static const char *dsi_path = "x";              // one of above if one works, non-null now just for lint

// forward references
static void engageDisplayBrightness(bool log);



/* set display brightness to bpwm.
 * on ESP we control backlight, RPi control displays, others systems ignored.
 */
static void setDisplayBrightness(bool log)
{
    // lint
    (void) dsi_path;

    #if defined(_IS_ESP8266)

        if (log)
            Serial.printf ("BR: setting bpwm %d\n", bpwm);

        // ESP: control backlight
        tft.PWM1out(bpwm);

    #else

        if (support_dim) {

            // control DSI backlight
            // could use 'vcgencmd set_backlight x' but still need a way to check whether bl is controllable
            int dsifd = open (dsi_path, O_WRONLY);
            if (dsifd < 0) {
                Serial.printf ("BR: %s: %s\n", dsi_path, strerror(errno));
            } else {
                if (log)
                    Serial.printf ("BR: setting bpwm %d\n", bpwm);
                FILE *dsifp = fdopen (dsifd, "w");
                fprintf (dsifp, "%d\n", bpwm);
                fclose (dsifp); // also closes dsifd
            }

        } else if (support_onoff) {

            // only hammer on system if changing
            static bool been_here;
            static bool was_on;
            bool want_on = bpwm > fast_bpwm_dim;

            if (!been_here || want_on != was_on) {

                #if defined(_IS_APPLE)

                    static const char apple_on[] = "caffeinate -u -t 1";
                    static const char apple_off[] = "pmset displaysleepnow";
                    const char *apple_cmd = want_on ? apple_on : apple_off;
                    system (apple_cmd);
                    if (log)
                        Serial.printf ("BR: bpwm %d cmd: %s\n", bpwm, apple_cmd);

                #else 

                    if (log)
                        Serial.printf ("BR: bpwm %d turning %s\n", bpwm, want_on ? "on" : "off");

                    // try lots of ways to control HDMI on or off
                    // N.B. can't use system() because we need to retain suid root
                    // N.B. only do the first 3 once
                    // N.B. only log the potentially verbose outputs the first time for the record

                    typedef const char *argv_t[10];
                    argv_t argvs[] = {
                        { "xset", "dpms", "0", "0", "0", NULL},         // no blanking
                        { "xset", "s", "0", "0", NULL},                 // no screen saver
                        { "xset", "s", "blank", "0", NULL},             // blank with hardware
                        { "vcgencmd", "display_power", want_on ? "1" : "0", NULL},
                        { "xset", "dpms", "force", want_on ? "on" : "off", NULL},
                        { "wlr-randr", "--output", "HDMI-A-1", want_on ? "--on" : "--off", NULL},
                        { NULL }
                    };

                    // needed if running automatically
                    setenv ("WAYLAND_DISPLAY", "wayland-1", 0);

                    for (int i = been_here ? 3 : 0; i < NARRAY(argvs); i++) {
                        const char **argv = argvs[i];
                        if (!been_here) {
                            char lm[200];
                            int lm_l = snprintf (lm, sizeof(lm), "BR: bpwm %d cmd: ", bpwm);
                            for (const char **ap = argv; *ap != NULL; ap++)
                                lm_l += snprintf (lm+lm_l, sizeof(lm)-lm_l, " %s", *ap);
                            Serial.println (lm);
                        }
                        int child_pid = fork();
                        if (child_pid < 0)
                            fatalError ("fork() failed: %s", strerror(errno));
                        if (child_pid == 0) {
                            // new child process, quiet except first time
                            if (been_here) {
                                int fd_null = open ("/dev/null", O_WRONLY);
                                dup2 (fd_null, 1);
                                dup2 (fd_null, 2);
                                close (fd_null);
                            }
                            execvp (argv[0], (char**)argv);
                            Serial.printf ("BR: execvp(%s) failed: %s\n", argv[0], strerror(errno));
                            exit(1);
                        } else {
                            // parent waits for child
                            int ws;
                            (void) wait (&ws);
                        }
                    }
                #endif

                // persist
                was_on = want_on;
                been_here = true;
            }
        }

    #endif
}

/* return current photo detector value, range [0..PHOT_MAX] increasing with brightness.
 * also set found_phot or found_ltr on first call.
 * try I2C else photocell if possible
 */
static uint16_t readPhot()
{
    // set after init attempts
    static bool tried_ltr;

    // value we will return. persistent for when no new data available and/or smoothing
    static uint16_t new_phot;

    // try LTR on I2C first

    if (!tried_ltr) {

        // try to init
        if (!ltr.begin()) {
            Serial.println ("BR: No LTR329");
            found_phot = found_ltr = false;
        } else {
            ltr.setGain(LTR3XX_GAIN_8);
            ltr.setIntegrationTime(LTR3XX_INTEGTIME_100);
            ltr.setMeasurementRate(LTR3XX_MEASRATE_200);
            found_phot = found_ltr = true;
            Serial.println ("BR: found LTR329");
        }

        // only one try
        tried_ltr = true;

    }

    if (found_ltr) {
        uint16_t ch0, ch1;
        if (ltr.readBothChannels(ch0, ch1)) {
            new_phot = ch0;
            if (new_phot > PHOT_MAX)
                new_phot = PHOT_MAX;
        }
    }

    // Serial.printf ("Phot %d\n", new_phot);                                         // RBF
    return (new_phot);

}


/* get dimensions of the phot slider control
 */
static void getPhotControl (SBox &b)
{
        // N.B. match getBrControl()
        b.w = SCALE_W;
        b.y = NCDXF_b.y + NCDXF_b.h/9;
        b.h = 6*NCDXF_b.h/10;

        // right third
        b.x = NCDXF_b.x + 2*(NCDXF_b.w - SCALE_W)/3;
}



/* draw a symbol for the photresistor in NCDXF_b.
 * skip if stopwatch is up.
 */
static void drawPhotSymbol()
{
        if (!mainpage_up)
            return;

        uint8_t n = 2;                                          // number of \/
        uint16_t w = 2*n+8;                                     // n steps across
        uint16_t s = NCDXF_b.w/w;                               // 1 x step length
        uint16_t x = NCDXF_b.x + (NCDXF_b.w-w*s)/2 + 2*s;       // initial x to center
        uint16_t y = NCDXF_b.y + NCDXF_b.h - 3*s;               // y center-line

        // lead in from left then up
        tft.drawLine (x, y, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y-s, PHOT_COL);
        x += s;

        // draw n \/
        for (uint8_t i = 0; i < n; i++) {
            tft.drawLine (x, y-s, x+s, y+s, PHOT_COL);
            x += s;
            tft.drawLine (x, y+s, x+s, y-s, PHOT_COL);
            x += s;
        }

        // down then lead out to right
        tft.drawLine (x, y-s, x+s, y, PHOT_COL);
        x += s;
        tft.drawLine (x, y, x+s, y, PHOT_COL);

        // incoming light arrows
        uint16_t ax = NCDXF_b.x + 6*s;                          // arrow head location

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL); // main shaft
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL); // lower shaft
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL); // upper shaft 

        ax += 2*s;                                              // move to second arrow head and repeat

        tft.drawLine (ax, y-2*s, ax-1*s,   y-3*s,    PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/4, y-19*s/8, PHOT_COL);
        tft.drawLine (ax, y-2*s, ax-3*s/8, y-11*s/4, PHOT_COL);
}

/* draw a scale useful for either brightness or phot in the given box.
 * show a marker MARKER_H high with top at val_y, and lines at bright/dim_y unless 0
 */
static void drawScale (SBox &b, uint16_t val_y, uint16_t bright_y, uint16_t dim_y, uint16_t color)
{

        // fresh box
        #if defined(_IS_ESP8266)
            tft.fillRect (b.x+1, b.y+1, b.w-1, b.h-1, RA8875_BLACK);    // leave border to avoid flicker
        #else
            fillSBox (b, RA8875_BLACK);
        #endif
        drawSBox (b, color);

        // draw value
        tft.fillRect (b.x, val_y, b.w-1, MARKER_H, color);

        // overlay markers unless 0
        if (bright_y && dim_y) {
            tft.fillRect (b.x, bright_y, b.w, 1, BRIGHT_COL);   // fits rect better at higher res
            tft.fillRect (b.x, dim_y, b.w, 1, DIM_COL);
        }
}

/* draw phot control.
 * skip if stopwatch is up.
 */
static void drawPhotControl()
{
        if (!mainpage_up)
            return;

        SBox b;
        getPhotControl (b);

        // phot marker
        uint16_t val_y = b.y + b.h - ((b.h-2-MARKER_H)*phot/PHOT_MAX + MARKER_H + 1);

        // overlay phot set points, avoid top and bottom
        uint16_t bright_y = b.y + b.h - ((b.h-2)*fast_phot_bright/PHOT_MAX + 1);
        uint16_t dim_y = b.y + b.h - ((b.h-2)*fast_phot_dim/PHOT_MAX + 1);

        drawScale (b, val_y, bright_y, dim_y, PHOT_COL);
}


/* get dimensions of the brightness slider control, depends on whether we alone or with phot scale.
 */
static void getBrControl (SBox &b)
{
        // N.B. match getPhotControl()
        b.w = SCALE_W;
        b.y = NCDXF_b.y + NCDXF_b.h/9;
        b.h = 6*NCDXF_b.h/10;

        // x depends on mode
        if (brb_mode == BRB_SHOW_PHOT)
            b.x = NCDXF_b.x + (NCDXF_b.w - SCALE_W)/3;
        else
            b.x = NCDXF_b.x + (NCDXF_b.w - SCALE_W)/2;
}


/* draw current brightness control.
 * skip if stopwatch is up.
 */
static void drawBrControl()
{
        if (!mainpage_up)
            return;

        SBox b;
        getBrControl (b);

        if (support_dim) {

            int user_range = user_max - user_min;

            // brightness marker
            // N.B. must be drawn [user_min,user_max] to match changeBrightness()
            uint16_t val_y = b.y + b.h - ((b.h-2-MARKER_H)*(bpwm-user_min)/user_range + MARKER_H + 1);

            // overlay bpwm set points if showing, avoid top and bottom
            uint16_t bright_y = 0, dim_y = 0;
            if (brb_mode == BRB_SHOW_PHOT) {
                bright_y = b.y + b.h - ((b.h-2)*(fast_bpwm_bright-user_min)/user_range + 1);
                dim_y = b.y + b.h - ((b.h-2)*(fast_bpwm_dim-user_min)/user_range + 1);
            }

            drawScale (b, val_y, bright_y, dim_y, BPWM_COL);

        } else {

            // brightness is not adjustable so just show an indication of whether on or off
            uint16_t val_y;
            if (bpwm > fast_bpwm_dim) {
                // top
                val_y = b.y + 1;
            } else {
                // bottom
                val_y = b.y + b.h - (MARKER_H + 1);
            }

            drawScale (b, val_y, b.y, b.y+b.h-2, BPWM_COL);
        }
}

/* draw mins_on/mins_off and idle controls.
 * skip if stopwatch is up or not in proper mode.
 * N.B. coordinate layout with changeOnOffSetting().
 */
static void drawOnOffControls()
{
        if (brb_mode != BRB_SHOW_ONOFF || !mainpage_up)
            return;

        tft.fillRect (NCDXF_b.x+1, NCDXF_b.y+1, NCDXF_b.w-2, NCDXF_b.h-2, RA8875_BLACK);
        tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y, GRAY);
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (RA8875_WHITE);

        // left x values
        uint16_t xl = NCDXF_b.x + 7;               // label indent
        uint16_t xn = NCDXF_b.x + 12;              // number indent

        // title
        tft.setCursor (xl, NCDXF_b.y+2);
        tft.print ("Display");

        // walk down by dy each time
        uint8_t dy = NCDXF_b.h/N_ROWS;
        uint16_t y = NCDXF_b.y + dy - SFONT_H/2;

        // gap
        y += dy;

        // idle
        tft.setCursor (xl-3, y+=dy);
        tft.print ("Idle in:");
        tft.setCursor (xn-3, y+=dy);
        tft.print (idle_mins);
        tft.print (" min");

        // gap
        y += dy;

        // time on
        tft.setCursor (xl, y+=dy);
        tft.print ("On at:");
        tft.setCursor (xn, y+=dy);
        int hr_on = mins_on/60;
        int mn_on = mins_on%60;
        if (hr_on < 10)
            tft.print('0');
        tft.print(hr_on);
        tft.print(':');
        if (mn_on < 10)
            tft.print('0');
        tft.print(mn_on);

        // gap
        y += dy;

        // time off
        tft.setCursor (xl, y+=dy);
        if (support_dim)
            tft.print ("Dim at:");
        else
            tft.print ("Off at:");
        tft.setCursor (xn, y+=dy);
        int hr_off = mins_off/60;
        int mn_off = mins_off%60;
        if (hr_off < 10)
            tft.print('0');
        tft.print(hr_off);
        tft.print(':');
        if (mn_off < 10)
            tft.print('0');
        tft.print(mn_off);
}


/* save on_mins and off_mins for the given dow 1..7 Sun..Sat within persistent storage NV_DAILYONOFF.
 * N.B. we do not validate dow
 */
static void persistOnOffTimes (int dow, uint16_t on_mins, uint16_t off_mins)
{
    // internally we want 0-based
    dow -= 1;

    // retrieve current values
    uint16_t ootimes[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)ootimes);

    // merge
    ootimes[dow] = on_mins;
    ootimes[dow+DAYSPERWEEK] = off_mins;

    // save
    NVWriteString (NV_DAILYONOFF, (char*)ootimes);
}

/* get on/off from presistent storage NV_DAILYONOFF for the given week day 1..7 Sun..Sat
 * N.B. we do not validate dow
 */
static void getPersistentOnOffTimes (int dow, uint16_t &on, uint16_t &off)
{
    // internally we require 0..6
    dow -= 1;

    uint16_t ootimes[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)ootimes);
    on = ootimes[dow];
    off = ootimes[dow+DAYSPERWEEK];
}

/* given screen tap location known to be within NCDXF_b, allow user to change on/off/idle setting
 * N.B. coordinate layout with drawOnOffControls().
 */
static void changeOnOffSetting (const SCoord &s)
{
        // decide which row and which left-right half were tapped
        int dy = NCDXF_b.h/N_ROWS;
        int row = (s.y - NCDXF_b.y + dy/2 - SFONT_H/2)/dy;          // center of rows in drawOnOffControls
        bool left_half = s.x - NCDXF_b.x < NCDXF_b.w/2;

        // minutes deltas, always forward
        uint16_t on_dt = 0, off_dt = 0;

        switch (row) {
        case 3:
            // increase idle time
            idle_mins += 5;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            break;

        case 4:
            // decrease idle time but never below 0
            if (idle_mins > 0) {
                idle_mins = idle_mins < 5 ? 0 : idle_mins - 5;
                NVWriteUInt16 (NV_BR_IDLE, idle_mins);
            }
            break;

        case 6:
            if (left_half) {
                // increase on-time one hour
                on_dt = 60;
            } else {
                // increase on-time 5 minutes
                on_dt = 5;
            }
            break;

        case 7:
            if (left_half) {
                // decrease on-time one hour
                on_dt = MINSPERDAY - 60;
            } else {
                // decrease on-time 5 minutes
                on_dt = MINSPERDAY - 5;
            }
            break;

        case 9:
            if (left_half) {
                // increase off-time one hour
                off_dt = 60;
            } else {
                // increase off-time 5 minutes
                off_dt = 5;
            }
            break;

        case 10:
            if (left_half) {
                // decrease off-time one hour
                off_dt = MINSPERDAY - 60;
            } else {
                // decrease off-time 5 minutes
                off_dt = MINSPERDAY - 5;
            }
            break;

        default:
            return; 
        }

        if (on_dt || off_dt) {
            // update and save new values
            mins_on = (mins_on + on_dt) % MINSPERDAY;
            mins_off = (mins_off + off_dt) % MINSPERDAY;
            persistOnOffTimes (DEWeekday(), mins_on, mins_off);
        }

        // redraw with new settings
        drawOnOffControls();
}



/* check whether it is time to turn display on or off from the timers or idle timeout.
 * check idle timeout first, then honor on/off settings
 * N.B. we maintain sync with the weekly NV_DAILYONOFF and redraw controls if brb_mode == BRB_SHOW_ONOFF
 */
static void checkOnOffTimers()
{
        // check idle timeout first, if enabled
        if (idle_mins > 0) {
            uint16_t ims = (millis() - idle_t0)/60000;   // ms -> mins
            if (ims >= idle_mins && !clock_off) {
                Serial.println ("BR: Idle timed out");
                bpwm = user_min;
                clock_off = false;
                engageDisplayBrightness(true);
                clock_off = true;
            }
        }

        // update on off times whenever DE's week day changes
        static int prev_dow = -1;
        int dow = DEWeekday();
        if (dow != prev_dow) {
            getPersistentOnOffTimes (dow, mins_on, mins_off);
            if (brb_mode == BRB_SHOW_ONOFF)
                drawOnOffControls();
            prev_dow = dow;
        }

        // ignore if on/off are the same
        if (mins_on == mins_off)
            return;

        // only check on/off times at top of each minute
        static time_t check_mins;
        time_t utc = nowWO();
        time_t utc_mins = utc/60;
        if (utc_mins == check_mins)
            return;
        check_mins = utc_mins;

        // check for time to turn on or off.
        // get local time
        time_t local = utc + getTZ (de_tz);
        int hr = hour (local);
        int mn = minute (local);
        uint16_t mins_now = hr*60 + mn;

        // Serial.printf("idle %d now %d on %d off %d,bpwm %d\n",idle_mins,mins_now,mins_on,mins_off,bpwm);

        // engage when its time
        if (mins_now == mins_on) {
            if (bpwm != user_max) {
                Serial.println ("BR: on");
                bpwm = user_max;
                clock_off = false;
                engageDisplayBrightness(true);
                idle_t0 = millis();             // consider this a user action else will turn off again
            }
        } else if (mins_now == mins_off) {
            if (bpwm != user_min) {
                Serial.println ("BR: off");
                bpwm = user_min;
                clock_off = false;              // just to be sure engage works
                engageDisplayBrightness(true);
                clock_off = true;
            }
        }
}



/* set brightness to bpwm and update GUI controls if visible.
 * N.B. beware states that should not be drawn
 * N.B. we do not change display if clock_off so beware calling order when changing it
 */
static void engageDisplayBrightness(bool log)
{
        if (!clock_off)
            setDisplayBrightness(log);

        // Serial.printf ("BR: engage mode %d\n", brb_mode);

        if (mainpage_up) {
            if (brb_mode == BRB_SHOW_BR)
                drawBrControl();
            else if (brb_mode == BRB_SHOW_PHOT) {
                drawBrControl();
                drawPhotControl();
            }
        }
}


#if defined(_SUPPORT_DSI)

/* return whether this is a linux RPi connected to a DSI display
 */
static bool isRPiDSI()
{
        static bool know;


        if (!know) {

            // try all
            dsi_path = NULL;
            for (int i = 0; i < NARRAY(dsi_paths); i++) {
                int dsifd = open (dsi_paths[i], O_WRONLY);
                if (dsifd >= 0) {
                    dsi_path = dsi_paths[i];
                    close (dsifd);
                    break;
                }
            }

            // report
            if (dsi_path)
                Serial.printf ("BR: found DSI display at %s\n", dsi_path);
            else
                Serial.print ("BR: no DSI display\n");

            // don't have to test again
            know = true;
        }

        return (dsi_path != NULL);
}

#endif


#if !defined(_WEB_ONLY) && !defined(_USE_FB0) && (defined(_IS_UNIX) || defined(_IS_LINUX_RPI))
/* try to determine whether an X connection is local or remote.
 */
static bool localX()
{
    const char *display = getenv ("DISPLAY");
    return (!display || strstr (display, ":0") != NULL);
}
#endif


/* return whether the display hardware brightness can be controlled.
 * function is intended for external use, use flags for internal use.
 */
bool brDimmableOk()
{
        #if defined(_WEB_ONLY)
            support_dim = false;                                // never via web
        #elif defined(_IS_ESP8266)
            support_dim = true;                                 // always works
        #elif defined(_USE_FB0)
            support_dim = isRPiDSI();                           // only if DSI
        #elif defined(_IS_LINUX_RPI)
            support_dim = getX11FullScreen() && localX() && isRPiDSI();     // only local DSI full screen
        #else
            support_dim = false;
        #endif

        return (support_dim);
}

/* return whether display hardware supports being turned on/off.
 * function is intended for external use, use flags for internal use.
 */
bool brOnOffOk()
{
        #if defined(_WEB_ONLY)
            support_onoff = false;                              // never via web
        #elif defined(_IS_ESP8266)
            support_onoff = true;                               // always works
        #elif defined(_USE_FB0)
            support_onoff = true;                               // always works
        #elif defined(_IS_UNIX)
            support_onoff = getX11FullScreen() && localX();     // works iff full screen on local display
        #else
            support_onoff = false;
        #endif

        return (support_onoff);
}

/* call this ONCE before Setup to determine hardware and set full brightness for now,
 * then call setupBrightness() ONCE after Setup to commence with user's brightness settings.
 */
void initBrightness()
{
        // once
        static bool before;
        if (before)
            return;
        before = true;

        // determine initial hw capabilities, might change depending on Setup
        (void) brDimmableOk();
        (void) brOnOffOk();

        // log
        Serial.printf ("BR: A onoff= %d dim= %d\n", support_onoff, support_dim);

        // full on for now
        bpwm = BPWM_MAX;
        setDisplayBrightness(true);
}

/* call this ONCE after Setup to commence with user's brightness controls and on/off times.
 */
void setupBrightness()
{
        // once
        static bool before;
        if (before)
            return;
        before = true;

        // final check of hw capabilities after Setup.
        (void) brDimmableOk();
        (void) brOnOffOk();

        // check whether photo sensor is connected
        phot = readPhot();
        Serial.printf ("BR: phot %d %s\n", phot, found_phot ? "found" : "not found");

        // init to user's full on and off brightness settings
        user_max = (getBrMax()*BPWM_MAX+50)/100;         // round
        user_min = (getBrMin()*BPWM_MAX+50)/100;
        bpwm = user_max;
        setDisplayBrightness(true);
        clock_off = false;

        // log
        Serial.printf ("BR: B %d .. %d  onoff= %d dim= %d\n",
                        user_min, user_max, support_onoff, support_dim);

        // init idle time and period
        idle_t0 = millis();
        if (!NVReadUInt16 (NV_BR_IDLE, &idle_mins)) {
            idle_mins = 0;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
        }

        // retrieve fast copies, init if first time, honor user settings

        if (!NVReadUInt16 (NV_BPWM_BRIGHT, &fast_bpwm_bright) || fast_bpwm_bright > user_max)
            fast_bpwm_bright = user_max;
        if (!NVReadUInt16 (NV_BPWM_DIM, &fast_bpwm_dim) || fast_bpwm_dim < user_min)
            fast_bpwm_dim = user_min;
        if (fast_bpwm_bright <= fast_bpwm_dim) {
            // new user range is completely outside 
            fast_bpwm_bright = user_max;
            fast_bpwm_dim = user_min;
        }
        NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
        NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);

        if (!NVReadUInt16 (NV_PHOT_BRIGHT, &fast_phot_bright)) {
            fast_phot_bright = PHOT_MAX;
            NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);
        }
        if (!NVReadUInt16 (NV_PHOT_DIM, &fast_phot_dim)) {
            fast_phot_dim = 0;
            NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
        }

        // check display mode
        if ((brb_rotset & (1 << BRB_SHOW_ONOFF)) && !support_onoff) {
            Serial.print ("BR: Removing BRB_SHOW_ONOFF from brb_rotset\n");
            brb_rotset &= ~(1 << BRB_SHOW_ONOFF);
            checkBRBRotset();
        }
        if ((brb_rotset & (1 << BRB_SHOW_PHOT)) && (!found_phot || (!support_onoff && !support_dim))) {
            Serial.print ("BR: Removing BRB_SHOW_PHOT from brb_rotset\n");
            brb_rotset &= ~(1 << BRB_SHOW_PHOT);
            checkBRBRotset();
        }
        if ((brb_rotset & (1 << BRB_SHOW_BR)) && (!support_dim || found_phot)) {
            Serial.print ("BR: Removing BRB_SHOW_BR from brb_rotset\n");
            brb_rotset &= ~(1 << BRB_SHOW_BR);
            checkBRBRotset();
        }
}

/* draw any of the brightness controls in NCDXF_b.
 */
void drawBrightness()
{
        switch (brb_mode) {

        case BRB_SHOW_ONOFF:
            drawOnOffControls();
            break;

        case BRB_SHOW_PHOT:
            drawBrControl();
            drawPhotControl();
            drawPhotSymbol();
            break;

        case BRB_SHOW_BR:
            drawBrControl();
            break;

        default:
            break;
        }
}


/* set display brightness according to current photo detector and/or clock settings
 */
void followBrightness()
{
        // not too fast (eg, while not updating map after new DE)
        static uint32_t follow_ms;
        if (!timesUp (&follow_ms, PHOT_DT))
            return;

        // always check on/off first
        if (support_onoff)
            checkOnOffTimers();

        // that's it if there is no light sensor or there is no control ability
        if (!found_phot || (!support_dim && !support_onoff))
            return;

        // fresh
        phot = readPhot();

        // update brightness from new phot reading using linear interpolation between limits.
        float del_phot = phot - fast_phot_dim;
        float bpwm_range = fast_bpwm_bright - fast_bpwm_dim;
        float phot_range = fast_phot_bright - fast_phot_dim;
        if (phot_range == 0)
            phot_range = 1;         // avoid /0
        float new_bpwm = fast_bpwm_dim + bpwm_range * del_phot / phot_range;
        if (new_bpwm < 0)
            new_bpwm = 0;
        else if (new_bpwm > BPWM_MAX)
            new_bpwm = BPWM_MAX;

        // smooth update and one final range check
        bpwm = BPWM_BLEND*new_bpwm + (1-BPWM_BLEND)*bpwm + 0.5F;
        if (bpwm < user_min)
            bpwm = user_min;
        if (bpwm > user_max)
            bpwm = user_max;

        // engage
        engageDisplayBrightness(false);
}

/* called on any tap anywhere to insure screen is on and reset idle_t0.
 * return whether we were off prior to tap.
 */
bool brightnessOn()
{
        idle_t0 = millis();

        if (clock_off) {
            Serial.println ("BR: display commanded on");
            bpwm = user_max;
            clock_off = false;
            engageDisplayBrightness(true);
            return (true);
        } else
            return (false);
}

/* turn screen off.
 */
void brightnessOff()
{
        Serial.println ("BR: display commanded off");
        bpwm = user_min;
        clock_off = false;              // just to be sure engage works
        engageDisplayBrightness(true);
        clock_off = true;
}

/* given a tap within NCDXF_b, change brightness or clock setting
 * N.B. scale assumed be drawn [user_min,user_max] in drawBrControl()
 * N.B. maintain invariant that X_dim < X_bright
 */
static void changeBrightness (const SCoord &s)
{
        if (brb_mode == BRB_SHOW_PHOT) {

            SBox b;
            getBrControl (b);

            // set brightness directly from tap location within allowed range
            if (s.y < b.y)
                bpwm = user_max;
            else if (s.y > b.y + b.h)
                bpwm = user_min;
            else
                bpwm = user_max - (user_max-user_min)*(s.y - b.y)/b.h;

            if (s.y < b.y + b.h/2) {

                // set new bright end
                fast_bpwm_bright = bpwm;

                // set bright phot but don't create a reversal
                if (phot > fast_phot_dim)
                    fast_phot_bright = phot;

                Serial.printf ("BR: set bright:   bpwm: %4d <= %4d <= %4d   phot: %4d <= %4d <= %4d\n",
                                            fast_bpwm_dim, bpwm, fast_bpwm_bright,
                                            fast_phot_dim, phot, fast_phot_bright);

                // persist
                NVWriteUInt16 (NV_BPWM_BRIGHT, fast_bpwm_bright);
                NVWriteUInt16 (NV_PHOT_BRIGHT, fast_phot_bright);

             } else {
                // set new dim end
                fast_bpwm_dim = bpwm;

                // set dim phot but don't create a reversal
                if (phot < fast_phot_bright)
                    fast_phot_dim = phot;

                Serial.printf ("BR: set dim:   bpwm: %4d <= %4d <= %4d   phot: %4d <= %4d <= %4d\n",
                                            fast_bpwm_dim, bpwm, fast_bpwm_bright,
                                            fast_phot_dim, phot, fast_phot_bright);

                // persist
                NVWriteUInt16 (NV_BPWM_DIM, fast_bpwm_dim);
                NVWriteUInt16 (NV_PHOT_DIM, fast_phot_dim);
            }

            clock_off = false;          // just to insure engage works
            engageDisplayBrightness(true);
        }

        else if (brb_mode == BRB_SHOW_BR) {

            SBox b;
            getBrControl (b);

            // set brightness directly from tap location within allowed range
            if (s.y < b.y)
                bpwm = user_max;
            else if (s.y > b.y + b.h)
                bpwm = user_min;
            else
                bpwm = user_min + (user_max-user_min)*(b.y + b.h - s.y)/b.h;

            clock_off = false;          // just to insure engage works
            engageDisplayBrightness(true);

        }

        else if (brb_mode == BRB_SHOW_ONOFF) {

            changeOnOffSetting (s);

        }

}

/* update after changing brb_mode
 */
static void engageNewBRBMode (void)
{
    // make a note
    logBRBRotSet();

    // match beacons to new state
    updateBeacons (true);

    // update on/off times if now used
    if (brb_mode == BRB_SHOW_ONOFF)
        getPersistentOnOffTimes (DEWeekday(), mins_on, mins_off);

    // immediate redraw
    brb_next_update = 0;

    // save
    NVWriteUInt16 (NV_BRB_ROTSET, brb_rotset);
}

/* run the brb/ncdxf box menu
 */
static void runNCDXFMenu (void)
{
        // this list of each BRB is to avoid knowing their values, nice BUT must be in same order as mitems[]
        static uint8_t mi_brb_order[BRB_N] = {
            BRB_SHOW_BEACONS, BRB_SHOW_SWSTATS, BRB_SHOW_ONOFF, BRB_SHOW_PHOT, BRB_SHOW_BR,
            BRB_SHOW_BME76, BRB_SHOW_BME77, BRB_SHOW_DXWX, BRB_SHOW_DEWX,
            BRB_SHOW_LIGHTNING
        };

        // don't show WX if already is already in a pane
        bool dx_anypane = findPaneForChoice (PLOT_CH_DXWX) != PANE_NONE;
        bool de_anypane = findPaneForChoice (PLOT_CH_DEWX) != PANE_NONE;

        // build menu, depending on current configuration
        #define _MI_INDENT 2
        MenuItem mitems[BRB_N] = {
             {MENU_AL1OFN, (bool)(brb_rotset & (1 << BRB_SHOW_BEACONS)), 1, _MI_INDENT,
                        brb_names[BRB_SHOW_BEACONS], 0},
             {MENU_AL1OFN, (bool)(brb_rotset & (1 << BRB_SHOW_SWSTATS)), 1, _MI_INDENT,
                        brb_names[BRB_SHOW_SWSTATS], 0},
             {support_onoff ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_ONOFF)),1,_MI_INDENT,brb_names[BRB_SHOW_ONOFF],0},
             {(support_onoff || support_dim) && found_phot ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_PHOT)),1,_MI_INDENT,brb_names[BRB_SHOW_PHOT],0},
             {support_dim && !found_phot ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BR)), 1, _MI_INDENT, brb_names[BRB_SHOW_BR], 0},
             {getBMEData(BME_76,false) != NULL ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BME76)),1,_MI_INDENT,brb_names[BRB_SHOW_BME76],0},
             {getBMEData(BME_77,false) != NULL ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_BME77)),1,_MI_INDENT,brb_names[BRB_SHOW_BME77],0},
             {dx_anypane ? MENU_IGNORE : MENU_AL1OFN,
                        (bool)(brb_rotset & (1 << BRB_SHOW_DXWX)), 1, _MI_INDENT, brb_names[BRB_SHOW_DXWX],0},
             {de_anypane ? MENU_IGNORE : MENU_AL1OFN,
                        (bool)(brb_rotset & (1 << BRB_SHOW_DEWX)), 1, _MI_INDENT, brb_names[BRB_SHOW_DEWX],0},
             {lightning_on ? MENU_AL1OFN : MENU_IGNORE,
                        (bool)(brb_rotset & (1 << BRB_SHOW_LIGHTNING)), 1, _MI_INDENT, brb_names[BRB_SHOW_LIGHTNING], 0},
        };

        // boxes
        SBox menu_b = NCDXF_b;                          // copy, not ref!
        menu_b.x += 5;
        menu_b.y += 10;
        menu_b.w = 0;                                   // shrink wrap
        SBox ok_b;

        // run menu
        MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_NOCANCEL, 1, BRB_N, mitems};   // no room for cancel
        bool ok = runMenu(menu);

        // engage new option unless canceled
        if (ok) {

            // build new rotset
            brb_rotset = 0;
            for (int i = 0; i < BRB_N; i++)
                if (mitems[i].set)
                    brb_rotset |= (1 << mi_brb_order[i]);

            // if brb_mode is not already in new set, just pick one
            if ((brb_rotset & (1 << brb_mode)) == 0) {
                for (int i = 0; i < BRB_N; i++) {
                    if (brb_rotset & (1 << i)) {
                        brb_mode = i;
                        break;
                    }
                }
            }

            // go
            engageNewBRBMode();
        }
}

/* perform proper action given s known to be within NCDXF_b.
 */
void doNCDXFBoxTouch (TouchType tt, const SCoord &s)
{
    if (s.y < NCDXF_b.y + NCDXF_b.h/10) {

        // tapped near the top so show menu of options or auto rotate if control-tap
        if (tt == TT_TAP_BX && (brb_rotset & ~(1U << brb_mode)) != 0)   // test for >1 rotset
            engageNewBRBMode();
        else
            runNCDXFMenu ();

    } else {

        // tapped below title so pass on to appropriate handler

        switch ((BRB_MODE)brb_mode) {

        case BRB_SHOW_BEACONS:
            // nothing
            break;

        case BRB_SHOW_ONOFF:
        case BRB_SHOW_PHOT:     // fallthru
        case BRB_SHOW_BR:       // fallthru
            changeBrightness (s);
            break;

        case BRB_SHOW_SWSTATS:
            doNCDXFSpcWxTouch (s);
            break;

        case BRB_SHOW_BME76:
        case BRB_SHOW_BME77:
            doBMETouch (s);
            break;

        case BRB_SHOW_DEWX:
        case BRB_SHOW_DXWX:     // fallthru
            doNCDXFWXTouch ((BRB_MODE)brb_mode);
            break;

        case BRB_SHOW_LIGHTNING:
            doLightningTouch();
            break;

        case BRB_N:
            // lint
            break;
        }
    }
}


/* set on/off/idle times for the given dow then engage if today is indeed dow.
 *   dow is 1..7 for Sun..Sat
 *   on/off times are minutes since DE midnight
 *   idle is mins
 * dow < 0 means ignore on/off too, idle < 0 means ignore idle.
 * since we only allow changing idle by multiples of 5, we enforce that here and caller can get back results.
 * N.B. this does NOT count as a new user interaction for determining idle timeout.
 * N.B. we do NOT validate args
 * return whether on/off is even implemented.
 */
bool setDisplayOnOffTimes (int dow, uint16_t new_on, uint16_t new_off, int &idle)
{
    if (support_onoff) {

        // persist new on/off if desired
        if (dow >= 1)
            persistOnOffTimes (dow, new_on, new_off);

        // persist new idle time if desired
        if (idle >= 0) {
            // enforce multiples of 5
            idle -= idle%5;
            // FYI: set idle_t0 too if you want this to count as a tap
            idle_mins = idle;
            NVWriteUInt16 (NV_BR_IDLE, idle_mins);
        }

        // engage and display to confirm if showing today
        if (brb_mode == BRB_SHOW_ONOFF && (idle >= 0 || dow == DEWeekday())) {
            mins_on = new_on;
            mins_off = new_off;
            drawOnOffControls();
        }

        return (true);

    } else {

        return (false);

    }
}

/* return clock timer settings and whether supported
 */
bool getDisplayInfo (uint16_t &percent, uint16_t &idle_min, uint16_t &idle_left_sec)
{
    if (support_onoff) {
        percent = 100*bpwm/BPWM_MAX;
        idle_min = idle_mins;

        uint16_t idle_dt_s = (millis() - idle_t0)/1000;
        idle_left_sec = idle_mins*60 > idle_dt_s ? idle_mins*60 - idle_dt_s: 0;

        return (true);

    } else {

        return (false);

    }
}

/* return on and off times for the given dow 1..7 Sun..Sat.
 * times are minutes 0 .. 24*60-1
 * N.B. we do NOT validate args
 * return whether even implemented.
 */
bool getDisplayOnOffTimes (int dow, uint16_t &on, uint16_t &off)
{
    if (support_onoff) {
        getPersistentOnOffTimes (dow, on, off);
        return (true);
    } else {
        return (false);
    }
}
