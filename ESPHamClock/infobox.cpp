/* handle the map info box
 */

#include "HamClock.h"

#define IB_LINEDY       9                       // line height, pixels
#define IB_NLINES       12                      // allow this many lines in box
#define IB_MAXCHARS     9                       // max chars wide
#define IB_INDENT       2                       // nominal indentation
#define CDOT_R          4                       // city dot radius


/* drawMouseLoc() helper to show age in nice units.
 * update ty by dy for each row used.
 */
static void drawIB_Age (time_t t, uint16_t tx, int dy, uint16_t &ty)
{
    // get age in seconds but never negative
    time_t n = myNow();
    time_t age_s = n > t ? n - t : 0;

    // show in nice units
    char str[10];
    tft.setCursor (tx, ty += dy);
    tft.printf ("Age  %s", formatAge (age_s, str, sizeof(str), 4));
}

/* drawMouseLoc() helper to show DE distance and bearing to given location.
 * update ty by dy for each row used.
 */
static void drawIB_DB (const LatLong &ll, uint16_t tx, int dy, uint16_t &ty)
{
    // get distance and bearing to spot location
    float dist, bearing;
    propDEPath (show_lp, ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (showDistKm())
        dist *= KM_PER_MI;

    // get bearing from DE in desired units
    bool bearing_ismag = desiredBearing (de_ll, bearing);

    // show direction
    tft.setCursor (tx, ty += dy);
    tft.printf ("%s %5.0f", show_lp ? "LP" : "SP", bearing);
    if (bearing_ismag) {
        tft.setCursor(tft.getCursorX()+2, ty-2); 
        tft.print ('M');
    } else {
        tft.drawCircle (tft.getCursorX()+2, ty+1, 1, RA8875_WHITE);         // home-made degree
    }

    // show distance
    tft.setCursor (tx, ty += dy);
    tft.printf ("%6.0f %s", dist, showDistKm() ? "km" : "mi");
}

/* drawMouseLoc() helper to show weather at the given location,
 * update ty by dy for each row used.
 */
static void drawIB_WX (const LatLong &ll, uint16_t tx, int dy, int max_ty, uint16_t &ty)
{
    WXInfo wi;
    if (ty <= max_ty && getFastWx (ll, wi)) {

        // previous could call updateClocks which changes font!
        selectFontStyle (LIGHT_FONT, FAST_FONT);

        // temperature in desired units
        float tmp = showTempC() ? wi.temperature_c : CEN2FAH(wi.temperature_c);
        tft.setCursor (tx, ty += dy);
        tft.printf ("Temp%4.0f%c", tmp, showTempC() ? 'C' : 'F');

        // conditions else wind if room
        if (ty <= max_ty) {
            int clen = strlen(wi.conditions);
            if (clen > 0) {
                tft.setCursor (tx, ty += dy);
                if (clen > IB_MAXCHARS)
                    clen = IB_MAXCHARS;
                tft.printf ("%*s%.*s", (IB_MAXCHARS-clen)/2, "", IB_MAXCHARS, wi.conditions);
            } else {
                float spd = (showDistKm() ? 3.6F : 2.237F) * wi.wind_speed_mps; // kph or mph
                char wbuf[30];
                snprintf (wbuf, sizeof(wbuf), "%s@%.0f", wi.wind_dir_name, spd);
                tft.setCursor (tx, ty += dy);
                tft.printf ("Wnd%6s", wbuf);
            }
        }
    }
}

/* drawMouseLoc() helper to show local mean time.
 * update ty by dy for each row used.
 */
static void drawIB_LMT (const LatLong &ll, uint16_t tx, int dy, uint16_t &ty)
{
    time_t t = myNow() + getFastTZ(ll);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (tx, ty += dy);
    tft.printf ("LMT %02d:%02d", hour(t), minute(t));
}

/* drawMouseLoc() helper to show frequency.
 * update ty by dy for each row used.
 */
static void drawIB_Freq (long hz, uint16_t tx, int dy, uint16_t &ty)
{
    tft.setCursor (tx, ty += dy);
    if (hz < 99999000)
        tft.printf ("f %7.1f", hz * 1e-3);
    else
        tft.printf ("f %7.0f", hz * 1e-3);
}

/* drawMouseLoc() helper to show mode, if known.
 * update ty by dy for each row used.
 */
static bool drawIB_Mode (const char *mode, uint16_t tx, int dy, uint16_t &ty)
{
    if (mode && strlen(mode) > 0) {
        char buf[IB_MAXCHARS+1];
        snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, mode);
        uint16_t tw = getTextWidth(buf);
        tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += dy);
        tft.printf (buf);
        return (true);
    } else
        return (false);
}

