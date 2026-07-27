/* Northen California DX Foundation Beacon Network.
 * http://www.ncdxf.org/beacon/index.html#Schedule
 */

/* manage the Northern California DX Foundation beacons.
 *
 * Each beacon is drawn as a colored triangle symbol with call sign text below. The triangle is drawn
 * to high res so is redrawn after being scanned. But the text is just jumped over and never redrawn.
 */

#include "HamClock.h"


#define NBEACONS        18                      // number of beacons
#define BEACONR         9                       // beacon symbol radius, pixels
#define BLEG            (BEACONR-4)             // beacon symbol leg length
#define BEACONCW        6                       // beacon char width

typedef struct {
    int16_t lat, lng;                           // location, degs north and east
    char call[7];                               // call sign
    SCoord s;                                   // screen coord of triangle symbol center
    uint16_t c;                                 // color
    SBox call_b;                                // enclosing background box
} NCDXFBeacon;

/* listed in order of 14, 18, 21, 24 and 28 MHz starting at 3N minutes after the hour.
 * 4 of the 18 stations each transmit for 10 seconds then rotate down.
 *
 * given s seconds after the hour, find index for each frequency:
 *   14 MHz i = (s/10+0+NBEACONS)%NBEACONS
 *   18 MHz i = (s/10-1+NBEACONS)%NBEACONS
 *   21 MHz i = (s/10-2+NBEACONS)%NBEACONS
 *   24 MHz i = (s/10-3+NBEACONS)%NBEACONS
 *   28 MHz i = (s/10-4+NBEACONS)%NBEACONS
 */
static NCDXFBeacon blist[NBEACONS] = {
    {  41,  -74, "4U1UN",  {0,0}, 0, {0,0,0,0}},
    {  68, -133, "VE8AT",  {0,0}, 0, {0,0,0,0}},
    {  37, -122, "W6WX",   {0,0}, 0, {0,0,0,0}},
    {  21, -156, "KH6RS",  {0,0}, 0, {0,0,0,0}},
    { -41,  176, "ZL6B",   {0,0}, 0, {0,0,0,0}},
    { -32,  116, "VK6RBP", {0,0}, 0, {0,0,0,0}},
    {  34,  137, "JA2IGY", {0,0}, 0, {0,0,0,0}},
    {  55,   83, "RR9O",   {0,0}, 0, {0,0,0,0}},
    {  22,  114, "VR2B",   {0,0}, 0, {0,0,0,0}},
    {   7,   80, "4S7B",   {0,0}, 0, {0,0,0,0}},
    { -26,   28, "ZS6DN",  {0,0}, 0, {0,0,0,0}},
    {  -1,   37, "5Z4B",   {0,0}, 0, {0,0,0,0}},
    {  32,   35, "4X6TU",  {0,0}, 0, {0,0,0,0}},
    {  60,   25, "OH2B",   {0,0}, 0, {0,0,0,0}},
    {  33,  -17, "CS3B",   {0,0}, 0, {0,0,0,0}},
    { -35,  -58, "LU4AA",  {0,0}, 0, {0,0,0,0}},
    { -12,  -77, "OA4B",   {0,0}, 0, {0,0,0,0}},
    {   9,  -68, "YV5B",   {0,0}, 0, {0,0,0,0}},
};


/* symbol color for each frequency
 */
#define BCOL_14 getMapColor(BAND20_CSPR)
#define BCOL_18 getMapColor(BAND17_CSPR)
#define BCOL_21 getMapColor(BAND15_CSPR)
#define BCOL_24 getMapColor(BAND12_CSPR)
#define BCOL_28 getMapColor(BAND10_CSPR)
#define BCOL_S  RA8875_BLACK            // silent, not actually drawn
#define BCOL_N  6                       // number of color states



/* using the current user time set the color state for each beacon.
 */
