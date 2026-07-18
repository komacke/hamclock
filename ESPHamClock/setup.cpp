/* handle the initial setup screen.
 */

#include <ctype.h>

#include "HamClock.h"

/* defaults
 */
#define DEF_SSID        "FiOS-9QRT4-Guest"
#define DEF_PASS        "Veritium2017"

#include <string.h>
#include <errno.h>

// only FB0 needs wifi now, even _WEB_ONLY users should set up their networking first
#if defined (_USE_FB0)
    #if !defined(_WIFI_NEVER)
        #define _WIFI_ALWAYS
    #endif
#elif !defined(_WIFI_NEVER)
    #define _WIFI_NEVER
#endif


// this was originally used on platforms that gave an option whether to set wifi creds
// #define _WIFI_ASK

static bool good_wpa;

// debugs: force all on just for visual testing, and show bounds
// #define _SHOW_ALL                    // RBF
// #define _MARK_BOUNDS                 // RBF
#if defined(_SHOW_ALL) || defined(_MARK_BOUNDS)
#warning _SHOW_ALL and/or _MARK_BOUNDS are set
#endif
#ifdef _SHOW_ALL
    #undef NO_UPGRADE
    #undef _WIFI_NEVER
    #undef _WIFI_ASK
    #define _WIFI_ALWAYS
    #define _SUPPORT_KX3 
    #define _SUPPORT_NATIVE_GPIO
#endif // _SHOW_ALL


// static storage for published setup items
static char wifi_ssid[NV_WIFI_SSID_LEN];
static char wifi_pw[NV_WIFI_PW_LEN];
static char dx_login[NV_DXLOGIN_LEN];
static char hamsat_key[NV_HAMSATKEY_LEN];
static char dx_host[NV_DXHOST_LEN];
static char rot_host[NV_ROTHOST_LEN];
static char rig_host[NV_RIGHOST_LEN];
static char flrig_host[NV_FLRIGHOST_LEN];
static char gpsd_host[NV_GPSDHOST_LEN];
static char nmea_file[NV_NMEAFILE_LEN];
static char ntp_host[NV_NTPHOST_LEN];
static uint8_t bright_min, bright_max;
static uint16_t dx_port;
static uint16_t rig_port, rot_port, flrig_port;
static float temp_corr[MAX_N_BME];
static float pres_corr[MAX_N_BME];
static int16_t center_lng;
static int16_t alt_center_lng;
static bool alt_center_lng_set;
static char dxcl_cmds[N_DXCLCMDS][NV_DXCLCMD_LEN];
static char dx_wlist[NV_DXWLIST_LEN];
static char adif_wlist[NV_ADIFWLIST_LEN];
static char adif_fn[NV_ADIFFN_LEN];
static char onta_wlist[NV_ONTAWLIST_LEN];
static char i2c_fn[NV_I2CFN_LEN];

// default DX cluster login SSID number
#define DXC_DEF_SSID 55
#define DXC_MAX_SSID 99

// layout constants
#define NQR             4                       // number of virtual keyboard rows
#define NQC             13                      // max number of keyboard columns
#define KB_CHAR_H       44                      // height of box containing 1 keyboard character
#define KB_CHAR_W       59                      // width "
#define KB_SPC_Y        (KB_Y0+NQR*KB_CHAR_H)   // top edge of special keyboard chars
#define KB_SPC_H        28                      // heights of special keyboard chars
#define KB_INDENT       16                      // keyboard indent
#define SBAR_X          (KB_INDENT+5*KB_CHAR_W/2)// space bar x coord
#define SBAR_W          (KB_CHAR_W*8)           // space bar width
#define F_DESCENT       5                       // font descent below baseline
#define TF_INDENT       8                       // top row font indent within square
#define BF_INDENT       34                      // bottom font indent within square
#define KB_Y0           264                     // y coord of keyboard top
#define TT_Y0           (480-9)                 // y coord of tooltip reminder
#define PR_W            18                      // width of character
#define PR_A            24                      // ascending height above font baseline
#define PR_D            9                       // descending height below font baseline
#define PR_H            (PR_A+PR_D)             // prompt height
#define ASK_TO          10                      // user option timeout, seconds
#define PAGE_W          120                     // page button width
#define PAGE_H          33                      // page button height
#define CURSOR_DROP     2                       // pixels to drop cursor
#define NVMS_MKMSK      0x3                     // NV_LBLSTYLE mark mask -- legacy prior to 4.10
#define R2Y(r)          ((r)*(PR_H+1))          // macro given row index from 0 return screen y
#define ERRDWELL_MS     2000                    // err message dwell time, ms
#define BTNDWELL_MS     200                     // button feedback dwell time, ms

// color selector layout
#define CSEL_SCX        435                     // all color control scales x coord
#define CSEL_COL1X      2                       // col 1 x (where editing tick box goes)
#define CSEL_COL2X      415                     // col 2 x (where editing tick box goes)
#define CSEL_SCY        45                      // top scale y coord
#define CSEL_SCW        256                     // color scale width -- lt this causes roundoff at end
#define CSEL_SCH        30                      // color scale height
#define CSEL_NW         50                      // number value width
#define CSEL_SCYG       15                      // scale y gap
#define CSEL_VDX        20                      // gap dx to value number
#define CSEL_VX         (CSEL_SCX+CSEL_SCW+CSEL_VDX)            // rgb values x
#define CSEL_VYR        (CSEL_SCY)                              // red value y
#define CSEL_VYG        (CSEL_SCY+CSEL_SCH+CSEL_SCYG)           // green value y
#define CSEL_VYB        (CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG)       // blue value y
#define CSEL_SCM_C      RA8875_WHITE            // scale marker color
#define CSEL_SCB_C      GRAY                    // scale slider border color
#define CSEL_PDX        120                     // prompt dx from tick box x
#define CSEL_PW         140                     // width
#define CSEL_DDX1       260                     // demo strip dx from tick box x col 1
#define CSEL_DDX2       230                     // demo strip dx from tick box x col 2
#define CSEL_DW         120                     // demo strip width
#define CSEL_DH         10                      // demo strip height place holder -- depends on t_state
#define CSEL_TBCOL      RA8875_GREEN            // tick box active color
#define CSEL_TBSZ       20                      // tick box size
#define CSEL_TBDY       5                       // tick box y offset from R2Y
#define CSEL_NDASH      21                      // n segments in dashed color sample
#define CSEL_DDY        12                      // demo strip dy down from rot top
#define CSEL_ADX        60                      // dashed tick box dx from first tick box x
#define CSEL_EDX        90                      // editing tick box dx from first tick box x
#define CSEL_TDX        30                      // thickness tick box dx from first tick box x
#define CSEL_GAMMA      1.5F                    // swatch background gray scale gamma correction

// color table save/load layout
#define CTSL_H          (PR_A+PR_D)             // color table save/load height
#define CTSL_Y          (480-CTSL_H-4)          // color table save/load row y
#define CTSL_SL_X       20                      // save/load control label x
#define CTSL_SA_X       (CTSL_SL_X+90)          // save A control x
#define CTSL_SA_W       (2*PR_W)                // save A control width
#define CTSL_SB_X       (CTSL_SA_X+40)          // save B control x
#define CTSL_SB_W       (2*PR_W)                // save B control width
#define CTSL_LL_X       (CTSL_SB_X+60)          // load A control label x
#define CTSL_LA_X       (CTSL_LL_X+120)         // load A control x
#define CTSL_LA_W       (2*PR_W)                // load A control width
#define CTSL_LB_X       (CTSL_LA_X+40)          // load B control x
#define CTSL_LB_W       (2*PR_W)                // load B control width
#define CTSL_LP_X       (CTSL_LB_X+40)          // load pskreporter control x
#define CTSL_LP_W       (7*PR_W)                // load pskreporter control width
#define CTSL_LD_X       (CTSL_LP_X+130)         // load default control x
#define CTSL_LD_W       (5*PR_W)                // load default control width
#define CTSL_DO_W       (4*PR_W)                // done control width
#define CTSL_DO_X       (800-CTSL_DO_W-KB_INDENT) // done control x

// OnOff layout constants
#define OO_Y0           150                     // top y
#define OO_X0           50                      // left x
#define OO_CI           50                      // OnOff label to first column indent
#define OO_CW           90                      // weekday column width
#define OO_RH           30                      // row height -- N.B. must be at least font height
#define OO_TO           20                      // extra title offset on top of table
#define OO_ASZ          10                      // arrow size
#define OO_DHX(d)       (OO_X0+OO_CI+(d)*OO_CW) // day of week to hours x
#define OO_CPLX(d)      (OO_DHX(d)+OO_ASZ)      // day of week to copy left x
#define OO_CPRX(d)      (OO_DHX(d)+OO_CW-OO_ASZ)// day of week to copy right x
#define OO_CHY          (OO_Y0-2)               // column headings y
#define OO_CPLY         (OO_Y0-OO_RH/2)         // copy left y
#define OO_CPRY         (OO_Y0-OO_RH/2)         // copy right y
#define OO_ONY          (OO_Y0+2*OO_RH-4)       // on row y
#define OO_OFFY         (OO_Y0+5*OO_RH-4)       // off row y
#define OO_TW           (OO_CI+OO_CW*DAYSPERWEEK)  // total width


// general colors
#define TX_C            RA8875_WHITE            // text color
#define BG_C            RA8875_BLACK            // overall background color
#define KB_C            RGB565(80,80,255)       // key border color
#define KF_C            RA8875_WHITE            // key face color
#define PR_C            RGB565(255,125,0)       // prompt color
#define DEL_C           RA8875_RED              // Delete color
#define DONE_C          RA8875_GREEN            // Done color
#define BUTTON_C        RA8875_CYAN             // option buttons color
#define CURSOR_C        RA8875_GREEN            // cursor color
#define ERR_C           RA8875_RED              // err msg color

// validation constants
#define MAX_BME_DTEMP   20
#define MAX_BME_DPRES   400                     // eg correction for 10k feet is 316 hPa


// NV_X11FLAGS bit defns
#define X11BIT_FULLSCREEN       0x1

// NV_USEGPSD bit defns
#define USEGPSD_FORTIME_BIT     0x1
#define USEGPSD_FORLOC_BIT      0x2

// NV_USENMEA bit defns
#define USENMEA_FORTIME_BIT     0x1
#define USENMEA_FORLOC_BIT      0x2



// entangled NTPA_BPR/NTPB_BPR state codes and names
#define NTP_STATES                      \
    X(NTPSC_NO,   "No")                 \
    X(NTPSC_DEF,  "Built-in")           \
    X(NTPSC_OS,   "Computer")           \
    X(NTPSC_HOST, "host")

#define X(a,b)  a,                              // expands NTP_STATES to each enum and comma
typedef enum {
    NTP_STATES
    NTPSC_N
} NTPStateCode;
#undef X

// NTP state names
#define X(a,b)  b,                              // expands NTP_STATES to each name plus comma
static const char *ntp_sn[NTPSC_N] = {
    NTP_STATES
};
#undef X




// entangled UNITSA_BPR/UNITSB_BPR codes and names
#define UNITS_CHOICES                   \
    X(UNITS_IMP,   "Imperial")          \
    X(UNITS_MET,   "Metric")            \
    X(UNITS_BRIT,  "British")           \

#define X(a,b) a,                               // expands UNITS_CHOICES to each enum and comma
typedef enum {
    UNITS_CHOICES
    UNITS_N
} UnitsCode;
#undef X

#define X(a,b) b,                               // expands UNITS_CHOICES to each name plus comma
static const char *units_names[UNITS_N] = {
    UNITS_CHOICES
};
#undef X




// whether auto upgrading is allowed is determined presence or absense of NO_UPGRADE

#if defined(NO_UPGRADE)

// entangled auto upgrade settings
#define AUP_SETTINGS                    \
    X(AUP_OFF,     "Off",    -1)        \
    X(AUP_P1,      "Off",    -1)        \
    X(AUP_P2,      "Off",    -1)        \
    X(AUP_P3,      "Off",    -1)

#else

// entangled auto upgrade settings
#define AUP_SETTINGS                    \
    X(AUP_OFF,     "Off",    -1)        \
    X(AUP_P1,      "03:00",   3)        \
    X(AUP_P2,      "12:00",  12)        \
    X(AUP_P3,      "21:00",  21)

#endif // NO_UPGRADE

typedef struct {
    const char *label;                  // menu label
    int local_hour;                     // local hour to ask upgrade
} AutoUpSetting;

#define X(a,b,c)  a,                    // expands AUP_SETTINGS to each enum and comma
typedef enum {
    AUP_SETTINGS
    AUP_N
} AUTOUP_ID;
#undef X

// table of auto upgrade labels and times
#define X(a,b,c) {b,c},                 // expands AUP_SETTINGS to each array initialization in {}
static AutoUpSetting autoup_tbl[AUP_N] = {
    AUP_SETTINGS
};
#undef X




// label names
#define X(a,b)  b,                              // expands LABELSTYLES to each name plus comma
static const char *lbl_styles[LBL_N] = {
    LABELSTYLES
};
#undef X

// tooltip usage
static const char tt_reminder[] = "Use control-click, Command-click or middle button to show a tooltip for most fields";

// define a string prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox v_box;                                 // value box
    const char *p_str;                          // prompt string
    char *v_str;                                // value string
    uint8_t v_len;                              // total size of v_str memory including EOS
    uint8_t v_ci;                               // v_str index of cursor: insert here, delete char before
    uint8_t v_wi;                               // v_str index of first character at left end of window
    const char *tt;                             // tooltip text, if any
} StringPrompt;



// max tle ages
static const char *maxTLEAges[] = {
    "3 days",
    "7 days",
    "14 days",
    "21 days"
};
#define N_MAXTLE NARRAY(maxTLEAges)
#define MAXTLE_DEF      1                       // index of default


// min label distances
static const char *minLblD[] = {
    "None",
    "1000",
    "2000",
    "4000"
};
#define N_MINLBL NARRAY(minLblD)
#define MINLBL_DEF      0                       // index of default



// N.B. must match string_pr[] order
typedef enum {
    // page "1"
    CALL_SPR,
    LAT_SPR,
    LNG_SPR,
    GRID_SPR,
    GPSDHOST_SPR,
    NMEAFILE_SPR,
    NTPHOST_SPR,
    WIFISSID_SPR,
    WIFIPASS_SPR,

    // page "2"
    DXWLIST_SPR,
    DXPORT_SPR,
    DXHOST_SPR,
    DXLOGIN_SPR,
    DXCLCMD0_SPR,                               // must be N_DXCLCMDS
    DXCLCMD1_SPR,
    DXCLCMD2_SPR,
    DXCLCMD3_SPR,
    DXCLCMD4_SPR,
    DXCLCMD5_SPR,
    DXCLCMD6_SPR,
    DXCLCMD7_SPR,
    DXCLCMD8_SPR,
    DXCLCMD9_SPR,
    DXCLCMD10_SPR,
    DXCLCMD11_SPR,

    // page "3"
    ROTPORT_SPR,
    ROTHOST_SPR,
    RIGPORT_SPR,
    RIGHOST_SPR,
    FLRIGPORT_SPR,
    FLRIGHOST_SPR,
    ADIFFN_SPR,
    ADIFWL_SPR,
    ONTAWL_SPR,

    // page "4"
    HAMSATKEY_SPR,
    CENTERLNG_SPR,
    I2CFN_SPR,
    BME76DT_SPR,
    BME76DP_SPR,
    BME77DT_SPR,
    BME77DP_SPR,
    BRMIN_SPR,
    BRMAX_SPR,

    // page "5"

    // page "6"
    CSELRED_SPR,
    CSELGRN_SPR,
    CSELBLU_SPR,


    N_SPR
} SPIds; 


// string prompts for each page. N.B. must match SPIds order
static StringPrompt string_pr[N_SPR] = {

    // "page 1" -- index 0

    {0, { 10, R2Y(0), 70, PR_H}, { 90, R2Y(0), 270, PR_H}, "Call:", cs_info.call, NV_CALLSIGN_LEN, 0, 0,
                "Enter an amateur radio call sign"}, 
    {0, { 10, R2Y(1),180, PR_H}, {190, R2Y(1), 110, PR_H}, "Enter DE Lat:", NULL, 0, 0, 0,
                "Enter latitude in decimal degrees (negative south) or DD::MM:SS (follow with S for south); "
                "or let it be set automatically from grid"},
                                                                                                 // shadowed
    {0, {380, R2Y(1), 50, PR_H}, {430, R2Y(1), 130, PR_H}, "Lng:", NULL, 0, 0, 0,
                "Enter longitude in decimal degrees (negative west) or DD::MM:SS (follow with W for west); "
                "or let it be set automatically from grid"},
                                                                                                 // shadowed
    {0, {560, R2Y(1), 60, PR_H}, {620, R2Y(1), 130, PR_H}, "Grid:", NULL, 0, 0, 0,
                "Enter 4 or 6 character grid square; or let it be set automatically from lat/long"},                                                                                                           // shadowed
    {0, {340, R2Y(2), 60, PR_H}, {400, R2Y(2), 300, PR_H}, "host:", gpsd_host, NV_GPSDHOST_LEN, 0, 0,
                "Enter IP address or DNS host name of gpsd daemon"},
    {0, {480, R2Y(3), 50, PR_H}, {530, R2Y(3), 270, PR_H}, "file:", nmea_file, NV_NMEAFILE_LEN, 0, 0,
                "Enter /dev name of serial connection to a GPS device reporting NMEA sentences"},
    {0, {180, R2Y(5), 60, PR_H}, {240, R2Y(5), 560, PR_H}, "host:", ntp_host, NV_NTPHOST_LEN, 0, 0,
                "Enter IP address or DNS host name of NTP server"},

    {0, { 90, R2Y(6), 60, PR_H}, {160, R2Y(6), 500, PR_H}, "SSID:", wifi_ssid, NV_WIFI_SSID_LEN, 0,0,NULL},
    {0, {670, R2Y(6),110, PR_H}, { 10, R2Y(7), 789, PR_H}, "Password:", wifi_pw, NV_WIFI_PW_LEN, 0,0,NULL},

    // "page 2" -- index 1

    {1, {140, R2Y(1),  0, PR_H}, {140, R2Y(1), 650, PR_H}, NULL, dx_wlist, NV_DXWLIST_LEN, 0, 0,
                "Enter cluster watch list description; may only be empty if Off"},
    {1, { 15, R2Y(2), 70, PR_H}, { 85, R2Y(2),  85, PR_H}, "port:", NULL, 0, 0, 0,
                "Enter cluster connection port number"},                                          // shadowed
    {1, { 15, R2Y(3), 70, PR_H}, { 85, R2Y(3), 260, PR_H}, "host:", dx_host, NV_DXHOST_LEN, 0, 0,
                "Enter cluster IP address or DNS host name, or subscription IP if UDP uses multicast"},
    {1, { 15, R2Y(4), 70, PR_H}, { 85, R2Y(4), 260, PR_H}, "login:", dx_login, NV_DXLOGIN_LEN, 0, 0,
                "Enter cluster login with SSID between 1-99"},

    // three overlapping sets, visibility depends on DXCLCMDPGA/B_BPR

    {1, {350, R2Y(3), 40, PR_H}, {390, R2Y(3), 409, PR_H}, NULL, dxcl_cmds[0], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(4), 40, PR_H}, {390, R2Y(4), 409, PR_H}, NULL, dxcl_cmds[1], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(5), 40, PR_H}, {390, R2Y(5), 409, PR_H}, NULL, dxcl_cmds[2], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(6), 40, PR_H}, {390, R2Y(6), 409, PR_H}, NULL, dxcl_cmds[3], NV_DXCLCMD_LEN, 0,0,NULL},

    {1, {350, R2Y(3), 40, PR_H}, {390, R2Y(3), 409, PR_H}, NULL, dxcl_cmds[4], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(4), 40, PR_H}, {390, R2Y(4), 409, PR_H}, NULL, dxcl_cmds[5], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(5), 40, PR_H}, {390, R2Y(5), 409, PR_H}, NULL, dxcl_cmds[6], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(6), 40, PR_H}, {390, R2Y(6), 409, PR_H}, NULL, dxcl_cmds[7], NV_DXCLCMD_LEN, 0,0,NULL},

    {1, {350, R2Y(3), 40, PR_H}, {390, R2Y(3), 409, PR_H}, NULL, dxcl_cmds[8], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(4), 40, PR_H}, {390, R2Y(4), 409, PR_H}, NULL, dxcl_cmds[9], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(5), 40, PR_H}, {390, R2Y(5), 409, PR_H}, NULL, dxcl_cmds[10], NV_DXCLCMD_LEN, 0,0,NULL},
    {1, {350, R2Y(6), 40, PR_H}, {390, R2Y(6), 409, PR_H}, NULL, dxcl_cmds[11], NV_DXCLCMD_LEN, 0,0,NULL},


    // "page 3" -- index 2

    {2, {160, R2Y(0), 60, PR_H}, {220, R2Y(0),  90, PR_H}, "port:", NULL, 0, 0, 0,
                "Enter the network port number for connecting to rotctld"},                       // shadowed
    {2, {310, R2Y(0), 60, PR_H}, {360, R2Y(0), 300, PR_H}, "host:", rot_host, NV_ROTHOST_LEN, 0, 0,
                "Enter the IP address or DNS host name for connecting to rotctld"},
    {2, {160, R2Y(1), 60, PR_H}, {220, R2Y(1),  90, PR_H}, "port:", NULL, 0, 0, 0,
                "Enter the network port number for connecting to rigctld"},                       // shadowed
    {2, {310, R2Y(1), 60, PR_H}, {360, R2Y(1), 300, PR_H}, "host:", rig_host, NV_RIGHOST_LEN, 0, 0,
                "Enter the IP address or DNS host name for connecting to rigctld"},
    {2, {160, R2Y(2), 60, PR_H}, {220, R2Y(2),  90, PR_H}, "port:", NULL, 0, 0, 0,
                "Enter the network port number for connecting to flrig"},                         // shadowed
    {2, {310, R2Y(2), 60, PR_H}, {360, R2Y(2), 300, PR_H}, "host:", flrig_host, NV_FLRIGHOST_LEN, 0, 0,
                "Enter the IP address or DNS host name for connecting to flrig"},

    {2, {100, R2Y(4), 60, PR_H}, {160, R2Y(4), 580, PR_H}, "file:", adif_fn, NV_ADIFFN_LEN, 0, 0,
                "Enter the path name to the ADIF file; "
                "you may use environment variables or ~ to refer to your home directory"},

    {2, {215, R2Y(5),  0, PR_H}, {215, R2Y(5), 580, PR_H}, NULL, adif_wlist, NV_ADIFWLIST_LEN, 0, 0,
                "Enter ADIF file watch list description; may only be empty if Off"},
    {2, {215, R2Y(6),  0, PR_H}, {215, R2Y(6), 580, PR_H}, NULL, onta_wlist, NV_ONTAWLIST_LEN, 0, 0,
                "Enter OnTheAir watch list description; may only be empty if Off"},


    // "page 4" -- index 3

    {3, { 10, R2Y(0), 190, PR_H}, {210, R2Y(0), 600, PR_H}, "hams.at key:  ", hamsat_key, NV_HAMSATKEY_LEN, 0, 0,
                "Enter your hams.at API key for personalized satellite activation match%/visibility, "
                "or leave blank"},

    {3, {10,  R2Y(1), 240, PR_H}, {250, R2Y(1), 100, PR_H}, "Map center longitude:", NULL, 0, 0, 0,
                "Enter the desired center longitude for the Mercator map projection in decimal degrees; "
                "use - or suffix W for west"},

    {3, {350, R2Y(2),  70, PR_H}, {440, R2Y(2), 360,PR_H},  "name:", i2c_fn, NV_I2CFN_LEN, 0, 0,
                "Enter the full /dev name for the I2C connection"},

    {3, {100, R2Y(3), 240, PR_H}, {350, R2Y(3),  80, PR_H}, "BME280@76    dTemp:", NULL, 0, 0, 0,
                "Enter a temperature correction to be added to the BME280 at I2C address 76; "
                "use the units selected on page 5"},                                              // shadowed
    {3, {440, R2Y(3), 80,  PR_H}, {530, R2Y(3),  80, PR_H}, "dPres:", NULL, 0, 0, 0,              // shadowed
                "Enter a pressure correction to be added to the BME280 at I2C address 76; "
                "use the units selected on page 5"},                                              // shadowed
    {3, {100, R2Y(4), 240, PR_H}, {350, R2Y(4),  80, PR_H}, "BME280@77    dTemp:", NULL, 0, 0, 0, // shadowed
                "Enter a temperature correction to be added to the BME280 at I2C address 77; "
                "use the units selected on page 5"},                                              // shadowed
    {3, {440, R2Y(4), 80,  PR_H}, {530, R2Y(4),  80, PR_H}, "dPres:", NULL, 0, 0, 0,              // shadowed
                "Enter a pressure correction to be added to the BME280 at I2C address 77; "
                "use the units selected on page 5"},                                              // shadowed

    {3, {10,  R2Y(6), 200, PR_H}, {250, R2Y(6),  80, PR_H}, "Brightness Min%:", NULL, 0, 0, 0,
                "Enter the percentage of hardware brightness to be used for Dim"},                // shadowed
    {3, {350, R2Y(6),  90, PR_H}, {450, R2Y(6),  80, PR_H}, "Max%:", NULL, 0, 0, 0,
                "Enter the percentage of hardware brightness to be used for Bright"},             // shadowed


    // "page 5" -- index 4

    // "page 6" -- index 5

    {5, {CSEL_VX, CSEL_VYR, 0, PR_H}, {CSEL_VX, CSEL_VYR, 80, PR_H}, NULL, NULL, 0, 0, 0,
                "Enter magnitude of red on a scale of 0-255"},                                    // shadowed
    {5, {CSEL_VX, CSEL_VYG, 0, PR_H}, {CSEL_VX, CSEL_VYG, 80, PR_H}, NULL, NULL, 0, 0, 0,
                "Enter magnitude of green on a scale of 0-255"},                                  // shadowed
    {5, {CSEL_VX, CSEL_VYB, 0, PR_H}, {CSEL_VX, CSEL_VYB, 80, PR_H}, NULL, NULL, 0, 0, 0,
                "Enter magnitude of blue on a scale of 0-255"},                                   // shadowed

    // "page 7" -- index 6

    // on/off table

};




// N.B. must match bool_pr[] order
typedef enum {
    // page "1"
    GPSDON_BPR,
    GPSDFOLLOW_BPR,
    NMEAON_BPR,
    NMEAFOLLOW_BPR,
    NMEABAUDA_BPR,
    NMEABAUDB_BPR,
    GEOIP_BPR,
    NTPA_BPR,
    NTPB_BPR,
    WIFI_BPR,

    // page "2"
    CLUSTER_BPR,
    CLISUDP_BPR,
    DXWLISTA_BPR,
    DXWLISTB_BPR,
    DXCLCMDPGA_BPR,
    DXCLCMDPGB_BPR,
    DXCLCMD0_BPR,                               // must be N_DXCLCMDS
    DXCLCMD1_BPR,
    DXCLCMD2_BPR,
    DXCLCMD3_BPR,
    DXCLCMD4_BPR,
    DXCLCMD5_BPR,
    DXCLCMD6_BPR,
    DXCLCMD7_BPR,
    DXCLCMD8_BPR,
    DXCLCMD9_BPR,
    DXCLCMD10_BPR,
    DXCLCMD11_BPR,

    // page "3"
    ROTUSE_BPR,
    RIGUSE_BPR,
    FLRIGUSE_BPR,
    SETRADIO_BPR,
    ADIFSET_BPR,
    ADIFWLISTA_BPR,
    ADIFWLISTB_BPR,
    ONTAWLISTA_BPR,
    ONTAWLISTB_BPR,

    // page "4"
    GPIOOK_BPR,
    I2CON_BPR,
    KX3A_BPR,
    KX3B_BPR,

    // page "5"
    DATEFMT_MDY_BPR,
    DATEFMT_DMYYMD_BPR,
    LOGUSAGE_BPR,

    WEEKDAY1MON_BPR,
    DEMO_BPR,

    UNITSA_BPR,
    UNITSB_BPR,
    BEARING_BPR,

    SHOWPIP_BPR,
    NEWDXDEWX_BPR,

    SPOTLBLA_BPR,
    SPOTLBLB_BPR,
    LBLDISTA_BPR,
    LBLDISTB_BPR,

    SCROLLDIR_BPR,
    GRAYA_BPR,
    GRAYB_BPR,

    PANE_ROTPA_BPR,
    PANE_ROTPB_BPR,
    MAP_ROTPA_BPR,
    MAP_ROTPB_BPR,

    UDPSPOTS_BPR,
    QRZBIOA_BPR,
    QRZBIOB_BPR,

    AUTOMAP_BPR,
    UDPSETSDX_BPR,

    WEB_FULLSCRN_BPR,
    AUTOUPA_BPR,
    AUTOUPB_BPR,

    X11_FULLSCRN_BPR,
    MAXTLEA_BPR,
    MAXTLEB_BPR,

    SHOWPLANETS_BPR,

    // page "6" -- color editor

    N_BPR,                                      // number of fields

    NOMATE                                      // flag for ent_mate

} BPIds;


// values for PANE_ROTPA_BPR and PANE_ROTPB_BPR -- N.B. init panerotp_strs to match
static int panerotp_vals[] = {5, 10, 30, 60};   // seconds
static char panerotp_strs[NARRAY(panerotp_vals)][20];

// values for MAP_ROTPA_BPR and MAP_ROTPB_BPR -- N.B. init maprotp_strs to match
static int maprotp_vals[] = {20, 60, 90, 120};  // seconds
static char maprotp_strs[NARRAY(maprotp_vals)][20];

