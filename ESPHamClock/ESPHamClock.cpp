/* HamClock
 */


// glue
#include "HamClock.h"

// clock, aux time and stopwatch boxes
SBox clock_b = { 0, 65, 230, 49};
SBox auxtime_b = { 0, 113, 204, 32};
SBox stopwatch_b = {149, 93, 38, 22};
SBox lkscrn_b = {217, 118, 11, 16};     // best if odd width
static SBox demo_b = {204, 121, 11, 13};// N.B. must match runner.pl

// DE and DX map boxes
SBox dx_info_b;                         // dx info location
static SBox askdx_b;                    // dx lat/lng dialog
SCircle dx_marker_c;                    // DX symbol in DX info pane
SBox satname_b;                         // satellite name
SBox de_info_b;                         // de info location
static SBox askde_b;                    // de lat/lng dialog
SBox de_title_b;                        // de title location
SBox map_b;                             // entire map, pixels only, not border
SBox view_btn_b;                        // map View button
SBox borders_btn_b;                     // on-map Borders On/Off badge, right of the View button
SBox dx_maid_b;                         // dx maindenhead 
SBox de_maid_b;                         // de maindenhead 

// time zone state
TZInfo de_tz = {{75, 158, 50, 17}, DE_COLOR, de_ll, true, 0};
TZInfo dx_tz = {{75, 307, 50, 17}, DX_COLOR, dx_ll, true, 0};

// NCDFX box, also used for brightness, on/off controls and space wx stats
SBox NCDXF_b = {738, 0, 62, PLOTBOX123_H};

// common "skip" button in several places
SBox skip_b    = {730,10,55,35};
bool skip_skip;
bool want_kbcursor;

// special options to force initing DE using our IP or given IP
bool init_iploc;
const char *init_locip;

// maidenhead label boxes
SBox maidlbltop_b;
SBox maidlblright_b;

// satellite pass horizon
SCircle satpass_c;

// whether to shade night or show place names
uint8_t night_on, names_on;
uint8_t borders_on;

// handy flag when showing main page
bool mainpage_up;

// grid styles
uint8_t mapgrid_choice;                         // one of MapGridStyle
const char *grid_styles[MAPGRID_N] = {
    "None",
    "Tropics",
    "Lat/Long",
    "Maidenhead",
    "Maidenhead+",
    "Azimuthal",
    "CQ Zones",
    "ITU Zones",
};

// map projections
uint8_t map_proj;                               // one of MapProjection
#define X(a,b)  b,                              // expands MAPPROJS to name plus comma
const char *map_projnames[MAPP_N] = {
    MAPPROJS
};
#undef X

// info to display below the call sign location
static SBox uptime_b;                           // show up time, just below call
#define UPTIME_INDENT   15                      // indent within uptime_b for "Up" label
static SBox version_b;                          // show or check version, just below call
static SBox wifi_b;                             // wifi info
#define RSSI_ALPHA 0.5F                         // rssi blending coefficient
#define CSINFO_DROP     2                       // gap below cs_info
#define CSINFO_H        9                       // up/wifi/version box heights

// de and dx sun rise/set boxes, dxsrss_b also used for DX prefix depending on dxsrss
SBox desrss_b, dxsrss_b;


// messages below call sign rotate through several display options
typedef enum {
    ROTM_RSSI,                                  // show RSSI
    ROTM_LIP,                                   // show local IP
    ROTM_PIP,                                   // show public IP
    ROTM_BE,                                    // show backend hostname
    ROTM_BEIP,                                  // show backend ip address
    ROTM_CPUTEMP,                               // show CPU temperature
    ROTM_FSUSE,                                 // show file system usage
    ROTM_N,                                     // n values
} RotMsg_t;
static RotMsg_t rot_msg = ROTM_RSSI;

// WiFi touch control
TouchType wifi_tt;
SCoord wifi_tt_s;

// set up TFT display controller RA8875 instance on hardware SPI plus reset and chip select
#define RA8875_RESET    16
#define RA8875_CS       2
Adafruit_RA8875 tft(RA8875_CS, RA8875_RESET);

// MCP23017 driver
Adafruit_MCP23X17 mcp;
bool found_mcp;

// millis() when DE-DX was first drawn, else 0
static uint32_t dxpath_time;
#define DEDX_MINPATH    (200/pan_zoom.zoom)     // don't show DE-DX path if separated by less than this, miles

// manage using a known DX cluster prefix or one derived from nearest LL
static bool dx_prefix_use_override;             // whether to use dx_override_prefixp[] or ll2Prefix()
static char dx_override_prefix[MAX_PREF_LEN];

// whether flash crc is ok -- from old ESP days
uint8_t flash_crc_ok = 1;

// whether and which new version is available
static bool new_avail;
#if !defined(NO_UPGRADE)
static char new_version[20];
#endif // !NO_UPGRADE

// name of each DETIME setting, for menu and set_defmt
#define X(a,b)  b,                              // expands DETIMES to name plus comma
const char *detime_names[DETIME_N] = {
    DETIMES
};
#undef X


// these are needed for any normal C++ compiler, not that crazy Arduino IDE
static void drawVersion(bool force);
static void checkTouch(void);
static void drawUptime(bool force);
static void drawRotatingMessage(void);
static void drawScreenLock(void);
static void toggleLockScreen(void);
static void setDXPrefixOverride (const char *ovprefix);
static void unsetDXPrefixOverride (void);
static void runShutdownMenu(void);
static void checkOnAirPin (void);



/* try to restore pi to somewhat normal config
 */
static void defaultState()
{
    // try to insure screen is back on -- har!
    brightnessOn();

    // return all IO pins to stable defaults
    SWresetIO();
    satResetIO();
    disableMCPPoller (ONAIR_PIN);
    radioResetIO();
}



/* print setting of several compile-time #defines
 */
static void showDefines(void)
{
    #define _PR_MAC(m)   Serial.printf ("#define %s\n", #m)

    #if defined(_IS_UNIX)
        _PR_MAC(_IS_UNIX);
    #endif

    #if defined(_IS_LINUX)
        _PR_MAC(_IS_LINUX);
    #endif

    #if defined(_IS_FREEBSD)
        _PR_MAC(_IS_FREEBSD);
    #endif

    #if defined(_IS_NETBSD)
        _PR_MAC(_IS_NETBSD);
    #endif

    #if defined(_IS_LINUX_RPI)
        _PR_MAC(_IS_LINUX_RPI);
    #endif

    #if defined(_IS_LINUX_ARMBIAN)
        _PR_MAC(_IS_LINUX_ARMBIAN);
    #endif

    #if defined(_NATIVE_I2C_FREEBSD)
        _PR_MAC(_NATIVE_I2C_FREEBSD);
    #endif

    #if defined(_NATIVE_I2C_LINUX)
        _PR_MAC(_NATIVE_I2C_LINUX);
    #endif

    #if defined(_NATIVE_GPIO_FREEBSD)
        _PR_MAC(_NATIVE_GPIO_FREEBSD);
    #endif

    #if defined(_NATIVE_GPIO_LINUX)
        _PR_MAC(_NATIVE_GPIO_LINUX);
    #endif

    #if defined(_NATIVE_GPIOD_LINUX)
        _PR_MAC(_NATIVE_GPIOD_LINUX);
    #endif

    #if defined(_USE_GPIOD)
        _PR_MAC(_USE_GPIOD);
    #endif

    #if defined(_NATIVE_GPIOBC_LINUX)
        _PR_MAC(_NATIVE_GPIOBC_LINUX);
    #endif

    #if defined(_SUPPORT_NATIVE_GPIO)
        _PR_MAC(_SUPPORT_NATIVE_GPIO);
    #endif

    #if defined(NO_UPGRADE)
        _PR_MAC(NO_UPGRADE);
    #endif

    #if defined(_SUPPORT_KX3)
        _PR_MAC(_SUPPORT_KX3);
    #endif
}

// flag to stop main loop by other threads
static volatile bool stop_main_thread;

// initial stack location
char *stack_start;

// called once
void setup()
{
    // init record of stack
    char stack;
    stack_start = &stack;

    // start trace and debug
    Serial.begin(115200);
    while (!Serial)
        wdDelay(500);
    Serial.printf("HamClock version %s platform %s\n", hc_version, platform);

    // show config
    showDefines();

    // random seed, not critical
    randomSeed(getpid());

    // Initialise the display -- not worth continuing if not found
    if (!tft.begin(RA8875_800x480)) {
        Serial.println("RA8875 Not Found!");
        while (1);
    }
    Serial.println("RA8875 found");

    // Adafruit assumed ESP8266 would run at 80 MHz, but we run it at 160
    extern uint32_t spi_speed;
    spi_speed *= 2;

    // turn display full on
    tft.displayOn(true);
    tft.GPIOX(true); 
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024); // PWM output for backlight
    initBrightness();

    // initialize Antennas
    initAntennas();

// #define _GFX_COORD_TEST                              // RBF
#if defined(_GFX_COORD_TEST)

    // confirm our posix porting layer graphics agree with Adafruit
    tft.fillScreen(RA8875_BLACK);
    tft.fillRect (100, 100, 6, 6, RA8875_RED);

    tft.drawPixel (399, 199, RA8875_GREEN);
    tft.drawPixel (400, 200, RA8875_GREEN);     // covered?
    tft.drawPixel (409, 209, RA8875_GREEN);     // covered?
    tft.drawPixel (410, 210, RA8875_GREEN);
    tft.drawRect  (400, 200, 10, 10, RA8875_RED);

    tft.drawPixel (399, 299, RA8875_GREEN);
    tft.drawPixel (400, 300, RA8875_GREEN);     // covered?
    tft.drawPixel (409, 309, RA8875_GREEN);     // covered?
    tft.drawPixel (410, 310, RA8875_GREEN);
    tft.fillRect  (400, 300, 10, 10, RA8875_RED);

    tft.drawRect (100, 100, 8, 8, RA8875_RED);
    tft.drawPixel (100,108,RA8875_RED);
    tft.drawPixel (102,108,RA8875_RED);
    tft.drawPixel (104,108,RA8875_RED);
    tft.drawPixel (106,108,RA8875_RED);
    tft.drawPixel (108,108,RA8875_RED);
    tft.drawCircle (100, 200, 1, RA8875_RED);
    tft.drawCircle (100, 200, 5, RA8875_RED);
    tft.fillCircle (110, 200, 3, RA8875_RED);
    tft.drawPixel (100,207,RA8875_RED);
    tft.drawPixel (100,208,RA8875_RED);
    tft.drawPixel (102,207,RA8875_RED);
    tft.drawPixel (104,207,RA8875_RED);
    tft.drawPixel (106,207,RA8875_RED);
    tft.drawPixel (108,207,RA8875_RED);
    tft.drawPixel (110,207,RA8875_RED);
    tft.drawPixel (110,208,RA8875_RED);
    tft.drawPixel (112,207,RA8875_RED);
    tft.drawPixel (114,207,RA8875_RED);
    tft.drawPixel (114,200,RA8875_RED);


    // explore thick line artifacts

    for (float a = 0; a < 2*M_PIF; a += M_PIF/47) {
        uint16_t x = 250*tft.SCALESZ;
        uint16_t y = 120*tft.SCALESZ;
        int16_t dx = 100*tft.SCALESZ * cosf(a);
        int16_t dy = -100*tft.SCALESZ * sinf(a);
        tft.drawLineRaw (x, y, x+dx, y+dy, tft.SCALESZ/2, RA8875_RED);
        x += 400;
        tft.drawLineRaw (x, y, x+dx, y+dy, tft.SCALESZ, RA8875_RED);
        x += 400;
        tft.drawLineRaw (x, y, x+dx, y+dy, 2*tft.SCALESZ, RA8875_RED);
    }

    uint16_t x = 250*tft.SCALESZ;
    uint16_t y = 320*tft.SCALESZ;
    tft.drawLineRaw (x, y, x + 50, y, tft.SCALESZ, RA8875_RED);
    tft.drawLineRaw (x + 50, y, x + 75, y + 25, tft.SCALESZ, RA8875_RED);

    while(1)
        wdDelay(100);
