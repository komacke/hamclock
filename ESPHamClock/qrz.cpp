/* handle opening a qrz-or-similar bio from a call
 */

#include "HamClock.h"


// table of labels and URL templates
#define X(a,b,c) {b,c},                 // expands QRZTABLE to each array initialization in {}
QRZURLTable qrz_urltable[QRZ_N] = {
    QRZTABLE
};
#undef X

void openQRZBio (const DXSpot &s)
{
    // get home call
    char home_call[NV_CALLSIGN_LEN];
    char dx_call[NV_CALLSIGN_LEN];
    splitCallSign (s.tx_call, home_call, dx_call);

    // get desired organization, if any
    QRZURLId qid = getQRZId();
    if (qid == QRZ_NONE) {
        Serial.printf ("QRZ: lookups are disabled\n");
        return;
    }

    // make upper-case copy of home call
    char call_uc[100];
    quietStrncpy (call_uc, home_call, sizeof(call_uc));
    strtoupper (call_uc);

    // replace keyword in url template with call
    const char *url_template = qrz_urltable[qid].url;
    if (!url_template) {
        Serial.printf ("QRZ: id %d has no template\n", qid);
        return;
    }
    const char *call_start = strstr (url_template, "WB0OEW");         // replace my call with call_uc
    if (!call_start) {
        Serial.printf ("QRZ: no keyword in template %d: %s\n", qid, url_template);
        return;
    }
    char url[200];
    snprintf (url, sizeof(url), "%.*s%s", (int)(call_start-url_template), url_template, call_uc);

    // open. all four providers were tested directly (a standalone <iframe> pointed at each): qrz.com
    // blocks framing outright (Firefox refuses to render it, showing its own error in the frame's
    // place), the other three don't -- so only qrz.com is held back from the Live Web embed.
    Serial.printf ("QRZ: opening %s\n", url);
    if (qid == QRZ_QRZ)
        openURLPopup (url);
    else
        openURLPopupEmbed (url);
}

/* like openURL(), but on the X11 desktop build, try to open the page as a chromeless Chromium
 * "app mode" popup positioned near the HamClock window, instead of a normal browser tab -- the
 * closest thing to an embedded browser achievable without linking a new dependency (CEF,
 * WebKitGTK, etc), since Chromium already ships by default on Raspberry Pi OS. Everywhere else
 * (fb0 -- no windowing layer to pop over; ESP32; Android; Live Web; or if no Chromium build is
 * found) this is the same as openURL().
 *
 * N.B. this launches a genuinely separate top-level window, just an undecorated, sized, and
 * positioned one -- there's no way to draw a second application's rendering inside HamClock's own
 * window without linking an embeddable browser engine. Live Web is the one place a true in-page
 * embed (an <iframe>) is possible, since that already runs inside a real browser tab -- but
 * whether it actually renders depends entirely on the target site's own X-Frame-Options/CSP
 * frame-ancestors policy: some sites (confirmed: Windy.com) block being framed by another site
 * outright, others (confirmed: ADS-B Exchange's globe map) allow it. Since that's a per-site
 * fact, not something generic, plain openURLPopup() never tries to embed -- see
 * openURLPopupEmbed() for the variant used by callers who've confirmed their target allows it.
 */
void openURLPopup (const char *url)
{
#if defined(_USE_X11)

    // Live Web already runs inside a real browser tab; just open a new one there, same as
    // openURL() -- see openURLPopupEmbed() if the target is known to allow framing
    if (isLiveWebTouch()) {
        openURL (url);
        return;
    }

    // find a Chromium build on PATH; try the common names in order
    static const char *chromium_names[] = { "chromium-browser", "chromium", "google-chrome" };
    const char *chromium = NULL;
    for (size_t i = 0; i < sizeof(chromium_names)/sizeof(chromium_names[0]); i++) {
        char which_cmd[100];
        snprintf (which_cmd, sizeof(which_cmd), "which %s >/dev/null 2>&1", chromium_names[i]);
        if (system (which_cmd) == 0) {
            chromium = chromium_names[i];
            break;
        }
    }

    if (chromium) {
        // default popup size, capped to something reasonable; position just inside the
        // HamClock window's own top-left corner if we can find it, else let the WM decide
        int popup_w = 900, popup_h = 650;
        int pos_x = -1, pos_y = -1;
        int wx, wy, ww, wh;
        if (tft.getWinScreenGeom (&wx, &wy, &ww, &wh)) {
            pos_x = wx + 40;
            pos_y = wy + 40;
        }

        StackMalloc cmd_mem (strlen(url) + strlen(chromium) + 150);
        char *cmd = (char *) cmd_mem.getMem();
        if (pos_x >= 0)
            snprintf (cmd, cmd_mem.getSize(),
                "%s --app=%s --window-size=%d,%d --window-position=%d,%d >/dev/null 2>&1 &",
                chromium, url, popup_w, popup_h, pos_x, pos_y);
        else
            snprintf (cmd, cmd_mem.getSize(),
                "%s --app=%s --window-size=%d,%d >/dev/null 2>&1 &", chromium, url, popup_w, popup_h);

        if ((system (cmd) >> 8) != 0)
            Serial.printf ("URL popup: fail: %s\n", cmd);
        else
            Serial.printf ("URL popup: ok: %s\n", cmd);
        return;
    }

    // no Chromium found -- fall back to the normal system browser
    openURL (url);

#else

    // fb0 has no windowing layer to pop a second window into; ESP32/Android have no
    // desktop-browser concept at all; Live Web is handled by openURLPopupEmbed() if the
    // caller wants embedding, else falls through to openURL() same as the X11 branch above
    openURL (url);

#endif
}

/* same as openURLPopup(), except a Live Web touch gets the in-page <iframe> overlay instead of a
 * plain new tab -- only call this for a target confirmed to allow being framed (see the N.B. on
 * openURLPopup() above; ADS-B Exchange's globe map is confirmed to allow it, Windy.com is
 * confirmed NOT to, which is why windbadge.cpp uses openURLPopup() instead of this).
 * on the X11 desktop build, local (non-Live-Web) touches still get the Chromium popup, same as
 * openURLPopup() -- that path was never affected by framing headers either way, since it opens a
 * genuine top-level window, not a frame.
 */
void openURLPopupEmbed (const char *url)
{
    if (isLiveWebTouch()) {
        openLiveWebURLEmbedded (url);
        return;
    }
    openURLPopup (url);
}

/* attempt to open the given web page in a new browser tab.
 *   if came from live web then open in its browser, else
 *   on macos: run open
 *   on ubuntu or RPi: sudo apt install xdg-utils
 *   on redhat: sudo yum install xdg-utils
 */
void openURL (const char *url)
{
    if (isLiveWebTouch()) {

        openLiveWebURL (url);

    } else {

#if defined (_IS_ANDROID)
        android_open_url (url);
#else
        StackMalloc cmd_mem(strlen(url) + 50);
        char *cmd = (char *) cmd_mem.getMem();
        #if defined (_IS_APPLE)
            snprintf (cmd, cmd_mem.getSize(), "open %s &", url);
        #else
            snprintf (cmd, cmd_mem.getSize(), "xdg-open %s &", url);
        #endif
        if ((system (cmd) >> 8) != 0)
            Serial.printf ("URL local: fail: %s\n", cmd);
        else
            Serial.printf ("URL local: ok: %s\n", cmd);
#endif
    }
}