// srting values for each possible watch list states
#define X(a,b) b,                               // expands _WATCH_DEFN to name then comma
static const char *wla_name[WLA_N] = {
    _WATCH_DEFN
};
#undef X


// define a boolean prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox s_box;                                 // state box, if t/f_str
    bool state;                                 // on or off
    const char *p_str;                          // prompt string, or NULL to use just f/t_str
    const char *f_str;                          // "false" string, or NULL
    const char *t_str;                          // "true" string, or NULL
    BPIds ent_mate;                             // entanglement partner, else NOMATE
    const char *tt;                             // tooltip text, if any
} BoolPrompt;

static char cc_tip[] = "Enter a native cluster command, turn On to send after each login or Off to just save for later";

/* bool prompts. N.B. must match BPIds order
 * N.B. some fields use two "entangled" bools to create 3 states
 */
static BoolPrompt bool_pr[N_BPR] = {

    // "page 1" -- index 0

    {0, { 10, R2Y(2), 180, PR_H}, {180, R2Y(2), 40,  PR_H}, false, "or use gpsd?", "No", "Yes", NOMATE,
                "Whether to use a gpsd daemon for time"},
    {0, {220, R2Y(2),  80, PR_H}, {300, R2Y(2), 40,  PR_H}, false, "follow?", "No", "Yes", NOMATE,
                "Whether to use the same gpsd daemon for location too"},


    {0, { 10, R2Y(3), 180, PR_H}, {180, R2Y(3), 40,  PR_H}, false, "or use NMEA?", "No", "Yes", NOMATE,
                "Whether to use a serial NMEA device for time"},
    {0, {220, R2Y(3),  80, PR_H}, {300, R2Y(3), 40,  PR_H}, false, "follow?", "No", "Yes", NOMATE,
                "Whether to use a serial NMEA device for location too"},


    {0, {340, R2Y(3),  70, PR_H},  {410, R2Y(3), 70, PR_H}, false, "baud:", "4800", NULL, NMEABAUDB_BPR,
                "Select baud rate of NMEA serial device"},
    {0, {340, R2Y(3),  70, PR_H},  {410, R2Y(3), 70, PR_H}, false, NULL, "9600", "38400", NMEABAUDA_BPR, 0},
                                             // 3x entangled: FX -> TF -> TT ...


    {0, { 10, R2Y(4), 180, PR_H}, {180, R2Y(4), 40,  PR_H}, false, "or IP Geolocate?", "No", "Yes", NOMATE,
                "Whether to get approximate location based on the public IP address records"},


    {0, { 10, R2Y(5), 180, PR_H}, {180, R2Y(5), 110, PR_H}, false, "NTP?",
                                                            ntp_sn[NTPSC_NO], ntp_sn[NTPSC_DEF], NTPB_BPR,
                "Whether to use the built-in list of NTP servers, a specified NTP server or computer time"},
    {0, { 10, R2Y(5), 180, PR_H}, {180, R2Y(5), 110, PR_H}, false, NULL,
                                                        ntp_sn[NTPSC_OS], ntp_sn[NTPSC_HOST], NTPA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    {0, {10,  R2Y(6),  70, PR_H}, {100, R2Y(6), 30,  PR_H}, false, "WiFi?", "No", NULL, NOMATE, NULL},


    // "page 2" -- index 1


    {1, {10,  R2Y(0),  90, PR_H}, {100, R2Y(0), 50,  PR_H}, false, "Cluster?", "No", "Yes", NOMATE,
                "Connect to DX Cluster or a local program sending UDP packets"},
    {1, {200, R2Y(0),  90, PR_H}, {290, R2Y(0), 50,  PR_H}, false, "UDP?", "No", "Yes", NOMATE,
                "Connect to a local program sending UDP packets"},


    {1, {15, R2Y(1),  55, PR_H},  {85, R2Y(1), 55, PR_H}, false, "watch:",
                                                        wla_name[WLA_OFF], wla_name[WLA_NOT], DXWLISTB_BPR,
                "Define the style and filter for a Cluster watch list"},
    {1, {15, R2Y(1),  55, PR_H},  {85, R2Y(1), 55, PR_H}, false, NULL,
                                                    wla_name[WLA_FLAG], wla_name[WLA_ONLY], DXWLISTA_BPR,0},
                                             // 4x entangled: FF -> TF -> FT -> TT -> ...


    // three overlapping sets, visibility depends on DXCLCMDPGA/B_BPR


    {1, {350, R2Y(2),   35, PR_H}, {385, R2Y(2), 20, PR_H},  false, "Pg", "1", NULL, DXCLCMDPGB_BPR,
                "Advance to next page of native cluster commands"},
    {1, {350, R2Y(2),   35, PR_H}, {385, R2Y(2), 20, PR_H},  false, "Pg", "2", "3", DXCLCMDPGA_BPR,0},
                                             // 3x entangled: FX -> TF -> TT ...


    {1, {350, R2Y(3),   0, PR_H},  {350, R2Y(3), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(4),   0, PR_H},  {350, R2Y(4), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(5),   0, PR_H},  {350, R2Y(5), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(6),   0, PR_H},  {350, R2Y(6), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},

    {1, {350, R2Y(3),   0, PR_H},  {350, R2Y(3), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(4),   0, PR_H},  {350, R2Y(4), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(5),   0, PR_H},  {350, R2Y(5), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(6),   0, PR_H},  {350, R2Y(6), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},

    {1, {350, R2Y(3),   0, PR_H},  {350, R2Y(3), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(4),   0, PR_H},  {350, R2Y(4), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(5),   0, PR_H},  {350, R2Y(5), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},
    {1, {350, R2Y(6),   0, PR_H},  {350, R2Y(6), 40, PR_H},  false, NULL, "Off:", "On:", NOMATE, cc_tip},


    // "page 3" -- index 2


    {2, {10,  R2Y(0),  90, PR_H},  {100, R2Y(0),  60, PR_H}, false, "rotctld?", "No", "Yes", NOMATE,
                "Whether to control a rotator using rotctld"},
    {2, {10,  R2Y(1),  90, PR_H},  {100, R2Y(1),  60, PR_H}, false, "rigctld?", "No", "Yes", NOMATE,
                "Whether to control a radio using rigctld"},
    {2, {10,  R2Y(2),  90, PR_H},  {100, R2Y(2),  60, PR_H}, false, "flrig?",   "No", "Yes", NOMATE,
                "Whether to control a radio using flrig"},

    {2, {10,  R2Y(3),  90, PR_H},  {100, R2Y(3), 150, PR_H}, false, "Radio:", "Monitor PTT","Control",NOMATE,
                "Whether to allow HamClock to control a radio or just passively monitor PTT"},


    {2, {10,  R2Y(4),  90, PR_H},  {100, R2Y(4), 300, PR_H}, false, "ADIF?", "No", NULL, NOMATE,
                "Whether to open and monitor an ADIF log file"},



    {2, {10,  R2Y(5), 150, PR_H},  {160, R2Y(5),  55, PR_H}, false, "ADIF watch:",
                                                    wla_name[WLA_OFF], wla_name[WLA_NOT], ADIFWLISTB_BPR,
                "Define the style and filter for an ADIF file watch list"},
    {2, {10,  R2Y(5), 150, PR_H},  {160, R2Y(5),  55, PR_H}, false, NULL,
                                                    wla_name[WLA_FLAG], wla_name[WLA_ONLY], ADIFWLISTA_BPR,0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...

    {2, {10,  R2Y(6), 150, PR_H},  {160, R2Y(6),  55, PR_H}, false, "ONTA watch:",
                                                    wla_name[WLA_OFF], wla_name[WLA_NOT], ONTAWLISTB_BPR,
                "Define the style and filter for OnTheAir watch list"},
    {2, {10,  R2Y(6), 150, PR_H},  {160, R2Y(6),  55, PR_H}, false, NULL,
                                                    wla_name[WLA_FLAG], wla_name[WLA_ONLY], ONTAWLISTA_BPR,0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    // "page 4" -- index 3

    {3, {10,  R2Y(2),  80, PR_H},  {100, R2Y(2), 110, PR_H}, false, "GPIO?", "Off", "Active", NOMATE,
                "Whether to control the RPi GPIO pins or ignore them"},
    {3, {250, R2Y(2),  80, PR_H},  {350, R2Y(2), 70,  PR_H}, false, "I2C file?", "No", NULL, NOMATE,
                "Whether to control I2C hardware devices"},


    {3, {100, R2Y(5), 120, PR_H},  {250, R2Y(5),  120, PR_H}, false, "KX3?", "No", NULL, KX3B_BPR,
                "Whether or at which baud rate to control a KX3 transceiver via the RPi GPIO pin"},
    {3, {250, R2Y(5),   0, PR_H},  {250, R2Y(5),  120, PR_H}, false, NULL, "4800 bps","38400 bps",KX3A_BPR,0},
                                             // 3x entangled: FX -> TF -> TT ...




    // "page 5" -- index 4

    {4, {10,  R2Y(1), 190, PR_H},  {200, R2Y(1), 170, PR_H}, false, "Date order?",
                    "Mon Day Year", NULL, DATEFMT_DMYYMD_BPR, "Set desired date format"},
    {4, {10,  R2Y(1), 190, PR_H},  {200, R2Y(1), 170, PR_H}, false, NULL,
                                        "Day Mon Year", "Year Mon Day", DATEFMT_MDY_BPR, 0},
                                             // 3x entangled: FX -> TF -> TT ...


    {4, {400, R2Y(1), 190, PR_H},  {590, R2Y(1), 170, PR_H}, false, "Log usage?", "Opt-Out", "Opt-In",NOMATE,
                    "Whether to report settings anonymously to support development"},


    {4, {10,  R2Y(2), 190, PR_H},  {200, R2Y(2), 170, PR_H}, false, "Week starts?", "Sunday","Monday",NOMATE,
                    "Select calendar first day of week"},

    {4, {400, R2Y(2), 190, PR_H},  {590, R2Y(2), 170, PR_H}, false, "Demo mode?", "No", "Yes", NOMATE,
                    "Select whether to make random changes automatically"},



    {4, {10,  R2Y(3), 190, PR_H},  {200, R2Y(3), 170, PR_H}, false, "Units?",
                    units_names[UNITS_IMP], NULL, UNITSB_BPR,
                    "Select units for temperature, distance and pressure"},
    {4, {10,  R2Y(3), 190, PR_H},  {200, R2Y(3), 170, PR_H}, false, NULL,
                                        units_names[UNITS_MET], units_names[UNITS_BRIT], UNITSA_BPR, 0},
                                             // 3x entangled: FX -> TF -> TT ...



    {4, {400, R2Y(3), 190, PR_H},  {590, R2Y(3), 170, PR_H}, false, "Bearings?","True N","Magnetic N",NOMATE,
                    "Choose geographic or magnetic azimuth bearings"},



    {4, {10,  R2Y(4), 190, PR_H},  {200, R2Y(4), 170, PR_H}, false, "Show public IP?", "No", "Yes", NOMATE,
                    "Whether to display HamClock's public IP address beneath the call sign"},

    {4, {400, R2Y(4), 190, PR_H},  {590, R2Y(4), 170, PR_H}, false, "New DE/DX Wx?",  "No", "Yes", NOMATE,
                    "Whether to briefly display local weather when setting new DX location"},



    {4, {10,  R2Y(5), 190, PR_H},  {200, R2Y(5), 170, PR_H}, false, "Spot labels?",
                    lbl_styles[LBL_NONE], lbl_styles[LBL_DOT], SPOTLBLB_BPR,
                    "How or whether to label spot locations on map"},
    {4, {10,  R2Y(5), 190, PR_H},  {200, R2Y(5), 170, PR_H}, false, NULL,
                    lbl_styles[LBL_PREFIX], lbl_styles[LBL_CALL], SPOTLBLA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    {4, {400, R2Y(5), 190, PR_H},  {590, R2Y(5), 170, PR_H}, false, "Min label dist?",
                    minLblD[0], minLblD[1], LBLDISTB_BPR,
                    "Minimum distance from DE to label spots; mi or km as per Units"},
    {4, {400, R2Y(5), 190, PR_H},  {590, R2Y(5), 170, PR_H}, false, NULL,
                    minLblD[2], minLblD[3], LBLDISTA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    {4, {10,  R2Y(6), 190, PR_H},  {200, R2Y(6), 170, PR_H}, false, "Scroll direction?",
                    "Bottom-Up", "Top-Down", NOMATE,
                    "Whether to scroll overflowing lists top-to-bottom or bottom-to-top"},


    {4, {400, R2Y(6), 190, PR_H},  {590, R2Y(6), 170, PR_H}, false, "Gray display?", "No", NULL, GRAYB_BPR,
                    "Whether to render the map or entire screen in shades of gray"},
    {4, {400, R2Y(6), 190, PR_H},  {590, R2Y(6), 170, PR_H}, false, NULL, "All", "Map", GRAYA_BPR, 0},
                                                // 3x entangled: FX -> TF -> TT ...
                                                // N.B. names must match getGrayDisplay();


    {4, {10,  R2Y(7), 190, PR_H},  {200, R2Y(7), 170, PR_H}, false, "Pane rotation?",
                    panerotp_strs[0], panerotp_strs[1], PANE_ROTPB_BPR,
                    "Select how often to change data pane if more than one is selected"},
    {4, {10,  R2Y(7), 190, PR_H},  {200, R2Y(7), 170, PR_H}, false, NULL,
                    panerotp_strs[2], panerotp_strs[3], PANE_ROTPA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    {4, {400, R2Y(7), 190, PR_H},  {590, R2Y(7), 170, PR_H}, false, "Map rotation?",
                    maprotp_strs[0], maprotp_strs[1], MAP_ROTPB_BPR,
                    "Select how often to change map style if more than one is selected"},
    {4, {400, R2Y(7), 190, PR_H},  {590, R2Y(7), 170, PR_H}, false, NULL,
                    maprotp_strs[2], maprotp_strs[3], MAP_ROTPA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...



    {4, { 10, R2Y(8), 190, PR_H},  {200, R2Y(8), 170, PR_H}, false, "Show UDP spots?", "By me", "All",NOMATE,
                    "Whether to show all spots from local UDP programs or just those from DE"},



    {4, {400, R2Y(8), 190, PR_H},  {590, R2Y(8), 170, PR_H}, false, "Look up bio?",
                    qrz_urltable[QRZ_NONE].label, qrz_urltable[QRZ_QRZ].label, QRZBIOB_BPR,
                    "Whether and from which resource to offer looking up cluster spot biography"},
    {4, {400, R2Y(8), 190, PR_H},  {590, R2Y(8), 170, PR_H}, false, NULL,
                                qrz_urltable[QRZ_HAMCALL].label, qrz_urltable[QRZ_CQQRZ].label,QRZBIOA_BPR,0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...



    {4, { 10, R2Y(9), 190, PR_H},  {200, R2Y(9), 170, PR_H}, false, "Auto SpcWx map?", "No", "Yes", NOMATE,
                    "Whether to automatically select Aurora or DRAP map styles when solar activity is high"},


    {4, {400, R2Y(9), 190, PR_H},  {590, R2Y(9), 170, PR_H}, false, "UDP sets DX?", "No", "Yes", NOMATE,
                    "Whether UDP spots from local programs also set DX"},



    {4, { 10, R2Y(10), 190, PR_H}, {200, R2Y(10), 170, PR_H}, false, "Full scrn web?", "No", "Yes", NOMATE,
                    "Whether the web interface will allow using the full screen"},


    {4, {400, R2Y(10), 190, PR_H},  {590, R2Y(10), 170, PR_H}, false, "Auto upgrade?",
                    autoup_tbl[AUP_OFF].label, autoup_tbl[AUP_P1].label, AUTOUPB_BPR,
#if defined(NO_UPGRADE)
                    "Automatic upgrade is not available with this build"},
#else
                    "Whether or during which hour HamClock will automatically update to latest version"},
#endif // NO_UPGRADE
    {4, {400, R2Y(10), 190, PR_H},  {590, R2Y(10), 170, PR_H}, false, NULL,
                    autoup_tbl[AUP_P2].label, autoup_tbl[AUP_P3].label, AUTOUPA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...


    {4, { 10, R2Y(11), 190, PR_H}, {200, R2Y(11), 170, PR_H}, false, "Full scrn direct?", "No", "Yes",NOMATE,
                    "Whether the direct screen display will be set to full screen"},
                                                // N.B. state box must be wide enough for "Won't fit"


    {4, {400, R2Y(11), 190, PR_H},  {590, R2Y(11), 170, PR_H}, false, "Max TLE age:",
                    maxTLEAges[0], maxTLEAges[1], MAXTLEB_BPR,
                    "Max allowed satellite TLE age; beware longer will result in larger errors"},
    {4, {400, R2Y(11), 190, PR_H},  {590, R2Y(11), 170, PR_H}, false, NULL,
                    maxTLEAges[2], maxTLEAges[3], MAXTLEA_BPR, 0},
                                                // 4x entangled: FF -> TF -> FT -> TT -> ...

    {4, { 10, R2Y(12), 190, PR_H}, {200, R2Y(12), 170, PR_H}, false, "Show Planets?", "No", "Yes", NOMATE,
                    "Whether to show the 7 visible planets on the map at their current position"},





    // "page 6" -- index 5

    // "page 7" -- index 6

    // on/off table


};


/* handy access to watch list info.
 * N.B. must be in same order as WatchListId
 */
typedef struct {
    char *wlist;                                // one of the *_list arrays used in the setup string prompts
    int len;                                    // length of "
    const char *name;                           // brief name
    BPIds a_bpr, b_bpr;                         // entangle control pair
    NV_Name nv_wl;                              // NV name for list itself
    NV_Name nv_wlmask;                          // NV name for mask
} WLInfo;
static WLInfo wl_info[WLID_N] = {               // N.B. must be in same order as WatchListId
    {dx_wlist,   NV_DXWLIST_LEN,   "DX",   DXWLISTA_BPR,   DXWLISTB_BPR,   NV_DXWLIST,   NV_DXWLISTMASK},
    {onta_wlist, NV_ONTAWLIST_LEN, "ONTA", ONTAWLISTA_BPR, ONTAWLISTB_BPR, NV_ONTAWLIST, NV_ONTAWLISTMASK},
    {adif_wlist, NV_ADIFWLIST_LEN, "ADIF", ADIFWLISTA_BPR, ADIFWLISTB_BPR, NV_ADIFWLIST, NV_ADIFWLISTMASK},
};



// store info about a given string or bool focus field
typedef struct {
    // N.B. always one, the other NULL
    StringPrompt *sp;
    BoolPrompt *bp;
} Focus;

// whether to show on/off page
#if defined(_SHOW_ALL)
    #define HAVE_ONOFF()      1
#else
    #define HAVE_ONOFF()      (brDimmableOk() || brOnOffOk())
#endif

// current focus and page names
#define LATLNG_PAGE     0                       // 0-based counting
#define SPIDER_PAGE     1                       // 0-based counting
#define ALLBOOLS_PAGE   4                       // 0-based counting
#define COLOR_PAGE      5                       // 0-based counting
#define ONOFF_PAGE      6                       // 0-based counting
#define KBPAGE_FIRST    0                       // first in a series of pages that need a keyboard
#define KBPAGE_LAST     3                       // last in a series of pages that need a keyboard
#define MAX_PAGES       7                       // max number of possible pages
#define N_PAGES         (HAVE_ONOFF() ? MAX_PAGES : (MAX_PAGES-1))      // last page only if on/off

static Focus cur_focus[MAX_PAGES];              // retain focus for each page
static int cur_page;                            // 0-based 0 .. N_PAGES-1


// dx cluster layout
#define SPIDER_TX       480                     // title x
#define SPIDER_TY       (R2Y(2) + PR_A + 2)     // title y
#define SPIDER_BX       345                     // border x
#define SPIDER_BY       (SPIDER_TY - PR_A - 1)  // border y
#define SPIDER_BRX      799                     // border right x
#define SPIDER_BBY      (R2Y(6) + PR_H + 1)     // border bottom y

/* color selector information.
 * since mouse is required it does not participate in tabbing or Focus.
 */

typedef struct {
    SBox p_box;                                 // prompt box
    SBox t_box;                                 // thick/thin select box
    SBox e_box;                                 // edit select box
    SBox o_box;                                 // on/off select box, .x == 0 if user can not change
    SBox d_box;                                 // demo patch box
    bool e_state;                               // whether editing this color
    bool o_state;                               // on or off
    bool t_state;                               // thin else thick
    uint16_t def_c;                             // default color -- NOT the current color
    NV_Name def_c_nv;                           // " nvram location
    const char *p_str;                          // prompt string
    SBox a_box;                                 // dashed control tick box, .x == 0 if not available
    bool a_state;                               // whether dashed is enabled
    uint8_t r, g, b;                            // current color in full precision color
} ColSelPrompt;

#define CSEL_DASHOK(p)  (p.a_box.x > 0)         // handy test whether this color has a dash control option
#define CSEL_ONOFFOK(p) (p.o_box.x > 0)         // handy test whether this color can be turned off


/* color selector controls and prompts.
 * N.B. must match ColorSelection order
 */
static ColSelPrompt csel_pr[N_CSPR] = {
    {{CSEL_COL1X+CSEL_PDX, R2Y(1), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(1)+CSEL_DDY, CSEL_DW, CSEL_DH},
            true, true, true, DE_COLOR, NV_SHORTPATHCOLOR, "Short path",
            {CSEL_COL1X+CSEL_ADX, R2Y(1)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(2), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(2)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(229,191,131), NV_LONGPATHCOLOR, "Long path",
            {CSEL_COL1X+CSEL_ADX, R2Y(2)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(3), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(3)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(3)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(3)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(3)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(175,38,127), NV_SAT1COLOR, "Satellite 1",
            {CSEL_COL1X+CSEL_ADX, R2Y(3)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(4), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(4)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(4)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(4)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(4)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(236,175,79), NV_SAT2COLOR, "Satellite 2",
            {CSEL_COL1X+CSEL_ADX, R2Y(4)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(5), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(5)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(5)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {0, 0, 0, 0},                                                       // always on
            {CSEL_COL1X+CSEL_DDX1, R2Y(5)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(44,42,99), NV_GRIDCOLOR, "Map grid",
            {0, 0, 0, 0}, false, 0, 0, 0},                                      // never dashed

    {{CSEL_COL1X+CSEL_PDX, R2Y(6), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(6)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(6)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RA8875_WHITE, NV_ROTCOLOR, "Rotator",
            {0, 0, 0, 0}, false, 0, 0, 0},                                      // never dashed

    {{CSEL_COL1X+CSEL_PDX, R2Y(7), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(7)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(128,0,0), NV_160M_COLOR, "160 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(8), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(8)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(128,128,0), NV_80M_COLOR, "80 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(9), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(9)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(230,25,75), NV_60M_COLOR, "60 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(10), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(10)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(245,130,48), NV_40M_COLOR, "40 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(11), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(11)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(200,176,20), NV_30M_COLOR, "30 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL1X+CSEL_PDX, R2Y(12), CSEL_PW, PR_H},
            {CSEL_COL1X+CSEL_TDX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_EDX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL1X+CSEL_DDX1, R2Y(12)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(255,250,0), NV_20M_COLOR, "20 m",
            {CSEL_COL1X+CSEL_ADX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(7), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(7)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(91,182,10), NV_17M_COLOR, "17 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(7)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(8), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(8)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(65,255,173), NV_15M_COLOR, "15 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(8)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(9), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(9)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(0,130,250), NV_12M_COLOR, "12 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(9)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(10), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(10)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(250,190,212), NV_10M_COLOR, "10 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(10)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(11), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(11)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(140,163,11), NV_6M_COLOR, "6 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(11)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},

    {{CSEL_COL2X+CSEL_PDX, R2Y(12), CSEL_PW, PR_H},
            {CSEL_COL2X+CSEL_TDX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_EDX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ},
            {CSEL_COL2X+CSEL_DDX2, R2Y(12)+CSEL_DDY, CSEL_DW, CSEL_DH},
            false, true, true, RGB565(100,100,100), NV_2M_COLOR, "2 m",
            {CSEL_COL2X+CSEL_ADX, R2Y(12)+CSEL_TBDY, CSEL_TBSZ, CSEL_TBSZ}, false, 0, 0, 0},
};



// overall color selector box, easier than 3 separate boxes. CSEL_SCYG below each swatch.
static const SBox csel_ctl_b = {CSEL_SCX, CSEL_SCY, CSEL_SCW+CSEL_VDX+CSEL_NW, 3*CSEL_SCH + 3*CSEL_SCYG};

// handy conversions between x coord and color value 0..255.
// N.B. only valid when x-CSEL_SCX in [0, CSEL_SCW)
#define X2V(x)  (255*((x)-CSEL_SCX)/(CSEL_SCW-1))
#define V2X(v)  (CSEL_SCX+(CSEL_SCW-1)*(v)/255)


// save/load/done controls
static const SBox ctsl_save1_b = {CTSL_SA_X, CTSL_Y, CTSL_SA_W, CTSL_H};
static const SBox ctsl_save2_b = {CTSL_SB_X, CTSL_Y, CTSL_SB_W, CTSL_H};
static const SBox ctsl_load1_b = {CTSL_LA_X, CTSL_Y, CTSL_LA_W, CTSL_H};
static const SBox ctsl_load2_b = {CTSL_LB_X, CTSL_Y, CTSL_LB_W, CTSL_H};
static const SBox ctsl_loadp_b = {CTSL_LP_X, CTSL_Y, CTSL_LP_W, CTSL_H};
static const SBox ctsl_loadd_b = {CTSL_LD_X, CTSL_Y, CTSL_LD_W, CTSL_H};
static const SBox ctsl_done_b  = {CTSL_DO_X, CTSL_Y, CTSL_DO_W, CTSL_H};


// virtual qwerty keyboard
typedef struct {
    char normal, shifted;                               // normal and shifted char
} OneKBKey;
static const OneKBKey qwerty[NQR][NQC] = {
    { {'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
      {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}
    },
    { {'Q', 'q'}, {'W', 'w'}, {'E', 'e'}, {'R', 'r'}, {'T', 't'}, {'Y', 'y'}, {'U', 'u'},
      {'I', 'i'}, {'O', 'o'}, {'P', 'p'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
    },
    { {'A', 'a'}, {'S', 's'}, {'D', 'd'}, {'F', 'f'}, {'G', 'g'}, {'H', 'h'}, {'J', 'j'},
      {'K', 'k'}, {'L', 'l'}, {';', ':'}, {'\'', '"'},
    },
    { {'Z', 'z'}, {'X', 'x'}, {'C', 'c'}, {'V', 'v'}, {'B', 'b'}, {'N', 'n'}, {'M', 'm'},
      {',', '<'}, {'.', '>'}, {'/', '?'},
    }
};


// horizontal pixel offset of each virtual keyboard row then each follows every KB_CHAR_W
static const uint8_t qroff[NQR] = {
    KB_INDENT,
    KB_INDENT,
    KB_INDENT+KB_CHAR_W,
    KB_INDENT+3*KB_CHAR_W/2
};

// special virtual keyboard chars
static const SBox space_b  = {SBAR_X, KB_SPC_Y, SBAR_W, KB_SPC_H};
static const SBox page_b   = {800-PAGE_W-KB_INDENT-1, 1, PAGE_W, PAGE_H};
static const SBox delete_b = {KB_INDENT+12*KB_CHAR_W, KB_Y0+2*KB_CHAR_H, KB_CHAR_W, KB_CHAR_H};
static const SBox done_b   = {KB_INDENT+23*KB_CHAR_W/2, KB_Y0+3*KB_CHAR_H, 3*KB_CHAR_W/2, KB_CHAR_H};
static const SBox left_b   = {SBAR_X+SBAR_W, KB_SPC_Y, 5*KB_CHAR_W/4, KB_SPC_H};
static const SBox right_b  = {SBAR_X+SBAR_W+5*KB_CHAR_W/4, KB_SPC_Y, 5*KB_CHAR_W/4, KB_SPC_H};

// note whether ll edited
static bool ll_edited;


// a few forward decls topo sort couldn't fix
static void eraseSPValue (const StringPrompt *sp);
static void drawSPValue (StringPrompt *sp);


/* set GRAYA_BPR and GRAYB_BPR to the given setting.
 */
static void setGrayDisplay (GrayDpy_t g)
{
    switch (g) {
    case GRAY_OFF:
        bool_pr[GRAYA_BPR].state = false;
        bool_pr[GRAYB_BPR].state = false;
        break;
    case GRAY_ALL:
        bool_pr[GRAYA_BPR].state = true;
        bool_pr[GRAYB_BPR].state = false;
        break;
    case GRAY_MAP:
        bool_pr[GRAYA_BPR].state = true;
        bool_pr[GRAYB_BPR].state = true;
        break;
    }
}


/* return the value string of the given entangled pair.
 * N.B. B must be the forward ent_mate of A
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static const char *getEntangledValue (BPIds a_bpr, BPIds b_bpr)
{
    const BoolPrompt &A = bool_pr[a_bpr];
    const BoolPrompt &B = bool_pr[b_bpr];

    if (a_bpr != B.ent_mate || b_bpr != A.ent_mate)
        fatalError ("getEntangledValue: %s vs %s", A.p_str, B.p_str);

    const char *s; 

    if (A.t_str) {
        // 4 states
        if (B.state)
            s = A.state ? B.t_str : B.f_str;
        else
            s = A.state ? A.t_str : A.f_str;
    } else {
        // 3 states
        if (A.state)
            s = B.state ? B.t_str : B.f_str;
        else
            s = A.f_str;
    }

    return (s);
}

/* return index of the entangled pair, either 0-2 or 0-3.
 * N.B. B must be the forward ent_mate of A
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static int getEntangledIndex (BPIds a_bpr, BPIds b_bpr)
{
    const BoolPrompt &A = bool_pr[a_bpr];
    const BoolPrompt &B = bool_pr[b_bpr];

    if (a_bpr != B.ent_mate || b_bpr != A.ent_mate)
        fatalError ("getEntangledIndex: %s vs %s", A.p_str, B.p_str);

    int n;

    if (A.t_str) {
        // 4 states
        if (B.state)
            n = A.state ? 3 : 2;
        else
            n = A.state ? 1 : 0;
    } else {
        // 3 states
        if (A.state)
            n = B.state ? 2 : 1;
        else
            n = 0;
    }

    return (n);
}


/* set the state of the entangled bool_pr[] pair to represent the given value.
 + N.B. fatal if value does not match at least its length in one of the t/f_str.
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static void setEntangledValue (BPIds a_bpr, BPIds b_bpr, const char *value)
{
    BoolPrompt &A = bool_pr[a_bpr];
    BoolPrompt &B = bool_pr[b_bpr];

    if (a_bpr != B.ent_mate || b_bpr != A.ent_mate)
        fatalError ("unpaired setEntangledValue %s %s", A.p_str, B.p_str);

    size_t v_len = strlen (value);

    if (A.t_str) {
        // 4 states
        if (strncmp (A.f_str, value, v_len) == 0) {
            A.state = false;
            B.state = false;
        } else if (strncmp (A.t_str, value, v_len) == 0) {
            A.state = true;
            B.state = false;
        } else if (strncmp (B.f_str, value, v_len) == 0) {
            A.state = false;
            B.state = true;
        } else if (strncmp (B.t_str, value, v_len) == 0) {
            A.state = true;
            B.state = true;
        } else
            fatalError ("unknown 4x entangle value %s", value);
    } else {
        // 3 states
        if (strncmp (A.f_str, value, v_len) == 0) {
            A.state = false;
            B.state = false;
        } else if (strncmp (B.f_str, value, v_len) == 0) {
            A.state = true;
            B.state = false;
        } else if (strncmp (B.t_str, value, v_len) == 0) {
            A.state = true;
            B.state = true;
        } else
            fatalError ("unknown 3x entangle value %s", value);
    }
}

/* handy as above but allows integer value
 */
static void setEntangledValue (BPIds a_bpr, BPIds b_bpr, int value)
{
    char buf[20];
    snprintf (buf, sizeof(buf), "%d", value);
    setEntangledValue (a_bpr, b_bpr, buf);
}

/* handy as above but allows best fit to possible integer values.
 * N.B. assume vals[] sorted in ascending order and either 3 or 4 to match the given entangled pair.
 */
static void setEntangledValue (BPIds a_bpr, BPIds b_bpr, int vals[], int value)
{
    int n_ent = bool_pr[a_bpr].t_str ? 4 : 3;
    for (int i = 0; i < n_ent; i++) {
        if (value <= vals[i]) {
            setEntangledValue (a_bpr, b_bpr, vals[i]);
            return;
        }
    }
    setEntangledValue (a_bpr, b_bpr, vals[n_ent-1]);
}

/* log all string and bool settings
 */
static void logAllPrompts(void)
{
    // strings
    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (sp->p_str && sp->v_str != wifi_ssid && sp->v_str != wifi_pw)
            Serial.printf ("Setup: string %3d: %s = %s\n", i, sp->p_str, sp->v_str ? sp->v_str : "NULL");
    }

    // bools
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (bp->ent_mate == i+1)                        // only print the forward-reference pair
            Serial.printf ("Setup:    ent %3d: %s = %s\n", i, bp->p_str,
                                        getEntangledValue ((BPIds)i, (BPIds)(i+1)));
        else if (bp->ent_mate == NOMATE && bp->p_str)
            Serial.printf ("Setup:   bool %3d: %s = %s\n", i, bp->p_str,
                bp->state ? (bp->t_str ? bp->t_str : "T-NULL") : (bp->f_str ? bp->f_str :"F-NULL"));
    }

    // on/off times
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    char oostr[40+6*DAYSPERWEEK];
    size_t sl = snprintf (oostr, sizeof(oostr), "Setup: DAILYONOFF =  On");
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t on = onoff[i];
        sl += snprintf (oostr+sl, sizeof(oostr)-sl, " %02u:%02u", on/60, on%60);
    }
    Serial.println (oostr);
    sl = snprintf (oostr, sizeof(oostr), "Setup: DAILYONOFF = Off");
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t off = onoff[i+DAYSPERWEEK];
        sl += snprintf (oostr+sl, sizeof(oostr)-sl, " %02u:%02u", off/60, off%60);
    }
    Serial.println (oostr);

    // cluster commands
    char clcmdstr[40+NV_DXCLCMD_LEN];
    for (int i = 0; i < N_DXCLCMDS; i++) {
        snprintf (clcmdstr, sizeof(clcmdstr), "Setup: DXCLCMD%d %s: %s", i,
                bool_pr[DXCLCMD0_BPR+i].state ? " on" : "off", string_pr[DXCLCMD0_SPR+i].v_str);
        Serial.println (clcmdstr);
    }

    // watch lists
    for (int i = 0; i < WLID_N; i++) {
        WLInfo &wli = wl_info[i];
        Serial.printf ("Setup: WL %-4s %5s %s\n", wli.name, getEntangledValue(wli.a_bpr,wli.b_bpr),wli.wlist);
    }

    // brightness controls
    Serial.printf ("Setup: dimmable: %s\n", brDimmableOk() ? "yes" : "no");
    Serial.printf ("Setup: br onoff: %s\n", brOnOffOk() ? "yes" : "no");
}

/* prepare the shadowed prompts
 * N.B. if call this, then always call freeShadowedParams() eventually
 */
static void initShadowedParams()
{

    string_pr[LAT_SPR].v_str = (char*)malloc(string_pr[LAT_SPR].v_len = 9);
                                formatLat (de_ll.lat_d, string_pr[LAT_SPR].v_str, string_pr[LAT_SPR].v_len);
    string_pr[LNG_SPR].v_str = (char*)malloc(string_pr[LNG_SPR].v_len = 9);
                                formatLng (de_ll.lng_d, string_pr[LNG_SPR].v_str, string_pr[LNG_SPR].v_len);
    string_pr[GRID_SPR].v_str = (char*)malloc(string_pr[GRID_SPR].v_len = MAID_CHARLEN);
                                getNVMaidenhead (NV_DE_GRID, string_pr[GRID_SPR].v_str);
    snprintf (string_pr[DXPORT_SPR].v_str = (char*)malloc(8), string_pr[DXPORT_SPR].v_len = 8,
                                "%u", dx_port);
    snprintf (string_pr[RIGPORT_SPR].v_str = (char*)malloc(8), string_pr[RIGPORT_SPR].v_len = 8,
                                "%u", rig_port);

    snprintf (string_pr[ROTPORT_SPR].v_str = (char*)malloc(8), string_pr[ROTPORT_SPR].v_len = 8,
                                "%u", rot_port);
    snprintf (string_pr[FLRIGPORT_SPR].v_str = (char*)malloc(8), string_pr[FLRIGPORT_SPR].v_len = 8,
                                "%u", flrig_port);
    snprintf (string_pr[BME76DT_SPR].v_str = (char*)malloc(8), string_pr[BME76DT_SPR].v_len = 8,
                                "%.2f", temp_corr[BME_76]);
    snprintf (string_pr[BME76DP_SPR].v_str = (char*)malloc(8), string_pr[BME76DP_SPR].v_len = 8,
                                "%.3f", pres_corr[BME_76]);
    snprintf (string_pr[BME77DT_SPR].v_str = (char*)malloc(8), string_pr[BME77DT_SPR].v_len = 8,
                                "%.2f", temp_corr[BME_77]);

    snprintf (string_pr[BME77DP_SPR].v_str = (char*)malloc(8), string_pr[BME77DP_SPR].v_len = 8,
                                "%.3f", pres_corr[BME_77]);
    snprintf (string_pr[BRMIN_SPR].v_str = (char*)malloc(8), string_pr[BRMIN_SPR].v_len = 8,
                                "%u", bright_min);
    snprintf (string_pr[BRMAX_SPR].v_str = (char*)malloc(8), string_pr[BRMAX_SPR].v_len = 8,
                                "%u", bright_max);
    snprintf (string_pr[CENTERLNG_SPR].v_str = (char*)malloc(5), string_pr[CENTERLNG_SPR].v_len = 5,
                                "%.0f%c", fabsf((float)center_lng), center_lng < 0 ? 'W' : 'E');
                                // conversion to float just to avoid g++ snprintf size warning

    // value is set when displayed
    string_pr[CSELRED_SPR].v_str = (char*)calloc(4,1); string_pr[CSELRED_SPR].v_len = 4;
    string_pr[CSELGRN_SPR].v_str = (char*)calloc(4,1); string_pr[CSELGRN_SPR].v_len = 4;
    string_pr[CSELBLU_SPR].v_str = (char*)calloc(4,1); string_pr[CSELBLU_SPR].v_len = 4;
}

/* free the shadowed parameters
 */
static void freeShadowedParams()
{
    free (string_pr[LAT_SPR].v_str);
    free (string_pr[LNG_SPR].v_str);
    free (string_pr[GRID_SPR].v_str);
    free (string_pr[DXPORT_SPR].v_str);
    free (string_pr[RIGPORT_SPR].v_str);

    free (string_pr[ROTPORT_SPR].v_str);
    free (string_pr[FLRIGPORT_SPR].v_str);
    free (string_pr[BME76DT_SPR].v_str);
    free (string_pr[BME76DP_SPR].v_str);
    free (string_pr[BME77DT_SPR].v_str);

    free (string_pr[BME77DP_SPR].v_str);
    free (string_pr[BRMIN_SPR].v_str);
    free (string_pr[BRMAX_SPR].v_str);
    free (string_pr[CENTERLNG_SPR].v_str);

    free (string_pr[CSELRED_SPR].v_str);
    free (string_pr[CSELGRN_SPR].v_str);
    free (string_pr[CSELBLU_SPR].v_str);
}

/* format latitude into s[].
 */
void formatLat (float lat_d, char s[], int s_len)
{
    snprintf (s, s_len, "%.3f%c", fabsf(lat_d), lat_d < 0 ? 'S' : 'N');
}

/* format longitude into s[].
 */
void formatLng (float lng_d, char s[], int s_len)
{
    snprintf (s, s_len, "%.3f%c", fabsf(lng_d), lng_d < 0 ? 'W' : 'E');
}


/* remove all blanks throughout s IN PLACE.
 */
static void noBlanks (char *s)
{
    char c, *s_to = s;
    while ((c = *s++) != '\0')
        if (c != ' ')
            *s_to++ = c;
    *s_to = '\0';
}

/* draw the extra commands table info and table
 */
static void drawExtraCluster()
{
    // command table border
    tft.drawLine (SPIDER_BX, SPIDER_BY, SPIDER_BRX, SPIDER_BY, GRAY);
    tft.drawLine (SPIDER_BX, SPIDER_BBY, SPIDER_BRX, SPIDER_BBY, GRAY);
    tft.drawLine (SPIDER_BX, SPIDER_BY, SPIDER_BX, SPIDER_BBY, GRAY);
    tft.drawLine (SPIDER_BRX, SPIDER_BY, SPIDER_BRX, SPIDER_BBY, GRAY);

    // command table labels
    tft.setTextColor (PR_C);
    tft.setCursor (SPIDER_TX, SPIDER_TY);
    tft.print ("Cluster Commands:");

    // info about SSID
    FontWeight fw;
    FontSize fs;
    getFontStyle (&fw, &fs);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (TX_C);
    uint16_t y = R2Y(5);
    tft.setCursor (15, y);
    tft.print ("login must include SSID in range 1-99 after call.");
    tft.setCursor (15, y+=13);
    tft.print ("SSID must be unique for each cluster connection.");
    if (cs_info.call[0]) {
        tft.setCursor (15, y+=13);
        tft.printf ("Example: %s-%d", cs_info.call, DXC_DEF_SSID);
    }
    selectFontStyle (fw, fs);
}

static void drawPageButton()
{
    char buf[32];
    snprintf (buf, sizeof(buf), "< Page %d >", cur_page+1);      // user sees 1-based
    drawStringInBox (buf, page_b, false, DONE_C);
}

/* draw the Done button, depending on state and page
 */
static void drawDoneButton(bool on)
{
    if (cur_page == COLOR_PAGE)
        drawStringInBox ("Done", ctsl_done_b, on, DONE_C);
    else
        drawStringInBox ("Done", done_b, on, DONE_C);
}


/* return whether the given bool prompt is currently relevant
 */
static bool boolIsRelevant (BoolPrompt *bp)
{
    if (bp->page != cur_page)
        return (false);

#if !defined (_SHOW_ALL)

#if !defined(_USE_X11)
    if (bp == &bool_pr[X11_FULLSCRN_BPR])
        return (false);
#endif

    if (bp == &bool_pr[WIFI_BPR]) {
        #if defined(_WIFI_ALWAYS) || defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            return (good_wpa);
        #endif
    }

    if (bp == &bool_pr[CLISUDP_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[DXCLCMDPGA_BPR] || bp == &bool_pr[DXCLCMDPGB_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state || bool_pr[CLISUDP_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[DXWLISTA_BPR] || bp == &bool_pr[DXWLISTB_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (bp >= &bool_pr[DXCLCMD0_BPR] && bp < &bool_pr[DXCLCMD0_BPR + N_DXCLCMDS]) {
        int pr_page = (bp - &bool_pr[DXCLCMD0_BPR])/4 + 1;
        int cmd_page = atoi (getEntangledValue (DXCLCMDPGA_BPR, DXCLCMDPGB_BPR));
        if (cmd_page != pr_page || !bool_pr[CLUSTER_BPR].state || bool_pr[CLISUDP_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[KX3A_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            return (bool_pr[GPIOOK_BPR].state);
        #endif
    }

    if (bp == &bool_pr[KX3B_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            if (!bool_pr[KX3A_BPR].state || !bool_pr[GPIOOK_BPR].state)
                return (false);
        #endif
    }

    if (bp == &bool_pr[DATEFMT_DMYYMD_BPR]) {
        // this test works correctly for display purposes, but prevents tabbing into DMYYMD if MDY is false
        if (!bool_pr[DATEFMT_MDY_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[GPIOOK_BPR]) {
        #if !defined(_SUPPORT_NATIVE_GPIO)
            return (false);
        #endif
    }

    if (bp == &bool_pr[GPSDFOLLOW_BPR]) {
        if (!bool_pr[GPSDON_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[NMEAFOLLOW_BPR]) {
        if (!bool_pr[NMEAON_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[NMEABAUDA_BPR] || bp == &bool_pr[NMEABAUDB_BPR]) {
        if (!bool_pr[NMEAON_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[SETRADIO_BPR]) {
        if (!bool_pr[FLRIGUSE_BPR].state && !bool_pr[RIGUSE_BPR].state)
            return (false);
    }

#endif // !_SHOW_ALL

    // use by default
    return (true);
}

/* return whether the given string prompt is currently relevant
 */
static bool stringIsRelevant (StringPrompt *sp)
{
    if (sp->page != cur_page)
        return (false);

#if !defined (_SHOW_ALL)

    if (sp == &string_pr[WIFISSID_SPR] || sp == &string_pr[WIFIPASS_SPR]) {
        #if defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            if (!bool_pr[WIFI_BPR].state)
                return (false);
        #endif
    }

    if (sp == &string_pr[DXHOST_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXPORT_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXLOGIN_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state || bool_pr[CLISUDP_BPR].state)
            return (false);
    }

    if (sp >= &string_pr[DXCLCMD0_SPR] && sp < &string_pr[DXCLCMD0_SPR + N_DXCLCMDS]) {
        int pr_page = (sp - &string_pr[DXCLCMD0_SPR])/4 + 1;
        int cmd_page = atoi (getEntangledValue (DXCLCMDPGA_BPR, DXCLCMDPGB_BPR));
        if (cmd_page != pr_page || !bool_pr[CLUSTER_BPR].state || bool_pr[CLISUDP_BPR].state)
            return (false);
    }

    if (sp == &string_pr[DXWLIST_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[RIGHOST_SPR] || sp == &string_pr[RIGPORT_SPR]) {
        if (!bool_pr[RIGUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[ROTHOST_SPR] || sp == &string_pr[ROTPORT_SPR]) {
        if (!bool_pr[ROTUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[FLRIGHOST_SPR] || sp == &string_pr[FLRIGPORT_SPR]) {
        if (!bool_pr[FLRIGUSE_BPR].state)
            return (false);
    }

    if (sp == &string_pr[NTPHOST_SPR]) {
        if (strcmp (getEntangledValue (NTPA_BPR, NTPB_BPR), "host"))
            return (false);
    }

    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR] || sp == &string_pr[GRID_SPR]) {
        if (bool_pr[GEOIP_BPR].state || bool_pr[GPSDON_BPR].state || bool_pr[NMEAON_BPR].state)
            return (false);
    }

    if (sp == &string_pr[GPSDHOST_SPR]) {
        if (!bool_pr[GPSDON_BPR].state)
            return (false);
    }

    if (sp == &string_pr[NMEAFILE_SPR]) {
        if (!bool_pr[NMEAON_BPR].state)
            return (false);
    }

    if (sp == &string_pr[I2CFN_SPR]) {
        return (bool_pr[I2CON_BPR].state);
    }

    if (sp == &string_pr[BME76DT_SPR] || sp == &string_pr[BME77DT_SPR]
                    || sp == &string_pr[BME76DP_SPR] || sp == &string_pr[BME77DP_SPR]) {
        return (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
    }

    if (sp == &string_pr[BRMIN_SPR] || sp == &string_pr[BRMAX_SPR])
        return (brDimmableOk());

    if (sp == &string_pr[ADIFFN_SPR]) {
        if (!bool_pr[ADIFSET_BPR].state)
            return (false);
    }

#endif // !_SHOW_ALL

    // no objections
    return (true);
}

/* move cur_focus[cur_page] to the next tab position.
 */
static void nextTabFocus (bool backwards)
{
    /* table of ordered fields for moving to next focus with each tab.
     * N.B. group and order within to their respective pages
     */
    static const Focus tab_fields[] = {
        // page 1

        {       &string_pr[CALL_SPR], NULL},
        {       &string_pr[LAT_SPR], NULL},
        {       &string_pr[LNG_SPR], NULL},
        {       &string_pr[GRID_SPR], NULL},
        { NULL, &bool_pr[GPSDON_BPR] },
        { NULL, &bool_pr[GPSDFOLLOW_BPR] },
        {       &string_pr[GPSDHOST_SPR], NULL},
        {       &string_pr[NMEAFILE_SPR], NULL},
        { NULL, &bool_pr[NMEABAUDA_BPR] },
        { NULL, &bool_pr[GEOIP_BPR] },
        { NULL, &bool_pr[NTPA_BPR] },
        {       &string_pr[NTPHOST_SPR], NULL},
        { NULL, &bool_pr[WIFI_BPR] },
        {       &string_pr[WIFISSID_SPR], NULL},
        {       &string_pr[WIFIPASS_SPR], NULL},

        // page 2

        { NULL, &bool_pr[CLUSTER_BPR] },
        { NULL, &bool_pr[CLISUDP_BPR] },
        { NULL, &bool_pr[DXWLISTA_BPR] },
        {       &string_pr[DXWLIST_SPR], NULL},
        {       &string_pr[DXPORT_SPR], NULL},
        {       &string_pr[DXHOST_SPR], NULL},
        {       &string_pr[DXLOGIN_SPR], NULL},
        { NULL, &bool_pr[DXCLCMDPGA_BPR] },
        { NULL, &bool_pr[DXCLCMD0_BPR] },
        {       &string_pr[DXCLCMD0_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD1_BPR] },
        {       &string_pr[DXCLCMD1_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD2_BPR] },
        {       &string_pr[DXCLCMD2_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD3_BPR] },
        {       &string_pr[DXCLCMD3_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD4_BPR] },
        {       &string_pr[DXCLCMD4_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD5_BPR] },
        {       &string_pr[DXCLCMD5_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD6_BPR] },
        {       &string_pr[DXCLCMD6_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD7_BPR] },
        {       &string_pr[DXCLCMD7_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD8_BPR] },
        {       &string_pr[DXCLCMD8_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD9_BPR] },
        {       &string_pr[DXCLCMD9_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD10_BPR] },
        {       &string_pr[DXCLCMD10_SPR], NULL},
        { NULL, &bool_pr[DXCLCMD11_BPR] },
        {       &string_pr[DXCLCMD11_SPR], NULL},

        // page 3

        { NULL, &bool_pr[ROTUSE_BPR] },
        {       &string_pr[ROTPORT_SPR], NULL},
        {       &string_pr[ROTHOST_SPR], NULL},
        { NULL, &bool_pr[RIGUSE_BPR] },
        {       &string_pr[RIGPORT_SPR], NULL},
        {       &string_pr[RIGHOST_SPR], NULL},
        { NULL, &bool_pr[FLRIGUSE_BPR] },
        {       &string_pr[FLRIGPORT_SPR], NULL},
        {       &string_pr[FLRIGHOST_SPR], NULL},
        { NULL, &bool_pr[SETRADIO_BPR] },
        { NULL, &bool_pr[ADIFSET_BPR] },
        {       &string_pr[ADIFFN_SPR], NULL},
        { NULL, &bool_pr[ADIFWLISTA_BPR] },
        {       &string_pr[ADIFWL_SPR], NULL},
        { NULL, &bool_pr[ONTAWLISTA_BPR] },
        {       &string_pr[ONTAWL_SPR], NULL},

        // page 4

        {       &string_pr[CENTERLNG_SPR], NULL},
        { NULL, &bool_pr[GPIOOK_BPR] },
        { NULL, &bool_pr[I2CON_BPR] },
        {       &string_pr[I2CFN_SPR], NULL},
        {       &string_pr[BME76DT_SPR], NULL},
        {       &string_pr[BME76DP_SPR], NULL},
        {       &string_pr[BME77DT_SPR], NULL},
        {       &string_pr[BME77DP_SPR], NULL},
        { NULL, &bool_pr[KX3A_BPR] },
        {       &string_pr[BRMIN_SPR], NULL},
        {       &string_pr[BRMAX_SPR], NULL},

        // page 5

        { NULL, &bool_pr[DATEFMT_MDY_BPR] },
        { NULL, &bool_pr[LOGUSAGE_BPR] },
        { NULL, &bool_pr[WEEKDAY1MON_BPR] },
        { NULL, &bool_pr[DEMO_BPR] },
        { NULL, &bool_pr[UNITSA_BPR] },
        { NULL, &bool_pr[BEARING_BPR] },
        { NULL, &bool_pr[SHOWPIP_BPR] },
        { NULL, &bool_pr[NEWDXDEWX_BPR] },
        { NULL, &bool_pr[SPOTLBLA_BPR] },
        { NULL, &bool_pr[LBLDISTA_BPR] },
        { NULL, &bool_pr[SCROLLDIR_BPR] },
        { NULL, &bool_pr[GRAYA_BPR] },
        { NULL, &bool_pr[PANE_ROTPA_BPR] },
        { NULL, &bool_pr[MAP_ROTPA_BPR] },
        { NULL, &bool_pr[UDPSPOTS_BPR] },
        { NULL, &bool_pr[QRZBIOA_BPR] },
        { NULL, &bool_pr[AUTOMAP_BPR] },
        { NULL, &bool_pr[UDPSETSDX_BPR] },
        { NULL, &bool_pr[WEB_FULLSCRN_BPR] },
        { NULL, &bool_pr[AUTOUPA_BPR] },
        { NULL, &bool_pr[X11_FULLSCRN_BPR] },
        { NULL, &bool_pr[MAXTLEA_BPR] },

        // page 6

        {       &string_pr[CSELRED_SPR], NULL},
        {       &string_pr[CSELGRN_SPR], NULL},
        {       &string_pr[CSELBLU_SPR], NULL},

    };
    #define N_TAB_FIELDS    NARRAY(tab_fields)

    // find current position in table
    int tab_pos;
    for (tab_pos = 0; tab_pos < N_TAB_FIELDS; tab_pos++)
        if (memcmp (&cur_focus[cur_page], &tab_fields[tab_pos], sizeof(Focus)) == 0)
            break;
    if (tab_pos == N_TAB_FIELDS) {
        Serial.printf ("Setup: cur_focus[%d] not found??\n", cur_page);
        return;
    }

    // set step direction multiplier
    int step_dir = backwards ? -1 : 1;

    // search up or down from tab_pos for next relevant field
    for (int i = 1; i < N_TAB_FIELDS; i++) {
        const Focus *fp = &tab_fields[(tab_pos + step_dir*i + N_TAB_FIELDS)%N_TAB_FIELDS];
        if (fp->sp) {
            if (stringIsRelevant(fp->sp)) {
                cur_focus[cur_page] = *fp;
                return;
            }
        } else {
            if (boolIsRelevant(fp->bp)) {
                cur_focus[cur_page] = *fp;
                return;
            }
        }
    }

    Serial.printf ("Setup: new focus not found??\n");
}

/* set focus on cur_page to the given string or bool prompt, opposite assumed to be NULL.
 * N.B. effect of setting both is undefined
 */
static void setFocus (StringPrompt *sp, BoolPrompt *bp)
{
    cur_focus[cur_page].sp = sp;
    cur_focus[cur_page].bp = bp;
}

/* set focus to the first relevant prompt in the current page, unless already set
 */
static void setInitialFocus()
{
    // skip if already set
    if (cur_focus[cur_page].sp || cur_focus[cur_page].bp)
        return;

    StringPrompt *sp0 = NULL;
    BoolPrompt *bp0 = NULL;

    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp)) {
            sp0 = sp;
            break;
        }
    }

    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp)) {
            bp0 = bp;
            break;
        }
    }

    // if find both, use the one on the higher row
    if (sp0 && bp0) {
        if (sp0->p_box.y < bp0->p_box.y)
            bp0 = NULL;
        else
            sp0 = NULL;
    }

    setFocus (sp0, bp0);
}

/* find pixel offset to cursor location
 */
static uint16_t getCursorX (const StringPrompt *sp)
{
    char copy[512];
    snprintf (copy, sizeof(copy), "%.*s", sp->v_ci - sp->v_wi, sp->v_str + sp->v_wi);
    return (getTextWidth (copy));
}

/* draw cursor for cur_focus[cur_page]
 */
static void drawCursor()
{
    uint16_t y, x1;

    if (cur_focus[cur_page].sp) {
        StringPrompt *sp = cur_focus[cur_page].sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_box.x + getCursorX (sp);
    } else if (cur_focus[cur_page].bp) {
        BoolPrompt *bp = cur_focus[cur_page].bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        if (bp->p_str) {
            // cursor in prompt
            x1 = bp->p_box.x;
        } else {
            // cursor in state
            x1 = bp->s_box.x;
        }
    } else {
        return;
    }

    uint16_t x2 = x1 + PR_W;

    tft.drawLine (x1, y, x2, y, CURSOR_C);
    tft.drawLine (x1, y+1, x2, y+1, CURSOR_C);
}

/* erase cursor for cur_focus[cur_page]
 */
static void eraseCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus[cur_page].sp) {
        StringPrompt *sp = cur_focus[cur_page].sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_box.x + getCursorX (sp);
        x2 = x1+PR_W;
    } else if (cur_focus[cur_page].bp) {
        BoolPrompt *bp = cur_focus[cur_page].bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        x1 = bp->p_box.x;
        x2 = bp->p_box.x+PR_W;
    } else {
        return;
    }

    tft.drawLine (x1, y, x2, y, BG_C);
    tft.drawLine (x1, y+1, x2, y+1, BG_C);
}

/* draw the prompt of the given StringPrompt, if any
 */
static void drawSPPrompt (StringPrompt *sp)
{
    if (sp->p_str) {
        tft.setTextColor (PR_C);
        tft.setCursor (sp->p_box.x, sp->p_box.y+sp->p_box.h-PR_D);
        tft.print(sp->p_str);
    }
#ifdef _MARK_BOUNDS
    drawSBox (sp->p_box, GRAY);
#endif // _MARK_BOUNDS
}

/* erase the prompt of the given StringPrompt
 */
static void eraseSPPrompt (const StringPrompt *sp)
{
    fillSBox (sp->p_box, BG_C);
}

/* erase the value of the given StringPrompt
 */
static void eraseSPValue (const StringPrompt *sp)
{
    fillSBox (sp->v_box, BG_C);
}

/* draw the value of the given StringPrompt.
 * adjust v_ci and v_wi to insure cursor still within v_box.
 * N.B. we assume v_box already erased
 */
static void drawSPValue (StringPrompt *sp)
{
    // prep writing into v_box
    tft.setTextColor (TX_C);
    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
    const uint16_t max_w = sp->v_box.w - PR_W;          // max visible string width, pixels

    // printf ("drawSPValue '%-*s' c= %2d w= %2d .. ", sp->v_len, sp->v_str, sp->v_ci, sp->v_wi);

    // check left end
    size_t v_len = strlen(sp->v_str);
    if (sp->v_ci > v_len)
        sp->v_ci = v_len;
    if (sp->v_ci < sp->v_wi)
        sp->v_wi = sp->v_ci;

    // check right end
    char *w_str = sp->v_str + sp->v_wi;                 // ptr to left v_str in box
    char *w_dup = strdup (w_str);                       // copy for maxStringW
    (void) maxStringW (w_dup, max_w);                   // truncate w_dup IN PLACE to fit within max_w
    size_t str_l = strlen (w_dup);                      // get length that fits

    // shift text to insure cursor still within box
    if (sp->v_ci > sp->v_wi + str_l) {
        sp->v_wi = sp->v_ci - str_l;
        w_str = sp->v_str + sp->v_wi;
        free (w_dup);
        w_dup = strdup (w_str);
        maxStringW (w_dup, max_w);
    }

    // print and free
    tft.print (w_dup);
    free (w_dup);

    // printf ("'%-*s' c= %2d w= %2d\n", sp->v_len, sp->v_str, sp->v_ci, sp->v_wi);

#ifdef _MARK_BOUNDS
    drawSBox (sp->v_box, GRAY);
#endif // _MARK_BOUNDS
}

/* draw both prompt and value of the given StringPrompt
 */
static void drawSPPromptValue (StringPrompt *sp)
{
    drawSPPrompt (sp);
    drawSPValue (sp);
}

/* erase both prompt and value of the given StringPrompt
 */
static void eraseSPPromptValue (StringPrompt *sp)
{
    eraseSPPrompt (sp);
    eraseSPValue (sp);
}

/* draw the prompt of the given BoolPrompt, if any.
 */
static void drawBPPrompt (BoolPrompt *bp)
{
    if (!bp->p_str)
        return;

    #ifdef _WIFI_ALWAYS
        if (bp == &bool_pr[WIFI_BPR])
            tft.setTextColor (PR_C);            // required wifi is just a passive prompt but ...
        else
    #endif
    tft.setTextColor (BUTTON_C);                // ... others are a question.


    tft.setCursor (bp->p_box.x, bp->p_box.y+bp->p_box.h-PR_D);
    tft.print(bp->p_str);

#ifdef _MARK_BOUNDS
    drawSBox (bp->p_box, GRAY);
#endif // _MARK_BOUNDS
}

/* draw the state of the given BoolPrompt, if any.
 * N.B. beware of a few special cases
 */
static void drawBPState (BoolPrompt *bp)
{
    bool show_t = bp->state && bp->t_str;
    bool show_f = !bp->state && bp->f_str;

    if (show_t || show_f) {

        // dx commands are unusual as they are colored like buttons to appear as active
        if (bp >= &bool_pr[DXCLCMD0_BPR] && bp < &bool_pr[DXCLCMD0_BPR + N_DXCLCMDS])
            tft.setTextColor (BUTTON_C);
        else
            tft.setTextColor (TX_C);

        tft.setCursor (bp->s_box.x, bp->s_box.y+bp->s_box.h-PR_D);
        fillSBox (bp->s_box, BG_C);
        if (show_t)
            tft.print(bp->t_str);
        if (show_f)
            tft.print(bp->f_str);
    #ifdef _MARK_BOUNDS
        drawSBox (bp->s_box, GRAY);
    #endif // _MARK_BOUNDS
    }
}

/* erase state of the given BoolPrompt, if any
 */
static void eraseBPState (BoolPrompt *bp)
{
    fillSBox (bp->s_box, BG_C);
}


/* draw both prompt and state of the given BoolPrompt
 */
static void drawBPPromptState (BoolPrompt *bp)
{
    drawBPPrompt (bp);
    drawBPState (bp);
}


/* erase prompt of the given BoolPrompt 
 */
static void eraseBPPrompt (BoolPrompt *bp)
{
    fillSBox (bp->p_box, BG_C);
}

/* erase both prompt and state of the given BoolPrompt
 */
static void eraseBPPromptState (BoolPrompt *bp)
{
    eraseBPPrompt (bp);
    eraseBPState (bp);
}


/* show msg in the given field, or default if not supplied.
 * if restore then after showing the message, dwell a bit then restore the field.
 */
static void flagErrField (StringPrompt *sp, bool restore = false, const char *msg = NULL)
{
    // erase and prep
    eraseSPValue (sp);
    tft.setTextColor (ERR_C);

    // show a copy to insure it fits
    if (!msg)
        msg = "Err";
    char *msg_dup = strdup (msg);                       // N.B. free!
    (void) maxStringW (msg_dup, sp->v_box.w);
    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
    tft.print (msg_dup);
    free (msg_dup);

    if (restore) {

        // erase all pending input before starting the dwell
        drainTouch();

        // dwell then restore value with handy cursor placed to edit
        wdDelay(ERRDWELL_MS);
        eraseCursor();
        eraseSPValue (sp);
        drawSPValue (sp);
        setFocus (sp, NULL);
        drawCursor();

    }
}

/* update interaction if sp is one of LAT/LNG/GRID_SPR.
 * and set ll_edited.
 */
static void checkLLGEdit(const StringPrompt *sp)
{
    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR]) {

        // convert to grid if possible
        LatLong ll;
        strTrimAll (string_pr[LAT_SPR].v_str);
        strTrimAll (string_pr[LNG_SPR].v_str);
        if (latSpecIsValid (string_pr[LAT_SPR].v_str, ll.lat_d)
                        && lngSpecIsValid (string_pr[LNG_SPR].v_str, ll.lng_d)) {
            ll.normalize();
            ll2maidenhead (string_pr[GRID_SPR].v_str, ll);
            eraseSPValue (&string_pr[GRID_SPR]);
            drawSPValue (&string_pr[GRID_SPR]);
        } else {
            flagErrField (&string_pr[GRID_SPR]);
        }

        ll_edited = true;

    } else if (sp == &string_pr[GRID_SPR]) {

        // convert to ll if possible
        LatLong ll;
        strTrimAll (sp->v_str);
        if (maidenhead2ll (ll, sp->v_str)) {
            formatLat (ll.lat_d, string_pr[LAT_SPR].v_str, string_pr[LAT_SPR].v_len);
            eraseSPValue (&string_pr[LAT_SPR]);
            drawSPValue (&string_pr[LAT_SPR]);
            formatLng (ll.lng_d, string_pr[LNG_SPR].v_str, string_pr[LNG_SPR].v_len);
            eraseSPValue (&string_pr[LNG_SPR]);
            drawSPValue (&string_pr[LNG_SPR]);
        } else {
            flagErrField (&string_pr[LAT_SPR]);
            flagErrField (&string_pr[LNG_SPR]);
        }

        ll_edited = true;
    }
}

/* draw the tooltip reminder across the very bottom
 */
void drawToolTipReminder(void)
{
    FontWeight fw;
    FontSize fs;
    getFontStyle (&fw, &fs);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t tt_w = getTextWidth (tt_reminder);
    tft.setTextColor(PR_C);
    tft.setCursor ((tft.width() - tt_w)/2, TT_Y0);
    tft.print (tt_reminder);
    selectFontStyle (fw, fs);
}

/* draw the virtual keyboard
 */
static void drawKeyboard()
{
    tft.fillRect (0, KB_Y0, tft.width(), tft.height()-KB_Y0-1, BG_C);
    tft.setTextColor (KF_C);

    for (int r = 0; r < NQR; r++) {
        uint16_t y = r * KB_CHAR_H + KB_Y0 + KB_CHAR_H;
        const OneKBKey *row = qwerty[r];
        for (int c = 0; c < NQC; c++) {
            const OneKBKey *kp = &row[c];
            char n = kp->normal;
            if (n) {
                uint16_t x = qroff[r] + c * KB_CHAR_W;

                // shifted char above left
                tft.setCursor (x+TF_INDENT, y-KB_CHAR_H/3-F_DESCENT);
                tft.print((char)kp->shifted);

                // non-shifted below right
                tft.setCursor (x+BF_INDENT, y-F_DESCENT);
                tft.print(n);

                // key border
                tft.drawRect (x, y-KB_CHAR_H, KB_CHAR_W, KB_CHAR_H, KB_C);
            }
        }
    }

    drawStringInBox ("", space_b, false, KF_C);
    drawStringInBox ("Del", delete_b, false, DEL_C);
    drawStringInBox ("<==", left_b, false, DEL_C);
    drawStringInBox ("==>", right_b, false, DEL_C);
}



/* convert a screen coord to its char value, if any.
 */
static bool s2char (SCoord &s, char &kbchar)
{
    // only one button on color page
    if (cur_page == COLOR_PAGE) {
        if (inBox(s, ctsl_done_b)) {
            kbchar = CHAR_NL;
            return (true);
        } else
            return (false);
    }

    // only one button on on-off page
    if (cur_page == ONOFF_PAGE) {
        if (inBox(s, done_b)) {
            kbchar = CHAR_NL;
            return (true);
        } else
            return (false);
    }

    // check main qwerty
    if (cur_page >= KBPAGE_FIRST && cur_page <= KBPAGE_LAST) {
        if (s.y >= KB_Y0) {
            uint16_t kb_y = s.y - KB_Y0;
            uint8_t row = kb_y/KB_CHAR_H;
            if (row < NQR && s.x > qroff[row]) {
                uint8_t col = (s.x-qroff[row])/KB_CHAR_W;
                if (col < NQC) {
                    const OneKBKey *kp = &qwerty[row][col];
                    char norm_char = kp->normal;
                    if (norm_char) {
                        // actually use shifted char if in top half
                        if (s.y < KB_Y0+row*KB_CHAR_H+KB_CHAR_H/2)
                            kbchar = kp->shifted;
                        else
                            kbchar = norm_char;
                        return (true);
                    }
                }
            }
        }
    }

    // check a few more special boxes

    if (inBox (s, space_b)) {
        kbchar = CHAR_SPACE;
        return (true);
    }
    if (inBox (s, delete_b)) {
        kbchar = CHAR_DEL;
        return (true);
    }
    if (inBox (s, left_b)) {
        kbchar = CHAR_LEFT;
        return (true);
    }
    if (inBox (s, right_b)) {
        kbchar = CHAR_RIGHT;
        return (true);
    }
    if (inBox (s, done_b)) {
        kbchar = CHAR_NL;
        return (true);
    }

    // s is not on the virtual keyboard
    return (false);
}

/* display an entangled pair of bools states:
 *   if A->t_str 4 states: FF A->f_str TF A->t_str FT B->f_str TT B->t_str
 *   else        3 state:s FX A->f_str FT B->f_str TT B->t_str
 * N.B. assumes both A and B's state boxes, ie s_box, are in identical locations.
 * 3x entangled: a: FX  b: TF  c: TT
 * 4x entangled: a: FF  b: TF  c: FT  d: TT
 */
static void drawEntangledBools (BoolPrompt *A, BoolPrompt *B)
{
    if (A->t_str) {
        // 4-states
        if (!B->state)
            drawBPState (A);
        else {
            if (!A->state) {
                // FT: must temporarily turn off B to show its f_str
                B->state = false;
                drawBPState (B);
                B->state = true;
            } else
                drawBPState (B);
        }
    } else {
        // 3 states
        if (A->state)
            drawBPState (B);
        else
            drawBPState (A);
    }
}

/* perform action resulting from tapping the given BoolPrompt.
 * handle both singles and entangled pairs.
 */
static void engageBoolTap (BoolPrompt *bp)
{
    // erase current cursor position
    eraseCursor ();

    // update state
    if (bp->ent_mate == NOMATE) {

        // just a lone bool
        bp->state = !bp->state;
        drawBPState (bp);

        // move cursor to tapped field
        setFocus (NULL, bp);
        drawCursor ();

    } else {

        // this is one of an entangled pair. N.B. primary is always lower in memory.
        // 3x entangled: a: FX  b: TF  c: TT
        // 4x entangled: a: FF  b: TF  c: FT  d: TT
        BoolPrompt *mate = &bool_pr[bp->ent_mate];
        BoolPrompt *A = mate < bp ? mate : bp;
        BoolPrompt *B = mate < bp ? bp : mate;

        // roll choice forward, regardless of which was tapped
        if (A->t_str) {
            // 4-states: FF -> TF -> FT -> TT -> ...
            if (A->state) {
                A->state = false;
                B->state = !B->state;
            } else {
                A->state = true;
            };
                
        } else {
            // 3-states: FX -> TF -> TT -> ...
            if (A->state) {
                if (B->state) {
                    A->state = false;
                    B->state = false;
                } else {
                    B->state = true;
                }
            } else {
                A->state = true;
                B->state = false;
            }
        }

        // draw new state
        drawEntangledBools (A, B);

        // move cursor to A
        setFocus (NULL, A);
        drawCursor ();
    }
}



/* find whether s is in any relevant string_pr.
 * if so return true and set *spp, else return false.
 */
static bool tappedStringPrompt (SCoord &s, StringPrompt **spp)
{
    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (!stringIsRelevant(sp))
            continue;
        if ((sp->p_str && inBox (s, sp->p_box)) || (sp->v_str && inBox (s, sp->v_box))) {
            *spp = sp;
            return (true);
        }
    }
    return (false);
}

/* find whether s is in any relevant bool_pr.
 * require s within prompt or state box.
 * if so return true and set *bpp, else return false.
 */
static bool tappedBool (SCoord &s, BoolPrompt **bpp)
{
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (!boolIsRelevant(bp))
            continue;
        if ((bp->p_str && inBox (s, bp->p_box))
               || (((bp->state && bp->t_str) || (!bp->state && bp->f_str)) && inBox (s, bp->s_box))) {
            *bpp = bp;
            return (true);
        }
    }
    return (false);
}


/* interpret the current state of the NTPA_BPR/NTPB_BPR entangled bools as a NTPStateCode
 */
static NTPStateCode getNTPStateCode (void)
{
    int i = getEntangledIndex (NTPA_BPR, NTPB_BPR);
    if (i >= 0 && i < NTPSC_N)
        return ((NTPStateCode)i);
    fatalError ("Bogus ntp entangled index: %d\n", i);
    return (NTPSC_NO);         // lint
}

/* draw the NTP prompts based on the current state of entangled pair NTPA_BPR/NTPB_BPR
 */
static void drawNTPPrompts (void)
{
    NTPStateCode sc = getNTPStateCode();
    if (sc == NTPSC_HOST) {
        // show ntp prompt and host over entangled prompt
        eraseBPState (&bool_pr[NTPA_BPR]);
        eraseBPState (&bool_pr[NTPB_BPR]);
        drawSPPromptValue (&string_pr[NTPHOST_SPR]);
    } else {
        // hide host and show entangled prompt
        eraseSPPromptValue (&string_pr[NTPHOST_SPR]);
        drawEntangledBools (&bool_pr[NTPA_BPR], &bool_pr[NTPB_BPR]);
    }
}

/* draw all prompts and values for the current page
 */
static void drawCurrentPageFields()
{
    // draw relevant string prompts on this page
    for (int i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp))
            drawSPPromptValue(sp);
    }

    // draw relevant bool prompts on this page
    for (int i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp)) {
            drawBPPrompt (bp);
            if (bp->ent_mate == i+1)
                drawEntangledBools(bp, &bool_pr[i+1]);
            else if (bp->ent_mate == NOMATE)
                drawBPState (bp);
        }
    }

    // ntp is unusual in that it overlays bool prompt with string prompt
    if (cur_page == bool_pr[NTPA_BPR].page)
        drawNTPPrompts();

    // draw spider header if appropriate
    if (cur_page == SPIDER_PAGE && bool_pr[CLUSTER_BPR].state && !bool_pr[CLISUDP_BPR].state)
        drawExtraCluster();

    #if defined(_WIFI_ALWAYS)
        // show prompt but otherwise is not relevant
        if (bool_pr[WIFI_BPR].page == cur_page)
            drawBPPrompt (&bool_pr[WIFI_BPR]);
    #endif

    // set initial focus
    setInitialFocus ();
    drawCursor ();
}


/* update the color component based on s known to be within csel_ctl_b.
 * return if real else just showing tooltip.
 */
static bool editCSelBoxColor (const SCoord &s, TouchType tt, uint8_t &r, uint8_t &g, uint8_t &b)
{
    // offset withing csel_ctl_b
    uint16_t dx = s.x - CSEL_SCX;
    uint16_t dy = s.y - CSEL_SCY;

    // check whether over color or numeric value
    if (dx < CSEL_SCW) {

        // inside color box: new color depends on s.x
        uint16_t new_v = X2V(s.x);

        // update one component to new color depending on y, leave the others unchanged
        if (dy < CSEL_SCYG/2 + CSEL_SCH) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click along this color scale to set value of red");
                return (false);
            }
            r = new_v;                          // tapped first row
        } else if (dy < CSEL_SCYG/2 + CSEL_SCH + CSEL_SCYG + CSEL_SCH) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click along this color scale to set value of green");
                return (false);
            }
            g = new_v;                          // tapped second row
        } else {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click along this color scale to set value of blue");
                return (false);
            }
            b = new_v;                          // tapped third row
        }

    } else if (dx > CSEL_SCW + CSEL_VDX) {

        // near a numeric value: just set focus to allow normal editing
        if (dy < CSEL_SCH + CSEL_SCYG/2) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to edit red component value from 1-255");
                return (false);
            }
            cur_focus[cur_page].sp = &string_pr[CSELRED_SPR];
        } else if (dy < 2*CSEL_SCH + 3*CSEL_SCYG/2) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to edit green component value from 1-255");
                return (false);
            }
            cur_focus[cur_page].sp = &string_pr[CSELGRN_SPR];
        } else {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to edit blue component value from 1-255");
                return (false);
            }
            cur_focus[cur_page].sp = &string_pr[CSELBLU_SPR];
        }
    }

    // real
    return (true);
}

/* internal version.
 * N.B. this always returns a finite size; use getSpotLabelType() to decide whether/how to draw at all.
 */
static int getRawSpotRadius (ColSelPrompt &csp)
{
    return (2*(csp.t_state ? RAWTHINPATHSZ : RAWWIDEPATHSZ));
}

/* draw a color selector demo
 */
static void drawCSelDemoSwatch (ColSelPrompt &csp)
{
    // get raw location and dimensions
    int raw_x = csp.d_box.x * tft.SCALESZ;
    int raw_y = csp.d_box.y * tft.SCALESZ;
    int raw_w = (csp.d_box.w-1) * tft.SCALESZ;
    int raw_h = csp.t_state ? RAWTHINPATHSZ : RAWWIDEPATHSZ;

    // erase previous by filling in the same gradient as drawCSelInitGUI()
    int can_radius = 2 * RAWWIDEPATHSZ / tft.SCALESZ + 1;               // worst-case with border
    for (int dx = 0; dx < csp.d_box.w; dx++) {
        int v = 255 * powf ((float)dx / (csp.d_box.w-1), CSEL_GAMMA);
        uint16_t c = RGB565 (v, v, v);
        tft.fillRect (csp.d_box.x+dx, csp.d_box.y-can_radius, 1, 2*can_radius, c);
    }

    // get color
    uint16_t c = RGB565(csp.r, csp.g, csp.b);

    // check for dashed, else solid
    if (CSEL_DASHOK(csp) && csp.a_state) {
        for (int i = 0; i < CSEL_NDASH; i += 2) {
            uint16_t dx = i * raw_w / CSEL_NDASH;
            tft.drawLineRaw (raw_x + dx, raw_y, raw_x + dx + raw_w/CSEL_NDASH, raw_y, raw_h, c);
        }
    } else {
        tft.drawLineRaw (raw_x, raw_y, raw_x + raw_w, raw_y, raw_h, c);
    }

    // dot 
    int radius = getRawSpotRadius (csp);
    drawSpotDot (raw_x + radius, raw_y, radius, LOME_TXEND, c);
    drawSpotDot (raw_x + raw_w - radius + 1, raw_y, radius, LOME_RXEND, c);
}

/* draw the dash control tick box, if used
 */
static void drawCSelDashTickBox(const ColSelPrompt &p)
{
    if (CSEL_DASHOK(p)) {
        fillSBox (p.a_box, p.a_state ? CSEL_TBCOL : RA8875_BLACK );
        drawSBox (p.a_box, RA8875_WHITE);
    }
}

/* draw a color selector prompt editing box, on or off depending on state.
 */
static void drawCSelEditingTickBox (const ColSelPrompt &p)
{
    fillSBox (p.e_box, p.e_state ? CSEL_TBCOL : RA8875_BLACK );
    drawSBox (p.e_box, RA8875_WHITE);
}

/* draw a color selector prompt on/off box, depending on state.
 */
static void drawCSelOnOffTickBox (const ColSelPrompt &p)
{
    if (CSEL_ONOFFOK(p)) {
        fillSBox (p.o_box, p.o_state ? CSEL_TBCOL : RA8875_BLACK);
        drawSBox (p.o_box, RA8875_WHITE);
    }
}

/* draw a color selector prompt thcinkess box, depending on state.
 */
static void drawCSelThicknessTickBox (const ColSelPrompt &p)
{
    fillSBox (p.t_box, p.t_state ? CSEL_TBCOL : RA8875_BLACK);
    drawSBox (p.t_box, RA8875_WHITE);
}

/* erase any possible existing then draw a new marker to indicate the current slider position at x,y.
 * beware sides.
 */
static void drawCSelCursor (uint16_t x, int16_t y)
{
    #define _CSEL_CR    2
    #define _CSEL_CH    (2*_CSEL_CR)

    tft.fillRect (CSEL_SCX, y, CSEL_SCW, _CSEL_CH, RA8875_BLACK);

    if (x < CSEL_SCX+_CSEL_CR)
        x = CSEL_SCX+_CSEL_CR;
    else if (x > CSEL_SCX + CSEL_SCW-_CSEL_CR-1)
        x = CSEL_SCX + CSEL_SCW-_CSEL_CR-1;
    tft.fillRect (x-_CSEL_CR, y, 2*_CSEL_CR+1, _CSEL_CH, CSEL_SCM_C);
}

/* indicate the color used by the given selector and optionally set new cursor locations for each number.
 */
static void drawCSelPromptColor (const ColSelPrompt &p, bool set_cursors)
{
    // draw the cursors
    drawCSelCursor (V2X(p.r), CSEL_SCY+CSEL_SCH);
    drawCSelCursor (V2X(p.g), CSEL_SCY+2*CSEL_SCH+CSEL_SCYG);
    drawCSelCursor (V2X(p.b), CSEL_SCY+3*CSEL_SCH+2*CSEL_SCYG);

    // handy access to the three color string values
    StringPrompt &rs = string_pr[CSELRED_SPR];
    StringPrompt &gs = string_pr[CSELGRN_SPR];
    StringPrompt &bs = string_pr[CSELBLU_SPR];

    // set strings -- already checked for 0..255
    snprintf (rs.v_str, rs.v_len, "%d", p.r);
    snprintf (gs.v_str, gs.v_len, "%d", p.g);
    snprintf (bs.v_str, bs.v_len, "%d", p.b);

    // update cursor position
    if (set_cursors) {
        rs.v_ci = strlen (rs.v_str);
        gs.v_ci = strlen (gs.v_str);
        bs.v_ci = strlen (bs.v_str);
    }

    // draw the value boxes
    eraseSPValue (&rs);
    drawSPValue  (&rs);
    eraseSPValue (&gs);
    drawSPValue  (&gs);
    eraseSPValue (&bs);
    drawSPValue  (&bs);
    drawCursor ();
}



/* draw the one-time color selector GUI features
 */
static void drawCSelInitGUI()
{
    // draw color control sliders
    fillSBox (csel_ctl_b, RA8875_BLACK);
    const uint16_t y0 = CSEL_SCY;
    const uint16_t y1 = CSEL_SCY+CSEL_SCH+CSEL_SCYG;
    const uint16_t y2 = CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG;
    for (int x = CSEL_SCX; x < CSEL_SCX+CSEL_SCW; x++) {
        uint8_t new_v = X2V(x);
        tft.drawLine (x, y0, x, y0+CSEL_SCH-1, 1, RGB565 (new_v, 0, 0));
        tft.drawLine (x, y1, x, y1+CSEL_SCH-1, 1, RGB565 (0, new_v, 0));
        tft.drawLine (x, y2, x, y2+CSEL_SCH-1, 1, RGB565 (0, 0, new_v));
    }

    // add borders
    tft.drawRect (CSEL_SCX, CSEL_SCY, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);
    tft.drawRect (CSEL_SCX, CSEL_SCY+CSEL_SCH+CSEL_SCYG, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);
    tft.drawRect (CSEL_SCX, CSEL_SCY+2*CSEL_SCH+2*CSEL_SCYG, CSEL_SCW, CSEL_SCH, CSEL_SCB_C);

    // label tick box headings
    // N.B. must restore default font after using smaller font
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (PR_C);
    tft.setCursor (CSEL_COL1X+4,         R2Y(0)+PR_H/2); tft.print ("On");
    tft.setCursor (CSEL_COL1X+CSEL_EDX,  R2Y(0)+PR_H/2); tft.print ("Edit");
    tft.setCursor (CSEL_COL1X+CSEL_ADX,  R2Y(0)+PR_H/2); tft.print ("Dash");
    tft.setCursor (CSEL_COL1X+CSEL_TDX,  R2Y(0)+PR_H/2); tft.print ("Thin");
    tft.setCursor (CSEL_COL2X+4,         R2Y(6)+PR_H/2); tft.print ("On");
    tft.setCursor (CSEL_COL2X+CSEL_EDX,  R2Y(6)+PR_H/2); tft.print ("Edit");
    tft.setCursor (CSEL_COL2X+CSEL_ADX,  R2Y(6)+PR_H/2); tft.print ("Dash");
    tft.setCursor (CSEL_COL2X+CSEL_TDX,  R2Y(6)+PR_H/2); tft.print ("Thin");

    // N.B. must restore default font after using smaller font
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (TX_C);
    tft.setCursor (350, 27); tft.print ("Color Editor");

    // draw gradient behind swatches -- N.B. match drawCSelDemoSwatch()
    for (int dx = 0; dx < CSEL_DW; dx++) {
        int v = 255 * powf ((float)dx / (CSEL_DW-1), CSEL_GAMMA);
        uint16_t c = RGB565 (v, v, v);
        tft.fillRect (CSEL_COL1X+CSEL_DDX1+dx, R2Y(1), 1, R2Y(13)-R2Y(1)-5, c);
        tft.fillRect (CSEL_COL2X+CSEL_DDX2+dx, R2Y(7), 1, R2Y(13)-R2Y(7)-5, c);
    }

    // draw tick boxes, their prompts and set color editing sliders from the one that is set
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        tft.setTextColor (TX_C);
        tft.setCursor (p.p_box.x, p.p_box.y+p.p_box.h-PR_D);
        tft.printf ("%s:", p.p_str);
        drawCSelEditingTickBox (p);
        drawCSelOnOffTickBox (p);
        drawCSelDemoSwatch (p);
        drawCSelDashTickBox(p);
        drawCSelThicknessTickBox(p);
        if (p.e_state)
            drawCSelPromptColor (p, true);
    }

    // draw Save controls
    tft.setTextColor (TX_C);
    tft.setCursor (CTSL_SL_X, CTSL_Y + PR_A);
    tft.print ("Save to:");
    drawStringInBox (" A ", ctsl_save1_b, false, BUTTON_C);
    drawStringInBox (" B ", ctsl_save2_b, false, BUTTON_C);

    // draw Load controls
    tft.setTextColor (TX_C);
    tft.setCursor (CTSL_LL_X, CTSL_Y + PR_A);
    tft.print ("Load from:");
    drawStringInBox (" A ", ctsl_load1_b, false, BUTTON_C);
    drawStringInBox (" B ", ctsl_load2_b, false, BUTTON_C);
    drawStringInBox ("pskreporter", ctsl_loadp_b, false, BUTTON_C);
    drawStringInBox ("default", ctsl_loadd_b, false, BUTTON_C);

    // draw the color value fields
    drawCurrentPageFields();
}


static void colorTableAck (const char *prompt, const SBox &box)
{
    drawStringInBox (prompt, box, true, BUTTON_C);
    wdDelay(BTNDWELL_MS);
    drawStringInBox (prompt, box, false, BUTTON_C);
}

/* save the current colors and states in the given NV table.
 * prompt and box are to show some feedback.
 */
static void saveColorTable (int tbl_AB, NV_Name a_nv, NV_Name t_nv, NV_Name o_nv,
const char *prompt, const SBox &box)
{
    // fill arrays and masks from csel_pr[]
    uint8_t r[N_CSPR], g[N_CSPR], b[N_CSPR];
    uint32_t a_mask = 0, t_mask = 0, o_mask = 0;
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &csp = csel_pr[i];
        r[i] = csp.r;
        g[i] = csp.g;
        b[i] = csp.b;
        if (csp.a_state) a_mask |= (1<<i);
        if (csp.t_state) t_mask |= (1<<i);
        if (csp.o_state) o_mask |= (1<<i);
    }

    // save to NV
    NVWriteColorTable (tbl_AB, r, g, b);
    NVWriteUInt32 (a_nv, a_mask);
    NVWriteUInt32 (t_nv, t_mask);
    NVWriteUInt32 (o_nv, o_mask);

    // ack
    colorTableAck (prompt, box);
}

/* load the colors and states from the given NV table.
 * prompt and box are to show some feedback.
 */
static void loadColorTable (int tbl_ab, NV_Name a_nv, NV_Name t_nv, NV_Name o_nv,
const char *prompt, const SBox &box)
{
    uint8_t r[N_CSPR], g[N_CSPR], b[N_CSPR];
    if (NVReadColorTable (tbl_ab, r, g, b)) {

        // fill colors
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &csp = csel_pr[i];
            csp.r = r[i];
            csp.g = g[i];
            csp.b = b[i];
        }

        // fill states too but ok if not saved yet for compatibility
        uint32_t a_mask, t_mask, o_mask;
        if (NVReadUInt32 (a_nv, &a_mask) && NVReadUInt32 (t_nv, &t_mask) && NVReadUInt32 (o_nv, &o_mask) ) {

            for (int i = 0; i < N_CSPR; i++) {
                ColSelPrompt &csp = csel_pr[i];
                csp.a_state = (a_mask & (1 << i)) != 0;
                csp.t_state = (t_mask & (1 << i)) != 0;
                csp.o_state = (o_mask & (1 << i)) != 0;
            }
        }

        // refresh
        drawCSelInitGUI();

        colorTableAck (prompt, box);

    } else {

        // show err briefly
        drawStringInBox ("??", box, false, ERR_C);
        wdDelay(ERRDWELL_MS);
        drawStringInBox (prompt, box, false, BUTTON_C);
    }
}

/* load the path colors to match pskreporter.
 */
static void loadPSKColorTable (void)
{
    // define pskreporter colors, thanks to G6NHU
    #define N_PSK       (BAND2_CSPR-BAND160_CSPR+1)     // inclusive
    static const uint8_t psk_r[N_PSK] = {
        156, 213,   0,  90, 131, 238, 246, 197, 164, 238, 238, 238
    };
    static const uint8_t psk_g[N_PSK] = {
        250,  89,   0,  89, 214, 198, 242, 161,  48, 113,  60,  56
    };
    static const uint8_t psk_b[N_PSK] = {
         74, 222, 131, 246, 115,  65, 123, 106,  41, 180,  32, 148
    };

    // load into csel_pr[]
    for (int i = 0; i < N_PSK; i++) {
        csel_pr[BAND160_CSPR+i].r = psk_r[i];
        csel_pr[BAND160_CSPR+i].g = psk_g[i];
        csel_pr[BAND160_CSPR+i].b = psk_b[i];
    }

    // ack
    colorTableAck ("pskreporter", ctsl_loadp_b);

    // redraw to show done
    drawCSelInitGUI();
}

/* load the default colors
 */
static void loadDefaultColorTable (void)
{
    // load def_c into csel_pr[]
    for (int i = 0; i < N_CSPR; i++) {
        csel_pr[i].r = RGB565_R(csel_pr[i].def_c);
        csel_pr[i].g = RGB565_G(csel_pr[i].def_c);
        csel_pr[i].b = RGB565_B(csel_pr[i].def_c);
    }

    // ack
    colorTableAck ("default", ctsl_loadd_b);

    // redraw to show done
    drawCSelInitGUI();
}

/* update the current color selection from the 3 string prompts.
 */
static void handleCSelKB (void)
{
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        if (p.e_state) {
            p.r = (uint8_t) CLAMPF (atoi (string_pr[CSELRED_SPR].v_str), 0, 255);
            p.g = (uint8_t) CLAMPF (atoi (string_pr[CSELGRN_SPR].v_str), 0, 255);
            p.b = (uint8_t) CLAMPF (atoi (string_pr[CSELBLU_SPR].v_str), 0, 255);
            drawCSelPromptColor (p, true);
            drawCSelDemoSwatch (p);
            break;
        }
    }
}


/* handle a possible touch event while on the color selection page.
 * return whether ours.
 * N.B. we assume ctsl_done_b has already been handled
 */
static bool handleCSelTouch (TouchType tt, SCoord &s)
{
    bool ours = false;

    // check for changing color of the current selection
    if (inBox (s, csel_ctl_b)) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (p.e_state) {
                editCSelBoxColor(s, tt, p.r, p.g, p.b);
                drawCSelPromptColor(p, true);
                drawCSelDemoSwatch (p);
                break;
            }
        }
        ours = true;
    }

    // else check for changing dashed state
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (CSEL_DASHOK(p) && inBox (s, p.a_box)) {
                if (tt == TT_TAP_BX)
                    tooltip (s, "Whether this line is drawn solid or dashed");
                else {
                    // toggle and redraw
                    p.a_state = !p.a_state;
                    drawCSelDemoSwatch (p);
                    drawCSelDashTickBox(p);
                }
                ours = true;
                break;
            }
        }
    }

    // else check for changing the selection being edited
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (inBox (s, p.e_box) && !p.e_state) {
                if (tt == TT_TAP_BX)
                    tooltip (s, "Select to edit the color of this line");
                else {
                    // clicked an off box, make it the only one on (ignore clicking an on box)
                    for (int j = 0; j < N_CSPR; j++) {
                        ColSelPrompt &pj = csel_pr[j];
                        if (pj.e_state) {
                            pj.e_state = false;
                            drawCSelEditingTickBox (pj);
                        }
                    }
                    p.e_state = true;
                    drawCSelEditingTickBox (p);
                    drawCSelPromptColor (p, true);
                }
                ours = true;
                break;
            }
        }
    }

    // else check for toggling a color on/off
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (CSEL_ONOFFOK(p) && inBox (s, p.o_box)) {
                if (tt == TT_TAP_BX)
                    tooltip (s, "Whether this line is drawn at all");
                else {
                    p.o_state = !p.o_state;
                    drawCSelOnOffTickBox (p);
                }
                ours = true;
                break;
            }
        }
    }

    // else check for toggling thick/thin
    if (!ours) {
        for (int i = 0; i < N_CSPR; i++) {
            ColSelPrompt &p = csel_pr[i];
            if (inBox (s, p.t_box)) {
                if (tt == TT_TAP_BX)
                    tooltip (s, "Whether this line is drawn thick or thin");
                else {
                    p.t_state = !p.t_state;
                    drawCSelThicknessTickBox (p);
                    drawCSelDemoSwatch (p);
                }
                ours = true;
                break;
            }
        }
    }

    // else check for save/load buttons
    if (!ours) {
        ours = true;
        if (inBox (s, ctsl_save1_b)) {
            if (tt == TT_TAP_BX)
                tooltip (s, "Save the current colors to memory table A");
            else
                saveColorTable (1, NV_CSELDASHED_A, NV_CSELTHIN_A, NV_CSELONOFF_A, " A ", ctsl_save1_b);
        } else if (inBox (s, ctsl_save2_b)) {
            if (tt == TT_TAP_BX)
                tooltip (s, "Save the current colors to memory table B");
            else
                saveColorTable (2, NV_CSELDASHED_B, NV_CSELTHIN_B, NV_CSELONOFF_B, " B ", ctsl_save2_b);
        } else if (inBox (s, ctsl_load1_b)) {
            if (tt == TT_TAP_BX)
                tooltip (s, "Restore colors from memory table A");
            else
                loadColorTable (1, NV_CSELDASHED_A, NV_CSELTHIN_A, NV_CSELONOFF_A, " A ", ctsl_load1_b);
        } else if (inBox (s, ctsl_load2_b)) {
            if (tt == TT_TAP_BX)
                tooltip (s, "Restore colors from memory table B");
            else
                loadColorTable (2, NV_CSELDASHED_B, NV_CSELTHIN_B, NV_CSELONOFF_B, " B ", ctsl_load2_b);
        } else if (inBox (s, ctsl_loadp_b))
            if (tt == TT_TAP_BX)
                tooltip (s, "Set colors similar to used at pskreporter.info");
            else
                loadPSKColorTable();
        else if (inBox (s, ctsl_loadd_b))
            if (tt == TT_TAP_BX)
                tooltip (s, "Set colors from a built-in default");
            else
                loadDefaultColorTable();
        else
            ours = false;
    }

    return (ours);
}


/* draw a V inscribed in a square with size s and given center in one of 4 directions.
 * dir is degs CCW from right
 */
static void drawVee (uint16_t x0, uint16_t y0, uint16_t s, uint16_t dir, uint16_t c16)
{
    uint16_t r = s/2;

    switch (dir) {
    case 0:     // point right
        tft.drawLine (x0+r, y0, x0-r, y0-r, c16);
        tft.drawLine (x0+r, y0, x0-r, y0+r, c16);
        break;
    case 90:    // point up
        tft.drawLine (x0, y0-r, x0-r, y0+r, c16);
        tft.drawLine (x0, y0-r, x0+r, y0+r, c16);
        break;
    case 180:   // point left
        tft.drawLine (x0-r, y0, x0+r, y0-r, c16);
        tft.drawLine (x0-r, y0, x0+r, y0+r, c16);
        break;
    case 270:   // point down
        tft.drawLine (x0, y0+r, x0-r, y0-r, c16);
        tft.drawLine (x0, y0+r, x0+r, y0-r, c16);
        break;
    }
}


/* given dow 0..6, y coord of text and hhmm time print new value
 */
static void drawOnOffTimeCell (int dow, uint16_t y, uint16_t thm)
{
    char buf[20];

    tft.setTextColor(TX_C);
    snprintf (buf, sizeof(buf), "%02d:%02d", thm/60, thm%60);
    tft.setCursor (OO_DHX(dow)+(OO_CW-getTextWidth(buf))/2, y);
    tft.fillRect (OO_DHX(dow)+1, y-OO_RH+1, OO_CW-2, OO_RH, RA8875_BLACK);
    tft.print (buf);
}

/* draw OnOff table from scratch
 */
static void drawOnOffControls()
{
    // title
    const char *title = brDimmableOk()
                        ? "DE Daily Display On/Dim Times"
                        : "DE Daily Display On/Off Times";
    tft.setCursor (OO_X0+(OO_TW-getTextWidth(title))/2, OO_Y0-OO_RH-OO_TO);
    tft.setTextColor (PR_C);
    tft.print (title);


    // DOW column headings and copy marks
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t l = getTextWidth(dayShortStr(i+1));
        tft.setTextColor (PR_C);
        tft.setCursor (OO_DHX(i)+(OO_CW-l)/2, OO_CHY);
        tft.print (dayShortStr(i+1));
        drawVee (OO_CPLX(i), OO_CPLY, OO_ASZ, 180, BUTTON_C);
        drawVee (OO_CPRX(i), OO_CPRY, OO_ASZ, 0, BUTTON_C);
    }

    // On Off labels
    tft.setTextColor (PR_C);
    tft.setCursor (OO_X0+2, OO_ONY);
    tft.print ("On");
    tft.setCursor (OO_X0+2, OO_OFFY);
    if (brDimmableOk())
        tft.print ("Dim");
    else
        tft.print ("Off");

    // inc/dec hints
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+1*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+5*OO_RH/2, OO_ASZ, 270, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+7*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+11*OO_RH/2, OO_ASZ, 270, BUTTON_C);

    // graph lines
    tft.drawRect (OO_X0, OO_Y0-OO_RH, OO_CI+OO_CW*DAYSPERWEEK, OO_RH*7, KB_C);
    tft.drawLine (OO_X0, OO_Y0, OO_X0+OO_CI+OO_CW*DAYSPERWEEK, OO_Y0, KB_C);
    for (int i = 0; i < DAYSPERWEEK; i++)
        tft.drawLine (OO_DHX(i), OO_Y0-OO_RH, OO_DHX(i), OO_Y0+6*OO_RH, KB_C);

    // init table
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    for (int i = 0; i < DAYSPERWEEK; i++) {
        drawOnOffTimeCell (i, OO_ONY, onoff[i]);
        drawOnOffTimeCell (i, OO_OFFY, onoff[i+DAYSPERWEEK]);
    }
}