#endif // _GFX_COORD_TEST

// #define _GFX_TEXTSZ                          // RBF
#if defined(_GFX_TEXTSZ)
    // just used to compare our text port with Adafruit GFX 
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    char str[] = "scattered clouds";
    int sum = 0;
    for (char *s = str; *s; s++) {
        char s1 = s[1];
        s[1] = '\0';
        int l = getTextWidth(s);
        Serial.printf ("%c %d\n", *s, l);
        s[1] = s1;
        sum += l;
    }
    Serial.printf ("Net %d\n", getTextWidth(str));
    Serial.printf ("Sum %d\n", sum);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (10,100);
    tft.print("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    tft.setCursor (10,120);
    tft.print("abcdefghijklmnopqrstuvwxyz");
    tft.setCursor (10,140);
    tft.print(R"(0123456789 ~!@#$%^&*()_+{}|:"<>? `-=[]\;',./)");

    while(1)
        wdDelay(100);
#endif // _GFX_TEXTSZ

    // enable touch screen system
    tft.touchEnable(true);

    // support live even in setup
    initLiveWeb(false);

    // set up brb_rotset and brb_mode
    initBRBRotset();

    // run Setup at full brighness
    clockSetup();

    // set desried gray display
    tft.setGrayDisplay(getGrayDisplay());

    // init pan and zoom
    if (!NVReadUInt8 (NV_ZOOM, &pan_zoom.zoom)) {
        pan_zoom.zoom = MIN_ZOOM;
        NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
    }
    if (!NVReadInt16 (NV_PANX, &pan_zoom.pan_x)) {
        pan_zoom.pan_x = 0;
        NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
    }
    if (!NVReadInt16 (NV_PANY, &pan_zoom.pan_y)) {
        pan_zoom.pan_y = 0;
        NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
    }
    normalizePanZoom(pan_zoom);

    // initialize MCP23017, fails gracefully so just log whether found
    found_mcp = mcp.begin_I2C();
    if (found_mcp)
        Serial.println("MCP: GPIO mechanism found");
    else
        Serial.println("MCP: GPIO mechanism not found");

    // start onair poller
    startMCPPoller (ONAIR_PIN);

    // continue with user's desired brightness
    setupBrightness();

    // do not display time until all set up
    hideClocks();

    // here we go
    eraseScreen();

    // draw initial callsign
    cs_info.box.x = (tft.width()-512)/2;
    cs_info.box.y = 10;                 // coordinate with tftMsg()
    cs_info.box.w = 512;
    cs_info.box.h = 50;
    initCallsignInfo();
    updateCallsign (true);

    // draw version just below
    tft.setTextColor (RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (cs_info.box.x+(cs_info.box.w-getTextWidth(hc_version))/2, cs_info.box.y+cs_info.box.h+10);
    tft.print (hc_version);

    // position the map box in lower right -- border is drawn outside
    map_b.w = EARTH_W;
    map_b.h = EARTH_H;
    map_b.x = tft.width() - map_b.w - 1;        // allow 1 for right border
    map_b.y = tft.height() - map_b.h - 1;       // allow 1 for bottom border

    // position View button in upper left
    view_btn_b.x = map_b.x;
    view_btn_b.y = map_b.y;                     // gets adjusted if showing grid label
    view_btn_b.w = 60;
    view_btn_b.h = 14;

    // position Borders on-map badge just to the right of the View button, with a small gap.
    // width is sized to fit "Borders Off", the longer of its two labels, plus padding.
    {
        const int gap = 4;                       // pixels between View button and badge
        const int pad = 8;                       // pixels of horizontal padding inside the badge
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        borders_btn_b.x = view_btn_b.x + view_btn_b.w + gap;
        borders_btn_b.y = view_btn_b.y;          // tracks view_btn_b.y each time it's (re)drawn
        borders_btn_b.w = getTextWidth ("Borders Off") + pad;
        borders_btn_b.h = view_btn_b.h;
    }

    // position WEFAX on-map badge in the same slot as the Borders badge, same convention.
    // the two are never visible at once -- bordersBadgeVisible() requires CM_CLOUDS/CM_TERRAIN
    // and wefaxBadgeVisible() requires CM_WX, and core_map can only be one value at a time --
    // so sharing the slot avoids a permanent gap next to the View button when neither is showing.
    {
        const int gap = 4;
        const int pad = 8;
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        wefax_btn_b.x = view_btn_b.x + view_btn_b.w + gap;
        wefax_btn_b.y = view_btn_b.y;
        wefax_btn_b.w = getTextWidth ("WEFAX Off") + pad;
        wefax_btn_b.h = view_btn_b.h;
    }

    // redefine callsign for main screen
    cs_info.box.x = 0;
    cs_info.box.y = 0;
    cs_info.box.w = 230;
    cs_info.box.h = 52;

    // uptime box
    uptime_b.x = cs_info.box.x;
    uptime_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    uptime_b.w = 62;
    uptime_b.h = CSINFO_H;

    // wifi info box
    wifi_b.x = uptime_b.x + uptime_b.w;
    wifi_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    wifi_b.w = 126;
    wifi_b.h = CSINFO_H;

    // version box
    version_b.x = wifi_b.x + wifi_b.w;
    version_b.w = cs_info.box.x + cs_info.box.w - version_b.x;
    version_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    version_b.h = CSINFO_H;

    // start WiFi, maps and set de_ll.lat_d/de_ll.lng_d from geolocation or gpsd as desired -- uses tftMsg()
    initSys();

    // get from nvram even if set prior from setup, geolocate or gpsd
    NVReadFloat(NV_DE_LAT, &de_ll.lat_d);
    NVReadFloat(NV_DE_LNG, &de_ll.lng_d);
    de_ll.normalize();
    if (!NVReadTZ (NV_DE_TZ, de_tz)) {
        setTZAuto (de_tz);
        NVWriteTZ (NV_DE_TZ, de_tz);
    }

#if !defined(NO_UPGRADE)
    // ask to update if new version available -- never returns if update succeeds
    if (!skip_skip) {
        new_avail = newVersionIsAvailable (new_version, sizeof(new_version));
        if (new_avail && askOTAupdate (new_version, true, false)) {
            if (askPasswd ("upgrade", false))
                doOTAupdate(new_version);
            eraseScreen();
        }
    }
#endif // !NO_UPGRADE

    // init sensors
    initBME280();

    // read plot settings from NVnsure sane defaults 
    initPlotPanes();

    // init rest of de info
    de_info_b.x = 1;                    // inside the border
    de_info_b.y = 185;                  // below DE: line
    de_info_b.w = map_b.x - 2;          // just inside border
    de_info_b.h = 109;                  // just above the DE-DX border at y 294
    uint16_t devspace = de_info_b.h/DE_INFO_ROWS;
    askde_b.x = de_info_b.x + 1;
    askde_b.y = de_info_b.y;
    askde_b.w = de_info_b.w - 2;
    askde_b.h = 3*devspace;
    de_maid_b.x = de_info_b.x;
    de_maid_b.y = de_info_b.y + 2*devspace;
    de_maid_b.w = de_info_b.w/2;
    de_maid_b.h = devspace;
    desrss_b.x = de_info_b.x + de_info_b.w/2;
    desrss_b.y = de_maid_b.y;
    desrss_b.w = de_info_b.w/2;
    desrss_b.h = devspace;

    if (!NVReadUInt8(NV_DE_SRSS, &desrss)) {
        desrss = false;
        NVWriteUInt8(NV_DE_SRSS, desrss);
    }
    if (!NVReadUInt8(NV_DE_TIMEFMT, &de_time_fmt)) {
        de_time_fmt = DETIME_INFO;
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    }
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    antipode (deap_ll, de_ll);
    ll2s (de_ll, de_c.s, DE_R);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    de_title_b.x = de_info_b.x;
    de_title_b.y = de_tz.box.y-5;
    de_title_b.w = 30;
    de_title_b.h = 30;

    // init dx unit
    if (!NVReadUInt8 (NV_LP, &show_lp)) {
        show_lp = false;
        NVWriteUInt8 (NV_LP, show_lp);
    }
    dx_info_b.x = 1;                    // inside the border
    dx_info_b.y = 295;                  // DE-DX border at y 294
    dx_info_b.w = de_info_b.w;
    dx_info_b.h = 184;
    uint16_t dxvspace = dx_info_b.h/DX_INFO_ROWS;
    askdx_b.x = dx_info_b.x+1;
    askdx_b.y = dx_info_b.y + dxvspace;
    askdx_b.w = dx_info_b.w-2;
    askdx_b.h = 3*dxvspace;
    dx_marker_c.s.x = dx_info_b.x+62;
    dx_marker_c.s.y = dx_tz.box.y+8;
    dx_marker_c.r = dx_c.r;
    dxsrss_b.x = dx_info_b.x + dx_info_b.w/2;
    dxsrss_b.y = dx_info_b.y + 3*dxvspace;
    dxsrss_b.w = dx_info_b.w/2;
    dxsrss_b.h = dxvspace;
    dx_maid_b.x = dx_info_b.x;
    dx_maid_b.y = dx_info_b.y + 3*dxvspace;
    dx_maid_b.w = dx_info_b.w/2;
    dx_maid_b.h = dxvspace;
    if (!NVReadUInt8(NV_DX_SRSS, &dxsrss)) {
        dxsrss = DXSRSS_INAGO;
        NVWriteUInt8(NV_DX_SRSS, dxsrss);
    }
    if (!NVReadFloat (NV_DX_LAT, &dx_ll.lat_d) || !NVReadFloat (NV_DX_LNG, &dx_ll.lng_d)
                                               || !NVReadTZ (NV_DX_TZ, dx_tz)) {
        // if never set, default to 0/0
        dx_ll.lat_d = 0;
        dx_ll.lng_d = 0;
        NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
        NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
        setNVMaidenhead(NV_DX_GRID, dx_ll);
        setTZAuto (dx_tz);
        NVWriteTZ (NV_DX_TZ, dx_tz);
    }
    dx_ll.normalize();
    ll2s (dx_ll, dx_c.s, DX_R);

    // sat pass circle
    satpass_c.r = dx_info_b.h/3 - 3;
    satpass_c.s.x = dx_info_b.x + dx_info_b.w/2;
    satpass_c.s.y = dx_info_b.y + dx_info_b.h - satpass_c.r - 4;


    // init portion of dx info used for satellite name
    satname_b.x = dx_info_b.x;
    satname_b.y = dx_info_b.y+1U;
    satname_b.w = dx_info_b.w;
    satname_b.h = dx_info_b.h/6;        // match FONT_H in earthsat.cpp

    // set up RSS state and banner box
    rss_bnr_b.x = map_b.x;
    rss_bnr_b.y = map_b.y + map_b.h - 68;
    rss_bnr_b.w = map_b.w;
    rss_bnr_b.h = 68;
    NVReadUInt8 (NV_RSS_ON, &rss_on);
    initLightning();
    initStorms();
    initWefax();
    if (!NVReadUInt8 (NV_RSS_INTERVAL, &rss_interval) || rss_interval < RSS_MIN_INT) {
        rss_interval = RSS_DEF_INT;
        NVWriteUInt8 (NV_RSS_INTERVAL, rss_interval);
    }

    // set up map projection
    if (!NVReadUInt8 (NV_MAPPROJ, &map_proj)) {
        map_proj = MAPP_MERCATOR;
        NVWriteUInt8 (NV_MAPPROJ, map_proj);
    }

    // get grid style state
    if (!NVReadUInt8 (NV_GRIDSTYLE, &mapgrid_choice)) {
        mapgrid_choice = MAPGRID_OFF;
        NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
    }

    // init psk reporter
    initPSKState();

    // position the maiden label boxes
    maidlbltop_b.x = map_b.x;
    maidlbltop_b.y = map_b.y;
    maidlbltop_b.w = map_b.w;
    maidlbltop_b.h = MH_TR_H;
    maidlblright_b.x = map_b.x + map_b.w - MH_RC_W;
    maidlblright_b.y = map_b.y;
    maidlblright_b.w = MH_RC_W;
    maidlblright_b.h = map_b.h;

    // position the map scale
    // N.B. mapscale_b.y is set dynamically in drawMapScale() depending on rss_on
    mapscale_b.x = map_b.x;
    mapscale_b.w = map_b.w;
    mapscale_b.h = 10;
    mapscale_b.y = rss_on ? rss_bnr_b.y - mapscale_b.h: map_b.y + map_b.h - mapscale_b.h;

    // check for saved satellite
    dx_info_for_sat = initSat();

    // prep stopwatch
    initStopwatch();

    // log screen lock
    Serial.printf ("Screen lock is now %s\n", screenIsLocked() ? "On" : "Off");

    // here we go
    initScreen();
}

// called repeatedly forever
void loop()
{
    // used by fatalError to stop main thread from scribbling over message
    if (stop_main_thread)
        return;

    // always do these
    drawFireworks();                    // only new years midnight
    updateSatPass ();                   // just for the satellite LED
    checkDXCluster ();                  // collect new spots if running
    checkEclipsePopupTimeout ();        // auto-dismiss the eclipse pane's detail popup, if up

    // update stopwatch exclusively, if active
    if (!runStopwatch()) {

        // check on wifi including plots and NCDXF_b
        updateWiFi();

        // update clocks
        updateClocks(false);

        // update sat pass (this is just the pass; the path is recomputed before each map sweep)
        updateSatPass();

        // display more of earth map
        drawMoreEarth();

        // refresh just the satellite footprint(s) at a much tighter cadence than the full map
        // sweep, without the cost of a full sweep (see updateSatFootprintFast())
        #define SAT_FOOT_FAST_MS 5000U
        static uint32_t sat_foot_ms;
        uint32_t now_ms = millis();
        if (now_ms - sat_foot_ms >= SAT_FOOT_FAST_MS) {
            sat_foot_ms = now_ms;
            updateSatFootprintFast();
        }

        // other goodies
        updateCallsignStatus(false);
        followBrightness();
        checkOnAirPin();
        readBME280();
        runNextDemoCommand();
        updateGPSDLoc();
        updateNMEALoc();
        updateCallsign (false);
        pollRadio();

        // check for touch events
        checkTouch();
    }
}


/* draw the one-time portion of de_info either because we just booted or because
 * we are transitioning back from being in sat mode or a menu
 */
void drawOneTimeDE()
{
    if (SHOWING_PANE_0())
        return;

    // outside the box
    // N.B. de_info_b does not include the DE: line
    uint16_t top_y = map_b.y - 1;                       // top border y
    uint16_t bot_y = de_info_b.y + de_info_b.h;         // yes, we want 1 farther for bottom border line
    tft.fillRect (de_info_b.x-1, top_y, de_info_b.w+2, bot_y - top_y + 1, RA8875_BLACK);
    tft.drawRect (de_info_b.x-1, top_y, de_info_b.w+2, bot_y - top_y + 1, GRAY);

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DE_COLOR);
    tft.setCursor(de_info_b.x, de_tz.box.y+18);
    tft.print("DE:");

    // save/restore de_c so it can be used for marker in box
    SCircle de_c_save = de_c;
    de_c.s.x = de_info_b.x+62;
    de_c.s.y = de_tz.box.y+8;
    drawDEMarker(true);
    de_c = de_c_save;
}

static void drawDXMarker (bool force)
{
    // check for being off zoomed mercator map
    if (dx_c.s.x == 0)
        return;

    if (force || showDXMarker()) {
        tft.fillCircle (dx_c.s.x, dx_c.s.y, DX_R, DX_COLOR);
        tft.drawCircle (dx_c.s.x, dx_c.s.y, DX_R, RA8875_BLACK);
        tft.fillCircle (dx_c.s.x, dx_c.s.y, 2, RA8875_BLACK);
    }
}

/* draw the one-time portion of dx_info either because we just booted or because
 * we are transitioning back from being in sat mode
 */
void drawOneTimeDX()
{
    if (SHOWING_PANE_0())
        return;

    if (dx_info_for_sat) {
        // sat
        drawSatPass();
    } else {
        // outside the box
        tft.fillRect (dx_info_b.x-1, dx_info_b.y-1, dx_info_b.w+2, dx_info_b.h+2, RA8875_BLACK);
        tft.drawRect (dx_info_b.x-1, dx_info_b.y-1, dx_info_b.w+2, dx_info_b.h+2, GRAY);

        // title
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        tft.setTextColor(DX_COLOR);
        tft.setCursor(dx_info_b.x, dx_info_b.y + 30);
        tft.print("DX:");

        // save/restore dx_c so it can be used for marker in box
        SCircle dx_c_save = dx_c;
        dx_c = dx_marker_c;
        drawDXMarker(true);
        dx_c = dx_c_save;
    }
}

/* assuming basic hw init is complete setup everything for the screen.
 * called once at startup and after each time returning from other full-screen options.
 * The overall layout is establihed by setting the various SBox values.
 * Some are initialized statically in setup() some are then set relative to these.
 */
void initScreen()
{
    // erase entire screen
    eraseScreen();

    // back to main page
    mainpage_up = true;

    // set protected region, which requires explicit call to tft.drawPR() to update
    tft.setPR (map_b.x, map_b.y, map_b.w, map_b.h);

    // us
    drawVersion(true);
    updateCallsign(true);

    // draw section borders
    tft.drawLine (0, map_b.y-1, tft.width()-1, map_b.y-1, GRAY);                        // top
    tft.drawLine (0, tft.height()-1, tft.width()-1, tft.height()-1, GRAY);              // bottom
    tft.drawLine (0, map_b.y-1, 0, tft.height()-1, GRAY);                               // left
    tft.drawLine (tft.width()-1, map_b.y-1, tft.width()-1, tft.height()-1, GRAY);       // right
    tft.drawLine (map_b.x-1, map_b.y-1, map_b.x-1, tft.height()-1, GRAY);               // left of map

    // one-time info
    setNewSatCircumstance();
    drawOneTimeDE();
    drawOneTimeDX();

    // enable clocks
    showClocks();
    drawMainPageStopwatch(true);

    // start 
    initEarthMap();
    initWiFiRetry();
    drawUptime(true);
    drawScreenLock();
    drawMOTDIcon();
    drawDemoRunner();

    // always close so it will restart if open in any pane
    closeGimbal();

    // flush any stale touchs
    drainTouch();
}

/* monitor for touch events
 */
static void checkTouch()
{
    TouchType tt;
    SCoord s;

    // check for remote and local touch
    if (wifi_tt != TT_NONE) {
        // save and reset remote touch.
        // N.B. remote touches never turn on brightness
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
    } else {
        // check tap
        tt = readCalTouch (s);
        if (tt == TT_NONE)
            tt = checkKBWarp (s);
        if (tt == TT_NONE)
            return;
        // don't do anything else if this tap just turned on brightness
        if (brightnessOn()) {
            drainTouch();
            return;
        }
    }

    // check lock
    if (inBox (s, lkscrn_b) || inBox (s, demo_b))
        runShutdownMenu();
    if (screenIsLocked())
        return;

    drainTouch();

    // check all touch locations, ones that can be over map checked first and beware showing PANE_0
    LatLong ll;
    if (inBox (s, view_btn_b)) {
        if (tt == TT_TAP_BX) {
            if (mapIsRotating()) {
                rotateNextMap();
                scheduleFreshMap();
            }
        } else {
            // set flag to draw map menu at next opportunity
            mapmenu_pending = true;
        }
    } else if (bordersBadgeVisible() && inBox (s, borders_btn_b)) {
        borders_on = !borders_on;
        NVWriteUInt8 (NV_CTYBORDERS_ON, borders_on);
        initEarthMap();
        tft.drawPR();
    } else if (wefaxBadgeVisible() && inBox (s, wefax_btn_b)) {
        // runWefaxViewer() blocks until the user closes it, and restores the map on the way out
        runWefaxViewer();
    } else if (checkSatMapTouch (s)) {
        // set showing sat in DX box
        dx_info_for_sat = true;
        drawSatPass();
    } else if (checkPlanetMapTouch (s)) {
        // handled entirely within checkPlanetMapTouch
    } else if (!overViewBtn(s, DX_R) && s2ll (s, ll)) {
        // tapped map: set flag to run popup after map finishes or set newDX here if special-tap
        if (names_on)
            (void) getNearestCity (ll, ll, NULL);               // ll is unchanged if no city found
        // roundLL (ll); TODO: why did we used to do this?
        if (tt == TT_TAP_BX)
            newDX (ll, NULL, NULL);
        else {
            map_popup.pending = true;
            map_popup.s = s;
            map_popup.ll = ll;
        }
    } else if (!SHOWING_PANE_0() && inBox (s, de_title_b)) {
        drawDEFormatMenu();
    } else if (inBox (s, stopwatch_b)) {
        // check this before checkClockTouch
        checkStopwatchTouch();
    } else if (checkClockTouch(s)) {
        updateClocks(true);
    } else if (!SHOWING_PANE_0() && inBox (s, de_tz.box)) {
        if (TZMenu (de_tz, de_ll)) {
            NVWriteTZ (NV_DE_TZ, de_tz);
            scheduleNewPlot(PLOT_CH_MOON);
            scheduleNewPlot(PLOT_CH_SDO);
            scheduleNewPlot(PLOT_CH_BC);
            scheduleNewPlot(PLOT_CH_ECLIPSE);
            drawDEInfo();
        }
    } else if (!SHOWING_PANE_0() && !dx_info_for_sat && inBox (s, dx_tz.box)) {
        if (TZMenu (dx_tz, dx_ll)) {
            NVWriteTZ (NV_DX_TZ, dx_tz);
            drawDXInfo();
        }
    } else if (inBox (s, cs_info.box)) {
        doCallsignTouch (s);
    } else if (!SHOWING_PANE_0() && !dx_info_for_sat && checkPathDirTouch(s)) {
        show_lp = !show_lp;
        NVWriteUInt8 (NV_LP, show_lp);
        drawDXInfo ();
        scheduleNewPlot(PLOT_CH_BC);
        scheduleNewCoreMap(core_map);
    } else if (!SHOWING_PANE_0() && inBox (s, askde_b)) {
        // N.B. askde overlaps the desrss box
        if (de_time_fmt == DETIME_INFO && inBox (s, desrss_b)) {
            desrss = !desrss;
            NVWriteUInt8 (NV_DE_SRSS, desrss);
            drawDEInfo();
        } else {
            char maid[MAID_CHARLEN];
            getNVMaidenhead (NV_DE_GRID, maid);
            if (askNewPos (askde_b, ll = de_ll, maid))
                newDE (ll, maid);
            else
                drawDEInfo();
        }
    } else if (!SHOWING_PANE_0() && !dx_info_for_sat && inBox (s, askdx_b)) {
        // N.B. askdx overlaps the dxsrss box
        if (inBox (s, dxsrss_b)) {
            dxsrss = (dxsrss+1)%DXSRSS_N;
            NVWriteUInt8 (NV_DX_SRSS, dxsrss);
            drawDXInfo();
        } else {
            char maid[MAID_CHARLEN];
            getNVMaidenhead (NV_DX_GRID, maid);
            if (askNewPos (askdx_b, ll = dx_ll, maid)) {
                newDX (ll, maid, NULL);
            } else
                drawDXInfo();
        }
    } else if (!SHOWING_PANE_0() && !dx_info_for_sat && inCircle(s, dx_marker_c)) {
        newDX (dx_ll, NULL, NULL);
    } else if (SHOWING_PANE_0() && checkPlotTouch(tt, s, PANE_0)) {
        updateWiFi();
    } else if (checkPlotTouch(tt, s, PANE_1)) {
        updateWiFi();
    } else if (checkPlotTouch(tt, s, PANE_2)) {
        updateWiFi();
    } else if (checkPlotTouch(tt, s, PANE_3)) {
        updateWiFi();
    } else if (inBox (s, NCDXF_b)) {
        doNCDXFBoxTouch(tt, s);
    } else if (!SHOWING_PANE_0() && checkSatNameTouch(s)) {
        dx_info_for_sat = querySatSelection();
        initScreen();
    } else if (!SHOWING_PANE_0() && dx_info_for_sat && inBox (s, dx_info_b)) {
        drawDXSatMenu(s);

#if !defined(NO_UPGRADE)
    } else if (inBox (s, version_b)) {
        new_avail = newVersionIsAvailable(new_version, sizeof(new_version));
        if (new_avail) {
            if (askOTAupdate (new_version, true, false) && askPasswd ("upgrade", false))
                doOTAupdate(new_version);
        } else {
            if (askOTAupdate (new_version, false, false) && askPasswd ("upgrade", false))
                doOTAupdate(new_version);
        }
        initScreen();
#endif // NO_UPGRADE

    } else if (inBox (s, wifi_b)) {
        // depends on what is currently displayed
        switch (rot_msg) {
        case ROTM_RSSI:
            plotWiFiHistory();
            break;
        case ROTM_CPUTEMP:
            plotCPUTempHistory();
            break;
        case ROTM_FSUSE:        // fallthru
        case ROTM_LIP:  // fallthru
        case ROTM_PIP:
        case ROTM_BE:
        case ROTM_BEIP:
            // sorry, nothing fun
            break;
        case ROTM_N:
            break;
        }
    } else if (overRSS(s)) {
        checkRSSTouch();
    }
}

/* set new DX location from ll in dx_info.
 * use the given grid, else look up from ll.
 * also set override prefix unless NULL
 */
void newDX (LatLong &ll, const char grid[MAID_CHARLEN], const char *ovprefix)
{
    // require password if set
    if (!askPasswd("newdx", true))
        return;

    // disable the sat info 
    if (dx_info_for_sat) {
        dx_info_for_sat = false;
        drawOneTimeDX();
    }

    // set grid and TZ
    ll.normalize();
    if (grid)
        NVWriteString (NV_DX_GRID, grid);
    else
        setNVMaidenhead (NV_DX_GRID, ll);
    setTZAuto (dx_tz);
    NVWriteTZ (NV_DX_TZ, dx_tz);

    // set new location
    dx_ll = ll;
    ll2s (dx_ll, dx_c.s, DX_R);

    // persist
    NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
    NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);

    // set DX prefix
    if (ovprefix)
        setDXPrefixOverride (ovprefix);
    else
        unsetDXPrefixOverride();

    // enable great path unless very close to DE
    dxpath_time = ERAD_M * dx_ll.GSD(de_ll) > DEDX_MINPATH ? millis() : 0;
    scheduleMapRedraw();

    // just call initEarthMap??
    drawDXInfo ();
    drawAllSymbols();

    // show DX weather and update band conditions if showing
    scheduleNewPlot(PLOT_CH_BC);
    scheduleNewPlot(PLOT_CH_DXWX);

    // log
    char dx_grid[MAID_CHARLEN];
    getNVMaidenhead (NV_DX_GRID, dx_grid);
    Serial.printf ("New DX: %g %g %s\n", dx_ll.lat_d, dx_ll.lng_d, dx_grid);
}

/* set new DE location, and optional grid
 */
void newDE (LatLong &ll, const char grid[MAID_CHARLEN])
{
    // require password if set
    if (!askPasswd("newde", true))
        return;

    // set grid and TZ
    ll.normalize();
    if (grid)
        NVWriteString (NV_DE_GRID, grid);
    else
        setNVMaidenhead (NV_DE_GRID, ll);
    setTZAuto (de_tz);
    NVWriteTZ (NV_DE_TZ, de_tz);

    // sat path will change, stop gimbal and require op to start
    stopGimbalNow();

    // new DE
    de_ll = ll;

    // intentionally do not show great path to DX when changing DE
    dxpath_time = 0;

    // persist
    NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
    NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

    // new map
    initEarthMap();

    // more updates that depend on DE regardless of projection
    scheduleNewPlot(PLOT_CH_MOON);
    scheduleNewPlot(PLOT_CH_SDO);
    scheduleNewPlot(PLOT_CH_BC);
    scheduleNewPlot(PLOT_CH_ECLIPSE);
    scheduleNewPlot(PLOT_CH_PSK);
    scheduleNewPlot(PLOT_CH_DEWX);
    scheduleNewCoreMap(core_map);
    sendDXClusterDELLGrid();
    if (setNewSatCircumstance())
        drawSatPass();

    // log
    char de_grid[MAID_CHARLEN];
    getNVMaidenhead (NV_DE_GRID, de_grid);
    Serial.printf ("New DE: %g %g %s\n", de_ll.lat_d, de_ll.lng_d, de_grid);

    // lightning data was relative to old DE — force immediate refetch
    resetLightning();
}

/* find long- or short-path angular distance and east-of-north bearing from_ll to_ll given helper
 * values for sin and cos of from lat. all values in radians in range 0..2pi.
 */
void propPath (bool long_path, const LatLong &from_ll, float sflat, float cflat, const LatLong &to_ll,
float *distp, float *bearp)
{
    // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
    // *bearp will be short-path from to ll east-to-north in radians, -pi..pi
    float cdist;
    solveSphere (to_ll.lng-from_ll.lng, M_PI_2F-to_ll.lat, sflat, cflat, &cdist, bearp);

    if (long_path) {
        *distp = 2*M_PIF - acosf(cdist);              // long path can be anywhere 0..2pi
        *bearp = fmodf (*bearp + 3*M_PIF, 2*M_PIF);   // +180 then clamp to 0..2pi
    } else {
        *distp = acosf(cdist);                        // short part always 0..pi
        *bearp = fmodf (*bearp + 2*M_PIF, 2*M_PIF);   // shift -pi..pi to 0..2pi
    }
}

/* handy shortcut path for starting from DE
 */
void propDEPath (bool long_path, const LatLong &to_ll, float *distp, float *bearp)
{
    return (propPath (long_path, de_ll, sdelat, cdelat, to_ll, distp, bearp));
}

/* convert the given true bearing in degrees [0..360) at ll to desired units.
 * return whether desired units are magnetic, so no change in bear if return false.
 */
bool desiredBearing (const LatLong &ll, float &bear)
{

    if (useMagBearing()) {
        float decl;
        time_t t0 = nowWO();
        float yr = year(t0) + ((month(t0)-1) + (day(t0)-1)/30.0F)/12.0F;        // approx
        if (!magdecl (ll.lat_d, ll.lng_d, 200, yr, &decl)) {
            Serial.printf ("Magnetic model only valid %g .. %g\n", decl, decl+5);
            return (false);
        } else if (!isfinite(decl)) {
            // Never allow a bad model result to turn a valid true bearing into NaN.
            Serial.printf ("Invalid magnetic declination at %g, %g\n", ll.lat_d, ll.lng_d);
            return (false);
        } else {
            // Serial.printf ("magdecl @ %g = %g\n", yr, decl);
            bear = fmodf (bear - decl + 360, 360);
            return (true);
        }
    }
    return (false);
}

/* draw great circle through DE and DX unless too close
 */
void drawDXPath ()
{
    // find short-path bearing and distance from DE to DX
    float dist, bear;
    propDEPath (false, dx_ll, &dist, &bear);

    // walk great circle path from DE through DX with segment lengths PATH_SEGLEN
    float ca, B;
    SCoord s0 = {0, 0}, s1;
    uint16_t sp_col = getMapColor(SHORTPATH_CSPR);
    uint16_t lp_col = getMapColor(LONGPATH_CSPR);
    bool sp_dashed = getPathDashed(SHORTPATH_CSPR);
    bool lp_dashed = getPathDashed(LONGPATH_CSPR);
    int sp_wid = getRawPathWidth(SHORTPATH_CSPR);
    int lp_wid = getRawPathWidth(LONGPATH_CSPR);
    int edge = (sp_wid > lp_wid ? sp_wid : lp_wid) + 2;
    bool dash_toggle = false;
    for (float b = 0; b < 2*M_PIF; b += deg2rad(PATH_SEGLEN)) {
        solveSphere (bear, b, sdelat, cdelat, &ca, &B);
        ll2sRaw (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s1, edge);
        bool sp = b < dist;
        bool show_seg= sp ? (sp_wid && (!sp_dashed || dash_toggle)) : (lp_wid && (!lp_dashed || dash_toggle));
        if (s0.x) {
            if (segmentSpanOkRaw (s0, s1, tft.SCALESZ)) {
                if (show_seg)
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, sp ? sp_wid : lp_wid, sp ? sp_col : lp_col);
            } else {
                s1.x = 0;
            }
        }
        s0 = s1;
        dash_toggle = !dash_toggle;
    }
}

/* return whether we are waiting for a DX path to linger.
 */
bool waiting4DXPath()
{
    return (dxpath_time > 0 && millis() - dxpath_time < DXPATH_LINGER);
}


/* change call sign to ON AIR as long as ONAIR_PIN is low
 */
static void checkOnAirPin()
{
    // force on for debugging else read IO line which is grounded when active
    bool on_now = debugLevel (DEBUG_RIG, 5) || readMCPPoller (ONAIR_PIN) == false;
    setOnAirHW (on_now);
}

// handy
void fillSBox (const SBox &box, uint16_t color)
{
    tft.fillRect (box.x, box.y, box.w, box.h, color);
}
void drawSBox (const SBox &box, uint16_t color)
{
    tft.drawRect (box.x, box.y, box.w, box.h, color);
}

/* draw the given string in the given color anchored at x0,y0 with optional black shadow.
 */
void shadowString (const char *str, bool shadow, uint16_t color, uint16_t x0, uint16_t y0)
{
    if (shadow && color != RA8875_BLACK) {
        tft.setTextColor (RA8875_BLACK);
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx != 0 && dy != 0) {
                    tft.setCursor (x0 + dx, y0 + dy);
                    tft.print (str);
                }
            }
        }
    }

    tft.setTextColor (color);
    tft.setCursor (x0, y0);
    tft.print (str);
}

#if !defined(NO_UPGRADE)
/* return whether this is a beta version
 */
static bool weAreBeta (void)
{
    return (strchr (hc_version, 'b') != NULL);
}
#endif // !NO_UPGRADE

/* draw current version if desired and occasionally check for new.
 */
static void drawVersion (bool draw)
{

#if !defined(NO_UPGRADE)

    // occasionally check for new version, much more often if beta
    const uint32_t checkv_dt = weAreBeta() ? 5*60*1000UL : 6*3600*1000UL;   // millis
    static uint32_t checkv_t;
    if (!new_avail && timesUp (&checkv_t, checkv_dt)) {
        new_avail = newVersionIsAvailable (new_version, sizeof(new_version));
        if (new_avail) {
            Serial.printf ("found new version %s available\n", new_version);
            draw = true;
        }
    }

    // perform update if scheduled
    if (new_avail) {

        // get desired local time of automatic upgrade, if desired
        int update_hour;
        if (autoUpgrade(update_hour)) {

            // user's now
            time_t now = nowWO();

            // establish initial check time at random moment into requested hour.
            // N.B. may or may not allow upgrade this time if currently within the appointed hour
            static time_t next_check;
            if (next_check == 0) {
                // init using local then convert back to utc, insuring first test is always in the future
                int tz = getTZ(de_tz);
                time_t local = now + tz;
                next_check = local - (local%(24*3600)) + 3600*update_hour + random(3600) - tz;
                while (now > next_check)
                    next_check += 24*3600;
                Serial.printf ("initial upgrade check at %ld for hour %d\n", (long)next_check, update_hour);
            }

            // ask when reach next_check then advance to next day if declined
            if (now > next_check) {

                // check for an even newer version
                char newer_ver[20];
                if (newVersionIsAvailable (newer_ver, sizeof(newer_ver)) && strcmp (newer_ver, new_version)) {
                    strcpy (new_version, newer_ver);
                    Serial.printf ("found even newer upgrade version %s\n", new_version);
                }

                Serial.printf ("auto upgrading to %s %ld > %ld hour %d\n",
                                                    new_version, now, next_check, hour(now+getTZ(de_tz)));
                if (askOTAupdate (new_version, true, true) && askPasswd ("upgrade", false))
                    doOTAupdate(new_version);               // never returns

                // declined, so advance to next day
                next_check += 24*3600;
                Serial.printf ("next upgrade check at %ld\n", (long)next_check);

                // N.B. must follow next_check update because it calls us!
                initScreen();
            }
        }

    }

#endif // !NO_UPGRADE

    // draw if desired
    if (draw) {
        // show current version, highlight if new version is available
        char ver[50];
        uint16_t col = new_avail ? RA8875_RED : GRAY;
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        snprintf (ver, sizeof(ver), "V%s", hc_version);
        uint16_t vw = getTextWidth (ver);
        tft.setTextColor (col);
        tft.setCursor (version_b.x+version_b.w-vw, version_b.y+1);      // right justify
        // clear from cs_info.box bottom down through status box to catch any descenders
        tft.fillRect (version_b.x, cs_info.box.y+cs_info.box.h, version_b.w, CSINFO_DROP+CSINFO_H+2, RA8875_BLACK);
        // drawSBox (version_b, RA8875_GREEN);         // RBF
        tft.print (ver);
    }
}

/* draw one of several possible rotating message beneath the call sign.
 */
static void drawRotatingMessage()
{
    // just once every few seconds is fine
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 5000))
        return;

    // default color, cases might change
    tft.setTextColor (GRAY);

    // rotate through the possibilities until one works, ie, puts something into str[]
    // N.B. we assume at least one case will do this!
    char str[40] = "";                          // \0 until known
    bool show_red = false;                      // warning

    do {

        // next
        rot_msg = (RotMsg_t)((((int)rot_msg)+1) % ROTM_N);

        switch (rot_msg) {

        case ROTM_RSSI: {

            static int prev_logv;               // only log large changes
            static int rssi_avg;                // running rssi mean

            // read
            int rssi;
            bool is_dbm;
            if (millis() > 30000 && readWiFiRSSI(rssi, is_dbm)) {    // initial ignore, reports of flakeness

                // blend, or use as-is if first time
                rssi_avg = prev_logv == 0 ? rssi : roundf(rssi*RSSI_ALPHA + rssi_avg*(1-RSSI_ALPHA));
                bool ok = (is_dbm && rssi_avg >= MIN_WIFI_DBM) || (!is_dbm && rssi_avg >= MIN_WIFI_PERCENT);

                // display value
                snprintf (str, sizeof(str), "WiFi %4d%s", rssi_avg, is_dbm ? " dBm" : "%");
                show_red = !ok;

                // log if changed more than a few
                if (abs(rssi_avg-prev_logv) > 3) {
                    Serial.printf ("RSSI %d\n", rssi_avg);
                    prev_logv = rssi_avg;
                }
            }
            }

            break;

        case ROTM_LIP: {

            IPAddress ip = WiFi.localIP();
            bool net_ok = ip[0] != '\0';
            if (net_ok) {
                snprintf (str, sizeof(str), "L-IP %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            } else {
                strcpy (str, "No Network");
                tft.setTextColor (RA8875_RED);
            }
            }

            break;

        case ROTM_PIP:

            if (showPIP()) {
                if (remote_addr[0]) {
                    snprintf (str, sizeof(str), "P-IP %s", remote_addr);
                } else {
                    strcpy (str, "No Network");
                    tft.setTextColor (RA8875_RED);
                }
            }

            break;

        case ROTM_CPUTEMP: {

            float T;
            if (getCPUTemp (T)) {
                if (showTempC())
                    snprintf (str, sizeof(str), "CPU %.2f C", T);
                else
                    snprintf (str, sizeof(str), "CPU %.2f F", 1.8F*T+32);
            }
            }

            break;

        case ROTM_FSUSE: {

            DSZ_t cap, used;
            if (getFSSize (cap, used)) {
                bool really_is_full;
                snprintf (str, sizeof(str), "Disk %llu%% full", 100*used/cap);
                show_red = checkFSFull (really_is_full);
                if (really_is_full)
                    fatalError ("Disk is full");
            }
            }

            break;

        case ROTM_BE: {

            if (backend_host[0]) {
                snprintf (str, sizeof(str), "%s", backend_host);
            } else {
                strcpy (str, "No host");
                tft.setTextColor (RA8875_RED);
            }
            }

            break;

        case ROTM_BEIP: {
            if (backend_host[0]) {
                struct addrinfo hints, *res;
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET; // Force IPv4 for a simple string
                hints.ai_socktype = SOCK_STREAM;

                // Attempt to resolve the hostname
                if (getaddrinfo(backend_host, NULL, &hints, &res) == 0) {
                    struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
                    char be_addr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(ipv4->sin_addr), be_addr, sizeof(be_addr));
                    snprintf (str, sizeof(str), "BE-IP %s", be_addr);
                    freeaddrinfo(res);
                } else {
                    strcpy(str, "Res Err");
                    tft.setTextColor(RA8875_RED);
                }
            } else {
                strcpy(str, "No host");
                tft.setTextColor(RA8875_RED);
            }
            break;
        }

        case ROTM_N:
            // lint -- never get here
            break;

        }

    } while (str[0] == '\0');                                   // try another until one works

    // show
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (show_red ? RA8875_RED : GRAY);
    uint16_t sw = getTextWidth(str);
    // clear from cs_info.box bottom down through status box to catch any descenders
    tft.fillRect (wifi_b.x, cs_info.box.y+cs_info.box.h, wifi_b.w, CSINFO_DROP+CSINFO_H+2, RA8875_BLACK);
    tft.setCursor (wifi_b.x+(wifi_b.w-sw)/2, wifi_b.y+1);
    tft.print(str);
}

/* set Up label and erase current time in uptime_b.
 * N.B. leave text cursor just after "Up"
 */
static void prepUptime()
{

    const uint16_t x = uptime_b.x+UPTIME_INDENT;
    const uint16_t y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    const uint16_t w = uptime_b.w - UPTIME_INDENT;

    // clear from cs_info.box bottom down through status box to catch any descenders
    tft.fillRect (x, cs_info.box.y+cs_info.box.h, w, CSINFO_DROP+CSINFO_H+2, RA8875_BLACK);             // Skip "Up"
    // drawSBox (uptime_b, RA8875_GREEN);                       // RBF

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    tft.setCursor (uptime_b.x, y+1);
    tft.print ("Up ");
}

/* return uptime in seconds, 0 if not ready yet.
 * already break into components if not NULL
 */
time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs)
{
    // "up" is elapsed time since first good value
    static time_t start_s;

    // get time now from NTP
    time_t now_s = myNow();
    if (now_s < 1490000000L)            // March 2017
        return (0);                     // not ready yet

    // get secs since starts_s unless first call or time ran backwards?!
    if (start_s == 0 || now_s < start_s)
        start_s = now_s;
    time_t up0 = now_s - start_s;

    // break out if interested
    if (days && hrs && mins && secs) {
        time_t up = up0;
        *days = up/SECSPERDAY;
        up -= *days*SECSPERDAY;
        *hrs = up/3600;
        up -= *hrs*3600;
        *mins = up/60;
        up -= *mins*60;
        *secs = up;
    }

    // return up secs
    return (up0);

}

/* draw time since boot in uptime_b.
 * keep drawing to a minimum and get out fast if no change unless force.
 */
static void drawUptime(bool force)
{
    // only do the real work once per second
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 1000))
        return;

    // only redraw if significant chars change
    static uint8_t prev_m = 99, prev_h = 99;

    // get uptime, bail if not ready yet.
    uint16_t days; uint8_t hrs, mins, secs;
    time_t upsecs = getUptime (&days, &hrs, &mins, &secs);
    if (!upsecs)
        return;

    // draw two most significant units if change
    if (upsecs < 60) {
        prepUptime();
        tft.printf ("    %2ds", upsecs);
    } else if (upsecs < 3600) {
        prepUptime();
        tft.printf ("%2dm %2ds", mins, secs);
    } else if (upsecs < SECSPERDAY) {
        if (mins != prev_m || force) {
            prepUptime();
            tft.printf ("%2dh %2dm", hrs, mins);
            prev_m = mins;
        }
    } else if (upsecs < SECSPERDAY+60) {
        prepUptime();
        tft.printf ("%2dd %2ds", days, secs);
    } else if (upsecs < SECSPERDAY+3600) {
        if (mins != prev_m || force) {
            prepUptime();
            tft.printf ("%2dd %2dm", days, mins);
            prev_m = mins;
        }
    } else if (hrs != prev_h || force) {
        prepUptime();
        tft.printf ("%2dd %2dh", days, hrs);
        prev_h = hrs;
    }
}

/* update gray status items under callsign (uptime, rotating message, version)
 */
void updateCallsignStatus (bool force)
{
    FontWeight fw;
    FontSize fs;
    getFontStyle (&fw, &fs);

    drawUptime (force);
    drawRotatingMessage ();
    drawVersion (force);

    selectFontStyle (fw, fs);
}

/* given an SCoord in raw coords, return one in app coords
 */
const SCoord raw2appSCoord (const SCoord &s_raw)
{
    SCoord s_app;
    s_app.x = s_raw.x/tft.SCALESZ;
    s_app.y = s_raw.y/tft.SCALESZ;
    return (s_app);
}

/* return whether coordinate s is over the maidenhead key around the edges.
 * N.B. assumes key is only shown in mercator projection.
 */
static bool overMaidKey (const SCoord &s)
{
    return (map_proj == MAPP_MERCATOR
                        && (mapgrid_choice == MAPGRID_MAID || mapgrid_choice == MAPGRID_MAID4)
                        && (inBox(s,maidlbltop_b) || inBox(s,maidlblright_b)) );
}

/* return whether s is over the map globe, depending on the current projection.
 */
static bool overGlobe (const SCoord &s)
{
    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
            // two adjacent hemispheres
            int map_r = map_b.w/4;
            int dy = (int)s.y - (int)(map_b.y + map_b.h/2);
            if (s.x < map_b.x + map_b.w/2) {
                // left globe
                int dx = (int)s.x - (int)(map_b.x + map_b.w/4);
                return (dx*dx + dy*dy <= map_r*map_r);
            } else {
                // right globe
                int dx = (int)s.x - (int)(map_b.x + 3*map_b.w/4);
                return (dx*dx + dy*dy <= map_r*map_r);
            }
        }

    case MAPP_AZIM1: {
            // one centered globe
            int map_r = map_b.w/4;
            int dy = (int)s.y - (int)(map_b.y + map_b.h/2);
            int dx = (int)s.x - (int)(map_b.x + map_b.w/2);
            return (dx*dx + dy*dy <= map_r*map_r);
        }

    case MAPP_MERCATOR:
        // full map_b
        return (inBox(s,map_b));

    case MAPP_ROB: {
            LatLong ll;
            return (s2llRobinson (s, ll));
        }

    default:
        fatalError ("overGlobe bogus projection %d", map_proj);
        return (false);         // lint 
    }
}

/* return whether coordinate s is over an active map scale
 */
bool overMapScale (const SCoord &s)
{
    return (mapScaleIsUp() && inBox(s,mapscale_b));
}

/* return whether coordinate s is over a usable map location
 */
bool overMap (const SCoord &s)
{
    return (overGlobe(s) && !overRSS(s) && !inBox(s,view_btn_b) && !overMaidKey(s) && !overMapScale(s));
}

/* return whether box b is over a usable map location
 */
bool overMap (const SBox &b)
{
    SCoord s00 = {(uint16_t) b.x,         (uint16_t) b.y};
    SCoord s01 = {(uint16_t) (b.x + b.w), (uint16_t) b.y};
    SCoord s10 = {(uint16_t) b.x,         (uint16_t) (b.y + b.h)};
    SCoord s11 = {(uint16_t) (b.x + b.w), (uint16_t) (b.y + b.h)};

    return (overMap(s00) && overMap(s01) && overMap(s10) && overMap(s11));
}


/* draw all symbols, order establishes layering priority
 */
void drawAllSymbols()
{
    updateClocks(false);

    if (mapScaleIsUp())
        drawMapScale();
    drawCountryBorders();
    if (overMap(sun_c.s))
        drawSun();
    if (overMap(moon_c.s))
        drawMoon();
    updateBeacons(true);
    if (!overRSS(deap_c.s))
        drawDEAPMarker();
    drawOnTheAirSpotsOnMap();
    drawDXClusterSpotsOnMap();
    drawADIFSpotsOnMap();
    drawDXPedsOnMap();
    drawHamsatOnMap();
    drawMeshtasticOnMap();
    drawPlanetsOnMap();
    drawLaunchesOnMap();
    drawActiveNetsOnMap();
    drawDEMarker(false);
    drawDXMarker(false);
    drawFarthestPSKSpots();
    drawLightningOnMap();
    drawStormsOnMap();
    drawSatPathAndFoot();
    drawSanta ();
    drawEnterprise ();

    updateClocks(false);
}

/* return whether coordinate s is over an active RSS region.
 */
bool overRSS (const SCoord &s)
{
    return (rss_on && inBox (s, rss_bnr_b));
}

/* return whether box b is over an active RSS banner
 */
bool overRSS (const SBox &b)
{
    return (rss_on && boxesOverlap (b, rss_bnr_b));
}

/* log for sure and write another line to the initial screen if verbose.
 * verbose messages are arranged in two columns. screen row is advanced afterwards unless this and
 *    previous line ended with \r, and fmt == NULL forces return to normal row advancing for next call.
 */
void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...)
{
    // setup
    #define MSG_NROWS   11              // actually 1- because we preincrement row
    #define MSG_ROWH    35
    #define MSG_COL1_X  10
    #define MSG_COL2_X  (tft.width()/2)
    #define MSG_ROW1_Y  100             // coordinate with _initial_ cs_info
    static uint8_t row, col;            // counters, not pixels
    static bool prev_hold;              // whether previous message stayed on row
    StackMalloc msg_buf(300);
    char *buf = (char *) msg_buf.getMem();

    // NULL fmt signals return to normal row advancing
    if (!fmt) {
        prev_hold = false;
        return;
    }

    // format msg
    va_list ap;
    va_start(ap, fmt);
    int l = vsnprintf (buf, msg_buf.getSize(), fmt, ap);
    va_end(ap);

    // note whether \r
    bool hold = buf[l-1] == '\r';
    // Serial.printf ("tftMsg hold= %d %s\n", hold, buf);

    // rm any \n and \r
    if (buf[l-1] == '\n' || buf[l-1] == '\r')
        buf[--l] = '\0';

    // log
    Serial.printf ("TFTMSG: %s\n", buf);

    // done unless verbose
    if (!verbose)
        return;


    // advance unless this and prev hold
    if (!(hold && prev_hold)) {
        if (++row == MSG_NROWS) {
            row = 1;
            col++;
        }
    }

    // set location
    uint16_t x = col ? MSG_COL2_X : MSG_COL1_X;
    uint16_t y = MSG_ROW1_Y + row*MSG_ROWH;

    // erase if this is another non-advance
    if (hold && prev_hold)
        tft.fillRect (x, y-(MSG_ROWH-9), tft.width()/2, MSG_ROWH, RA8875_BLACK);

    // draw
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (x, y);
    tft.print(buf);

    if (dwell_ms)
        wdDelay (dwell_ms);

    // record
    prev_hold = hold;
}

/* toggle the NV_LKSCRN_ON value
 */
static void toggleLockScreen()
{
    uint8_t lock_on = !screenIsLocked();
    Serial.printf ("Screen lock is now %s\n", lock_on ? "On" : "Off");
    NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
}

/* set the lock screen state.
 * then draw iff showing main display.
 */
void setScreenLock (bool on)
{
    if (on != screenIsLocked()) {
        toggleLockScreen();
        if (mainpage_up)
            drawScreenLock();
    }
}

/* draw, or erase, the demo runner
 */
void drawDemoRunner()
{
    fillSBox (demo_b, RA8875_BLACK);

    if (getDemoMode()) {

        // runner icon at full res

        const uint16_t rx = tft.SCALESZ*demo_b.x;
        const uint16_t ry = tft.SCALESZ*demo_b.y;

        for (uint16_t dy = 0; dy < HC_RUNNER_H; dy++)
            for (uint16_t dx = 0; dx < HC_RUNNER_W; dx++)
                tft.drawPixelRaw (rx+dx, ry+dy, runner[dy*HC_RUNNER_W + dx]);

    }
}

/* draw the lock screen symbol according to NV_LKSCRN_ON.
 */
void drawScreenLock()
{
    fillSBox (lkscrn_b, RA8875_BLACK);

    uint16_t hh = lkscrn_b.h/2;
    uint16_t hw = lkscrn_b.w/2;

    tft.fillRect (lkscrn_b.x, lkscrn_b.y+hh, lkscrn_b.w, hh, RA8875_WHITE);
    tft.drawLine (lkscrn_b.x+hw, lkscrn_b.y+hh+2, lkscrn_b.x+hw, lkscrn_b.y+hh+hh/2, 2, RA8875_BLACK);
    tft.drawCircle (lkscrn_b.x+hw, lkscrn_b.y+hh, hw, RA8875_WHITE);

    if (!screenIsLocked())
        tft.fillRect (lkscrn_b.x+hw+2, lkscrn_b.y, hw, hh, RA8875_BLACK);
}

/* offer menu of DE format options and engage selection
 */
void drawDEFormatMenu()
{
    // minor and Major indent
    const int mi = 2;
    const int Mi = 8;

    // number of core menu items
    #define N_DEFMT_CORE        8

    MenuItem mitems[N_DEFMT_CORE+N_PANE_0_CH] = {

        // outer menu is whether to display the DE pane or use both DE+DX for a pane choice
        {MENU_1OFN, !SHOWING_PANE_0(), 1, mi, "DE format:", NULL},

            // top submenu is list of de display formats
            {MENU_1OFN, de_time_fmt == DETIME_INFO,        2, Mi, detime_names[DETIME_INFO], NULL},
            {MENU_1OFN, de_time_fmt == DETIME_ANALOG,      2, Mi, detime_names[DETIME_ANALOG], NULL},
            {MENU_1OFN, de_time_fmt == DETIME_CAL,         2, Mi, detime_names[DETIME_CAL], NULL},
            {MENU_1OFN, de_time_fmt == DETIME_ANALOG_DTTM, 2, Mi, detime_names[DETIME_ANALOG_DTTM], NULL},
            {MENU_1OFN, de_time_fmt == DETIME_DIGITAL_12,  2, Mi, detime_names[DETIME_DIGITAL_12], NULL},
            {MENU_1OFN, de_time_fmt == DETIME_DIGITAL_24,  2, Mi, detime_names[DETIME_DIGITAL_24], NULL},

        {MENU_1OFN, SHOWING_PANE_0(), 1, mi, "Data Panes:", NULL},

            // bottom submenu is filled below from PANE_0_CH_MASK, including high-word choices.
    };

    // prepare each suitable Pane 0 choice in PlotChoice order
    PlotChoice menu_ch[N_PANE_0_CH];
    int n_menu_ch = 0;
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice pc = (PlotChoice)i;
        if (!PLOT_CH_IS_REAL(pc) || !(PANE_0_CH_MASK & PLOTBIT(pc)))
            continue;

        menu_ch[n_menu_ch] = pc;
        PlotPane pp = findPaneForChoice (pc);
        bool available = plotChoiceIsAvailable(pc) && (pp == PANE_NONE || pp == PANE_0);
        MenuFieldType type = available ? MENU_AL1OFN : MENU_IGNORE;
        mitems[N_DEFMT_CORE+n_menu_ch] = {
            type, !!(plot_rotset[PANE_0] & PLOTBIT(pc)), 3, Mi, plot_names[pc], 0
        };
        n_menu_ch++;
    }
    if (n_menu_ch != N_PANE_0_CH)
        fatalError ("drawDEFormatMenu() found %d of %d Pane 0 choices", n_menu_ch, N_PANE_0_CH);

    // create a box for the menu
    SBox menu_b;
    menu_b.x = de_info_b.x + 4;
    menu_b.y = de_info_b.y;
    menu_b.w = 0;       // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_NOCLOCKS, M_CANCELOK, 1, NARRAY(mitems), mitems};
    if (!runMenu (menu))
        return;

    if (mitems[0].set) {

        // set desired DE time format and restore the normal DE/DX layout
        int new_fmt = -1;
        for (int i = 0; i < DETIME_N; i++) {
            if (mitems[i+1].set) {
                new_fmt = i;
                break;
            }
        }
        if (new_fmt < 0)
            fatalError ("drawDEFormatMenu: No de fmt");

        de_time_fmt = (uint8_t)new_fmt;
        NVWriteUInt8 (NV_DE_TIMEFMT, de_time_fmt);
        restoreNormPANE0();

    } else {

        // collect low- and high-word choices into one in-memory rotation mask
        PlotMask new_rotset = 0;
        for (int i = 0; i < N_PANE_0_CH; i++) {
            if (mitems[N_DEFMT_CORE+i].set)
                new_rotset |= PLOTBIT(menu_ch[i]);
        }

        if (new_rotset == 0) {
            restoreNormPANE0();
        } else {
            plot_rotset[PANE_0] = new_rotset;

            // retain the current choice when still selected, otherwise choose the first set bit
            if (!(plot_rotset[PANE_0] & PLOTBIT(plot_ch[PANE_0]))) {
                for (int i = 0; i < PLOT_CH_N; i++) {
                    if (plot_rotset[PANE_0] & PLOTBIT(i)) {
                        plot_ch[PANE_0] = (PlotChoice)i;
                        break;
                    }
                }
            }

            if (!setPlotChoice (PANE_0, plot_ch[PANE_0]))
                fatalError ("drawDEFormatMenu: can not set Pane 0 choice %d", (int)plot_ch[PANE_0]);
        }
    }

    logPaneRotSet (PANE_0, plot_ch[PANE_0]);
}

/* resume using nearestPrefix
 */
static void unsetDXPrefixOverride ()
{
    dx_prefix_use_override = false;
}

/* set an override prefix for getDXPrefix() to use instead of using nearestPrefix()
 */
static void setDXPrefixOverride (const char *ovprefix)
{
    // extract
    findCallPrefix (ovprefix, dx_override_prefix);

    // flag ready
    dx_prefix_use_override = true;
}

/* return the override prefix else nearest one based on ll, if any
 */
bool getDXPrefix (char p[MAX_PREF_LEN])
{
    if (dx_prefix_use_override) {
        quietStrncpy (p, dx_override_prefix, MAX_PREF_LEN);
        return (true);
    } else {
        return (ll2Prefix (dx_ll, p));
    }
}

/* this function positions a box to contain short text beneath a symbol at location s that has radius r,
 *   where s is assumed to be over map. the preferred position is centered below s, but it may be moved
 *   above or to either side to avoid going off the map.
 * N.B. r == 0 is a special case to mean center the text exactly at s, not beneath it.
 * N.B. coordinate with drawMapTag()
 */
void setMapTagBox (const char *tag, const SCoord &s, uint16_t r, SBox &box)
{
    // get text size
    uint16_t cw, ch;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    getTextBounds (tag, &cw, &ch);

    // set box size to text with a small margin
    box.w = cw+2;
    box.h = ch+2;

    // init at preferred location
    box.x = s.x - box.w/2;                      // center horizontally at s
    box.y = s.y + (r ? r : -box.h/2);           // below but if r is 0 then center at s

    // check corners
    const uint16_t rx = box.x + box.w;
    const uint16_t ly = box.y + box.h;
    const bool ul_ok = overMap (SCoord{box.x,box.y});
    const bool ur_ok = overMap (SCoord{rx,box.y});
    const bool ll_ok = overMap (SCoord{box.x,ly});
    const bool lr_ok = overMap (SCoord{rx,ly});

    bool recheck = false;

    if (!ul_ok && !ur_ok) {
        // move above
        box.y = s.y - r - box.h;
    } else if (!ul_ok || !ll_ok) {
        // move to centered right
        box.x = s.x + r;
        box.y = s.y - box.h/2;
        recheck = true;
    } else if (!ur_ok || !lr_ok) {
        // move to centered left
        box.x = s.x - r - box.w;
        box.y = s.y - box.h/2;
        recheck = true;
    } else if (map_proj == MAPP_AZIMUTHAL) {
        // if sides are in opposite hemispheres shift to same side as s
        uint16_t mc = map_b.x + map_b.w/2;
        if ((box.x < mc) != (box.x+box.w < mc)) {
            if (s.x < mc) {
                // center box to the left of s
                box.x = s.x - r - box.w;
                box.y = s.y - box.h/2;
            } else {
                // center box to the right of s
                box.x = s.x + r;
                box.y = s.y - box.h/2;
            }
        }
    }

    if (recheck) {
        // lower may still not be visible
        uint16_t bot_y = box.y + box.h;
        uint16_t rit_x = box.x + box.w;
        if (!overMap (SCoord{box.x,bot_y}) || !overMap (SCoord{rit_x,bot_y})) {
            // move above
            box.x = s.x - box.w/2;
            box.y = s.y - r - box.h;
        }
    }
}

/* draw a string within the given box set by setMapTagBox, using the optional color (default white).
 * actually this is no longer a box but text with a shifted background -- more moxy!
 * still, knowing the box has been positioned well allows for this masking.
 * N.B. coordinate with setMapTagBox()
 */
void drawMapTag (const char *tag, const SBox &box, uint16_t txt_color, uint16_t bg_color)
{
    // small font
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // text position
    uint16_t x0 = box.x+1;
    uint16_t y0 = box.y+1;

#if BUILD_W > 800

    // draw background by shifting O's around
    int tag_len = strlen(tag);
    StackMalloc bkg_str(tag_len+1);
    char *bkg = (char*) bkg_str.getMem();
    memset (bkg, 'O', tag_len);
    bkg[tag_len] = '\0';
    tft.setTextColor (bg_color);
    for (int16_t dx = -1; dx <= 1; dx++) {
        for (int16_t dy = -1; dy <= 1; dy++) {
            if (dx || dy) {
                tft.setCursor (x0+dx, y0+dy);
                tft.print(bkg);
            }
        }
    }

    // avoid some bit turds in the center of the Os
    for (unsigned i = 0; i < strlen(tag); i++)
        tft.fillRect (x0+i*6,y0,5,7,bg_color);

#else

    // just too small for finessing
    fillSBox (box, bg_color);
    
#endif

    // draw text
    tft.setTextColor (txt_color);
    tft.setCursor (x0, y0);
    tft.print((char*)tag);
}

/* return whether screen is currently locked
 */
bool screenIsLocked()
{
    uint8_t lock_on;
    if (!NVReadUInt8 (NV_LKSCRN_ON, &lock_on)) {
        lock_on = 0;
        NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
    }
    return (lock_on != 0);
}

/* return whether SCoord is within SBox
 */
bool inBox (const SCoord &s, const SBox &b)
{
    return (s.x >= b.x && s.x < b.x+b.w && s.y >= b.y && s.y < b.y+b.h);
}

/* return whether SCoord is within SCircle.
 * N.B. must match Adafruit_RA8875::fillCircle()
 */
bool inCircle (const SCoord &s, const SCircle &c)
{
    // beware unsigned subtraction
    uint16_t dx = (s.x >= c.s.x) ? s.x - c.s.x : c.s.x - s.x;
    uint16_t dy = (s.y >= c.s.y) ? s.y - c.s.y : c.s.y - s.y;
    return (4*dx*dx + 4*dy*dy <= 4*c.r*(c.r+1)+1);
}

/* return whether any part of b1 and b2 overlap.
 */
bool boxesOverlap (const SBox &b1, const SBox &b2)
{
    return ((b1.x + b1.w >= b2.x && b1.x <= b2.x + b2.w)
         && (b1.y + b1.h >= b2.y && b1.y <= b2.y + b2.h));
}

/* erase the given SCircle
 */
void eraseSCircle (const SCircle &c)
{
    // scan a circle of radius r+1/2 to include whole pixel.
    // radius (r+1/2)^2 = r^2 + r + 1/4 so we use 2x everywhere to avoid floats
    uint16_t radius2 = 4*c.r*(c.r + 1) + 1;
    for (int16_t dy = -2*c.r; dy <= 2*c.r; dy += 2) {
        for (int16_t dx = -2*c.r; dx <= 2*c.r; dx += 2) {
            int16_t xy2 = dx*dx + dy*dy;
            if (xy2 <= radius2)
                drawMapCoord (c.s.x+dx/2, c.s.y+dy/2);
        }
    }
}

/* erase entire screen engage immediate graphical updates
 */
void eraseScreen()
{
    tft.setPR (0, 0, 0, 0);
    tft.fillScreen(RA8875_BLACK);
    tft.drawPR();

    // we assume erasing is prep for some other page
    hideClocks();
    mainpage_up = false;
}

/* like delay() but breaks into small chunks so we can update live web
 */
void wdDelay(int ms)
{
    #define WD_DELAY_DT   50
    uint32_t t0 = millis();
    int dt;
    while ((dt = millis() - t0) < ms) {
        checkWebServer(true);
        if (dt < WD_DELAY_DT)
            delay (dt);
        else
            delay (WD_DELAY_DT);
    }
}

/* handy utility to return whether now is atleast_dt ms later than prev.
 * if so, update *prev and return true, else return false.
 */
bool timesUp (uint32_t *prev, uint32_t atleast_dt)
{
    uint32_t ms = millis();
    uint32_t dt = ms - *prev;   // works ok if millis rolls over
    if (dt > atleast_dt) {
        *prev = ms;
        return (true);
    }
    return (false);
}




/* called to post our diagnostics files to the server for later analysis.
 * include the ip in the name which identifies us using our public IP address.
 * return whether all ok.
 */
bool postDiags (void)
{
    WiFiClient pd_client;
    bool ok = false;

    // build a unique filename relative to hamclock server root dir and id
    char fn[300];
    snprintf (fn, sizeof(fn), "/ham/HamClock/diagnostic-logs/dl-%lld-%s-%u.txt", (long long)myNow(),
                                                remote_addr, ESP.getChipId());

    // get total size of all diag files for content length
    // N.B. DO NOT use Serial after this because it adds to the log file !
    struct stat s;
    int cl = 0;
    for (int i = 0; i < N_DIAG_FILES; i++) {
        std::string dp = our_dir + diag_files[i];
        if (stat (dp.c_str(), &s) == 0)
            cl += s.st_size;
    }

    // add eeprom file
    if (stat (EEPROM.getFilename(), &s) == 0)
        cl += s.st_size;


    if (pd_client.connect (backend_host, backend_port)) {

        char buf[4096];
        int buf_l = 0;

        // hand-crafted POST header, move to its own func if ever used for something else
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "POST %s HTTP/1.0\r\n", fn);
        buf_l += snprintf (buf+buf_l, sizeof(buf)-buf_l, "Content-Length: %d\r\n", cl);
        pd_client.print (buf);
        sendUserAgent (pd_client);
        pd_client.print ("\r\n");

        // just concat each file including eeprom
        for (int i = 0; i <= N_DIAG_FILES; i++) {                       // 1 more for eeprom
            std::string dp = i < N_DIAG_FILES ? our_dir + diag_files[i] : EEPROM.getFilename();
            FILE *fp = fopen (dp.c_str(), "r");
            if (fp) {
                int n_r;
                while ((n_r = fread (buf, 1, sizeof(buf), fp)) > 0)
                    pd_client.write ((uint8_t*)buf, n_r);
                fclose (fp);
            }
        }
        
        pd_client.stop();
        ok = true;

        Serial.printf ("Diag: %d %s\n", cl, fn);

    } else {

        Serial.printf ("postDiags() failed to connect to %s:%d\n", backend_host, backend_port);
        ok = false;
    }


    // ok?
    return (ok);
}


/* fork then run execv(3) command without a shell to retain our rootiness.
 * return exit status from waitpid().
 * N.B. caller must end list with a NULL
 * N.B. tempting to just assume the args are already a list on the stack but we'd rather not.
 */
static int runExecv (const char *path, ...)
{
        int pid = fork();

        if (pid == 0) {
            // child
            char **argv = NULL;
            int n_argv = 0;
            va_list ap;
            va_start (ap, path);
            for(;;) {
                char *arg = va_arg (ap, char *);
                argv = (char **) realloc (argv, (n_argv+1) * sizeof(char *));
                argv[n_argv++] = arg;
                if (arg == NULL)
                    break;
            }
            va_end (ap);
            execv (path, (char **) argv);
            Serial.printf ("execv(%s): %s\n", path, strerror(errno));
            free ((void*)argv);
            _exit(1);
        }

        int status;
        waitpid (pid, &status, 0);
        return (status);
}


/* ask Are You Sure for the given question.
 * return whether yes
 */
static bool RUSure (SBox &box, const char *q)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    const char *r = "Are you sure?";
    uint8_t q_indent = (box.w - getTextWidth(q))/2 - 2;         // - MENU_RM in menu.cpp
    uint8_t r_indent = (box.w - getTextWidth(r))/2 - 2;         //            "
    MenuItem mitems[] = {
        {MENU_BLANK, false, 0, 0, NULL, 0},
        {MENU_LABEL, false, 0, q_indent, q, 0},
        {MENU_BLANK, false, 0, 0, NULL, 0},
        {MENU_LABEL, false, 0, r_indent, r, 0},
        {MENU_BLANK, false, 0, 0, NULL, 0},
    };
    const int n_rusm = NARRAY(mitems);

    SBox ok_b;

    MenuInfo menu = {box, ok_b, UF_NOCLOCKS, M_CANCELOK, 1, n_rusm, mitems};
    return (runMenu (menu));
}

/* offer power down, restart etc 
 */
static void runShutdownMenu(void)
{
    closeGimbal();          // avoid dangling connection

    // if screen is locked, the only menu option is to unlock
    bool locked = screenIsLocked();

    const int SHM_INDENT = 3;
    MenuItem mitems[] = {
        {MENU_TOGGLE,                        locked,        1, SHM_INDENT, "Lock screen ", 0},     // 0
        {locked ? MENU_IGNORE : MENU_TOGGLE, getDemoMode(), 2, SHM_INDENT, "Demo mode", 0},        // 1
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Configurations", 0},   // 2
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Post diagnostics", 0}, // 3
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Restart HamClock", 0}, // 4
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Exit HamClock", 0},    // 5
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Reboot computer", 0},  // 6
        {locked ? MENU_IGNORE : MENU_01OFN,  false,         3, SHM_INDENT, "Shutdown computer", 0},// 7
    };
    const int n_shm = NARRAY(mitems);

    // boxes
    uint16_t menu_x = locked ? 150 : 114;       // just one entry if locked
    uint16_t menu_y = locked ? 105 : 25;        // just one entry if locked
    SBox menu_b = {menu_x, menu_y, 0, 0};       // shrink wrap size
    SBox ok_b;

    // run menu
    bool do_full_init = false;
    MenuInfo menu = {menu_b, ok_b, UF_NOCLOCKS, M_CANCELOK, 1, n_shm, mitems};
    if (runMenu(menu)) {

        // engage each selection

        if (mitems[0].set) {
            // anyone can lock
            setScreenLock (true);
        } else if (locked) {
            // can only unlock if have pw (or no pw)
            if (askPasswd("unlock", true))
                setScreenLock (false);
        }

        setDemoMode (mitems[1].set);
        drawDemoRunner();

        if (mitems[2].set) {
            if (askPasswd ("configurations", true))
                runConfigManagement();
        }

        if (mitems[3].set) {
            if (RUSure (menu_b, mitems[3].label)) {
                menuMsg (menu_b, RA8875_WHITE, "posting...");
                if (postDiags())
                    menuMsg (menu_b, RA8875_GREEN, "posting complete");
                else
                    menuMsg (menu_b, RA8875_RED, "posting failed");
            }
        }

        if (mitems[4].set) {
            if (RUSure (menu_b, mitems[4].label) && askPasswd ("restart", true)) {
                Serial.print ("Restarting\n");
                eraseScreen();  // fast touch feedback
                doReboot(false, false);
            }
        }

        if (mitems[5].set) {
            if (RUSure (menu_b, mitems[5].label) && askPasswd ("exit", true)) {
                Serial.print ("Exiting\n");
                doExit();
            }
        }

        if (mitems[6].set) {
            if (RUSure (menu_b, mitems[6].label) && askPasswd ("reboot", true)) {
                Serial.print ("Rebooting\n");
                eraseScreen();
                selectFontStyle (BOLD_FONT, SMALL_FONT);
                tft.setCursor (350, 200);
                tft.print ("Rebooting...");
                wdDelay (1000);
                int x = runExecv ("/sbin/reboot", "reboot", NULL);
                if (WIFEXITED(x) && WEXITSTATUS(x) == 0)
                    doExit();
                else {
                    Serial.printf ("reboot exited %d\n", x);
                    eraseScreen();
                    tft.setCursor (350, 200);
                    tft.print ("Reboot failed");
                    wdDelay (1000);
                    do_full_init = true;
                }
            }
        }

        if (mitems[7].set) {
            if (RUSure (menu_b, mitems[7].label) && askPasswd ("shutdown", true)) {
                Serial.print ("Shutting down\n");
                eraseScreen();
                selectFontStyle (BOLD_FONT, SMALL_FONT);
                tft.setCursor (350, 200);
                tft.print ("Shutting down...");
                wdDelay (1000);
                int x = runExecv ("/sbin/poweroff", "poweroff", NULL);
                if (WIFEXITED(x) && WEXITSTATUS(x) == 0)
                    doExit();
                else {
                    // well then try halt
                    Serial.printf ("poweroff exited %d, trying halt\n", x);
                    x = runExecv ("/sbin/halt", "halt", NULL);
                    if (WIFEXITED(x) && WEXITSTATUS(x) == 0)
                        doExit();
                    else {
                        Serial.printf ("halt exited %d\n", x);
                        eraseScreen();
                        tft.setCursor (350, 200);
                        tft.print ("Shutdown failed");
                        wdDelay (1000);
                        do_full_init = true;
                    }
                }
            }
        }
    }

    if (do_full_init)
        initScreen();
}


/* reboot
 */
void doReboot(bool minus_K, bool minus_0)
{
    defaultState();
    ESP.restart (minus_K, minus_0);
    for(;;);
}

/* do exit, as best we can
 */
void doExit()
{
    Serial.printf ("doExit()\n");
    defaultState();
    #if defined(_USE_FB0)
        // X11 calls doExit on window close, so drawing would be recursive back to that thread
        eraseScreen();
    #endif
    _exit(0);
}

/* call to display one final message, never returns
 */
void fatalError (const char *fmt, ...)
{
    // this might be called from other than the main thread
    stop_main_thread = true;

    // format message, accommodate really long strings
    const int mem_inc = 500;
    int mem_len = 0;
    char *msg = NULL;
    int msg_len;
    for(bool stop = false; !stop;) {
        msg = (char *) realloc (msg, mem_len += mem_inc);
        if (!msg) {
            // go back to last successful size then stop
            msg = (char *) malloc (mem_len -= mem_inc);
            if (!msg) {
                // no mem at all, at least show fmt
                Serial.printf ("fatalError: %s\n", fmt);
                doExit();
            }
            stop = true;
        }
        va_list ap;
        va_start(ap, fmt);
        msg_len = vsnprintf (msg, mem_len, fmt, ap);
        va_end(ap);
        // stop when all fits
        if (msg_len < mem_len - 10)
            break;
    }

    // log and post diags for sure
    Serial.printf ("Fatal: %s\n", msg);
    postDiags();

    // it may still be very early so wait a short while for display
    for (int i = 0; i < 20; i++) {
        if (tft.displayReady())
            break;
        wdDelay(100);
    }

    // nothing more unless display
    if (tft.displayReady()) {

        // fresh screen
        eraseScreen();

        // prep display
        tft.setTextColor (RA8875_WHITE);
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        const uint16_t line_h = 34;                     // line height
        const uint16_t char_w = 12;                     // char width, approx is ok
        uint16_t y = line_h;                            // walking y coord of font baseline
        tft.setCursor (0, y);

        // display msg with both line and page wrap to insure seeing end
        for (char *mp = msg; *mp; mp++) {
            if (tft.getCursorX() > tft.width() - char_w || *mp == '\n') {
                // drop down a line or wrap back to top, then erase
                if ((y += line_h) > tft.height() - 3*line_h)
                    y = line_h;
                tft.setCursor (0, y);
                tft.fillRect (0, y - line_h + 3, tft.width(), line_h, RA8875_BLACK);
            }
            if (*mp != '\n')
                tft.print (*mp);
        }

        // button font
        selectFontStyle (LIGHT_FONT, SMALL_FONT);

        // draw boxes and wait for click in either
        SBox screen_b;
        screen_b.x = 0;
        screen_b.y = 0;
        screen_b.w = tft.width();
        screen_b.h = tft.height();
        SBox r_b = {250, 400, 100, 50};
        SBox x_b = {450, 400, 100, 50};
        const char r_msg[] = "Restart";
        const char x_msg[] = "Exit";
        bool select_restart = true;

        UserInput ui = {
            screen_b,
            UI_UFuncNone,
            UF_UNUSED,
            UI_NOTIMEOUT,
            UF_NOCLOCKS,
            {0, 0}, TT_NONE, '\0', false, false
        };

        drainTouch();

        // must select restart or exit by some means
        for(;;) {

            // show current selection
            drawStringInBox (r_msg, r_b, select_restart, RA8875_WHITE);
            drawStringInBox (x_msg, x_b, !select_restart, RA8875_WHITE);

            // wait for any user action
            (void) waitForUser(ui);

            if (ui.kb_char == CHAR_LEFT || ui.kb_char == CHAR_RIGHT) {

                // L/R arrow keys appear to move to opposite selection
                select_restart = !select_restart;

            } else {

                bool typed_ok = ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL;

                if ((typed_ok && select_restart) || (ui.kb_char == CHAR_NONE && inBox(ui.tap, r_b))) {
                    drawStringInBox (r_msg, r_b, true, RA8875_WHITE);
                    Serial.print ("Fatal error: rebooting\n");
                    doReboot(false, false);
                }

                if ((typed_ok && !select_restart) || (ui.kb_char == CHAR_NONE && inBox(ui.tap, x_b))) {
                    drawStringInBox (x_msg, x_b, true, RA8875_WHITE);
                    Serial.print ("Fatal error: exiting\n");
                    doExit();
                }
            }
        }
    }

    // bye bye
    free (msg);
    doExit();
}


/* return the worst offending heap and stack
 */
static int worst_heap = 900000000;
static int worst_stack;
void getWorstMem (int *heap, int *stack)
{
    *heap = worst_heap;
    *stack = worst_stack;
}

/* log current heap and stack usage, record worst offenders
 */
void printFreeHeap (const __FlashStringHelper *label)
{
    // compute sizes
    char stack_here;
    int free_heap = ESP.getFreeHeap();
    int stack_used = stack_start - &stack_here;

    // log..
    // getFreeHeap() is close to binary search of max malloc
    // N.B. do not use getUptime here, it loops NTP
    String l(label);
    Serial.printf ("MEM %s(): free heap %d, stack size %d\n", l.c_str(), free_heap, stack_used);

    // record worst
    if (free_heap < worst_heap)
        worst_heap = free_heap;
    if (stack_used > worst_stack)
        worst_stack = stack_used;
}

/* return a high contrast text color to overlay the given background color
 * https://www.w3.org/TR/AERT#color-contrast
 */
uint16_t getGoodTextColor (uint16_t bg_col)
{
    uint8_t r = RGB565_R(bg_col);
    uint8_t g = RGB565_G(bg_col);
    uint8_t b = RGB565_B(bg_col);
    int perceived_brightness = 0.299*r + 0.587*g + 0.114*b;
    uint16_t text_col = perceived_brightness > 70 ? RA8875_BLACK : RA8875_WHITE;
    return (text_col);
}
