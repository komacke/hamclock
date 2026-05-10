/* implement a live web server connection so browsers can see and control HamClock.
 *
 * we listen to liveweb_rw_port and liveweb_ro_port for live.html or web socket upgrades.
 *
 * Browser displays entire HamClock frame buffer. Complete frame is sent initially then only the
 * pixels that change.
 *
 * N.B. this server-side code must work in concert with client-side code in liveweb-html.cpp.
 */


#include "HamClock.h"

// web socket library
#include "ws.h"


// import png writer -- complete implementation in a header file -- amazing.
// https://github.com/nothings/stb
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "zlib.h"
static unsigned char * myZDeflate(unsigned char *data, int data_len, int *out_len, int quality);
#define STBIW_ZLIB_COMPRESS myZDeflate
#include "stb_image_write.h"



// public
int n_roweb, n_rwweb;                                   // stats for user agent
bool liveweb_fs_ready;                                  // set when ok to send fullscreen command
int liveweb_rw_port = LIVEWEB_RW_PORT;                  // r/w server port -- can be changed with -w
int liveweb_ro_port = LIVEWEB_RO_PORT;                  // r/o server port -- can be changed with -r
int liveweb_max = 10;                                   // max allowed connections < ws.h::MAX_CLIENTS
int liveweb_to;                                         // client inactivity timeout, minutes
const int liveweb_maxmax = MAX_CLIENTS-1;               // max max for -help


// png format is 3 bytes per pixel
#define LIVE_NPIX       (BUILD_H*BUILD_W)               // pixels per complete image
#define LIVE_NBYTES     (LIVE_NPIX*LIVE_BYPPIX)         // bytes per complete image
#define LIVE_RBYTES     (BUILD_W*LIVE_BYPPIX)           // bytes per row
#define COMP_RGB        3                               // composition request code for RGB pixels



// record client and possible URL for it to display.
static ws_cli_conn_t *lastest_ws_touch_client;          // most recent client performing set_touch
static char *liveweb_openurl;                           // malloced url to attempt to open, else NULL
static pthread_mutex_t lw_url_lock = PTHREAD_MUTEX_INITIALIZER; // thread-safe access for liveweb_openurl


// complete scene on browser for each web socket
typedef struct {
    ws_cli_conn_t *client;                              // pointer unique to each connection, else NULL
    uint8_t *pixels;                                    // this client's current display image
    int pixel_users;                                    // active borrowers of this pixels buffer
    bool pixels_retiring;                               // close has begun; free pixels once the borrowers are gone
} SessionInfo;
static SessionInfo *si_list;                            // malloced list
static int si_n;                                        // n malloced
static pthread_mutex_t si_lock = PTHREAD_MUTEX_INITIALIZER;     // atomic updates

#if defined(__GNUC__)
static void bye (const char *fmt, ...) __attribute__ ((format (__printf__, 1, 2)));
#else
static void bye (const char *fmt, ...);
#endif


/* we can't use fatalError because the main thread will keep running and quickly obscure it.
 * N.B. we assume final message will include trailing \n
 */
static void bye (const char *fmt, ...)
{
    Serial.printf ("LIVE: ");

    va_list ap;
    va_start (ap, fmt);
    vprintf (fmt, ap);
    va_end (ap);

    exit(1);
}

/* zlib deflator used by stb_image_write.h, see there for usage.
 * patterned after zpipe def().
 */
static unsigned char * myZDeflate(unsigned char *data, int data_len, int *out_len, int quality)
{
    int ret, flush;
    z_stream strm;

    /* output buffer surely less than data_len but allow for some unknown header size*/
    int n_out = data_len + 1000;
    unsigned char *out_mem = (unsigned char *) malloc (n_out);
    if (!out_mem)
        bye ("myZDeflate no memory for %d\n", n_out);

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, quality);
    if (ret != Z_OK)
        bye ("myZDeflate deflateInit returns %d\n", ret);

    /* compress data_len in one gulp */
    strm.avail_in = data_len;
    strm.next_in = data;
    flush = Z_FINISH;
    strm.avail_out = n_out;
    strm.next_out = out_mem;
    ret = deflate(&strm, flush);
    if (ret != Z_STREAM_END)
        bye ("myZDeflate error %d %d/%d\n", ret, *out_len, data_len);

    /* done */
    *out_len = n_out - strm.avail_out;
    (void)deflateEnd(&strm);
    // printf ("myZDeflate %d -> %d %d%%\n", data_len, *out_len, 100 * *out_len/data_len);      // RBF
    return (out_mem);
}

