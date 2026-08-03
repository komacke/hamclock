/* wefax.cpp -- on-demand WEFAX chart viewer, proxied and cached via OHB.
 *
 * Adds a "WEFAX" badge that shares the on-map badge slot next to the
 * View button with the Borders badge (the two are mutually exclusive: this
 * one is only shown while core_map == CM_WX, Borders only for CM_CLOUDS/
 * CM_TERRAIN, and core_map can only be one value at a time).
 *
 * Pressing the badge takes over map_b exactly the way satsked.cpp's
 * drawSatGroupSchedule() does for the satellite pass table: DE/DX info and
 * the side panes keep running underneath, only the map area itself is
 * replaced, and the normal map redraws when the user leaves.
 *
 * This module does NOT fetch real WEFAX radio broadcasts -- it fetches the
 * same chart images the agencies already publish on the web (the identical
 * GIFs that get transmitted over HF), via OHB. OHB is responsible for:
 *   - knowing each provider/region/product's real source URL and issue schedule
 *   - fetching the source image and converting it to a 565 BMP on demand
 *   - caching per (provider,region,product) key until the next scheduled
 *     issue time, and coalescing concurrent requests for the same key so
 *     N clients asking for the same chart in the same TTL window cost OHB
 *     exactly one upstream fetch
 *
 * OHB endpoint:
 *   GET /ham/HamClock/wefax/chart.pl?provider=P&region=R&product=Q
 *   Response: zlib-deflated 16bpp BMP, arbitrary WxH. Fit mode fits the whole
 *   chart into the box via installBMPBox()'s FIT_RESIZE (unlike SDO's square
 *   disk image, a fax chart's edges carry real data -- isobars and station
 *   plots run to the border -- so cropping would lose content; shrinking to
 *   fit keeps everything visible, just smaller). Zoomed mode shows native
 *   pixel size instead, with tap-to-recenter panning -- see the "zoom + pan"
 *   section below for why that needs its own decode path.
 */

#include "HamClock.h"


// ---------------------------------------------------------------------
// provider / region / product tables -- extend as more agencies are added.
// kept deliberately small: a big menu of every product an agency publishes
// just invites cold, rarely-used cache entries on OHB for little benefit.
// ---------------------------------------------------------------------

typedef struct {
    const char *id;              // short id sent to OHB, eg "OPC"
    const char *name;             // display name
} WefaxProvider;

typedef struct {
    const char *provider_id;      // which WefaxProvider this belongs to
    const char *id;                // short id sent to OHB, eg "atl"
    const char *name;              // display name, eg "N Atlantic"
    float ctr_lat, ctr_lng;        // rough coverage centroid, for a sane default pick
    uint8_t avail_products;        // bitmask, bit i set means wefax_products[i] exists for
                                    // this region on the backend -- keep in sync with
                                    // wefax_chart.pl's %SOURCES, that table is the real
                                    // source of truth this mirrors
} WefaxRegion;

typedef struct {
    const char *id;                // short id sent to OHB, eg "sfcanal"
    const char *name;              // display name, eg "Surface Analysis"
} WefaxProduct;

static const WefaxProvider wefax_providers[] = {
    { "OPC",  "NOAA/OPC" },     // Ocean Prediction Center -- N Atlantic/Pacific (US broadcast)
    { "UKMO", "Met Office" },   // UK Met Office/Northwood -- N Atlantic/Europe. Name matches the
                                // Open Government Licence's own suggested attribution wording
                                // ("Content supplied by the Met Office"), since the region cycler
                                // below displays "<provider name> <region name>" directly next to
                                // the chart every time it's shown -- that satisfies the licence's
                                // "prominent acknowledgment... adjacent to the content" requirement
                                // without needing a separate credits UI element.
};
#define N_WEFAX_PROVIDERS  NARRAY(wefax_providers)

static const WefaxRegion wefax_regions[] = {
    { "OPC",  "atl",  "N Atlantic",     35.0,  -50.0, 0x07 },   // sfcanal, wwave, f24
    { "OPC",  "pac",  "N Pacific",      35.0, -150.0, 0x07 },   // sfcanal, wwave, f24
    { "UKMO", "natl", "N Atl/Eur",      50.0,  -10.0, 0x01 },   // sfcanal only -- confirmed
                                                                  // absent on the Met Office's
                                                                  // site, see wefax_chart.pl
};
#define N_WEFAX_REGIONS  NARRAY(wefax_regions)

static const WefaxProduct wefax_products[] = {
    { "sfcanal", "Surface Analysis" },
    { "wwave",   "Wind & Wave"      },
    { "f24",     "24hr Forecast"    },
};
#define N_WEFAX_PRODUCTS  NARRAY(wefax_products)
// return whether wefax_products[prod_i] exists for wefax_regions[reg_i], per that
// region's avail_products bitmask.
static bool wefaxProductAvailable (uint8_t reg_i, uint8_t prod_i)
{
    return (wefax_regions[reg_i].avail_products & (1 << prod_i)) != 0;
}