/* drawMouseLoc() helper for looking up city.
 * if city is wanted, found and over map: change ll, ms and names_w IN PLACE and draw dot and name
 * N.B. we assume the city name background is already erased
 */
static bool drawIB_City (LatLong &ll, SCoord &ms, const SBox &info_b, uint16_t &names_w, uint16_t names_y)
{
    // skip if not wanted
    if (!names_on)
        return (false);

    // get city info if one is near
    LatLong city_ll;
    SCoord city_ms;
    int max_cl;
    const char *city = getNearestCity (ll, city_ll, &max_cl);
    if (city) {
        // update caller unless not over map
        ll2s (city_ll, city_ms, CDOT_R);
        if (overMap (city_ms)) {
            // still have to check whether over the info box
            uint16_t min_x = info_b.x + info_b.w + CDOT_R;
            uint16_t min_y = info_b.y + info_b.h + CDOT_R;
            if (city_ms.x > min_x || city_ms.y > min_y) {
                // ok! pass back new location and max name width
                ll = city_ll;
                ms = city_ms;
                names_w = max_cl * 6;           // * font width
                // dot
                tft.fillCircle (city_ms.x, city_ms.y, CDOT_R, RA8875_RED);
                // name, assuming background is already erased
                uint16_t c_w = getTextWidth (city);
                tft.setCursor (map_b.x + (map_b.w-c_w)/2, names_y + 3);
                tft.print(city);
            } else {
                city = NULL;
            }
        }
    }

    return (city != NULL);
}

/* drawMouseLoc() helper to show map at the given ll, taking care to stay within map and out us.
 * option to ignore caring about minfo used for targets that do not have menus, eg, sun and moon.
 */
static void drawIB_MapMarker (const LatLong &ll, const SBox &minfo_b, bool ignore_minfo)
{
    // define mark geometry
    #define MAPMARK_R 11                            // canonical outer radius, circles grow inward
    static uint16_t marker_colors[] = {             // outer to inner radii
        RA8875_RED, RA8875_RED, RA8875_BLACK, RA8875_BLACK, RA8875_RED, RA8875_RED
    };
    #define N_MAPMARK NARRAY(marker_colors)

    // get canonical screen loc to check whether to draw at all
    SCoord mark_s;
    ll2s (ll, mark_s, MAPMARK_R);

    // show iff over map and does not overlap with info box
    if (overMap (mark_s) && (ignore_minfo || mark_s.x > minfo_b.x + minfo_b.w + MAPMARK_R
                          || mark_s.y > minfo_b.y + minfo_b.h + MAPMARK_R)) {

        if (tft.SCALESZ == 1) {
            for (int i = 0; i < N_MAPMARK; i++)
                tft.drawCircle (mark_s.x, mark_s.y, MAPMARK_R-i, marker_colors[i]);
        } else {
            // use full res to exactly match how spots are drawn
            SCoord mark_s_raw;
            ll2sRaw (ll, mark_s_raw, MAPMARK_R*tft.SCALESZ);
            for (int i = 0; i < N_MAPMARK; i++) {
                uint16_t raw_r = (MAPMARK_R - i)*tft.SCALESZ;
                tft.drawCircleRaw (mark_s_raw.x, mark_s_raw.y, raw_r, tft.SCALESZ, marker_colors[i]);
            }
        }
    }
}

/* draw info for PSK, WSPR or RBN spot
 */
static void drawIB_PSK (const SBox &minfo_b, const DXSpot &dx_s, const LatLong &dxc_ll)
{
    char buf[IB_MAXCHARS+1];
    uint16_t tx = minfo_b.x;
    uint16_t ty = minfo_b.y + 2;
    uint16_t tw;

    // show tx info
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.tx_call);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty);
    tft.printf (buf);
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.tx_grid);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show rx info
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.rx_call);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.rx_grid);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show mode
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.mode);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show freq
    drawIB_Freq (1000*dx_s.kHz, tx+IB_INDENT, IB_LINEDY, ty);

    // show age
    drawIB_Age (dx_s.spotted, tx+IB_INDENT, IB_LINEDY, ty);

    // show snr
    tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
    tft.printf ("SNR %5.0f", dx_s.snr);

    // show distance and bearing
    drawIB_DB (dxc_ll, tx+IB_INDENT, IB_LINEDY, ty);

    // show weather
    drawIB_WX (dxc_ll, tx+IB_INDENT, IB_LINEDY, minfo_b.y+minfo_b.h-IB_LINEDY, ty);

    // border in band color
    drawSBox (minfo_b, getBandColor(dx_s.kHz));
}

