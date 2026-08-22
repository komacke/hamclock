#ifndef _NVRAMLEN_H
#define _NVRAMLEN_H


#define MAID_CHARLEN     7


#define NV_CALLSIGN_LEN         12      // max call sign, including EOS


// nvram

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
#define NV_HAMSATKEY_LEN        64
#define NV_SATGROUP_LEN         240
#define NV_MESHWATCHLIST_LEN    120
#define NV_APRSHOST_LEN          26
#define NV_HAMALERT_PASSWD_LEN  32      // HamAlert.org telnet password (separate from web password)
#define NV_HAMALERT_LOGIN_LEN   64      // HamAlert.org telnet login -- call sign or, per HamAlert's
                                         // own account system, sometimes an email address

#endif // _NVRAMLEN_H