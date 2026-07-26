/* Planets -- shows the 7 visible planets (Mercury, Venus, Mars, Jupiter, Saturn, Uranus,
 * Neptune -- Earth is excluded since we're observing from it) on the map at their current
 * sub-point, when enabled via Setup's "Show Planets?" option.
 *
 * Positions use the classic low-precision orbital-element method (aka "Paul Schlyter's formulas",
 * the same public-domain technique many planetarium/amateur astronomy tools use for
 * non-critical-precision work -- roughly arc-minute accuracy, more than sufficient for "which
 * way do I look"). No network access or ephemeris file needed. This mirrors, at a small scale,
 * the same style of low-precision solar formula P13.cpp's Sun class already uses for eclipse
 * calculations -- just extended to all 7 planets and fully self-contained here.
 *
 * A planet is only shown once its elevation at DE is above PLANET_MIN_EL.
 */

#include "HamClock.h"


#define PLANET_MIN_EL   10.0F                            // minimum elevation to show, degrees
#define PLANET_R        4                                // marker radius, canonical px
#define N_PLANETS       7

typedef enum {
    PL_MERCURY, PL_VENUS, PL_MARS, PL_JUPITER, PL_SATURN, PL_URANUS, PL_NEPTUNE
} PlanetId;

typedef struct {
    const char *name;
    uint16_t color;
    // orbital elements at epoch (degrees, AU) and their rate of change per day
    float N0, Nd;                                         // longitude of ascending node
    float i0, id;                                         // inclination
    float w0, wd;                                         // argument of perihelion
    float a0, ad;                                         // semi-major axis, AU
    float e0, ed;                                         // eccentricity
    float M0, Md;                                         // mean anomaly
} PlanetElements;

static const PlanetElements planet_els[N_PLANETS] = {

    // name       color                       N0        Nd            i0       id             w0         wd            a0          ad            e0         ed             M0         Md
    { "Mercury", RGB565(180,160,140),  48.3313F, 3.24587e-5F,  7.0047F, 5.00e-8F,  29.1241F, 1.01444e-5F,  0.387098F, 0.0F,       0.205635F,  5.59e-10F,   168.6562F, 4.0923344368F },
    { "Venus",   RGB565(230,220,170),  76.6799F, 2.46590e-5F,  3.3946F, 2.75e-8F,  54.8910F, 1.38374e-5F,  0.723330F, 0.0F,       0.006773F, -1.302e-9F,    48.0052F, 1.6021302244F },
    { "Mars",    RGB565(230,80,50),    49.5574F, 2.11081e-5F,  1.8497F,-1.78e-8F, 286.5016F, 2.92961e-5F,  1.523688F, 0.0F,       0.093405F,  2.516e-9F,    18.6021F, 0.5240207766F },
    { "Jupiter", RGB565(210,150,90),  100.4542F, 2.76854e-5F,  1.3030F,-1.557e-7F,273.8777F, 1.64505e-5F,  5.20256F,  0.0F,       0.048498F,  4.469e-9F,    19.8950F, 0.0830853001F },
    { "Saturn",  RGB565(220,200,130), 113.6634F, 2.38980e-5F,  2.4886F,-1.081e-7F,339.3939F, 2.97661e-5F,  9.55475F,  0.0F,       0.055546F, -9.499e-9F,   316.9670F, 0.0334442282F },
    { "Uranus",  RGB565(140,220,230),  74.0005F, 1.3978e-5F,   0.7733F, 1.9e-8F,   96.6612F, 3.0565e-5F,  19.18171F,-1.55e-8F,    0.047318F,  7.45e-9F,    142.5905F, 0.011725806F },
    { "Neptune", RGB565(80,110,230),  131.7806F, 3.0173e-5F,   1.7700F,-2.55e-7F, 272.8461F,-6.027e-6F,   30.05826F, 3.313e-8F,   0.008606F,  2.15e-9F,    260.2471F, 0.005995147F },
};

// Sun's orbital elements (equivalently, Earth's), same method/epoch, needed to convert
// heliocentric planet positions to geocentric
#define SUN_w0  282.9404F
#define SUN_wd  4.70935e-5F
#define SUN_a   1.000000F
#define SUN_e0  0.016709F
#define SUN_ed  -1.151e-9F
#define SUN_M0  356.0470F
#define SUN_Md  0.9856002585F
#define OBLECL0 23.4393F                                  // obliquity of the ecliptic
#define OBLECLd -3.563e-7F

