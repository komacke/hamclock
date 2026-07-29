/* code to manage the earth map
 */

/* main map drawing routines.
 */


#include "HamClock.h"

// pan, zoom and popup state
PanZoom pan_zoom = {MIN_ZOOM, 0, 0};
MapPopup map_popup;

// DX location and path to DE
SCircle dx_c = {{0,0},DX_R};                    // screen coords of DX symbol
LatLong dx_ll;                                  // geo coords of dx spot

// DE and AntiPodal location
SCircle de_c = {{0,0},DE_R};                    // screen coords of DE symbol
LatLong de_ll;                                  // geo coords of DE
float sdelat, cdelat;                           // handy tri
SCircle deap_c = {{0,0},DEAP_R};                // screen coords of DE antipode symbol
LatLong deap_ll;                                // geo coords of DE antipode

// sun
AstroCir solar_cir;
SCircle sun_c = {{0,0},SUN_R};                  // screen coords of sun symbol
LatLong sun_ss_ll;                              // subsolar location
float csslat, ssslat;                           // handy trig

// moon
AstroCir lunar_cir;
SCircle moon_c = {{0,0},MOON_R};                // screen coords of moon symbol
LatLong moon_ss_ll;                             // sublunar location

// dx options
uint8_t show_lp;                                // display long path, else short part heading

#define GRAYLINE_COS    (-0.208F)               // cos(90 + grayline angle), we use 12 degs
#define GRAYLINE_POW    (0.75F)                 // cos power exponent, sqrt is too severe, 1 is too gradual
static SCoord moremap_s;                        // drawMoreEarth() scanning location
static bool moremap_active;                     // whether a map sweep is currently in progress
static uint32_t moremap_generation;             // incremented whenever a new sweep is explicitly scheduled
static uint32_t next_redraw_ms;                 // next periodic redraw deadline once a sweep completes
static bool mm_p;                              // mouse moved during a map sweep; redraw again when done
static uint32_t mm_ms;                         // throttle mouse-driven city hover redraws
static bool mp_a;                              // map popup menu currently running
static bool mv_a;                              // map view menu currently running
#define EARTH_REDRAW_INTERVAL_MS 30000U         // redraw slow-changing map overlays at a conservative cadence

// cached grid colors
uint16_t EARTH_GRIDC, EARTH_GRIDC00;            // main and highlighted

// flag to defer drawing over map until opportune time:
bool mapmenu_pending;

static void updateCircumstances();
static void drawMapGrid();

void finishMapSweepNow (void)
{
	while (moremap_active)
		drawMoreEarth();
}

/* request a fresh visual sweep of the current map without reloading its source data.
 * used to clear old overlays and redraw moving symbols on demand.
 */
void scheduleMapRedraw (void)
{
    moremap_s.x = 0;
    moremap_s.y = map_b.y;
    moremap_active = true;
    next_redraw_ms = 0;
    moremap_generation++;
}

/* return whether a full, row-by-row map sweep is currently in progress. used by
 * updateSatFootprintFast() to avoid racing with a sweep that has recomputed the satellite's
 * position but not yet painted it (see earthsat.cpp).
 */
bool mapSweepActive (void)
{
    return (moremap_active);
}

/* redraw just the given screen box: restore the underlying map pixels then redraw everything
 * that might legitimately overlay them (grid, paths, planet/DX/sat markers, info box, etc).
 *
 * Use this -- instead of scheduleMapRedraw()/scheduleFreshMap() -- to erase a small transient
 * popup. Those functions only (re)draw overlay symbols once, at the end of a full, multi-frame,
 * row-by-row sweep of the entire map (see drawMoreEarth() below); if something calls them again
 * before that sweep finishes (e.g. a user tapping open/closed a planet popup faster than a sweep
 * completes) the sweep restarts from the top and the end-of-sweep symbol redraw keeps getting
 * starved, which is what let old overlay data (planet dots, DX/sat markers, paths, etc) disappear,
 * partially or completely, until a sweep was finally left undisturbed long enough to finish.
 * This function instead does its work synchronously and immediately, with no dependency on the
 * sweep's progress, so it can't be interrupted or starved no matter how quickly it's re-invoked.
 */
void redrawMapBox (const SBox &box)
{
    for (uint16_t y = box.y; y < box.y + box.h; y++)
        for (uint16_t x = box.x; x < box.x + box.w; x++)
            drawMapCoord (x, y);

    if (core_map != CM_USER) {
        drawMapGrid();
        drawSatPathAndFoot();
        if (waiting4DXPath())
            drawDXPath();
        drawPSKPaths();
        drawAllSymbols();
        drawSatName();
        drawInfoBox();
    }

    tft.drawPR();
}



void mm_redraw (void)
{
    uint32_t now_ms = millis();

    // City hover is cursor-driven. Do not restart an active progressive map sweep;
    // remember that another sweep is needed and start it as soon as the current one finishes.
    if (now_ms - mm_ms < 33U || moremap_active) {
        mm_p = true;
        return;
    }

    mm_p = false;
    scheduleMapRedraw();
    mm_ms = now_ms;
}

bool mm_up (void)
{
    return (mp_a || mv_a);
}

// grid spacing, degrees
#define LL_LAT_GRID     15
#define LL_LNG_GRID     15
#define RADIAL_GRID     15
#define THETA_GRID      15
#define FINESTEP_GRID   (1.0F/pan_zoom.zoom)


// establish EARTH_GRIDC and EARTH_GRIDC00
static void getGridColorCache()
{
    // get base color
    EARTH_GRIDC = getMapColor(GRID_CSPR);

    // hi contrast
    uint8_t h, s, v;
    RGB565_2_HSV (EARTH_GRIDC, &h, &s, &v);
    EARTH_GRIDC00 = v < 128 ? RA8875_WHITE : RA8875_BLACK;
}

/* erase the DE symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEMarker()
{
    eraseSCircle (de_c);
}

/* return whether to display DE marker
 */
bool showDEMarker()
{
    return (overMap(de_c.s));
}

/* draw DE marker.
 */
void drawDEMarker(bool force)
{
    // check for being off zoomed mercator map
    if (de_c.s.x == 0)
        return;

    if (force || showDEMarker()) {
        tft.fillCircle (de_c.s.x, de_c.s.y, DE_R, RA8875_BLACK);
        tft.drawCircle (de_c.s.x, de_c.s.y, DE_R, DE_COLOR);
        tft.fillCircle (de_c.s.x, de_c.s.y, DE_R/2, DE_COLOR);
    }
}

/* erase the antipode symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEAPMarker()
{
    eraseSCircle (deap_c);
}

/* return whether to display the DE antipode
 */
static bool showDEAPMarker()
{
    return (map_proj != MAPP_AZIM1 && !dx_info_for_sat && overMap(deap_c.s));
}

/* return whether to display the DX marker:
 *   over map and either not showing sat or showing either DX weather or VOACAP.
 */
bool showDXMarker()
{
    return ((!dx_info_for_sat
                    || findPaneChoiceNow(PLOT_CH_DXWX) != PANE_NONE
                    || findPaneChoiceNow(PLOT_CH_BC) != PANE_NONE)
            && overMap(dx_c.s));
}

/* draw antipodal marker if applicable.
 */
void drawDEAPMarker()
{
    // checkf for being off zoomed mercator map
    if (deap_c.s.x == 0)
        return;

    if (showDEAPMarker()) {
        tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R, DE_COLOR);
        tft.drawCircle (deap_c.s.x, deap_c.s.y, DEAP_R, RA8875_BLACK);
        tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R/2, RA8875_BLACK);
    }
}

/* draw the NVRAM grid square to 4 chars in the given screen location
 */
static void drawMaidenhead(NV_Name nv, SBox &b, uint16_t color)
{
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv, maid);
    maid[4] = 0;

    fillSBox (b, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (color);
    tft.setCursor (b.x, b.y+b.h-7);
    tft.print (maid);
}

/* draw de_info_b according to de_time_fmt unless showing a pane choice
 */
void drawDEInfo()
{
    // skip if showing pane choice
    if (SHOWING_PANE_0())
        return;

    // init box and set step size
    fillSBox (de_info_b, RA8875_BLACK);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;

    // draw desired contents
    switch (de_time_fmt) {
    case DETIME_INFO:

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (DE_COLOR);

        // time
        drawDECalTime(false);

        // lat and lon
        char buf[50];
        snprintf (buf, sizeof(buf), "%.0f%c  %.0f%c",
                    roundf(fabsf(de_ll.lat_d)), de_ll.lat_d < 0 ? 'S' : 'N',
                    roundf(fabsf(de_ll.lng_d)), de_ll.lng_d < 0 ? 'W' : 'E');
        tft.setCursor (de_info_b.x, de_info_b.y+2*vspace-6);
        tft.print(buf);

        // maidenhead
        drawMaidenhead(NV_DE_GRID, de_maid_b, DE_COLOR);

        // sun rise/set info
        drawDESunRiseSetInfo();

        break;

    case DETIME_ANALOG:         // fallthru
    case DETIME_ANALOG_DTTM:    // fallthru
    case DETIME_DIGITAL_12:     // fallthru
    case DETIME_DIGITAL_24:

        drawTZ (de_tz);
        updateClocks(true);
        break;

    case DETIME_CAL:

        drawDECalTime(true);
        drawCalendar(true);
        break;
    }
}