/* draw info regarding a DXPed
 */
static void drawIB_DXPed (const SBox &minfo_b, const DXPedEntry *dxp, const SCoord &ms)
{
    char buf[IB_MAXCHARS+1];
    uint16_t tx = minfo_b.x;
    uint16_t ty = minfo_b.y + 2;
    uint16_t tw;
    tft.setTextColor (RA8875_WHITE);

    // show expedition call on first row
    tw = getTextWidth(dxp->call);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty);
    tft.printf (dxp->call);

    // show expedition DXCC on second row
    snprintf (buf, sizeof(buf), "DXCC %4d", dxp->dxcc);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show frequency and mode if currently on the cluster list
    const DXSpot *spotted = findDXCCall (dxp->call);
    if (spotted) {
        drawIB_Freq (spotted->kHz*1000, tx+IB_INDENT, IB_LINEDY, ty);
        drawIB_Mode (spotted->mode, tx+IB_INDENT, IB_LINEDY, ty);
    }

    // then either worked ADIF entries or normal stuff
    DXPedsWorked *worked;
    int n_worked = findDXPedsWorked (dxp, worked);
    if (n_worked > 0) {

        // format band-mode to fit nicely in MAXCHARS
        typedef char BandModeStr_t[IB_MAXCHARS+1];          // + 1 for EOS
        StackMalloc bm_mem(n_worked * sizeof(BandModeStr_t));
        BandModeStr_t *bm = (BandModeStr_t *) bm_mem.getMem();
        for (int i = 0; i < n_worked; i++) {
            const char *band_name = findBandName (worked[i].hb);
            int gap = IB_MAXCHARS - 1 - strlen(band_name) - strlen(worked[i].mode);         // -1 for 'm'
            snprintf (bm[i], IB_MAXCHARS+1, "%sm%*s%s", band_name, gap, "", worked[i].mode);
        }

        // show each band+mode, but set last line to "..." if they don't all fit
        for (int i = 0; i < n_worked; i++) {
            bool full = (ty-minfo_b.y)/IB_LINEDY == IB_NLINES-2;
            const char *w = full ? "..." : bm[i];
            tw = getTextWidth(w);
            tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
            tft.printf (w);
            if (full)
                break;
        }

        // clean up
        free ((void*)worked);

    } else {

        // just show misc stuff

        // show lat/long
        tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
        tft.printf ("Lat %4.0f%c", fabsf(dxp->ll.lat_d), dxp->ll.lat_d < 0 ? 'S' : 'N');
        tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
        tft.printf ("Lng %4.0f%c", fabsf(dxp->ll.lng_d), dxp->ll.lng_d < 0 ? 'W' : 'E');

        // show maid
        char maid[MAID_CHARLEN];
        ll2maidenhead (maid, dxp->ll);
        tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
        tft.printf ("Grid %4.4s", maid);

        // cq zone
        int cqzone_n = 0;
        if (findZoneNumber (ZONE_CQ, ms, &cqzone_n)) {
            tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
            tft.printf ("CQ  %5d", cqzone_n);
        }

        // itu zone
        int ituzone_n = 0;
        if (findZoneNumber (ZONE_ITU, ms, &ituzone_n)) {
            tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
            tft.printf ("ITU %5d", ituzone_n);
        }

        // show local time
        drawIB_LMT (dxp->ll, tx+IB_INDENT, IB_LINEDY, ty);

        // show distance and bearing
        drawIB_DB (dxp->ll, tx+IB_INDENT, IB_LINEDY, ty);

        // show weather
        drawIB_WX (dxp->ll, tx+IB_INDENT, IB_LINEDY, minfo_b.y+minfo_b.h-IB_LINEDY, ty);
    }

    // border
    drawSBox (minfo_b, RA8875_BLACK);
}

/* draw info for DX Cluster or POTA/SOTA or ADIF spot
 */