/* stbi_write_png_to_func helper to write the given array to the given ws_cli_conn_t in context.
 */
static void wifiSTBWrite_helper (void *context, void *data, int size)
{
    ws_cli_conn_t *client = (ws_cli_conn_t *)context;
    int n_sent = ws_sendframe_bin (client, (const char *) data, size);
    if (n_sent != size)
        Serial.printf ("LIVE: client %s: wrong png write len: %d != %d\n", ws_getaddress(client), n_sent, size);
    if (debugLevel (DEBUG_WEB, 2)) {
        Serial.printf ("LIVE: sent image %d bytes\n", size);
        if (debugLevel (DEBUG_WEB, 3)) {
            FILE *fp = fopen ("/tmp/live.png", "w");
            fwrite (data, size, 1, fp);
            fclose(fp);
        }
    }
}

/* borrow pixels for the given client and keep them alive until return_session_pixels().
 */
static uint8_t *borrow_session_pixels (ws_cli_conn_t *client)
{
    pthread_mutex_lock (&si_lock);

    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (sip->client == client && sip->pixels) {
            sip->pixel_users++;
            uint8_t *pixels = sip->pixels;
            pthread_mutex_unlock (&si_lock);
            return (pixels);
        }
    }

    pthread_mutex_unlock (&si_lock);
    Serial.printf ("LIVE: client %s: no session pixels to borrow\n", ws_getaddress(client));
    return (NULL);
}

/* give back pixels borrowed from borrow_session_pixels().
 */
static void return_session_pixels (uint8_t *pixels)
{
    pthread_mutex_lock (&si_lock);

    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (sip->pixels == pixels) {
            if (sip->pixel_users > 0)
                sip->pixel_users--;
            if (sip->pixels_retiring && sip->pixel_users == 0) {
                free (sip->pixels);
                sip->pixels = NULL;
                sip->pixels_retiring = false;
            }
            break;
        }
    }

    pthread_mutex_unlock (&si_lock);
}

/* send difference between client's last known screen image and the current image,
 * then store current image back in client's SessionInfo.
 */