/* draw the time in de_info_b suitable for DETIME_INFO and DETIME_CALENDAR formats
 */
void drawDECalTime(bool center)
{
    drawTZ (de_tz);

    // get time
    time_t utc = nowWO();
    time_t local = utc + getTZ (de_tz);
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    // generate text
    char buf[32];
    if (getDateFormat() == DF_MDY || getDateFormat() == DF_YMD)
        snprintf (buf, sizeof(buf), "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);
    else
        snprintf (buf, sizeof(buf), "%02d:%02d %d %s", hr, mn, dy, monthShortStr(mo));

    // set position
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
    uint16_t x0 = de_info_b.x;
    if (center) {
        uint16_t bw = getTextWidth (buf);
        x0 += (de_info_b.w - bw)/2;
    }

    // draw
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, vspace, RA8875_BLACK);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x0, de_info_b.y+vspace-6);
    tft.print(buf);
}

/* draw the Maidenhead grid key around the map if appropriate.
 */
static void drawMaidGridKey()
{
    // only if selected and using mercator projection
    if ((mapgrid_choice != MAPGRID_MAID && mapgrid_choice != MAPGRID_MAID4) || map_proj != MAPP_MERCATOR)
        return;


    // keep right stripe above RSS and map scale, if on
    uint16_t right_h = map_b.h;
    if (rss_on)
        right_h = rss_bnr_b.y - map_b.y;
    if (mapScaleIsUp())
        right_h = mapscale_b.y - map_b.y;           // drap_b.y already above rss if on

    // prep background stripes
    tft.fillRect (map_b.x, map_b.y, map_b.w, MH_TR_H, RA8875_BLACK);                            // top
    tft.fillRect (map_b.x+map_b.w-MH_RC_W, map_b.y, MH_RC_W, right_h, RA8875_BLACK);            // right

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // print field letter labels across the top, use latitude of map center then scan lng
    uint16_t rowy = map_b.y + MH_TR_DY;
    LatLong ll;
    s2ll (map_b.x+map_b.w/2, map_b.y+map_b.h/2, ll);
    for (uint8_t i = 0; i < 18; i++) {
        SCoord s;
        ll.lng_d = -180 + (i+0.45F)*360/18;     // center character within square
        ll2s (ll, s, 10);
        if (s.x) {                              // might be off screen when mercator is zoomed
            tft.setCursor (s.x, rowy);
            tft.print ((char)('A' + (180+ll.lng_d)/20));
        }
    }

    // print field letter labels down the right, use lng of map center then scan lat
    uint16_t colx = map_b.x + map_b.w - MH_RC_W + MH_RC_DX;
    s2ll (map_b.x+map_b.w/2, map_b.y+map_b.h/2, ll);
    for (uint8_t i = 0; i < 18; i++) {
        SCoord s;
        ll.lat_d = 90 - (i+0.45F)*180/18;       // center character within square
        ll2s (ll, s, 10);
        if (s.x) {                              // might be off screen when mercator is zoomed
            tft.setCursor (colx, s.y);
            tft.print ((char)('A' + 17 - i));
        }
    }

}

/* check and fix pz to be sure it is legal
 */
void normalizePanZoom (PanZoom &pz)
{
    pz.zoom  = CLAMPF (pz.zoom, MIN_ZOOM, MAX_ZOOM);            // N.B. set zoom first
    pz.pan_x = ((pz.pan_x + EARTH_W + EARTH_W/2) % EARTH_W) - EARTH_W/2;
    pz.pan_y = CLAMPF (pz.pan_y, MIN_PANY(pz.zoom), MAX_PANY(pz.zoom));
}

/* set pz so the given location is at the center of the Mercator map.
 * N.B. pz zoom is not changed.
 */
static void centerMercatorPanZoom (PanZoom &pz, const LatLong &ll)
{
    pz.pan_x = roundf (map_b.w*(ll.lng_d - getCenterLng())/360);
    pz.pan_y = roundf (map_b.h*ll.lat_d/180);
    normalizePanZoom (pz);
}

/* Pan, and optionally zoom, the map so the given location is centered.
 * 'zoom' is clamped to [MIN_ZOOM,MAX_ZOOM]; pass MAX_ZOOM to zoom in as far as possible.
 * Centering works in Mercator and Robinson projections; zoom is only meaningful in
 * Mercator so it is left unchanged in other projections. The new view is persisted
 * to NVRAM and the map is scheduled for a fresh redraw.
 * Returns true if the view actually changed, else false (e.g. unsupported projection).
 */
bool panZoomToLocation (const LatLong &ll, uint8_t zoom)
{
    // pan is only supported on the flat projections; nothing sensible to do otherwise
    if (map_proj != MAPP_MERCATOR && map_proj != MAPP_ROB)
        return false;

    PanZoom new_pz = pan_zoom;

    // zoom only applies to Mercator
    if (map_proj == MAPP_MERCATOR)
        new_pz.zoom = CLAMPF (zoom, MIN_ZOOM, MAX_ZOOM);

    // center the requested location (also normalizes)
    centerMercatorPanZoom (new_pz, ll);

    // nothing to do if unchanged
    if (memcmp (&pan_zoom, &new_pz, sizeof(pan_zoom)) == 0)
        return false;

    // commit, persist and refresh
    pan_zoom = new_pz;
    NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
    NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
    NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
    initEarthMap();
    scheduleFreshMap();

    Serial.printf ("PANZOOM: center %.2f,%.2f zoom %d -> pan_x %d pan_y %d\n",
                   ll.lat_d, ll.lng_d, pan_zoom.zoom, pan_zoom.pan_x, pan_zoom.pan_y);
    return true;
}

/* Restore a previously-saved map view (pan and zoom), persist it and refresh the map.
 * Used to undo a pan/zoom such as the Storms pane "center on storm" action.
 */
void restorePanZoom (const PanZoom &pz)
{
    pan_zoom = pz;
    normalizePanZoom (pan_zoom);
    NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
    NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
    NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
    initEarthMap();
    scheduleFreshMap();
}

/* draw and operate the map popup menu
 */
