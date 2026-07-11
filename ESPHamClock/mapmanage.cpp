/* this file manages the background maps, both static styles and VOACAP area propagation.
 *
 * all map files are RGB565 BMP V4 format.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>


#include "HamClock.h"
#include "zlib.h"                                       // ours

// persistent state of open files, allows restarting
static FILE *day_fp, *night_fp;                         // open day and night files
static int day_nbytes, night_nbytes;                    // bytes mmap'ed
static char *day_pixels, *night_pixels;                 // pixels mmap'ed


// BMP file format parameters
#define COREHDRSZ 14                                    // always 14 bytes at front of header
#define HDRVER 108                                      // BITMAPV4HEADER, these many more bytes in subheader
#define BHDRSZ (COREHDRSZ+HDRVER)                       // total header size
#define BPERBMPPIX 2                                    // bytes per BMP pixel

// box in which to draw map scales
SBox mapscale_b;

// current CoreMap designation
CoreMaps core_map = CM_NONE;                            // current core map, if any

// mask of 1<<CoreMaps currently rotating. N.B. must always include 1<<core_map
uint16_t map_rotset;

// supporting info for each core map style
#define X(a,b,c,d,e,f)  {b,c,d,e,f},                    // expands COREMAPS entries for CoreMapInfo
CoreMapInfo cm_info[CM_N] = {
    COREMAPS
};
#undef X


// prop and muf style names
static const char prop_style[] = "PropMap";
static const char muf_v_style[] = "MUFMap";

// handy zoomed w and h
#define ZOOM_W  (HC_MAP_W*pan_zoom.zoom)
#define ZOOM_H  (HC_MAP_H*pan_zoom.zoom)


/* download and save the given file from its zlib compression arriving on client.
 * client is already postioned at first byte of compressed image then expect len more bytes.
 * if all ok return true and leave client positioned at end of image -- there might be another :-)
 */
static bool downloadZFile (WiFiClient &client, const char *filename, long len)
{
        // create file
        FILE *fp = fopenOurs (filename, "w");
        if (!fp)
            fatalError ("Error creating file %s: %s", filename, strerror(errno));       // never returns

        // expand
        bool ok = zinfWiFiFILE (client, len, fp);

        // close and remove if trouble
        fclose (fp);
        if (!ok) {
            Serial.printf ("%s: inflate failed\n", filename);
            unlinkOurs (filename);
        }

        return (ok);
}


/* invalidate pixel connection until proven good again.
 */
static void invalidatePixels()
{
        // disconnect from tft thread
        tft.setEarthPix (NULL, NULL, 0, 0);

        if (getGrayDisplay() == GRAY_OFF) {
            // unmap pixel arrays
            if (day_pixels) {
                munmap (day_pixels, day_nbytes);
                day_pixels = NULL;
            }
            if (night_pixels) {
                munmap (night_pixels, night_nbytes);
                night_pixels = NULL;
            }
        } else {
            // gray scale pixels are local arrays
            free (day_pixels);
            day_pixels = NULL;
            free (night_pixels);
            night_pixels = NULL;
        }
}

/* convert the given RGB565 from color to gray
 */
static uint16_t RGB565TOGRAY (uint16_t c)
{
        uint16_t r = RGB565_R(c);
        uint16_t g = RGB565_G(c);
        uint16_t b = RGB565_B(c);
        uint16_t gray = RGB2GRAY(r,g,b);
        return (RGB565 (gray, gray, gray));
}

/* make the day and night file names for the given map style
 */
static void mkMapFilenames (CoreMaps cm, char dfile[], char nfile[], int zoom, size_t fn_l)
{
        int zoom_w = HC_MAP_W * zoom;
        int zoom_h = HC_MAP_H * zoom;

        if (cm == CM_DRAP) {
            snprintf (dfile, fn_l, "map-D-%dx%d-DRAP-S.bmp", zoom_w, zoom_h);
            snprintf (nfile, fn_l, "map-N-%dx%d-DRAP-S.bmp", zoom_w, zoom_h);
        } else if (cm == CM_WX) {
            const char *units = showATMhPa() ? "mB" : "in";
            snprintf (dfile, fn_l, "map-D-%dx%d-Wx-%s.bmp", zoom_w, zoom_h, units);
            snprintf (nfile, fn_l, "map-N-%dx%d-Wx-%s.bmp", zoom_w, zoom_h, units);
        } else {
            const char *style = cm_info[cm].name;
            snprintf (dfile, fn_l, "map-D-%dx%d-%s.bmp", zoom_w, zoom_h, style);
            snprintf (nfile, fn_l, "map-N-%dx%d-%s.bmp", zoom_w, zoom_h, style);
        }
}

/* prepare open day_fp and night_fp for pixel access.
 * gray images are converted into memory arrays.
 * return whether ok
 */
