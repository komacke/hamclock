#ifndef _NVRAMSIZE_H
#define _NVRAMSIZE_H

/* number of bytes for each NV_Name.
 * N.B. must be in the same order.
 * Format of size lines must be contain the following columns
 *   size,
 *   //
 *   NV_xxx
 * sizes that are not numbers must be of the form NV_xxx_LEN and be defined in nvramlen.h
 */
static const uint8_t nv_sizes[NV_N] = {

    // 0
    4,                          // NV_TOUCH_CAL_A
    4,                          // NV_TOUCH_CAL_B
    4,                          // NV_TOUCH_CAL_C
    4,                          // NV_TOUCH_CAL_D
    4,                          // NV_TOUCH_CAL_E

    // 5
    4,                          // NV_TOUCH_CAL_F
    4,                          // NV_TOUCH_CAL_DIV
    1,                          // NV_DXMAX_N
    1,                          // NV_DE_TIMEFMT
    4,                          // NV_DE_LAT

    // 10
    4,                          // NV_DE_LNG
    4,                          // NV_PANE0ROTSET
    1,                          // NV_PLOT_0
    4,                          // NV_DX_LAT
    4,                          // NV_DX_LNG

    // 15
    4,                          // NV_DX_GRID_OLD
    2,                          // NV_CALL_FG
    2,                          // NV_CALL_BG
    1,                          // NV_CALL_RAINBOW
    1,                          // NV_PSK_SHOWDIST

    // 20
    4,                          // NV_UTC_OFFSET
    1,                          // NV_PLOT_1
    1,                          // NV_PLOT_2
    1,                          // NV_BRB_ROTSET_OLD
    1,                          // NV_PLOT_3

    // 25
    1,                          // NV_RSS_ON
    2,                          // NV_BPWM_DIM
    2,                          // NV_PHOT_DIM
    2,                          // NV_BPWM_BRIGHT
    2,                          // NV_PHOT_BRIGHT

    // 30
    1,                          // NV_LP
    1,                          // NV_UNITS
    1,                          // NV_LKSCRN_ON
    1,                          // NV_MAPPROJ
    1,                          // NV_ROTATE_SCRN_OLD

    // 35
    NV_WIFI_SSID_LEN,           // NV_WIFI_SSID
    NV_WIFI_PW_OLD_LEN,         // NV_WIFI_PASSWD_OLD
    NV_CALLSIGN_LEN,            // NV_CALLSIGN
    NV_SATNAME_LEN,             // NV_SAT1NAME
    1,                          // NV_DE_SRSS

    // 40
    1,                          // NV_DX_SRSS
    1,                          // NV_GRIDSTYLE
    2,                          // NV_DPYON_OLD
    2,                          // NV_DPYOFF_OLD
    NV_DXHOST_LEN,              // NV_DXHOST

    // 45
    2,                          // NV_DXPORT
    1,                          // NV_SWHUE
    4,                          // NV_TEMPCORR76
    NV_GPSDHOST_OLD_LEN,        // NV_GPSDHOST_OLD
    4,                          // NV_KX3BAUD

    // 50
    2,                          // NV_BCPOWER
    4,                          // NV_CD_PERIOD
    4,                          // NV_PRESCORR76
    2,                          // NV_BR_IDLE
    1,                          // NV_BR_MIN

    // 55
    1,                          // NV_BR_MAX
    4,                          // NV_DE_TZ
    4,                          // NV_DX_TZ
    NV_COREMAPSTYLE_LEN,        // NV_COREMAPSTYLE
    1,                          // NV_USEDXCLUSTER

    // 60
    1,                          // NV_USEGPSD
    1,                          // NV_LOGUSAGE
    1,                          // NV_LBLSTYLE
    NV_WIFI_PW_LEN,             // NV_WIFI_PASSWD
    1,                          // NV_NTPSET

    // 65
    NV_NTPHOST_OLD_LEN,         // NV_NTPHOST_OLD
    1,                          // NV_GPIOOK
    2,                          // NV_SAT1COLOR
    2,                          // NV_SAT2COLOR
    2,                          // NV_X11FLAGS

    // 70
    2,                          // NV_BCFLAGS
    NV_DAILYONOFF_LEN,          // NV_DAILYONOFF
    4,                          // NV_TEMPCORR77
    4,                          // NV_PRESCORR77
    2,                          // NV_SHORTPATHCOLOR

    // 75
    2,                          // NV_LONGPATHCOLOR
    2,                          // NV_PLOTOPS_OLD
    1,                          // NV_NIGHT_ON
    NV_DE_GRID_LEN,             // NV_DE_GRID
    NV_DX_GRID_LEN,             // NV_DX_GRID

    // 80
    2,                          // NV_GRIDCOLOR
    2,                          // NV_CENTERLNG
    1,                          // NV_NAMES_ON
    4,                          // NV_PANE1ROTSET
    4,                          // NV_PANE2ROTSET

    // 85
    4,                          // NV_PANE3ROTSET
    1,                          // NV_AUX_TIME
    2,                          // NV_DAILYALARM
    1,                          // NV_BC_UTCTIMELINE
    1,                          // NV_RSS_INTERVAL

    // 90
    1,                          // NV_DATEMDY
    1,                          // NV_DATEDMYYMD
    1,                          // NV_ROTUSE
    NV_ROTHOST_LEN,             // NV_ROTHOST
    2,                          // NV_ROTPORT

    // 95
    1,                          // NV_RIGUSE
    NV_RIGHOST_LEN,             // NV_RIGHOST
    2,                          // NV_RIGPORT
    NV_DXLOGIN_LEN,             // NV_DXLOGIN
    1,                          // NV_FLRIGUSE

    // 100
    NV_FLRIGHOST_LEN,           // NV_FLRIGHOST
    2,                          // NV_FLRIGPORT
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD0_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD1_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD2_OLD

    // 105
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD3_OLD
    1,                          // NV_DXCMDUSED_OLD
    1,                          // NV_PSK_MODEBITS
    4,                          // NV_PSK_BANDS
    2,                          // NV_160M_COLOR

    // 110
    2,                          // NV_80M_COLOR
    2,                          // NV_60M_COLOR
    2,                          // NV_40M_COLOR
    2,                          // NV_30M_COLOR
    2,                          // NV_20M_COLOR

    // 115
    2,                          // NV_17M_COLOR
    2,                          // NV_15M_COLOR
    2,                          // NV_12M_COLOR
    2,                          // NV_10M_COLOR
    2,                          // NV_6M_COLOR

    // 120
    2,                          // NV_2M_COLOR
    4,                          // NV_CSELDASHED
    1,                          // NV_BEAR_MAG
    1,                          // NV_WSJT_SETSDX_OLD
    1,                          // NV_WSJT_DX

    // 125
    2,                          // NV_PSK_MAXAGE
    1,                          // NV_WEEKMON
    1,                          // NV_BCMODE
    1,                          // NV_SDO
    1,                          // NV_SDOROT

    // 130
    1,                          // NV_ONTASPOTA_OLD
    1,                          // NV_ONTASSOTA_OLD
    2,                          // NV_BRB_ROTSET
    2,                          // NV_ROTCOLOR
    1,                          // NV_CONTESTS

    // 135
    4,                          // NV_BCTOA
    NV_ADIFFN_OLD_LEN,          // NV_ADIFFN_OLD
    NV_I2CFN_LEN,               // NV_I2CFN
    1,                          // NV_I2CON
    4,                          // NV_DXMAX_T

    // 140
    NV_POTAWLIST1_OLD_LEN,      // NV_POTAWLIST1_OLD
    1,                          // NV_SCROLLDIR
    1,                          // NV_SCROLLLEN_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD4_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD5_OLD

    // 145
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD6_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD7_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD8_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD9_OLD
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD10_OLD

    // 150
    NV_DXCLCMD_OLD_LEN,         // NV_DXCMD11_OLD
    2,                          // NV_DXCMDMASK
    1,                          // NV_DXWLISTMASK
    1,                          // NV_RANKSW_OLD
    1,                          // NV_NEWDXDEWX

    // 155
    1,                          // NV_WEBFS
    1,                          // NV_ZOOM
    2,                          // NV_PANX
    2,                          // NV_PANY
    1,                          // NV_POTAWLISTMASK_OLD

    // 160
    NV_SOTAWLIST1_OLD_LEN,      // NV_SOTAWLIST1_OLD
    4,                          // NV_ONCEALARM
    1,                          // NV_ONCEALARMMASK
    1,                          // NV_PANEROTP
    1,                          // NV_SHOWPIP

    // 165
    1,                          // NV_MAPROTP
    2,                          // NV_MAPROTSET
    1,                          // NV_GRAYDPY
    1,                          // NV_SOTAWLISTMASK_OLD
    1,                          // NV_ADIFWLISTMASK

    // 170
    NV_DXWLIST_LEN,             // NV_DXWLIST
    NV_ADIFWLIST_LEN,           // NV_ADIFWLIST
    1,                          // NV_ADIFSORT
    4,                          // NV_ADIFBANDS_OLD
    NV_POTAWLIST_OLD_LEN,       // NV_POTAWLIST_OLD

    // 175
    NV_SOTAWLIST_OLD_LEN,       // NV_SOTAWLIST_OLD
    NV_ADIFFN_LEN,              // NV_ADIFFN
    NV_NTPHOST_LEN,             // NV_NTPHOST
    NV_GPSDHOST_LEN,            // NV_GPSDHOST
    NV_NMEAFILE_LEN,            // NV_NMEAFILE

    // 180
    1,                          // NV_USENMEA
    2,                          // NV_NMEABAUD
    1,                          // NV_BCTOABAND
    1,                          // NV_BCRELBAND
    1,                          // NV_AUTOMAP

    // 185
    1,                          // NV_DXCAGE
    2,                          // NV_ONAIR_FG
    2,                          // NV_ONAIR_BG
    1,                          // NV_ONAIR_RAINBOW
    NV_DXCLCMD_LEN,             // NV_DXCMD0

    // 190
    NV_DXCLCMD_LEN,             // NV_DXCMD1
    NV_DXCLCMD_LEN,             // NV_DXCMD2
    NV_DXCLCMD_LEN,             // NV_DXCMD3
    NV_DXCLCMD_LEN,             // NV_DXCMD4
    NV_DXCLCMD_LEN,             // NV_DXCMD5

    // 195
    NV_DXCLCMD_LEN,             // NV_DXCMD6
    NV_DXCLCMD_LEN,             // NV_DXCMD7
    NV_DXCLCMD_LEN,             // NV_DXCMD8
    NV_DXCLCMD_LEN,             // NV_DXCMD9
    NV_DXCLCMD_LEN,             // NV_DXCMD10

    // 200
    NV_DXCLCMD_LEN,             // NV_DXCMD11
    NV_ONAIR_LEN,               // NV_ONAIR_MSG
    1,                          // NV_SETRADIO
    NV_ONTAWLIST_LEN,           // NV_ONTAWLIST
    1,                          // NV_ONTAWLISTMASK

    // 205
    1,                          // NV_ONTASORTBY
    NV_ONTAORG_LEN,             // NV_ONTAORG
    NV_TITLE_LEN,               // NV_TITLE
    2,                          // NV_TITLE_FG
    2,                          // NV_TITLE_BG

    // 210
    1,                          // NV_TITLE_RAINBOW
    1,                          // NV_ONTA_MAXAGE
    1,                          // NV_QRZID
    1,                          // NV_CALLPREF
    4,                          // NV_CSELONOFF

    // 215
    4,                          // NV_CSELTHIN
    4,                          // NV_CSELDASHED_A
    4,                          // NV_CSELONOFF_A
    4,                          // NV_CSELTHIN_A
    4,                          // NV_CSELDASHED_B

    // 220
    4,                          // NV_CSELONOFF_B
    4,                          // NV_CSELTHIN_B
    1,                          // NV_ONTABIO
    1,                          // NV_DXCBIO
    2,                          // NV_X11GEOM_X

    // 225
    2,                          // NV_X11GEOM_Y
    2,                          // NV_X11GEOM_W
    2,                          // NV_X11GEOM_H
    2,                          // NV_DEWXCHOICE
    2,                          // NV_DXWXCHOICE

    // 230
    1,                          // NV_UDPSETSDX
    4,                          // NV_SPCWXCHOICE
    1,                          // NV_DXPEDS
    1,                          // NV_UDPSPOTS
    1,                          // NV_AUTOUPGRADE

    // 235
    1,                          // NV_MAXTLEAGE
    2,                          // NV_MINLBLDIST
    NV_SATNAME_LEN,             // NV_SAT2NAME
    1,                          // NV_PSK_SHOWPATH
    1,                          // NV_SAT1FLAGS

    // 240
    1,                          // NV_SAT2FLAGS
    2,                          // NV_ANT_DE_INDEX
    2,                          // NV_ANT_DX_INDEX
    1,                          // NV_ANT_DEDX_CONTROL
    4,                          // NV_ANT_DE_AZ

    // 245
    4,                          // NV_ANT_DX_AZ

    // 246
    1,                          // NV_LIGHTNING_ON

    // 247
    1,                          // NV_LTG_WORLDWIDE
    2,                          // NV_LTG_RADIUS

    // 249
    1,                          // NV_STORMS_ON

    // 250
    NV_HAMSATKEY_LEN,           // NV_HAMSATKEY

    // 251
    NV_SATGROUP_LEN,            // NV_SATGROUP

    // 252
    1,                          // NV_SHOWPLANETS

    // 253 -- retained compatibility marker
    1,                          // NV_PLOTSCHEMA

    // 254 -- high words are appended; no existing item moves
    4,                          // NV_PANE0ROTSET_HI
    4,                          // NV_PANE1ROTSET_HI
    4,                          // NV_PANE2ROTSET_HI
    4,                          // NV_PANE3ROTSET_HI

    // 255
    NV_MESHWATCHLIST_LEN,       // NV_MESHWATCHLIST

    // 256
    NV_PIAWAREHOST_LEN,         // NV_PIAWAREHOST

    // 257
    1,                          // NV_ONTA_MODES
    4,                          // NV_ONTA_BANDS

    // 258 
    1,                          // NV_CTYBORDERS_ON

    // 259
    2,                          // NV_BORDERCOLOR
    2,                          // NV_STATECOLOR

    // 260
    1,                          // NV_WEFAX_REGION
    1,                          // NV_WEFAX_PRODUCT
    1,                          // NV_WEFAX_ENABLE

    // 261
    1,                          // NV_STORM_SHOWPATH

    // 262
    1,                          // NV_MAINCLOCK_LOCAL

    // 263
    1,                          // NV_DXC_HIDEIOTA
    1,                          // NV_DXC_MODES
    4,                          // NV_DXC_BANDS

};

#endif // _NVRAMSIZE_H