static void drawMapPopup(void)
{
    // offer to set DX or DE and possibly control pan and zoom, depending on context

    Serial.printf ("POPUP before: pan_x %d pan_y %d zoom %d\n", pan_zoom.pan_x, pan_zoom.pan_y,
                                pan_zoom.zoom);

    const int ZINDENT = 2;

    bool zoom_ok = map_proj == MAPP_MERCATOR;
    bool pan_ok = map_proj == MAPP_MERCATOR || map_proj == MAPP_ROB;
    bool reset_ok = pan_ok && (pan_zoom.pan_x != 0 || pan_zoom.pan_y != 0 || pan_zoom.zoom != MIN_ZOOM);
    MenuFieldType z1_mft = zoom_ok ? MENU_1OFN : MENU_IGNORE;
    MenuFieldType z2_mft = zoom_ok ? MENU_1OFN : MENU_IGNORE;
    MenuFieldType z3_mft = zoom_ok ? MENU_1OFN : MENU_IGNORE;
    MenuFieldType z4_mft = zoom_ok && BUILD_W == 800 ? MENU_1OFN : MENU_IGNORE; // only 800x480 can 4x
    MenuFieldType ctr_mft = pan_ok ? (reset_ok ? MENU_01OFN : MENU_TOGGLE) : MENU_IGNORE;
    MenuFieldType rst_mft = reset_ok ? (pan_ok ? MENU_01OFN : MENU_TOGGLE) : MENU_IGNORE;

    char storm_l1[36], storm_l2[36], storm_l3[36];
    bool have_storm = getStormMapMenuInfo (map_popup.ll,
                    storm_l1, sizeof(storm_l1), storm_l2, sizeof(storm_l2), storm_l3, sizeof(storm_l3));
    char storm_url[100];
    bool have_storm_url = have_storm && getStormTropicalTidbitsURL (map_popup.ll, storm_url, sizeof(storm_url));

    // launch-site POI info, if the tap landed near a launch pad
    char launch_l1[52], launch_l2[52], launch_l3[36];
    const char *launch_wiki = NULL;
    bool have_launch = getLaunchMapMenuInfo (map_popup.ll,
                    launch_l1, sizeof(launch_l1), launch_l2, sizeof(launch_l2),
                    launch_l3, sizeof(launch_l3), &launch_wiki);

    MenuItem mitems[20];
    int n_menu = 0;
    int mi_wiki = -1;                                           // set below iff a Wikipedia item is added
    int mi_stormurl = -1;                                       // set below iff a storm web link is added

    if (have_storm) {
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, storm_l1, 0};
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, storm_l2, 0};
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, storm_l3, 0};
        if (have_storm_url) {
            mi_stormurl = n_menu;
            mitems[n_menu++] = {MENU_TOGGLE, false, 3, ZINDENT, "Open Tropical Tidbits", 0};
        }
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, "-------------", 0};
    }

    if (have_launch) {
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, launch_l1, 0};
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, launch_l2, 0};
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, launch_l3, 0};
        if (launch_wiki) {
            mi_wiki = n_menu;
            mitems[n_menu++] = {MENU_TOGGLE, false, 3, ZINDENT, "Open Launch Complex Wikipedia", 0};
        }
        mitems[n_menu++] = {MENU_LABEL, false, 0, ZINDENT, "-------------", 0};
    }

    int mi_setdx = n_menu;
    mitems[n_menu++] = {MENU_01OFN, false,          1, ZINDENT, "Set DX", 0};
    int mi_setde = n_menu;
    mitems[n_menu++] = {MENU_01OFN, false,          1, ZINDENT, "Set DE", 0};
    mitems[n_menu++] = {MENU_BLANK, false,          0, ZINDENT, NULL, 0};
    int mi_zoom1 = n_menu;
    mitems[n_menu++] = {z1_mft, pan_zoom.zoom == 1, 2, ZINDENT, "Zoom 1x", 0};
    int mi_zoom2 = n_menu;
    mitems[n_menu++] = {z2_mft, pan_zoom.zoom == 2, 2, ZINDENT, "Zoom 2x", 0};
    int mi_zoom3 = n_menu;
    mitems[n_menu++] = {z3_mft, pan_zoom.zoom == 3, 2, ZINDENT, "Zoom 3x", 0};
    int mi_zoom4 = n_menu;
    mitems[n_menu++] = {z4_mft, pan_zoom.zoom == 4, 2, ZINDENT, "Zoom 4x", 0};
    int mi_ctr = n_menu;
    mitems[n_menu++] = {ctr_mft, false,             4, ZINDENT, "Recenter", 0};
    int mi_rst = n_menu;
    mitems[n_menu++] = {rst_mft, false,             4, ZINDENT, "Reset", 0};

    // boxes
    SBox menu_b = {map_popup.s.x, map_popup.s.y, 0, 0};         // shrink wrap
    SBox ok_b;

    // go
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, n_menu, mitems};
    mp_a = true;
    bool ok = runMenu (menu);
    mp_a = false;
    if (ok) {
        // init copy for changes
        PanZoom new_pz = pan_zoom;

        // check for new DX or DE, rely on runMenu to never set both
        if (mitems[mi_setdx].set)
            newDX (map_popup.ll, NULL, NULL);
        if (mitems[mi_setde].set)
            newDE (map_popup.ll, NULL);

        // open the launch site's Wikipedia page if requested
        if (mi_wiki >= 0 && mitems[mi_wiki].set && launch_wiki)
            openURL (launch_wiki);

        // open the storm's Tropical Tidbits satellite loop page if requested
        if (mi_stormurl >= 0 && mitems[mi_stormurl].set)
            openURL (storm_url);

        // reset else other stuff
        if (mitems[mi_rst].set) {
            new_pz.pan_x = new_pz.pan_y = 0;
            new_pz.zoom = MIN_ZOOM;
        } else {
            // N.B. rely on menu setup to know these make sense
	    uint8_t old_zoom = new_pz.zoom;
            if (mitems[mi_zoom1].set)
                new_pz.zoom = MIN_ZOOM;
            else if (mitems[mi_zoom2].set) {
                new_pz.zoom = MIN_ZOOM + 1;
	    }
            else if (mitems[mi_zoom3].set) {
                new_pz.zoom = MIN_ZOOM + 2;
	    }
            else if (mitems[mi_zoom4].set) {
                new_pz.zoom = MIN_ZOOM + 3;
	    }

	    // Zoom only -> centers on DE
	    // Recenter only -> centers on clicked map point
	    // Zoom + Recenter -> zooms and centers on clicked map point
            bool zoom_changed = new_pz.zoom != old_zoom;
	    if (mitems[mi_ctr].set)
	       centerMercatorPanZoom (new_pz, map_popup.ll);
	    else if (new_pz.zoom > MIN_ZOOM && zoom_changed)
	       centerMercatorPanZoom (new_pz, de_ll);
	    else
               normalizePanZoom (new_pz);
        }

        // save and do full update if pz changed
        if (memcmp (&pan_zoom, &new_pz, sizeof(pan_zoom))) {
            pan_zoom = new_pz;
            NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
            NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
            NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
            initEarthMap();
            scheduleFreshMap();
        }

        Serial.printf ("POPUP after: pan_x %d pan_y %d zoom %d\n", pan_zoom.pan_x, pan_zoom.pan_y,
                                pan_zoom.zoom);
    }
}

/* draw lat/long with given step sizes (used for ll and maidenhead).
 */
static void drawLLGrid (int lat_step, int lng_step)
{
    SCoord s0, s1;                                              // end points

    // thickness if any
    int lw = getRawPathWidth (GRID_CSPR);
    if (lw == 0)
        return;

    // lines of latitude, exclude the poles
    for (float lat = -90+lat_step; lat < 90; lat += lat_step) {
        ll2sRaw (deg2rad(lat), deg2rad(-180), s0, lw);
        for (float lng = -180+lng_step; lng <= 180; lng += lng_step) {
            ll2sRaw (deg2rad(lat), deg2rad(lng), s1, lw);
            for (float lg = lng-lng_step+FINESTEP_GRID; lg <= lng; lg += FINESTEP_GRID) {
                ll2sRaw (deg2rad(lat), deg2rad(lg), s1, lw);
                if (segmentSpanOkRaw (s0, s1, lw))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, lw, lat == 0 ? EARTH_GRIDC00 : EARTH_GRIDC);
                s0 = s1;
            }
            s0 = s1;
        }
    }

    // lines of longitude -- pole to pole
    for (float lng = -180; lng < 180; lng += lng_step) {
        ll2sRaw (deg2rad(-90), deg2rad(lng), s0, lw);
        for (float lat = -90+lat_step; lat <= 90; lat += lat_step) {
            ll2sRaw (deg2rad(lat), deg2rad(lng), s1, lw);
            for (float lt = lat-lat_step+FINESTEP_GRID; lt <= lat; lt += FINESTEP_GRID) {
                ll2sRaw (deg2rad(lt), deg2rad(lng), s1, lw);
                if (segmentSpanOkRaw (s0, s1, lw))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, lw, lng == 0 ? EARTH_GRIDC00 : EARTH_GRIDC);
                s0 = s1;
            }
            s0 = s1;
        }
    }
}

/* draw azimuthal grid lines from DE
 */
static void drawAzimGrid ()
{
    const float min_pole_lat = deg2rad(-89);
    const float max_pole_lat = deg2rad(89);
    const float max_az1_r = deg2rad(RADIAL_GRID*floorf(rad2deg(M_PIF)/AZIM1_ZOOM/RADIAL_GRID));
    const float min_az_gap = deg2rad(90-RADIAL_GRID);
    const float max_az_gap = deg2rad(90+RADIAL_GRID);

    SCoord s0, s1;

    // thickness if any
    int lw = getRawPathWidth (GRID_CSPR);
    if (lw == 0)
        return;

    // radial lines
    for (int ti = 0; ti < 360/THETA_GRID; ti++) {
        float t = deg2rad (ti * THETA_GRID);
        s0.x = 0;
        for (float r = 0; r <= M_PIF; r += deg2rad(FINESTEP_GRID)) {
            // skip near 90 for AZM and everything over the ZOOM horizon for AZIM1
            if (map_proj == MAPP_AZIMUTHAL && r > min_az_gap && r < max_az_gap) {
                s0.x = 0;
                continue;
            }
            if (map_proj == MAPP_AZIM1 && r > max_az1_r)
                break;
            float ca, B;
            solveSphere (t, r, sdelat, cdelat, &ca, &B);
            float lat = M_PI_2F - acosf(ca);
            // avoid poles on mercator plots
            if (map_proj != MAPP_MERCATOR || (lat > min_pole_lat && lat < max_pole_lat)) {
                float lng = de_ll.lng + B;
                ll2sRaw (lat, lng, s1, lw);
                if (segmentSpanOkRaw (s0, s1, lw))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, lw, EARTH_GRIDC);
                s0 = s1;
            } else
                s0.x = 0;
        }
    }

    // theta rings
    for (int ri = 1; ri < 180/RADIAL_GRID; ri++) {
        float r = deg2rad (ri * RADIAL_GRID);
        // skip near 90 for AZM and everything over the ZOOM horizon for AZIM1
        if (map_proj == MAPP_AZIMUTHAL && r > min_az_gap && r < max_az_gap) {
            s0.x = 0;
            continue;
        }
        if (map_proj == MAPP_AZIM1 && r > max_az1_r)
            break;
        s0.x = 0;
        // reduce zaggies on smaller circles
        float fine_step = r < M_PIF/4 || r > 3*M_PIF/4 ? 2*FINESTEP_GRID : FINESTEP_GRID;
        for (int ti = 0; ti <= 360/fine_step; ti++) {
            float t = deg2rad (ti * fine_step);
            float ca, B;
            solveSphere (t, r, sdelat, cdelat, &ca, &B);
            float lat = M_PI_2F - acosf(ca);
            // avoid poles on mercator plots
            if (map_proj != MAPP_MERCATOR || (lat > min_pole_lat && lat < max_pole_lat)) {
                float lng = de_ll.lng + B;
                ll2sRaw (lat, lng, s1, lw);
                if (segmentSpanOkRaw (s0, s1, lw))
                    tft.drawLineRaw (s0.x, s0.y, s1.x, s1.y, lw, EARTH_GRIDC);
                s0 = s1;
            } else
                s0.x = 0;
        }
    }
}