static bool installFilePixels (const char *dfile, const char *nfile)
{
        bool ok = false;

        // mmap pixels if both files are open
        if (day_fp && night_fp) {

            day_nbytes = BHDRSZ + ZOOM_W*ZOOM_H*BPERBMPPIX;
            night_nbytes = BHDRSZ + ZOOM_W*ZOOM_H*BPERBMPPIX;
            day_pixels = (char *)                               // allow OS to choose addrs
                    mmap (NULL, day_nbytes, PROT_READ, MAP_PRIVATE, fileno(day_fp), 0);
            night_pixels = (char *)
                    mmap (NULL, night_nbytes, PROT_READ, MAP_PRIVATE, fileno(night_fp), 0);

            ok = day_pixels != MAP_FAILED && night_pixels != MAP_FAILED;
        }

        // install pixels if ok
        if (ok) {

            // Serial.println ("both mmaps good");

            // don't need files open once mmap has been established
            fclose(day_fp);
            day_fp = NULL;
            fclose(night_fp);
            night_fp = NULL;

            if (getGrayDisplay() != GRAY_OFF) {

                // convert to gray images in memory

                // prep new arrays
                const int n_mem_bytes = ZOOM_W*ZOOM_H*BPERBMPPIX;
                char *mem_day_pixels = (char *) malloc (n_mem_bytes);
                char *mem_night_pixels = (char *) malloc (n_mem_bytes);
                if (!mem_day_pixels || !mem_night_pixels)
                    fatalError ("No memory for gray scale image");

                // handy pixel pointers from mmap to memory
                uint16_t *fdp = (uint16_t *) (day_pixels + BHDRSZ);
                uint16_t *fnp = (uint16_t *) (night_pixels + BHDRSZ);
                uint16_t *tdp = (uint16_t *) (mem_day_pixels);
                uint16_t *tnp = (uint16_t *) (mem_night_pixels);

                // convert each pixel
                int n_mem_pix = n_mem_bytes/2;
                struct timeval tv0, tv1;
                gettimeofday (&tv0, NULL);
                while (--n_mem_pix >= 0) {
                    *tdp++ = RGB565TOGRAY(*fdp++);
                    *tnp++ = RGB565TOGRAY(*fnp++);
                }
                gettimeofday (&tv1, NULL);
                Serial.printf ("gray conversion took %ld us\n",
                                            (tv1.tv_sec-tv0.tv_sec)*1000000 + (tv1.tv_usec - tv0.tv_usec));

                // replace mmap with gray memory copy
                munmap (day_pixels, day_nbytes);
                day_pixels = mem_day_pixels;
                munmap (night_pixels, night_nbytes);
                night_pixels = mem_night_pixels;

                // install in tft at start of pixels
                tft.setEarthPix (day_pixels, night_pixels, ZOOM_W, ZOOM_H);

            } else {
                // install in tft at start of pixels
                tft.setEarthPix (day_pixels+BHDRSZ, night_pixels+BHDRSZ, ZOOM_W, ZOOM_H);
            }

        } else {

            // no go -- clean up

            if (day_fp) {
                fclose(day_fp);
                day_fp = NULL;
            } else
                Serial.printf ("%s not open\n", dfile);
            if (day_pixels == MAP_FAILED)
                Serial.printf ("%s mmap failed: %s\n", dfile, strerror(errno));
            else if (day_pixels)
                munmap (day_pixels, day_nbytes);
            day_pixels = NULL;

            if (night_fp) {
                fclose(night_fp);
                night_fp = NULL;
            } else
                Serial.printf ("%s not open\n", nfile);
            if (night_pixels == MAP_FAILED)
                Serial.printf ("%s mmap failed: %s\n", nfile, strerror(errno));
            else if (night_pixels)
                munmap (night_pixels, night_nbytes);
            night_pixels = NULL;

        }

        return (ok);
}

/* prepare and test whether the query files for the given time style and MHz are already local.
 * N.B. query[] nfn[] and dfn[] are all the same length of qdn_len.
 */
static bool checkDayNightFiles (int yr, int mo, int hr, const char *page, const char *style,
const float MHz, char query[], char dfn[], char nfn[], size_t qdn_len)
{
        static const char qfmt[] = "YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%.3f&TXLNG=%.3f&PATH=%d&WATTS=%d&WIDTH=%d&HEIGHT=%d&MHZ=%.2f&TOA=%.1f&MODE=%d&TOA=%.1f";

        snprintf (query, qdn_len, qfmt,
                    yr, mo, hr, de_ll.lat_d, de_ll.lng_d, show_lp, bc_power, ZOOM_W, ZOOM_H,
                    MHz, bc_toa, bc_modevalue, bc_toa);
		antenna_addargs(query+strlen(query), qdn_len-strlen(query));   // add antenna selection arguments to query
        // by storing the entire query in the file name we easily know if a new download is needed.
        snprintf (dfn, qdn_len, "map-D-%s-%s-%010u.bmp", style, page, stringHash(query));
        snprintf (nfn, qdn_len, "map-N-%s-%s-%010u.bmp", style, page, stringHash(query));

        // test for existance
        bool ok = false;
        FILE *fp = fopenOurs (dfn, "r");
        if (fp) {
            fclose (fp);
            fp = fopenOurs (nfn, "r");
            if (fp) {
                fclose (fp);
                ok = true;
            }
        }

        return (ok);
}

/* install maps that require a query, spreading load across current hour if possible.
 * page is the fetch*.pl CGI handler, we add the query here based on current circumstances.
 * clean style cache of any older than max_age.
 * return whether ok
 */