// return the index of the first available product for the given region. every region
// is expected to have at least one bit set; falls back to 0 if that's ever not true
// rather than getting stuck, though that would itself be a config bug worth fixing.
static uint8_t wefaxFirstAvailProduct (uint8_t reg_i)
{
    for (uint8_t i = 0; i < N_WEFAX_PRODUCTS; i++)
        if (wefaxProductAvailable (reg_i, i))
            return i;
    return 0;
}

// return whether more than one product is available for the given region -- if not,
// the product cycler button has nothing to cycle to and should be hidden entirely,
// same reasoning as hiding pan arrows that can't move any further.
static bool wefaxMultipleProductsAvailable (uint8_t reg_i)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < N_WEFAX_PRODUCTS; i++)
        if (wefaxProductAvailable (reg_i, i))
            n++;
    return (n > 1);
}


// ---------------------------------------------------------------------
// state
// ---------------------------------------------------------------------

uint8_t wefax_on;                  // extern; true only while the modal viewer is on screen
SBox    wefax_btn_b;               // extern; badge box, geometry set in ESPHamClock.cpp

static uint8_t wefax_region_i;     // index into wefax_regions[], persisted
static uint8_t wefax_product_i;    // index into wefax_products[], persisted
static bool    wefax_zoomed;       // false=Fit (whole chart resized to box, small text),
                                    // true=Zoomed (native pixel size, legible text, pannable);
                                    // runtime only, resets to false each time the viewer opens
                                    // or the region/product selection changes

// full native-resolution decode, only populated while wefax_zoomed -- see decodeWefaxFull()
// for why this needs to bypass installBMPBox()/readBMPImage()'s normal fixed-center FIT_CROP
static uint16_t *wefax_full565;
static int wefax_full_w, wefax_full_h;     // dimensions of wefax_full565
static int wefax_pan_x, wefax_pan_y;       // raw-pixel top-left of the current pan window
static bool wefax_need_center;             // true right after a decode, until drawWefaxZoomed()
                                            // has centered wefax_pan_x/y for the first time
static int wefax_max_pan_x, wefax_max_pan_y;   // current clamp bounds, updated by drawWefaxZoomed()
                                                // -- lets drawWefaxPanArrows() know which arrows
                                                // would be no-ops and hide those specifically

#define WEFAX_FN_LEN   64
static char wefax_cache_fn[WEFAX_FN_LEN];

#define WEFAX_MAXAGE   (15*60)     // local cache age -- OHB is the real freshness authority,
                                    // this just avoids re-fetching on every reopen within a session
#define WEFAX_MINSIZE  100


// ---------------------------------------------------------------------
// setup / persistence
// ---------------------------------------------------------------------

/* pick the region whose coverage centroid is nearest DE, as a first-run default.
 * trivial with today's two OPC regions but stays correct as more providers/regions
 * are added -- no special-casing needed here when eg a Japan region shows up later.
 */
static uint8_t nearestWefaxRegion (void)
{
    uint8_t best_i = 0;
    float best_d = 1e9;
    for (uint8_t i = 0; i < N_WEFAX_REGIONS; i++) {
        float dlat = de_ll.lat_d - wefax_regions[i].ctr_lat;
        float dlng = de_ll.lng_d - wefax_regions[i].ctr_lng;
        // wrap to [-180,180] so eg a region centroid near +150 doesn't look
        // artificially "far" from a DE longitude just across the dateline at -150
        if (dlng > 180)  dlng -= 360;
        if (dlng < -180) dlng += 360;
        float d = dlat*dlat + dlng*dlng;            // crude, but fine for picking a default
        if (d < best_d) {
            best_d = d;
            best_i = i;
        }
    }
    return (best_i);
}

/* restore NV state at startup. call once from initial setup, same as initLightning().
 */
void initWefax (void)
{
    wefax_on = 0;                                    // never persisted -- see HamClock.h comment

    if (!NVReadUInt8 (NV_WEFAX_REGION, &wefax_region_i) || wefax_region_i >= N_WEFAX_REGIONS) {
        wefax_region_i = nearestWefaxRegion();
        NVWriteUInt8 (NV_WEFAX_REGION, wefax_region_i);
    }

    if (!NVReadUInt8 (NV_WEFAX_PRODUCT, &wefax_product_i) || wefax_product_i >= N_WEFAX_PRODUCTS) {
        wefax_product_i = 0;                          // default to Surface Analysis
        NVWriteUInt8 (NV_WEFAX_PRODUCT, wefax_product_i);
    }

    // stale NV state (eg saved before a region's availability mask existed, or before this
    // region existed at all) could otherwise leave us pointed at a combo guaranteed to 404
    if (!wefaxProductAvailable (wefax_region_i, wefax_product_i)) {
        wefax_product_i = wefaxFirstAvailProduct (wefax_region_i);
        NVWriteUInt8 (NV_WEFAX_PRODUCT, wefax_product_i);
    }
}