static SBox planet_btn_b[N_PLANETS];                      // last-drawn marker box, for tap testing
static bool planet_visible[N_PLANETS];                    // currently above PLANET_MIN_EL


/* return days since 1999-12-31 00:00 UT (the epoch these elements are referenced to),
 * matching the standard "Schlyter" convention
 */
static double daysSinceEpoch (time_t t)
{
    // JD of 1999-12-31 00:00 UT = 2451543.5; unix epoch (1970-01-01 00:00 UT) = JD 2440587.5
    double jd = t/86400.0 + 2440587.5;
    return (jd - 2451543.5);
}

/* solve Kepler's equation E - e*sin(E) = M for E, both in degrees, e dimensionless
 */
static float solveKepler (float M_deg, float e)
{
    float M = deg2rad (M_deg);
    float E = M + e*sinf(M)*(1.0F + e*cosf(M));
    for (int i = 0; i < 5; i++) {
        float dE = (E - e*sinf(E) - M) / (1.0F - e*cosf(E));
        E -= dE;
        if (fabsf(dE) < 1e-6F)
            break;
    }
    return (E);                                           // radians
}

/* compute heliocentric ecliptic rectangular coords (AU) and heliocentric distance for one body
 * given its orbital elements evaluated at d days since epoch
 */
static void heliocentric (float N, float inc, float w, float a, float e, float M,
                            float &xh, float &yh, float &zh, float &r)
{
    float E = solveKepler (M, e);
    float xv = a*(cosf(E) - e);
    float yv = a*(sqrtf(1.0F-e*e)*sinf(E));
    r = sqrtf(xv*xv + yv*yv);
    float v = atan2f (yv, xv);

    float Nr = deg2rad(N), ir = deg2rad(inc), vw = v + deg2rad(w);

    xh = r*(cosf(Nr)*cosf(vw) - sinf(Nr)*sinf(vw)*cosf(ir));
    yh = r*(sinf(Nr)*cosf(vw) + cosf(Nr)*sinf(vw)*cosf(ir));
    zh = r*(sinf(vw)*sinf(ir));
}

/* full "now" circumstances for one planet
 */
typedef struct {
    float az_deg, el_deg;                                 // topocentric, at DE
    float ra_deg, dec_deg;                                 // geocentric equatorial, deg
    float dist_au;                                         // geocentric distance, AU
    float mag;                                              // apparent magnitude
    float illum_pct;                                        // illuminated fraction, percent
    float sub_lat, sub_lng;                                 // sub-point, degrees
} PlanetNow;

/* compute pn for planet pid at time t, observed from DE
 */