static void updateExistingClient (ws_cli_conn_t *client)
{
    // curious how long these steps take
    struct timeval tv0;
    gettimeofday (&tv0, NULL);

    // use the client's pixels buffer directly, protected by a borrowed reference
    uint8_t *pixels = borrow_session_pixels(client);
    if (!pixels)
        return;

    // keep only a copy of the client's prior image for diffing
    uint8_t *img_client = (uint8_t *) malloc (LIVE_NBYTES);
    if (!img_client) {
        return_session_pixels (pixels);
        bye ("No memory for LIVE update\n");
    }
    memcpy (img_client, pixels, LIVE_NBYTES);

    // replace client's current image with a fresh capture
    if (!tft.getRawPix (pixels, LIVE_NPIX))
        bye ("getRawPix for update failed\n");
    uint8_t *img_now = pixels;

    if (debugLevel (DEBUG_WEB, 2)) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: copying img and reading new pixels took %ld usec\n",
                                ws_getaddress(client), TVDELUS (tv0,tv1));
    }

    // draw connection counter on main page
    if (mainpage_up) {
        #define CTR_RAWW (3*tft.SCALESZ)
        #define CTR_RAWH (5*tft.SCALESZ)
        #define CTR_RAWX (tft.SCALESZ*(lkscrn_b.x-4))
        #define CTR_RAWY (tft.SCALESZ*(lkscrn_b.y+lkscrn_b.h+3))
        SBox digit_b = {(uint16_t)CTR_RAWX, (uint16_t)CTR_RAWY, (uint16_t)CTR_RAWW, (uint16_t)CTR_RAWH};
        if (client->port == liveweb_ro_port) {
            static const uint8_t txt_clr[LIVE_BYPPIX] = {255U,50U,50U};
            // n_roweb = 1234567890;       // RBF
            if (n_roweb < 10)
                digit_b.x += CTR_RAWW;
            drawImgNumber (n_roweb, img_now, digit_b, txt_clr);
            digit_b.x += 2*digit_b.w/3;
            drawImgR (img_now, digit_b, txt_clr);
            digit_b.x += 3*digit_b.w/2;
            drawImgO (img_now, digit_b, txt_clr);
        } else {
            static const uint8_t txt_clr[LIVE_BYPPIX] = {255U,255U,255U};
            // n_rwweb = 1234567890;       // RBF
            if (n_rwweb < 10)
                digit_b.x += CTR_RAWW;
            drawImgNumber (n_rwweb, img_now, digit_b, txt_clr);
            digit_b.x += 2*digit_b.w/3;
            drawImgR (img_now, digit_b, txt_clr);
            digit_b.x += 3*digit_b.w/2;
            drawImgW (img_now, digit_b, txt_clr);
        }
    }

    // we only send small regions that have changed since previous, ie changes from img_client to img_now.
    // image is divided into fixed sized blocks and those which have changed are coalesced into regions
    // of height one block but variable length. these are collected and sent as one image of height one
    // block preceded by a header defining the location and size of each region. the coordinates and
    // length of a region are in units of blocks, not pixels, to reduce each value's size to one byte
    // each in the header. smaller regions are more efficient but the coords must fit in 8 bit header value.
    #define BLOK_W      (BUILD_W>1600?16:8)             // pixels wide
    #define BLOK_H      8                               // pixels high
    #define BLOK_NCOLS  (BUILD_W/BLOK_W)                // blocks in each row over entire image
    #define BLOK_NROWS  (BUILD_H/BLOK_H)                // blocks in each col over entire image
    #define BLOK_NPIX   (BLOK_W*BLOK_H)                 // size of 1 block, pixels
    #define BLOK_NBYTES (BLOK_NPIX*LIVE_BYPPIX)         // size of 1 block, bytes
    #define BLOK_WBYTES (BLOK_W*LIVE_BYPPIX)            // width of 1 block, bytes
    #define MAX_REGNS   (BLOK_NCOLS*BLOK_NROWS)         // worse case number of regions
    #if BLOK_NCOLS > 255                                // insure fits into uint8_t
        #error too many block columns
    #endif
    #if BLOK_NROWS > 255                                // insure fits into uint8_t
        #error too many block rows
    #endif
    #if MAX_REGNS > 65535                               // insure fits into uint16_t
        #error too many live regions
    #endif

    // time block creation
    gettimeofday (&tv0, NULL);

    // set header to location and length of each changed region.
    typedef struct {
        uint8_t x, y, l;                                // region location and length in units of blocks
    } RegnLoc;
    RegnLoc locs[MAX_REGNS];                            // room for max number of header region entries
    uint16_t n_regns = 0;                               // n regions defined so far
    int n_bloks = 0;                                    // n blocks within all regions so far

    // build locs by checking each region for change across then down
    for (int ry = 0; ry < BLOK_NROWS; ry++) {

        // pre-check an image band all the way across BLOK_COLS hi, skip entirely if no change anywhere
        int band_start = ry*LIVE_BYPPIX*BLOK_H*BUILD_W;
        if (memcmp (&img_now[band_start], &img_client[band_start], LIVE_BYPPIX*BLOK_H*BUILD_W) == 0)
            continue;

        // something changed, scan across this band checking each block
        locs[n_regns].l = 0;                            // init n contiguous blocks that start here
        for (int rx = 0; rx < BLOK_NCOLS; rx++) {
            int blok_start = band_start + rx*BLOK_WBYTES;
            uint8_t *now0 = &img_now[blok_start];       // first pixel in this block of current image
            uint8_t *pre0 = &img_client[blok_start];    // first pixel in this block of image in client

            // check each row of this block for any change, start or add to region 
            bool blok_changed = false;                  // set if any changed pixels in this block
            for (int rr = 0; rr < BLOK_H; rr++) {
                if (memcmp (now0+rr*LIVE_BYPPIX*BUILD_W, pre0+rr*LIVE_BYPPIX*BUILD_W, BLOK_WBYTES) != 0) {
                    blok_changed = true;
                    break;
                }
            }

            // add to or start new region
            if (blok_changed) {
                if (locs[n_regns].l == 0) {
                    locs[n_regns].x = rx;
                    locs[n_regns].y = ry;
                }
                locs[n_regns].l++;
                n_bloks++;
            } else {
                if (locs[n_regns].l > 0) {
                    n_regns++;
                    locs[n_regns].l = 0;
                }
            }
        }

        // add last region too if started
        if (locs[n_regns].l > 0) {
            n_regns++;
        }
    }

    // now create one wide image containing each region as a separate sprite.
    // remember each region must work as a separate image of size lx1 blocks.
    uint8_t *chg_regns = (uint8_t*) malloc (n_bloks * BLOK_NBYTES);
    if (!chg_regns)
        bye ("No memory for sprites %d\n", n_bloks);
    uint8_t *chg0 = chg_regns;
    for (int ry = 0; ry < BLOK_H; ry++) {
        for (int i = 0; i < n_regns; i++) {
            RegnLoc *rp = &locs[i];
            uint8_t *now0 = &img_now[BUILD_W*LIVE_BYPPIX*(ry+BLOK_H*rp->y) + BLOK_WBYTES*rp->x];
            memcpy (chg0, now0, BLOK_WBYTES*rp->l);
            chg0 += BLOK_WBYTES*rp->l;
        }
    }
    if (n_bloks != (chg0-chg_regns)/BLOK_NBYTES)        // assert
        bye ("live regions %d != %d\n", n_bloks, (int)((chg0-chg_regns)/BLOK_NBYTES));

    if (debugLevel (DEBUG_WEB, 2)) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: built %d regions from %d blocks in %ld usec\n",
                        ws_getaddress(client), n_regns, n_bloks, TVDELUS (tv0, tv1));
    }

    // time header creation and png write
    gettimeofday (&tv0, NULL);

    // build 4-byte header followed by x,y,l of each of n regions in units of blocks.
    unsigned hdr_l = 4+3*n_regns;
    StackMalloc hdr_mem(hdr_l);
    uint8_t *hdr = (uint8_t *) hdr_mem.getMem();
    hdr[0] = BLOK_W;                            // block width, pixels
    hdr[1] = BLOK_H;                            // block height, pixels
    hdr[2] = n_regns >> 8;                      // n regions, MSB
    hdr[3] = n_regns & 0xff;                    // n regions, LSB
    for (int i = 0; i < n_regns; i++) {
        hdr[4+3*i] = locs[i].x;
        hdr[5+3*i] = locs[i].y;
        hdr[6+3*i] = locs[i].l;
        if (debugLevel (DEBUG_WEB, 3))
            Serial.printf ("   %d,%d %dx%d\n", locs[i].x*BLOK_W, locs[i].y*BLOK_H, locs[i].l*BLOK_W, BLOK_H);
    }

    // always send header
    unsigned n_hdrsent = ws_sendframe_bin (client, (const char *) hdr, hdr_l);
    if (n_hdrsent != hdr_l)
        Serial.printf ("LIVE: client %s: wrong header write %u != %d\n", ws_getaddress(client), n_hdrsent, hdr_l);

    // followed by one image containing one column BLOK_W wide of all changed regions
    stbi_write_png_to_func (wifiSTBWrite_helper, client, BLOK_W*n_bloks, BLOK_H,
                            COMP_RGB, chg_regns, BLOK_WBYTES*n_bloks);

    if (debugLevel (DEBUG_WEB, 2)) {
        struct timeval tv1;
        gettimeofday (&tv1, NULL);
        Serial.printf ("LIVE: client %s: write hdr %d bytes and update took %ld usec\n",
                        ws_getaddress(client), hdr_l, TVDELUS (tv0,tv1));
    }

    if (debugLevel (DEBUG_WEB, 2))
        Serial.printf ("LIVE: client %s: sent update with %d regions %d blocks\n",
                        ws_getaddress(client), n_regns, n_bloks);

    // finished with temps
    free (chg_regns);
    free (img_client);
    return_session_pixels (pixels);
}