/* whether the on-map WEFAX badge should currently be shown.
 * only offered while the Weather core map style is active -- WEFAX charts are a
 * different thing entirely from CM_WX's synthetic temperature map, but it's the
 * natural place for a user to think to look for real weather chart imagery.
 */
bool wefaxBadgeVisible (void)
{
    return (core_map == CM_WX && wefaxEnabled());
}


// ---------------------------------------------------------------------
// fetch
// ---------------------------------------------------------------------

/* build the OHB query page (as a zlib-deflated resource, like SDO's /SDO/<file>.z)
 * and a filesystem-safe local cache file name for the current region/product selection.
 */
static void buildWefaxPaths (char *page, size_t page_len)
{
    const WefaxRegion &reg = wefax_regions[wefax_region_i];
    const WefaxProduct &prod = wefax_products[wefax_product_i];

    // NOTE: httpHCGET() (wifi.cpp) prepends "/ham/HamClock" to whatever page we
    // give it -- do NOT include it here, same convention as dxpeds_page[],
    // onta_page[], and SDO's url[] all follow. Including it here would send
    // "/ham/HamClock/ham/HamClock/wefax/chart.pl?..." over the wire.
    snprintf (page, page_len, "/wefax/chart.pl?provider=%s&region=%s&product=%s&fmt=z",
              reg.provider_id, reg.id, prod.id);

    snprintf (wefax_cache_fn, sizeof(wefax_cache_fn), "wefax_%s_%s_%s.bmp",
              reg.provider_id, reg.id, prod.id);
}

/* download the current chart selection's deflated BMP and inflate it to save_path.
 * N.B. modeled directly on sdo.cpp's retrieveSDO() -- this is binary image data, not text,
 * so it must NOT go through openCachedFile()/getTCPLine() the way cities2.txt/onta.txt do:
 * that path writes "%s\n" per line and would corrupt any embedded NUL or newline byte in
 * the image. Content-Length + zinfWiFiFILE() is the binary-safe way, same as every other
 * BMP HamClock fetches (SDO, core map styles).
 */
static bool downloadWefaxChart (const char *page, const char *save_path)
{
    WiFiClient client;
    bool ok = false;

    Serial.println (page);
    if (client.connect (backend_host, backend_port)) {
        updateClocks (false);

        httpHCGET (client, backend_host, page);

        char cl_str[100];
        if (httpSkipHeader (client, "Content-Length: ", cl_str, sizeof(cl_str))) {
            int cl = atol (cl_str);
            FILE *fp = fopen (save_path, "w");
            if (fp) {
                if (fchown (fileno(fp), getuid(), getgid()) < 0)
                    Serial.printf ("WEFAX: chown(%s,%d,%d): %s\n",
                                   save_path, getuid(), getgid(), strerror(errno));
                ok = zinfWiFiFILE (client, cl, fp);
                fclose (fp);
                if (!ok)
                    unlink (save_path);         // don't leave a half-written file behind
            }
        } else
            Serial.printf ("WEFAX: no Content-Length\n");
    } else
        Serial.printf ("WEFAX: connect %s:%d failed\n", backend_host, backend_port);

    client.stop();
    return (ok);
}

/* return whether the local cache file is fresh enough to reuse without a new download.
 * WEFAX_MAXAGE is deliberately just a local-session guard, not the real freshness source --
 * OHB is the authority on whether a given chart has actually been reissued, via its own
 * per-(provider,region,product) TTL keyed to each product's real issue schedule.
 */
static bool wefaxCacheFresh (const char *path)
{
    struct stat sbuf;
    if (stat (path, &sbuf) < 0)
        return (false);
    if (sbuf.st_size < WEFAX_MINSIZE)
        return (false);
    return (myNow() - sbuf.st_mtime <= WEFAX_MAXAGE);
}

/* whether a usable (right type, big enough) local file exists at all, regardless of age --
 * used only for the stale-is-better-than-nothing fallback after a failed download.
 */
static bool wefaxCachePresent (const char *path)
{
    struct stat sbuf;
    return (stat (path, &sbuf) == 0 && sbuf.st_size >= WEFAX_MINSIZE);
}

/* ensure a fresh local copy of the current chart selection exists, downloading if needed.
 * on success leaves a readable local file at our_dir/wefax_cache_fn.
 */
static bool retrieveWefaxChart (Message &ynot)
{
    char page[128];
    buildWefaxPaths (page, sizeof(page));

    char local_path[200];
    snprintf (local_path, sizeof(local_path), "%s/%s", our_dir.c_str(), wefax_cache_fn);

    if (wefaxCacheFresh (local_path))
        return (true);

    if (!downloadWefaxChart (page, local_path)) {
        // tolerate a stale-but-present file rather than show nothing, same spirit
        // as openCachedFile()'s own fallback behavior elsewhere in HamClock
        if (wefaxCachePresent (local_path)) {
            Serial.printf ("WEFAX: download failed, reusing stale %s\n", wefax_cache_fn);
            return (true);
        }
        ynot.set ("WEFAX fetch failed");
        return (false);
    }

    return (true);
}

