/* Country-borders and states/provinces overlay.
 *
 * Vector data is NOT baked into the binary -- it's fetched once from the
 * OHB backend (same download+inflate mechanism as the file-style core maps)
 * and cached locally, then reused across restarts. Unlike Clouds/Terrain/etc
 * this data never expires (borders don't change), so there's no age check --
 * once cached, it's used indefinitely.
 *
 * Both country borders and states/provinces share one on-disk format (magic
 * "HCBD": ring count, then per-ring point count + int16 centidegree lon/lat
 * pairs) and one in-memory representation (BorderSet), so loading/fetching/
 * reprojecting/drawing is written once and used for both datasets.
 *
 * Screen-coordinate projection is cached per set and only recomputed when
 * pan/zoom/projection/map geometry actually changes (checked cheaply on
 * every call), since re-projecting tens of thousands of points on every
 * map redraw would be wasteful, especially on constrained builds. States
 * are also only meaningful at HamClock's closest zoom level -- world view
 * with every state/province outline for the whole planet is just noise --
 * so they're skipped entirely (no reprojection, no draw) at any other zoom.
 *
 * Drawing follows the same pattern as satellite ground tracks in
 * earthsat.cpp: walk consecutive vertex pairs, skip (don't draw) any
 * segment segmentSpanOkRaw() says shouldn't be connected (off map, wraps
 * around an edge, crosses an azimuthal hemisphere, etc) rather than trying
 * to clip/split polygons the way the filled CQ/ITU zones do -- borders are
 * outlines only, so a skipped segment is invisible, not wrong-looking.
 */

#include "HamClock.h"

/* return c blended most of the way toward white, for a brighter day-side rendition of whatever
 * base border/state color the user picked in Setup's color editor.
 */
static uint16_t brightenForDay (uint16_t c)
{
    uint8_t r = RGB565_R(c);
    uint8_t g = RGB565_G(c);
    uint8_t b = RGB565_B(c);
    r += (255-r)*3/4;
    g += (255-g)*3/4;
    b += (255-b)*3/4;
    return (RGB565(r,g,b));
}

/* return the night-side (base) line thickness, in pixels, for the given color selection.
 * N.B. deliberately reads the color editor's thin/thick tick box directly rather than going through
 * getRawPathWidth(), which also folds in that entry's on/off state -- borders' on/off is controlled by
 * the on-map badge and the max-zoom auto-trigger, not this page, so its color-editor on/off is disabled.
 */
static int borderBaseThick (ColorSelection id)
{
    return (getMapColorThin(id) ? 1 : 2);
}

typedef struct {
    const char *filename;              // local cache filename
    const char *url_path;              // backend path, e.g. "/maps/borders.bin.z"
    const char *log_tag;                // e.g. "borders" or "states", for Serial logging only

    float *lat;                         // radians, flat across all rings
    float *lng;                         // radians, flat across all rings
    uint16_t *ring_len;                 // point count per ring
    uint16_t n_rings;
    uint32_t n_pts;                     // total points, sum of ring_len[]
    SCoord *scr;                        // parallel projected screen coords, raw pixel scale
    bool loaded;
    time_t last_try;                    // backoff for retrying a failed fetch

    // cache guard -- avoid re-projecting when nothing relevant moved
    int16_t last_pan_x, last_pan_y;
    uint8_t last_zoom;
    uint8_t last_proj;
    SBox last_map_b;
} BorderSet;

static BorderSet bset_borders = {
    "borders.bin", "/maps/borders.bin.z", "borders",
    NULL, NULL, NULL, 0, 0, NULL, false, 0,
    INT16_MIN, INT16_MIN, 0, 0xFF, {0,0,0,0}
};

static BorderSet bset_states = {
    "states.bin", "/maps/states.bin.z", "states",
    NULL, NULL, NULL, 0, 0, NULL, false, 0,
    INT16_MIN, INT16_MIN, 0, 0xFF, {0,0,0,0}
};

/* parse an already-downloaded/cached HCBD file from disk into bs's flat arrays.
 * return whether successful.
 */