/* draw tropics grid lines from DE
 */
static void drawTropicsGrid()
{
    // thickness if any
    int lw = getRawPathWidth (GRID_CSPR);
    if (lw == 0)
        return;

    // draw 2 lines at lat +- 23.5.
    // use loop for all projections to correctly handle pan and zoom.
    // N.B. drawLineRaw avoids double-scaling the line width.
    SCoord s00, s01, s10, s11;
    ll2sRaw (deg2rad(-23.5F), deg2rad(-180), s00, lw);
    ll2sRaw (deg2rad(23.5F), deg2rad(-180), s10, lw);
    for (float lng = -180; lng <= 180; lng += FINESTEP_GRID) {
        ll2sRaw (deg2rad(-23.5), deg2rad(lng), s01, lw);
        ll2sRaw (deg2rad(23.5), deg2rad(lng), s11, lw);
        if (segmentSpanOkRaw (s00, s01, lw))
            tft.drawLineRaw (s00.x, s00.y, s01.x, s01.y, lw, EARTH_GRIDC);
        s00 = s01;
        if (segmentSpanOkRaw (s10, s11, lw))
            tft.drawLineRaw (s10.x, s10.y, s11.x, s11.y, lw, EARTH_GRIDC);
        s10 = s11;
    }
}

/* draw the complete proper map grid
 */
static void drawMapGrid()
{

    switch ((MapGridStyle)mapgrid_choice) {

    case MAPGRID_OFF:
        break;

    case MAPGRID_MAID:

        drawMaidGridKey();
        drawLLGrid (10, 20);
        break;

    case MAPGRID_MAID4:

        drawMaidGridKey();
        // inner grid lines only at max zoom on mercator -- log on state change so we can
        // diagnose any user reports of inner lines appearing at the wrong zoom or projection
        {
            bool show_inner = (pan_zoom.zoom >= MAX_ZOOM && map_proj == MAPP_MERCATOR);
            static int last_zoom = -1;
            static int last_proj = -1;
            static int last_show = -1;
            if ((int)pan_zoom.zoom != last_zoom || (int)map_proj != last_proj || (int)show_inner != last_show) {
                Serial.printf ("MAID4: zoom=%d (MAX=%d) proj=%d (MERC=%d) inner=%s\n",
                               pan_zoom.zoom, MAX_ZOOM, map_proj, MAPP_MERCATOR,
                               show_inner ? "yes" : "no");
                last_zoom = pan_zoom.zoom;
                last_proj = map_proj;
                last_show = show_inner;
            }
            if (show_inner)
                drawLLGrid (1, 2);      // 4-char squares: 1 deg lat x 2 deg lng -- max zoom mercator only
        }
        drawLLGrid (10, 20);            // field boundaries
        break;

    case MAPGRID_LATLNG:

        drawLLGrid (LL_LAT_GRID, LL_LNG_GRID);
        break;

    case MAPGRID_TROPICS:

        drawTropicsGrid();
        break;

    case MAPGRID_AZIM:

        drawAzimGrid();
        break;

    case MAPGRID_CQZONES:
        drawZone (ZONE_CQ, EARTH_GRIDC, -1);
        break;

    case MAPGRID_ITUZONES:
        drawZone (ZONE_ITU, EARTH_GRIDC, -1);
        break;

    default:
        fatalError ("drawMapGrid() bad mapgrid_choice: %d", mapgrid_choice);
        break;
    }
}

/* draw some fake stars for the azimuthal projection
 */