static void computePlanet (PlanetId pid, time_t t, PlanetNow &pn)
{
    double d = daysSinceEpoch (t);
    const PlanetElements &pe = planet_els[pid];

    float N = pe.N0 + pe.Nd*d;
    float inc = pe.i0 + pe.id*d;
    float w = pe.w0 + pe.wd*d;
    float a = pe.a0 + pe.ad*d;
    float e = pe.e0 + pe.ed*d;
    float M = fmodf (pe.M0 + pe.Md*d, 360.0F);

    float xh, yh, zh, r;
    heliocentric (N, inc, w, a, e, M, xh, yh, zh, r);

    // Sun/Earth: N=0, i=0 so heliocentric == already in ecliptic plane
    float sM = fmodf (SUN_M0 + SUN_Md*d, 360.0F);
    float sE = solveKepler (sM, SUN_e0 + (float)(SUN_ed*d));
    float sw = SUN_w0 + SUN_wd*d;
    float sxv = SUN_a*(cosf(sE) - (SUN_e0+(float)(SUN_ed*d)));
    float syv = SUN_a*(sqrtf(1.0F-(SUN_e0+(float)(SUN_ed*d))*(SUN_e0+(float)(SUN_ed*d)))*sinf(sE));
    float sr = sqrtf(sxv*sxv+syv*syv);
    float sv = atan2f (syv, sxv);
    float slon = sv + deg2rad(sw);                        // Sun's true geocentric ecliptic longitude
    float xs = sr*cosf(slon);
    float ys = sr*sinf(slon);

    // geocentric ecliptic coords of the planet
    float xg = xh + xs;
    float yg = yh + ys;
    float zg = zh;
    float rg = sqrtf(xg*xg + yg*yg + zg*zg);

    // equatorial (RA/Dec)
    float oblecl = deg2rad (OBLECL0 + OBLECLd*d);
    float xe = xg;
    float ye = yg*cosf(oblecl) - zg*sinf(oblecl);
    float ze = yg*sinf(oblecl) + zg*cosf(oblecl);
    float ra = atan2f (ye, xe);
    float dec = atan2f (ze, sqrtf(xe*xe+ye*ye));

    // Greenwich Mean Sidereal Time via Schlyter's shortcut: GMST0 = Ls + 180 deg, Ls = sun's
    // mean longitude = sM + sw (approx, using mean anomaly rather than true -- fine at this
    // precision), then add UT in sidereal degrees/hour
    float Ls = fmodf (sM + sw, 360.0F);
    float GMST0 = fmodf (Ls + 180.0F, 360.0F);
    float ut_hrs = hour(t) + minute(t)/60.0F + second(t)/3600.0F;
    float GMST = fmodf (GMST0 + ut_hrs*15.041069F, 360.0F);
    float gha = deg2rad (GMST) - ra;

    // topocentric az/el at DE
    float lat = de_ll.lat, ha = gha + deg2rad(de_ll.lng_d);
    // N.B. gha above is Greenwich HA; local HA = GHA + observer longitude (east +)
    float sinAlt = sinf(dec)*sinf(lat) + cosf(dec)*cosf(lat)*cosf(ha);
    float alt = asinf (sinAlt > 1 ? 1 : sinAlt < -1 ? -1 : sinAlt);
    float cosAz = (sinf(dec) - sinf(lat)*sinAlt) / (cosf(lat)*cosf(alt) + 1e-9F);
    cosAz = cosAz > 1 ? 1 : cosAz < -1 ? -1 : cosAz;
    float az = acosf (cosAz);
    if (sinf(ha) > 0)
        az = 2*M_PIF - az;

    // phase angle and magnitude
    float cospsi = (r*r + rg*rg - sr*sr) / (2*r*rg + 1e-9F);
    cospsi = cospsi > 1 ? 1 : cospsi < -1 ? -1 : cospsi;
    float FV = rad2deg (acosf (cospsi));                  // phase angle, degrees

    float mag;
    switch (pid) {
    case PL_MERCURY: mag = -0.36F + 5*log10f(r*rg) + 0.027F*FV + 2.2e-13F*powf(FV,6); break;
    case PL_VENUS:   mag = -4.34F + 5*log10f(r*rg) + 0.013F*FV + 4.2e-7F*powf(FV,3); break;
    case PL_MARS:    mag = -1.51F + 5*log10f(r*rg) + 0.016F*FV; break;
    case PL_JUPITER: mag = -9.25F + 5*log10f(r*rg) + 0.014F*FV; break;
    case PL_SATURN:  mag = -8.88F + 5*log10f(r*rg) + 0.044F*FV; break;
    case PL_URANUS:  mag = -7.19F + 5*log10f(r*rg); break;
    default:         mag = -6.87F + 5*log10f(r*rg); break;    // Neptune
    }

    pn.az_deg = rad2deg (az);
    pn.el_deg = rad2deg (alt);
    pn.ra_deg = rad2deg (ra); if (pn.ra_deg < 0) pn.ra_deg += 360;
    pn.dec_deg = rad2deg (dec);
    pn.dist_au = rg;
    pn.mag = mag;
    pn.illum_pct = (1.0F + cosf(deg2rad(FV))) / 2.0F * 100.0F;
    pn.sub_lat = pn.dec_deg;
    pn.sub_lng = -rad2deg (gha);
    if (pn.sub_lng > 180) pn.sub_lng -= 360;
    if (pn.sub_lng < -180) pn.sub_lng += 360;
}


/* *********************************************************************************************
 * drawing and interaction
 */

/* draw all currently-visible planet markers on the map. call once per main map refresh, same
 * point drawDXPedsOnMap()/drawHamsatOnMap() are called from.
 */