static bool parseBorderSetFile (BorderSet *bs)
{
    FILE *fp = fopenOurs (bs->filename, "r");
    if (!fp) {
        Serial.printf ("%s: not found\n", bs->filename);
        return (false);
    }

    bool ok = false;
    char magic[4];
    uint8_t version;
    uint16_t n_rings;

    if (fread (magic, 1, 4, fp) == 4 && !memcmp (magic, "HCBD", 4)
                        && fread (&version, 1, 1, fp) == 1 && version == 1
                        && fread (&n_rings, 2, 1, fp) == 1) {

        uint16_t *ring_len = (uint16_t *) malloc (n_rings * sizeof(uint16_t));
        if (!ring_len)
            fatalError ("No memory for %u %s rings", n_rings, bs->log_tag);

        // first pass: read ring lengths and total point count, seeking past coordinate data
        uint32_t total_pts = 0;
        bool sizes_ok = true;
        for (uint16_t r = 0; sizes_ok && r < n_rings; r++) {
            uint16_t npts;
            if (fread (&npts, 2, 1, fp) != 1) {
                sizes_ok = false;
                break;
            }
            ring_len[r] = npts;
            total_pts += npts;
            if (fseek (fp, npts * 4L, SEEK_CUR) != 0) {          // 2 int16 per point
                sizes_ok = false;
                break;
            }
        }

        if (sizes_ok && total_pts > 0) {
            float *lat_a = (float *) malloc (total_pts * sizeof(float));
            float *lng_a = (float *) malloc (total_pts * sizeof(float));
            SCoord *scr_a = (SCoord *) calloc (total_pts, sizeof(SCoord));
            if (!lat_a || !lng_a || !scr_a)
                fatalError ("No memory for %u %s points", total_pts, bs->log_tag);

            // second pass: rewind past header and read actual coordinates
            fseek (fp, 4+1+2, SEEK_SET);
            uint32_t pi = 0;
            bool pts_ok = true;
            for (uint16_t r = 0; pts_ok && r < n_rings; r++) {
                uint16_t npts;
                if (fread (&npts, 2, 1, fp) != 1) { pts_ok = false; break; }
                for (uint16_t p = 0; p < npts; p++) {
                    int16_t lon_cd, lat_cd;
                    if (fread (&lon_cd, 2, 1, fp) != 1 || fread (&lat_cd, 2, 1, fp) != 1) {
                        pts_ok = false;
                        break;
                    }
                    lng_a[pi] = deg2rad (lon_cd / 100.0F);
                    lat_a[pi] = deg2rad (lat_cd / 100.0F);
                    pi++;
                }
            }

            if (pts_ok && pi == total_pts) {
                bs->lat = lat_a;
                bs->lng = lng_a;
                bs->scr = scr_a;
                bs->ring_len = ring_len;
                bs->n_rings = n_rings;
                bs->n_pts = total_pts;
                ok = true;
            } else {
                free (lat_a);
                free (lng_a);
                free (scr_a);
                free (ring_len);
            }
        } else {
            free (ring_len);
        }
    } else {
        Serial.printf ("%s: bad header\n", bs->filename);
    }

    fclose (fp);
    return (ok);
}

/* one-time fetch (from local cache if present, else download from OHB backend)
 * and parse into bs. This data doesn't change, so unlike Clouds/Terrain/etc
 * there's no age check or re-download once cached.
 */
static void initBorderSet (BorderSet *bs)
{
    if (bs->loaded)
        return;

    // try local cache first
    if (parseBorderSetFile (bs)) {
        bs->loaded = true;
        Serial.printf ("%s: loaded %u rings, %u pts from local cache\n",
                                                        bs->log_tag, bs->n_rings, bs->n_pts);
        return;
    }

    // not cached (or corrupt) -- download from backend, same idea as openMapFile()
    if (!backend_host[0])
        return;

    WiFiClient client;
    if (!client.connect (backend_host, backend_port))
        return;

    Serial.printf ("%s: downloading %s\n", bs->log_tag, bs->url_path);
    httpHCGET (client, backend_host, bs->url_path);
    char c_l[100];
    bool dl_ok = false;
    if (httpSkipHeader (client, "Content-Length: ", c_l, sizeof(c_l)))
        dl_ok = downloadZFile (client, bs->filename, atol(c_l));
    client.stop();

    if (dl_ok && parseBorderSetFile (bs)) {
        bs->loaded = true;
        Serial.printf ("%s: loaded %u rings, %u pts after download\n",
                                                        bs->log_tag, bs->n_rings, bs->n_pts);
    } else {
        Serial.printf ("%s: download/parse failed, overlay unavailable this session\n", bs->log_tag);
        unlinkOurs (bs->filename);
    }
}

/* reproject bs's cached lat/lng into current screen coords, but only if
 * something that would change the projection has actually changed since
 * last call. Also doubles as the retry point for initBorderSet() -- the
 * caller runs often enough at real startup and after connectivity is
 * established that a dedicated single boot-time call isn't needed. Backs
 * off so a persistent failure doesn't retry on every single map refresh.
 */
static void updateBorderSet (BorderSet *bs)
{
    if (!bs->loaded) {
        time_t now = myNow();
        if (now - bs->last_try < 60)
            return;
        bs->last_try = now;
        initBorderSet (bs);
        if (!bs->loaded)
            return;
    }

    if (pan_zoom.pan_x == bs->last_pan_x && pan_zoom.pan_y == bs->last_pan_y
                        && pan_zoom.zoom == bs->last_zoom && map_proj == bs->last_proj
                        && map_b.x == bs->last_map_b.x && map_b.y == bs->last_map_b.y
                        && map_b.w == bs->last_map_b.w && map_b.h == bs->last_map_b.h)
        return;                 // nothing relevant changed, skip the recompute

    for (uint32_t i = 0; i < bs->n_pts; i++)
        ll2sRaw (bs->lat[i], bs->lng[i], bs->scr[i], 1);

    bs->last_pan_x = pan_zoom.pan_x;
    bs->last_pan_y = pan_zoom.pan_y;
    bs->last_zoom = pan_zoom.zoom;
    bs->last_proj = map_proj;
    bs->last_map_b = map_b;
}