/* capture fresh screen image for client and send.
 */
static void sendClientPNG (ws_cli_conn_t *client)
{
    uint8_t *pixels = borrow_session_pixels(client);
    if (!pixels)
        return;

    // fresh capture
    if (!tft.getRawPix (pixels, LIVE_NPIX))
        bye ("getRawPix for png failed\n");

    // convert image to and send as png
    stbi_write_png_compression_level = 2;       // faster with hardly any increase in size
    stbi_write_png_to_func (wifiSTBWrite_helper, client, BUILD_W, BUILD_H, COMP_RGB, pixels, LIVE_RBYTES);

    if (debugLevel (DEBUG_WEB, 1))
        Serial.printf ("LIVE: client %s: sent full PNG\n", ws_getaddress(client));

    return_session_pixels (pixels);
}

/* send message that user wants full screen.
 * N.B. coordinate with liveweb-html
 */
static void sendFullScreen(ws_cli_conn_t *client)
{
    ws_sendframe_txt (client, "full-screen");
}

/* send message to open a url.
 * N.B. coordinate with liveweb-html
 */
static void sendURL (ws_cli_conn_t *client, const char *url)
{
    StackMalloc opencmd_mem(strlen(url)+50);
    char *opencmd = (char *)opencmd_mem.getMem();
    snprintf (opencmd, opencmd_mem.getSize(), "open %s", url);
    ws_sendframe_txt (client, opencmd);
}

/* client running liveweb-html.cpp is asking for a complete screen capture as png file.
 */