static void drawIB_DX (const SBox &minfo_b, const DXSpot &dx_s, const LatLong &dxc_ll)
{
    char buf[IB_MAXCHARS+1];
    uint16_t tx = minfo_b.x;
    uint16_t ty = minfo_b.y + 2;
    uint16_t tw;

    // show tx info
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.tx_call);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty);
    tft.printf (buf);
    snprintf (buf, sizeof(buf), "%.*s", 4, dx_s.tx_grid);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show rx info
    snprintf (buf, sizeof(buf), "%.*s", IB_MAXCHARS, dx_s.rx_call);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);
    snprintf (buf, sizeof(buf), "%.*s", 4, dx_s.rx_grid);
    tw = getTextWidth(buf);
    tft.setCursor (tx + (view_btn_b.w-tw)/2, ty += IB_LINEDY);
    tft.printf (buf);

    // show mode if known
    if (!drawIB_Mode (dx_s.mode, tx+IB_INDENT, IB_LINEDY, ty))
        ty += IB_LINEDY;

    // show freq
    drawIB_Freq (dx_s.kHz*1000, tx+IB_INDENT, IB_LINEDY, ty);

    // show spot age
    drawIB_Age (dx_s.spotted, tx+IB_INDENT, IB_LINEDY, ty);

    // show local time
    drawIB_LMT (dxc_ll, tx+IB_INDENT, IB_LINEDY, ty);

    // show distance and bearing
    drawIB_DB (dxc_ll, tx+IB_INDENT, IB_LINEDY, ty);

    // show weather
    drawIB_WX (dxc_ll, tx+IB_INDENT, IB_LINEDY, minfo_b.y+minfo_b.h-IB_LINEDY, ty);

    // border in band color
    drawSBox (minfo_b, getBandColor(dx_s.kHz));
}

/* draw info box for an arbitrary cursor location, not a spot
 */
static void drawIB_Loc (const SBox &minfo_b, const LatLong &ll, const SCoord &ms)
{
    uint16_t tx = minfo_b.x;
    uint16_t ty = minfo_b.y + 2;

    // show lat/long
    tft.setCursor (tx+IB_INDENT, ty);
    tft.printf ("Lat %4.0f%c", fabsf(ll.lat_d), ll.lat_d < 0 ? 'S' : 'N');
    tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
    tft.printf ("Lng %4.0f%c", fabsf(ll.lng_d), ll.lng_d < 0 ? 'W' : 'E');

    // show maid
    char maid[MAID_CHARLEN];
    ll2maidenhead (maid, ll);
    tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
    tft.printf ("Grid %4.4s", maid);

    // cq zone
    int cqzone_n = 0;
    if (findZoneNumber (ZONE_CQ, ms, &cqzone_n)) {
        tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
        tft.printf ("CQ  %5d", cqzone_n);
    }

    // itu zone
    int ituzone_n = 0;
    if (findZoneNumber (ZONE_ITU, ms, &ituzone_n)) {
        tft.setCursor (tx+IB_INDENT, ty += IB_LINEDY);
        tft.printf ("ITU %5d", ituzone_n);
    }

    // show local time
    drawIB_LMT (ll, tx+IB_INDENT, IB_LINEDY, ty);

    // prefix
    char prefix[MAX_PREF_LEN];
    ty += IB_LINEDY;
    if (ll2Prefix (ll, prefix)) {
        tft.setCursor (tx+IB_INDENT, ty);
        tft.printf ("Pfx %5.5s", prefix);
    }

    // gap
    ty += IB_LINEDY;

    // show distance and bearing
    drawIB_DB (ll, tx+IB_INDENT, IB_LINEDY, ty);

    // show weather
    drawIB_WX (ll, tx+IB_INDENT, IB_LINEDY, minfo_b.y+minfo_b.h-IB_LINEDY, ty);

    // border
    drawSBox (minfo_b, RA8875_WHITE);
}

/* draw local information about the current cursor position over the world map or pane spot.
 * called after every map draw so we only have to erase parts of azm outside the hemispheres.
 */