static bool installQueryMaps (const char *page, const char *msg, const char *style, const float MHz,
long max_age)
{
        // fresh start
        invalidatePixels();
        (void) cleanCache (style, 2*max_age);           // don't hammer immediately

        // get user clock time
        time_t t = nowWO();
        int yr = year(t);
        int mo = month(t);
        int hr = hour(t);

        // required buffers
        #define QBUFLEN 200
        char query[QBUFLEN];
        char q_dfn[QBUFLEN];
        char q_nfn[QBUFLEN];

        // check if suitable file already exist
        bool ok = checkDayNightFiles (yr, mo, hr, page, style, MHz, query, q_dfn, q_nfn, QBUFLEN);
        if (!ok) {

            // avoid all maps updating at top of the hour when in rotation group
            static int use_prev_minute;                 // prev hour ok if current minute is less than this
            if (use_prev_minute == 0) {
                use_prev_minute = 1 + random(58);       // [1,58]
                Serial.printf ("maps can update after %d min after the hour\n", use_prev_minute);
            }

            // reuse previous hour's file if early enough within this hour
            if (minute(t) < use_prev_minute) {
                // see if file for previous hour exists
                time_t prev_t = t - 3600;
                int prev_yr = year(prev_t);
                int prev_mo = month(prev_t);
                int prev_hr = hour(prev_t);
                ok = checkDayNightFiles (prev_yr, prev_mo, prev_hr, page, style, MHz, query,
                                                                        q_dfn, q_nfn, QBUFLEN);
                if (!ok) {
                    // no file for previous hour either so might as well download for current hour
                    ok = checkDayNightFiles (yr, mo, hr, page, style, MHz, query, q_dfn, q_nfn, QBUFLEN);
                }
             }
        }

        if (ok) {
            Serial.printf ("%s: using local D and N files\n", style);
        } else {
            // download new twin voacap maps
            Serial.printf ("%s: downloading fresh D and N files\n", style);
            updateClocks(false);

            // belt-and-braces: honor VOACAP rate limit before any network I/O
            time_t now = myNow();
            if (voacapThrottled(now)) {
                long since = (long)(now - lastVOACAPAttempt());
                Serial.printf ("VOACAP: %s throttled in installQueryMaps (%ld s since last)\n",
                               style, since);
                mapMsg (3000, "Server Busy Please Wait");
                return false;          // caller will schedule via nextVOACAPRetry()
            }
            noteVOACAPAttempt(now);

            WiFiClient client;
            client.setTimeout(VOACAP_TIMEOUT_MS);
            if (client.connect(backend_host, backend_port)) {
                mapMsg (0, "%s", msg);
                char url[2*QBUFLEN];
                snprintf (url, sizeof(url), "/%s?%s", page, query);
                Serial.printf ("running %s\n", url);
                httpHCGET (client, backend_host, url);
                char x_len[100];
                if (httpSkipHeader (client, "X-2Z-lengths: ", x_len, sizeof(x_len))) {
                    long l1, l2;
                    if (sscanf (x_len, "%ld %ld", &l1, &l2) == 2) {
                        ok = downloadZFile (client, q_dfn, l1) && downloadZFile (client, q_nfn, l2);
                    } else {
                        Serial.printf ("%s: bogus multipart: '%s'\n", page, x_len);
                    }
                } else {
                    Serial.printf ("%s: header failed\n", page);
                }
                client.stop();
            } else {
                Serial.printf ("%s: connection failed\n", page);
            }
        }

        // install if ok
        if (ok) {
            day_fp = fopenOurs (q_dfn, "r");
            night_fp = fopenOurs (q_nfn, "r");
            ok = installFilePixels (q_dfn, q_nfn);
        }

        // check again
        if (!ok)
            mapMsg (3000, "%s: fail", style);

        return (ok);
}


/* open the given CoreMaps RGB565 BMP file, downloading fresh if absent or too old.
 * if ok, return open FILE* positioned at first pixel, else return NULL.
 */
static FILE *openMapFile (CoreMaps cm, const char *filename, const char *title)
{
        // trust but verify
        bool ok = true;

        // open local file
        FILE *fp = fopenOurs (filename, "r");
        if (!fp) {
            Serial.printf ("%s: not local\n", filename);
            ok = false;
        }

        // check age if care
        if (ok && cm_info[cm].max_age != CACHE_FOREVER) {
            struct stat sbuf;
            if (fstat (fileno(fp), &sbuf) < 0) {
                printf ("%s: time fstat(%d): %s\n", filename, fileno(fp), strerror(errno));
                ok = false;
            } else {
                long age = myNow() - sbuf.st_mtime;
                if (age > cm_info[cm].max_age) {
                    Serial.printf ("%s too old: %ld > %d secs\n", filename, age, cm_info[cm].max_age);
                    ok = false;
                } else
                    Serial.printf ("%s: age ok: %ld < %d\n", filename, age, cm_info[cm].max_age);
            }
        }


        // if still looks promising, read the header
        int img_w = 0, img_h = 0, img_bpp = 0, img_pad = 0;
        if (ok) {
            GenReader gr(fp);
            Message ynot;
            if (!readBMPHeader (gr, img_w, img_h, img_bpp, img_pad, ynot)) {
                Serial.printf ("%s: %s\n", filename, ynot.get());
                ok = false;
            }
        }

        // suitable for Earth map?
        if (ok) {
            // negative img_h is required to indicate pixels can be displayed top-to-bottom
            if (img_w != ZOOM_W || -img_h != ZOOM_H || img_bpp != 16 || img_pad != 0) {
                Serial.printf ("%s: unsuitable image: w= %d h= %d bpp= %d pad= %d\n", filename,
                                        img_w, img_h, img_bpp, img_pad);
                ok = false;
            }
        }

        // if no good, try downloading unless CM_USER
        if (!ok) {

            // start over
            if (fp) {
                fclose(fp);
                fp = NULL;
                unlinkOurs(filename);
            }

            if (cm != CM_USER) {

                // download and open again if success
                WiFiClient client;
                if (client.connect(backend_host, backend_port)) {
                    // show message for larger images
                    if (BUILD_W * pan_zoom.zoom > 4800)
                        mapMsg (0, "%s", title);
                    char url[256];
                    snprintf (url, sizeof(url), "/maps/%s.z", filename);
                    Serial.printf ("downloading %s\n", url);
                    httpHCGET (client, backend_host, url);
                    char c_l[100];
                    if (httpSkipHeader (client, "Content-Length: ", c_l, sizeof(c_l)) &&
                                                    downloadZFile (client, filename, atol(c_l)))
                        fp = fopenOurs (filename, "r");
                    client.stop();
                }
                if (!fp)
                    mapMsg (1000, "%s: download failed", title);
            }
        }

        // return result, open if good or closed if not
        return (fp);
}

/* read any BMP from the given web connection and save for use by CM_USER.
 * client is positioned at start of image.
 * return whether image is suitable with reason why if not.
 */