static void drawAzmStars()
{
    #define N_AZMSTARS 100
    uint8_t n_stars = 0;

    switch ((MapProjection)map_proj) {

    case MAPP_MERCATOR:
        break;

    case MAPP_AZIMUTHAL:
        while (n_stars < N_AZMSTARS) {
            int32_t x = random (map_b.w);
            int32_t y = random (map_b.h);
            int32_t dx = (x > map_b.w/2) ? (x - 3*map_b.w/4) : (x - map_b.w/4);
            int32_t dy = y - map_b.h/2;
            if (dx*dx + dy*dy > map_b.w*map_b.w/16) {
                uint16_t c = random(256);
                tft.drawPixel (map_b.x+x, map_b.y+y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    case MAPP_AZIM1:
        while (n_stars < N_AZMSTARS) {
            int32_t x = random (map_b.w);
            int32_t y = random (map_b.h);
            int32_t dx = x - map_b.w/2;
            int32_t dy = y - map_b.h/2;
            if (dx*dx + dy*dy > map_b.h*map_b.h/4) {
                uint16_t c = random(256);
                tft.drawPixel (map_b.x+x, map_b.y+y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    case MAPP_ROB:
        while (n_stars < N_AZMSTARS) {
            LatLong ll;
            SCoord star;
            star.x = map_b.x + random(map_b.w);
            star.y = map_b.y + random(map_b.h);
            if (!s2llRobinson(star,ll)) {
                uint16_t c = random(256);
                tft.drawPixel (star.x, star.y, RGB565(c,c,c));
                n_stars++;
            }
        }
        break;

    default:
        fatalError ("drawAzmStars() bad map_proj %d", map_proj);
    }
}

static void updateCircumstances()
{
    time_t utc = nowWO();

    getSolarCir (utc, de_ll, solar_cir);
    sun_ss_ll.lat_d = rad2deg(solar_cir.dec);
    sun_ss_ll.lng_d = -rad2deg(solar_cir.gha);
    sun_ss_ll.normalize();
    csslat = cosf(sun_ss_ll.lat);
    ssslat = sinf(sun_ss_ll.lat);
    ll2s (sun_ss_ll, sun_c.s, SUN_R+1);

    getLunarCir (utc, de_ll, lunar_cir);
    moon_ss_ll.lat_d = rad2deg(lunar_cir.dec);
    moon_ss_ll.lng_d = -rad2deg(lunar_cir.gha);
    moon_ss_ll.normalize();
    ll2s (moon_ss_ll, moon_c.s, MOON_R+1);

    updateSatPath();
}

/* draw the map view menu button.
 * N.B. adjust y position depending on whether we are drawing the maidenhead labels
 */
static void drawMapMenuButton()
{

    if ((mapgrid_choice == MAPGRID_MAID || mapgrid_choice == MAPGRID_MAID4) && map_proj == MAPP_MERCATOR)
        view_btn_b.y = map_b.y + MH_TR_H;
    else
        view_btn_b.y = map_b.y;

    // 1 pixel inside so overMap() gives 2-pixel thick sat footprints some room
    tft.fillRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_WHITE);

    char style_mem[NV_COREMAPSTYLE_LEN];
    const char *str = getCoreMapStyle (core_map, style_mem);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t str_w = getTextWidth(str);
    tft.setCursor (view_btn_b.x+(view_btn_b.w-str_w)/2, view_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (str);
}

/* draw (or blank) the on-map "Borders On/Off" badge just to the right of the View button.
 * Only shown on Clouds/Terrain in the Mercator or Robinson projections -- see bordersBadgeVisible().
 * Hiding it never touches borders_on, so whatever the user last set just keeps applying silently.
 *
 * Deliberately looks different when engaged: filled light with dark text, like a pressed button,
 * vs its normal black-fill/white-outline/white-text look when off -- same visual family as the
 * View button next to it, just inverted to read as "active".
 */
static void drawBordersButton()
{
    borders_btn_b.y = view_btn_b.y;             // track the View button's row, incl grid-label offset

    if (!bordersBadgeVisible()) {
        // wrong core map or an azimuthal projection -- just be sure it's left blank
        tft.fillRect (borders_btn_b.x, borders_btn_b.y, borders_btn_b.w-1, borders_btn_b.h-1, RA8875_BLACK);
        return;
    }

    uint16_t fill_clr = borders_on ? RA8875_WHITE : RA8875_BLACK;
    uint16_t text_clr = borders_on ? RA8875_BLACK : RA8875_WHITE;
    const char *label = borders_on ? "Borders On" : "Borders Off";

    tft.fillRect (borders_btn_b.x, borders_btn_b.y, borders_btn_b.w-1, borders_btn_b.h-1, fill_clr);
    tft.drawRect (borders_btn_b.x, borders_btn_b.y, borders_btn_b.w-1, borders_btn_b.h-1, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t lbl_w = getTextWidth(label);
    tft.setCursor (borders_btn_b.x+(borders_btn_b.w-lbl_w)/2, borders_btn_b.y+2);
    tft.setTextColor (text_clr);
    tft.print (label);
}

/* draw, perform and engage results of the map View menu
 */
static void drawMapMenu()
{
    enum MIName {     // menu items -- N.B. must be in same order as mitems[]
        MI_STY_TTL,
            MI_STY_CTY, MI_STY_TER, MI_STY_DRA, MI_STY_MUF, MI_STY_MRT, MI_STY_AUR, MI_STY_WXX,
            MI_STY_CLO, MI_STY_TRP, MI_STY_USR, MI_STY_TOA, MI_STY_REL, 
        MI_GRD_TTL,
            MI_GRD_NON, MI_GRD_TRO, MI_GRD_LLG, MI_GRD_MAI, MI_GRD_MA4, MI_GRD_AZM, MI_GRD_CQZ, MI_GRD_ITU,
        MI_PRJ_TTL,
            MI_PRJ_MER, MI_PRJ_AZM, MI_PRJ_AZ1, MI_PRJ_MOL,
        MI_RSS_YES,
        MI_NON_YES,
        MI_CTY_YES,
        MI_LTG_YES,
        MI_N
    };
    #define PRI_INDENT 2
    #define SEC_INDENT 8
    MenuItem mitems[MI_N] = {
        {MENU_LABEL, false, 0, PRI_INDENT, "Style:", 0},
            {MENU_AL1OFN, IS_CMROT(CM_COUNTRIES), 1, SEC_INDENT, cm_info[CM_COUNTRIES].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_TERRAIN),   1, SEC_INDENT, cm_info[CM_TERRAIN].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_DRAP),      1, SEC_INDENT, cm_info[CM_DRAP].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_MUF_V),     1, SEC_INDENT, cm_info[CM_MUF_V].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_MUF_RT),    1, SEC_INDENT, cm_info[CM_MUF_RT].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_AURORA),    1, SEC_INDENT, cm_info[CM_AURORA].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_WX),        1, SEC_INDENT, cm_info[CM_WX].name, 0},
            {MENU_AL1OFN, IS_CMROT(CM_CLOUDS),    1, SEC_INDENT, cm_info[CM_CLOUDS].name, 0},
	    {MENU_AL1OFN, IS_CMROT(CM_TROPO),     1, SEC_INDENT, cm_info[CM_TROPO].name,  0},
            {MENU_IGNORE, false,                  1, SEC_INDENT, NULL, 0}, // CM_USER see below
            {MENU_IGNORE, false,                  1, SEC_INDENT, NULL, 0}, // CM_PMTOA see below
            {MENU_IGNORE, false,                  1, SEC_INDENT, NULL, 0}, // CM_PMREL see below
        {MENU_LABEL, false, 0, PRI_INDENT, "Grid:", 0},
            {MENU_1OFN, false, 2, SEC_INDENT, "None", 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_TROPICS], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_LATLNG], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_MAID], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_MAID4], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_AZIM], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_CQZONES], 0},
            {MENU_1OFN, false, 2, SEC_INDENT, grid_styles[MAPGRID_ITUZONES], 0},
        {MENU_LABEL, false, 0, PRI_INDENT, "Projection:", 0},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_MERCATOR], 0},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_AZIMUTHAL], 0},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_AZIM1], 0},
            {MENU_1OFN, false, 3, SEC_INDENT, map_projnames[MAPP_ROB], 0},
        {MENU_TOGGLE, false, 4, PRI_INDENT, "RSS", 0},
        {MENU_TOGGLE, false, 5, PRI_INDENT, "Night", 0},
        {MENU_TOGGLE, false, 6, PRI_INDENT, "Cities", 0},
        {MENU_TOGGLE, false, 7, PRI_INDENT, "Lightning", 0},
    };

    // init selections with current states

    // if TOA and/or REL are in rotation add to menu
    char propname_toa[NV_COREMAPSTYLE_LEN];             // N.B. must be persistent for lifetime of runMenu()
    char propname_rel[NV_COREMAPSTYLE_LEN];             // N.B. must be persistent for lifetime of runMenu()
    if (IS_CMROT(CM_PMTOA)) {
        mitems[MI_STY_TOA].type = MENU_AL1OFN;
        mitems[MI_STY_TOA].set = true;
        mitems[MI_STY_TOA].label = getCoreMapStyle (CM_PMTOA, propname_toa);
    }
    if (IS_CMROT(CM_PMREL)) {
        mitems[MI_STY_REL].type = MENU_AL1OFN;
        mitems[MI_STY_REL].set = true;
        mitems[MI_STY_REL].label = getCoreMapStyle (CM_PMREL, propname_rel);
    }

    // if CM_USER is possible add to menu
    if (allWebMapImagesOk()) {
        mitems[MI_STY_USR].type = MENU_AL1OFN;
        mitems[MI_STY_USR].set = IS_CMROT(CM_USER);
        mitems[MI_STY_USR].label = cm_info[CM_USER].name;
    }

    mitems[MI_GRD_NON].set = mapgrid_choice == MAPGRID_OFF;
    mitems[MI_GRD_TRO].set = mapgrid_choice == MAPGRID_TROPICS;
    mitems[MI_GRD_LLG].set = mapgrid_choice == MAPGRID_LATLNG;
    mitems[MI_GRD_MAI].set = mapgrid_choice == MAPGRID_MAID;
    mitems[MI_GRD_MA4].set = mapgrid_choice == MAPGRID_MAID4;
    mitems[MI_GRD_AZM].set = mapgrid_choice == MAPGRID_AZIM;
    mitems[MI_GRD_CQZ].set = mapgrid_choice == MAPGRID_CQZONES;
    mitems[MI_GRD_ITU].set = mapgrid_choice == MAPGRID_ITUZONES;

    mitems[MI_PRJ_MER].set = map_proj == MAPP_MERCATOR;
    mitems[MI_PRJ_AZM].set = map_proj == MAPP_AZIMUTHAL;
    mitems[MI_PRJ_AZ1].set = map_proj == MAPP_AZIM1;
    mitems[MI_PRJ_MOL].set = map_proj == MAPP_ROB;

    mitems[MI_RSS_YES].set = rss_on;
    mitems[MI_NON_YES].set = night_on;
    mitems[MI_CTY_YES].set = names_on;
    mitems[MI_LTG_YES].set = lightning_on;

    // create a box for the menu
    SBox menu_b;
    menu_b.x = view_btn_b.x + 1;                // left edge matches view button with slight indent
    menu_b.y = view_btn_b.y+view_btn_b.h;       // top just below view button
    menu_b.w = 0;                               // shrink to fit

    // run menu
    SBox ok_b;
    MenuInfo menu = {menu_b, ok_b, UF_CLOCKSOK, M_CANCELOK, 1, MI_N, mitems};
    mv_a = true;
    bool ok = runMenu (menu);
    mv_a = false;

    if (ok) {


        // build new map_rotset
        uint16_t prev_rotset = map_rotset;
        map_rotset = 0;
        if (mitems[MI_STY_CTY].set)
            scheduleNewCoreMap (CM_COUNTRIES);
        if (mitems[MI_STY_TER].set)
            scheduleNewCoreMap (CM_TERRAIN);
        if (mitems[MI_STY_DRA].set)
            scheduleNewCoreMap (CM_DRAP);
        if (mitems[MI_STY_MUF].set)
            scheduleNewCoreMap (CM_MUF_V);
        if (mitems[MI_STY_MRT].set)
            scheduleNewCoreMap (CM_MUF_RT);
        if (mitems[MI_STY_AUR].set)
            scheduleNewCoreMap (CM_AURORA);
        if (mitems[MI_STY_WXX].set)
            scheduleNewCoreMap (CM_WX);
        if (mitems[MI_STY_CLO].set)
            scheduleNewCoreMap (CM_CLOUDS);
	if (mitems[MI_STY_TRP].set)
	    scheduleNewCoreMap (CM_TROPO);
        if (mitems[MI_STY_USR].set)
            scheduleNewCoreMap (CM_USER);
        if (mitems[MI_STY_TOA].set)
            scheduleNewCoreMap (CM_PMTOA);
        if (mitems[MI_STY_REL].set)
            scheduleNewCoreMap (CM_PMREL);

        // check for changes and confirm core_map
        if (map_rotset != prev_rotset) {
            // pick one and do full refresh if core_map no longer selected
            if (!IS_CMROT(core_map))
                insureCoreMap();
            saveCoreMaps();
        }
        logMapRotSet();

        // check for different grid
        if (mitems[MI_GRD_NON].set && mapgrid_choice != MAPGRID_OFF) {
            mapgrid_choice = MAPGRID_OFF;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_TRO].set && mapgrid_choice != MAPGRID_TROPICS) {
            mapgrid_choice = MAPGRID_TROPICS;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_LLG].set && mapgrid_choice != MAPGRID_LATLNG) {
            mapgrid_choice = MAPGRID_LATLNG;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_MAI].set && mapgrid_choice != MAPGRID_MAID) {
            mapgrid_choice = MAPGRID_MAID;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_MA4].set && mapgrid_choice != MAPGRID_MAID4) {
            mapgrid_choice = MAPGRID_MAID4;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_AZM].set && mapgrid_choice != MAPGRID_AZIM) {
            mapgrid_choice = MAPGRID_AZIM;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_CQZ].set && map_proj != MAPGRID_CQZONES) {
            mapgrid_choice = MAPGRID_CQZONES;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        } else if (mitems[MI_GRD_ITU].set && map_proj != MAPGRID_ITUZONES) {
            mapgrid_choice = MAPGRID_ITUZONES;
            NVWriteUInt8 (NV_GRIDSTYLE, mapgrid_choice);
        }

        // check for different map projection. on any projection change, reset pan and zoom
        // back to the default view so the user doesn't carry stale state across projections
        // (e.g. a leftover MAX_ZOOM from a previous Mercator session would otherwise persist
        // through azimuth/robinson and then re-apply when switching back to Mercator).
        bool proj_changed = false;
        if (mitems[MI_PRJ_MER].set && map_proj != MAPP_MERCATOR) {
            map_proj = MAPP_MERCATOR;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            proj_changed = true;
        } else if (mitems[MI_PRJ_AZM].set && map_proj != MAPP_AZIMUTHAL) {
            map_proj = MAPP_AZIMUTHAL;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            proj_changed = true;
        } else if (mitems[MI_PRJ_AZ1].set && map_proj != MAPP_AZIM1) {
            map_proj = MAPP_AZIM1;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            proj_changed = true;
        } else if (mitems[MI_PRJ_MOL].set && map_proj != MAPP_ROB) {
            map_proj = MAPP_ROB;
            NVWriteUInt8 (NV_MAPPROJ, map_proj);
            proj_changed = true;
        }
        if (proj_changed) {
            pan_zoom.zoom = MIN_ZOOM;
            pan_zoom.pan_x = 0;
            pan_zoom.pan_y = 0;
            NVWriteUInt8 (NV_ZOOM, pan_zoom.zoom);
            NVWriteInt16 (NV_PANX, pan_zoom.pan_x);
            NVWriteInt16 (NV_PANY, pan_zoom.pan_y);
        }

        // check for change night option
        if (mitems[MI_NON_YES].set != night_on) {
            night_on = mitems[MI_NON_YES].set;
            NVWriteUInt8 (NV_NIGHT_ON, night_on);
        }

        // check for change of names option
        if (mitems[MI_CTY_YES].set != names_on) {
            names_on = mitems[MI_CTY_YES].set;
            NVWriteUInt8 (NV_NAMES_ON, names_on);
        }

        // check for changed RSS
        if (mitems[MI_RSS_YES].set != rss_on) {
            rss_on = mitems[MI_RSS_YES].set;
            NVWriteUInt8 (NV_RSS_ON, rss_on);
        }

        // check for change of lightning option
        if (mitems[MI_LTG_YES].set != lightning_on) {
            lightning_on = mitems[MI_LTG_YES].set;
            NVWriteUInt8 (NV_LIGHTNING_ON, lightning_on);
            resetLightning();

            // The NCDXF Lightning choice depends on the main map Lightning overlay.
            // If Lightning is turned off here, also remove it from the NCDXF rotation
            // set so it can not keep rotating as a hidden zero-count panel.
            if (!lightning_on && (brb_rotset & (1 << BRB_SHOW_LIGHTNING))) {
                brb_rotset &= ~(1 << BRB_SHOW_LIGHTNING);
                checkBRBRotset();               // falls back to beacons if this emptied the set
                brb_next_update = 0;            // force prompt NCDXF redraw/update
            }
        }

        // engage change
        initEarthMap();
        tft.drawPR();
    }

    // discard any extra taps
    drainTouch();

    printFreeHeap ("drawMapMenu");

}