static void setBeaconStates ()
{
    time_t t = nowWO();
    int mn = minute(t);
    int sc = second(t);
    uint16_t s_10 = (60*mn + sc)/10;

    for (int id = 0; id < NBEACONS; id++)
        blist[id].c = BCOL_S;

    blist[(s_10-0+NBEACONS)%NBEACONS].c = BCOL_14;
    blist[(s_10-1+NBEACONS)%NBEACONS].c = BCOL_18;
    blist[(s_10-2+NBEACONS)%NBEACONS].c = BCOL_21;
    blist[(s_10-3+NBEACONS)%NBEACONS].c = BCOL_24;
    blist[(s_10-4+NBEACONS)%NBEACONS].c = BCOL_28;
}


/* draw beacon symbol centered at given screen location with the given color
 */
static void drawBeaconSymbol (const SCoord &s, uint16_t c)
{
    tft.fillTriangle (s.x, s.y-BEACONR, s.x-9*BEACONR/10, s.y+BEACONR/2,
                s.x+9*BEACONR/10, s.y+BEACONR/2, RA8875_BLACK);
    tft.fillTriangle (s.x, s.y-BLEG, s.x-9*BLEG/10, s.y+BLEG/2, s.x+9*BLEG/10, s.y+BLEG/2, c);
}

/* draw the given beacon, including callsign beneath.
 */
static void drawBeacon (NCDXFBeacon &nb)
{
    // triangle symbol
    drawBeaconSymbol (nb.s, nb.c);

    // draw call sign
    drawMapTag (nb.call, nb.call_b);
}

/* update map beacons, typically on each 10 second period unless immediate.
 */
void updateBeacons (bool immediate)
{
    // counts as on as long as in rotation set, need not be in front now
    bool beacons_on = brb_rotset & (1 << BRB_SHOW_BEACONS);
    if (!beacons_on)
        return;

    // process if immediate or it's a new time period
    static uint8_t prev_sec10;
    uint8_t sec10 = second(nowWO())/10;
    if (!immediate && sec10 == prev_sec10)
        return;
    prev_sec10 = sec10;

    // now update each beacon as required
    setBeaconStates();
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        if (bp->c != BCOL_S && overMap(bp->s) && !overRSS (bp->call_b))
            drawBeacon (*bp);
    }

    updateClocks(false);
}

/* update screen location for all beacons.
 */
void updateBeaconMapLocations()
{
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        ll2s (deg2rad(bp->lat), deg2rad(bp->lng), bp->s, 3*BEACONCW);   // about max
        setMapTagBox (bp->call, bp->s, BEACONR/2+1, bp->call_b);
    }
}

/* draw the beacon key in NCDXF_b.
 */
static void drawBeaconKey()
{
    // tiny font
    selectFontStyle (BOLD_FONT, FAST_FONT);

    // draw title
    tft.setCursor (NCDXF_b.x+13, NCDXF_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print ("NCDXF");

    // draw each key

    SCoord s;
    s.x = NCDXF_b.x + BEACONR+1;
    s.y = NCDXF_b.y + 16 + BEACONR;
    uint8_t dy = (NCDXF_b.h-16)/(BCOL_N-1);         // silent color not drawn
    uint16_t c;
    
    c = BCOL_14;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print ("14.10");

    s.y += dy;
    c = BCOL_18;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print ("18.11");

    s.y += dy;
    c = BCOL_21;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print ("21.15");

    s.y += dy;
    c = BCOL_24;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print ("24.93");

    s.y += dy;
    c = BCOL_28;
    drawBeaconSymbol (s, c);
    tft.setTextColor (c);
    tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
    tft.print ("28.20");
}

/* draw any of the various contents in NCDXF_b depending on brb_mode.
 * most just draw but return success for options that require fresh lookups.
 */
bool drawNCDXFBox()
{
    bool ok = true;

    // erase
    fillSBox (NCDXF_b, RA8875_BLACK);

    // draw appropriate content
    switch ((BRB_MODE)brb_mode) {

    case BRB_SHOW_BEACONS:

        drawBeaconKey();
        break;

    case BRB_SHOW_SWSTATS:

        (void) checkForNewSpaceWx();
        drawNCDXFSpcWxStats(RA8875_BLACK);
        break;

    case BRB_SHOW_BME76:        // fallthru
    case BRB_SHOW_BME77:

        drawBMEStats();
        break;

    case BRB_SHOW_ONOFF:        // fallthru
    case BRB_SHOW_PHOT:         // fallthru
    case BRB_SHOW_BR:

        drawBrightness();
        break;

    case BRB_SHOW_DEWX: // fallthru
    case BRB_SHOW_DXWX:
        ok = drawNCDXFWx ((BRB_MODE)brb_mode);
        break;

    case BRB_SHOW_LIGHTNING:
        drawNCDXFLightningStats();
        break;

    case BRB_N:
        
        // lint
        break;
    }

    // border -- avoid base line
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y, GRAY);
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x, NCDXF_b.y+NCDXF_b.h-1, GRAY);
    tft.drawLine (NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w-1, NCDXF_b.y+NCDXF_b.h-1, GRAY);

    // ack
    return (ok);
}