bool installWebMapImages (WiFiClient &client, long content_length, ImageRefit fit, Message &ynot)
{
        // time download
        struct timeval tv0;
        if (debugLevel(DEBUG_BMP, 1))
            gettimeofday (&tv0, NULL);

        // read contents
        StackMalloc image_mem(content_length);
        char *image = (char *) image_mem.getMem();
        if (!image)
            fatalError ("No memory for User image %ld", content_length);
        long n_read;
        for (long i = 0; i < content_length; i += n_read) {
            n_read = client.readArray ((uint8_t*)&image[i], content_length - i);
            if (n_read == 0) {
                ynot.printf ("image is short: %ld < %ld", i, content_length);
                return (false);
            }
        }

        if (debugLevel(DEBUG_BMP, 1)) {
            struct timeval tv1;
            gettimeofday (&tv1, NULL);
            Serial.printf ("BMP: download %ld bytes in %ld us\n", content_length, (long)TVDELUS(tv0,tv1));
        }

        // fresh
        rmWebMapImages();

        // save at each possible zoom level
        for (int z = MIN_ZOOM; z <= MAX_ZOOM; z++) {
            char dfile[100], nfile[100];
            GenReader gr(image, content_length);
            SBox z_b;
            z_b.x = z_b.y = 0;
            z_b.w = HC_MAP_W * z;
            z_b.h = HC_MAP_H * z;
            uint16_t *z_565;
            mkMapFilenames (CM_USER, dfile, nfile, z, sizeof(dfile));
            if (!readBMPImage (gr, z_b, z_565, fit, ynot)) {                    // N.B. free z_565!
                return(false);
            }
            if (!writeBMP565File (dfile, z_565, z_b.w, z_b.h, ynot)) {
                free (z_565);
                return(false);
            }
            if (!writeBMP565File (nfile, z_565, z_b.w, z_b.h, ynot)) {
                free (z_565);
                return(false);
            }
            free (z_565);
        }

        // ok!
        return (true);
}

/* remove any CM_USER images
 */
void rmWebMapImages (void)
{
    cleanCache (cm_info[CM_USER].name, CACHE_NONE);
}

/* return whether all required CM_USER images are present
 */
bool allWebMapImagesOk (void)
{
    for (int z = MIN_ZOOM; z <= MAX_ZOOM; z++) {
        char dfile[100], nfile[100];
        mkMapFilenames (CM_USER, dfile, nfile, z, sizeof(dfile));
        FILE *dfp = fopenOurs (dfile, "r");
        if (!dfp)
            return (false);
        fclose (dfp);
        FILE *nfp = fopenOurs (nfile, "r");
        if (!nfp)
            return (false);
        fclose (nfp);
    }

    // all present
    return (true);
}

/* install maps for the given CoreMap that are just files maintained on the server, no update query required.
 * Download only if absent or stale.
 * return whether ok
 */
static bool installFileMaps (CoreMaps cm)
{
        // confirm core_map is one of the file styles
        if (!CM_ISFILE(cm))
            fatalError ("installFileMaps(%d) invalid", cm);        // does not return

        // create names and titles
        const char *style = cm_info[cm].name;
        char dfile[100];
        char nfile[100];
        char dtitle[NV_COREMAPSTYLE_LEN+10];
        char ntitle[NV_COREMAPSTYLE_LEN+10];

        mkMapFilenames (cm, dfile, nfile, pan_zoom.zoom, sizeof(dfile));
        snprintf (dtitle, NV_COREMAPSTYLE_LEN+10, "%s D map", style);
        snprintf (ntitle, NV_COREMAPSTYLE_LEN+10, "%s N map", style);

        // fresh start
        invalidatePixels();
        (void) cleanCache (style, 2*cm_info[cm].max_age);         // don't hammer immediately
        if (day_fp)
            fclose(day_fp);
        if (night_fp)
            fclose(night_fp);

        // open each file, downloading if newer or not found locally
        day_fp = openMapFile (cm, dfile, dtitle);
        night_fp = openMapFile (cm, nfile, ntitle);

        // install pixels
        return (installFilePixels (dfile, nfile));
}

/* install fresh core_map.
 * return whether ok
 * N.B. drain pending clicks that may have accumulated during slow downloads.
 */
bool installFreshMaps()
{
        char s[NV_COREMAPSTYLE_LEN];
        char msg[100];
        snprintf (msg, sizeof(msg), "Calculating %s...", getCoreMapStyle(core_map, s));

        bool ok = false;

        switch (core_map) {
        case CM_PMTOA:
            ok = installQueryMaps ("fetchVOACAP-TOA.pl", msg, prop_style,
                                propBand2MHz(cm_info[CM_PMTOA].band), cm_info[CM_PMTOA].max_age);
            break;
        case CM_PMREL:
            ok = installQueryMaps ("fetchVOACAPArea.pl", msg, prop_style,
                                propBand2MHz(cm_info[CM_PMREL].band), cm_info[CM_PMREL].max_age);
            break;
        case CM_MUF_V:
            ok = installQueryMaps ("fetchVOACAP-MUF.pl", msg, muf_v_style, 0, cm_info[CM_MUF_V].max_age);
            break;
        case CM_COUNTRIES:
        case CM_TERRAIN:
        case CM_DRAP:
        case CM_MUF_RT:
        case CM_AURORA:
        case CM_CLOUDS:
        case CM_WX:
	case CM_TROPO:
        case CM_USER:
            ok = installFileMaps (core_map);
            break;
        case CM_N:              // lint
            break;
        }

        drainTouch();

        return (ok);
}

/* init core_map from NV, or set a default
 * return whether ok
 */
void initCoreMaps()
{
        // init map from NV if present and valid
        char s[NV_COREMAPSTYLE_LEN];
        core_map = CM_NONE;
        if (NVReadString (NV_COREMAPSTYLE, s)) {
            for (int i = 0; i < CM_N; i++) {
                if (strcmp (cm_info[i].name, s) == 0) {
                    core_map = (CoreMaps)i;
                    break;
                }
            }
        } else
            NVWriteString (NV_COREMAPSTYLE, cm_info[CM_COUNTRIES].name);

        // init map_rotset
        if (!NVReadUInt16 (NV_MAPROTSET, &map_rotset)) {
            map_rotset = 0;
            NVWriteUInt16 (NV_MAPROTSET, map_rotset);
        }

        // init CM_USER if not installed
        if (IS_CMROT(CM_USER) && !allWebMapImagesOk()) {
            Serial.printf ("removing CM_USER from initial rotset\n");
            RM_CMROT (CM_USER);
            insureCoreMap();
            saveCoreMaps();
        }

        // init BC bands
        uint8_t band;
        if (!NVReadUInt8 (NV_BCTOABAND, &band)) {
            band = PROPBAND_NONE;
            NVWriteUInt8 (NV_BCTOABAND, band);
        }
        if (band >= PROPBAND_NONE)
            RM_CMROT (CM_PMTOA);
        cm_info[CM_PMTOA].band = (PropMapBand) band;

        if (!NVReadUInt8 (NV_BCRELBAND, &band)) {
            band = PROPBAND_NONE;
            NVWriteUInt8 (NV_BCRELBAND, band);
        }
        if (band >= PROPBAND_NONE)
            RM_CMROT (CM_PMREL);
        cm_info[CM_PMREL].band = (PropMapBand) band;

        // insure sane
        insureCoreMap();

        // log initial settings
        logMapRotSet();
}