/* handle possible touch on the onoff controls page.
 * return whether really ours
 */
static bool checkOnOffTouch (TouchType tt, SCoord &s)
{

    if (!HAVE_ONOFF())
        return (false);

    // done if not in table at all
    int dow = ((int)s.x - (OO_X0+OO_CI))/OO_CW;
    int row = ((int)s.y - (OO_Y0-OO_RH))/OO_RH;
    if (dow < 0 || dow >= DAYSPERWEEK || row < 0 || row > 6)
        return (false);

    // read onoff times and make handy shortcuts
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    uint16_t *ontimes = &onoff[0];
    uint16_t *offtimes = &onoff[DAYSPERWEEK];

    int col = ((s.x - (OO_X0+OO_CI))/(OO_CW/2)) % 2;
    bool hour_col   = col == 0; // same as copy left
    bool mins_col   = col == 1; // same as copy right
    bool oncpy_row  = row == 0;
    bool oninc_row  = row == 1;
    bool ondec_row  = row == 3;
    bool offinc_row = row == 4;
    bool offdec_row = row == 6;

    if (oncpy_row) {
        int newdow;
        if (hour_col) {
            // copy left
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to copy today's settings to previous day");
                return (true);
            }
            newdow = (dow - 1 + DAYSPERWEEK) % DAYSPERWEEK;
        } else if (mins_col) {
            // copy right
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to copy today's settings to next day");
                return (true);
            }
            newdow = (dow + 1) % DAYSPERWEEK;
        } else
            return (false);
        ontimes[newdow] = ontimes[dow];
        offtimes[newdow] = offtimes[dow];
        drawOnOffTimeCell (newdow, OO_ONY, ontimes[newdow]);
        drawOnOffTimeCell (newdow, OO_OFFY, offtimes[newdow]);
    } else if (oninc_row) {
        if (hour_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to increase On time by one hour");
                return (true);
            }
            ontimes[dow] = (ontimes[dow] + 60) % MINSPERDAY;
        } else if (mins_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to increase On time by five minutes");
                return (true);
            }
            ontimes[dow] = (ontimes[dow] + 5)  % MINSPERDAY;
        } else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (ondec_row) {
        if (hour_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to decrease On time by one hour");
                return (true);
            }
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        } else if (mins_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to decrease On time by five minutes");
                return (true);
            }
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        } else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (offinc_row) {
        if (hour_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to increase Off time by one hour");
                return (true);
            }
            offtimes[dow] = (offtimes[dow] + 60) % MINSPERDAY;
        } else if (mins_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to increase Off time by five minutes");
                return (true);
            }
            offtimes[dow] = (offtimes[dow] + 5)  % MINSPERDAY;
        } else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    } else if (offdec_row) {
        if (hour_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to decrease Off time by one hour");
                return (true);
            }
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        } else if (mins_col) {
            if (tt == TT_TAP_BX) {
                tooltip (s, "Click to decrease Off time by five minutes");
                return (true);
            }
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        } else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    }

    // save
    NVWriteString (NV_DAILYONOFF, (char*)onoff);

    // ok
    return (true);
}



