/* DO NOT HAND EDIT THIS FILE.
 * built using ./mkbands.pl from DX Cluster bands.pl Fri Jan 31 22:51:56 2025Z
 */

#include "HamClock.h"


// misc info per band
typedef struct {
    int band_meters;                            // band, meters
    const char *name;                           // name with m suffix
    int bandes_idx;                             // band_es[] index of first entry for this band
    ColorSelection cid;                         // color id
} BandInfo;

#define X(a,b,c,d,e) {b,c,d,e},                 // expands SUPPORTED_BANDS to each BandInfo
static const BandInfo band_info[] = {
    SUPPORTED_BANDS
};
#undef X


// each subband
typedef struct {
    int band_meters;                            // integer band
    const char *mode;                           // subband name, or BAND for full band
    float min_kHz, max_kHz;
} BandEdge;

static const BandEdge band_es[] = {
    {   2, "BAND",    144000, 148000},
    {   2, "BEACON",  144400, 144490},
    {   2, "CW",      144000, 144150},
    {   2, "SSB",     144150, 144400},
    {   6, "BAND",     50000,  54000},
    {   6, "BEACON",   50000,  50100},
    {   6, "BEACON",   50400,  50500},
    {   6, "CW",       50000,  50100},
    {   6, "DATA",     50300,  50500},
    {   6, "FT4",      50318,  50321},
    {   6, "FT8",      50313,  50316},
    {   6, "SSB",      50100,  50400},
    {  10, "BAND",     28000,  29700},
    {  10, "BEACON",   28190,  28225},
    {  10, "BEACON",   28225,  28300},
    {  10, "CW",       28000,  28198},
    {  10, "DATA",     28050,  28149},
    {  10, "DATA",     29200,  29299},
    {  10, "DATA",     28180,  28183},
    {  10, "FT4",      28180,  28183},
    {  10, "FT8",      28074,  28077},
    {  10, "RTTY",     28050,  28149},
    {  10, "SPACE",    29200,  29300},
    {  10, "SSB",      28201,  29299},
    {  10, "SSB",      29550,  29700},
    {  12, "BAND",     24890,  24990},
    {  12, "BEACON",   24929,  24931},
    {  12, "CW",       24890,  24930},
    {  12, "DATA",     24920,  24929},
    {  12, "DATA",     24915,  24918},
    {  12, "FT4",      24919,  24922},
    {  12, "FT8",      24915,  24918},
    {  12, "RTTY",     24920,  24929},
    {  12, "SSB",      24931,  24990},
    {  15, "BAND",     21000,  21450},
    {  15, "BEACON",   21149,  21151},
    {  15, "CW",       21000,  21150},
    {  15, "DATA",     21070,  21119},
    {  15, "DATA",     21140,  21143},
    {  15, "FT4",      21140,  21143},
    {  15, "FT8",      21074,  21077},
    {  15, "RTTY",     21070,  21119},
    {  15, "SSB",      21151,  21450},
    {  17, "BAND",     18068,  18168},
    {  17, "BEACON",   18109,  18111},
    {  17, "CW",       18068,  18100},
    {  17, "DATA",     18100,  18108},
    {  17, "FT4",      18104,  18107},
    {  17, "FT8",      18100,  18103},
    {  17, "RTTY",     18101,  18108},
    {  17, "SSB",      18111,  18168},
    {  20, "BAND",     14000,  14350},
    {  20, "BEACON",   14099,  14101},
    {  20, "CW",       14000,  14100},
    {  20, "DATA",     14070,  14098},
    {  20, "DATA",     14101,  14111},
    {  20, "FT4",      14080,  14083},
    {  20, "FT8",      14074,  14077},
    {  20, "RTTY",     14070,  14098},
    {  20, "RTTY",     14101,  14111},
    {  20, "SSB",      14101,  14350},
    {  20, "SSTV",     14225,  14235},
    {  30, "BAND",     10100,  10150},
    {  30, "CW",       10100,  10130},
    {  30, "DATA",     10141,  10149},
    {  30, "DATA",     10131,  10134},
    {  30, "DATA",     10140,  10143},
    {  30, "FT4",      10140,  10143},
    {  30, "FT8",      10131,  10134},
    {  30, "RTTY",     10141,  10149},
    {  40, "BAND",      7000,   7300},
    {  40, "CW",        7000,   7040},
    {  40, "DATA",      7040,   7100},
    {  40, "FT4",       7047,   7051},
    {  40, "FT8",       7074,   7077},
    {  40, "RTTY",      7040,   7060},
    {  40, "SSB",       7080,   7300},
    {  60, "BAND",      5195,   5450},
    {  60, "CW",        5258,   5264},
    {  60, "CW",        5351,   5366},
    {  60, "DATA",      5366,   5467},
    {  60, "SSB",       5276,   5410},
    {  80, "BAND",      3500,   4000},
    {  80, "CW",        3500,   3600},
    {  80, "DATA",      3570,   3619},
    {  80, "FT4",       3575,   3578},
    {  80, "FT8",       3573,   3576},
    {  80, "RTTY",      3580,   3619},
    {  80, "SSB",       3601,   4000},
    {  80, "SSTV",      3730,   3740},
    { 160, "BAND",      1800,   2000},
    { 160, "CW",        1800,   1840},
    { 160, "DATA",      1838,   1843},
    { 160, "FT8",       1840,   1843},
    { 160, "RTTY",      1838,   1841},
    { 160, "SSB",       1831,   2000},
    // 630m -- appended here (not sorted in among the other bands) to match band_info[]'s
    // own append-only ordering (see SUPPORTED_BANDS in HamClock.h) and to keep every other
    // band's bandes_idx unchanged. Only the overall allocation is listed -- CW/FT8/WSPR
    // sub-band edges deliberately omitted rather than guessed here; this whole file is
    // machine-generated from an external bands.pl source (see file header) that this repo
    // doesn't have -- get the real sub-edges from mkbands.pl's actual source data instead
    // of hand-adding them, the way every other row here was produced.
    { 630, "BAND",       472,    479},
    // 4m and 2200m -- appended for the same reason as 630m just above: keeps every prior
    // band's bandes_idx unchanged, and only the overall allocation is listed since this repo
    // has no access to mkbands.pl's real source data for sub-band edges. 4m's allocation
    // varies by country/region; 70.0-70.5 MHz is the widest commonly-used allocation (eg IARU
    // Region 1). 2200m is the internationally recognized LF ham allocation, 135.7-137.8 kHz.
    {   4, "BAND",     70000,  70500},
    {2200, "BAND",     135.7,  137.8},
};