/* save map_rotset core_map and BC bands
 */
void saveCoreMaps(void)
{
        if ((int)core_map >= CM_N)
            fatalError ("saveMapRotSet() core_map %d\n", core_map);

        NVWriteString (NV_COREMAPSTYLE, cm_info[core_map].name);
        NVWriteUInt16 (NV_MAPROTSET, map_rotset);
        NVWriteUInt8 (NV_BCTOABAND, cm_info[CM_PMTOA].band);
        NVWriteUInt8 (NV_BCRELBAND, cm_info[CM_PMREL].band);
}

/* log map_rotset and core_map
 */
void logMapRotSet(void)
{
        char s[NV_COREMAPSTYLE_LEN];
        char line[512];
        size_t ll = 0;
        ll += snprintf (line+ll, sizeof(line)-ll, "Active Map styles: ");
        for (int i = 0; i < CM_N; i++) {
            int ci = (core_map + i) % CM_N;             // start with core_map
            if (IS_CMROT(ci))
                ll += snprintf (line+ll, sizeof(line)-ll, "%s ", getCoreMapStyle ((CoreMaps)ci, s));
        }
        Serial.printf ("%s\n", line);
}

/* return the given map style name, suitable for menu or log entries.
 * N.B. not for file names
 */
const char *getCoreMapStyle (CoreMaps cm, char s[NV_COREMAPSTYLE_LEN])
{
        const char *name = cm_info[cm].name;

        if (cm == CM_PMTOA || cm == CM_PMREL)
            snprintf (s, NV_COREMAPSTYLE_LEN, "%dm/%s", propBand2Band (cm_info[cm].band), name);
        else
            snprintf (s, NV_COREMAPSTYLE_LEN, "%s", name);

        return (s);
}

/* return MHz for the given PropMapBand
 * N.B. match column headings in voacapx.out
 */
float propBand2MHz (PropMapBand band)
{
        switch (band) {
        case PROPBAND_80M: return ( 3.6);
        case PROPBAND_40M: return ( 7.1);
        case PROPBAND_30M: return (10.1);
        case PROPBAND_20M: return (14.1);
        case PROPBAND_17M: return (18.1);
        case PROPBAND_15M: return (21.1);
        case PROPBAND_12M: return (24.9);
        case PROPBAND_10M: return (28.2);
        default: fatalError ("propBand2MHz MHz %d", band);
        }

        // lint
        return (0);
}

/* return band for the given PropMapBand
 */
int propBand2Band (PropMapBand band)
{
        switch (band) {
        case PROPBAND_80M: return (80);
        case PROPBAND_40M: return (40);
        case PROPBAND_30M: return (30);
        case PROPBAND_20M: return (20);
        case PROPBAND_17M: return (17);
        case PROPBAND_15M: return (15);
        case PROPBAND_12M: return (12);
        case PROPBAND_10M: return (10);
        default: fatalError ("propBand2Band %d", band);
        }

        // lint
        return (0);
}


/* return whether the map scale is (or should be) visible now
 * N.B. must agree with drawMapScale()
 */
bool mapScaleIsUp(void)
{
    switch (core_map) {
    case CM_DRAP:
    case CM_MUF_V:
    case CM_MUF_RT:
    case CM_AURORA:
    case CM_WX:
    case CM_PMTOA:
    case CM_PMREL:
    case CM_TROPO:
        return (true);
    case CM_COUNTRIES:
    case CM_TERRAIN:
    case CM_CLOUDS:
    case CM_USER:
    case CM_N:          // lint
        return (false);
    }
    return (false);
}

/* draw the appropriate scale at mapscale_b depending on core_map, if any.
 * N.B. we move mapscale_b depending on rss_on
 */