static void getLivePNG (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)args;
    (void)args_len;

    sendClientPNG (client);
}

/* client running liveweb-html.cpp is asking for incremental screen update.
 * we might also:
 *   send a message to enable fullscreen once ready from setup.cpp. must be sent continuously
 *     because we can't tell when user reloaded their page;
 *   send a command to the browser to open liveweb_openurl
 *   timeout if reach inactivity limit
 */
static void getLiveUpdate (ws_cli_conn_t *client, char args[], size_t args_len)
{
    (void)args;
    (void)args_len;

    // inform we want full screen
    if (liveweb_fs_ready && getWebFullScreen())
        sendFullScreen (client);

    // check for pending openurl if this client did a touch
    pthread_mutex_lock (&lw_url_lock);
    if (lastest_ws_touch_client == client) {
        if (liveweb_openurl) {
            Serial.printf ("LIVE: sending URL %s\n", liveweb_openurl);
            sendURL (client, liveweb_openurl);
            free (liveweb_openurl);
            liveweb_openurl = NULL;
            lastest_ws_touch_client = NULL;
        }
    }
    pthread_mutex_unlock (&lw_url_lock);

    // close if time out
    if (liveweb_to) {
        // N.B. beware first connection before time is available
        time_t n = myNow();
        if (n > 0) {
            if (client->action_t == 0) {
                Serial.printf ("LIVE: reset session start to %ld\n", (long)n);
                client->action_t = n;
            }
            if (n - client->action_t > liveweb_to*60) {         // mins to secs
                Serial.printf ("LIVE: session timeout at %ld\n", (long)n);
                ws_sendframe_txt (client, "Session timed out"); // N.B. must match liveweb html
                ws_close_client (client);
                return;
            }
        }
    }

    // finally: what we came here for :-)
    updateExistingClient (client);
}

/* client running liveweb-html.cpp sending us a character to act on as if typed locally.
 */
static void setLiveChar (ws_cli_conn_t *client, char args[], size_t args_len)
{
    // ignore if this is from the r/o port
    if (client->port == liveweb_ro_port) {
        if (debugLevel (DEBUG_WEB, 1))
            Serial.printf ("LIVE: ignoring setLiveChar on r/o port %d\n", liveweb_ro_port);
        return;
    }

    // update action time
    client->action_t = myNow();

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "char";
    wa.name[wa.nargs++] = "mod";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_char garbled: %s\n", args);

    } else if (!wa.found[0]) {

        Serial.printf ("LIVE: set_char missing char\n");

    } else if (!wa.found[1]) {

        Serial.printf ("LIVE: set_char missing mod\n");

    } else {

        // decode char

        const char *str = wa.value[0];
        int strl = strlen(str);
        char c = 0;
        if (strl > 1) {
            // one of a named set
            if (strcmp (str, "Escape") == 0)
                c = CHAR_ESC;
            else if (strcmp (str, "Enter") == 0 || strcmp (str, "Return") == 0)
                c = CHAR_NL;
            else if (strcmp (str, "Tab") == 0)
                c = CHAR_TAB;
            else if (strcmp (str, "Delete") == 0)
                c = CHAR_DEL;
            else if (strcmp (str, "Backspace") == 0)
                c = CHAR_BS;
            else if (strcmp (str, "Space") == 0)
                c = CHAR_SPACE;
            else if (strcmp (str, "ArrowLeft") == 0)
                c = CHAR_LEFT;
            else if (strcmp (str, "ArrowDown") == 0)
                c = CHAR_DOWN;
            else if (strcmp (str, "ArrowUp") == 0)
                c = CHAR_UP;
            else if (strcmp (str, "ArrowRight") == 0)
                c = CHAR_RIGHT;
            else
                Serial.printf ("LIVE: Unknown char name: %s\n", str);
        } else {
            // literal
            if (strl != 1 || !isprint(str[0]))
                Serial.printf ("LIVE: Unknown char 0x%02X\n", str[0]);
            else
                c = str[0];
        }

        // decode mod
        bool ctrl = strchr (wa.value[1], 'C') != NULL;
        bool shift = strchr (wa.value[1], 'S') != NULL;

        if (c) {

            // insert into getChar queue
            tft.putChar (c, ctrl, shift);
            if (debugLevel (DEBUG_WEB, 1))
                Serial.printf ("LIVE: set_char %d %c\n", c, c);
        }
    }
}

/* client running liveweb-html.cpp sending us a touch event.
 */