/* change cur_page to the given page
 */
static void changePage (int new_page)
{
    // save current then update
    int prev_page = cur_page;
    cur_page = new_page;

    // draw new page with minimal erasing if possible

    if (new_page == ALLBOOLS_PAGE) {
        // new page is all bools, always a fresh start
        eraseScreen();
        drawPageButton();
        drawCurrentPageFields();
        drawDoneButton(false);
        drawToolTipReminder();

    } else if (new_page == ONOFF_PAGE) {
        // new page is just the on/off table
        eraseScreen();
        drawPageButton();
        drawOnOffControls();
        drawDoneButton(false);
        drawToolTipReminder();

    } else if (new_page == COLOR_PAGE) {
        // new page is color, always a fresh start
        eraseScreen();
        drawPageButton();
        drawCSelInitGUI();
        drawDoneButton(false);
        setInitialFocus ();

    } else {
        if (prev_page >= KBPAGE_FIRST && prev_page <= KBPAGE_LAST) {
            // just refresh top portion, keyboard already ok
            tft.fillRect (0, 0, tft.width(), KB_Y0-1, BG_C);
            drawPageButton();
            drawCurrentPageFields();
        } else {
            // full refresh to insure no keyboard
            eraseScreen();
            drawPageButton();
            drawCurrentPageFields();
            drawKeyboard();
            drawDoneButton(false);
            drawToolTipReminder();
        }
    }
}