void drawMapScale()
{
    // color scale. values must be monotonically increasing.
    typedef struct {
        float value;                                    // world value
        uint32_t color;                                 // 24 bit RGB scale color
        bool black_text;                                // black text, else white
    } MapScalePoint;

    // CM_DRAP and CM_MUF_V and CM_MUF_RT
    static const MapScalePoint d_scale[] = {            // see fetchDRAP.pl and fetchVOACAP-MUF.pl
        {0,  0x000000, 0},
        {4,  0x4E138A, 0},
        {9,  0x001EF5, 0},
        {15, 0x78FBD6, 1},
        {20, 0x78FA4D, 1},
        {27, 0xFEFD54, 1},
        {30, 0xEC6F2D, 1},
        {35, 0xE93323, 1},
    };

    // CM_AURORA
    static const MapScalePoint a_scale[] = {            // see fetchAurora.pl
        {0,   0x282828, 0},
        {25,  0x00FF00, 1},
        {50,  0xFFFF00, 1},
        {75,  0xEA6D2D, 1},
        {100, 0xFF0000, 1},
    };

    // CM_WX
    static const MapScalePoint w_scale[] = {            // see fetchWordWx.pl
        // values are degs C
        {-50,  0xD1E7FF, 1},
        {-40,  0xB5D5FF, 1},
        {-30,  0x88BFFF, 1},
        {-20,  0x73AAFF, 1},
        {-10,  0x4078D9, 0},
        {0,    0x2060A6, 0},
        {10,   0x009EDC, 1},
        {20,   0xBEE5B4, 1},
        {30,   0xFF8C24, 1},
        {40,   0xEE0051, 1},
        {50,   0x5B0023, 1},
    };

    // CM_PMTOA
    static const MapScalePoint t_scale[] = {            // see fetchVOACAP-TOA.pl
        {0,    0x0000F0, 0},
        {6,    0xF0B060, 1},
        {30,   0xF00000, 1},
    };

    // CM_PMREL
    static const MapScalePoint r_scale[] = {            // see fetchVOACAPArea.pl
        {0,    0x666666, 0},
        {21,   0xEE6766, 0},
        {40,   0xEEEE44, 1},
        {60,   0xEEEE44, 1},
        {83,   0x44CC44, 0},
        {100,  0x44CC44, 0},
    };

    static const MapScalePoint tropo_scale[] = {
        {0,  0x444444, 0},  
        {1,  0x8603F1, 0},   
        {2,  0x01B4EF, 1},   
        {3,  0x02D083, 1},   
        {4,  0xA5EB01, 1},  
        {5,  0xEFDE05, 1},   
        {6,  0xE9B10C, 1},   
        {7,  0xFF8000, 1},   
        {8,  0xFF0000, 0},   
        {9,  0xFF80C0, 1},   
        {10, 0xFFB4DC, 1},   
    };

    // set these depending on map
    const MapScalePoint *msp = NULL;                    // one of above tables
    unsigned n_scale = 0;                               // n entries in table
    unsigned n_labels = 0;                              // n labels in scale
    const char *title = NULL;                           // scale title

    switch (core_map) {
    case CM_COUNTRIES:
    case CM_TERRAIN:
    case CM_CLOUDS:
    case CM_USER:
    case CM_N:                                          // lint
        // no scale
        return;
    case CM_MUF_V:          // fallthru
    case CM_MUF_RT:         // fallthru
    case CM_DRAP:
        msp = d_scale;
        n_scale = NARRAY(d_scale);
        n_labels = 8;
        title = "MHz";
        break;
    case CM_AURORA:
        msp = a_scale;
        n_scale = NARRAY(a_scale);
        n_labels = 11;
        title = "% Chance";
        break;
    case CM_TROPO:
        msp = tropo_scale;
        n_scale = NARRAY(tropo_scale);
        n_labels = 11;
        title = "Index";
        break;
    case CM_WX:
        msp = w_scale;
        n_scale = NARRAY(w_scale);
        n_labels = showTempC() ? 11 : 10;
        title = "Degs C";
        break;
    case CM_PMTOA:
        msp = t_scale;
        n_scale = NARRAY(t_scale);
        n_labels = 7;
        title = "DE TOA, degs";
        break;
    case CM_PMREL:
        msp = r_scale;
        n_scale = NARRAY(r_scale);
        n_labels = 6;
        title = "% Reliability";
        break;
    }


    // handy accessors
    #define _MS_PTV(i)  (msp[i].value)                          // handy access to msp[i].value
    #define _MS_PTC(i)  (msp[i].color)                          // handy access to msp[i].color
    #define _MS_PTB(i)  (msp[i].black_text)                     // handy access to msp[i].black_text

    // geometry setup
    #define _MS_X0      mapscale_b.x                            // left x
    #define _MS_X1      (mapscale_b.x + mapscale_b.w)           // right x
    #define _MS_DX      (_MS_X1-_MS_X0)                         // width
    #define _MS_MINV    _MS_PTV(0)                              // min value
    #define _MS_MAXV    _MS_PTV(n_scale-1)                      // max value
    #define _MS_DV      (_MS_MAXV-_MS_MINV)                     // value span
    #define _MS_V2X(v)  (_MS_X0 + _MS_DX*((v)-_MS_MINV)/_MS_DV) // convert value to x
    #define _MS_PRY     (mapscale_b.y+1U)                       // text y

    // set mapscale_b.y above RSS if on else at the bottom
    mapscale_b.y = rss_on ? rss_bnr_b.y - mapscale_b.h: map_b.y + map_b.h - mapscale_b.h;

    // draw smoothly-interpolated color scale
    for (unsigned i = 1; i < n_scale; i++) {
        uint8_t dm = _MS_PTV(i) - _MS_PTV(i-1);
        uint8_t r0 = _MS_PTC(i-1) >> 16;
        uint8_t g0 = (_MS_PTC(i-1) >> 8) & 0xFF;
        uint8_t b0 = _MS_PTC(i-1) & 0xFF;
        uint8_t r1 = _MS_PTC(i) >> 16;
        uint8_t g1 = (_MS_PTC(i) >> 8) & 0xFF;
        uint8_t b1 = _MS_PTC(i) & 0xFF;
        for (uint16_t x = _MS_V2X(_MS_PTV(i-1)); x <= _MS_V2X(_MS_PTV(i)); x++) {
            if (x < mapscale_b.x + mapscale_b.w) {              // the _MS macros can overflow slightlty
                float value = _MS_MINV + (float)_MS_DV*(x - _MS_X0)/_MS_DX;
                float frac = CLAMPF ((value - _MS_PTV(i-1))/dm,0,1);
                uint16_t new_c = RGB565(r0+frac*(r1-r0), g0+frac*(g1-g0), b0+frac*(b1-b0));
                tft.drawLine (x, mapscale_b.y, x, mapscale_b.y+mapscale_b.h-1, 1, new_c);
            }
        }
    }

    // determine marker location, if used
    uint16_t marker_x = 0;
    float v = 0;
    bool v_ok = false;
    if (core_map == CM_DRAP) {
        (void) checkForNewDRAP();
        if (space_wx[SPCWX_DRAP].value_ok) {
            v = space_wx[SPCWX_DRAP].value;
            v_ok = true;
        }
    } else if (core_map == CM_AURORA) {
        (void) checkForNewAurora();
        if (space_wx[SPCWX_AURORA].value_ok) {
            v = space_wx[SPCWX_AURORA].value;
            v_ok = true;
        }
    } else if (core_map == CM_TROPO) {
        // N.B. unlike DRAP/Aurora above, there's no independent "current value" feed to poll --
        // Hepburn doesn't publish one and neither do we. Instead read whatever pixel is actually
        // on screen under the cursor right now (getPixelRaw already reflects the correct
        // projection/zoom/pan and day-night blend, so none of that needs re-deriving here) and
        // nearest-match it against the same 11 colours the legend itself is drawn from, so this
        // stays correct automatically if tropo_scale[] is ever retuned.
        //
        // The raw tropo_scale colour never actually appears on screen unblended -- the rendered
        // map is always tinted (see update_tropo_maps.sh's make_bmp_v4_rgb565_topdown: 20% blend
        // toward 205/220/205 by day, brightness x0.15 by night), so match against BOTH tinted
        // candidates, derived here from the same table, and take whichever is closer. Verified
        // this is exact away from the day/night terminator; in the narrow blended band right at
        // the terminator itself, compositing two different colours can produce a third colour
        // that's genuinely closer to some OTHER band's tinted variant -- that's an inherent limit
        // of colour-matching a blended pixel, not something fixable here.
        uint16_t mx, my;
        if (tft.getMouse (&mx, &my)) {
            SCoord ms = {mx, my};
            uint8_t pr, pg, pb;
            if (overMap(ms) && tft.getPixelRaw (mx, my, &pr, &pg, &pb)) {
                int best_i = -1;
                long best_d2 = 0;
                for (unsigned i = 0; i < n_scale; i++) {
                    uint8_t cr = _MS_PTC(i) >> 16, cg = (_MS_PTC(i) >> 8) & 0xFF, cb = _MS_PTC(i) & 0xFF;
                    long ddr = (long)pr - (long)(cr*0.8F + 205*0.2F);
                    long ddg = (long)pg - (long)(cg*0.8F + 220*0.2F);
                    long ddb = (long)pb - (long)(cb*0.8F + 205*0.2F);
                    long dday2 = ddr*ddr + ddg*ddg + ddb*ddb;
                    long dnr = (long)pr - (long)(cr*0.15F);
                    long dng = (long)pg - (long)(cg*0.15F);
                    long dnb = (long)pb - (long)(cb*0.15F);
                    long dnight2 = dnr*dnr + dng*dng + dnb*dnb;
                    long d2 = dday2 < dnight2 ? dday2 : dnight2;
                    if (best_i < 0 || d2 < best_d2) {
                        best_d2 = d2;
                        best_i = (int)i;
                    }
                }
                if (best_i >= 0) {
                    v = _MS_PTV(best_i);
                    v_ok = true;
                }
            }
        }
        // v_ok simply stays false whenever the cursor isn't over the map (or app), which is exactly
        // what makes the marker disappear next redraw -- drawMapScale() repaints the whole gradient
        // bar unconditionally every call (see the loop above), so there is nothing to explicitly
        // erase; an absent marker this time overwrites a present one from last time for free.
    }
    if (v_ok) {
        // find marker but beware range overflow and leave room for full width
        float clamp_v = CLAMPF (v, _MS_MINV, _MS_MAXV);
        marker_x = CLAMPF (_MS_V2X(clamp_v), mapscale_b.x + 3, mapscale_b.x + mapscale_b.w - 4);
    }

    // draw labels inside mapscale_b but may need to build F scale for WX

    // use labels directly unless need to create F weather scale
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    StackMalloc ticks_v_mem((n_labels+2)*sizeof(float));
    StackMalloc ticks_x_mem((n_labels+2)*sizeof(uint16_t));
    float *ticks_v = (float *) ticks_v_mem.getMem();
    uint16_t *ticks_x = (uint16_t *) ticks_x_mem.getMem();
    int n_ticks;
    const char *my_title;

    // prep values and center x locations
    if (core_map == CM_WX && !showTempC()) {

        // switch to F scale
        my_title = "Degs F";
        n_ticks = tickmarks (CEN2FAH(_MS_MINV), CEN2FAH(_MS_MAXV), n_labels, ticks_v);
        for (int i = 0; i < n_ticks; i++) {
            float value = roundf(ticks_v[i]);                   // value printed is F ...
            float value_c = FAH2CEN(value);                     // ... but position is based on C
            ticks_v[i] = value;
            ticks_x[i] = _MS_V2X(value_c);
        }

    } else {

        // generate evenly-spaced labels from min to max, inclusive
        my_title = title;
        n_ticks = n_labels;
        for (unsigned i = 0; i < n_labels; i++) {
            ticks_v[i] = _MS_MINV + _MS_DV*i/(n_labels-1);
            ticks_x[i] = _MS_V2X(ticks_v[i]);
        }

    }

    // print tick marks across mapscale_b but avoid marker
    for (int i = 1; i < n_ticks; i++) {                         // skip first for title

        // skip if off scale or near marker
        const uint16_t ti_x = ticks_x[i];
        if (ti_x < _MS_X0 || ti_x > _MS_X1 || (marker_x && ti_x >= marker_x - 15 && ti_x <= marker_x + 15))
            continue;

        // center but beware edges (we already skipped first so left edge is never a problem)
        char buf[20];
        snprintf (buf, sizeof(buf), "%.0f", ticks_v[i]);
        uint16_t buf_w = getTextWidth(buf);
        uint16_t buf_lx = ti_x - buf_w/2;
        uint16_t buf_rx = buf_lx + buf_w;
        if (buf_rx > _MS_X1 - 2) {
            buf_rx = _MS_X1 - 2;
            buf_lx = buf_rx - buf_w;
        }
        tft.setCursor (buf_lx, _MS_PRY);
        tft.setTextColor (_MS_PTB(i*n_scale/n_ticks) ? RA8875_BLACK : RA8875_WHITE);
        tft.print (buf);
    }

    // draw scale meaning
    tft.setTextColor (_MS_PTB(0) ? RA8875_BLACK : RA8875_WHITE);
    tft.setCursor (_MS_X0 + 4, _MS_PRY);
    tft.print (my_title);

    // draw marker
    if (marker_x) {
        // use lines, not rect, for a perfect vetical match to scale
        tft.drawLine (marker_x-2, mapscale_b.y, marker_x-2, mapscale_b.y+mapscale_b.h-1, 1, RA8875_BLACK);
        tft.drawLine (marker_x-1, mapscale_b.y, marker_x-1, mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (marker_x,   mapscale_b.y, marker_x,   mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (marker_x+1, mapscale_b.y, marker_x+1, mapscale_b.y+mapscale_b.h-1, 1, RA8875_RED);
        tft.drawLine (marker_x+2, mapscale_b.y, marker_x+2, mapscale_b.y+mapscale_b.h-1, 1, RA8875_BLACK);
    }

}

/* log and show message in a nice box over map_b.
 */
void mapMsg (uint32_t dwell_ms, const char *fmt, ...)
{
    // format msg
    va_list ap;
    va_start(ap, fmt);
    char msg[500];
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end(ap);

    // log
    Serial.printf ("mapMsg: %s\n", msg);

    // get msg width
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    uint16_t msg_w = getTextWidth(msg);

    // set constant box size but allow larger
    const uint16_t margin = 50;
    uint16_t box_w = map_b.w - 200;
    if (msg_w + margin > box_w) {
        box_w = map_b.w - 20;
        msg_w = maxStringW (msg, box_w-margin);
    }

    // draw both centered
    uint16_t msg_y = map_b.y + map_b.h/10;
    tft.fillRect (map_b.x + (map_b.w-box_w)/2, msg_y, box_w, 30, RA8875_BLUE);
    tft.setCursor (map_b.x + (map_b.w-msg_w)/2, msg_y + 12);
    tft.print(msg);
    tft.drawPR();

    // dwell with clocks
    while (dwell_ms > 200) {
        usleep (200000);
        updateClocks (false);
        dwell_ms -= 200;
    }
}
/* return whether background maps are rotating
 */
bool mapIsRotating()
{
    // rotating if more than 1 bit is on
    return ((map_rotset & (map_rotset-1U)) != 0);       // removes lowest set bit
}

/* make sure core_map is set to one of the entries in map_rotset, else set a default
 */
void insureCoreMap()
{
    // done if core_map is already in the set
    if (IS_CMROT(core_map))
        return;

    // pick another
    core_map = CM_NONE;
    for (int i = 0; i < CM_N && core_map == CM_NONE; i++)
        if (IS_CMROT(i))
            core_map = (CoreMaps)i;

    // if none at all, set a default
    if (core_map == CM_NONE)
        DO_CMROT(CM_COUNTRIES);
}


/* return next map refresh time: if rotating use getMapRotationPeriod() else the given interval
 */
time_t nextMapUpdate (int interval)
{
    time_t next_t = myNow();
    if (mapIsRotating())
        next_t += getMapRotationPeriod();
    else
        next_t += interval;
    return (next_t);
}

/* update core_map per map_rotset.
 * N.B. we assume mapIsRotating() is rtue.
 */
void rotateNextMap()
{
    // rotate to the "next" CoreMaps bit after core_map
    int new_cm = -1;
    for (int i = 1; i < CM_N; i++) {
        int ci = (core_map + i) % CM_N;
        if (IS_CMROT(ci)) {
            new_cm = ci;
            break;
        }
    }
    if (new_cm < 0)
        fatalError ("Bogus map rotation set: 0x%x\n", map_rotset);
    core_map = (CoreMaps) new_cm;
}


/* Decompress in_n bytes from client to out_fp until stream ends or EOF.
 * return whether successful.
 * modeled after zpipe.
 */
bool zinfWiFiFILE (WiFiClient &client, int in_n, FILE *out_fp)
{
    #define CHUNK 16384
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];
    int in_read = 0;
    int out_n = 0;

    // allocate inflate state
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK) {
        Serial.printf ("inflateInit failed: %s\n", strm.msg);
        return (false);
    }

    // decompress until deflate stream ends, read in_n bytes or end of file
    do {
        // fill in[]
        int n_read = in_n - in_read;
        if (n_read > CHUNK)
            n_read = CHUNK;
        strm.avail_in = client.readArray (in, n_read);
        if (strm.avail_in == 0)
            break;
        in_read += strm.avail_in;
        strm.next_in = in;

        // run inflate() on input until output buffer not full
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     // fall thru
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                Serial.printf ("unzip error: %s\n", strm.msg);
                (void)inflateEnd(&strm);
                return (false);
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, out_fp) != have || ferror(out_fp)) {
                Serial.printf ("unzip write err: %s\n", strerror(errno));
                (void)inflateEnd(&strm);
                return (false);
            }
            out_n += have;
        } while (strm.avail_out == 0);

        // done when inflate() says it's done
    } while (ret != Z_STREAM_END);

    // clean up and return.
    (void)inflateEnd(&strm);
    if (ret == Z_STREAM_END) {
        // N.B. an empty file will return Z_STREAM_END!
        if (out_n > in_n) {
            Serial.printf ("inflated %d -> %d\n", in_n, out_n);
            return (true);
        }
        Serial.printf ("inflate did not expand: %d -> %d\n", in_n, out_n);
        return (false);
    } else {
        Serial.printf ("inflate failed: %s\n", strm.msg);
        return (false);
    }
}

/* like fopen() but filename is in our working directory.
 * chown to read uid if creating.
 */
FILE *fopenOurs (const char *filename, const char *how)
{
    // full path
    std::string dp = our_dir + filename;
    const char *full = dp.c_str();
    FILE *fp = fopen (full, how);

    // chown to the real us if creating
    if (fp && (strchr(how,'w') || strchr(how,'a')) && fchown(fileno(fp), getuid(), getgid()) < 0)
        Serial.printf ("chown(%s): %s\n", filename, strerror(errno));

    return (fp);
}

/* like unlink() but filename is in our working directory
 */
void unlinkOurs (const char *filename)
{
    std::string dp = our_dir + filename;
    const char *path = dp.c_str();
    if (unlink (path) < 0)
        Serial.printf ("unlink(%s): %s\n", filename, strerror(errno));
}