static void setLiveTouch (ws_cli_conn_t *client, char args[], size_t args_len)
{
    // ignore if this is from the r/o port
    if (client->port == liveweb_ro_port) {
        if (debugLevel (DEBUG_WEB, 1))
            Serial.printf ("LIVE: ignoring setLiveTouch on r/o port %d\n", liveweb_ro_port);
        return;
    }

    // update action time
    client->action_t = myNow();

    // define all possible args
    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";
    wa.name[wa.nargs++] = "button";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_touch garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        Serial.printf ("LIVE: set_touch missing x,y\n");

    } else {

        // require x and y within screen size
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {
            Serial.printf ("LIVE: require 0 .. %d .. %d and 0 .. %d .. %d\n", x, tft.width()-1, y, tft.height()-1);

        } else {

            // inform checkTouch() to use wifi_tt_s; it will reset
            int button = wa.found[2] ? atoi (wa.value[2]) : 0;
            wifi_tt_s.x = x;
            wifi_tt_s.y = y;
            wifi_tt = button ? TT_TAP_BX : TT_TAP;              // 0 means button 1 -- go figure

            // record this client as the latest to do a touch
            pthread_mutex_lock (&lw_url_lock);
            lastest_ws_touch_client = client;
            pthread_mutex_unlock (&lw_url_lock);

            Serial.printf ("LIVE: set_touch %d %d with %d\n", wifi_tt_s.x, wifi_tt_s.y, button);
        }
    }
}

/* client running liveweb-html.cpp sending us a mouse motion event (buttons use set_touch()).
 */
static void setLiveMouse (ws_cli_conn_t *client, char args[], size_t args_len)
{
    // ignore if this is from the r/o port
    if (client->port == liveweb_ro_port) {
        if (debugLevel (DEBUG_WEB, 1))
            Serial.printf ("LIVE: ignoring setLiveMouse on r/o port %d\n", liveweb_ro_port);
        return;
    }

    WebArgs wa;
    wa.nargs = 0;
    wa.name[wa.nargs++] = "x";
    wa.name[wa.nargs++] = "y";

    // parse
    if (!parseWebCommand (wa, args, args_len)) {

        Serial.printf ("LIVE: set_mouse garbled: %s\n", args);

    } else if (!wa.found[0] || !wa.found[1]) {

        Serial.printf ("LIVE: set_mouse missing x,y\n");

    } else {

        // set
        int x = atoi(wa.value[0]);
        int y = atoi(wa.value[1]);
        tft.setMouse (x, y);
        if (debugLevel (DEBUG_WEB, 1))
            Serial.printf ("LIVE: set_mouse %d %d\n", x, y);
            
    }
}

/* send live.html to browser on the given FILE *
 */
static void sendLiveHTML (FILE *sockfp)
{
    // send proper header
    fprintf (sockfp, "HTTP/1.0 200 OK\r\n");
    fprintf (sockfp, "Content-Type: text/html; charset=us-ascii\r\n");
    fprintf (sockfp, "Connection: close\r\n\r\n");

    // send page
    fprintf (sockfp, "%s\r\n", live_html);

    if (debugLevel (DEBUG_WEB, 1))
        Serial.printf ("LIVE: sent live.html\n");
}

/* called when browser wants favicon.ico
 */
static void sendLiveFavicon (FILE *sockfp)
{
    // send the web page header
    fprintf (sockfp, "HTTP/1.0 200 OK\r\n");
    fprintf (sockfp, "Content-Type: image/x-icon\r\n");
    fprintf (sockfp, "Connection: close\r\n\r\n");

    // send icon
    writeFavicon (sockfp);

    if (debugLevel (DEBUG_WEB, 1))
        Serial.printf ("LIVE: sent favicon\n");
}

/* callback when browser asks for a new websocket connection.
 * assign a fresh si_list for keeping track its pixels.
 */