/* check whether the given string is a number between min_port and 65535.
 * if so, set port and return true, else return false.
 */
static bool portOK (char *port_str, int min_port, uint16_t *portp)
{
    char *first_bad;
    strTrimAll (port_str);
    int portn = strtol (port_str, &first_bad, 10);
    if (first_bad == port_str || *first_bad != '\0' || portn < min_port || portn > 65535)
        return (false);
    *portp = portn;
    return (true);
}


/* return whether the given string seems to be a legit host name and fits in the given length.
 * N.B. all blanks are removed IN PLACE
 */
static bool hostOK (char *host_str, int max_len)
{
    noBlanks(host_str);

    // not too long or too short
    int hl = strlen (host_str);
    if (hl > max_len-1 || hl == 0)
        return (false);

    // localhost?
    if (!strcmp (host_str, "localhost"))
        return (true);

    // need at least one dot for TLD or exactly 3 if looks like dotted ip notation
    int n_dots = 0;
    int n_digits = 0;
    int n_other = 0;
    for (int i = 0; i < hl; i++) {
        if (host_str[i] == '.')
            n_dots++;
        else if (isdigit(host_str[i]))
            n_digits++;
        else
            n_other++;
    }
    if (n_dots < 1 || host_str[0] == '.' || host_str[hl-1] == '.')
        return (false);
    if (n_other == 0 && (n_dots != 3 || n_digits != hl-3))
        return (false);

    return (true);
}

/* return whether the i2c_fn looks legit
 */
static bool I2CFnOk(void)
{
    bool ok = strncmp (i2c_fn, "/dev/", 5) == 0 && strlen (i2c_fn) > 5;

    // try to open and lock the same as Wire will do
    if (ok) {
        int fd = open (i2c_fn, O_RDWR);
        if (fd < 0) {
            Serial.printf ("I2C: %s: %s\n", i2c_fn, strerror(errno));
            ok = false;
        } else {
            ok = ::flock (fd, LOCK_EX|LOCK_NB) == 0;
            Serial.printf ("I2C: %s: %s\n", i2c_fn, ok ? "ok" : strerror(errno));
            close (fd);
        }
    }

    return (ok);
}

/* return whether dx_login looks ok
 */
static bool clusterLoginOk()
{
    // must contain DE call and SSID
    noBlanks(dx_login);
    char call[100];
    int ssid = 0;
    return (sscanf (dx_login, "%99[^-]-%d", call, &ssid) == 2
                && ssid > 0 && ssid <= DXC_MAX_SSID
                && strcasecmp (call, cs_info.call) == 0);
}

/* set a default cluster login if possible
 */
static void setClusterLogin()
{
    if (cs_info.call[0] != '\0')
        snprintf (dx_login, sizeof(dx_login), "%.*s-%d", (int)sizeof(dx_login)-4, cs_info.call, DXC_DEF_SSID);
}

/* return whether the candidate string looks anything like a valid call sign
 */
static bool callsignOk (const char *s)
{
    // only punct allowed is one slash
    const char *slash = NULL;
    for (const char *p = s; *p != '\0'; p++) {
        if (*p == '/') {
            if (slash)
                return (false);         // > 1 slash
            slash = p;
        } else if (ispunct(*p)) {
            return (false);             // no other punct allowed
        }
    }

    // slash must be followed by something else
    size_t s_len = strlen(s);
    if (slash == s+s_len-1)
        return (false);                 // slash is at the end

    return (s_len < NV_CALLSIGN_LEN
                && s_len >= 3
                && !strHasSpace(s)
                && strHasDigit(s)
                && strHasAlpha(s) 
           );
}

/* return whether string fields are all valid.
 * if show_errors then temporarily indicate ones in error.
 */
static bool validateStringPrompts (bool show_errors)
{
    // collect bad ids to flag
    SPIds badsids[N_SPR];
    uint8_t n_badsids = 0;

    // optional error msg -- can only handle one at a time
    Message err_buf;
    const char *err_msg = NULL;               // points to err_buf.get() if we want to report an error
    SPIds err_sid = N_SPR;

    // check call
    strTrimAll(cs_info.call);
    if (!callsignOk (cs_info.call))
        badsids[n_badsids++] = CALL_SPR;

    // check lat/long unless using something else
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSDON_BPR].state && !bool_pr[NMEAON_BPR].state) {

        if (!latSpecIsValid (string_pr[LAT_SPR].v_str, de_ll.lat_d))
            badsids[n_badsids++] = LAT_SPR;

        if (!lngSpecIsValid (string_pr[LNG_SPR].v_str, de_ll.lng_d))
            badsids[n_badsids++] = LNG_SPR;

        LatLong ll;
        if (!maidenhead2ll (ll, string_pr[GRID_SPR].v_str))
            badsids[n_badsids++] = GRID_SPR;
    }

    // check cluster info if used
    if (bool_pr[CLUSTER_BPR].state) {
        char *clhost = string_pr[DXHOST_SPR].v_str;
        if (!bool_pr[CLISUDP_BPR].state && !hostOK(clhost,NV_DXHOST_LEN)) // DXHOST_SPR not required for UDP
            badsids[n_badsids++] = DXHOST_SPR;
        if (!portOK (string_pr[DXPORT_SPR].v_str, 23, &dx_port))          // 23 is telnet
            badsids[n_badsids++] = DXPORT_SPR;
        if (!bool_pr[CLISUDP_BPR].state && !clusterLoginOk()) {           // login is not used with wsjt
            badsids[n_badsids++] = err_sid = DXLOGIN_SPR;
        }

        // clean up any extra white space in the commands then check for blank entries that are on
        for (int i = 0; i < N_DXCLCMDS; i++) {
            strTrimAll(dxcl_cmds[i]);
            if (strlen(dxcl_cmds[i]) == 0 && bool_pr[DXCLCMD0_BPR+i].state)
                badsids[n_badsids++] = (SPIds)(DXCLCMD0_SPR+i);
        }

        // watch list must compile successfully if being used
        if (getWatchListState (WLID_DX, NULL) != WLA_OFF) {
            if (!compileWatchList (WLID_DX, dx_wlist, err_buf)) {
                err_msg = err_buf.get();
                badsids[n_badsids++] = err_sid = DXWLIST_SPR;
            }
        }
    }

    // ONTA watch list must compile successfully if being used
    if (getWatchListState (WLID_ONTA, NULL) != WLA_OFF) {
        strTrimAll (onta_wlist);
        if (!compileWatchList (WLID_ONTA, onta_wlist, err_buf)) {
            err_msg = err_buf.get();
            badsids[n_badsids++] = err_sid = ONTAWL_SPR;
        }
    }

    // ADIF watch list must compile successfully
    if (getWatchListState (WLID_ADIF, NULL) != WLA_OFF) {
        strTrimAll (adif_wlist);
        if (!compileWatchList (WLID_ADIF, adif_wlist, err_buf)) {
            err_msg = err_buf.get();
            badsids[n_badsids++] = err_sid = ADIFWL_SPR;
        }
    }

    // check rig_host and port if used
    if (bool_pr[RIGUSE_BPR].state) {
        if (!hostOK(string_pr[RIGHOST_SPR].v_str,NV_RIGHOST_LEN))
            badsids[n_badsids++] = RIGHOST_SPR;
        if (!portOK (string_pr[RIGPORT_SPR].v_str, 1000, &rig_port))
            badsids[n_badsids++] = RIGPORT_SPR;
    }

    // check rot_host and port if used
    if (bool_pr[ROTUSE_BPR].state) {
        if (!hostOK(string_pr[ROTHOST_SPR].v_str,NV_ROTHOST_LEN))
            badsids[n_badsids++] = ROTHOST_SPR;
        if (!portOK (string_pr[ROTPORT_SPR].v_str, 1000, &rot_port))
            badsids[n_badsids++] = ROTPORT_SPR;
    }

    // check flrig host and port if used
    if (bool_pr[FLRIGUSE_BPR].state) {
        if (!hostOK(string_pr[FLRIGHOST_SPR].v_str,NV_FLRIGHOST_LEN))
            badsids[n_badsids++] = FLRIGHOST_SPR;
        if (!portOK (string_pr[FLRIGPORT_SPR].v_str, 1000, &flrig_port))
            badsids[n_badsids++] = FLRIGPORT_SPR;
    }

    // check for plausible temperature and pressure corrections and file name if used
    if (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state) {
        char *tc_str = string_pr[BME76DT_SPR].v_str;
        temp_corr[BME_76] = atof (tc_str);
        if (fabsf(temp_corr[BME_76]) > MAX_BME_DTEMP)
            badsids[n_badsids++] = BME76DT_SPR;
        char *tc2_str = string_pr[BME77DT_SPR].v_str;
        temp_corr[BME_77] = atof (tc2_str);
        if (fabsf(temp_corr[BME_77]) > MAX_BME_DTEMP)
            badsids[n_badsids++] = BME77DT_SPR;

        char *pc_str = string_pr[BME76DP_SPR].v_str;
        pres_corr[BME_76] = atof (pc_str);
        if (fabsf(pres_corr[BME_76]) > MAX_BME_DPRES)
            badsids[n_badsids++] = BME76DP_SPR;
        char *pc2_str = string_pr[BME77DP_SPR].v_str;
        pres_corr[BME_77] = atof (pc2_str);
        if (fabsf(pres_corr[BME_77]) > MAX_BME_DPRES)
            badsids[n_badsids++] = BME77DP_SPR;
    }

    // require ssid and pw if wifi
    if (bool_pr[WIFI_BPR].state) {
        if (strlen (string_pr[WIFISSID_SPR].v_str) == 0)
            badsids[n_badsids++] = WIFISSID_SPR;
        if (strlen (string_pr[WIFIPASS_SPR].v_str) == 0)
            badsids[n_badsids++] = WIFIPASS_SPR;
    }

    // require plausible gpsd host name if used
    if (bool_pr[GPSDON_BPR].state) {
        if (!hostOK(string_pr[GPSDHOST_SPR].v_str, NV_GPSDHOST_LEN))
            badsids[n_badsids++] = GPSDHOST_SPR;
    }

    // require plausible NMEA file name if used
    if (bool_pr[NMEAON_BPR].state) {
        if (!checkNMEAFilename (string_pr[NMEAFILE_SPR].v_str, err_buf)) {
            err_msg = err_buf.get();
            badsids[n_badsids++] = err_sid = NMEAFILE_SPR;
        }
    }

    // require plausible ntp host name or a few special cases if used
    if (strcmp (getEntangledValue (NTPA_BPR, NTPB_BPR), "host") == 0) {
        if (!hostOK (string_pr[NTPHOST_SPR].v_str, NV_NTPHOST_LEN))
            badsids[n_badsids++] = NTPHOST_SPR;
    }

    // require both brightness 0..100 and min < max.
    if (brDimmableOk()) {
        // Must use ints to check for < 0
        int brmn = atoi (string_pr[BRMIN_SPR].v_str);
        int brmx = atoi (string_pr[BRMAX_SPR].v_str);
        bool brmn_ok = brmn >= 0 && brmn <= 100;
        bool brmx_ok = brmx >= 0 && brmx <= 100;
        bool order_ok = brmn < brmx;
        if (!brmn_ok || (!order_ok && brmx_ok))
            badsids[n_badsids++] = BRMIN_SPR;
        if (!brmx_ok || (!order_ok && brmn_ok))
            badsids[n_badsids++] = BRMAX_SPR;
        if (brmn_ok && brmx_ok && order_ok) {
            bright_min = brmn;
            bright_max = brmx;
        }
    }

    // require mercator center longitude -180 <= x < 180
    float clng;
    if (lngSpecIsValid (string_pr[CENTERLNG_SPR].v_str, clng))
        center_lng = clng;
    else
        badsids[n_badsids++] = CENTERLNG_SPR;

    // check ADIF file name
    if (bool_pr[ADIFSET_BPR].state) {
        if (!checkADIFFilename (adif_fn, err_buf)) {
            err_msg = err_buf.get();
            badsids[n_badsids++] = err_sid = ADIFFN_SPR;
        }
    }

    // check I2C file name
    if (bool_pr[I2CON_BPR].state) {
        strTrimAll (i2c_fn);
        if (!I2CFnOk())
            badsids[n_badsids++] = I2CFN_SPR;
    }

    // if not showing, just return whether all ok
    if (!show_errors)
        return (n_badsids == 0);

    // indicate one bad field, if any
    if (n_badsids > 0) {

        // show first bad sid, starting with current page
        bool show_bad = false;
        for (int pg_offset = 0; !show_bad && pg_offset < N_PAGES; pg_offset++) {
            int tmp_page = (cur_page + pg_offset) % N_PAGES;
            for (int badsid_i = 0; !show_bad && badsid_i < n_badsids; badsid_i++) {
                SPIds bad_sid = badsids[badsid_i];
                StringPrompt *sp = &string_pr[bad_sid];
                if (sp->page == tmp_page) {

                    // set DXCLCMDPGA/B_BPR] if this is one of the cluster commands
                    bool chg_pg1 = false;
                    if (sp >= &string_pr[DXCLCMD0_SPR] && sp < &string_pr[DXCLCMD0_SPR + N_DXCLCMDS]) {
                        int pr_page = (sp - &string_pr[DXCLCMD0_SPR])/4 + 1;
                        int cmd_page = atoi (getEntangledValue (DXCLCMDPGA_BPR, DXCLCMDPGB_BPR));
                        if (pr_page != cmd_page) {
                            setEntangledValue (DXCLCMDPGA_BPR, DXCLCMDPGB_BPR, pr_page);
                            chg_pg1 = true;
                        }
                    }

                    // change page, if not already showing
                    if (tmp_page != cur_page || chg_pg1)
                        changePage(tmp_page);

                    // flag erroneous field
                    if (err_msg && err_sid == bad_sid)
                        flagErrField (sp, true, err_msg);
                    else
                        flagErrField (sp, true);

                    // flagged one
                    show_bad = true;
                }
            }
        }
        if (!show_bad)
            fatalError ("%d bad fields but none found", n_badsids);

        // at least one bad field
        return (false);
    }

    // all good
    return (true);
}


/* try to set NV_WIFI_SSID and NV_WIFI_PASSWD from a valid wpa_supplicant.conf
 */
