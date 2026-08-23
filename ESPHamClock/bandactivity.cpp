/* Band Activity heatmap pane.
 *
 * Downloads band_activity.txt (see gen_bandactivity.pl) -- a small file of
 * "band,continent,count" lines, aggregated centrally by OHB from Spothole's
 * broad spot feed -- and renders it as a grid: bands down the rows,
 * continents across the columns, each cell's color intensity showing how
 * much distinct-station activity that combination has seen in the last 30
 * minutes.
 *
 * Deliberately NOT built from this instance's own DX Cluster pane: that
 * would be empty for anyone without a DX Cluster configured, and would vary
 * wildly between users depending on which cluster they happen to connect
 * to. Downloading a centrally-aggregated file instead means this pane shows
 * the same global picture regardless of what else is or isn't configured --
 * same reasoning ONTA already relies on for POTA/SOTA/WWFF/etc.
 */

#include "HamClock.h"

#define BANDACT_COLOR    RGB565(255,140,0)      // title color -- warm amber, matches the "hot" end
                                                  // of the heatmap's own color ramp so the title
                                                  // reads as an extension of the same visual idea
#define BANDACT_BASE     RGB565(20,20,30)        // "cold"/empty cell color -- dark blue-grey, not
                                                  // pure black, so the grid's structure (every cell
                                                  // is still a visible rectangle) stays legible even
                                                  // when there's no activity at all to show
#define BANDACT_HEAT     RGB565(255,140,0)       // "hot"/maximum cell color -- same amber as the title
#define BANDACT_FLOOR_OP 0.25                    // minimum opacity for ANY nonzero count, however
                                                  // small -- see bandactOpacity() for why a floor
                                                  // matters here

static const char bandact_page[] = "/ONTA/band_activity.txt";
static const char bandact_file[] = "band_activity.txt";

// canonical band list and display order -- classic HF (160m-10m) plus 6m, by deliberate
// choice: SUPPORTED_BANDS in HamClock.h also includes 2m, but this pane stops at 6m on
// purpose -- narrower VHF/UHF/satellite bands (2m, 70cm, 23cm, ...) see too little
// cluster/RBN spot traffic to make a meaningful "how active is this band right now" cell,
// and would mostly just add more near-always-dark rows. band_activity.txt's own band
// strings (Spothole's, eg "20m") are matched against this list at parse time; anything
// that doesn't match -- including real activity on bands outside this list, eg the 70cm/
// 23cm/4m/Unknown lines gen_bandactivity.pl can legitimately produce -- is silently
// dropped rather than growing the grid to fit it, keeping the grid a fixed, predictable
// shape release to release instead of its row count jittering based on what happened to
// be active this run. widen this list (matching more of SUPPORTED_BANDS, or beyond it)
// if VHF/UHF/satellite activity ever becomes worth showing here.
static const char *bandact_bands[] = { "160","80","60","40","30","20","17","15","12","10","6" };
#define N_BANDACT_BANDS  NARRAY(bandact_bands)

// canonical continent list and display order -- Spothole's own 2-letter continent codes,
// passed through as-is; not otherwise used/duplicated anywhere in HamClock so no existing
// canonical list to defer to the way bands has one.
static const char *bandact_continents[] = { "NA","SA","EU","AF","AS","OC","AN" };
#define N_BANDACT_CONTINENTS NARRAY(bandact_continents)

static uint32_t bandact_counts[N_BANDACT_BANDS][N_BANDACT_CONTINENTS];  // 0 until first fetch
static time_t bandact_generated;                                        // server's generation time,
                                                                          // from the file's own header
static bool bandact_have_data;                                          // false until first
                                                                          // successful parse

/* find the row index for a band_activity.txt band string (eg "20m"), or -1 if it's not one
 * of bandact_bands[] -- see that array's own comment for why a non-matching band is dropped
 * rather than grown into.
 */
static int bandactBandRow (const char *band_str)
{
    // strip a trailing 'm' the same way band_activity.txt's source (Spothole) always writes
    // its band field, eg "20m" -> "20"; tolerate its absence too, just in case
    char stripped[8];
    quietStrncpy (stripped, band_str, sizeof(stripped));
    size_t l = strlen (stripped);
    if (l > 0 && (stripped[l-1] == 'm' || stripped[l-1] == 'M'))
        stripped[l-1] = '\0';

    for (size_t i = 0; i < N_BANDACT_BANDS; i++)
        if (!strcasecmp (stripped, bandact_bands[i]))
            return ((int)i);
    return (-1);
}