/* draw the current chart selection into box using Fit mode (whole chart resized to fill
 * box -- small text, but nothing panned or cropped away). Assumes the local cache file is
 * already fresh; caller is responsible for calling retrieveWefaxChart() first.
 */
static bool drawWefaxFit (const SBox &box, Message &ynot)
{
    char local_path[200];
    snprintf (local_path, sizeof(local_path), "%s/%s", our_dir.c_str(), wefax_cache_fn);
    FILE *fp = fopen (local_path, "r");
    if (!fp) {
        ynot.set ("local WEFAX file missing");
        return (false);
    }

    GenReader gr (fp);
    bool ok = installBMPBox (gr, box, FIT_RESIZE, ynot);
    fclose (fp);
    return (ok);
}

/* fill box with a simple "loading" placeholder. the fetch this precedes can block for a
 * couple seconds on a cold cache (real network fetch + convert on OHB), so without this
 * the viewer just sits on a blank black box with no feedback that anything is happening.
 */
static void drawWefaxLoading (const SBox &box)
{
    fillSBox (box, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    static const char msg[] = "Loading WEFAX chart...";
    uint16_t w = getTextWidth (msg);
    tft.setCursor (box.x + (box.w-w)/2, box.y + box.h/2);
    tft.print (msg);
}


// ---------------------------------------------------------------------
// zoom + pan
//
// installBMPBox()/readBMPImage()'s FIT_CROP always crops to a fixed center
// with no offset control, so real panning needs the full decoded image kept
// around, not just a final cropped box. bmp.cpp's own row decoder
// (read565TB()) is static/private to that file, so rather than touch
// bmp.cpp we get the full buffer through readBMPImage()'s existing public
// API: request a box whose dimensions exactly match the native image size.
// cropU16Image()'s "exact size match" branch is then just a memcpy -- no
// actual cropping happens, so what comes back is genuinely the whole
// decoded image, unmodified, ready for us to pan across ourselves.
// ---------------------------------------------------------------------

static void freeWefaxFull (void)
{
    if (wefax_full565) {
        free (wefax_full565);
        wefax_full565 = NULL;
    }
}

/* decode the current local cache file at full native resolution into wefax_full565.
 * assumes retrieveWefaxChart() has already been called. return whether ok.
 */
static bool decodeWefaxFull (Message &ynot)
{
    freeWefaxFull();

    char local_path[200];
    snprintf (local_path, sizeof(local_path), "%s/%s", our_dir.c_str(), wefax_cache_fn);

    // pass 1: just enough to learn the native width/height
    FILE *fp1 = fopen (local_path, "r");
    if (!fp1) {
        ynot.set ("local WEFAX file missing");
        return (false);
    }
    GenReader gr1 (fp1);
    int w, h, bpp, pad;
    bool hdr_ok = readBMPHeader (gr1, w, h, bpp, pad, ynot);
    fclose (fp1);
    if (!hdr_ok)
        return (false);

    // readBMPImage() silently discards the last column when width is odd (to avoid 565
    // row padding) -- match that adjustment here too, or the box we ask for below won't
    // equal what it computes internally, and cropU16Image() hits a fatal "bad overlap"
    // instead of the intended exact-size-match fast path.
    if (w & 1)
        w -= 1;

    // pass 2: decode the WHOLE image -- see the block comment above for why asking for a
    // box that exactly matches native size yields the unmodified full-resolution buffer
    FILE *fp2 = fopen (local_path, "r");
    if (!fp2) {
        ynot.set ("local WEFAX file missing");
        return (false);
    }
    GenReader gr2 (fp2);
    SBox full_b = {0, 0, (uint16_t)w, (uint16_t)abs(h)};
    bool ok = readBMPImage (gr2, full_b, wefax_full565, FIT_CROP, ynot);
    fclose (fp2);
    if (!ok)
        return (false);

    wefax_full_w = w;               // already odd-adjusted above -- matches the actual buffer
    wefax_full_h = abs(h);

    // drawWefaxZoomed() centers the pan window on its first call after a decode -- a
    // dedicated flag, not a sentinel value in wefax_pan_x/y themselves: nudgeWefaxPan()
    // can legitimately drive those negative (panning past an edge, clamped back on the
    // next redraw), and a sign-based sentinel would misfire on exactly that case, mistaking
    // "panned past the top" for "never centered" and snapping back to the middle instead of
    // clamping to the real edge -- which is what caused the up-arrow bounce.
    wefax_need_center = true;

    return (true);
}

/* blit the current pan window of wefax_full565 into box, at native (1x) pixel scale.
 * clamps the pan offset to keep the window within the decoded image; centers within
 * box on any axis where the decoded image is smaller than the box.
 */
static void drawWefaxZoomed (const SBox &box)
{
    fillSBox (box, RA8875_BLACK);

    SBox raw_b;
    raw_b.x = box.x * tft.SCALESZ;
    raw_b.y = box.y * tft.SCALESZ;
    raw_b.w = box.w * tft.SCALESZ;
    raw_b.h = box.h * tft.SCALESZ;

    if (wefax_need_center) {
        wefax_pan_x = wefax_full_w > raw_b.w ? (wefax_full_w - raw_b.w)/2 : 0;
        wefax_pan_y = wefax_full_h > raw_b.h ? (wefax_full_h - raw_b.h)/2 : 0;
        wefax_need_center = false;
    }

    int max_pan_x = wefax_full_w > raw_b.w ? wefax_full_w - raw_b.w : 0;
    int max_pan_y = wefax_full_h > raw_b.h ? wefax_full_h - raw_b.h : 0;
    if (wefax_pan_x > max_pan_x) wefax_pan_x = max_pan_x;
    if (wefax_pan_y > max_pan_y) wefax_pan_y = max_pan_y;
    if (wefax_pan_x < 0) wefax_pan_x = 0;
    if (wefax_pan_y < 0) wefax_pan_y = 0;
    wefax_max_pan_x = max_pan_x;       // published for drawWefaxPanArrows()
    wefax_max_pan_y = max_pan_y;

    int show_w = wefax_full_w < raw_b.w ? wefax_full_w : raw_b.w;
    int show_h = wefax_full_h < raw_b.h ? wefax_full_h : raw_b.h;
    int dst_x0 = raw_b.x + (wefax_full_w < raw_b.w ? (raw_b.w - wefax_full_w)/2 : 0);
    int dst_y0 = raw_b.y + (wefax_full_h < raw_b.h ? (raw_b.h - wefax_full_h)/2 : 0);

    for (int dy = 0; dy < show_h; dy++)
        for (int dx = 0; dx < show_w; dx++)
            tft.drawPixelRaw (dst_x0+dx, dst_y0+dy,
                               wefax_full565[(wefax_pan_y+dy)*wefax_full_w + (wefax_pan_x+dx)]);
}

/* nudge the pan window by a fixed fraction of the box size in the given direction
 * (dx_dir/dy_dir each -1, 0, or +1) and redraw immediately. A fast local operation --
 * no network re-check, no loading flash, no re-decode -- just re-blit the already-
 * decoded buffer at the new offset, same reasoning tap-to-recenter used to have.
 */
#define WEFAX_PAN_STEP_DIV  4     // each arrow click moves this fraction of the visible window

static void nudgeWefaxPan (const SBox &box, int dx_dir, int dy_dir)
{
    if (!wefax_full565)
        return;

    int raw_w = box.w * tft.SCALESZ;
    int raw_h = box.h * tft.SCALESZ;

    wefax_pan_x += dx_dir * (raw_w / WEFAX_PAN_STEP_DIV);
    wefax_pan_y += dy_dir * (raw_h / WEFAX_PAN_STEP_DIV);

    // drawWefaxZoomed() clamps pan_x/y to valid bounds before drawing
}

/* draw one small filled+bordered arrow button with the given single-character label.
 * filled (not just outlined) so it reads clearly as a button against whatever chart
 * content happens to be underneath, rather than blending into it.
 */
static void drawWefaxArrowBtn (const SBox &b, const char *lbl)
{
    tft.fillRect (b.x, b.y, b.w, b.h, DKGRAY);
    tft.drawRect (b.x, b.y, b.w, b.h, RA8875_WHITE);
    uint16_t w = getTextWidth (lbl);
    tft.setCursor (b.x + (b.w-w)/2, b.y + b.h/2 - 4);
    tft.print (lbl);
}

/* draw whichever pan arrow buttons are currently meaningful -- only shown while zoomed
 * (caller only calls this then), and individually skipped once panning further in that
 * direction would be a no-op: no point showing a left arrow when already at the left
 * edge of the decoded image. wefax_max_pan_x/y are published by drawWefaxZoomed()'s own
 * clamping logic, called just before this every time, so they're always current.
 *
 * These are small, fixed hit-boxes using the exact same inBox()-based dispatch every other
 * button in this viewer already uses (region/product/zoom/close), which is confirmed to work
 * identically through local touch and the web view's remote-touch relay. The earlier tap-
 * anywhere-in-the-image approach used a much larger hit area (all of img_b) and did not work
 * reliably through the web view; arrows sidestep that by matching the pattern that's already
 * proven to work everywhere. Also just clearer UX on its own merits: a fixed nudge in a chosen
 * direction beats a blind jump-to-tapped-point with no preview of where you're about to land.
 */
static void drawWefaxPanArrows (const SBox &left_b, const SBox &right_b,
                                 const SBox &up_b, const SBox &down_b)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    if (wefax_pan_x > 0)                drawWefaxArrowBtn (left_b,  "<");
    if (wefax_pan_x < wefax_max_pan_x)  drawWefaxArrowBtn (right_b, ">");
    if (wefax_pan_y > 0)                drawWefaxArrowBtn (up_b,    "^");
    if (wefax_pan_y < wefax_max_pan_y)  drawWefaxArrowBtn (down_b,  "v");
}