static bool getWPACreds()
{
#if defined(_IS_LINUX)

    // first look in the wpa conf file

    // open
    static const char wpa_fn[] = "/etc/wpa_supplicant/wpa_supplicant.conf";
    FILE *wpa_fp = fopen (wpa_fn, "r");
    if (!wpa_fp) {
        Serial.printf ("Setup: %s: %s\n", wpa_fn, strerror(errno));
        return (false);
    }

    // look for ssid and psk
    char buf[100], wpa_ssid[100], wpa_psk[100];
    bool found_ssid = false, found_psk = false;
    while (fgets (buf, sizeof(buf), wpa_fp)) {
        if (sscanf (buf, " ssid=\"%100[^\"]\"", wpa_ssid) == 1)
            found_ssid = true;
        if (sscanf (buf, " psk=\"%100[^\"]\"", wpa_psk) == 1)
            found_psk = true;
    }

    // finished with file
    fclose (wpa_fp);

    // false unless find both
    if (!found_ssid || !found_psk)
        return (false);

    // but unless this is debain < bookworm these are old/bogus

    static const char osr_fn[] = "/etc/os-release";
    FILE *osr_fp = fopen (osr_fn, "r");
    if (!osr_fp) {
        Serial.printf ("Setup: %s: %s\n", osr_fn, strerror(errno));
        return (false);
    }
    bool is_debian = false;
    bool found_version = false;
    int version = 0;
    while (fgets (buf, sizeof(buf), osr_fp)) {
        // see https://www.freedesktop.org/software/systemd/man/latest/os-release.html
        char *eq = strchr (buf, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *name = buf;
        const char *value = eq+1;
        if (*value == '"')
            value++;
        if ((!strcmp (name, "ID") || !strcmp (name, "ID_LIKE")) && strstr (value, "debian"))
            is_debian = true;
        else if (!strcmp (name, "VERSION_ID")) {
            version = atoi(value);
            found_version = true;
        }
    }
    fclose (osr_fp);

    // wpa file is encrypted starting with debian 12 (bookworm)
    if (!(is_debian && found_version && version < 12))
        return (false);

    // ok!
    wpa_ssid[NV_WIFI_SSID_LEN-1] = '\0';
    strcpy (wifi_ssid, wpa_ssid);
    NVWriteString(NV_WIFI_SSID, wifi_ssid);
    wpa_psk[NV_WIFI_PW_LEN-1] = '\0';
    strcpy (wifi_pw, wpa_psk);
    NVWriteString(NV_WIFI_PASSWD, wifi_pw);

    return (true);

#else

    return (false);

#endif // _IS_LINUX
}


/* set dxcl_cmds[cmds_i] from new else old NV
 */
static void initDXCMD (NV_Name old_e, NV_Name new_e, int cmds_i)
{
    // check old first then invalidate
    if (NVReadString(old_e, dxcl_cmds[cmds_i]) && dxcl_cmds[cmds_i][0] != '\0') {
        char s[2] = "";
        NVWriteString(old_e, s);
    } else if (!NVReadString(new_e, dxcl_cmds[cmds_i])) {
        char s[2] = "";
        NVWriteString(new_e, s);
    }
}


/* load all setup values from nvram or set default values:
 */
static void initSetup()
{
    // report any active env var overrides regardless of -o flag
    struct { const char *name; const char *val; } env_vars[] = {
        { "HAMCLOCK_DX_HOST",        getenv("HAMCLOCK_DX_HOST")        },
        { "HAMCLOCK_DX_PORT",        getenv("HAMCLOCK_DX_PORT")        },
        { "HAMCLOCK_X11_FULLSCREEN", getenv("HAMCLOCK_X11_FULLSCREEN") },
        { "HAMCLOCK_OPT_IN",         getenv("HAMCLOCK_OPT_IN")         },
        { "HAMCLOCK_AUTO_UPDATE",    getenv("HAMCLOCK_AUTO_UPDATE")    },
    };
    for (unsigned i = 0; i < NARRAY(env_vars); i++) {
        if (env_vars[i].val)
            ::printf ("Setup: env override: %s=%s\n", env_vars[i].name, env_vars[i].val);
    }

    // init wifi, accept OLD PW if valid

    // see if we have a known WPA format
    good_wpa = getWPACreds();

    if (!good_wpa && !NVReadString(NV_WIFI_SSID, wifi_ssid)) {
        good_wpa = true;        // for Veritium
        strncpy (wifi_ssid, DEF_SSID, NV_WIFI_SSID_LEN-1);
        NVWriteString(NV_WIFI_SSID, wifi_ssid);
    }
    if (!NVReadString(NV_WIFI_PASSWD, wifi_pw) && !NVReadString(NV_WIFI_PASSWD_OLD, wifi_pw)) {
        strncpy (wifi_pw, DEF_PASS, NV_WIFI_PW_LEN-1);
        NVWriteString(NV_WIFI_PASSWD, wifi_pw);
    }



    // init call sign, no default

    NVReadString(NV_CALLSIGN, cs_info.call);
    strtoupper (cs_info.call);


    // init gpsd host and option

    if (!NVReadString (NV_GPSDHOST, gpsd_host)) {
        // try NV_GPSDHOST_OLD first time then erase
        char gpsd_host_old[NV_GPSDHOST_OLD_LEN];
        if (NVReadString (NV_GPSDHOST_OLD, gpsd_host_old)) {
            strcpy (gpsd_host, gpsd_host_old);
            memset (gpsd_host_old, 0, NV_GPSDHOST_OLD_LEN);
            NVWriteString (NV_GPSDHOST_OLD, gpsd_host_old);
        } else
            strcpy (gpsd_host, "localhost");
        NVWriteString (NV_GPSDHOST, gpsd_host);
    }
    uint8_t nv_gpsd;
    if (!NVReadUInt8 (NV_USEGPSD, &nv_gpsd)) {
        bool_pr[GPSDON_BPR].state = false;
        bool_pr[GPSDFOLLOW_BPR].state = false;
        NVWriteUInt8 (NV_USEGPSD, 0);
    } else {
        bool_pr[GPSDON_BPR].state = (nv_gpsd & USEGPSD_FORTIME_BIT) != 0;
        bool_pr[GPSDFOLLOW_BPR].state = bool_pr[GPSDON_BPR].state && (nv_gpsd & USEGPSD_FORLOC_BIT) != 0;
    }


    // init NMEA

    if (!NVReadString (NV_NMEAFILE, nmea_file)) {
        memset (nmea_file, 0, NV_NMEAFILE_LEN);
        NVWriteString (NV_NMEAFILE, nmea_file);
    }
    uint8_t nv_nmea_use;
    if (!NVReadUInt8 (NV_USENMEA, &nv_nmea_use)) {
        bool_pr[NMEAON_BPR].state = false;
        bool_pr[NMEAFOLLOW_BPR].state = false;
        NVWriteUInt8 (NV_USENMEA, 0);
    } else {
        bool_pr[NMEAON_BPR].state = (nv_nmea_use & USENMEA_FORTIME_BIT) != 0;
        bool_pr[NMEAFOLLOW_BPR].state = bool_pr[NMEAON_BPR].state && (nv_nmea_use & USENMEA_FORLOC_BIT) != 0;
    }
    uint16_t nv_nmea_baud;
    if (!NVReadUInt16 (NV_NMEABAUD, &nv_nmea_baud))
        nv_nmea_baud = 9600;
    setEntangledValue (NMEABAUDA_BPR, NMEABAUDB_BPR, nv_nmea_baud);



    // init ntp host and option
    // 4.07 lengthed string from NV_NTPHOST_OLD_LEN to NV_NTPHOST_LEN
    // 4.08 made NTP an entangled pair with explicit computer value -- no longer use "OS" in host name

    if (!NVReadString (NV_NTPHOST, ntp_host)) {
        // try NV_NTPHOST_OLD first time then erase
        char ntp_host_old[NV_NTPHOST_OLD_LEN];
        if (NVReadString (NV_NTPHOST_OLD, ntp_host_old)) {
            memcpy (ntp_host, ntp_host_old, NV_NTPHOST_OLD_LEN);
            memset (ntp_host_old, 0, NV_NTPHOST_OLD_LEN);
            NVWriteString (NV_NTPHOST_OLD, ntp_host_old);
        } else
            memset (ntp_host, 0, NV_NTPHOST_LEN);
        NVWriteString (NV_NTPHOST, ntp_host);
    }
    uint8_t nv_ntp;
    if (strcasecmp (ntp_host, "OS") == 0) {
        // backwards compatable with setting host to "OS"
        memset (ntp_host, 0, NV_NTPHOST_LEN);
        NVWriteString (NV_NTPHOST, ntp_host);
        nv_ntp = NTPSC_OS;
        NVWriteUInt8 (NV_NTPSET, nv_ntp);
    } else {
        if (!NVReadUInt8 (NV_NTPSET, &nv_ntp) || nv_ntp >= NTPSC_N) {
            nv_ntp = NTPSC_DEF;
            NVWriteUInt8 (NV_NTPSET, nv_ntp);
        }
    }
    setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[nv_ntp]);


    // init ADIF, use old file name first time

    char adiffn_old[NV_ADIFFN_OLD_LEN];
    if (NVReadString (NV_ADIFFN_OLD, adiffn_old) && adiffn_old[0] != '\0') {
        memset (adif_fn, 0, sizeof(adif_fn));
        memcpy (adif_fn, adiffn_old, sizeof(adiffn_old));
        memset (adiffn_old, 0, sizeof(adiffn_old));
        NVWriteString (NV_ADIFFN_OLD, adiffn_old);
    } else if (!NVReadString (NV_ADIFFN, adif_fn)) {
        memset (adif_fn, 0, sizeof(adif_fn));
        NVWriteString (NV_ADIFFN, adif_fn);
    }
    bool_pr[ADIFSET_BPR].state = adif_fn[0] != '\0';



    // init I2C

    if (!NVReadString (NV_I2CFN, i2c_fn)) {
        // supply a reasonable system-dependent default
        #if defined (_I2C_FREEBSD)
            strcpy (i2c_fn, "/dev/iic0");
        #elif defined (_I2C_LINUX)
            strcpy (i2c_fn, "/dev/i2c-1");
        #else
            i2c_fn[0] = '\0';
        #endif
        NVWriteString (NV_I2CFN, i2c_fn);
    }
    uint8_t i2c_on;
    if (!NVReadUInt8 (NV_I2CON, &i2c_on)) {
        i2c_on = 0;
        NVWriteUInt8 (NV_I2CON, i2c_on);
    }
    bool_pr[I2CON_BPR].state = (i2c_on != 0);


    // init rigctld host, port and option

    if (!NVReadString (NV_RIGHOST, rig_host)) {
        strcpy (rig_host, "localhost");
        NVWriteString (NV_RIGHOST, rig_host);
    }
    if (!NVReadUInt16(NV_RIGPORT, &rig_port)) {
        rig_port = 4532;
        NVWriteUInt16(NV_RIGPORT, rig_port);
    }
    uint8_t nv_rig;
    if (!NVReadUInt8 (NV_RIGUSE, &nv_rig) || (nv_rig != 0 && nv_rig != 1)) {
        bool_pr[RIGUSE_BPR].state = false;
        NVWriteUInt8 (NV_RIGUSE, 0);
    } else
        bool_pr[RIGUSE_BPR].state = (nv_rig != 0);


    // init rotctld host, port and option

    if (!NVReadString (NV_ROTHOST, rot_host)) {
        strcpy (rot_host, "localhost");
        NVWriteString (NV_ROTHOST, rot_host);
    }
    if (!NVReadUInt16(NV_ROTPORT, &rot_port)) {
        rot_port = 4533;
        NVWriteUInt16(NV_ROTPORT, rot_port);
    }
    uint8_t nv_rot;
    if (!NVReadUInt8 (NV_ROTUSE, &nv_rot) || (nv_rot != 0 && nv_rot != 1)) {
        bool_pr[ROTUSE_BPR].state = false;
        NVWriteUInt8 (NV_ROTUSE, 0);
    } else
        bool_pr[ROTUSE_BPR].state = (nv_rot != 0);



    // init flrig host, port and option

    if (!NVReadString (NV_FLRIGHOST, flrig_host)) {
        strcpy (flrig_host, "localhost");
        NVWriteString (NV_FLRIGHOST, flrig_host);
    }
    if (!NVReadUInt16(NV_FLRIGPORT, &flrig_port)) {
        flrig_port = 12345;
        NVWriteUInt16(NV_FLRIGPORT, flrig_port);
    }
    uint8_t nv_flrig;
    if (!NVReadUInt8 (NV_FLRIGUSE, &nv_flrig) || (nv_flrig != 0 && nv_flrig != 1)) {
        bool_pr[FLRIGUSE_BPR].state = false;
        NVWriteUInt8 (NV_FLRIGUSE, 0);
    } else
        bool_pr[FLRIGUSE_BPR].state = (nv_flrig != 0);



    // init whether to command radio
    uint8_t set_radio;
    if (!NVReadUInt8 (NV_SETRADIO, &set_radio)) {
        set_radio = 0;
        NVWriteUInt8 (NV_SETRADIO, set_radio);
    }
    bool_pr[SETRADIO_BPR].state = (set_radio != 0);



    // init dx cluster info

    if (!NVReadString(NV_DXHOST, dx_host)) {
        memset (dx_host, 0, sizeof(dx_host));
        NVWriteString(NV_DXHOST, dx_host);
    }
    // env var overrides NV but does not persist to NV
    {
        const char *env_host = getenv ("HAMCLOCK_DX_HOST");
        if (env_host) {
            strncpy (dx_host, env_host, sizeof(dx_host)-1);
            dx_host[sizeof(dx_host)-1] = '\0';
        }
    }
    if (!NVReadString(NV_DXLOGIN, dx_login) || !clusterLoginOk()) {
        setClusterLogin();
        NVWriteString(NV_DXLOGIN, dx_login);
    }
    if (!NVReadString(NV_HAMSATKEY, hamsat_key)) {
        memset (hamsat_key, 0, sizeof(hamsat_key));
        NVWriteString(NV_HAMSATKEY, hamsat_key);
    }
    if (!NVReadUInt16(NV_DXPORT, &dx_port)) {
        dx_port = 0;
        NVWriteUInt16(NV_DXPORT, dx_port);
    }
    // env var overrides NV but does not persist to NV
    {
        const char *env_port = getenv ("HAMCLOCK_DX_PORT");
        if (env_port)
            dx_port = (uint16_t)atoi(env_port);
    }

    if (!NVReadString(NV_DXWLIST, dx_wlist)) {
        memset (dx_wlist, 0, sizeof(dx_wlist));
        NVWriteString(NV_DXWLIST, dx_wlist);
    }
    uint8_t dxwlist_mask;
    if (!NVReadUInt8(NV_DXWLISTMASK, &dxwlist_mask)) {
        dxwlist_mask = 0;
        NVWriteUInt8(NV_DXWLISTMASK, dxwlist_mask);
    }
    bool_pr[DXWLISTA_BPR].state = (dxwlist_mask & 1) == 1;
    bool_pr[DXWLISTB_BPR].state = (dxwlist_mask & 2) == 2;


    // DX commands -- accept previous

    initDXCMD (NV_DXCMD0_OLD, NV_DXCMD0, 0);
    initDXCMD (NV_DXCMD1_OLD, NV_DXCMD1, 1);
    initDXCMD (NV_DXCMD2_OLD, NV_DXCMD2, 2);
    initDXCMD (NV_DXCMD3_OLD, NV_DXCMD3, 3);
    initDXCMD (NV_DXCMD4_OLD, NV_DXCMD4, 4);
    initDXCMD (NV_DXCMD5_OLD, NV_DXCMD5, 5);
    initDXCMD (NV_DXCMD6_OLD, NV_DXCMD6, 6);
    initDXCMD (NV_DXCMD7_OLD, NV_DXCMD7, 7);
    initDXCMD (NV_DXCMD8_OLD, NV_DXCMD8, 8);
    initDXCMD (NV_DXCMD9_OLD, NV_DXCMD9, 9);
    initDXCMD (NV_DXCMD10_OLD, NV_DXCMD10, 10);
    initDXCMD (NV_DXCMD11_OLD, NV_DXCMD11, 11);

    uint8_t nv_wsjt;
    if (!NVReadUInt8 (NV_WSJT_DX, &nv_wsjt)) {
        // check host for possible backwards compat
        if (strcasecmp(dx_host,"WSJT-X") == 0 || strcasecmp(dx_host,"JTDX") == 0) {
            nv_wsjt = 1;
            memset (dx_host, 0, sizeof(dx_host));
            NVWriteString(NV_DXHOST, dx_host);
        } else
            nv_wsjt = 0;
        NVWriteUInt8 (NV_WSJT_DX, nv_wsjt);
    }
    bool_pr[CLISUDP_BPR].state = (nv_wsjt != 0);

    uint8_t nv_dx;
    if (!NVReadUInt8 (NV_USEDXCLUSTER, &nv_dx)) {
        nv_dx = false;
        NVWriteUInt8 (NV_USEDXCLUSTER, nv_dx);
    }
    bool_pr[CLUSTER_BPR].state = (nv_dx != 0);



    // init watch lists


    if (!NVReadString(NV_ADIFWLIST, adif_wlist)) {
        memset (adif_wlist, 0, sizeof(adif_wlist));
        NVWriteString(NV_ADIFWLIST, adif_wlist);
    }
    uint8_t adifwlist_mask;
    if (!NVReadUInt8(NV_ADIFWLISTMASK, &adifwlist_mask)) {
        adifwlist_mask = 0;
        NVWriteUInt8(NV_ADIFWLISTMASK, adifwlist_mask);
    }
    bool_pr[ADIFWLISTA_BPR].state = (adifwlist_mask & 1) == 1;
    bool_pr[ADIFWLISTB_BPR].state = (adifwlist_mask & 2) == 2;



    if (!NVReadString(NV_ONTAWLIST, onta_wlist)) {
        memset (onta_wlist, 0, sizeof(onta_wlist));
        NVWriteString(NV_ONTAWLIST, onta_wlist);
    }
    uint8_t ontawlist_mask;
    if (!NVReadUInt8(NV_ONTAWLISTMASK, &ontawlist_mask)) {
        ontawlist_mask = 0;
        NVWriteUInt8(NV_ONTAWLISTMASK, ontawlist_mask);
    }
    bool_pr[ONTAWLISTA_BPR].state = (ontawlist_mask & 1) == 1;
    bool_pr[ONTAWLISTB_BPR].state = (ontawlist_mask & 2) == 2;



    uint8_t lblstyle;
    if (!NVReadUInt8 (NV_LBLSTYLE, &lblstyle)) {
        lblstyle = LBL_PREFIX;
        NVWriteUInt8 (NV_LBLSTYLE, lblstyle);
    } else
        lblstyle &= NVMS_MKMSK;                          // mask off bits used prior to 4.10
    // these should have been made to align with LABELSTYLES
    bool_pr[SPOTLBLA_BPR].state = lblstyle == LBL_DOT || lblstyle == LBL_CALL;
    bool_pr[SPOTLBLB_BPR].state = lblstyle == LBL_PREFIX || lblstyle == LBL_CALL;

    uint16_t dx_cmdmask;
    if (!NVReadUInt16 (NV_DXCMDMASK, &dx_cmdmask)) {
        dx_cmdmask = 0;
        NVWriteUInt16 (NV_DXCMDMASK, dx_cmdmask);
    }
    for (int i = 0; i < N_DXCLCMDS; i++)
        bool_pr[DXCLCMD0_BPR+i].state = (dx_cmdmask & (1<<i)) != 0;


    // init de lat/lng

    // if de never set before set to cental US so it differs from default DX which is 0/0.
    char de_maid[MAID_CHARLEN];
    if (!NVReadFloat (NV_DE_LAT, &de_ll.lat_d) || !NVReadFloat (NV_DE_LNG, &de_ll.lng_d)
                                               || !NVReadString (NV_DE_GRID, de_maid)) {
        // http://www.kansastravel.org/geographicalcenter.htm
        de_ll.lng_d = -99;
        de_ll.lat_d = 40;
        de_ll.normalize();
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
        ll2maidenhead (de_maid, de_ll);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        // N.B. do not set TZ here because network not yet up -- rely on main setup()
    }

    // reset until ll fields are edited this session
    ll_edited = false;


    // init KX3. NV stores actual baud rate, we just toggle between 4800 and 38400, 0 means off

    uint32_t kx3;
    if (!NVReadUInt32 (NV_KX3BAUD, &kx3)) {
        kx3 = 0;                                // default off
        NVWriteUInt32 (NV_KX3BAUD, kx3);
    }
    bool_pr[KX3A_BPR].state = (kx3 != 0);
    bool_pr[KX3B_BPR].state = (kx3 == 38400);


    // init GPIOOK -- might effect KX3ON

    uint8_t gpiook;
    if (!NVReadUInt8 (NV_GPIOOK, &gpiook)) {
        gpiook = 0;                             // default off
        NVWriteUInt8 (NV_GPIOOK, gpiook);
    }
    bool_pr[GPIOOK_BPR].state = (gpiook != 0);
    if (!gpiook && bool_pr[KX3A_BPR].state) {
        // no KX3 if no GPIO
        bool_pr[KX3A_BPR].state = false;
        NVWriteUInt32 (NV_KX3BAUD, 0);
    }


    // init WiFi

#if defined(_WIFI_ALWAYS)
    bool_pr[WIFI_BPR].state = true;             // always on
    bool_pr[WIFI_BPR].p_str = "WiFi:";          // not a question
#elif defined(_WIFI_ASK)
    bool_pr[WIFI_BPR].state = false;            // default off
#endif


    // init ColSelPrompt settings

    uint32_t a_mask, t_mask, o_mask;
    if (!NVReadUInt32 (NV_CSELDASHED, &a_mask)) {
        a_mask = 0;                             // none dashed by default
        NVWriteUInt32 (NV_CSELDASHED, a_mask);
    }
    if (!NVReadUInt32 (NV_CSELTHIN, &t_mask)) {
        t_mask = (1U<<N_CSPR) - 1;              // all thin by default
        NVWriteUInt32 (NV_CSELTHIN, t_mask);
    }
    if (!NVReadUInt32 (NV_CSELONOFF, &o_mask)) {
        o_mask = (1U<<N_CSPR) - 1;              // all on by default
        NVWriteUInt32 (NV_CSELONOFF, o_mask);
    }
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        uint16_t c;
        if (!NVReadUInt16 (p.def_c_nv, &c)) {
            c = p.def_c;
            NVWriteUInt16 (p.def_c_nv, c);
        }
        p.r = RGB565_R(c);
        p.g = RGB565_G(c);
        p.b = RGB565_B(c);
        p.a_state = (a_mask & (1<<i)) != 0;
        p.t_state = (t_mask & (1<<i)) != 0;
        p.o_state = (o_mask & (1<<i)) != 0;
    }


    // X11 flags, engage immediately if defined or sensible thing to do
    uint16_t x11flags;
    int dspw, dsph;
    tft.getScreenSize (&dspw, &dsph);
    Serial.printf ("Display is %d x %d\n", dspw, dsph);
    Serial.printf ("Built for %d x %d\n", BUILD_W, BUILD_H);
    if (NVReadUInt16 (NV_X11FLAGS, &x11flags)) {
        Serial.printf ("x11flags found 0x%02X\n", x11flags);
        bool_pr[X11_FULLSCRN_BPR].state = (x11flags & X11BIT_FULLSCREEN) == X11BIT_FULLSCREEN;
        tft.X11OptionsEngageNow(getX11FullScreen());
    } else {
        // set typical defaults but wait for user choices to save
        bool_pr[X11_FULLSCRN_BPR].state = false;

        // engage full screen now if required to see app
        if (BUILD_W == dspw || BUILD_H == dsph) {
            bool_pr[X11_FULLSCRN_BPR].state = true;
            tft.X11OptionsEngageNow(getX11FullScreen());
        }
    }
    // env var overrides NV but does not persist
    if (getenv ("HAMCLOCK_X11_FULLSCREEN")) {
        bool_pr[X11_FULLSCRN_BPR].state = true;
        tft.X11OptionsEngageNow(getX11FullScreen());
    }

    // init and validate daily on-off times

    uint16_t onoff[NV_DAILYONOFF_LEN];
    if (!NVReadString (NV_DAILYONOFF, (char*)onoff)) {
        // try to init from deprecated values
        uint16_t on, off;
        if (!NVReadUInt16 (NV_DPYON_OLD, &on))
            on = 0;
        if (!NVReadUInt16 (NV_DPYOFF_OLD, &off))
            off = 0;   
        for (int i = 0; i < DAYSPERWEEK; i++) {
            onoff[i] = on;
            onoff[i+DAYSPERWEEK] = off;
        }
        NVWriteString (NV_DAILYONOFF, (char*)onoff);
    } else {
        // reset all if find any bogus from 2.60 bug
        for (int i = 0; i < 2*DAYSPERWEEK; i++) {
            if (onoff[i] >= MINSPERDAY || (onoff[i]%5)) {
                memset (onoff, 0, sizeof(onoff));
                NVWriteString (NV_DAILYONOFF, (char*)onoff);
                break;
            }
        }
    }


    // init several more misc

    uint8_t df_mdy, df_dmyymd;
    if (!NVReadUInt8 (NV_DATEMDY, &df_mdy) || !NVReadUInt8 (NV_DATEDMYYMD, &df_dmyymd)) {
        df_mdy = 0;
        df_dmyymd = 0;
        NVWriteUInt8 (NV_DATEMDY, df_mdy);
        NVWriteUInt8 (NV_DATEDMYYMD, df_dmyymd);
    }
    bool_pr[DATEFMT_MDY_BPR].state = (df_mdy != 0);
    bool_pr[DATEFMT_DMYYMD_BPR].state = (df_dmyymd != 0);

    uint8_t logok;
    if (!NVReadUInt8 (NV_LOGUSAGE, &logok)) {
        logok = 0;
        NVWriteUInt8 (NV_LOGUSAGE, logok);
    }
    // env var overrides NV but does not persist
    if (getenv ("HAMCLOCK_OPT_IN"))
        logok = 1;
    bool_pr[LOGUSAGE_BPR].state = (logok != 0);

    uint8_t units;
    if (!NVReadUInt8 (NV_UNITS, &units)) {
        units = UNITS_MET;
        NVWriteUInt8 (NV_UNITS, units);
    }
    setEntangledValue (UNITSA_BPR, UNITSB_BPR, units_names[units]);

    uint8_t weekmon;
    if (!NVReadUInt8 (NV_WEEKMON, &weekmon)) {
        weekmon = 0;
        NVWriteUInt8 (NV_WEEKMON, weekmon);
    }
    bool_pr[WEEKDAY1MON_BPR].state = (weekmon != 0);

    uint8_t b_mag;
    if (!NVReadUInt8 (NV_BEAR_MAG, &b_mag)) {
        b_mag = 0;
        NVWriteUInt8 (NV_BEAR_MAG, b_mag);
    }
    bool_pr[BEARING_BPR].state = (b_mag != 0);

    if (!NVReadInt16 (NV_CENTERLNG, &center_lng)) {
        center_lng = 0;
        NVWriteInt16 (NV_CENTERLNG, center_lng);
    }

    // init night option
    if (!NVReadUInt8 (NV_NIGHT_ON, &night_on)) {
        night_on = 1;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
    }

    // init place names option
    if (!NVReadUInt8 (NV_NAMES_ON, &names_on)) {
        names_on = 0;
        NVWriteUInt8 (NV_NAMES_ON, names_on);
    }

    if (!NVReadFloat (NV_TEMPCORR76, &temp_corr[BME_76])) {
        temp_corr[BME_76] = 0;
        NVWriteFloat (NV_TEMPCORR76, temp_corr[BME_76]);
    }
    if (!NVReadFloat (NV_PRESCORR76, &pres_corr[BME_76])) {
        pres_corr[BME_76] = 0;
        NVWriteFloat (NV_PRESCORR76, pres_corr[BME_76]);
    }
    if (!NVReadFloat (NV_TEMPCORR77, &temp_corr[BME_77])) {
        temp_corr[BME_77] = 0;
        NVWriteFloat (NV_TEMPCORR77, temp_corr[BME_77]);
    }
    if (!NVReadFloat (NV_PRESCORR77, &pres_corr[BME_77])) {
        pres_corr[BME_77] = 0;
        NVWriteFloat (NV_PRESCORR77, pres_corr[BME_77]);
    }

    bool_pr[GEOIP_BPR].state = false;

    if (!NVReadUInt8 (NV_BR_MIN, &bright_min)) {
        bright_min = 0;
        NVWriteUInt8 (NV_BR_MIN, bright_min);
    }
    if (!NVReadUInt8 (NV_BR_MAX, &bright_max)) {
        bright_max = 100;
        NVWriteUInt8 (NV_BR_MAX, bright_max);
    }

    uint8_t scroll_dir;
    if (!NVReadUInt8 (NV_SCROLLDIR, &scroll_dir)) {
        scroll_dir = 0;
        NVWriteUInt8 (NV_SCROLLDIR, scroll_dir);
    }
    bool_pr[SCROLLDIR_BPR].state = (scroll_dir != 0);

    uint8_t show_pip;
    if (!NVReadUInt8 (NV_SHOWPIP, &show_pip)) {
        show_pip = 0;
        NVWriteUInt8 (NV_SHOWPIP, show_pip);
    }
    bool_pr[SHOWPIP_BPR].state = (show_pip != 0);

    uint8_t show_planets;
    if (!NVReadUInt8 (NV_SHOWPLANETS, &show_planets)) {
        show_planets = 0;
        NVWriteUInt8 (NV_SHOWPLANETS, show_planets);
    }
    bool_pr[SHOWPLANETS_BPR].state = (show_planets != 0);

    uint8_t auto_map;
    if (!NVReadUInt8 (NV_AUTOMAP, &auto_map)) {
        auto_map = 0;
        NVWriteUInt8 (NV_AUTOMAP, auto_map);
    }
    bool_pr[AUTOMAP_BPR].state = (auto_map != 0);

    uint8_t newdxdewx;
    if (!NVReadUInt8 (NV_NEWDXDEWX, &newdxdewx)) {
        newdxdewx = 1;                                  // default yes
        NVWriteUInt8 (NV_NEWDXDEWX, newdxdewx);
    }
    bool_pr[NEWDXDEWX_BPR].state = (newdxdewx != 0);

    uint8_t webfs;
    if (!NVReadUInt8 (NV_WEBFS, &webfs)) {
        webfs = 0;
        NVWriteUInt8 (NV_WEBFS, webfs);
    }
    bool_pr[WEB_FULLSCRN_BPR].state = (webfs != 0);



    // pane rotation value and strings

    for (int i = 0; i < NARRAY(panerotp_strs); i++)
        snprintf (panerotp_strs[i], sizeof(panerotp_strs[i]), "%d seconds", panerotp_vals[i]);

    uint8_t pane_rotp;
    if (!NVReadUInt8 (NV_PANEROTP, &pane_rotp)) {
        pane_rotp = panerotp_vals[2];
        NVWriteUInt8 (NV_PANEROTP, pane_rotp);
    }
    setEntangledValue (PANE_ROTPA_BPR, PANE_ROTPB_BPR, panerotp_vals, pane_rotp);




    // map rotation value and strings

    for (int i = 0; i < NARRAY(maprotp_strs); i++)
        snprintf (maprotp_strs[i], sizeof(maprotp_strs[i]), "%d seconds", maprotp_vals[i]);

    uint8_t map_rotp;
    if (!NVReadUInt8 (NV_MAPROTP, &map_rotp)) {
        map_rotp = maprotp_vals[2];
        NVWriteUInt8 (NV_MAPROTP, map_rotp);
    }
    setEntangledValue (MAP_ROTPA_BPR, MAP_ROTPB_BPR, maprotp_vals, map_rotp);




    uint8_t gray_dpy;
    if (!NVReadUInt8 (NV_GRAYDPY, &gray_dpy))
        gray_dpy = GRAY_OFF;
    setGrayDisplay((GrayDpy_t)gray_dpy);

    // last chance to insure some time source is active
    // N.B. see wifi.cpp::initSys()
    if (!useGPSDTime() && !useNMEATime() && !useOSTime() && !useLocalNTPHost())
        setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[NTPSC_DEF]);


    uint8_t qrz_id;
    if (!NVReadUInt8 (NV_QRZID, &qrz_id) || qrz_id >= QRZ_N) {
        qrz_id = QRZ_NONE;
        NVWriteUInt8 (NV_QRZID, qrz_id);
    }
    setEntangledValue (QRZBIOA_BPR, QRZBIOB_BPR, qrz_urltable[qrz_id].label);

    // insure default DX
    char dx_grid[MAID_CHARLEN];
    if (!NVReadString (NV_DX_GRID, dx_grid)) {
        // presume none have been set
        NVWriteString (NV_DX_GRID, "JJ00aa");
        NVWriteFloat (NV_DX_LAT, 0.0F);
        NVWriteFloat (NV_DX_LNG, 0.0F);
        NVWriteInt32 (NV_DX_TZ, 0);
    }

    uint8_t udpsetsdx;
    if (!NVReadUInt8 (NV_UDPSETSDX, &udpsetsdx)) {
        udpsetsdx = 0;
        NVWriteUInt8 (NV_UDPSETSDX, udpsetsdx);
    }
    bool_pr[UDPSETSDX_BPR].state = (udpsetsdx != 0);

    uint8_t udpspots;
    if (!NVReadUInt8 (NV_UDPSPOTS, &udpspots)) {
        udpspots = 0;
        NVWriteUInt8 (NV_UDPSPOTS, udpspots);
    }
    bool_pr[UDPSPOTS_BPR].state = (udpspots != 0);

    int8_t aup_hr;
    bool aup_ok = false;
    if (!NVReadInt8 (NV_AUTOUPGRADE, &aup_hr)) {
        aup_hr = autoup_tbl[AUP_OFF].local_hour;
        NVWriteInt8 (NV_AUTOUPGRADE, aup_hr);
    }
    // env var overrides NV but does not persist — picks randomly from the
    // three upgrade times so not all installs hit the server simultaneously
    if (getenv ("HAMCLOCK_AUTO_UPDATE")) {
        static const AUTOUP_ID aup_choices[] = { AUP_P1, AUP_P2, AUP_P3 };
        AUTOUP_ID aup_choice = aup_choices[(unsigned)time(NULL) % 3];
        aup_hr = autoup_tbl[aup_choice].local_hour;
    }
    for (int i = 0; i < AUP_N; i++) {
        if (aup_hr == autoup_tbl[i].local_hour) {
            setEntangledValue (AUTOUPA_BPR, AUTOUPB_BPR, autoup_tbl[i].label);
            aup_ok = true;
            break;
        }
    }
    if (!aup_ok) {
        int8_t fixed_aup_hr = autoup_tbl[AUP_OFF].local_hour;
        Serial.printf ("Setup: fixing bogus NV_AUTOUPGRADE from %d to %d\n", aup_hr, fixed_aup_hr);
        aup_hr = fixed_aup_hr;
        NVWriteInt8 (NV_AUTOUPGRADE, aup_hr);
        setEntangledValue (AUTOUPA_BPR, AUTOUPB_BPR, autoup_tbl[AUP_OFF].label);
    }



    uint8_t max_tle = 0;
    int max_tle_i = -1;                                 // index of matching maxTLEAges, if any
    (void) NVReadUInt8 (NV_MAXTLEAGE, &max_tle);        // will fix if bogus
    for (int i = 0; i < N_MAXTLE; i++) {
        if (max_tle == atoi(maxTLEAges[i])) {
            max_tle_i = i;
            break;
        }
    }
    if (max_tle_i < 0) {
        max_tle_i = MAXTLE_DEF;
        uint8_t fix_max_tle = atoi(maxTLEAges[MAXTLE_DEF]);
        NVWriteUInt8 (NV_MAXTLEAGE, fix_max_tle);
        Serial.printf ("Setup: fixing NV_MAXTLEAGE from %d to %d\n", max_tle, fix_max_tle);
    }
    setEntangledValue (MAXTLEA_BPR, MAXTLEB_BPR, maxTLEAges[max_tle_i]);


    uint16_t min_lbl = 0;
    int min_lbl_i = -1;                                 // index of matching maxTLEAges, if any
    (void) NVReadUInt16 (NV_MINLBLDIST, &min_lbl);      // will fix if bogus
    for (int i = 0; i < N_MINLBL; i++) {
        if (min_lbl == atoi(minLblD[i])) {
            min_lbl_i = i;
            break;
        }
    }
    if (min_lbl_i < 0) {
        min_lbl_i = MINLBL_DEF;
        uint16_t fix_min_lbl = atoi(minLblD[MINLBL_DEF]);
        NVWriteUInt16 (NV_MINLBLDIST, fix_min_lbl);
        Serial.printf ("Setup: fixing NV_MINLBLDIST from %d to %d\n", min_lbl, fix_min_lbl);
    }
    setEntangledValue (LBLDISTA_BPR, LBLDISTB_BPR, minLblD[min_lbl_i]);
}