/* find the column index for a continent code, or -1 if unrecognized
 */
static int bandactContinentCol (const char *cont_str)
{
    for (size_t i = 0; i < N_BANDACT_CONTINENTS; i++)
        if (!strcasecmp (cont_str, bandact_continents[i]))
            return ((int)i);
    return (-1);
}

/* download and parse band_activity.txt into bandact_counts[][]. return whether io ok --
 * NOT whether any data was found, same "ok" convention retrieveONTASource() uses, so a
 * transient empty window (eg 3am somewhere quiet) doesn't get treated as a fetch failure.
 */
static bool retrieveBandActivity (void)
{
    FILE *fp = openCachedFile (bandact_file, bandact_page, ONTA_INTERVAL, 0);
    if (!fp)
        return (false);

    // reset -- unlike onta_spots' additive merge across sources, this file is the ENTIRE
    // picture each time, so a fresh parse should fully replace the previous one, including
    // zeroing out any cell that had activity last time but doesn't now
    memset (bandact_counts, 0, sizeof(bandact_counts));
    bandact_generated = 0;

    char line[100];
    while (fgets (line, sizeof(line), fp)) {

        chompString (line);

        if (line[0] == '#') {
            // pull generated= out of our own header line, eg
            // "#band_activity v1 generated=1787144040 window_secs=1800"
            const char *g = strstr (line, "generated=");
            if (g)
                bandact_generated = (time_t) atol (g + 10);
            continue;
        }

        if (!line[0])
            continue;

        // band,continent,count
        char *band_str = line;
        char *comma1 = strchr (band_str, ',');
        if (!comma1)
            continue;
        *comma1 = '\0';
        char *cont_str = comma1 + 1;
        char *comma2 = strchr (cont_str, ',');
        if (!comma2)
            continue;
        *comma2 = '\0';
        char *count_str = comma2 + 1;

        int row = bandactBandRow (band_str);
        int col = bandactContinentCol (cont_str);
        if (row < 0 || col < 0)
            continue;                            // unrecognized band/continent -- see header comments

        long count = atol (count_str);
        if (count < 0)
            continue;

        bandact_counts[row][col] = (uint32_t) count;
    }

    fclose (fp);

    bandact_have_data = true;
    return (true);
}

/* compute this cell's opacity in [0,1] from its count and the current max count anywhere in
 * the grid. LOG-scaled, not linear, and with a floor for any nonzero count -- both
 * deliberate:
 *   - linear scaling means one very busy band/continent (practically always 20m to EU or NA)
 *     would make every other cell round down to visually indistinguishable-from-zero, since
 *     most real activity distributions are heavily right-skewed. Log compresses that range so
 *     genuinely quiet-but-nonzero cells still show up as visibly different from truly empty.
 *   - even log-scaled, a count of 1 against a max of, say, 400 would still round to a barely
 *     perceptible few percent opacity -- indistinguishable from empty at a glance, which
 *     defeats the point of a heatmap (that's supposed to show WHERE things are happening, not
 *     just where they're happening THE MOST). BANDACT_FLOOR_OP guarantees "some activity"
 *     always reads as visibly different from "no activity", regardless of scale.
 */
static float bandactOpacity (uint32_t count, uint32_t max_count)
{
    if (count == 0)
        return (0.0F);
    if (max_count < 1)
        max_count = 1;                           // shouldn't happen if count > 0, but avoid /0
    float op = logf ((float)count + 1) / logf ((float)max_count + 1);
    if (op > 1.0F)
        op = 1.0F;
    if (op < BANDACT_FLOOR_OP)
        op = BANDACT_FLOOR_OP;
    return (op);
}

/* blend base -> heat by opacity op in [0,1], each RGB565 channel independently.
 * no native alpha channel on this display (RGB565 has none), so this does the lerp by hand:
 * unpack each 16-bit color to 8-bit R/G/B via the RGB565_R/G/B macros, interpolate each
 * channel, repack via RGB565(). same technique used to convert to/from HSV in color.cpp,
 * just linear instead of going through hue/sat/val.
 */
static uint16_t bandactBlend (uint16_t base, uint16_t heat, float op)
{
    uint8_t br = RGB565_R(base), bg = RGB565_G(base), bb = RGB565_B(base);
    uint8_t hr = RGB565_R(heat), hg = RGB565_G(heat), hb = RGB565_B(heat);
    uint8_t r = (uint8_t)(br + (hr - br) * op);
    uint8_t g = (uint8_t)(bg + (hg - bg) * op);
    uint8_t b = (uint8_t)(bb + (hb - bb) * op);
    return (RGB565(r,g,b));
}