/* restart map for current projection and de_ll and dx_ll
 */
void initEarthMap()
{

    // completely erase map
    fillSBox (map_b, RA8875_BLACK);

    // add funky star field if azm
    drawAzmStars();

    // get grid colors
    getGridColorCache();

    // freshen RSS and clocks
    scheduleRSSNow();
    updateClocks(true);

    // draw map view button
    drawMapMenuButton();
    drawBordersButton();

    // update astro info
    updateCircumstances();

    // update DE and DX info
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    ll2s (de_ll, de_c.s, DE_R);
    antipode (deap_ll, de_ll);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    ll2s (dx_ll, dx_c.s, DX_R);

    // show updated info
    drawDEInfo();
    drawDXInfo();

    // insure NCDXF screen coords match current map type
    updateBeaconMapLocations();

    // update zone screen boundaries
    updateZoneSCoords(ZONE_CQ);
    updateZoneSCoords(ZONE_ITU);

    // reproject country-borders overlay if pan/zoom/projection/geometry changed
    updateCountryBorders();

    // now main loop can resume with drawMoreEarth()
    scheduleMapRedraw();
}

/* display another earth map row at mmoremap_s.
 */
void drawMoreEarth()
{
    // check for mouse movement (both local and web)
    static uint16_t lx = -1;
    static uint16_t ly = -1;
    static bool lom = false;
    uint16_t mx, my;
    bool has_mouse = tft.getMouse (&mx, &my);
    if (has_mouse) {
        bool om = overMap(SCoord{mx, my}) || overHoverPane(SCoord{mx, my});
        bool mv = mx != lx || my != ly;
        if (mv) {
            lx = mx;
            ly = my;
            if (mainpage_up && !mm_up() && (om || lom))
                mm_redraw();
            lom = om;
        }
    } else {
        if (lom) {
            mm_redraw();
            lom = false;
            lx = -1;
            ly = -1;
        }
    }

    if (!moremap_active) {
        uint32_t now_ms = millis();
        bool mm_due = mm_p && now_ms - mm_ms >= 33U;
        bool map_due = (int32_t)(now_ms - next_redraw_ms) >= 0;

        // Only consume deferred overlays while the map is stable. If a redraw is
        // due now, leave them pending so the fresh map does not immediately paint
        // over them before the end-of-sweep handlers run.
        if (!mm_due && !map_due) {
            if (mapmenu_pending) {
                drawMapMenu();
                mapmenu_pending = false;
            }
            if (map_popup.pending) {
                drawMapPopup();
                map_popup.pending = false;
            }
            return;
        }

        if (mm_due) {
            mm_p = false;
            mm_ms = now_ms;
        }
        updateCircumstances();
        scheduleMapRedraw();
    }

    uint32_t sweep_generation = moremap_generation;
    uint16_t last_x = map_b.x + EARTH_W - 1;

    // draw next row
    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x++)
        drawMapCoord (moremap_s);               // does not draw grid

    // advance row, wrap and reset and finish up at the end
    if ((moremap_s.y += 1) >= map_b.y + EARTH_H) {

        // draw goodies unless showing CM_USER
        if (core_map != CM_USER) {
            drawMapGrid();
            drawSatPathAndFoot();
            if (waiting4DXPath())
                drawDXPath();
            drawPSKPaths ();
            drawAllSymbols();
            drawSatName();
            drawInfoBox();
        }

        // the sweep just painted over the whole map, including these -- restore them every time,
        // not just after a full initEarthMap(), or they silently vanish after the next redraw
        drawMapMenuButton();
        drawBordersButton();

        // draw now
        tft.drawPR();

        // check pending events
        if (mapmenu_pending) {
            drawMapMenu();
            mapmenu_pending = false;
        }
        if (map_popup.pending) {
            drawMapPopup();
            map_popup.pending = false;
        }

        // rotate?
        checkBGMap();

        // a menu action or map refresh may have already restarted the sweep.
        if (moremap_generation != sweep_generation)
            return;

        // otherwise stop here and wait until the next periodic or explicit redraw request.
        moremap_active = false;

        if (mm_p) {
            mm_p = false;
            mm_ms = millis();
            scheduleMapRedraw();
        }
        next_redraw_ms = millis() + EARTH_REDRAW_INTERVAL_MS;

    // #define TIME_MAP_DRAW                             // RBF
    #if defined(TIME_MAP_DRAW)
        static struct timeval tv0;
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        if (tv0.tv_sec != 0)
            Serial.printf ("****** map %ld us\n", TVDELUS (tv0, tv1));
        tv0 = tv1;
    #endif // TIME_MAP_DRAW

    }
}

/* convert lat and long in radians to scaled screen coords.
 * keep result no closer than the given raw edge distance.
 * probably should return false bool for zoomed mercator but we just set s.x = 0 for segmentSpanOk()
 */
