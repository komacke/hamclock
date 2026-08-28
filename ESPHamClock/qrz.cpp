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

    // open
    Serial.printf ("QRZ: opening %s\n", url);
    openURL (url);
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