static void ws_onopen(ws_cli_conn_t *client)
{
    Serial.printf ("LIVE: client %s: new websocket request\n", ws_getaddress(client));

    // protect list while manipulating -- N.B. unlock before returning!
    pthread_mutex_lock (&si_lock);

    // scan for unused entry to reuse
    SessionInfo *new_sip = NULL;
    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (!sip->client && !sip->pixels && sip->pixel_users == 0 && !sip->pixels_retiring) {
            new_sip = sip;
            break;
        }
    }

    // grow list up to liveweb_max if none found for reuse.
    if (!new_sip) {
        if (si_n < liveweb_max) {
            si_list = (SessionInfo *) realloc (si_list, (si_n + 1) * sizeof(SessionInfo));
            if (!si_list)
                bye ("No memory for new live session info %d\n", si_n);
            new_sip = &si_list[si_n++];
            memset (new_sip, 0, sizeof (*new_sip));
        } else {
            Serial.printf ("LIVE: hit max %d connections\n", liveweb_max);
            ws_sendframe_txt (client, "Too many connections");  // N.B. must match liveweb html
            ws_close_client (client);
        }
    }

    // init including memory for pixels but don't capture until client asks for them
    if (new_sip) {
        new_sip->client = client;
        new_sip->pixels = (uint8_t *) malloc (LIVE_NBYTES);
        if (!new_sip->pixels)
            bye ("No memory for new live session pixels\n");

        // increment appropriate counter
        if (client->port == liveweb_ro_port) {
            n_roweb += 1;
            Serial.printf ("LIVE: new RO session %d from %s now running\n", n_roweb, ws_getaddress(client));
        } else {
            n_rwweb += 1;
            Serial.printf ("LIVE: new RW session %d from %s now running\n", n_rwweb, ws_getaddress(client));
        }

        // init user action time
        client->action_t = myNow();
    }

    // ok
    pthread_mutex_unlock (&si_lock);
}

/* callback when browser closes the given websocket connection.
 */
static void ws_onclose (ws_cli_conn_t *client)
{
    pthread_mutex_lock (&si_lock);

    // remove from si_list
    for (int i = 0; i < si_n; i++) {
        SessionInfo *sip = &si_list[i];
        if (sip->client == client) {
            sip->client = NULL;

            // recycle pixel memory
            if (sip->pixels) {
                if (sip->pixel_users > 0)
                    sip->pixels_retiring = true;
                else {
                    free (sip->pixels);
                    sip->pixels = NULL;
                }
            }

            // decrement appropriate counter
            if (client->port == liveweb_ro_port) {
                n_roweb -= 1;
                Serial.printf ("LIVE: RO client %s disconnected, now %d\n", ws_getaddress(client), n_roweb);
            } else {
                n_rwweb -= 1;
                Serial.printf ("LIVE: RW client %s disconnected, now %d\n", ws_getaddress(client), n_rwweb);
            }

            pthread_mutex_unlock (&si_lock);

            // discard stale touch/url state from the dead client
            pthread_mutex_lock (&lw_url_lock);
            if (lastest_ws_touch_client == client) {
                lastest_ws_touch_client = NULL;
                if (liveweb_openurl) {
                    free (liveweb_openurl);
                    liveweb_openurl = NULL;
                }
                if (wifi_tt != TT_NONE)
                    wifi_tt = TT_NONE;
            }
            pthread_mutex_unlock (&lw_url_lock);

            return;
        }
    }

    pthread_mutex_unlock (&si_lock);

    // if get here, client was not found in si_list -- usually just because closed because too many open
    // Serial.printf ("LIVE: client %s: disappeared after closing websocket\n", ws_getaddress(client));
}

/* callback when browser sends us a message on a websocket
 */
static void ws_onmessage (ws_cli_conn_t *client, const unsigned char *msg, uint64_t size, int type)
{
    // unused
    (void)type;

    // list of core commands
    static struct {
        const char *cmd_name;
        void (*cmd_fp) (ws_cli_conn_t *, char [], size_t);
    } commands[] = {
        // list in rough order of decreasing usage
        {"get_live.bin?", getLiveUpdate},
        {"set_mouse?",    setLiveMouse},
        {"set_touch?",    setLiveTouch},
        {"set_char?",     setLiveChar},
        {"get_live.png?", getLivePNG},
    };

    // msg as null-terminated string cmd
    StackMalloc cmd_mem(size+1);
    char *cmd = (char *) cmd_mem.getMem();
    memcpy (cmd, msg, size);
    cmd[size] = 0;

    // run the matching command, if any
    bool found = false;
    for (int i = 0; !found && i < NARRAY(commands); i++) {
        const char *cmd_name = commands[i].cmd_name;
        size_t cmd_name_len = strlen(cmd_name);
        if (strncmp (cmd, cmd_name, cmd_name_len) == 0) {
            if (debugLevel (DEBUG_WEB, 2))
                Serial.printf ("LIVE: running %s\n", cmd);
            char *args = cmd + cmd_name_len;
            (*commands[i].cmd_fp) (client, args, strlen(args));
            found = true;
        }
    }

    // check
    if (!found)
        Serial.printf ("LIVE: ignoring unknown ws command: %s\n", cmd);
}

/* called when we receive a "normal" client message, ie, one that is not from a web socket.
 * sockfp is prepared for writing and is positioned just after the header. caller will close, not us.
 */