static void ll2sScaled (const LatLong &ll, SCoord &s, uint8_t edge, int scale)
{

    uint16_t map_x = scale*map_b.x;
    uint16_t map_y = scale*map_b.y;
    uint16_t map_w = scale*map_b.w;
    uint16_t map_h = scale*map_b.h;

    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        if (ca > 0) {
            // front (left) side, centered at DE
            float a = acosf (ca);
            float R = fminf (a*map_w/(2*M_PIF), map_w/4 - edge - 1);        // well clear
            float dx = R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_x + map_w/4 + dx);
            s.y = roundf(map_y + map_h/2 - dy);
        } else {
            // back (right) side, centered at DE antipode
            float a = M_PIF - acosf (ca);
            float R = fminf (a*map_w/(2*M_PIF), map_w/4 - edge - 1);        // well clear
            float dx = -R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_x + 3*map_w/4 + dx);
            s.y = roundf(map_y + map_h/2 - dy);
        }
        } break;

    case MAPP_AZIM1: {
        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);
        float a = AZIM1_ZOOM*acosf (ca);
        float R = fminf (map_h/2*powf(a/M_PIF,1/AZIM1_FISHEYE), map_h/2 - edge - 1);
        float dx = R*sinf(B);
        float dy = R*cosf(B);
        s.x = roundf(map_x + map_w/2 + dx);
        s.y = roundf(map_y + map_h/2 - dy);
        } break;

    case MAPP_MERCATOR: {

        // straight rectangular Mercator projection

        // find distance from center of scaled but unzoomed map
        float dx = map_w*(ll.lng_d-getCenterLng())/360 - scale*pan_zoom.pan_x;

        // this is still full scale so will be visible for sure so wrap onto map
        dx = fmodf (dx + 5*map_w/2, map_w) - map_w/2;

        // now zoom and place on real map
        s.x = roundf (map_x + map_w/2 + pan_zoom.zoom*dx);

        // y is much easier because there's no getCenterLat() and it doesn't wrap
        s.y = roundf (map_y + map_h/2 - pan_zoom.zoom * (map_h*ll.lat_d/180 - scale*pan_zoom.pan_y));

        // guard edge or mark as invisible to inBox() and segmentSpanOk()
        if (s.x < map_x || s.x >= map_x + map_w || s.y < map_y || s.y >= map_y + map_h) {
            s.x = 0;
        } else {
            uint16_t e;
            e = map_x + edge;
            if (s.x < e)
                s.x = e;
            e = map_x + map_w - edge - 1;
            if (s.x > e)
                s.x = e;
            e = map_y + edge;
            if (s.y < e)
                s.y = e;
            e = map_y + map_h - edge - 1;
            if (s.y > e)
                s.y = e;
        }
        } break;

    case MAPP_ROB:
        ll2sRobinson (ll, s, edge, scale);
        break;

    default:
        fatalError ("ll2sRaw() bad map_proj %d", map_proj);
    }
}

/* the first overload wants rads, the second wants fully populated LatLong
 */
void ll2s (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2sScaled (ll, s, edge, 1);
}
void ll2s (const LatLong &ll, SCoord &s, uint8_t edge)
{
    ll2sScaled (ll, s, edge, 1);
}

/* same but to full screen res
 */
void ll2sRaw (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2sScaled (ll, s, edge, tft.SCALESZ);
}
void ll2sRaw (const LatLong &ll, SCoord &s, uint8_t edge)
{
    ll2sScaled (ll, s, edge, tft.SCALESZ);
}

/* convert a screen coord to lat and long.
 * return whether location is really over valid map.
 */
bool s2ll (uint16_t x, uint16_t y, LatLong &ll)
{
    SCoord s;
    s.x = x;
    s.y = y;
    return (s2ll (s, ll));
}
bool s2ll (const SCoord &s, LatLong &ll)
{
    // avoid map
    if (!overMap(s))
        return (false);

    switch ((MapProjection)map_proj) {

    case MAPP_AZIMUTHAL: {
        // radius from center of point's hemisphere
        bool on_right = s.x > map_b.x + map_b.w/2;
        float dx = on_right ? s.x - (map_b.x + 3*map_b.w/4) : s.x - (map_b.x + map_b.w/4);
        float dy = (map_b.y + map_b.h/2) - s.y;
        float r2 = dx*dx + dy*dy;

        // see if really on surface
        float w2 = map_b.w*map_b.w/16;
        if (r2 > w2)
            return(false);

        // use screen triangle to find globe
        float b = sqrtf((float)r2/w2)*(M_PI_2F);
        float A = (M_PI_2F) - atan2f (dy, dx);
        float ca, B;
        solveSphere (A, b, (on_right ? -1 : 1) * sdelat, cdelat, &ca, &B);
        ll.lat = M_PI_2F - acosf(ca);
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = fmodf (de_ll.lng + B + (on_right?6:5)*M_PIF, 2*M_PIF) - M_PIF;
        ll.lng_d = rad2deg(ll.lng);

        } break;

    case MAPP_AZIM1: {

        // radius from center
        float dx = s.x - (map_b.x + map_b.w/2);
        float dy = (map_b.y + map_b.h/2) - s.y;
        float r2 = dx*dx + dy*dy;

        // see if really on surface
        float h2 = map_b.h*map_b.h/4;
        if (r2 > h2)
            return(false);

        // use screen triangle to find globe
        float b = powf((float)r2/h2, AZIM1_FISHEYE/2.0F) * M_PIF / AZIM1_ZOOM;     // /2 just for sqrt
        float A = (M_PI_2F) - atan2f (dy, dx);
        float ca, B;
        solveSphere (A, b, sdelat, cdelat, &ca, &B);
        ll.lat = M_PI_2F - acosf(ca);
        ll.lat_d = rad2deg(ll.lat);
        ll.lng = fmodf (de_ll.lng + B + 5*M_PIF, 2*M_PIF) - M_PIF;
        ll.lng_d = rad2deg(ll.lng);

        } break;

    case MAPP_MERCATOR: {

        // straight rectangular mercator projection
        ll.lat_d = 180.0F*((map_b.y + map_b.h/2 - s.y)/(float)pan_zoom.zoom + pan_zoom.pan_y)/map_b.h;
        ll.lng_d = 360.0F*((s.x - map_b.x - map_b.w/2)/(float)pan_zoom.zoom + pan_zoom.pan_x)/map_b.w;
        if (core_map != CM_USER)
            ll.lng_d += getCenterLng();
        ll.normalize();

        } break;

    case MAPP_ROB:

        return (s2llRobinson (s, ll));
        break;

    default:
        fatalError ("s2ll() bad map_proj %d", map_proj);
    }


    return (true);
}

/* given numeric difference between two longitudes in degrees, return shortest diff
 */
float lngDiff (float dlng)
{
    float fdiff = fmodf(fabsf(dlng + 720), 360);
    if (fdiff > 180)
        fdiff = 360 - fdiff;
    return (fdiff);
}


/* draw at the given screen location, if it's over the map.
 */
void drawMapCoord (uint16_t x, uint16_t y)
{

    SCoord s;
    s.x = x;
    s.y = y;
    drawMapCoord (s);
}
void drawMapCoord (const SCoord &s)
{
    // draw one map pixel at full screen resolution. requires lat/lng gradients.

    // find lat/lng at this screen location, bale if not over map
    LatLong lls;
    if (!s2ll(s,lls))
        return; 

    /* even though we only draw one application point, s, plotEarth needs points r and d to
     * interpolate to full map resolution.
     *   s - - - r
     *   |
     *   d
     */
    SCoord sr, sd;
    LatLong llr, lld;
    sr.x = s.x + 1;
    sr.y = s.y;
    if (!s2ll(sr,llr))
        llr = lls;
    sd.x = s.x;
    sd.y = s.y + 1;
    if (!s2ll(sd,lld))
        lld = lls;

    // find angle between subsolar point and any visible near this location
    // TODO: actually different at each subpixel, this causes striping
    float clat = cosf(lls.lat);
    float slat = sinf(lls.lat);
    float cos_t = ssslat*slat + csslat*clat*cosf(sun_ss_ll.lng-lls.lng);

    // decide day, night or twilight
    float fract_day;
    if (!night_on || cos_t > 0) {
        // < 90 deg: sunlit
        fract_day = 1;
    } else if (cos_t > GRAYLINE_COS) {
        // blend from day to night
        fract_day = 1 - powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
    } else {
        // night side
        fract_day = 0;
    }

    // draw the full res map point
    tft.plotEarth (s.x, s.y, lls.lat_d, lls.lng_d, llr.lat_d - lls.lat_d, llr.lng_d - lls.lng_d,
                    lld.lat_d - lls.lat_d, lld.lng_d - lls.lng_d, fract_day);
}

/* draw sun symbol.
 */