/* redraw the zoomed chart plus its pan arrows together -- arrows overlay the image area,
 * so they need to be redrawn every time drawWefaxZoomed()'s fillSBox(box, BLACK) would
 * otherwise erase them.
 */
static void redrawWefaxZoomed (const SBox &img_b, const SBox &left_b, const SBox &right_b,
                                const SBox &up_b, const SBox &down_b)
{
    drawWefaxZoomed (img_b);
    drawWefaxPanArrows (left_b, right_b, up_b, down_b);
}


// ---------------------------------------------------------------------
// badge
// ---------------------------------------------------------------------

/* draw (or blank) the on-map "WEFAX" badge, styled to match drawBordersButton():
 * inverted fill when engaged, normal black-fill/white-outline/white-text look when off.
 */
/* draw the on-map "WEFAX" badge, styled to match drawBordersButton() minus the on/off
 * invert: unlike Borders (a persistent overlay toggle), this is purely a launch button --
 * runWefaxViewer() takes over the badge's own screen position with its own header the
 * instant it opens, so an "engaged" visual state here would never actually be seen.
 */
void drawWefaxButton (void)
{
    if (!wefaxBadgeVisible())
        return;

    static const char label[] = "WEFAX";

    tft.fillRect (wefax_btn_b.x, wefax_btn_b.y, wefax_btn_b.w-1, wefax_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (wefax_btn_b.x, wefax_btn_b.y, wefax_btn_b.w-1, wefax_btn_b.h-1, RA8875_WHITE);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t lbl_w = getTextWidth (label);
    tft.setCursor (wefax_btn_b.x + (wefax_btn_b.w-lbl_w)/2, wefax_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (label);
}


// ---------------------------------------------------------------------
// viewer -- blocking modal takeover of map_b, modeled directly on
// satsked.cpp's drawSatGroupSchedule()
// ---------------------------------------------------------------------

#define WFV_TB       26            // header band height
#define WFV_BTN_H    22
#define WFV_BTN_GAP  8
#define WFV_ARROW_W  34
#define WFV_ARROW_H  34

/* every box the viewer needs, computed once per open in runWefaxViewer() and passed
 * around by const-ref from there -- individual SBox parameters got unwieldy once pan
 * arrows brought the total to ten.
 */
typedef struct {
    SBox hdr_b, img_b;
    SBox close_b, zoom_b, prod_b, reg_b;
    SBox left_b, right_b, up_b, down_b;    // pan arrows, only meaningful while zoomed
} WefaxLayout;

static WefaxLayout buildWefaxLayout (void)
{
    WefaxLayout wl;

    wl.hdr_b = {map_b.x, map_b.y, map_b.w, WFV_TB};
    wl.img_b = {map_b.x, (uint16_t)(map_b.y+WFV_TB), map_b.w, (uint16_t)(map_b.h-WFV_TB)};

    uint16_t x = map_b.x + map_b.w - 15;
    const uint16_t close_w = 55, zoom_w = 55, prod_w = 110, reg_w = 140;
    x -= close_w;                wl.close_b = {x, (uint16_t)(map_b.y+2), close_w, WFV_BTN_H};
    x -= WFV_BTN_GAP + zoom_w;   wl.zoom_b  = {x, (uint16_t)(map_b.y+2), zoom_w,  WFV_BTN_H};
    x -= WFV_BTN_GAP + prod_w;   wl.prod_b  = {x, (uint16_t)(map_b.y+2), prod_w,  WFV_BTN_H};
    x -= WFV_BTN_GAP + reg_w;    wl.reg_b   = {x, (uint16_t)(map_b.y+2), reg_w,   WFV_BTN_H};

    wl.left_b  = {(uint16_t)(wl.img_b.x+4),
                  (uint16_t)(wl.img_b.y + wl.img_b.h/2 - WFV_ARROW_H/2), WFV_ARROW_W, WFV_ARROW_H};
    wl.right_b = {(uint16_t)(wl.img_b.x + wl.img_b.w - WFV_ARROW_W - 4),
                  (uint16_t)(wl.img_b.y + wl.img_b.h/2 - WFV_ARROW_H/2), WFV_ARROW_W, WFV_ARROW_H};
    wl.up_b    = {(uint16_t)(wl.img_b.x + wl.img_b.w/2 - WFV_ARROW_W/2),
                  (uint16_t)(wl.img_b.y+4), WFV_ARROW_W, WFV_ARROW_H};
    wl.down_b  = {(uint16_t)(wl.img_b.x + wl.img_b.w/2 - WFV_ARROW_W/2),
                  (uint16_t)(wl.img_b.y + wl.img_b.h - WFV_ARROW_H - 4), WFV_ARROW_W, WFV_ARROW_H};

    return (wl);
}

/* draw the header row: title + region/product cyclers + Zoom toggle + Close.
 */
static void drawWefaxHeader (const WefaxLayout &wl)
{
    fillSBox (wl.hdr_b, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (wl.hdr_b.x+4, wl.hdr_b.y+6);
    tft.print ("WEFAX");

    // region cycler -- tap to advance to the next region
    const SBox &reg_b = wl.reg_b;
    tft.fillRect (reg_b.x, reg_b.y, reg_b.w, reg_b.h, DKGRAY);
    tft.drawRect (reg_b.x, reg_b.y, reg_b.w, reg_b.h, RA8875_WHITE);
    tft.setTextColor (RA8875_WHITE);
    char reg_lbl[40];
    const WefaxRegion &reg = wefax_regions[wefax_region_i];
    const char *prov_name = reg.provider_id;             // fallback if somehow not found below
    for (uint8_t i = 0; i < N_WEFAX_PROVIDERS; i++)
        if (strcmp (wefax_providers[i].id, reg.provider_id) == 0) {
            prov_name = wefax_providers[i].name;
            break;
        }
    snprintf (reg_lbl, sizeof(reg_lbl), "%s %s", prov_name, reg.name);
    tft.setCursor (reg_b.x + (reg_b.w-getTextWidth(reg_lbl))/2, reg_b.y+6);
    tft.print (reg_lbl);

    // product cycler -- tap to advance to the next product. hidden entirely when the current
    // region only has one product available (nothing to cycle to) -- eg UKMO today, which
    // only has Surface Analysis wired up on the backend.
    if (wefaxMultipleProductsAvailable (wefax_region_i)) {
        const SBox &prod_b = wl.prod_b;
        tft.fillRect (prod_b.x, prod_b.y, prod_b.w, prod_b.h, DKGRAY);
        tft.drawRect (prod_b.x, prod_b.y, prod_b.w, prod_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
        const char *prod_lbl = wefax_products[wefax_product_i].name;
        tft.setCursor (prod_b.x + (prod_b.w-getTextWidth(prod_lbl))/2, prod_b.y+6);
        tft.print (prod_lbl);
    }

    // zoom toggle -- Fit (whole chart, small text) vs Zoomed (native pixel size, legible
    // text, pannable via arrows). inverted fill when zoomed in, like the on-map badges
    // use, so it's obvious at a glance which mode is active.
    const SBox &zoom_b = wl.zoom_b;
    uint16_t zoom_fill = wefax_zoomed ? RA8875_WHITE : RA8875_BLACK;
    uint16_t zoom_text = wefax_zoomed ? RA8875_BLACK : RA8875_WHITE;
    const char *zoom_lbl = wefax_zoomed ? "Zoomed" : "Fit";
    tft.fillRect (zoom_b.x, zoom_b.y, zoom_b.w, zoom_b.h, zoom_fill);
    tft.drawRect (zoom_b.x, zoom_b.y, zoom_b.w, zoom_b.h, RA8875_WHITE);
    tft.setTextColor (zoom_text);
    tft.setCursor (zoom_b.x + (zoom_b.w-getTextWidth(zoom_lbl))/2, zoom_b.y+6);
    tft.print (zoom_lbl);
    tft.setTextColor (RA8875_WHITE);          // restore default for whatever draws next

    // close
    const SBox &close_b = wl.close_b;
    tft.fillRect (close_b.x, close_b.y, close_b.w, close_b.h, DKGRAY);
    tft.drawRect (close_b.x, close_b.y, close_b.w, close_b.h, RA8875_WHITE);
    tft.setTextColor (RA8875_WHITE);
    static const char close_lbl[] = "Close";
    tft.setCursor (close_b.x + (close_b.w-getTextWidth(close_lbl))/2, close_b.y+6);
    tft.print (close_lbl);
}

/* redraw header + chart together: header first, then a "loading" placeholder flushed to
 * screen immediately, THEN the (possibly slow, network-bound) fetch, THEN the actual chart
 * -- Fit mode via installBMPBox(), Zoomed mode via our own full-decode + pan blit (plus the
 * pan arrows, only while zoomed). Without the intermediate flush the loading text would sit
 * in the same undrawn frame as the chart and the user would just see a black box for however
 * long the fetch takes.
 */
static void refreshWefaxView (const WefaxLayout &wl)
{
    drawWefaxHeader (wl);
    drawWefaxLoading (wl.img_b);
    tft.drawPR();

    Message ynot;
    bool ok = retrieveWefaxChart (ynot);       // both modes need a fresh local file first

    if (ok) {
        if (wefax_zoomed) {
            if (!wefax_full565)                // not yet decoded for this chart selection
                ok = decodeWefaxFull (ynot);
            if (ok)
                redrawWefaxZoomed (wl.img_b, wl.left_b, wl.right_b, wl.up_b, wl.down_b);
        } else {
            freeWefaxFull();                   // no need to hold the big buffer in Fit view
            ok = drawWefaxFit (wl.img_b, ynot);
        }
    }

    if (!ok)
        plotMessage (wl.img_b, RA8875_RED, ynot.get());

    tft.drawPR();
}

/* take over map_b to show the current WEFAX chart selection, with controls to cycle
 * region/product, toggle Fit/Zoomed, pan (via arrows, while zoomed), and a Close button,
 * until the user leaves. Coexists with the DE/DX info and side panes exactly like
 * satsked.cpp's schedule view -- only map_b itself is replaced.
 * N.B. modeled directly on satsked.cpp's drawSatGroupSchedule().
 */
void runWefaxViewer (void)
{
    wefax_on = 1;
    wefax_zoomed = false;       // always open on the whole-chart Fit view
    drawWefaxButton();          // repaint the badge inverted before the header covers it anyway

    WefaxLayout wl = buildWefaxLayout();

    fillSBox (map_b, RA8875_BLACK);
    refreshWefaxView (wl);

    UserInput ui = {
        map_b,
        UI_UFuncNone,
        UF_UNUSED,
        UI_NOTIMEOUT,
        UF_CLOCKSOK,
        {0, 0}, TT_NONE, '\0', false, false
    };

    while (waitForUser (ui)) {

        if (ui.kb_char == CHAR_ESC
                    || inBox (ui.tap, wl.close_b)
                    || (ui.kb_char == CHAR_NONE && !inBox (ui.tap, map_b)))
            break;

        bool changed = false;

        if (inBox (ui.tap, wl.reg_b)) {
            wefax_region_i = (wefax_region_i + 1) % N_WEFAX_REGIONS;
            NVWriteUInt8 (NV_WEFAX_REGION, wefax_region_i);
            if (!wefaxProductAvailable (wefax_region_i, wefax_product_i)) {
                wefax_product_i = wefaxFirstAvailProduct (wefax_region_i);
                NVWriteUInt8 (NV_WEFAX_PRODUCT, wefax_product_i);
            }
            wefax_zoomed = false;      // a different chart just came in, show the whole thing first
            freeWefaxFull();           // old decode belongs to the chart we're leaving
            changed = true;
        } else if (wefaxMultipleProductsAvailable (wefax_region_i) && inBox (ui.tap, wl.prod_b)) {
            do {
                wefax_product_i = (wefax_product_i + 1) % N_WEFAX_PRODUCTS;
            } while (!wefaxProductAvailable (wefax_region_i, wefax_product_i));
            NVWriteUInt8 (NV_WEFAX_PRODUCT, wefax_product_i);
            wefax_zoomed = false;
            freeWefaxFull();
            changed = true;
        } else if (inBox (ui.tap, wl.zoom_b)) {
            wefax_zoomed = !wefax_zoomed;
            changed = true;
        } else if (wefax_zoomed && inBox (ui.tap, wl.left_b)) {
            nudgeWefaxPan (wl.img_b, -1, 0);
            redrawWefaxZoomed (wl.img_b, wl.left_b, wl.right_b, wl.up_b, wl.down_b);
            tft.drawPR();
        } else if (wefax_zoomed && inBox (ui.tap, wl.right_b)) {
            nudgeWefaxPan (wl.img_b, +1, 0);
            redrawWefaxZoomed (wl.img_b, wl.left_b, wl.right_b, wl.up_b, wl.down_b);
            tft.drawPR();
        } else if (wefax_zoomed && inBox (ui.tap, wl.up_b)) {
            nudgeWefaxPan (wl.img_b, 0, -1);
            redrawWefaxZoomed (wl.img_b, wl.left_b, wl.right_b, wl.up_b, wl.down_b);
            tft.drawPR();
        } else if (wefax_zoomed && inBox (ui.tap, wl.down_b)) {
            nudgeWefaxPan (wl.img_b, 0, +1);
            redrawWefaxZoomed (wl.img_b, wl.left_b, wl.right_b, wl.up_b, wl.down_b);
            tft.drawPR();
        }
        // N.B. the four arrow branches above are deliberately NOT routed through
        // refreshWefaxView()/changed=true: panning is a fast local re-blit of the
        // already-decoded buffer, not a full refresh (no network re-check, no loading
        // flash, no re-decode) -- that's what makes it feel immediate rather than janky.

        if (changed)
            refreshWefaxView (wl);
    }

    // leaving -- release the decode buffer, restore the normal map underneath, badge included
    wefax_on = 0;
    freeWefaxFull();
    initEarthMap();
    tft.drawPR();
}