/* return whether user wants to run setup.
 */ 
static bool askRun()
{
    eraseScreen();

    if (skip_skip) {
        Serial.printf ("Setup: skipping because -k\n");
        return (false);
    }

    drawStringInBox ("Skip", skip_b, false, TX_C);

    tft.setTextColor (TX_C);
    tft.setCursor (tft.width()/6, tft.height()/5);

    // appropriate prompt
    tft.print ("Click anywhere to enter Setup ... ");

    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    uint16_t to;
    for (to = ASK_TO*10; to > 0; --to) {
        if ((to+9)/10 != (to+10)/10) {
            tft.fillRect (x, y-PR_A, 2*PR_W, PR_A+PR_D, BG_C);
            tft.setCursor (x, y);
            tft.print((to+9)/10);
        }

        // check for touch, type ESC or abort box
        SCoord s;
        TouchType tt = readCalTouchWS (s);
        if (tt == TT_TAP_BX)
            tooltip (s, "click to skip Setup");
        else {
            char c = tft.getChar (NULL, NULL);
            if (tt != TT_NONE || c != CHAR_NONE) {
                drainTouch();
                if (c == CHAR_ESC || (tt != TT_NONE && inBox (s, skip_b))) {
                    drawStringInBox ("Skip", skip_b, true, TX_C);
                    return (false);
                }
                    
                break;
            }
            wdDelay(100);
        }
    }

    return (to > 0);
}





/* init display and supporting StringPrompt and BoolPrompt data structs
 */
static void initDisplay()
{
    // erase screen
    eraseScreen();

    // set invalid page
    cur_page = -1;


#if defined(_SHOW_ALL) || defined(_MARK_BOUNDS)
    // don't show my creds when testing
    strcpy (wifi_ssid, "mywifissid");
    strcpy (wifi_pw, "mywifipassword");
#endif

    // set all v_ci to right ends
    for (int i = 0; i < N_SPR; i++)
        string_pr[i].v_ci = strlen (string_pr[i].v_str);

    // force drawing first page
    cur_page = -1;
    changePage(0);
}

static void drawBMEPrompts (bool on)
{
    if (on) {
        drawSPPromptValue (&string_pr[BME76DT_SPR]);
        drawSPPromptValue (&string_pr[BME76DP_SPR]);
        drawSPPromptValue (&string_pr[BME77DT_SPR]);
        drawSPPromptValue (&string_pr[BME77DP_SPR]);
    } else {
        eraseSPPromptValue (&string_pr[BME76DT_SPR]);
        eraseSPPromptValue (&string_pr[BME76DP_SPR]);
        eraseSPPromptValue (&string_pr[BME77DT_SPR]);
        eraseSPPromptValue (&string_pr[BME77DP_SPR]);
    }
}

static void drawNMEAPrompts (bool on)
{
    if (on) {
        drawSPPromptValue (&string_pr[NMEAFILE_SPR]);
        drawBPPromptState (&bool_pr[NMEAFOLLOW_BPR]);
        drawBPPromptState (&bool_pr[NMEABAUDA_BPR]);
        drawBPPromptState (&bool_pr[NMEABAUDB_BPR]);
        bool_pr[NMEAON_BPR].state = true;
        drawBPState (&bool_pr[NMEAON_BPR]);
    } else {
        eraseSPPromptValue (&string_pr[NMEAFILE_SPR]);
        eraseBPPromptState (&bool_pr[NMEAFOLLOW_BPR]);
        eraseBPPromptState (&bool_pr[NMEABAUDA_BPR]);
        eraseBPPromptState (&bool_pr[NMEABAUDB_BPR]);
        bool_pr[NMEAON_BPR].state = false;
        drawBPState (&bool_pr[NMEAON_BPR]);
    }
}

static void drawGPSDPrompts (bool on)
{
    if (on) {
        drawSPPromptValue (&string_pr[GPSDHOST_SPR]);
        drawBPPromptState (&bool_pr[GPSDFOLLOW_BPR]);
        bool_pr[GPSDON_BPR].state = true;
        drawBPState (&bool_pr[GPSDON_BPR]);
    } else {
        eraseSPPromptValue (&string_pr[GPSDHOST_SPR]);
        eraseBPPromptState (&bool_pr[GPSDFOLLOW_BPR]);
        bool_pr[GPSDON_BPR].state = false;
        drawBPState (&bool_pr[GPSDON_BPR]);
    }
}

static void drawLLGPrompts (bool on)
{
    if (on) {
        drawSPPromptValue (&string_pr[LAT_SPR]);
        drawSPPromptValue (&string_pr[LNG_SPR]);
        drawSPPromptValue (&string_pr[GRID_SPR]);
    } else {
        eraseSPPromptValue (&string_pr[LAT_SPR]);
        eraseSPPromptValue (&string_pr[LNG_SPR]);
        eraseSPPromptValue (&string_pr[GRID_SPR]);
    }
}

/* insure at least one time source is selected
 */
static void insureOneTimeSource (void)
{
    // turn on NTP default if nothing else
    if (!bool_pr[GPSDON_BPR].state && !bool_pr[NMEAON_BPR].state && getNTPStateCode() == NTPSC_NO) {
        setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[NTPSC_DEF]);
        drawNTPPrompts ();
    }
}

static void drawGEOIPPrompt (bool on)
{
    bool_pr[GEOIP_BPR].state = on;
    drawBPState (&bool_pr[GEOIP_BPR]);
}

/* run the setup screen until all fields check ok and user wants to exit
 */
static void runSetup()
{
    drainTouch();

    SBox screen;
    screen.x = 0;
    screen.y = 0;
    screen.w = tft.width();
    screen.h = tft.height();

    UserInput ui = {
        screen,         // bounding box
        UI_UFuncNone,   // no aux function
        UF_UNUSED,      // don't care
        UI_NOTIMEOUT,   // wait forever
        UF_NOCLOCKS,    // no clocks
        {0, 0}, TT_NONE, '\0', false, false
    };

    do {
        StringPrompt *sp;
        BoolPrompt *bp = NULL;

        // wait forever for next tap or character input
        (void) waitForUser(ui);
        if (ui.kb_char == CHAR_NONE) {
            if (!s2char (ui.tap, ui.kb_char))
                ui.kb_char = CHAR_NONE;
        }

        // check NL
        if (ui.kb_char == CHAR_NL)
            continue;

        // process special cases first

        if (inBox (ui.tap, page_b)) {

            // change page back or forward depending on whether tapped in left or right half
            if (ui.tap.x > page_b.x + page_b.w/2)
                changePage ((cur_page+1)%N_PAGES);
            else
                changePage ((N_PAGES+cur_page-1)%N_PAGES);
            continue;
        }

        if (ui.kb_char == CHAR_ESC) {              // esc

            // show next page
            changePage ((cur_page+1)%N_PAGES);
            continue;
        }

        if (cur_page == COLOR_PAGE) {

            // check color page if tapped -- strings are checked as part of editing
            if (ui.kb_char == CHAR_NONE && handleCSelTouch (ui.tt, ui.tap))
                continue;
        }

        if (cur_page == ONOFF_PAGE) {

            if (checkOnOffTouch(ui.tt, ui.tap))
                continue;
        }

        // proceed with normal fields processing

        if (ui.kb_char == CHAR_TAB || ui.kb_char == CHAR_UP || ui.kb_char == CHAR_DOWN) {

            // move focus to next or prior tab position depending on shift modified
            eraseCursor();
            nextTabFocus((ui.kb_char == CHAR_TAB && ui.kb_shift) || ui.kb_char == CHAR_UP);
            drawCursor();

        } else if (cur_focus[cur_page].sp && ui.kb_char == CHAR_LEFT) {

            // move cursor one left as possible
            StringPrompt *sp = cur_focus[cur_page].sp;
            if (sp->v_ci > 0) {
                eraseSPValue (sp);
                sp->v_ci -= 1;
                drawSPValue (sp);
                drawCursor ();
            }

        } else if (cur_focus[cur_page].sp && ui.kb_char == CHAR_RIGHT) {

            // move cursor one right as possible

            StringPrompt *sp = cur_focus[cur_page].sp;
            eraseSPValue (sp);
            sp->v_ci += 1;
            drawSPValue (sp);
            drawCursor ();

        } else if (cur_focus[cur_page].sp && (ui.kb_char == CHAR_DEL || ui.kb_char == CHAR_BS)) {

            // tapped Delete while focus is string

            StringPrompt *sp = cur_focus[cur_page].sp;
            size_t vl = strlen (sp->v_str);
            if (vl > 0 && sp->v_ci > 0) {

                eraseSPValue (sp);

                // remove v_str[v_ci-1] and redraw
                memmove (&sp->v_str[sp->v_ci-1], &sp->v_str[sp->v_ci], vl - sp->v_ci + 1);      // w/ EOS
                sp->v_ci -= 1;
                drawSPValue (sp);
                drawCursor ();

                // check special interest string fields
                if (cur_page == LATLNG_PAGE)
                    checkLLGEdit(sp);
                if (cur_page == COLOR_PAGE)
                    handleCSelKB();

            } else
                flagErrField (sp, true, "empty");


        } else if (cur_focus[cur_page].sp && isprint(ui.kb_char)) {

            // received a new char for inserting into string with focus

            StringPrompt *sp = cur_focus[cur_page].sp;

            // enforce call sign upper case
            if (sp == &string_pr[CALL_SPR])
                ui.kb_char = toupper(ui.kb_char);

            // insert ui.kb_char at v_ci if room, else ignore
            size_t vl = strlen (sp->v_str);
            if (vl < sp->v_len-1U) {

                eraseSPValue (sp);

                // make room by shifting right and redraw
                memmove (&sp->v_str[sp->v_ci+1], &sp->v_str[sp->v_ci], vl - sp->v_ci + 1);      // w/EOS
                sp->v_str[sp->v_ci++] = ui.kb_char;
                drawSPValue (sp);
                drawCursor ();

                // check special interest string fields
                if (cur_page == LATLNG_PAGE)
                    checkLLGEdit(sp);
                if (cur_page == COLOR_PAGE)
                    handleCSelKB();

            } else
                flagErrField (sp, true, "full");

        } else if (tappedBool (ui.tap, &bp) || (ui.kb_char == CHAR_SPACE && cur_focus[cur_page].bp)) {

            // typing space applies to focus bool
            if (ui.kb_char == CHAR_SPACE)
                bp = cur_focus[cur_page].bp;

            // ignore tapping on bools not being shown
            if (!bp || !boolIsRelevant(bp))
                continue;

            // check for tooltip
            if (bp->tt && ui.tt == TT_TAP_BX) {
                tooltip (ui.tap, bp->tt);
                continue;
            }

            // toggle and redraw with new cursor position
            engageBoolTap (bp);

            // check for possible secondary implications

            if (bp == &bool_pr[X11_FULLSCRN_BPR]) {

                // check for full screen that won't fit
                if (bp->state) {
                    int maxw = 0, maxh = 0;
                    tft.getScreenSize (&maxw, &maxh);
                    if (BUILD_W > maxw || BUILD_H > maxh) {
                        tft.setCursor (bp->s_box.x, bp->s_box.y+PR_H-PR_D);
                        tft.setTextColor (RA8875_RED);
                        eraseBPState (bp);
                        tft.print ("Won't fit");
                        wdDelay (ERRDWELL_MS);
                        bp->state = false;
                        drawBPState (bp);
                    }
                }
            }

            else if (bp == &bool_pr[ADIFSET_BPR]) {
                // show/hide ADIF file name
                if (bp->state) {
                    // show file name
                    eraseBPState (&bool_pr[ADIFSET_BPR]);
                    drawSPPromptValue (&string_pr[ADIFFN_SPR]);
                } else {
                    // show no
                    eraseSPPromptValue (&string_pr[ADIFFN_SPR]);
                    drawBPState (&bool_pr[ADIFSET_BPR]);
                }
            }

            else if (bp == &bool_pr[RIGUSE_BPR]) {

                // show/hide rigctld host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[RIGUSE_BPR]);
                    drawSPPromptValue (&string_pr[RIGHOST_SPR]);
                    drawSPPromptValue (&string_pr[RIGPORT_SPR]);
                    // show control
                    drawBPPrompt (&bool_pr[SETRADIO_BPR]);
                    drawBPState (&bool_pr[SETRADIO_BPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[RIGUSE_BPR]);
                    eraseSPPromptValue (&string_pr[RIGHOST_SPR]);
                    eraseSPPromptValue (&string_pr[RIGPORT_SPR]);
                    // no control if FLRIG also not on
                    if (!bool_pr[FLRIGUSE_BPR].state) {
                        eraseBPPrompt (&bool_pr[SETRADIO_BPR]);
                        eraseBPState (&bool_pr[SETRADIO_BPR]);
                    }
                }
            }

            else if (bp == &bool_pr[ROTUSE_BPR]) {
                // show/hide rotctld host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    drawSPPromptValue (&string_pr[ROTHOST_SPR]);
                    drawSPPromptValue (&string_pr[ROTPORT_SPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[ROTUSE_BPR]);
                    eraseSPPromptValue (&string_pr[ROTHOST_SPR]);
                    eraseSPPromptValue (&string_pr[ROTPORT_SPR]);
                }
            }

            else if (bp == &bool_pr[FLRIGUSE_BPR]) {

                // show/hide flrig host and port
                if (bp->state) {
                    // show host and port prompts and say yes
                    drawBPState (&bool_pr[FLRIGUSE_BPR]);
                    drawSPPromptValue (&string_pr[FLRIGHOST_SPR]);
                    drawSPPromptValue (&string_pr[FLRIGPORT_SPR]);
                    // show control
                    drawBPPrompt (&bool_pr[SETRADIO_BPR]);
                    drawBPState (&bool_pr[SETRADIO_BPR]);
                } else {
                    // hide and say no
                    drawBPState (&bool_pr[FLRIGUSE_BPR]);
                    eraseSPPromptValue (&string_pr[FLRIGHOST_SPR]);
                    eraseSPPromptValue (&string_pr[FLRIGPORT_SPR]);
                    // no control if RIG also not on
                    if (!bool_pr[RIGUSE_BPR].state) {
                        eraseBPPrompt (&bool_pr[SETRADIO_BPR]);
                        eraseBPState (&bool_pr[SETRADIO_BPR]);
                    }
                }
            }

            else if (bp == &bool_pr[CLUSTER_BPR] || bp == &bool_pr[CLISUDP_BPR]) {
                // so many show/hide dx cluster prompts easier to just redraw the page
                changePage (cur_page);
            }

            else if (bp == &bool_pr[NTPA_BPR] || bp == &bool_pr[NTPB_BPR]) {
                if (getNTPStateCode() == NTPSC_NO) {
                    // prevent NO if others also no
                    if (!bool_pr[GPSDON_BPR].state && !bool_pr[NMEAON_BPR].state) {
                        setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[NTPSC_DEF]);
                        drawNTPPrompts();
                    }
                } else {
                    drawGPSDPrompts (false);
                    drawNMEAPrompts (false);
                    drawLLGPrompts (true);
                }
                drawNTPPrompts ();
            }

            else if (bp == &bool_pr[GEOIP_BPR]) {
                if (bp->state) {
                    drawGEOIPPrompt (true);
                    drawGPSDPrompts (false);
                    drawNMEAPrompts (false);
                    drawLLGPrompts (false);
                    insureOneTimeSource();
                } else {
                    drawGEOIPPrompt (false);
                    drawLLGPrompts (true);
                }
            }

            else if (bp == &bool_pr[GPSDON_BPR]) {
                if (bp->state) {
                    drawGPSDPrompts (true);
                    drawGEOIPPrompt (false);
                    drawNMEAPrompts (false);
                    drawLLGPrompts (false);
                    setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[NTPSC_NO]);
                    drawNTPPrompts();
                } else {
                    drawGPSDPrompts (false);
                    drawLLGPrompts (true);
                    insureOneTimeSource();
                }
            }

            else if (bp == &bool_pr[NMEAON_BPR]) {
                if (bp->state) {
                    drawNMEAPrompts (true);
                    drawGPSDPrompts (false);
                    drawGEOIPPrompt (false);
                    drawLLGPrompts (false);
                    setEntangledValue (NTPA_BPR, NTPB_BPR, ntp_sn[NTPSC_NO]);
                    drawNTPPrompts();
                } else {
                    drawNMEAPrompts (false);
                    drawLLGPrompts (true);
                    insureOneTimeSource();
                }
            }

            else if (bp == &bool_pr[GPIOOK_BPR]) {
                if (bp->state) {
                    drawBPPrompt (&bool_pr[KX3A_BPR]);
                    drawEntangledBools(&bool_pr[KX3A_BPR], &bool_pr[KX3B_BPR]);
                } else {
                    bool_pr[KX3A_BPR].state = false;
                    eraseBPPromptState (&bool_pr[KX3A_BPR]);
                    eraseBPPromptState (&bool_pr[KX3B_BPR]);
                }
                drawBMEPrompts (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
            }

            else if (bp == &bool_pr[I2CON_BPR]) {
                if (bp->state) {
                    // show file name
                    eraseBPState (&bool_pr[I2CON_BPR]);
                    drawSPPromptValue (&string_pr[I2CFN_SPR]);
                } else {
                    // show no
                    eraseSPPromptValue (&string_pr[I2CFN_SPR]);
                    drawBPState (&bool_pr[I2CON_BPR]);
                }
                drawBMEPrompts (bool_pr[GPIOOK_BPR].state || bool_pr[I2CON_BPR].state);
            }

            else if (bp == &bool_pr[DXCLCMDPGA_BPR] || bp == &bool_pr[DXCLCMDPGB_BPR]) {

                // redraw showing next page of commands.
                // TODO: just draw the commands to avoid moving focus back to the beginning
                changePage (cur_page);
            } 

          #if defined(_WIFI_ASK)
            else if (bp == &bool_pr[WIFI_BPR]) {
                // show/hide wifi prompts
                if (bp->state) {
                    eraseBPState (&bool_pr[WIFI_BPR]);
                    drawSPPromptValue (&string_pr[WIFISSID_SPR]);
                    drawSPPromptValue (&string_pr[WIFIPASS_SPR]);
                } else {
                    eraseSPPromptValue (&string_pr[WIFISSID_SPR]);
                    eraseSPPromptValue (&string_pr[WIFIPASS_SPR]);
                    drawBPState (&bool_pr[WIFI_BPR]);
                }
            }
          #endif // _WIFI_ASK

          #if defined(_SUPPORT_KX3)
            else if (bp == &bool_pr[KX3A_BPR]) {
                // show/hide baud rate but honor GPIOOK
                if (bool_pr[GPIOOK_BPR].state) {
                    drawEntangledBools(&bool_pr[KX3A_BPR], &bool_pr[KX3B_BPR]);
                } else if (bool_pr[KX3A_BPR].state) {
                    // maintain off if no GPIO
                    bool_pr[KX3A_BPR].state = false;
                    drawBPPromptState (&bool_pr[KX3A_BPR]);
                }
            }
          #endif // _SUPPORT_KX3

        } else if (tappedStringPrompt (ui.tap, &sp) && stringIsRelevant (sp)) {

            // check for tooltip
            if (sp->tt && ui.tt == TT_TAP_BX) {
                tooltip (ui.tap, sp->tt);
                continue;
            }

            // move focus here unless already there
            if (cur_focus[cur_page].sp != sp) {
                eraseCursor ();
                setFocus (sp, NULL);
                drawCursor ();
            }
        }

    } while (!(ui.kb_char == CHAR_CR || ui.kb_char == CHAR_NL) || !validateStringPrompts(true));

    drawDoneButton(true);

    // all fields are valid

}

/* update the case of each component of the given grid square.
 * N.B. we do NOT validate the grid
 */
static char *scrubGrid (char *g)
{
    g[0] = toupper(g[0]);
    g[1] = toupper(g[1]);
    g[4] = tolower(g[4]);
    g[5] = tolower(g[5]);

    return (g);
}

/* save all parameters to NVRAM
 */