void drawSun ()
{
    // check for being off zoomed mercator map
    if (sun_c.s.x == 0)
        return;

    
    // draw at full display precision

    #define      N_SUN_RAYS      8

    SCoord sc;
    const uint16_t sun_r = tft.SCALESZ * SUN_R;
    const uint16_t body_r = sun_r/2;
    ll2sRaw (sun_ss_ll, sc, sun_r);
    const uint16_t raw_x = sc.x;
    const uint16_t raw_y = sc.y;
    tft.fillCircleRaw (raw_x, raw_y, sun_r, RA8875_BLACK);
    tft.fillCircleRaw (raw_x, raw_y, body_r, RA8875_YELLOW);
    for (uint8_t i = 0; i < N_SUN_RAYS; i++) {
        float a = i*2*M_PIF/N_SUN_RAYS;
        float sa = sinf(a);
        float ca = cosf(a);
        uint16_t x0 = raw_x + roundf ((body_r+tft.SCALESZ)*ca);
        uint16_t y0 = raw_y + roundf ((body_r+tft.SCALESZ)*sa);
        uint16_t x1 = raw_x + roundf (sun_r*ca);
        uint16_t y1 = raw_y + roundf (sun_r*sa);
        tft.drawLineRaw (x0, y0, x1, y1, tft.SCALESZ-1, RA8875_YELLOW);
    }

#   undef N_SUN_RAYS
}

/* draw moon symbol.
 */
void drawMoon ()
{
    // check for being off zoomed mercator map
    if (moon_c.s.x == 0)
        return;


    float phase = lunar_cir.phase;
    
    // draw at full display precision -- similar to drawMoonImage()

    SCoord mc;
    const uint16_t moon_r = MOON_R*tft.SCALESZ;
    ll2sRaw (moon_ss_ll, mc, moon_r);
    const int16_t raw_x = mc.x;
    const int16_t raw_y = mc.y;
    const int16_t flip = de_ll.lat < 0 ? -1 : 1;
    for (int16_t dy = -moon_r; dy <= moon_r; dy++) {
        float Ry = sqrtf(moon_r*moon_r-dy*dy);
        int16_t Ryi = roundf(Ry);
        for (int16_t dx = -Ryi; dx <= Ryi; dx++) {
            float a = acosf(dx/Ry);
            uint16_t color = (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                                ? RA8875_BLACK : RA8875_WHITE;
            tft.drawPixelRaw (raw_x+dx*flip, raw_y+dy*flip, color);
        }
    }
}

/* display some info about DX location in dx_info_b unless showing pane 0
 */
void drawDXInfo ()
{

    // skip if dx_info_b being used for sat info or pane 0
    if (dx_info_for_sat || SHOWING_PANE_0())
        return;

    // divide into 5 rows
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    // time
    drawDXTime();

    // erase and init
    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+2*vspace, dx_info_b.w, dx_info_b.h-2*vspace-1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);

    // lat and long
    char buf[50];
    snprintf (buf, sizeof(buf), "%.0f%c  %.0f%c",
                roundf(fabsf(dx_ll.lat_d)), dx_ll.lat_d < 0 ? 'S' : 'N',
                roundf(fabsf(dx_ll.lng_d)), dx_ll.lng_d < 0 ? 'W' : 'E');
    tft.setCursor (dx_info_b.x, dx_info_b.y+3*vspace-8);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);

    // maidenhead
    drawMaidenhead(NV_DX_GRID, dx_maid_b, DX_COLOR);

    // compute dist and bearing in desired units
    float dist, bearing;
    propDEPath (show_lp, dx_ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (showDistKm())
        dist *= KM_PER_MI;

    // convert to magnetic if desired
    bool bearing_ismag = desiredBearing (de_ll, bearing);

    // print, capturing where units and deg/path can go
    tft.setCursor (dx_info_b.x, dx_info_b.y+5*vspace-4);
    tft.printf ("%.0f", dist);
    uint16_t units_x = tft.getCursorX()+2;
    tft.setCursor (units_x + 6, tft.getCursorY());
    tft.printf ("@%.0f", bearing);
    uint16_t deg_x = tft.getCursorX() + 3;
    uint16_t deg_y = tft.getCursorY();

    // home-made degree symbol if true, else M for magnetic
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (deg_x, deg_y-bh-bh/6);
    tft.print (bearing_ismag ? 'M' : 'o');

    // rows for small chars
    uint16_t sm_y0 = deg_y - 13*bh/20;
    uint16_t sm_y1 = deg_y - 6*bh/20;

    // path direction
    tft.setCursor (deg_x, sm_y0);
    tft.print (show_lp ? 'L' : 'S');
    tft.setCursor (deg_x, sm_y1);
    tft.print ('P');

    // distance units
    tft.setCursor (units_x, sm_y0);
    tft.print(showDistKm() ? 'k' : 'm');
    tft.setCursor (units_x, sm_y1);
    tft.print(showDistKm() ? 'm' : 'i');

    // sun rise/set or prefix
    if (dxsrss == DXSRSS_PREFIX) {
        fillSBox (dxsrss_b, RA8875_BLACK);
        char prefix[MAX_PREF_LEN];
        if (getDXPrefix (prefix)) {
            prefix[4] = '\0';   // max room
            tft.setTextColor(DX_COLOR);
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            bw = getTextWidth (prefix);
            tft.setCursor (dxsrss_b.x+(dxsrss_b.w-bw)/2, dxsrss_b.y + 29);
            tft.print (prefix);
        }
    } else {
        drawDXSunRiseSetInfo();
    }
}

/* return whether s is over DX path direction portion of dx_info_b
 */
bool checkPathDirTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x + dx_info_b.w/2;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* draw DX time unless in sat mode or showing a pane choice
 */
void drawDXTime()
{
    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat || SHOWING_PANE_0())
        return;

    drawTZ (dx_tz);

    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    time_t utc = nowWO();
    time_t local = utc + getTZ (dx_tz);
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+vspace, dx_info_b.w, vspace, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);
    tft.setCursor (dx_info_b.x, dx_info_b.y+2*vspace-8);

    char buf[32];
    if (getDateFormat() == DF_MDY || getDateFormat() == DF_YMD)
        snprintf (buf, sizeof(buf), "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);
    else
        snprintf (buf, sizeof(buf), "%02d:%02d %d %s", hr, mn, dy, monthShortStr(mo));
    tft.print(buf);
}

/* set `to' to the antipodal location of coords in `from'.
 */
void antipode (LatLong &to, const LatLong &from)
{
    to.lat_d = -from.lat_d;
    to.lng_d = from.lng_d+180;
    to.normalize();
}

/* return whether s is over the view_btn_b, including an extra border for fat lines or DX etc.
 * also extends over the Borders badge, when it's currently shown, since the two sit side by side.
 */
bool overViewBtn (const SCoord &s, uint16_t border)
{
    border += 1;
    uint16_t right_edge = view_btn_b.x + view_btn_b.w;
    if (bordersBadgeVisible())
        right_edge = borders_btn_b.x + borders_btn_b.w;
    return (s.x < right_edge + border && s.y < view_btn_b.y + view_btn_b.h + border);
}

/* return whether the given line segment spans a reasonable portion of the map.
 * beware map edge, view button, wrap-around and crossing center of azm map
 * .x == 0 denotes off the map entirely.
 */
bool segmentSpanOk (const SCoord &s0, const SCoord &s1, uint16_t border)
{
    if (s0.x == 0 || s1.x == 0)
        return (false);
    if (s0.x > s1.x ? (s0.x - s1.x > map_b.w/4) : (s1.x - s0.x > map_b.w/4))
        return (false);         // too wide
    if (s0.y > s1.y ? (s0.y - s1.y > map_b.h/3) : (s1.y - s0.y > map_b.h/3))
        return (false);         // too hi
    if (map_proj == MAPP_AZIMUTHAL && ((s0.x < map_b.x+map_b.w/2) != (s1.x < map_b.x+map_b.w/2)))
        return (false);         // crosses azimuthal hemispheres
    if (overViewBtn(s0,border) || overViewBtn(s1,border))
        return (false);         // over the view button
    if (!overMap(s0) || !overMap(s1))
        return (false);         // off the map entirely
    return (true);              // ok!
}

/* return whether the given line segment spans a reasonable portion of the map.
 * beware map edge, view button, wrap-around and crossing center of azm map
 * .x == 0 denotes off the map entirely.
 * coords are in raw pixels.
 */
bool segmentSpanOkRaw (const SCoord &s0, const SCoord &s1, uint16_t border)
{
    uint16_t map_x = tft.SCALESZ*map_b.x;
    uint16_t map_w = tft.SCALESZ*map_b.w;
    uint16_t map_h = tft.SCALESZ*map_b.h;

    if (s0.x == 0 || s1.x == 0)
        return (false);
    if (s0.x > s1.x ? (s0.x - s1.x > map_w/4) : (s1.x - s0.x > map_w/4))
        return (false);         // too wide
    if (s0.y > s1.y ? (s0.y - s1.y > map_h/3) : (s1.y - s0.y > map_h/3))
        return (false);         // too hi
    if (map_proj == MAPP_AZIMUTHAL && ((s0.x < map_x+map_w/2) != (s1.x < map_x+map_w/2)))
        return (false);         // crosses azimuthal hemisphere
    if (overViewBtn(raw2appSCoord(s0),border/tft.SCALESZ)
                                || overViewBtn(raw2appSCoord(s1),border/tft.SCALESZ))
        return (false);         // over the view button
    if (!overMap(raw2appSCoord(s0)) || !overMap(raw2appSCoord(s1)))
        return (false);         // off the map entirely
    return (true);              // ok!
}