static void ws_not (FILE *sockfp, const char *header)
{
    // first line should be the GET containing the desired page
    char fn[50];
    if (sscanf (header, "GET /%49s", fn) != 1) {
        Serial.printf ("LIVE: expecting GET http header but got: '%s'\n", header);
        return;
    }

    if (debugLevel (DEBUG_WEB, 1))
        Serial.printf ("LIVE: ws_not GET %s\n", fn);

    // dispatch according to GET file
    if (strcmp (fn, "live.html") == 0)
        sendLiveHTML (sockfp);
    else if (strcmp (fn, "favicon.ico") == 0)
        sendLiveFavicon (sockfp);
    else {
        Serial.printf ("LIVE: unknown GET %s\n", fn);
        fprintf (sockfp, "HTTP/1.0 400 Bad request\r\n");
        fprintf (sockfp, "Content-Type: text/plain; charset=us-ascii\r\n");
        fprintf (sockfp, "Connection: close\r\n\r\n");
        fprintf (sockfp, "%s: not found\r\n", fn);
    }
}

/* called in two ways:
 *   verbose false: really start, be silent if ok, else call bye if trouble
 *   verbose true:  already running so just call tftMsg with status similar to initWebServer().
 */
void initLiveWeb (bool verbose)
{
    if (verbose) {

        // just report

        if (liveweb_rw_port > 0)
            tftMsg (verbose, 0, "Live R/W Web port %d max %d", liveweb_rw_port, liveweb_max);
        else
            tftMsg (verbose, 0, "Live R/W Web server disabled");
        if (liveweb_ro_port > 0)
            tftMsg (verbose, 0, "Live R/O Web port %d max %d", liveweb_ro_port, liveweb_max);
        else
            tftMsg (verbose, 0, "Live R/O Web server disabled");

    } else {

        // insure liveweb_max < ws.h::MAX_CLIENTS to allow for error page
        if (liveweb_max >= MAX_CLIENTS)
            bye ("liveweb_max must be %d < %d\n", liveweb_max, MAX_CLIENTS);

        // handle all write errors inline
        signal (SIGPIPE, SIG_IGN);

        // actually start stuff unless not wanted

        // R/W service
        if (liveweb_rw_port < 0) {
            if (debugLevel (DEBUG_WEB, 1))
                Serial.printf ("LIVE: R/W live web is disabled\n");
        } else {
            struct ws_events evs;
            evs.onopen    = ws_onopen;
            evs.onclose   = ws_onclose;
            evs.onmessage = ws_onmessage;
            evs.onnonws   = ws_not;
            ws_socket (&evs, liveweb_rw_port, 1, 1000);
            if (debugLevel (DEBUG_WEB, 1))
                Serial.printf ("LIVE: started r/w server thread on port %d\n", liveweb_rw_port);
        }

        // R/O service
        if (liveweb_ro_port < 0) {
            if (debugLevel (DEBUG_WEB, 1))
                Serial.printf ("LIVE: R/O live web is disabled\n");
        } else {
            struct ws_events evs;
            evs.onopen    = ws_onopen;
            evs.onclose   = ws_onclose;
            evs.onmessage = ws_onmessage;
            evs.onnonws   = ws_not;
            ws_socket (&evs, liveweb_ro_port, 1, 1000);
            if (debugLevel (DEBUG_WEB, 1))
                Serial.printf ("LIVE: started r/o server thread on port %d\n", liveweb_ro_port);
        }

        // log timeout, if any
        if (liveweb_to > 0)
            Serial.printf ("LIVE: timeout set to %d minutes\n", liveweb_to);
    }
}

/* record desire to open the given url.
 * client to which it will be sent will be most recent to have done a set_touch.
 */
void openLiveWebURL (const char *url)
{
    // replace liveweb_openurl

    pthread_mutex_lock (&lw_url_lock);
    if (liveweb_openurl) {
        Serial.printf ("LIVE: discarded late URL %s\n", liveweb_openurl);
        free (liveweb_openurl);
    }
    liveweb_openurl = strdup (url);
    Serial.printf ("LIVE: live web staging URL %s\n", url);
    pthread_mutex_unlock (&lw_url_lock);
}

/* return whether there is a pending set_touch
 */
bool isLiveWebTouch (void)
{
    pthread_mutex_lock (&lw_url_lock);
    bool is_live_touch_pending = lastest_ws_touch_client != NULL;
    pthread_mutex_unlock (&lw_url_lock);
    return (is_live_touch_pending);
}