void drawPlanetsOnMap (void)
{
    if (!showPlanets())
        return;

    time_t t = myNow();

    for (int i = 0; i < N_PLANETS; i++) {

        PlanetNow pn;
        computePlanet ((PlanetId)i, t, pn);

        planet_visible[i] = (pn.el_deg >= PLANET_MIN_EL);
        if (!planet_visible[i])
            continue;

        LatLong ll (pn.sub_lat, pn.sub_lng);
        SCoord sc;
        ll2s (ll, sc, PLANET_R+1);
        if (sc.x == 0 && sc.y == 0)
            continue;                                       // off zoomed/rotated map

        tft.fillCircle (sc.x, sc.y, PLANET_R, planet_els[i].color);
        tft.drawCircle (sc.x, sc.y, PLANET_R, RA8875_BLACK);

        planet_btn_b[i].x = sc.x - PLANET_R;
        planet_btn_b[i].y = sc.y - PLANET_R;
        planet_btn_b[i].w = 2*PLANET_R;
        planet_btn_b[i].h = 2*PLANET_R;
    }
}

/* show a small popup with pn's current circumstances for the named planet, dismissed by any
 * subsequent tap -- same self-contained popup convention used elsewhere (e.g. satsked.cpp)
 */
static void showPlanetPopup (int pid, const SCoord &s)
{
    PlanetNow pn;
    computePlanet ((PlanetId)pid, myNow(), pn);

    SBox popup_b;
    popup_b.w = 150;
    popup_b.h = 80;
    popup_b.x = s.x;
    popup_b.y = s.y;
    if (popup_b.x + popup_b.w > tft.width())
        popup_b.x = tft.width() - popup_b.w;
    if (popup_b.y + popup_b.h > tft.height())
        popup_b.y = tft.height() - popup_b.h;

    fillSBox (popup_b, RA8875_BLACK);
    drawSBox (popup_b, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (planet_els[pid].color);
    tft.setCursor (popup_b.x+4, popup_b.y+2);
    tft.print (planet_els[pid].name);

    char buf[40];
    tft.setTextColor (RA8875_WHITE);

    snprintf (buf, sizeof(buf), "Az %.0f  El %.0f", pn.az_deg, pn.el_deg);
    tft.setCursor (popup_b.x+4, popup_b.y+14);
    tft.print (buf);

    snprintf (buf, sizeof(buf), "Dist %.3f AU", pn.dist_au);
    tft.setCursor (popup_b.x+4, popup_b.y+26);
    tft.print (buf);

    int ra_h = (int)(pn.ra_deg/15);
    int ra_m = (int)((pn.ra_deg/15 - ra_h)*60);
    snprintf (buf, sizeof(buf), "RA %02dh%02dm  Dec %.1f", ra_h, ra_m, pn.dec_deg);
    tft.setCursor (popup_b.x+4, popup_b.y+38);
    tft.print (buf);

    snprintf (buf, sizeof(buf), "Mag %.1f", pn.mag);
    tft.setCursor (popup_b.x+4, popup_b.y+50);
    tft.print (buf);

    snprintf (buf, sizeof(buf), "Illum %.0f%%", pn.illum_pct);
    tft.setCursor (popup_b.x+4, popup_b.y+62);
    tft.print (buf);

    tft.drawPR();

    drainTouch ();
    UserInput dismiss_ui = {
        map_b, UI_UFuncNone, UF_UNUSED, UI_NOTIMEOUT, UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };
    waitForUser (dismiss_ui);

    // erase just the popup box and redraw anything that overlays it, synchronously.
    // N.B. do NOT use scheduleFreshMap()/scheduleMapRedraw() here: those only redraw overlay
    // symbols (planet dots, DX/sat markers, paths, etc) once, at the end of a multi-frame,
    // row-by-row sweep of the whole map. If the user taps open/closed another planet popup
    // before that sweep finishes, the sweep restarts from scratch and the symbol redraw never
    // gets to run, which is what caused the overlay data to disappear (partially or completely).
    redrawMapBox (popup_b);
}

/* check for a tap on a planet marker. return whether handled.
 */
bool checkPlanetMapTouch (const SCoord &s)
{
    if (!showPlanets())
        return (false);

    for (int i = 0; i < N_PLANETS; i++) {
        if (planet_visible[i] && inBox (s, planet_btn_b[i])) {
            showPlanetPopup (i, s);
            return (true);
        }
    }

    return (false);
}
