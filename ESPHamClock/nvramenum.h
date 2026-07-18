#ifndef _NVRAMENUM_H
#define _NVRAMENUM_H




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
	NV_ANT_DE_INDEX,            // Antenna index for TX
	NV_ANT_DX_INDEX,            // Antenna index for RX
	NV_ANT_DEDX_CONTROL,        // Antenna selection b0 DE b1 DX
	NV_ANT_DE_AZ,               // Antenna Azimuth for DE

	// 245
	NV_ANT_DX_AZ,               // Antenna Azimuth for DX

    // 246
    NV_LIGHTNING_ON,            // whether to show lightning strikes overlay

    // 247
    NV_LTG_WORLDWIDE,           // lightning: 1=worldwide, 0=radius
    NV_LTG_RADIUS,              // lightning: search radius in km

    // 249
    NV_STORMS_ON,               // whether to show storm overlay and pane

    // 250
    NV_HAMSATKEY,                // hams.at API key

    // 251
    NV_SATGROUP,                 // newline-separated satellite names for group schedule

    // 252
    NV_SHOWPLANETS,               // whether to show planets on the map

    // 253 -- retain the experimental marker so already-patched EEPROM files remain compatible
    NV_PLOTSCHEMA,                 // reserved plot settings schema marker

    // 254 -- appended so all prior EEPROM addresses remain unchanged
    NV_PANE0ROTSET_HI,            // high 32 bits of pane 0 PlotChoice rotation mask
    NV_PANE1ROTSET_HI,            // high 32 bits of pane 1 PlotChoice rotation mask
    NV_PANE2ROTSET_HI,            // high 32 bits of pane 2 PlotChoice rotation mask
    NV_PANE3ROTSET_HI,            // high 32 bits of pane 3 PlotChoice rotation mask

	NV_N

} NV_Name;

#endif // _NVRAMENUM_H