#define N_BE NARRAY(band_es)


static bool isValidHBS (HamBandSetting h)
{
    return ((int)h >= 0 && (int)h < HAMBAND_N);
}

/* return whether the given mode is in _any_ band
 * used to test for general validity of mode, not whether it is defined for a given band.
 */
bool isValidSubBand (const char *mode)
{
    for (int i = 0; i < N_BE; i++)
        if (strcasecmp (mode, band_es[i].mode) == 0)
            return (true);
    return (false);
}

/* given HamBandSetting and optional mode, pass back up to n_kHz mode edges and count.
 */
int findBandEdges (HamBandSetting h, const char *mode, float min_kHz[], float max_kHz[], int n_kHz)
{
    if (!isValidHBS(h))
        fatalError ("bug! HamBandSetting %d", (int)h);

    const BandInfo &bh = band_info[h];

    int n_found = 0;
    for (int i = bh.bandes_idx; i < N_BE; i++) {
        const BandEdge &be = band_es[i];
        if (be.band_meters == bh.band_meters) {
            if ((mode && strcasecmp (be.mode, mode) == 0) || (!mode && strcmp (be.mode, "BAND") == 0)) {
                if (n_found < n_kHz) {
                    min_kHz[n_found] = be.min_kHz;
                    max_kHz[n_found] = be.max_kHz;
                    n_found++;
                } else {
                    fatalError ("findBandEdges: too few edges: %d", n_kHz);
                }
            }
        } else {
            break;
        }
    }

    return (n_found);
}

/* given band in meters return one of HamBandSetting if possible else HAMBAND_NONE
 */
HamBandSetting findHamBand (int meters)
{
    for (int i = 0; i < HAMBAND_N; i++)
        if (band_info[i].band_meters == meters)
            return ((HamBandSetting)i);
    return (HAMBAND_NONE);
}

/* given freq in kHz return one of HamBandSetting if contained therein else HAMBAND_NONE
 */
HamBandSetting findHamBand (float kHz)
{
    // find band containing kHz in meters
    int meters = -1;
    for (int i = 0; i < N_BE; i++) {
        const BandEdge &be = band_es[i];
        if (strcmp (be.mode, "BAND") == 0 && be.min_kHz <= kHz && kHz <= be.max_kHz) {
            meters = be.band_meters;
            break;
        }
    }
    if (meters < 0)
        return (HAMBAND_NONE);

    return (findHamBand (meters));
}

/* given HamBandSetting return the corresponding ColorSelection
 */
ColorSelection findColSel (HamBandSetting h)
{
    if (!isValidHBS(h))
        fatalError ("bug! findColSel %d", (int)h);
    return (band_info[h].cid);
}

/* given HamBandSetting return the corresponding name
 */
const char *findBandName (HamBandSetting h)
{
    if (!isValidHBS(h))
        fatalError ("bug! findBandName %d", (int)h);
    return (band_info[h].name);
}

/* given a freq in kHz return pointer to static string of the best estimate of the mode.
 * or return "" if "BAND" or none.
 */
const char *findHamMode (float kHz)
{
    const BandEdge *best_be = NULL;
    float min_range = 1e10F;

    for (int i = 0; i < N_BE; i++) {
        const BandEdge *bep = &band_es[i];
        float range = bep->max_kHz - bep->min_kHz;
        if (bep->min_kHz <= kHz && kHz <= bep->max_kHz && range < min_range) {
            best_be = bep;
            min_range = range;
        }
    }

    if (best_be && strcmp (best_be->mode, "BAND"))
        return (best_be->mode);
    else
        return ("");
}

/* return whether the given frequency is within the given mode range.
 */
bool testBandMode (float kHz, const char *mode)
{
    for (int i = 0; i < N_BE; i++) {
        const BandEdge &be = band_es[i];
        if (be.min_kHz <= kHz && kHz <= be.max_kHz && strcasecmp (be.mode, mode) == 0)
            return (true);
    }
    return (false);
}