/* common template to draw table of stats in NCDXF_b.
 * divide NCDXF_b into NCDXF_B_NFIELDS equal regions vertically with titles near each bottom.
 * use white text and colors for each unless color is black in which case us it for everything.
 */
void drawNCDXFStats (uint16_t color,
                     const char titles[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                     const char values[NCDXF_B_NFIELDS][NCDXF_B_MAXLEN],
                     const uint16_t colors[NCDXF_B_NFIELDS])
{
    // each box height and nice offsets down to value and title
    const uint16_t h = NCDXF_b.h / NCDXF_B_NFIELDS;
    const uint16_t vo = 24*h/37;                         // font baseline
    const uint16_t to = 28*h/37;                         // font top

    // show each item
    for (int i = 0; i < NCDXF_B_NFIELDS; i++) {

        // top height
        uint16_t y = i * NCDXF_b.h / NCDXF_B_NFIELDS;

        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (color == RA8875_BLACK ? colors[i] : color);
        tft.fillRect (NCDXF_b.x+1, y, NCDXF_b.w-2, h, RA8875_BLACK);
        // tft.drawRect (NCDXF_b.x+1, y, NCDXF_b.w-2, h, RA8875_RED);              // RBF
        tft.setCursor (NCDXF_b.x + (NCDXF_b.w-getTextWidth(values[i]))/2, y+vo);
        tft.print (values[i]);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor (color == RA8875_BLACK ? RA8875_WHITE : color);
        tft.setCursor (NCDXF_b.x + (NCDXF_b.w-getTextWidth(titles[i]))/2, y+to);
        tft.print (titles[i]);
    }
}

/* init brb_rotset and brb_mode
 */
void initBRBRotset()
{
    if (!NVReadUInt16 (NV_BRB_ROTSET, &brb_rotset)) {
        // check for old style
        uint8_t old_rotset;
        if (NVReadUInt8 (NV_BRB_ROTSET_OLD, &old_rotset))
            brb_rotset = old_rotset;
        else
            brb_rotset = 0;
    }
    checkBRBRotset();
}

/* insure brb_rotset and brb_mode are set sensibly
 */
void checkBRBRotset()
{
    if (!brb_rotset) {
        brb_mode = BRB_SHOW_BEACONS;            // just pick one that is always possible
        brb_rotset = 1 << BRB_SHOW_BEACONS;    
    } else {
        // arbitrarily set brb_mode to first set bit
        for (int i = 0; i < BRB_N; i++) {
            if (brb_rotset & (1 << i)) {
                brb_mode = i;
                break;
            }
        }
    }
    NVWriteUInt16 (NV_BRB_ROTSET, brb_rotset);
    logBRBRotSet();
}