/* draw the grid, freshly, filling the given box below its title.
 */
static void drawBandActivityGrid (const SBox &box)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);

    // freshness caption where a subtitle would normally go
    char fresh[40];
    if (bandact_generated > 0) {
        int age_min = (myNow() - bandact_generated) / 60;
        if (age_min < 1)
            snprintf (fresh, sizeof(fresh), "just now");
        else
            snprintf (fresh, sizeof(fresh), "%d m ago", age_min);
    } else
        snprintf (fresh, sizeof(fresh), "no data yet");
    uint16_t fw = getTextWidth (fresh);
    tft.setTextColor (BANDACT_COLOR);
    tft.setCursor (box.x + (box.w-fw)/2, box.y + SUBTITLE_Y0);
    tft.print (fresh);

    // layout: a label column down the left for band names, a header row across the top for
    // continent codes, then the grid itself filling whatever's left. computed from box
    // dimensions rather than hand-picked per pane size, so this adapts to both the narrow
    // data pane and the normal wider-but-shorter pane instead of needing two separate layouts
    // the way the DX Cluster settings menu did.
    const uint16_t label_col_w = 22;
    const uint16_t header_row_h = 12;
    const uint16_t grid_y0 = box.y + SUBTITLE_Y0 + 10;
    const uint16_t grid_x0 = box.x + 2 + label_col_w;
    const uint16_t grid_w = box.w - 4 - label_col_w;
    const uint16_t grid_h = box.h - (grid_y0 - box.y) - 2;
    const uint16_t cell_w = grid_w / N_BANDACT_CONTINENTS;
    const uint16_t cell_h = (grid_h - header_row_h) / N_BANDACT_BANDS;

    // continent header row
    tft.setTextColor (RA8875_WHITE);
    for (size_t c = 0; c < N_BANDACT_CONTINENTS; c++) {
        uint16_t cx = grid_x0 + c*cell_w;
        uint16_t tw = getTextWidth (bandact_continents[c]);
        tft.setCursor (cx + (cell_w>tw ? (cell_w-tw)/2 : 0), grid_y0);
        tft.print (bandact_continents[c]);
    }

    // find current max count anywhere in the grid, for bandactOpacity()'s scaling
    uint32_t max_count = 0;
    for (size_t r = 0; r < N_BANDACT_BANDS; r++)
        for (size_t c = 0; c < N_BANDACT_CONTINENTS; c++)
            if (bandact_counts[r][c] > max_count)
                max_count = bandact_counts[r][c];

    // band rows + grid cells
    for (size_t r = 0; r < N_BANDACT_BANDS; r++) {

        uint16_t ry = grid_y0 + header_row_h + r*cell_h;

        // row label
        tft.setTextColor (RA8875_WHITE);
        tft.setCursor (box.x + 2, ry + cell_h/2 - 3);
        tft.print (bandact_bands[r]);

        for (size_t c = 0; c < N_BANDACT_CONTINENTS; c++) {
            uint32_t count = bandact_counts[r][c];
            float op = bandactOpacity (count, max_count);
            uint16_t cell_color = bandactBlend (BANDACT_BASE, BANDACT_HEAT, op);
            uint16_t cx = grid_x0 + c*cell_w;
            tft.fillRect (cx, ry, cell_w-1, cell_h-1, cell_color);
        }
    }
}

/* called to draw or refresh the pane in box.
 * return whether io ok, same convention every other update*() pane function uses.
 */
bool updateBandActivity (const SBox &box, bool fresh)
{
    (void) fresh;                    // always fully redraw, see comment below

    bool ok = retrieveBandActivity();
    if (!ok)
        return (false);

    // unlike panes with scrollable/incremental content (eg dxcluster.cpp's spot list), this
    // grid is entirely regenerated from the freshly (re)parsed counts every call -- there's no
    // partial-update path, so always fully clearing first is both simplest and correct: it's
    // what prevents the "X m ago" freshness caption from ghosting digits when a shorter new
    // string doesn't fully overwrite a longer old one, same failure mode fixed elsewhere in
    // this codebase by clearing before redrawing text that can change width between updates.
    prepPlotBox (box);

    // title
    const char *title = "Band Activity";
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (BANDACT_COLOR);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x + (box.w-tw)/2, box.y + PANETITLE_H);
    tft.print (title);

    drawBandActivityGrid (box);

    return (true);
}