static void saveParams2NV()
{
    // persist results 

#if !defined(_SHOW_ALL) && !defined(_MARK_BOUNDS)
    // only persist creds when not testing
    NVWriteString(NV_WIFI_SSID, wifi_ssid);
    NVWriteString(NV_WIFI_PASSWD, wifi_pw);
#endif

    strtoupper (cs_info.call);
    NVWriteString(NV_CALLSIGN, cs_info.call);
    NVWriteUInt8 (NV_UNITS, getEntangledIndex (UNITSA_BPR, UNITSB_BPR));
    NVWriteUInt8 (NV_WEEKMON, bool_pr[WEEKDAY1MON_BPR].state);
    NVWriteUInt8 (NV_BEAR_MAG, bool_pr[BEARING_BPR].state);
    NVWriteUInt32 (NV_KX3BAUD, bool_pr[KX3A_BPR].state ? (bool_pr[KX3B_BPR].state ? 38400 : 4800) : 0);
    NVWriteFloat (NV_TEMPCORR76, temp_corr[BME_76]);
    NVWriteFloat (NV_PRESCORR76, pres_corr[BME_76]);
    NVWriteFloat (NV_TEMPCORR77, temp_corr[BME_77]);
    NVWriteFloat (NV_PRESCORR77, pres_corr[BME_77]);
    NVWriteUInt8 (NV_BR_MIN, bright_min);
    NVWriteUInt8 (NV_BR_MAX, bright_max);
    NVWriteUInt8 (NV_USEGPSD, (bool_pr[GPSDON_BPR].state ? USEGPSD_FORTIME_BIT : 0)
                | (bool_pr[GPSDON_BPR].state && bool_pr[GPSDFOLLOW_BPR].state ? USEGPSD_FORLOC_BIT : 0));
    NVWriteString (NV_GPSDHOST, gpsd_host);
    NVWriteUInt8 (NV_USENMEA, (bool_pr[NMEAON_BPR].state ? USENMEA_FORTIME_BIT : 0)
                | (bool_pr[NMEAON_BPR].state && bool_pr[NMEAFOLLOW_BPR].state ? USENMEA_FORLOC_BIT : 0));
    NVWriteString (NV_NMEAFILE, nmea_file);
    NVWriteUInt16 (NV_NMEABAUD, (uint16_t) atoi(getEntangledValue (NMEABAUDA_BPR, NMEABAUDB_BPR)));
    NVWriteUInt8 (NV_USEDXCLUSTER, bool_pr[CLUSTER_BPR].state);
    NVWriteUInt8 (NV_WSJT_DX, bool_pr[CLISUDP_BPR].state);
    NVWriteString (NV_DXHOST, dx_host);

    NVWriteString (NV_DXWLIST, dx_wlist);
    NVWriteUInt8 (NV_DXWLISTMASK, bool_pr[DXWLISTA_BPR].state | (bool_pr[DXWLISTB_BPR].state << 1));
    NVWriteString (NV_ADIFWLIST, adif_wlist);
    NVWriteUInt8 (NV_ADIFWLISTMASK, bool_pr[ADIFWLISTA_BPR].state | (bool_pr[ADIFWLISTB_BPR].state << 1));
    NVWriteString (NV_ONTAWLIST, onta_wlist);
    NVWriteUInt8 (NV_ONTAWLISTMASK, bool_pr[ONTAWLISTA_BPR].state | (bool_pr[ONTAWLISTB_BPR].state << 1));

    // N.B. these are NOT contiguous so can not loop through N_DXCLCMDS
    NVWriteString (NV_DXCMD0, dxcl_cmds[0]);
    NVWriteString (NV_DXCMD1, dxcl_cmds[1]);
    NVWriteString (NV_DXCMD2, dxcl_cmds[2]);
    NVWriteString (NV_DXCMD3, dxcl_cmds[3]);
    NVWriteString (NV_DXCMD4, dxcl_cmds[4]);
    NVWriteString (NV_DXCMD5, dxcl_cmds[5]);
    NVWriteString (NV_DXCMD6, dxcl_cmds[6]);
    NVWriteString (NV_DXCMD7, dxcl_cmds[7]);
    NVWriteString (NV_DXCMD8, dxcl_cmds[8]);
    NVWriteString (NV_DXCMD9, dxcl_cmds[9]);
    NVWriteString (NV_DXCMD10, dxcl_cmds[10]);
    NVWriteString (NV_DXCMD11, dxcl_cmds[11]);

    uint16_t dx_cmdmask = 0;
    for (int i = 0; i < N_DXCLCMDS; i++)
        if (bool_pr[DXCLCMD0_BPR+i].state)
            dx_cmdmask |= (1<<i);
    NVWriteUInt16 (NV_DXCMDMASK, dx_cmdmask);

    // insure legal dx_login
    if (!clusterLoginOk())
        setClusterLogin();

    NVWriteUInt16 (NV_DXPORT, dx_port);
    NVWriteString (NV_DXLOGIN, dx_login);
    NVWriteString (NV_HAMSATKEY, hamsat_key);

    NVWriteUInt8 (NV_LOGUSAGE, bool_pr[LOGUSAGE_BPR].state);
    NVWriteUInt8 (NV_LBLSTYLE,
              bool_pr[SPOTLBLA_BPR].state ? (bool_pr[SPOTLBLB_BPR].state ? LBL_CALL : LBL_DOT)
                                          : (bool_pr[SPOTLBLB_BPR].state ? LBL_PREFIX : LBL_NONE)
    );
    NVWriteUInt8 (NV_NTPSET, (uint8_t)getNTPStateCode());
    NVWriteString (NV_NTPHOST, ntp_host);
    NVWriteString (NV_ADIFFN, bool_pr[ADIFSET_BPR].state ? adif_fn : "");       // TODO: separate on/off
    NVWriteUInt8 (NV_I2CON, bool_pr[I2CON_BPR].state);
    NVWriteString (NV_I2CFN, i2c_fn);
    NVWriteUInt8 (NV_DATEMDY, bool_pr[DATEFMT_MDY_BPR].state);
    NVWriteUInt8 (NV_DATEDMYYMD, bool_pr[DATEFMT_DMYYMD_BPR].state);
    NVWriteUInt8 (NV_GPIOOK, bool_pr[GPIOOK_BPR].state);
    NVWriteInt16 (NV_CENTERLNG, center_lng);
    NVWriteUInt8 (NV_RIGUSE, bool_pr[RIGUSE_BPR].state);
    NVWriteString (NV_RIGHOST, rig_host);
    NVWriteUInt16 (NV_RIGPORT, rig_port);
    NVWriteUInt8 (NV_ROTUSE, bool_pr[ROTUSE_BPR].state);
    NVWriteString (NV_ROTHOST, rot_host);
    NVWriteUInt16 (NV_ROTPORT, rot_port);
    NVWriteUInt8 (NV_FLRIGUSE, bool_pr[FLRIGUSE_BPR].state);
    NVWriteString (NV_FLRIGHOST, flrig_host);
    NVWriteUInt16 (NV_FLRIGPORT, flrig_port);
    NVWriteUInt8 (NV_SETRADIO, bool_pr[SETRADIO_BPR].state);
    NVWriteUInt8 (NV_SCROLLDIR, bool_pr[SCROLLDIR_BPR].state);
    NVWriteUInt8 (NV_NEWDXDEWX, bool_pr[NEWDXDEWX_BPR].state);
    NVWriteUInt8 (NV_WEBFS, bool_pr[WEB_FULLSCRN_BPR].state);
    NVWriteUInt8 (NV_PANEROTP, getPaneRotationPeriod());
    NVWriteUInt8 (NV_MAPROTP, getMapRotationPeriod());
    NVWriteUInt8 (NV_SHOWPIP, showPIP());
    NVWriteUInt8 (NV_SHOWPLANETS, showPlanets());
    NVWriteUInt8 (NV_AUTOMAP, autoMap());
    NVWriteUInt8 (NV_GRAYDPY, (uint8_t)getGrayDisplay());
    NVWriteUInt8 (NV_QRZID, getQRZId());
    NVWriteUInt8 (NV_UDPSPOTS, bool_pr[UDPSPOTS_BPR].state);
    NVWriteUInt8 (NV_UDPSETSDX, bool_pr[UDPSETSDX_BPR].state);
    NVWriteUInt8 (NV_MAXTLEAGE, maxTLEAgeDays());
    NVWriteUInt16 (NV_MINLBLDIST, atoi(getEntangledValue (LBLDISTA_BPR, LBLDISTB_BPR)));

    // save and engage user's X11 settings
    uint16_t x11flags = 0;
    if (bool_pr[X11_FULLSCRN_BPR].state)
        x11flags |= X11BIT_FULLSCREEN;
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
    tft.X11OptionsEngageNow(getX11FullScreen());

    // save colors and state
    uint32_t a_mask = 0, t_mask = 0, o_mask = 0;
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        uint16_t c = RGB565(p.r, p.g, p.b);
        NVWriteUInt16 (p.def_c_nv, c);
        if (p.a_state) a_mask |= (1<<i);
        if (p.t_state) t_mask |= (1<<i);
        if (p.o_state) o_mask |= (1<<i);
    }
    NVWriteUInt32 (NV_CSELDASHED, a_mask);
    NVWriteUInt32 (NV_CSELTHIN, t_mask);
    NVWriteUInt32 (NV_CSELONOFF, o_mask);

    // save DE tz and grid only if ll was edited and op is not using some other method to set location
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSDON_BPR].state && !bool_pr[NMEAON_BPR].state && ll_edited) {
        de_ll.normalize();
        NVWriteFloat(NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat(NV_DE_LNG, de_ll.lng_d);
        NVWriteString(NV_DE_GRID, scrubGrid(string_pr[GRID_SPR].v_str));
        // N.B. do not set TZ here because network not yet up -- rely on main setup()
    }

    // save auto update time
    int aup;
    (void) autoUpgrade (aup);
    NVWriteInt8 (NV_AUTOUPGRADE, (int8_t)aup);
}

/* draw the given string with border centered inside the given box using the current font.
 */
void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color)
{
    // get size and colors
    uint16_t sw = getTextWidth ((char*)str);
    uint16_t fg = inverted ? BG_C : color;
    uint16_t bg = inverted ? color : BG_C;

    // set location, allowing that the FAST font coords are from its top, the others are from the baseline.
    FontWeight fw;
    FontSize fs;
    getFontStyle (&fw, &fs);
    uint16_t fy = b.y + (fs == FAST_FONT ? b.h/5 : 3*b.h/4);
    tft.setCursor (b.x+(b.w-sw)/2, fy);

    // draw
    fillSBox (b, bg);
    drawSBox (b, KB_C);
    tft.setTextColor (fg);
    tft.print(str);
}


/* grab everything from NV, setting defaults if first time, then allow user to change,
 * saving to NV if needed.
 */
void clockSetup()
{
    // set initial font, could use BOLD if not for long wifi password
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // load values from nvram, else set defaults
    initSetup();

    // prep shadowed params, if nothing else for logging them
    initShadowedParams();

    // ask user whether they want to run setup, display anyway if any strings are invalid
    bool str_ok = validateStringPrompts (false);
    if ((!str_ok || askRun()) && askPasswd ("setup", false)) {

        // init display prompts and options
        initDisplay();

        // start by indicating any errors
        if (!str_ok)
            validateStringPrompts (true);

        // main interaction loop
        Serial.printf ("Setup: running\n");
        runSetup();
        Serial.printf ("Setup: complete\n");

        // inform cluster if location changed
        if (ll_edited)
            sendDXClusterDELLGrid();

    } else
        Serial.printf ("Setup: declined\n");

    // save, log and clean up shadowed params
    logAllPrompts();
    saveParams2NV();
    freeShadowedParams();

    // ok to send liveweb full screen setting
    liveweb_fs_ready = true;
}

/* return whether the given string is a valid latitude specification, if so set lat in degrees
 */
bool latSpecIsValid (const char *lat_spec, float &lat)
{
    char *endp;
    lat = strtod (lat_spec, &endp);
    char ns = *endp;
    if (ns == 'S' || ns == 's')
        lat = -lat;
    else if (ns != 'N' && ns != 'n' && ns != '\0')
        return (false);
    if (lat < -90 || lat > 90)
        return (false);
    return (true);
}

/* return whether the given string is a valid longitude specification, if so set lng in degrees
 * N.B. we allow 180 east in spec but return lng as 180 west.
 */
bool lngSpecIsValid (const char *lng_spec, float &lng)
{
    char *endp;
    lng = strtod (lng_spec, &endp);
    char ew = *endp;
    if (ew == 'W' || ew == 'w')
        lng = -lng;
    else if (ew != 'E' && ew != 'e' && ew != '\0')
        return (false);
    if (lng < -180 || lng > 180)
        return (false);
    if (lng == 180)
        lng = -180;

    return (true);
}



/* only for main() to call once very early to allow setting initial default
 */
void setX11FullScreen (bool on)
{
    uint16_t x11flags = (on ? X11BIT_FULLSCREEN : 0);
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
}


/* return pointer to static storage containing the WiFi SSID, else NULL if not used
 */
const char *getWiFiSSID()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifi_ssid);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the WiFi password, else NULL if not used
 */
const char *getWiFiPW()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifi_pw);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the Callsign
 */
const char *getCallsign()
{
    return (cs_info.call);
}

/* set a new default/persistent DE call sign and dx_login to match.
 * intended for use by set_newde API.
 * return whether s qualifies.
 */
bool setCallsign (const char *cs)
{
    if (!callsignOk(cs))
        return (false);

    strncpy (cs_info.call, cs, sizeof(cs_info.call)-1);
    setClusterLogin();

    NVWriteString (NV_CALLSIGN, cs_info.call);
    NVWriteString (NV_DXLOGIN, dx_login);
    return (true);
}

/* return pointer to static storage containing the DX cluster host
 * N.B. only sensible if useDXCluster() and !useWSJTX()
 */
const char *getDXClusterHost()
{
    return (dx_host);
}

/* return pointer to static storage containing the GPSD host
 * N.B. only sensible if useGPSDTime() and/or useGPSDLoc() is true
 */
const char *getGPSDHost()
{
    return (gpsd_host);
}

/* return pointer to static storage containing the NMEA host
 * N.B. only sensible if useNMEATime() and/or useNMEALoc() is true
 */
const char *getNMEAFile()
{
    return (nmea_file);
}

/* return pointer to static storage containing the NTP host defined herein
 * N.B. only sensible if useLocalNTPHost() is true
 */
const char *getLocalNTPHost()
{
    return (ntp_host);
}

/* return dx cluster node port
 * N.B. only sensible if useDXCluster() is true
 */
int getDXClusterPort()
{
    return (dx_port);
}

/* return whether we should be allowing DX cluster
 */
bool useDXCluster()
{
    return (bool_pr[CLUSTER_BPR].state);
}

/* return whether week starts on Monday, else Sunday
 */
bool weekStartsOnMonday()
{
    return (bool_pr[WEEKDAY1MON_BPR].state);
}

/* return whether bearings should be magnetic, else true
 */
bool useMagBearing()
{
    return (bool_pr[BEARING_BPR].state);
}

/* return Raw path width for the given color, or zero if path is not to be drawn.
 * caller can divide by tft.SCALESZ if want canonical units.
 */
int getRawPathWidth (ColorSelection id)
{
    ColSelPrompt &csp = csel_pr[id];
    return (csp.o_state ? (csp.t_state ? RAWTHINPATHSZ : RAWWIDEPATHSZ) : 0);
}

/* return Raw spot dot radius to be used with the given color path.
 * caller can divide by tft.SCALESZ if want canonical units.
 * N.B. this always returns a finite size; use getSpotLabelType() to decide whether/how to draw at all.
 */
int getRawSpotRadius (ColorSelection id)
{
    ColSelPrompt &csp = csel_pr[id];
    return (getRawSpotRadius (csp));
}

/* return desired spot label style
 */
LabelType getSpotLabelType (void)
{
    const char *lbl = getEntangledValue (SPOTLBLA_BPR, SPOTLBLB_BPR);
    for (int i = 0; i < LBL_N; i++)
        if (strcmp (lbl_styles[i], lbl) == 0)
            return ((LabelType)i);
    fatalError ("Bogus label type: %s", lbl);
    return (LBL_NONE);  // lint
}

/* return whether to use IP geolocation
 */
bool useGeoIP()
{
    return (bool_pr[GEOIP_BPR].state);
}

/* return whether to use GPSD for time
 */
bool useGPSDTime()
{
    return (bool_pr[GPSDON_BPR].state);
}

/* return whether to use GPSD for location
 */
bool useGPSDLoc()
{
    return (bool_pr[GPSDON_BPR].state && bool_pr[GPSDFOLLOW_BPR].state);
}

/* return whether to use NMEA for time
 */
bool useNMEATime()
{
    return (bool_pr[NMEAON_BPR].state);
}

/* return whether to use NMEA for location
 */
bool useNMEALoc()
{
    return (bool_pr[NMEAON_BPR].state && bool_pr[NMEAFOLLOW_BPR].state);
}

/* return NMEA connection speed
 */
const char *getNMEABaud(void)
{
    return (getEntangledValue (NMEABAUDA_BPR, NMEABAUDB_BPR));
}

/* return whether to use the NTP host set herein
 */
bool useLocalNTPHost()
{
    return (strcmp (getEntangledValue (NTPA_BPR, NTPB_BPR), ntp_sn[NTPSC_HOST]) == 0);
}

/* return whether to use OS for time
 */
bool useOSTime()
{
    return (strcmp (getEntangledValue (NTPA_BPR, NTPB_BPR), ntp_sn[NTPSC_OS]) == 0);
}

/* return desired date format
 */
DateFormat getDateFormat()
{
    if (bool_pr[DATEFMT_MDY_BPR].state)
        return (bool_pr[DATEFMT_DMYYMD_BPR].state ? DF_YMD : DF_DMY);
    else
        return (DF_MDY);
}

/* return whether user is ok with logging usage
 */
bool logUsageOk()
{
    return (bool_pr[LOGUSAGE_BPR].state);
}

/* return whether ok to use GPIO
 */
bool GPIOOk ()
{
    return (bool_pr[GPIOOK_BPR].state);
}



/* set temp correction, i is BME_76 or BME_77.
 * caller should establish units according to useMetricUnits()/useBritishUnits().
 * save in NV if ok.
 * return whether appropriate.
 */
bool setBMETempCorr(BMEIndex i, float delta)
{
    if ((i != BME_76 && i != BME_77) || !getBMEData(i, false))
        return (false);

    // engage
    temp_corr[(int)i] = delta;

    // persist
    NVWriteFloat (i == BME_76 ? NV_TEMPCORR76 : NV_TEMPCORR77, temp_corr[i]);

    return (true);
}

/* return temperature correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()/useBritishUnits().
 */
float getBMETempCorr(int i)
{
    return (temp_corr[i % MAX_N_BME]);
}

/* set pressure correction, i is BME_76 or BME_77.
 * caller should establish units according to useMetricUnits()/useBritishUnits().
 * save in NV if ok.
 * return whether appropriate.
 */
bool setBMEPresCorr(BMEIndex i, float delta)
{
    if ((i != BME_76 && i != BME_77) || !getBMEData(i, false))
        return (false);

    // engage
    pres_corr[(int)i] = delta;

    // persist
    NVWriteFloat (i == BME_76 ? NV_PRESCORR76 : NV_PRESCORR77, pres_corr[i]);

    return (true);
}

/* return pressure correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()/useBritishUnits().
 */
float getBMEPresCorr(int i)
{
    return (pres_corr[i % MAX_N_BME]);
}



/* return KX3 baud rate, 0 if off or no GPIO
 */
uint32_t getKX3Baud()
{
    return (bool_pr[KX3A_BPR].state && bool_pr[GPIOOK_BPR].state
                ? (bool_pr[KX3B_BPR].state ? 38400 : 4800) : 0);
}

/* return desired maximum brightness, percentage
 */
uint8_t getBrMax()
{
    return (brDimmableOk() ? bright_max : 100);
}

/* return desired minimum brightness, percentage
 */
uint8_t getBrMin()
{
    return (brDimmableOk() ? bright_min : 0);
}

/* whether to engage full screen using X11.
 */
bool getX11FullScreen(void)
{
    return (bool_pr[X11_FULLSCRN_BPR].state);
}

/* whether to engage full screen using the web interface.
 */
bool getWebFullScreen(void)
{
    return (bool_pr[WEB_FULLSCRN_BPR].state);
}

/* whether demo mode is requested
 */
bool getDemoMode(void)
{
    return (bool_pr[DEMO_BPR].state);
}

/* set whether demo mode is active
 */
void setDemoMode(bool on)
{
    bool_pr[DEMO_BPR].state = on;
}

/* return desired mercator map center longitude.
 * caller may assume -180 <= x < 180
 */
int16_t getCenterLng()
{
    return (alt_center_lng_set ? alt_center_lng : center_lng);
}

/* set desired mercator map center longitude.
 * N.B. only works for subsequenct calls to getCenterLng(): ignores initSetup() and not stored to NVRAM
 */
void setCenterLng (int16_t l)
{
    alt_center_lng  = ((l + (180+360*10)) % 360) - 180;       // enforce [-180, 180)
    alt_center_lng_set = true;
}

/* get rigctld host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getRigctld (char host[NV_RIGHOST_LEN], int *portp)
{
    if (bool_pr[RIGUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, rig_host);
        if (portp != NULL)
            *portp = rig_port;
        return (true);
    }
    return (false);
}

/* get rotctld host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getRotctld (char host[NV_ROTHOST_LEN], int *portp)
{
    if (bool_pr[ROTUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, rot_host);
        if (portp != NULL)
            *portp = rot_port;
        return (true);
    }
    return (false);
}

/* get flrig host and port and return whether it's to be used at all.
 * if just want yes/no either or both pointers can be NULL.
 */
bool getFlrig (char host[NV_FLRIGHOST_LEN], int *portp)
{
    if (bool_pr[FLRIGUSE_BPR].state) {
        if (host != NULL)
            strcpy (host, flrig_host);
        if (portp != NULL)
            *portp = flrig_port;
        return (true);
    }
    return (false);
}

/* return whether to issue radio commands, even if looking for PTT
 */
bool setRadio (void)
{
    return (bool_pr[SETRADIO_BPR].state);
}

/* get name to use for cluster login
 */
const char *getDXClusterLogin()
{
    return (dx_login[0] != '\0' ? dx_login : cs_info.call);
}

/* return cluster commands and whether each is on or off.
 */
void getDXClCommands(const char *cmds[N_DXCLCMDS], bool on[N_DXCLCMDS])
{
    for (int i = 0; i < N_DXCLCMDS; i++) {
        cmds[i] = dxcl_cmds[i];
        on[i] = bool_pr[DXCLCMD0_BPR+i].state;
    }
}

/* set a new host and port, return reason if false.
 * N.B. host is trimmed IN PLACE
 */
bool setDXCluster (char *host, char *port_str, Message &ynot)
{
    if (!hostOK(host,NV_DXHOST_LEN)) {
        ynot.set ("Bad host");
        return (false);
    }
    if (!portOK (port_str, 1000, &dx_port)) {
        ynot.set ("Bad port");
        return (false);
    }
    strncpy (dx_host, host, NV_DXHOST_LEN-1);
    NVWriteString (NV_DXHOST, dx_host);
    NVWriteUInt16 (NV_DXPORT, dx_port);
    return(true);
}

/* return current rgb565 color for the given ColorSelection
 */
uint16_t getMapColor (ColorSelection id)
{
    uint16_t c;
    if (id >= 0 && id < N_CSPR) {
        ColSelPrompt &p = csel_pr[id];
        c = RGB565(p.r, p.g, p.b);
    } else
        c = RA8875_BLACK;
    return (c);
}

/* return name of the given color
 */
const char* getMapColorName (ColorSelection id)
{
    const char *n;
    if (id >= 0 && id < N_CSPR)
        n = csel_pr[id].p_str;
    else
        n = "???";
    return (n);
}

/* try to set the specified color, name may use '_' or ' '.
 * return whether name is found.
 * N.B. this only saves the new value, other subsystems must do their own query to utilize new values.
 */
bool setMapColor (const char *name, uint16_t rgb565)
{
    // name w/o _
    char scrub_name[50];
    strncpySubChar (scrub_name, name, ' ', '_', sizeof(scrub_name));

    // look for match
    for (int i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = csel_pr[i];
        if (strcmp (scrub_name, p.p_str) == 0) {
            NVWriteUInt16 (p.def_c_nv, rgb565);
            p.r = RGB565_R(rgb565);
            p.g = RGB565_G(rgb565);
            p.b = RGB565_B(rgb565);
            return (true);
        }
    }
    return (false);
}

/* return whether the given color line should be dashed
 */
bool getPathDashed (ColorSelection id)
{
    return (csel_pr[id].a_state);
}

/* return whether dx host is actually WSJT-X or actually any UDP source.
 */
bool useWSJTX(void)
{
    return (bool_pr[CLISUDP_BPR].state);
}

/* return name of ADIF, else NULL if not set
 */
const char *getADIFilename(void)
{
    return (bool_pr[ADIFSET_BPR].state ? adif_fn : NULL);
}

/* save new ADIF file name and set it On in case getADIFilename is called later
 * N.B. we assume checkADIFFilename has already been used.
 */
void setADIFFilename (const char *fn)
{
    snprintf (adif_fn, NV_ADIFFN_LEN, "%s", fn);
    NVWriteString (NV_ADIFFN, adif_fn);
    bool_pr[ADIFSET_BPR].state = true;
}

/* return name of I2C device to use, else NULL
 */
const char *getI2CFilename(void)
{
    // N.B. do not call I2CFnOk here, just rely on it having been used by setup to determine I2CON_BPR
    return (bool_pr[I2CON_BPR].state ? i2c_fn : NULL);
}


/* given a WatchList state name, return matching WatchListState, else WLA_NONE.
 */
WatchListState lookupWatchListState (const char *wl_state)
{
    for (int i = 0; i < WLA_N; i++) {
        if (strcmp (wl_state, wla_name[i]) == 0) {
            return ((WatchListState)i);
        }
    }
    return (WLA_NONE);
}


/* return the filtering state and optionally the name of the given watch list
 */
WatchListState getWatchListState (WatchListId wl_id, char name[WLA_MAXLEN])
{
    // insure valid
    if (!wlIdOk (wl_id))
        fatalError ("getWatchListState %d", (int)wl_id);
    WLInfo &wli = wl_info[wl_id];

    // get bool pair state
    const char *wl_v = getEntangledValue (wli.a_bpr, wli.b_bpr);

    // look up in name list
    WatchListState wl_s = lookupWatchListState (wl_v);
    if (wl_s == WLA_NONE)
        fatalError ("getWatchListState for %d unknown state %s", wl_id, wl_v);

    // pass back if interested
    if (name)
        snprintf (name, WLA_MAXLEN, "%s", wla_name[wl_s]);

    return (wl_s);
}

/* return a full-length malloced copy of the given watch list string and its total possible length.
 * N.B. caller must free
 */
void getWatchList (WatchListId wl_id, char **wl_copypp, size_t *wl_lenp)
{
    if (!wlIdOk (wl_id))
        fatalError ("getWatchList %d", (int)wl_id);

    char *wl_string = wl_info[wl_id].wlist;
    size_t wl_len = wl_info[wl_id].len;;

    *wl_copypp = (char *) malloc (wl_len);
    snprintf (*wl_copypp, wl_len, "%s", wl_string);
    *wl_lenp = wl_len;
}

/* save the given string in the given watch list.
 * N.B. silently truncated if too long
 */
void setWatchList (WatchListId wl_id, const char *new_state, char *new_wlstr)
{
    // get corresponding info
    if (!wlIdOk (wl_id))
        fatalError ("setWatchList %d %.10s %.10s", (int)wl_id, new_state, new_wlstr);
    WLInfo &wli = wl_info[wl_id];

    // set state
    setEntangledValue (wli.a_bpr, wli.b_bpr, new_state);

    // set cleaned up watchlist
    snprintf (wli.wlist, wli.len, "%s", wlCompress(new_wlstr));

    // save to NV
    NVWriteString (wli.nv_wl, wli.wlist);
    NVWriteUInt8 (wli.nv_wlmask, bool_pr[wli.a_bpr].state | (bool_pr[wli.b_bpr].state << 1));
}

/* given the text name of a WatchListState in tfp->label, change it IN PLACE to next in the series
 */
void rotateWatchListState (struct _menu_text *tfp)
{
    WatchListState wl_s = lookupWatchListState (tfp->label);
    if (wl_s == WLA_NONE)
        fatalError ("rotateWatchListState unknown name: %.10s", tfp->label);

    snprintf (tfp->label, tfp->l_mem, "%s", wla_name[((int)wl_s+1) % WLA_N]);
}

/* handy way to get the name of a watch list
 */
const char *getWatchListName (WatchListId wl_id)
{
    if (!wlIdOk (wl_id))
        fatalError ("getWatchListName %d", (int)wl_id);
    return (wl_info[wl_id].name);
}



/* return whether scolling panes should show the newest entry on top, else newest on bottom
 */
bool scrollTopToBottom(void)
{
    return (bool_pr[SCROLLDIR_BPR].state);
}


/* return whether to automatically show new DX or DE weather when either changes
 */
bool showNewDXDEWx(void)
{
    return (bool_pr[NEWDXDEWX_BPR].state);
}

/* return the desired pane rotation period, seconds
 */
int getPaneRotationPeriod (void)
{
    return (atoi (getEntangledValue (PANE_ROTPA_BPR, PANE_ROTPB_BPR)));
}

/* return whether to show the puplic IP address
 */
bool showPIP()
{
    return (bool_pr[SHOWPIP_BPR].state);
}

/* return whether to show planets on the map
 */
bool showPlanets()
{
    return (bool_pr[SHOWPLANETS_BPR].state);
}

/* return whether to run the automatic space weather map detection.
 */
bool autoMap()
{
    return (bool_pr[AUTOMAP_BPR].state);
}

/* return map rotation period, seconds
 */
int getMapRotationPeriod()
{
    return (atoi (getEntangledValue (MAP_ROTPA_BPR, MAP_ROTPB_BPR)));
}

/* return gray scale setting
 */
GrayDpy_t getGrayDisplay(void)
{
    const char *v = getEntangledValue (GRAYA_BPR, GRAYB_BPR);
    if (strcmp (v, "Map") == 0) return (GRAY_MAP);
    if (strcmp (v, "All") == 0) return (GRAY_ALL);
    return (GRAY_OFF);  // default?
}

/* return the user's chosen qrz_urltable index.
 */
QRZURLId getQRZId(void)
{
    const char *label = getEntangledValue (QRZBIOA_BPR, QRZBIOB_BPR);
    for (int i = 0; i < QRZ_N; i++)
        if (strcmp (label, qrz_urltable[i].label) == 0)
            return ((QRZURLId)i);
    fatalError ("unknown call bio label: %s", label);
    return (QRZ_NONE);  // lint
}

/* return whether to use metric units
 */
static bool useMetricUnits()
{
    return (strcmp (getEntangledValue (UNITSA_BPR, UNITSB_BPR), units_names[UNITS_MET]) == 0);
}

/* return whether to use british units
 */
static bool useBritishUnits()
{
    return (strcmp (getEntangledValue (UNITSA_BPR, UNITSB_BPR), units_names[UNITS_BRIT]) == 0);
}

/* show temperature in C, else F
 */
bool showTempC(void)
{
    return (useMetricUnits() || useBritishUnits());
}

/* show atmopsheric pressure in hPa, else inHg
 */
bool showATMhPa(void)
{
    return (useMetricUnits() || useBritishUnits());
}

/* distance in km and speeds in km/hr, else mi and mph
 */
bool showDistKm(void)
{
    return (useMetricUnits());
}

/* return whether a UDP spot also sets DX
 */
bool UDPSetsDX(void)
{
    return (bool_pr[UDPSETSDX_BPR].state);
}

/* return whether to accept the given spot based on rx_call equal (or not) to our call
 */
bool useUDPSpot (const DXSpot &s)
{
    // true means any spot is ok, false means accept iff rx_call is our call

    if (bool_pr[UDPSPOTS_BPR].state)
        return (true);

    if (strcistr (s.rx_call, cs_info.call) != NULL)
        return (true);
    else {
        Serial.printf("DXC: UDP: rejecting tx_call %s because rx_call %s != %s\n",
                                    s.tx_call, s.rx_call, cs_info.call);
        return (false);
    }
}

/* return whether to upgrade automatically when available and pass back the local hour to do so.
 */
bool autoUpgrade (int &local_hour)
{
    const char *label = getEntangledValue (AUTOUPA_BPR, AUTOUPB_BPR);
    for (int i = 0; i < AUP_N; i++) {
        if (strcmp (label, autoup_tbl[i].label) == 0) {
            local_hour = autoup_tbl[i].local_hour;
            return (i != AUP_OFF);
        }
    }
    fatalError ("unknown auto up: %s", label);
    return (false);  // lint
}

/* return longest allow TLE age in days
 */
int maxTLEAgeDays(void)
{
    return (atoi (getEntangledValue (MAXTLEA_BPR, MAXTLEB_BPR)));
}

/* return minimum distance to label spots in great circle radians
 */
float minLabelDist(void)
{
    int mld_linear = atoi (getEntangledValue (LBLDISTA_BPR, LBLDISTB_BPR));
    float mld_angle = mld_linear / (showDistKm() ? (ERAD_M*KM_PER_MI): ERAD_M);
    return (mld_angle);
}