void drawInfoBox()
{
    // position box just below map View button which itself moves depending whenther showing maindenhead grid
    uint16_t tx = view_btn_b.x;                         // current text x coord
    uint16_t ty = view_btn_b.y + view_btn_b.h;          // initial then walking text y coord
    SBox minfo_b = {tx, ty, (uint16_t)(view_btn_b.w-1), IB_LINEDY*IB_NLINES+2};
    ty += 2;                                            // top border

    // then set size and location of the city names bar at same y
    const uint16_t names_y = view_btn_b.y;
    const uint16_t names_h = 14;
    static uint16_t names_w = 500;                      // might be updated via getNearestCity()
    uint16_t names_x = map_b.x + (map_b.w-names_w)/2;   // centered also "

    // persistent flag whether city was draw last time
    static bool was_city;

    // find what needs to be shown, if any
    SCoord ms;                                          // mouse loc but may change to city loc
    LatLong ll;                                         // ll at ms
    DXSpot dx_s;                                        // spot info
    LatLong dxc_ll;                                     // ll to mark
    DXPedEntry *dxp = NULL;                             // set if over
    PlotPane pp;                                        // set if over SDO or Moon pane

    bool over_app = tft.getMouse (&ms.x, &ms.y);
    bool over_map = over_app && s2ll (ms, ll);
    bool over_psk = over_map && getClosestPSK (ll, &dx_s, &dxc_ll);             // only one that sets snr
    bool over_dxped = (over_map && getClosestDXPed (ll, dxp)) || (!over_map && getPaneDXPed (ms, dxp));
    bool over_spot = over_map && !over_psk && !over_dxped &&
                                (getClosestDXCluster (ll, &dx_s, &dxc_ll)
                                    || getClosestOnTheAirSpot (ll, &dx_s, &dxc_ll)
                                    || getClosestADIFSpot (ll, &dx_s, &dxc_ll)
                                );
    bool over_pane = over_app && !over_map && !over_dxped &&
                                (getDXCPaneSpot (ms, &dx_s, &dxc_ll)
                                    || getMaxDistPSK (ms, &dx_s, &dxc_ll)
                                    || getOnTheAirPaneSpot (ms, &dx_s, &dxc_ll)
                                    || getADIFPaneSpot (ms, &dx_s, &dxc_ll)
                                );
    bool over_sdo = over_app && !over_map && (pp = findPaneChoiceNow(PLOT_CH_SDO)) != PANE_NONE
                        && inBox (ms, plot_b[pp]);
    bool over_moon = over_app && !over_map && (pp = findPaneChoiceNow(PLOT_CH_MOON)) != PANE_NONE
                        && inBox (ms, plot_b[pp]);
    bool draw_info = over_map || over_psk || over_spot || over_pane || over_dxped;

    // erase any previous city then reset was_city as flag for next time
    if (was_city) {
        tft.fillRect (names_x, names_y, names_w, names_h, RA8875_BLACK);
        was_city = false;
    }

    // highlight the zone containing the cursor if appropriate
    if (draw_info && over_map) {
        int cqzone_n = 0, ituzone_n = 0;
        if (mapgrid_choice == MAPGRID_CQZONES && findZoneNumber (ZONE_CQ, ms, &cqzone_n))
            drawZone (ZONE_CQ, EARTH_GRIDC00, cqzone_n);
        if (mapgrid_choice == MAPGRID_ITUZONES && findZoneNumber (ZONE_ITU, ms, &ituzone_n))
            drawZone (ZONE_ITU, EARTH_GRIDC00, ituzone_n);
    }

    // erase menu area if going to show new data or clean up edges when not mercator
    static bool drew_menu;
    if (draw_info || (map_proj != MAPP_MERCATOR && drew_menu))
        fillSBox (minfo_b, RA8875_BLACK);
    drew_menu = draw_info;

    // mark map spot if any
    if (over_sdo)
        drawIB_MapMarker (sun_ss_ll, minfo_b, true);
    else if (over_moon)
        drawIB_MapMarker (moon_ss_ll, minfo_b, true);
    else if (over_psk || over_spot || over_pane)
        drawIB_MapMarker (dxc_ll, minfo_b, false);
    else if (dxp)
        drawIB_MapMarker (dxp->ll, minfo_b, true);

    // that's all folks if no menu
    if (!draw_info)
        return;



    // draw spot info in info table, if any.
    // N.B. show city dot and name only if no spot to avoid appearance of fake association.

    // prep for text
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // draw, depending on type
    if (over_psk)
        drawIB_PSK (minfo_b, dx_s, dxc_ll);

    else if (dxp)
        drawIB_DXPed (minfo_b, dxp, ms);

    else if (over_spot || over_pane)
        drawIB_DX (minfo_b, dx_s, dxc_ll);

    else if (over_map) {

        // storm hover wins over city name when cursor is close to a current storm position
        const char *storm_label = getStormHoverLabel (ll);
        if (storm_label) {
            tft.fillCircle (ms.x, ms.y, CDOT_R, RA8875_RED);
            names_w = getTextWidth (storm_label);
            tft.setCursor (map_b.x + (map_b.w - names_w)/2, names_y + 3);
            tft.print (storm_label);
            was_city = true;
        } else {
            // move ll to city and draw if interested
            was_city = drawIB_City (ll, ms, minfo_b, names_w, names_y);
        }

        drawIB_Loc (minfo_b, ll, ms);
    }
}