/* return whether the given border point (lat/lng, radians) currently falls in the sunlit half
 * of the map. Same subsolar-angle test drawMapCoord() uses to shade the map itself, just reduced
 * to a plain day/night bool since borders only need to pick between two line styles, not blend a
 * full grayline gradient. If night shading is off entirely, the whole map reads as "day" so borders
 * do too, matching what the user actually sees.
 */
static bool borderPtIsDay (float lat, float lng)
{
    if (!night_on)
        return (true);
    float cos_t = ssslat*sinf(lat) + csslat*cosf(lat)*cosf(sun_ss_ll.lng-lng);
    return (cos_t > 0);
}

/* draw bs's overlay from its cached projected points, using day_color/day_thick over the sunlit
 * half of the map and night_color/night_thick over the night (or twilight) half, so the overlay
 * stays visible against the brighter daytime map imagery.
 */
static void drawBorderSet (BorderSet *bs, uint16_t day_color, uint16_t night_color,
                            uint16_t day_thick, uint16_t night_thick)
{
    if (!bs->loaded)
        return;

    uint32_t pi = 0;
    for (uint16_t r = 0; r < bs->n_rings; r++) {
        uint16_t npts = bs->ring_len[r];
        for (uint16_t p = 0; p < npts; p++) {
            uint32_t i0 = pi + p;
            uint32_t i1 = pi + (p+1) % npts;             // wrap last vertex back to first, closed ring
            if (segmentSpanOkRaw (bs->scr[i0], bs->scr[i1], 1)) {
                bool day = borderPtIsDay (bs->lat[i0], bs->lng[i0]);
                tft.drawLineRaw (bs->scr[i0].x, bs->scr[i0].y, bs->scr[i1].x, bs->scr[i1].y,
                                  day ? day_thick : night_thick, day ? day_color : night_color);
            }
        }
        pi += npts;
    }
}

/* one-time fetch/load for the country-borders set. Called from HamClock.h's
 * extern decl; kept for API compatibility even though updateCountryBorders()
 * (which runs far more often) already self-heals via updateBorderSet().
 */
void initCountryBorders(void)
{
    initBorderSet (&bset_borders);
}

/* reproject both sets as needed. Skipped entirely -- no fetch kick-off, no
 * reprojection -- unless Clouds or Terrain is the active core map (see
 * drawCountryBorders() for why). States are additionally skipped unless
 * already loaded or at max zoom, so a user who never zooms all the way in
 * never pays for it beyond the one initial (tiny) download.
 */
void updateCountryBorders(void)
{
    if (core_map != CM_CLOUDS && core_map != CM_TERRAIN)
        return;

    updateBorderSet (&bset_borders);

    if (bset_states.loaded || pan_zoom.zoom == MAX_ZOOM)
        updateBorderSet (&bset_states);
}

/* draw whichever overlays are enabled and applicable at the current zoom.
 * Both are gated on the single "Borders" on-map badge -- states are an
 * automatic extra level of detail at max zoom, not a separate toggle.
 *
 * Only applies to Clouds and Terrain. DRAP, Aurora, Weather and Tropo still
 * get borders baked in server-side (see OHB's update_*_maps.sh) so older
 * HamClock builds without this overlay still show borders on them -- drawing
 * the vector overlay there too would just double up the lines.
 */
void drawCountryBorders(void)
{
    if (core_map != CM_CLOUDS && core_map != CM_TERRAIN)
        return;

    if (!borders_on)
        return;

    uint16_t bclr_night = getMapColor (BORDERS_CSPR);
    uint16_t bclr_day = brightenForDay (bclr_night);
    int bthick_night = borderBaseThick (BORDERS_CSPR);

    drawBorderSet (&bset_borders, bclr_day, bclr_night, bthick_night+1, bthick_night);

    if (pan_zoom.zoom == MAX_ZOOM) {
        uint16_t sclr_night = getMapColor (STATES_CSPR);
        uint16_t sclr_day = brightenForDay (sclr_night);
        int sthick_night = borderBaseThick (STATES_CSPR);

        drawBorderSet (&bset_states, sclr_day, sclr_night, sthick_night+1, sthick_night);
    }
}

/* return whether the on-map "Borders On/Off" badge should currently be shown.
 * Only offered on Clouds/Terrain, and only in the Mercator or Robinson projections -- the badge
 * is just a UI convenience, not the source of truth, so hiding it never touches borders_on: whatever
 * the user last set stays in effect (and keeps drawing, per drawCountryBorders() above) even while
 * the badge itself is off-screen, e.g. after switching to an azimuthal projection.
 */
bool bordersBadgeVisible(void)
{
    return ((core_map == CM_CLOUDS || core_map == CM_TERRAIN)
                        && (map_proj == MAPP_MERCATOR || map_proj == MAPP_ROB));
}
